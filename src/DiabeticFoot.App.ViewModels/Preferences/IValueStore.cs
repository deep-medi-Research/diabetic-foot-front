namespace DiabeticFoot.App.Preferences;

/// <summary>
/// 기억한 값을 문자열로 읽고 쓰는 저장소입니다. 키는 "shot/root"처럼 '/'로 묶은 이름입니다.
/// </summary>
public interface IValueStore
{
    /// <summary>다음 실행에도 값이 남으면 true입니다.</summary>
    bool IsPersistent { get; }

    /// <summary>값을 읽습니다. 없거나 읽지 못하면 null입니다.</summary>
    string? Get(string key);

    /// <summary>값을 씁니다. 쓰지 못하면 조용히 넘어갑니다(기억하지 못해도 촬영은 계속해야 합니다).</summary>
    void Set(string key, string value);
}
