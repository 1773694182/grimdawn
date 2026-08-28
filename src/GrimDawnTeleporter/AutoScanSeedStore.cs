using System.IO;
using System.Text.Json;
using GrimDawnTeleporter.Models;

namespace GrimDawnTeleporter;

public sealed class AutoScanSeedStore
{
    private readonly string _path;
    private readonly JsonSerializerOptions _jsonOptions = new() { WriteIndented = true };

    public AutoScanSeedStore(string path)
    {
        _path = path;
    }

    public void Save(TeleportPoint point, float tolerance)
    {
        var file = new AutoScanSeedFile
        {
            Name = point.Name,
            X = point.X,
            Y = point.Y,
            Z = point.Z,
            Tolerance = tolerance,
            SavedAt = DateTime.Now
        };

        Directory.CreateDirectory(Path.GetDirectoryName(_path) ?? ".");
        File.WriteAllText(_path, JsonSerializer.Serialize(file, _jsonOptions));
    }

    public (TeleportPoint Point, float Tolerance)? TryLoad()
    {
        if (!File.Exists(_path))
        {
            return null;
        }

        var json = File.ReadAllText(_path);
        var file = JsonSerializer.Deserialize<AutoScanSeedFile>(json);
        if (file is null)
        {
            return null;
        }

        return (new TeleportPoint
        {
            Name = file.Name,
            X = file.X,
            Y = file.Y,
            Z = file.Z,
            Note = "自动扫描种子",
            CreatedAt = file.SavedAt
        }, file.Tolerance);
    }

    public void Clear()
    {
        if (File.Exists(_path))
        {
            File.Delete(_path);
        }
    }
}
