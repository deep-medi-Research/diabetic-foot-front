using DiabeticFoot.App.Analysis;
using DiabeticFoot.Platform;

namespace DiabeticFoot.App.Tests.Analysis;

/// <summary>관류 분석 프로그램의 진행 줄을 화면 문장과 비율로 바꾸는 규칙입니다.</summary>
public sealed class AnalysisTextTests
{
    // 5단계 분석은 단계별 실측 시간 비율에 하위 단계를 더하고, 그 밖의 단계 수는 균등하게 나눕니다.
    [Theory]
    [InlineData(5, 1, 0, 0, 0.0)]
    [InlineData(5, 1, 1, 2, 0.31)]
    [InlineData(5, 2, 0, 0, 0.62)]
    [InlineData(5, 3, 1, 2, 0.86)]
    [InlineData(5, 5, 2, 4, 0.96)]
    [InlineData(3, 1, 1, 2, 0.5)]
    [InlineData(4, 4, 1, 1, 1.0)]
    [InlineData(0, 0, 0, 0, 0.0)]
    public void ComputesProgress(int total, int step, int subStep, int subTotal, double expected)
    {
        var status = new AnalyzerStatus { Total = total, Step = step, SubStep = subStep, SubTotal = subTotal };

        Assert.Equal(expected, AnalysisViewModel.ProgressOf(status), precision: 6);
    }

    [Theory]
    [InlineData(5, 0, "분석 모델 준비")]
    [InlineData(5, 1, "영상 읽기와 발 찾기")]
    [InlineData(5, 3, "관류 계산")]
    [InlineData(5, 5, "결과 파일 저장")]
    [InlineData(4, 2, "그 판의 문구")]
    public void NamesStage(int total, int step, string expected)
    {
        var status = new AnalyzerStatus { Total = total, Step = step, Message = "그 판의 문구" };

        Assert.Equal(expected, AnalysisViewModel.StageTextOf(status));
    }

    // 진행 비율이 낮거나 끝났으면 예상하지 않고, 5초 단위로 올려 보입니다.
    [Theory]
    [InlineData(10_000L, 0.05, "")]
    [InlineData(10_000L, 1.0, "")]
    [InlineData(10_000L, 0.5, "약 10초 남음")]
    [InlineData(1_000L, 0.5, "약 5초 남음")]
    [InlineData(10_000L, 0.4, "약 15초 남음")]
    [InlineData(26_500L, 0.62, "약 20초 남음")]
    public void EstimatesRemainingTime(long elapsedMs, double progress, string expected)
    {
        Assert.Equal(expected, AnalysisViewModel.RemainingOf(elapsedMs, progress));
    }

    [Theory]
    [InlineData(0L, "0초")]
    [InlineData(499L, "0초")]
    [InlineData(500L, "1초")]
    [InlineData(1_499L, "1초")]
    [InlineData(1_500L, "2초")]
    public void RoundsSeconds(long ms, string expected) => Assert.Equal(expected, AnalysisViewModel.Seconds(ms));

    // 연구 코드의 예외 기록은 앞 세 줄과 240자까지만 보입니다.
    [Fact]
    public void ShortensLongErrors()
    {
        Assert.Equal("a\nb", AnalysisViewModel.Shorten("a\n\nb"));
        Assert.Equal("1\n2\n3", AnalysisViewModel.Shorten("1\n2\n3"));
        Assert.Equal("1\n2\n3\n…", AnalysisViewModel.Shorten("1\n2\n3\n4\n5"));
        Assert.Equal(new string('가', 240) + "…", AnalysisViewModel.Shorten(new string('가', 300)));
        Assert.Equal("", AnalysisViewModel.Shorten(""));
    }

    [Theory]
    [InlineData(@"C:\저장\홍길동_19551103\D\20261022_101454", "20261022_101454")]
    [InlineData("/저장/홍길동_19551103/D/20261022_101454/", "20261022_101454")]
    [InlineData("", "")]
    public void TakesLastFolderName(string folder, string expected) => Assert.Equal(expected, AnalysisViewModel.FolderName(folder));
}
