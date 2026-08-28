using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using GrimDawnTeleporter.Models;

namespace GrimDawnTeleporter;

public sealed class GameProcessService
{
    private readonly List<int> _startedProcessIds = [];

    [DllImport("kernel32.dll", SetLastError = true, CallingConvention = CallingConvention.Winapi)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool IsWow64Process(IntPtr process, out bool wow64Process);

    public GameProcessInfo? FindProcess(string processName, string preferredArchitecture)
    {
        var name = Path.GetFileNameWithoutExtension(processName);
        var processes = Process.GetProcessesByName(name)
            .Select(CreateProcessInfo)
            .Where(info => info is not null)
            .Cast<GameProcessInfo>()
            .OrderByDescending(info => info.Process.StartTime)
            .ToList();

        if (string.Equals(preferredArchitecture, "x86", StringComparison.OrdinalIgnoreCase))
        {
            return processes.FirstOrDefault(info => info.IsX86);
        }

        if (string.Equals(preferredArchitecture, "x64", StringComparison.OrdinalIgnoreCase))
        {
            return processes.FirstOrDefault(info => info.IsX64);
        }

        return processes.FirstOrDefault();
    }

    public Process StartGame(string exePath)
    {
        if (!File.Exists(exePath))
        {
            throw new FileNotFoundException("找不到游戏主程序。", exePath);
        }

        var startInfo = new ProcessStartInfo
        {
            FileName = exePath,
            WorkingDirectory = Path.GetDirectoryName(exePath) ?? string.Empty,
            UseShellExecute = true
        };

        var process = Process.Start(startInfo) ?? throw new InvalidOperationException("启动游戏失败。");
        _startedProcessIds.Add(process.Id);
        return process;
    }

    public void CloseStartedProcesses()
    {
        foreach (var processId in _startedProcessIds.Distinct().ToArray())
        {
            CloseProcessTree(processId);
        }

        _startedProcessIds.Clear();
    }

    private static void CloseProcessTree(int processId)
    {
        try
        {
            using var process = Process.GetProcessById(processId);
            if (process.HasExited)
            {
                return;
            }
        }
        catch (ArgumentException)
        {
            return;
        }

        try
        {
            using var taskkill = Process.Start(new ProcessStartInfo
            {
                FileName = "taskkill.exe",
                Arguments = $"/PID {processId} /T /F",
                CreateNoWindow = true,
                UseShellExecute = false
            });
            taskkill?.WaitForExit(5000);
        }
        catch (Win32Exception)
        {
        }
        catch (InvalidOperationException)
        {
        }
    }

    private static GameProcessInfo? CreateProcessInfo(Process process)
    {
        try
        {
            return new GameProcessInfo
            {
                Process = process,
                IsX86 = IsProcessX86(process)
            };
        }
        catch (Win32Exception)
        {
            return null;
        }
        catch (InvalidOperationException)
        {
            return null;
        }
    }

    public static bool IsProcessX86(Process process)
    {
        if (!Environment.Is64BitOperatingSystem)
        {
            return true;
        }

        if (!IsWow64Process(process.Handle, out var isWow64))
        {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }

        return isWow64;
    }
}
