using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using GrimDawnTeleporter.Models;

namespace GrimDawnTeleporter;

public sealed class MemoryReader : IDisposable
{
    private const int ProcessVmRead = 0x0010;
    private const int ProcessVmWrite = 0x0020;
    private const int ProcessVmOperation = 0x0008;
    private const int ProcessQueryInformation = 0x0400;
    private const uint MemCommit = 0x1000;
    private const uint PageNoAccess = 0x01;
    private const uint PageGuard = 0x100;
    private const uint PageReadWrite = 0x04;
    private const uint PageWriteCopy = 0x08;
    private const uint PageExecuteReadWrite = 0x40;
    private const uint PageExecuteWriteCopy = 0x80;
    private const uint MemPrivate = 0x20000;
    private const int MaxRegionReadSize = 16 * 1024 * 1024;

    private readonly Process _process;
    private readonly IntPtr _handle;
    private readonly int _pointerSize;

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(int desiredAccess, bool inheritHandle, int processId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool ReadProcessMemory(IntPtr process, IntPtr baseAddress, byte[] buffer, int size, out int bytesRead);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool WriteProcessMemory(IntPtr process, IntPtr baseAddress, byte[] buffer, int size, out int bytesWritten);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern UIntPtr VirtualQueryEx(IntPtr process, IntPtr address, out MemoryBasicInformation buffer, UIntPtr length);

    [StructLayout(LayoutKind.Sequential)]
    private struct MemoryBasicInformation
    {
        public IntPtr BaseAddress;
        public IntPtr AllocationBase;
        public uint AllocationProtect;
        public IntPtr RegionSize;
        public uint State;
        public uint Protect;
        public uint Type;
    }

    public MemoryReader(Process process, int pointerSize = 4)
    {
        _process = process;
        _pointerSize = pointerSize;
        _handle = OpenProcess(ProcessVmRead | ProcessVmWrite | ProcessVmOperation | ProcessQueryInformation, false, process.Id);
        if (_handle == IntPtr.Zero)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "无法打开游戏进程。请尝试用管理员权限运行工具。");
        }
    }

    public Coordinate3 ReadCoordinate(CoordinateAddressConfig config)
    {
        ValidateConfig(config);
        return new Coordinate3(
            ReadFloat(ResolveAddress(config.ModuleName, config.BaseOffset, config.XOffsets)),
            ReadFloat(ResolveAddress(config.ModuleName, config.BaseOffset, config.YOffsets)),
            ReadFloat(ResolveAddress(config.ModuleName, config.BaseOffset, config.ZOffsets)));
    }

    public void WriteCoordinate(CoordinateAddressConfig config, Coordinate3 coordinate)
    {
        ValidateConfig(config);
        WriteFloat(ResolveAddress(config.ModuleName, config.BaseOffset, config.XOffsets), coordinate.X);
        WriteFloat(ResolveAddress(config.ModuleName, config.BaseOffset, config.YOffsets), coordinate.Y);
        WriteFloat(ResolveAddress(config.ModuleName, config.BaseOffset, config.ZOffsets), coordinate.Z);
    }

    public Coordinate3 ReadCoordinate(DirectCoordinateAddress address)
    {
        return new Coordinate3(
            ReadFloat(address.XAddress),
            ReadFloat(address.YAddress),
            ReadFloat(address.ZAddress));
    }

    public void WriteCoordinate(DirectCoordinateAddress address, Coordinate3 coordinate)
    {
        WriteFloat(address.XAddress, coordinate.X);
        WriteFloat(address.YAddress, coordinate.Y);
        WriteFloat(address.ZAddress, coordinate.Z);
    }

    public List<DirectCoordinateAddress> ScanCoordinateAddresses(Coordinate3 target, float tolerance, int maxResults = 20)
    {
        if (tolerance <= 0)
        {
            throw new ArgumentOutOfRangeException(nameof(tolerance), "容差必须大于 0。");
        }

        var results = new List<DirectCoordinateAddress>();
        var address = IntPtr.Zero;
        var mbiSize = (UIntPtr)Marshal.SizeOf<MemoryBasicInformation>();

        while (VirtualQueryEx(_handle, address, out var info, mbiSize) != UIntPtr.Zero)
        {
            var regionSize = info.RegionSize.ToInt64();
            if (regionSize > 0 && IsReadable(info))
            {
                ScanRegion(info.BaseAddress, regionSize, target, tolerance, maxResults, results);
                if (results.Count >= maxResults)
                {
                    break;
                }
            }

            var next = info.BaseAddress.ToInt64() + Math.Max(regionSize, 0x1000);
            if (next <= address.ToInt64() || next < 0)
            {
                break;
            }

            address = new IntPtr(next);
        }

        return results;
    }

    public List<UnknownCoordinateCandidate> ScanUnknownCoordinateCandidates(int maxResults = 100000)
    {
        var results = new List<UnknownCoordinateCandidate>();
        var address = IntPtr.Zero;
        var mbiSize = (UIntPtr)Marshal.SizeOf<MemoryBasicInformation>();

        while (VirtualQueryEx(_handle, address, out var info, mbiSize) != UIntPtr.Zero)
        {
            var regionSize = info.RegionSize.ToInt64();
            if (regionSize > 0 && IsWritablePrivate(info))
            {
                ScanUnknownRegion(info.BaseAddress, regionSize, maxResults, results);
                if (results.Count >= maxResults)
                {
                    break;
                }
            }

            var next = info.BaseAddress.ToInt64() + Math.Max(regionSize, 0x1000);
            if (next <= address.ToInt64() || next < 0)
            {
                break;
            }

            address = new IntPtr(next);
        }

        return results;
    }

    public List<UnknownCoordinateCandidate> ReadUnknownCoordinateCandidates(IEnumerable<UnknownCoordinateCandidate> candidates)
    {
        var results = new List<UnknownCoordinateCandidate>();
        foreach (var candidate in candidates)
        {
            try
            {
                var value = ReadCoordinate(candidate.Address);
                if (IsReasonableCoordinate(value.X) && IsReasonableCoordinate(value.Y) && IsReasonableCoordinate(value.Z))
                {
                    results.Add(candidate with { Value = value });
                }
            }
            catch (Win32Exception)
            {
            }
        }

        return results;
    }

    public List<PointerReference> FindPointerReferences(IntPtr targetAddress, int maxOffset, int maxResults)
    {
        var results = new List<PointerReference>();
        var target = targetAddress.ToInt64();
        var address = IntPtr.Zero;
        var mbiSize = (UIntPtr)Marshal.SizeOf<MemoryBasicInformation>();

        while (VirtualQueryEx(_handle, address, out var info, mbiSize) != UIntPtr.Zero)
        {
            var regionSize = info.RegionSize.ToInt64();
            if (regionSize > 0 && IsReadable(info))
            {
                ScanPointerRegion(info.BaseAddress, regionSize, target, maxOffset, maxResults, results);
                if (results.Count >= maxResults)
                {
                    break;
                }
            }

            var next = info.BaseAddress.ToInt64() + Math.Max(regionSize, 0x1000);
            if (next <= address.ToInt64() || next < 0)
            {
                break;
            }

            address = new IntPtr(next);
        }

        return results;
    }

    private void ScanRegion(IntPtr baseAddress, long regionSize, Coordinate3 target, float tolerance, int maxResults, List<DirectCoordinateAddress> results)
    {
        for (long offset = 0; offset < regionSize && results.Count < maxResults; offset += MaxRegionReadSize)
        {
            var size = (int)Math.Min(MaxRegionReadSize, regionSize - offset);
            if (size < 12)
            {
                continue;
            }

            var currentAddress = new IntPtr(baseAddress.ToInt64() + offset);
            var buffer = new byte[size];
            if (!ReadProcessMemory(_handle, currentAddress, buffer, buffer.Length, out var read) || read < 12)
            {
                continue;
            }

            for (var i = 0; i <= read - 12 && results.Count < maxResults; i += 4)
            {
                var x = BitConverter.ToSingle(buffer, i);
                var y = BitConverter.ToSingle(buffer, i + 4);
                var z = BitConverter.ToSingle(buffer, i + 8);
                if (IsNear(x, target.X, tolerance) && IsNear(y, target.Y, tolerance) && IsNear(z, target.Z, tolerance))
                {
                    var xAddress = IntPtr.Add(currentAddress, i);
                    results.Add(new DirectCoordinateAddress(xAddress, IntPtr.Add(xAddress, 4), IntPtr.Add(xAddress, 8)));
                }
            }
        }
    }

    private void ScanPointerRegion(IntPtr baseAddress, long regionSize, long target, int maxOffset, int maxResults, List<PointerReference> results)
    {
        for (long offset = 0; offset < regionSize && results.Count < maxResults; offset += MaxRegionReadSize)
        {
            var size = (int)Math.Min(MaxRegionReadSize, regionSize - offset);
            if (size < _pointerSize)
            {
                continue;
            }

            var currentAddress = new IntPtr(baseAddress.ToInt64() + offset);
            var buffer = new byte[size];
            if (!ReadProcessMemory(_handle, currentAddress, buffer, buffer.Length, out var read) || read < _pointerSize)
            {
                continue;
            }

            for (var i = 0; i <= read - _pointerSize && results.Count < maxResults; i += _pointerSize)
            {
                var pointerValue = _pointerSize == 8 ? BitConverter.ToInt64(buffer, i) : BitConverter.ToUInt32(buffer, i);
                var pointerOffset = target - pointerValue;
                if (pointerOffset >= 0 && pointerOffset <= maxOffset && pointerOffset % 4 == 0)
                {
                    results.Add(new PointerReference(IntPtr.Add(currentAddress, i), checked((int)pointerOffset)));
                }
            }
        }
    }

    private void ScanUnknownRegion(IntPtr baseAddress, long regionSize, int maxResults, List<UnknownCoordinateCandidate> results)
    {
        for (long offset = 0; offset < regionSize && results.Count < maxResults; offset += MaxRegionReadSize)
        {
            var size = (int)Math.Min(MaxRegionReadSize, regionSize - offset);
            if (size < 12)
            {
                continue;
            }

            var currentAddress = new IntPtr(baseAddress.ToInt64() + offset);
            var buffer = new byte[size];
            if (!ReadProcessMemory(_handle, currentAddress, buffer, buffer.Length, out var read) || read < 12)
            {
                continue;
            }

            for (var i = 0; i <= read - 12 && results.Count < maxResults; i += 4)
            {
                var x = BitConverter.ToSingle(buffer, i);
                var y = BitConverter.ToSingle(buffer, i + 4);
                var z = BitConverter.ToSingle(buffer, i + 8);
                if (IsReasonableCoordinate(x) && IsReasonableCoordinate(y) && IsReasonableCoordinate(z))
                {
                    var xAddress = IntPtr.Add(currentAddress, i);
                    results.Add(new UnknownCoordinateCandidate(
                        new DirectCoordinateAddress(xAddress, IntPtr.Add(xAddress, 4), IntPtr.Add(xAddress, 8)),
                        new Coordinate3(x, y, z)));
                }
            }
        }
    }

    private static bool IsReadable(MemoryBasicInformation info)
    {
        return info.State == MemCommit && (info.Protect & PageNoAccess) == 0 && (info.Protect & PageGuard) == 0;
    }

    private static bool IsWritablePrivate(MemoryBasicInformation info)
    {
        var writable = (info.Protect & (PageReadWrite | PageWriteCopy | PageExecuteReadWrite | PageExecuteWriteCopy)) != 0;
        return IsReadable(info) && info.Type == MemPrivate && writable;
    }

    private static bool IsNear(float actual, float expected, float tolerance)
    {
        return !float.IsNaN(actual) && !float.IsInfinity(actual) && Math.Abs(actual - expected) <= tolerance;
    }

    private static bool IsReasonableCoordinate(float value)
    {
        return !float.IsNaN(value) && !float.IsInfinity(value) && value > -200000 && value < 200000;
    }

    private static void ValidateConfig(CoordinateAddressConfig config)
    {
        if (ParseOffset(config.BaseOffset) == 0 || config.XOffsets.Count == 0 || config.YOffsets.Count == 0 || config.ZOffsets.Count == 0)
        {
            throw new InvalidOperationException("尚未配置坐标内存地址。请先完成当前游戏架构的坐标指针链验证，并更新 data/MemoryConfig.json。");
        }
    }

    private IntPtr ResolveAddress(string moduleName, string baseOffset, IReadOnlyList<string> offsets)
    {
        var module = _process.Modules.Cast<ProcessModule>()
            .FirstOrDefault(item => string.Equals(item.ModuleName, moduleName, StringComparison.OrdinalIgnoreCase));

        if (module is null)
        {
            throw new InvalidOperationException($"找不到模块：{moduleName}");
        }

        var address = ReadPointer(IntPtr.Add(module.BaseAddress, ParseOffset(baseOffset)));
        for (var i = 0; i < offsets.Count; i++)
        {
            var offset = ParseOffset(offsets[i]);
            address = IntPtr.Add(address, offset);
            if (i == offsets.Count - 1)
            {
                return address;
            }

            address = ReadPointer(address);
        }

        return address;
    }

    private IntPtr ReadPointer(IntPtr address)
    {
        var buffer = ReadBytes(address, _pointerSize);
        return _pointerSize == 8 ? new IntPtr(BitConverter.ToInt64(buffer, 0)) : new IntPtr(BitConverter.ToInt32(buffer, 0));
    }

    private float ReadFloat(IntPtr address)
    {
        var buffer = ReadBytes(address, 4);
        return BitConverter.ToSingle(buffer, 0);
    }

    private void WriteFloat(IntPtr address, float value)
    {
        var buffer = BitConverter.GetBytes(value);
        if (!WriteProcessMemory(_handle, address, buffer, buffer.Length, out var written) || written != buffer.Length)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "写入游戏内存失败。");
        }
    }

    private byte[] ReadBytes(IntPtr address, int size)
    {
        var buffer = new byte[size];
        if (!ReadProcessMemory(_handle, address, buffer, size, out var read) || read != size)
        {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "读取游戏内存失败。");
        }

        return buffer;
    }

    private static int ParseOffset(string value)
    {
        var text = value.Trim();
        if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            return int.Parse(text[2..], NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        }

        return int.Parse(text, CultureInfo.InvariantCulture);
    }

    public void Dispose()
    {
        if (_handle != IntPtr.Zero)
        {
            CloseHandle(_handle);
        }
    }
}

public readonly record struct PointerReference(IntPtr PointerAddress, int Offset);
