using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text.Json;
using GrimDawnTeleporter.Models;

namespace GrimDawnTeleporter;

public sealed class SessionAddressStore
{
    private readonly string _path;
    private readonly JsonSerializerOptions _jsonOptions = new() { WriteIndented = true };

    public SessionAddressStore(string path)
    {
        _path = path;
    }

    public void Save(Process process, DirectCoordinateAddress address)
    {
        var file = new SessionAddressFile
        {
            ProcessId = process.Id,
            ProcessStartTimeTicks = process.StartTime.Ticks,
            XAddress = ToHex(address.XAddress),
            YAddress = ToHex(address.YAddress),
            ZAddress = ToHex(address.ZAddress),
            SavedAt = DateTime.Now
        };

        Directory.CreateDirectory(Path.GetDirectoryName(_path) ?? ".");
        File.WriteAllText(_path, JsonSerializer.Serialize(file, _jsonOptions));
    }

    public DirectCoordinateAddress? TryLoadFor(Process process)
    {
        if (!File.Exists(_path))
        {
            return null;
        }

        var json = File.ReadAllText(_path);
        var file = JsonSerializer.Deserialize<SessionAddressFile>(json);
        if (file is null || file.ProcessId != process.Id || file.ProcessStartTimeTicks != process.StartTime.Ticks)
        {
            return null;
        }

        return new DirectCoordinateAddress(ParseHex(file.XAddress), ParseHex(file.YAddress), ParseHex(file.ZAddress));
    }

    public void Clear()
    {
        if (File.Exists(_path))
        {
            File.Delete(_path);
        }
    }

    private static string ToHex(IntPtr address) => $"0x{address.ToInt64():X}";

    private static IntPtr ParseHex(string value)
    {
        var text = value.Trim();
        if (text.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
        {
            text = text[2..];
        }

        return new IntPtr(long.Parse(text, NumberStyles.HexNumber, CultureInfo.InvariantCulture));
    }
}
