using System.Globalization;
using System.IO;
using System.Text.Json;
using GrimDawnTeleporter.Models;

namespace GrimDawnTeleporter;

public sealed class TeleportPointStore
{
    private readonly string _path;
    private readonly JsonSerializerOptions _jsonOptions = new() { WriteIndented = true };

    public TeleportPointStore(string path)
    {
        _path = path;
    }

    public List<TeleportPoint> Load()
    {
        return LoadFile().Points;
    }

    public TeleportPointFile LoadFile()
    {
        if (!File.Exists(_path))
        {
            var emptyFile = new TeleportPointFile();
            SaveFile(emptyFile);
            return emptyFile;
        }

        var json = File.ReadAllText(_path);
        return JsonSerializer.Deserialize<TeleportPointFile>(json) ?? new TeleportPointFile();
    }

    public void Save(IEnumerable<TeleportPoint> points)
    {
        SaveFile(new TeleportPointFile { Points = points.ToList() });
    }

    public void SaveFile(TeleportPointFile file)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(_path) ?? ".");
        File.WriteAllText(_path, JsonSerializer.Serialize(file, _jsonOptions));
    }

    public static List<TeleportPoint> ImportGrimInternals(string path)
    {
        var result = new List<TeleportPoint>();
        foreach (var line in File.ReadLines(path))
        {
            var trimmed = line.Trim();
            if (string.IsNullOrWhiteSpace(trimmed) || trimmed.StartsWith('#'))
            {
                continue;
            }

            var parts = trimmed.Split(',', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
            if (parts.Length < 4)
            {
                continue;
            }

            if (!float.TryParse(parts[1], NumberStyles.Float, CultureInfo.InvariantCulture, out var x) ||
                !float.TryParse(parts[2], NumberStyles.Float, CultureInfo.InvariantCulture, out var y) ||
                !float.TryParse(parts[3], NumberStyles.Float, CultureInfo.InvariantCulture, out var z))
            {
                continue;
            }

            result.Add(new TeleportPoint
            {
                Name = parts[0],
                X = x,
                Y = y,
                Z = z,
                Note = "从 Grim Internals 导入",
                CreatedAt = DateTime.Now
            });
        }

        return result;
    }

    public static void ExportGrimInternals(string path, IEnumerable<TeleportPoint> points)
    {
        var lines = points.Select(point => string.Format(CultureInfo.InvariantCulture,
            "{0}, {1:0.###}, {2:0.###}, {3:0.###},",
            point.Name,
            point.X,
            point.Y,
            point.Z));
        File.WriteAllLines(path, lines);
    }
}
