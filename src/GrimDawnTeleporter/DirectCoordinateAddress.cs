namespace GrimDawnTeleporter;

public readonly record struct DirectCoordinateAddress(IntPtr XAddress, IntPtr YAddress, IntPtr ZAddress)
{
    public override string ToString() => $"X=0x{XAddress.ToInt64():X8}, Y=0x{YAddress.ToInt64():X8}, Z=0x{ZAddress.ToInt64():X8}";
}
