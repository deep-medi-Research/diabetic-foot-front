namespace DiabeticFoot.App.Startup;

/// <summary>
/// 앱 버전입니다. 릴리즈 빌드는 태그 이름(vX.Y.Z)이고, 개발 빌드는 버전이 비어 있어 빌드한 커밋을 보입니다.
/// 노트북에 설치된 개발 빌드가 어느 코드인지 화면에서 확인하기 위함입니다.
/// </summary>
/// <param name="Version">릴리즈 버전(vX.Y.Z)입니다. 개발 빌드는 빈 문자열입니다.</param>
/// <param name="Commit">빌드한 커밋(7자리)입니다. 커밋하지 않은 변경이 있으면 -dirty가 붙고, git으로 읽지 못하면 빈 문자열입니다.</param>
public sealed record AppVersion(string Version = "", string Commit = "")
{
    private const string Product = "당뇨발 촬영";

    /// <summary>
    /// 어셈블리의 정보 버전("0.1.17+abc1234", 개발 빌드는 "0.0.0+abc1234")을 나눕니다.
    /// MSBuild의 Version 속성이 앞부분이고, 빌드한 커밋이 '+' 뒤입니다(App.csproj).
    /// </summary>
    public static AppVersion FromInformationalVersion(string? text)
    {
        if (string.IsNullOrEmpty(text)) return new AppVersion();
        var plus = text.IndexOf('+');
        var number = plus < 0 ? text : text[..plus];
        var commit = plus < 0 ? "" : text[(plus + 1)..];
        return new AppVersion(number is "" or "0.0.0" ? "" : "v" + number, commit);
    }

    /// <summary>화면에 쓰는 버전 문장입니다. 개발 빌드는 "(개발 abc1234)", 커밋도 모르면 빈 문자열입니다.</summary>
    public string Display => Version.Length > 0 ? Version : Commit.Length > 0 ? $"(개발 {Commit})" : "";

    /// <summary>창 제목입니다. 현장에서 어느 버전인지 창 제목으로 확인합니다.</summary>
    public string WindowTitle => Display.Length > 0 ? $"{Product} {Display}" : Product;
}
