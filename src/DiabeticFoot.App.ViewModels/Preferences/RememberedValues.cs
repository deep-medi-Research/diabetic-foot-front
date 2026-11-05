using System.Globalization;
using DiabeticFoot.Core.Pixel;

namespace DiabeticFoot.App.Preferences;

/// <summary>
/// 화면에서 고른 값 중 다음 실행이 이어 쓰는 값(기억한 값)을 읽고 씁니다.
/// 저장 위치, 발 상자 영역, 켜 둔 분석 판, LED와 PPG 변환기, 카메라 설정이 모두 이 객체를 거칩니다.
/// 다음 실행에 남길지는 저장소(<see cref="IValueStore"/>)를 고를 때 한 번 정하고, 쓰는 쪽은 남길지 묻지 않습니다.
/// </summary>
/// <remarks>
/// 시험 실행(--fake 등)은 <see cref="ThisRunOnly"/>로 메모리에만 두어, 시험에서 바꾼 값이 장비 PC의 실제 값을 덮어쓰지 않게 합니다.
/// </remarks>
public sealed class RememberedValues
{
    private readonly IValueStore _store;

    public RememberedValues(IValueStore store)
    {
        _store = store;
    }

    /// <summary>이번 실행 동안만 둡니다. 처음에는 비어 있어 지난 실행의 값을 읽지 않습니다.</summary>
    public static RememberedValues ThisRunOnly() => new(new InMemoryValueStore());

    /// <summary>다음 실행에 남기는지입니다.</summary>
    public bool IsSaved => _store.IsPersistent;

    public string? GetString(string key) => _store.Get(key);

    public void SetString(string key, string value) => _store.Set(key, value);

    /// <summary>"true"나 "false"가 아니면 없는 값으로 봅니다.</summary>
    public bool? GetBool(string key) => bool.TryParse(_store.Get(key), out var value) ? value : null;

    public void SetBool(string key, bool value) => _store.Set(key, value ? "true" : "false");

    /// <summary>"폭,높이" 형식입니다. 읽지 못하면 null입니다.</summary>
    public (int Width, int Height)? GetSize(string key)
    {
        var parts = Numbers(key, 2);
        return parts is null ? null : (parts[0], parts[1]);
    }

    public void SetSize(string key, int width, int height) => _store.Set(key, Format(width, height));

    /// <summary>"x,y,폭,높이" 형식입니다. 읽지 못하면 null입니다.</summary>
    public Rect? GetRect(string key)
    {
        var parts = Numbers(key, 4);
        return parts is null ? null : new Rect(parts[0], parts[1], parts[2], parts[3]);
    }

    public void SetRect(string key, Rect rect) => _store.Set(key, Format(rect.X, rect.Y, rect.Width, rect.Height));

    private static string Format(params int[] numbers) =>
        string.Join(',', numbers.Select(n => n.ToString(CultureInfo.InvariantCulture)));

    private int[]? Numbers(string key, int count)
    {
        var text = _store.Get(key);
        if (text is null) return null;
        var parts = text.Split(',');
        if (parts.Length != count) return null;
        var numbers = new int[count];
        for (var i = 0; i < count; i++)
        {
            if (!int.TryParse(parts[i], NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out numbers[i])) return null;
        }
        return numbers;
    }
}
