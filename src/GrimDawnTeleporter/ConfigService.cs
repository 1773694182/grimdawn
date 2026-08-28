using System.IO;
using System.Text.Json;
using GrimDawnTeleporter.Models;

namespace GrimDawnTeleporter;

public sealed class ConfigService
{
    private readonly JsonSerializerOptions _jsonOptions = new() { WriteIndented = true };

    public string DataDirectory { get; }
    public string MemoryConfigPath { get; }
    public string TeleportPointsPath { get; }
    public string SessionAddressPath { get; }
    public string AutoScanSeedPath { get; }

    public ConfigService()
    {
        DataDirectory = Path.Combine(AppContext.BaseDirectory, "data");
        MemoryConfigPath = Path.Combine(DataDirectory, "MemoryConfig.json");
        TeleportPointsPath = Path.Combine(DataDirectory, "TeleportPoints.json");
        SessionAddressPath = Path.Combine(DataDirectory, "SessionAddress.json");
        AutoScanSeedPath = Path.Combine(DataDirectory, "AutoScanSeed.json");
        Directory.CreateDirectory(DataDirectory);
    }

    public MemoryConfig LoadMemoryConfig()
    {
        if (!File.Exists(MemoryConfigPath))
        {
            var config = new MemoryConfig();
            SaveMemoryConfig(config);
            return config;
        }

        var json = File.ReadAllText(MemoryConfigPath);
        return Normalize(JsonSerializer.Deserialize<MemoryConfig>(json) ?? new MemoryConfig());
    }

    public void SaveMemoryConfig(MemoryConfig config)
    {
        File.WriteAllText(MemoryConfigPath, JsonSerializer.Serialize(config, _jsonOptions));
    }

    private static MemoryConfig Normalize(MemoryConfig config)
    {
        if (string.IsNullOrWhiteSpace(config.GameExePathX86))
        {
            config.GameExePathX86 = config.GameExePath;
        }

        if (string.IsNullOrWhiteSpace(config.GameExePathX64))
        {
            config.GameExePathX64 = @"K:\SteamLibrary\steamapps\common\Grim Dawn\x64\Grim Dawn.exe";
        }

        if (IsConfigured(config.CoordinateAddress) && !IsConfigured(config.CoordinateAddressX86))
        {
            config.CoordinateAddressX86 = config.CoordinateAddress;
        }

        if (string.IsNullOrWhiteSpace(config.PreferredArchitecture))
        {
            config.PreferredArchitecture = "Auto";
        }

        return config;
    }

    private static bool IsConfigured(CoordinateAddressConfig config)
    {
        return config.BaseOffset != "0x0" && config.XOffsets.Count > 0 && config.YOffsets.Count > 0 && config.ZOffsets.Count > 0;
    }
}
