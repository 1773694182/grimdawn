using System.Diagnostics;
using GrimDawnTeleporter.Models;

namespace GrimDawnTeleporter;

public sealed class PointerChainGenerator
{
    private static readonly string[] PreferredModules = ["Grim Dawn.exe", "Game.dll", "Engine.dll"];
    private readonly MemoryReader _reader;
    private readonly Process _process;

    public PointerChainGenerator(Process process, MemoryReader reader)
    {
        _process = process;
        _reader = reader;
    }

    public CoordinateAddressConfig Generate(DirectCoordinateAddress address, int maxDepth = 5, int maxOffset = 8192)
    {
        var chain = FindChain(address.XAddress, maxDepth, maxOffset)
            ?? throw new InvalidOperationException("未找到可用静态指针链。请在角色加载完成后重试，或提高扫描深度和偏移范围。");

        var xOffsets = chain.Offsets.Select(offset => ToHex(offset)).ToList();
        var yOffsets = chain.Offsets.Select(offset => ToHex(offset)).ToList();
        var zOffsets = chain.Offsets.Select(offset => ToHex(offset)).ToList();

        yOffsets[^1] = ToHex(chain.Offsets[^1] + 4);
        zOffsets[^1] = ToHex(chain.Offsets[^1] + 8);

        var config = new CoordinateAddressConfig
        {
            ModuleName = chain.ModuleName,
            BaseOffset = ToHex(chain.BaseOffset),
            XOffsets = xOffsets,
            YOffsets = yOffsets,
            ZOffsets = zOffsets
        };

        _ = _reader.ReadCoordinate(config);
        return config;
    }

    private PointerChain? FindChain(IntPtr targetAddress, int maxDepth, int maxOffset)
    {
        var modules = GetModules();
        var frontier = new List<PointerNode> { new(targetAddress, []) };

        for (var depth = 1; depth <= maxDepth; depth++)
        {
            var next = new List<PointerNode>();
            foreach (var node in frontier)
            {
                foreach (var reference in _reader.FindPointerReferences(node.Address, maxOffset, 1200))
                {
                    var offsets = new List<int>(node.OffsetsFromRoot.Count + 1) { reference.Offset };
                    offsets.AddRange(node.OffsetsFromRoot);

                    var module = modules.FirstOrDefault(item => item.Contains(reference.PointerAddress));
                    if (module is not null)
                    {
                        return new PointerChain(module.Name, reference.PointerAddress.ToInt64() - module.BaseAddress.ToInt64(), offsets);
                    }

                    if (next.Count < 2500)
                    {
                        next.Add(new PointerNode(reference.PointerAddress, offsets));
                    }
                }
            }

            frontier = next;
            if (frontier.Count == 0)
            {
                break;
            }
        }

        return null;
    }

    private List<ModuleRange> GetModules()
    {
        return _process.Modules.Cast<ProcessModule>()
            .Where(module => PreferredModules.Contains(module.ModuleName, StringComparer.OrdinalIgnoreCase))
            .Select(module => new ModuleRange(module.ModuleName, module.BaseAddress, module.ModuleMemorySize))
            .OrderBy(module => Array.FindIndex(PreferredModules, name => string.Equals(name, module.Name, StringComparison.OrdinalIgnoreCase)))
            .ToList();
    }

    private static string ToHex(long value) => $"0x{value:X}";

    private sealed record PointerNode(IntPtr Address, List<int> OffsetsFromRoot);

    private sealed record PointerChain(string ModuleName, long BaseOffset, List<int> Offsets);

    private sealed record ModuleRange(string Name, IntPtr BaseAddress, int Size)
    {
        public bool Contains(IntPtr address)
        {
            var value = address.ToInt64();
            var start = BaseAddress.ToInt64();
            return value >= start && value < start + Size;
        }
    }
}
