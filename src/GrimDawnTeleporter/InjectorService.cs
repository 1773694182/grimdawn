using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;

namespace GrimDawnTeleporter;

public sealed class InjectorService
{
    private const int ProcessCreateThread = 0x0002;
    private const int ProcessQueryInformation = 0x0400;
    private const int ProcessVmOperation = 0x0008;
    private const int ProcessVmWrite = 0x0020;
    private const int ProcessVmRead = 0x0010;
    private const uint MemCommit = 0x1000;
    private const uint MemReserve = 0x2000;
    private const uint PageReadWrite = 0x04;

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(int desiredAccess, bool inheritHandle, int processId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr address, nuint size, uint allocationType, uint protect);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool WriteProcessMemory(IntPtr process, IntPtr baseAddress, byte[] buffer, int size, out int bytesWritten);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr threadAttributes, uint stackSize, IntPtr startAddress, IntPtr parameter, uint creationFlags, out uint threadId);

    [DllImport("ntdll.dll", SetLastError = true)]
    private static extern int NtCreateThreadEx(out IntPtr threadHandle, uint desiredAccess, IntPtr objectAttributes, IntPtr processHandle, IntPtr startRoutine, IntPtr argument, uint createFlags, nuint zeroBits, nuint stackSize, nuint maximumStackSize, IntPtr attributeList);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetExitCodeThread(IntPtr thread, out uint exitCode);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr GetModuleHandle(string moduleName);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
    private static extern IntPtr GetProcAddress(IntPtr module, string procName);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    public void Inject(Process process, string dllPath)
    {
        process.Refresh();
        if (process.HasExited)
        {
            throw new InvalidOperationException($"目标游戏进程已经退出：PID {process.Id}。");
        }

        if (!Environment.Is64BitProcess)
        {
            throw new InvalidOperationException("注入 x64 插件需要运行 x64 版本 Launcher。");
        }

        if (!IsRunningAsAdministrator())
        {
            throw new InvalidOperationException("注入插件需要管理员权限。请关闭 Launcher，然后右键以管理员身份运行。");
        }

        if (!File.Exists(dllPath))
        {
            throw new FileNotFoundException("找不到插件 DLL。", dllPath);
        }

        var fullPath = Path.GetFullPath(dllPath);
        var payload = Encoding.Unicode.GetBytes(fullPath + "\0");
        var handle = OpenProcess(ProcessCreateThread | ProcessQueryInformation | ProcessVmOperation | ProcessVmWrite | ProcessVmRead, false, process.Id);
        if (handle == IntPtr.Zero)
        {
            ThrowWin32("无法打开游戏进程用于注入。请确认 Launcher 已用管理员权限运行。");
        }

        try
        {
            var remoteMemory = VirtualAllocEx(handle, IntPtr.Zero, (nuint)payload.Length, MemCommit | MemReserve, PageReadWrite);
            if (remoteMemory == IntPtr.Zero)
            {
                ThrowWin32("远程内存分配失败。");
            }

            if (!WriteProcessMemory(handle, remoteMemory, payload, payload.Length, out var written) || written != payload.Length)
            {
                ThrowWin32("写入 DLL 路径失败。");
            }

            var localKernel32 = GetModuleHandle("kernel32.dll");
            if (localKernel32 == IntPtr.Zero)
            {
                ThrowWin32("无法定位 kernel32.dll。");
            }

            var localLoadLibrary = GetProcAddress(localKernel32, "LoadLibraryW");
            if (localLoadLibrary == IntPtr.Zero)
            {
                ThrowWin32("无法定位 LoadLibraryW。");
            }

            var localLoadLibraryModule = GetLocalModuleForAddress(localLoadLibrary)
                ?? throw new InvalidOperationException("无法确定 LoadLibraryW 所在模块。");
            var loadLibraryOffset = localLoadLibrary.ToInt64() - localLoadLibraryModule.BaseAddress.ToInt64();
            var remoteLoadLibraryModule = GetRemoteModuleBase(process, localLoadLibraryModule.ModuleName);
            if (remoteLoadLibraryModule == IntPtr.Zero)
            {
                throw new InvalidOperationException($"无法在游戏进程中定位 {localLoadLibraryModule.ModuleName}。");
            }

            var candidates = new List<(string Source, IntPtr Address)>
            {
                ($"{localLoadLibraryModule.ModuleName}+0x{loadLibraryOffset:X}", new IntPtr(remoteLoadLibraryModule.ToInt64() + loadLibraryOffset))
            };

            if (!string.Equals(localLoadLibraryModule.ModuleName, "kernel32.dll", StringComparison.OrdinalIgnoreCase))
            {
                var kernel32Offset = localLoadLibrary.ToInt64() - localKernel32.ToInt64();
                var remoteKernel32 = GetRemoteModuleBase(process, "kernel32.dll");
                if (remoteKernel32 != IntPtr.Zero && kernel32Offset >= 0)
                {
                    candidates.Add(($"kernel32.dll+0x{kernel32Offset:X}", new IntPtr(remoteKernel32.ToInt64() + kernel32Offset)));
                }
            }

            candidates.Add(("local LoadLibraryW address", localLoadLibrary));

            var thread = CreateRemoteThreadWithFallback(handle, remoteMemory, candidates);

            try
            {
                WaitForSingleObject(thread, 10000);
                if (GetExitCodeThread(thread, out var exitCode) && exitCode == 0)
                {
                    throw new InvalidOperationException($"LoadLibraryW 返回 0，插件可能未加载：{fullPath}");
                }
            }
            finally
            {
                CloseHandle(thread);
            }
        }
        finally
        {
            CloseHandle(handle);
        }
    }

    public string ResolvePluginPath()
    {
        var local = Path.Combine(AppContext.BaseDirectory, "GrimDawnTeleporter.Plugin.dll");
        if (File.Exists(local))
        {
            return local;
        }

        var dev = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "GrimDawnTeleporter.Plugin", "bin", "x64", "Release", "GrimDawnTeleporter.Plugin.dll"));
        return dev;
    }

    private static IntPtr CreateRemoteThreadWithFallback(IntPtr processHandle, IntPtr parameter, List<(string Source, IntPtr Address)> candidates)
    {
        var failures = new List<string>();
        foreach (var candidate in candidates.Where(candidate => candidate.Address != IntPtr.Zero).DistinctBy(candidate => candidate.Address))
        {
            var thread = CreateRemoteThread(processHandle, IntPtr.Zero, 0, candidate.Address, parameter, 0, out _);
            if (thread != IntPtr.Zero)
            {
                return thread;
            }

            var win32 = Marshal.GetLastWin32Error();
            failures.Add($"CreateRemoteThread {candidate.Source}=0x{candidate.Address.ToInt64():X}: Win32={win32}");

            var status = NtCreateThreadEx(out thread, 0x001FFFFF, IntPtr.Zero, processHandle, candidate.Address, parameter, 0, 0, 0, 0, IntPtr.Zero);
            if (thread != IntPtr.Zero && status >= 0)
            {
                return thread;
            }

            failures.Add($"NtCreateThreadEx {candidate.Source}=0x{candidate.Address.ToInt64():X}: NTSTATUS=0x{status:X8}");
            if (thread != IntPtr.Zero)
            {
                CloseHandle(thread);
            }
        }

        throw new InvalidOperationException($"创建远程线程失败。{string.Join("；", failures)}");
    }

    private static IntPtr GetRemoteModuleBase(Process process, string moduleName)
    {
        process.Refresh();
        foreach (ProcessModule module in process.Modules)
        {
            if (string.Equals(module.ModuleName, moduleName, StringComparison.OrdinalIgnoreCase))
            {
                return module.BaseAddress;
            }
        }

        return IntPtr.Zero;
    }

    private static ProcessModule? GetLocalModuleForAddress(IntPtr address)
    {
        foreach (ProcessModule module in Process.GetCurrentProcess().Modules)
        {
            var baseAddress = module.BaseAddress.ToInt64();
            var endAddress = baseAddress + module.ModuleMemorySize;
            var targetAddress = address.ToInt64();
            if (targetAddress >= baseAddress && targetAddress < endAddress)
            {
                return module;
            }
        }

        return null;
    }

    private static bool IsRunningAsAdministrator()
    {
        using var identity = WindowsIdentity.GetCurrent();
        var principal = new WindowsPrincipal(identity);
        return principal.IsInRole(WindowsBuiltInRole.Administrator);
    }

    private static void ThrowWin32(string message)
    {
        var error = Marshal.GetLastWin32Error();
        throw new Win32Exception(error, $"{message} Win32={error}。");
    }
}
