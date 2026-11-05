using DiabeticFoot.App.Startup;

namespace DiabeticFoot.App.Tests.Startup;

public sealed class AppVersionTests
{
    // 릴리즈 빌드는 vX.Y.Z를, 개발 빌드는 빌드한 커밋을 창 제목에 보입니다.
    [Theory]
    [InlineData("0.1.17+abc1234", "v0.1.17", "abc1234", "v0.1.17", "당뇨발 촬영 v0.1.17")]
    [InlineData("0.0.0+abc1234-dirty", "", "abc1234-dirty", "(개발 abc1234-dirty)", "당뇨발 촬영 (개발 abc1234-dirty)")]
    [InlineData("0.1.17", "v0.1.17", "", "v0.1.17", "당뇨발 촬영 v0.1.17")]
    [InlineData("0.0.0", "", "", "", "당뇨발 촬영")]
    [InlineData("", "", "", "", "당뇨발 촬영")]
    [InlineData(null, "", "", "", "당뇨발 촬영")]
    public void ReadsInformationalVersion(string? text, string version, string commit, string display, string title)
    {
        var parsed = AppVersion.FromInformationalVersion(text);

        Assert.Equal(version, parsed.Version);
        Assert.Equal(commit, parsed.Commit);
        Assert.Equal(display, parsed.Display);
        Assert.Equal(title, parsed.WindowTitle);
    }
}
