namespace DiabeticFoot.App.Dialogs;

/// <summary>대화상자 제목 줄 앞의 점 색입니다. 색은 상태에만 씁니다.</summary>
public enum DialogTone
{
    /// <summary>진행 중이거나 주의가 필요함(Theme의 settling 색)입니다.</summary>
    Warning,

    /// <summary>문제가 있음(Theme의 mismatch 색)입니다.</summary>
    Error,
}
