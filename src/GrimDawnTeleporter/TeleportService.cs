using GrimDawnTeleporter.Models;

namespace GrimDawnTeleporter;

public sealed class TeleportService
{
    private readonly MemoryConfig _config;
    private readonly GameProcessService _processService;
    private readonly SessionAddressStore _sessionAddressStore;
    private readonly AutoScanSeedStore _autoScanSeedStore;
    private DateTime _lastTeleportAt = DateTime.MinValue;
    private DirectCoordinateAddress? _directAddress;

    public TeleportService(
        MemoryConfig config,
        GameProcessService processService,
        SessionAddressStore sessionAddressStore,
        AutoScanSeedStore autoScanSeedStore)
    {
        _config = config;
        _processService = processService;
        _sessionAddressStore = sessionAddressStore;
        _autoScanSeedStore = autoScanSeedStore;
    }

    public Coordinate3? LastPosition { get; private set; }

    public bool HasDirectAddress => _directAddress.HasValue;

    public bool TryRestoreSessionAddress()
    {
        var info = GetGameProcess();
        var address = _sessionAddressStore.TryLoadFor(info.Process);
        if (address is not { } directAddress)
        {
            return false;
        }

        using var reader = new MemoryReader(info.Process, info.PointerSize);
        _ = reader.ReadCoordinate(directAddress);
        _directAddress = directAddress;
        return true;
    }

    public GameProcessInfo GetGameProcess()
    {
        var info = _processService.FindProcess(_config.ProcessName, _config.PreferredArchitecture) ?? throw new InvalidOperationException("未找到 Grim Dawn 进程。");
        if (info.IsX64 && !Environment.Is64BitProcess)
        {
            throw new InvalidOperationException("检测到 x64 游戏进程。请使用 x64 版本 GrimDawnTeleporter.exe，或关闭 x64 游戏并启动 x86 游戏。");
        }

        return info;
    }

    public DirectCoordinateAddress? TryAutoScanFromSavedSeed()
    {
        var seed = _autoScanSeedStore.TryLoad();
        if (seed is not { } value)
        {
            return null;
        }

        try
        {
            return AutoScanFromKnownPoint(value.Point, value.Tolerance, saveSeed: false);
        }
        catch (InvalidOperationException)
        {
            return null;
        }
    }

    public Coordinate3 ReadCurrentCoordinate()
    {
        var info = GetGameProcess();
        using var reader = new MemoryReader(info.Process, info.PointerSize);
        if (_directAddress is { } directAddress)
        {
            return reader.ReadCoordinate(directAddress);
        }

        return reader.ReadCoordinate(GetCoordinateAddressConfig(info));
    }

    public DirectCoordinateAddress AutoScanFromKnownPoint(TeleportPoint point, float tolerance, bool saveSeed = true)
    {
        var info = GetGameProcess();
        using var reader = new MemoryReader(info.Process, info.PointerSize);
        var candidates = reader.ScanCoordinateAddresses(new Coordinate3(point.X, point.Y, point.Z), tolerance, 10);
        if (candidates.Count == 0)
        {
            throw new InvalidOperationException("未扫描到匹配坐标。请确认角色当前正站在选中传送点附近，或适当增大容差。");
        }

        var address = candidates[0];
        _directAddress = address;
        _sessionAddressStore.Save(info.Process, address);
        if (saveSeed)
        {
            _autoScanSeedStore.Save(point, tolerance);
        }

        return address;
    }

    public List<UnknownCoordinateCandidate> ScanUnknownCoordinateCandidates()
    {
        var info = GetGameProcess();
        using var reader = new MemoryReader(info.Process, info.PointerSize);
        return reader.ScanUnknownCoordinateCandidates();
    }

    public List<UnknownCoordinateCandidate> RefreshUnknownCoordinateCandidates(IEnumerable<UnknownCoordinateCandidate> candidates)
    {
        var info = GetGameProcess();
        using var reader = new MemoryReader(info.Process, info.PointerSize);
        return reader.ReadUnknownCoordinateCandidates(candidates);
    }

    public Coordinate3 UseUnknownCoordinateCandidate(UnknownCoordinateCandidate candidate)
    {
        var info = GetGameProcess();
        using var reader = new MemoryReader(info.Process, info.PointerSize);
        _directAddress = candidate.Address;
        _sessionAddressStore.Save(info.Process, candidate.Address);
        return reader.ReadCoordinate(candidate.Address);
    }

    public CoordinateAddressConfig GeneratePointerChainConfig()
    {
        if (_directAddress is not { } directAddress)
        {
            throw new InvalidOperationException("请先执行自动扫描坐标，确认本次会话动态地址可用后再生成指针链。");
        }

        var info = GetGameProcess();
        using var reader = new MemoryReader(info.Process, info.PointerSize);
        var generator = new PointerChainGenerator(info.Process, reader);
        var config = generator.Generate(directAddress);
        SetCoordinateAddressConfig(info, config);
        return config;
    }

    public void ClearDirectAddress()
    {
        _directAddress = null;
        _sessionAddressStore.Clear();
        _autoScanSeedStore.Clear();
    }

    public void TeleportTo(TeleportPoint point)
    {
        if ((DateTime.Now - _lastTeleportAt).TotalSeconds < 1)
        {
            throw new InvalidOperationException("传送过于频繁，请稍后再试。");
        }

        var info = GetGameProcess();
        using var reader = new MemoryReader(info.Process, info.PointerSize);
        if (_directAddress is { } directAddress)
        {
            LastPosition = reader.ReadCoordinate(directAddress);
            reader.WriteCoordinate(directAddress, new Coordinate3(point.X, point.Y, point.Z));
        }
        else
        {
            var config = GetCoordinateAddressConfig(info);
            LastPosition = reader.ReadCoordinate(config);
            reader.WriteCoordinate(config, new Coordinate3(point.X, point.Y, point.Z));
        }

        _lastTeleportAt = DateTime.Now;
    }

    public void ReturnToLastPosition()
    {
        if (LastPosition is not { } coordinate)
        {
            throw new InvalidOperationException("还没有可返回的上一个位置。");
        }

        var info = GetGameProcess();
        using var reader = new MemoryReader(info.Process, info.PointerSize);
        if (_directAddress is { } directAddress)
        {
            reader.WriteCoordinate(directAddress, coordinate);
        }
        else
        {
            reader.WriteCoordinate(GetCoordinateAddressConfig(info), coordinate);
        }

        _lastTeleportAt = DateTime.Now;
    }

    private CoordinateAddressConfig GetCoordinateAddressConfig(GameProcessInfo info)
    {
        if (info.IsX64)
        {
            return _config.CoordinateAddressX64;
        }

        return IsConfigured(_config.CoordinateAddressX86) ? _config.CoordinateAddressX86 : _config.CoordinateAddress;
    }

    private void SetCoordinateAddressConfig(GameProcessInfo info, CoordinateAddressConfig config)
    {
        if (info.IsX64)
        {
            _config.CoordinateAddressX64 = config;
        }
        else
        {
            _config.CoordinateAddressX86 = config;
            _config.CoordinateAddress = config;
        }
    }

    private static bool IsConfigured(CoordinateAddressConfig config)
    {
        return config.BaseOffset != "0x0" && config.XOffsets.Count > 0 && config.YOffsets.Count > 0 && config.ZOffsets.Count > 0;
    }
}
