namespace GrimDawnTeleporter.Models;

public sealed class AutoScanSeedFile
{
    public string Name { get; set; } = string.Empty;
    public float X { get; set; }
    public float Y { get; set; }
    public float Z { get; set; }
    public float Tolerance { get; set; } = 0.75f;
    public DateTime SavedAt { get; set; } = DateTime.Now;
}
