using DiabeticFoot.Core.Pixel;
using static DiabeticFoot.Core.Pixel.PixelFunctions;

namespace DiabeticFoot.Core.Tests.Pixel;

public class RgbSumsTests
{
    // UYVY 프레임과 그 바이트입니다. 행 끝에 빈 바이트를 두어 행 간격을 따르는지도 봅니다.
    private sealed record Frame(byte[] Bytes, FrameView View);

    private const int PadBytes = 8;

    private static Frame MakeUyvy(int width, int height, uint seed)
    {
        int stride = width * 2 + PadBytes;
        var bytes = new byte[stride * height];
        // 같은 값이 반복되지 않게 선형 합동 수열로 채웁니다. 0~255 전 범위가 나와 위아래로 잘리는 화소도 생깁니다.
        for (int i = 0; i < bytes.Length; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            bytes[i] = (byte)(seed >> 24);
        }
        var view = new FrameView
        {
            Format = PixelFormat.Uyvy,
            Width = width,
            Height = height,
            Plane0 = bytes,
            RowStride0 = stride,
        };
        return new Frame(bytes, view);
    }

    private static Frame MakeFlat(int width, int height, byte u, byte y, byte v)
    {
        var bytes = new List<byte>();
        for (int i = 0; i < width / 2 * height; ++i)
        {
            bytes.AddRange(new[] { u, y, v, y });
        }
        byte[] array = bytes.ToArray();
        var view = new FrameView
        {
            Format = PixelFormat.Uyvy,
            Width = width,
            Height = height,
            Plane0 = array,
            RowStride0 = width * 2,
        };
        return new Frame(array, view);
    }

    // 미리보기 변환(step 1)으로 만든 RGB에서 영역의 step 화소마다 직접 더한 값입니다.
    private static RgbSums SumFromPreview(FrameView view, Rect rect, int step)
    {
        var rgb = new byte[view.Width * view.Height * 3];
        Assert.True(UyvyToRgbPreview(view, 1, rgb));
        long pixelCount = 0;
        var sum = new long[3];
        var saturated = new long[3];
        for (int y = rect.Y; y < rect.Y + rect.Height; y += step)
        {
            for (int x = rect.X; x < rect.X + rect.Width; x += step)
            {
                int i = (y * view.Width + x) * 3;
                for (int c = 0; c < 3; ++c)
                {
                    sum[c] += rgb[i + c];
                    saturated[c] += rgb[i + c] == 255 ? 1 : 0;
                }
                ++pixelCount;
            }
        }
        return new RgbSums
        {
            PixelCount = pixelCount,
            Sum = new RgbChannels(sum[0], sum[1], sum[2]),
            Saturated = new RgbChannels(saturated[0], saturated[1], saturated[2]),
        };
    }

    private static void ExpectSame(RgbSums a, RgbSums b)
    {
        Assert.Equal(b.PixelCount, a.PixelCount);
        Assert.Equal(b.Sum, a.Sum);
        Assert.Equal(b.Saturated, a.Saturated);
    }

    // 모든 화소를 읽으면 미리보기 RGB를 더한 값과 같습니다. 화면 색과 패널 숫자가 같은 계산이라는 계약입니다.
    [Fact]
    public void MatchesPreviewConversion()
    {
        Frame f = MakeUyvy(16, 6, 1);
        var whole = new Rect(0, 0, 16, 6);

        RgbSums sums = MeasureRgb(f.View, whole, 1);

        Assert.Equal(16 * 6, sums.PixelCount);
        ExpectSame(sums, SumFromPreview(f.View, whole, 1));
        Assert.True(sums.Saturated[0] + sums.Saturated[1] + sums.Saturated[2] > 0);
    }

    // 홀수 시작 위치와 step 2, 3도 미리보기 RGB에서 같은 화소를 고른 값과 같습니다.
    [Fact]
    public void StepAndOddStartPickSamePixels()
    {
        Frame f = MakeUyvy(20, 9, 7);
        foreach (Rect rect in new[] { new Rect(1, 1, 13, 7), new Rect(3, 0, 16, 9), new Rect(0, 2, 1, 1) })
        {
            foreach (int step in new[] { 1, 2, 3 })
            {
                ExpectSame(MeasureRgb(f.View, rect, step), SumFromPreview(f.View, rect, step));
            }
        }
        // 13 x 7 영역을 step 2로 읽으면 가로 7개, 세로 4개입니다.
        Assert.Equal(7 * 4, MeasureRgb(f.View, new Rect(1, 1, 13, 7), 2).PixelCount);
    }

    // 색차가 없는 Y 235는 흰색이라 세 채널이 모두 포화이고, Y 16은 검정이라 포화가 없습니다.
    [Fact]
    public void CountsSaturatedPerChannel()
    {
        Frame white = MakeFlat(4, 2, 128, 235, 128);
        RgbSums w = MeasureRgb(white.View, new Rect(0, 0, 4, 2), 1);
        Assert.Equal(new RgbChannels(255 * 8, 255 * 8, 255 * 8), w.Sum);
        Assert.Equal(new RgbChannels(8, 8, 8), w.Saturated);

        // V가 크면 R만 255에 닿습니다. Y 180, U 128, V 240: R = (298 * 164 + 409 * 112 + 128) >> 8 > 255
        Frame red = MakeFlat(4, 2, 128, 180, 240);
        RgbSums r = MeasureRgb(red.View, new Rect(0, 0, 4, 2), 1);
        Assert.Equal(new RgbChannels(8, 0, 0), r.Saturated);
        Assert.Equal(255 * 8, r.Sum[0]);
        Assert.True(r.Sum[1] < 255 * 8);

        Frame black = MakeFlat(4, 2, 128, 16, 128);
        RgbSums b = MeasureRgb(black.View, new Rect(0, 0, 4, 2), 1);
        Assert.Equal(new RgbChannels(0, 0, 0), b.Sum);
        Assert.Equal(new RgbChannels(0, 0, 0), b.Saturated);
    }

    // 형식, 영역, step이 맞지 않으면 화소 수가 0입니다.
    [Fact]
    public void RejectsWrongInput()
    {
        Frame f = MakeUyvy(8, 4, 3);
        Assert.Equal(0, MeasureRgb(f.View, new Rect(0, 0, 9, 4), 1).PixelCount);
        Assert.Equal(0, MeasureRgb(f.View, new Rect(-1, 0, 4, 4), 1).PixelCount);
        Assert.Equal(0, MeasureRgb(f.View, new Rect(0, 0, 0, 4), 1).PixelCount);
        Assert.Equal(0, MeasureRgb(f.View, new Rect(0, 0, 8, 4), 0).PixelCount);
        FrameView y16 = f.View with { Format = PixelFormat.Y16 };
        Assert.Equal(0, MeasureRgb(y16, new Rect(0, 0, 8, 4), 1).PixelCount);
    }
}
