namespace GrimDawnTeleporter.Models;

public sealed class MemoryConfig
{
    public string ProcessName { get; set; } = "Grim Dawn";
    public string GameExePath { get; set; } = @"K:\SteamLibrary\steamapps\common\Grim Dawn\Grim Dawn.exe";
    public string GameExePathX86 { get; set; } = @"K:\SteamLibrary\steamapps\common\Grim Dawn\Grim Dawn.exe";
    public string GameExePathX64 { get; set; } = @"K:\SteamLibrary\steamapps\common\Grim Dawn\x64\Grim Dawn.exe";
    public string PreferredArchitecture { get; set; } = "Auto";
    public CoordinateAddressConfig CoordinateAddress { get; set; } = new();
    public CoordinateAddressConfig CoordinateAddressX86 { get; set; } = new();
    public CoordinateAddressConfig CoordinateAddressX64 { get; set; } = new() { ModuleName = "Grim Dawn.exe" };
}

public sealed class CoordinateAddressConfig
{
    public string ModuleName { get; set; } = "Grim Dawn.exe";
    public string BaseOffset { get; set; } = "0x0";
    public List<string> XOffsets { get; set; } = [];
    public List<string> YOffsets { get; set; } = [];
    public List<string> ZOffsets { get; set; } = [];
}
