using System.Diagnostics;
using System.IO;
using System.Text.Json;
using GrimDawnTeleporter.Models;
using Microsoft.Win32;

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

        // 兼容性处理：配置文件里的游戏路径可能来自另一台机器（例如物理机用 K: 盘，
        // 虚拟机用 C:\Program Files (x86)\Steam）。路径不存在时自动定位本机游戏。
        if (!File.Exists(config.GameExePathX64) || !File.Exists(config.GameExePathX86))
        {
            var located = TryLocateGameExecutable();
            if (!string.IsNullOrWhiteSpace(located))
            {
                if (!File.Exists(config.GameExePathX64))
                {
                    config.GameExePathX64 = located;
                }

                var legacyPath = located.Replace(@"\x64\", @"\", StringComparison.OrdinalIgnoreCase);
                if (!File.Exists(config.GameExePathX86))
                {
                    config.GameExePathX86 = legacyPath;
                }

                if (string.IsNullOrWhiteSpace(config.GameExePath) || !File.Exists(config.GameExePath))
                {
                    config.GameExePath = legacyPath;
                }
            }
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

    /// <summary>
    /// 在本机寻找 Grim Dawn 主程序：优先使用正在运行的进程，其次是 Steam 安装目录与常见路径。
    /// </summary>
    private static string? TryLocateGameExecutable()
    {
        var candidates = new List<string>();

        // 1) 正在运行的游戏进程
        try
        {
            foreach (var process in Process.GetProcessesByName("Grim Dawn"))
            {
                try
                {
                    var path = process.MainModule?.FileName;
                    if (!string.IsNullOrWhiteSpace(path) && path.EndsWith("Grim Dawn.exe", StringComparison.OrdinalIgnoreCase))
                    {
                        candidates.Add(path);
                    }
                }
                catch (Exception)
                {
                }
                finally
                {
                    process.Dispose();
                }
            }
        }
        catch (Exception)
        {
        }

        // 2) Steam 注册表记录的安装位置
        try
        {
            var steamPath = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string
                ?? Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath", null) as string
                ?? Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Valve\Steam", "InstallPath", null) as string;
            if (!string.IsNullOrWhiteSpace(steamPath))
            {
                candidates.Add(Path.Combine(steamPath, "steamapps", "common", "Grim Dawn", "x64", "Grim Dawn.exe"));
            }
        }
        catch (Exception)
        {
        }

        // 3) 常见盘符与 Steam 库目录
        foreach (var drive in new[] { "C", "D", "E", "F", "G", "K", "Z" })
        {
            candidates.Add($@"{drive}:\Program Files (x86)\Steam\steamapps\common\Grim Dawn\x64\Grim Dawn.exe");
            candidates.Add($@"{drive}:\Steam\steamapps\common\Grim Dawn\x64\Grim Dawn.exe");
            candidates.Add($@"{drive}:\SteamLibrary\steamapps\common\Grim Dawn\x64\Grim Dawn.exe");
            candidates.Add($@"{drive}:\Games\SteamLibrary\steamapps\common\Grim Dawn\x64\Grim Dawn.exe");
            candidates.Add($@"{drive}:\GOG Games\Grim Dawn\x64\Grim Dawn.exe");
        }

        return candidates.FirstOrDefault(File.Exists);
    }

    private static bool IsConfigured(CoordinateAddressConfig config)
    {
        return config.BaseOffset != "0x0" && config.XOffsets.Count > 0 && config.YOffsets.Count > 0 && config.ZOffsets.Count > 0;
    }
}
