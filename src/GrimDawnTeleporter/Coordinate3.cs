namespace GrimDawnTeleporter;

public readonly record struct Coordinate3(float X, float Y, float Z)
{
    public override string ToString() => $"{X:0.###}, {Y:0.###}, {Z:0.###}";

    public bool IsNear(Coordinate3 other, float tolerance)
    {
        return Math.Abs(X - other.X) <= tolerance && Math.Abs(Y - other.Y) <= tolerance && Math.Abs(Z - other.Z) <= tolerance;
    }

    public float DistanceTo(Coordinate3 other)
    {
        var dx = X - other.X;
        var dy = Y - other.Y;
        var dz = Z - other.Z;
        return MathF.Sqrt(dx * dx + dy * dy + dz * dz);
    }
}
