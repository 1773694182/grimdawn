using System.Diagnostics;

namespace GrimDawnTeleporter.Models;

public sealed class GameProcessInfo
{
    public required Process Process { get; init; }
    public required bool IsX86 { get; init; }
    public bool IsX64 => !IsX86;
    public int PointerSize => IsX86 ? 4 : 8;
    public string Architecture => IsX86 ? "x86" : "x64";
    public string DisplayName => $"{Process.ProcessName} {Architecture} (PID {Process.Id})";
}
