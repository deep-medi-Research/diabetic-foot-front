using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Platform;

namespace DiabeticFoot.Platform.Tests;

// 원본 test_template_map.cc의 시험입니다(표준 발 틀 PI 지도 그리기).
public class TemplateMapTests
{
    // 시간 창 1개, 2 x 2 격자, 폭이 길이의 0.4인 사각 윤곽입니다. 왼쪽 위 칸만 계산하지 않습니다.
    private static PerfusionDetail Square(double leftValue, double rightValue) => new()
    {
        WindowCentersS = [5.0],
        MapRows = 2,
        MapCols = 2,
        Maps = [double.NaN, leftValue, leftValue, leftValue, double.NaN, rightValue, rightValue, rightValue],
        MapValid = [0, 1, 1, 1],
        OutlineU = [0.0, 1.0],
        OutlineLeft = [-0.2, -0.2],
        OutlineRight = [0.2, 0.2],
    };

    private static Rgb8 Pixel(RgbaImage image, int x, int y)
    {
        var at = ((long)y * image.Width + x) * 4;
        return new Rgb8(image.Rgba[at], image.Rgba[at + 1], image.Rgba[at + 2]);
    }

    private static byte Alpha(RgbaImage image, int x, int y) => image.Rgba[((long)y * image.Width + x) * 4 + 3];

    [Fact]
    public void TurboRunsFromBlueToRed()
    {
        var low = TemplateMap.TurboColor(0.02, 0.2);
        var high = TemplateMap.TurboColor(0.2, 0.2);
        Assert.True(low.B > low.R);
        Assert.True(high.R > high.B);
        Assert.Equal(high, TemplateMap.TurboColor(0.5, 0.2));  // 위끝보다 큰 값은 위끝 색
        Assert.Equal(new Rgb8(128, 128, 128), TemplateMap.TurboColor(double.NaN, 0.2));
        Assert.Equal(new Rgb8(128, 128, 128), TemplateMap.TurboColor(0.1, 0.0));
    }

    [Fact]
    public void PlacesLeftFootLeftAndMirrorsRightFoot()
    {
        var d = Square(0.0, 0.2);
        var image = TemplateMap.DrawTemplateMap(d, 0, 51, 0.2);
        Assert.NotNull(image);
        // 발 폭은 0.4 * 50 + 1 = 21화소, 두 발 사이는 4화소입니다.
        Assert.Equal(51, image!.Height);
        Assert.Equal(21 * 2 + 4, image.Width);
        Assert.Equal(0, Alpha(image, 22, 25));  // 두 발 사이는 투명
        // 왼발 왼쪽 위는 계산하지 않는 칸, 오른쪽 아래는 0.0의 색
        Assert.Equal(new Rgb8(34, 38, 44), Pixel(image, 2, 2));
        Assert.Equal(TemplateMap.TurboColor(0.0, 0.2), Pixel(image, 18, 48));
        // 오른발은 좌우반전이라 계산하지 않는 칸이 오른쪽 위에 옵니다.
        Assert.Equal(new Rgb8(34, 38, 44), Pixel(image, image.Width - 3, 2));
        Assert.Equal(TemplateMap.TurboColor(0.2, 0.2), Pixel(image, 27, 48));
    }

    [Fact]
    public void RejectsMissingValuesAndBadWindow()
    {
        var d = Square(0.1, 0.1);
        Assert.Null(TemplateMap.DrawTemplateMap(d, 1, 51, 0.2));
        Assert.Null(TemplateMap.DrawTemplateMap(d, 0, 1, 0.2));
        d = d with { OutlineU = [] };
        Assert.Null(TemplateMap.DrawTemplateMap(d, 0, 51, 0.2));
        Assert.Null(TemplateMap.TemplateMapTop(d));
    }

    [Fact]
    public void TopUsesValidCellsOnly()
    {
        var d = Square(0.1, 0.3);
        var maps = d.Maps.ToArray();
        maps[0] = 9.0;  // 계산하지 않는 칸의 값은 보지 않습니다
        d = d with { Maps = maps };
        var top = TemplateMap.TemplateMapTop(d);
        Assert.NotNull(top);
        Assert.Equal(0.3, top!.Value, 15);
    }

    [Fact]
    public void DrawsV8Result()
    {
        var d = PerfusionResults.ReadPerfusionDetail(Path.Combine(AppContext.BaseDirectory, "Data", "160307_v8_perfusion.npz"), out var error);
        Assert.True(d is not null, error);
        var top = TemplateMap.TemplateMapTop(d!);
        Assert.NotNull(top);
        // numpy로 구한 같은 값: 모든 창과 두 발의 계산 칸 11396개 중 95번째 백분위수
        Assert.True(Math.Abs(0.22829335615099622 - top!.Value) <= 1e-12);
        var image = TemplateMap.DrawTemplateMap(d!, 5, 236, top.Value);
        Assert.NotNull(image);
        Assert.Equal(236, image!.Height);
        long painted = 0;
        for (var i = 3; i < image.Rgba.Length; i += 4) painted += image.Rgba[i] != 0 ? 1 : 0;
        // 윤곽 안 화소가 그림의 절반쯤입니다(발 모양이 사각형보다 좁음).
        Assert.True(painted > image.Rgba.Length / 4 / 3);
        Assert.True(painted < image.Rgba.Length / 4 * 3 / 4);
    }
}
