namespace DiabeticFoot.App.Preferences;

/// <summary>이번 실행 동안만 값을 둡니다. 처음에는 비어 있어 지난 실행의 값을 읽지 않습니다.</summary>
public sealed class InMemoryValueStore : IValueStore
{
    private readonly Dictionary<string, string> _values = new();

    public bool IsPersistent => false;

    public string? Get(string key) => _values.GetValueOrDefault(key);

    public void Set(string key, string value) => _values[key] = value;
}
