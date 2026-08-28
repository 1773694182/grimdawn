namespace GrimDawnTeleporter.Models;

public sealed class TeleportPoint
{
    public const string UngroupedName = "未分组";

    public string Name { get; set; } = string.Empty;
    public float X { get; set; }
    public float Y { get; set; }
    public float Z { get; set; }
    public string Group { get; set; } = string.Empty;
    public string Area { get; set; } = "campaign";
    public string Note { get; set; } = string.Empty;
    public DateTime CreatedAt { get; set; } = DateTime.Now;

    public string GroupDisplayName => string.IsNullOrWhiteSpace(Group) ? UngroupedName : Group.Trim();
    public string CoordinateText => $"{X:0.###}, {Y:0.###}, {Z:0.###}";
}
