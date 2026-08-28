namespace GrimDawnTeleporter;

public sealed class UnknownCoordinateScanner
{
    private readonly List<UnknownCoordinateCandidate> _candidates = [];

    public IReadOnlyList<UnknownCoordinateCandidate> Candidates => _candidates;

    public void Reset(IEnumerable<UnknownCoordinateCandidate> candidates)
    {
        _candidates.Clear();
        _candidates.AddRange(candidates);
    }

    public void KeepUnchanged(IEnumerable<UnknownCoordinateCandidate> candidates, float tolerance)
    {
        Filter(candidates, (oldValue, newValue) => oldValue.IsNear(newValue, tolerance));
    }

    public void KeepChanged(IEnumerable<UnknownCoordinateCandidate> candidates, float minDelta)
    {
        Filter(candidates, (oldValue, newValue) => oldValue.DistanceTo(newValue) >= minDelta, preserveWhenEmpty: true);
    }

    private void Filter(IEnumerable<UnknownCoordinateCandidate> candidates, Func<Coordinate3, Coordinate3, bool> predicate, bool preserveWhenEmpty = false)
    {
        var latest = candidates.ToDictionary(candidate => candidate.Address.XAddress.ToInt64());
        var filtered = new List<UnknownCoordinateCandidate>();

        foreach (var current in _candidates)
        {
            if (latest.TryGetValue(current.Address.XAddress.ToInt64(), out var newCandidate) && predicate(current.Value, newCandidate.Value))
            {
                filtered.Add(newCandidate);
            }
        }

        if (preserveWhenEmpty && filtered.Count == 0)
        {
            return;
        }

        _candidates.Clear();
        _candidates.AddRange(filtered);
    }
}

public readonly record struct UnknownCoordinateCandidate(DirectCoordinateAddress Address, Coordinate3 Value);
