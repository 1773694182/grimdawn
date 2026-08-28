namespace GrimDawnTeleporter.Models;

public sealed class SessionAddressFile
{
    public int ProcessId { get; set; }
    public long ProcessStartTimeTicks { get; set; }
    public string XAddress { get; set; } = string.Empty;
    public string YAddress { get; set; } = string.Empty;
    public string ZAddress { get; set; } = string.Empty;
    public DateTime SavedAt { get; set; } = DateTime.Now;
}
