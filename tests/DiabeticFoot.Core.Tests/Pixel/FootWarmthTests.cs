using DiabeticFoot.Core.Pixel;
using static DiabeticFoot.Core.Pixel.PixelFunctions;

namespace DiabeticFoot.Core.Tests.Pixel;

public class ThermalContrastTests
{
    // 20 x 10 Y16 프레임. 왼쪽 10열은 배경 1000에 발 화소 25개를 footLeft로, 오른쪽 10열은 배경 1000에 발 화소 25개를
    // footRight로 둡니다.
    private static ushort[] Frame(ushort footLeft, ushort footRight)
    {
        var px = new ushort[200];
        Array.Fill(px, (ushort)1000);
        for (int y = 0; y < 10; ++y)
        {
            for (int x = 0; x < 20; ++x)
            {
                bool foot = y >= 5 && (x % 10) < 5; // 상자마다 25화소
                if (foot) px[y * 20 + x] = x < 10 ? footLeft : footRight;
            }
        }
        return px;
    }

    private static FrameView View(ushort[] px)
    {
        var bytes = new byte[px.Length * 2];
        for (int i = 0; i < px.Length; ++i)
        {
            bytes[i * 2] = (byte)(px[i] & 0xFF);
            bytes[i * 2 + 1] = (byte)(px[i] >> 8);
        }
        return new FrameView
        {
            Format = PixelFormat.Y16,
            Width = 20,
            Height = 10,
            Plane0 = bytes,
            RowStride0 = 40,
        };
    }

    [Fact]
    public void IsTopTenthMinusLowerQuarter()
    {
        ushort[] px = Frame(1100, 1030);
        // 상자 100화소 중 75화소가 배경이라 하위 25%는 배경, 상위 10%는 발 화소입니다.
        Assert.Equal(100, ThermalContrast(View(px), new Rect(0, 0, 10, 10)));
        Assert.Equal(30, ThermalContrast(View(px), new Rect(10, 0, 10, 10)));
        Assert.Equal(0, ThermalContrast(View(px), new Rect(0, 0, 0, 0)));
    }
}

public class FootWarmthCheckTests
{
    [Fact]
    public void NeedsFullWindowBeforeJudging()
    {
        var check = new FootWarmthCheck();
        for (int i = 0; i + 1 < FootWarmthCheck.WarmFrames; ++i) check.Push(80, 30);
        Assert.Equal(Warmth.Checking, check.State(0));
        Assert.Equal(Warmth.Checking, check.State(1));
        check.Push(80, 30);
        Assert.Equal(Warmth.Warm, check.State(0));
        Assert.Equal(Warmth.Cold, check.State(1));
        Assert.Equal(30, check.Lowest(1));
    }

    [Fact]
    public void OneLowFrameKeepsFootColdUntilItLeavesTheWindow()
    {
        var check = new FootWarmthCheck();
        for (int i = 0; i < FootWarmthCheck.WarmFrames; ++i) check.Push(FootWarmthCheck.WarmContrastMin, 90);
        Assert.Equal(Warmth.Warm, check.State(0)); // 기준과 같은 값은 따뜻함
        check.Push(FootWarmthCheck.WarmContrastMin - 1, 90);
        Assert.Equal(Warmth.Cold, check.State(0));
        for (int i = 0; i + 1 < FootWarmthCheck.WarmFrames; ++i) check.Push(70, 90);
        Assert.Equal(Warmth.Cold, check.State(0)); // 낮은 장이 아직 최근 WarmFrames장 안에 있음
        check.Push(70, 90);
        Assert.Equal(Warmth.Warm, check.State(0));
        check.Reset();
        Assert.Equal(Warmth.Checking, check.State(0));
        Assert.Equal(0, check.Lowest(0));
    }
}
