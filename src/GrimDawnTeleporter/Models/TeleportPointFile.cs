namespace GrimDawnTeleporter.Models;

public sealed class TeleportPointFile
{
    public string Game { get; set; } = "Grim Dawn";
    public string Mode { get; set; } = "x86/x64";
    public List<string> Groups { get; set; } = [];
    public List<TeleportPoint> Points { get; set; } = [];
}
