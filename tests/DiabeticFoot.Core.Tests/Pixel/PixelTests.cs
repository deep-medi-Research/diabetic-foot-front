using DiabeticFoot.Core.Pixel;
using static DiabeticFoot.Core.Pixel.PixelFunctions;

namespace DiabeticFoot.Core.Tests.Pixel;

// 화소 (x, y)의 Y를 x + 10 * y로 채운 UYVY 프레임입니다. 행 끝에 빈 바이트 pad를 둡니다.
internal sealed record UyvyFrame(byte[] Bytes, FrameView View)
{
    public static UyvyFrame Make(int width, int height, int pad)
    {
        int stride = width * 2 + pad;
        var bytes = new byte[stride * height];
        Array.Fill(bytes, (byte)0xEE);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int pair = y * stride + (x / 2) * 4;
                bytes[pair + (x % 2 == 0 ? 1 : 3)] = (byte)(x + 10 * y);
                bytes[pair + (x % 2 == 0 ? 0 : 2)] = (byte)(x % 2 == 0 ? 128 : 129);
            }
        }
        var view = new FrameView
        {
            Format = PixelFormat.Uyvy,
            Width = width,
            Height = height,
            Plane0 = bytes,
            RowStride0 = stride,
        };
        return new UyvyFrame(bytes, view);
    }
}

public class RectTests
{
    // 색차 격자 밖으로 나간 시작점은 안쪽으로 올리고 끝은 내립니다.
    [Fact]
    public void FitToGridMovesInwardForNv12()
    {
        Rect fitted = FitToGrid(new Rect(1, 1, 6, 6), PixelFormat.Nv12, 16, 16);

        Assert.Equal(new Rect(2, 2, 4, 4), fitted);
    }

    // 프레임을 벗어난 부분은 잘라냅니다.
    [Fact]
    public void FitToGridClipsToFrame()
    {
        Rect fitted = FitToGrid(new Rect(-4, 10, 30, 30), PixelFormat.Uyvy, 16, 16);

        Assert.Equal(new Rect(0, 10, 16, 6), fitted);
    }

    // 열화상 원시값은 화소마다 독립이라 홀수 좌표도 그대로 둡니다.
    [Fact]
    public void Y16KeepsOddCoordinates()
    {
        Assert.True(IsAligned(new Rect(3, 5, 7, 9), PixelFormat.Y16, 640, 480));
        Assert.False(IsAligned(new Rect(3, 5, 7, 9), PixelFormat.Uyvy, 640, 480));
    }

    // 높이가 폭보다 커야 세로형입니다. 정사각형과 빈 영역은 세로형이 아닙니다.
    [Fact]
    public void IsPortraitRequiresHeightAboveWidth()
    {
        Assert.True(new Rect(0, 0, 960, 1080).IsPortrait);
        Assert.False(new Rect(0, 0, 1920, 1080).IsPortrait);
        Assert.False(new Rect(0, 0, 540, 540).IsPortrait);
        Assert.False(new Rect(0, 0, 0, 1080).IsPortrait);
    }
}

public class BrightnessTests
{
    // UYVY 밝기는 U, V가 아니라 Y만 읽고, 행 끝 빈 바이트는 읽지 않습니다.
    [Fact]
    public void ReadsOnlyLumaFromUyvy()
    {
        UyvyFrame frame = UyvyFrame.Make(8, 4, 6);

        Brightness b = MeasureBrightness(frame.View, new Rect(1, 1, 3, 2), 255);

        // Y = x + 10y, x는 1..3, y는 1..2
        Assert.Equal(6, b.PixelCount);
        Assert.Equal((11 + 12 + 13) + (21 + 22 + 23), b.Sum);
        Assert.Equal(17.0, MeanOf(b));
    }

    // 설정 변경 직후 블랙 레벨이 모든 화소에서 약 7 오른 것을 평균으로 구분합니다.
    [Fact]
    public void DetectsBlackLevelOffsetOfSeven()
    {
        UyvyFrame dark = UyvyFrame.Make(16, 8, 0);
        UyvyFrame lifted = UyvyFrame.Make(16, 8, 0);
        // Y 최댓값이 15 + 70이라 7을 더해도 255를 넘지 않습니다.
        for (int i = 1; i < lifted.Bytes.Length; i += 2)
        {
            lifted.Bytes[i] = (byte)(lifted.Bytes[i] + 7);
        }
        var whole = new Rect(0, 0, 16, 8);

        double liftedMean = MeanOf(MeasureBrightness(lifted.View, whole, 255));
        double darkMean = MeanOf(MeasureBrightness(dark.View, whole, 255));

        Assert.Equal(7.0, liftedMean - darkMean);
    }

    // clip_level 이상인 화소만 셉니다.
    [Fact]
    public void CountsClippedPixels()
    {
        UyvyFrame frame = UyvyFrame.Make(8, 4, 0);

        // Y가 30 이상인 화소는 y = 3 행의 8개입니다.
        Brightness b = MeasureBrightness(frame.View, new Rect(0, 0, 8, 4), 30);

        Assert.Equal(8, b.ClippedPixels);
    }

    // 열화상 원시값은 리틀 엔디언 16비트로 읽습니다.
    [Fact]
    public void ReadsLittleEndianY16()
    {
        byte[] bytes = { 0x34, 0x12, 0xFF, 0xFF };
        var view = new FrameView
        {
            Format = PixelFormat.Y16,
            Width = 2,
            Height = 1,
            Plane0 = bytes,
            RowStride0 = 4,
        };

        Brightness b = MeasureBrightness(view, new Rect(0, 0, 2, 1), 0xFFFF);

        Assert.Equal(0x1234 + 0xFFFF, b.Sum);
        Assert.Equal(1, b.ClippedPixels);
    }

    // 프레임을 벗어난 영역은 재지 않습니다.
    [Fact]
    public void RejectsRectOutsideFrame()
    {
        UyvyFrame frame = UyvyFrame.Make(8, 4, 0);

        Brightness b = MeasureBrightness(frame.View, new Rect(6, 0, 4, 4), 255);

        Assert.Equal(0, b.PixelCount);
        Assert.Equal(0.0, MeanOf(b));
    }
}

public class CropTests
{
    // 잘라낸 UYVY는 행 사이 빈 바이트 없이 원래 바이트 순서 그대로 담깁니다.
    [Fact]
    public void CopiesUyvyRowsWithoutPadding()
    {
        UyvyFrame frame = UyvyFrame.Make(8, 4, 6);
        var rect = new Rect(2, 1, 4, 2);
        var output = new byte[PackedBytes(PixelFormat.Uyvy, rect)];

        Assert.True(CopyRect(frame.View, rect, output));

        Assert.Equal(16, output.Length);
        byte[] expected =
        {
            128, 12, 129, 13, 128, 14, 129, 15,
            128, 22, 129, 23, 128, 24, 129, 25,
        };
        Assert.Equal(expected, output);
    }

    // NV12는 Y 행 뒤에 절반 높이의 UV 행이 붙습니다.
    [Fact]
    public void CopiesNv12PlanesInOrder()
    {
        const int width = 4;
        const int height = 4;
        var luma = new byte[width * height];
        for (int i = 0; i < luma.Length; ++i)
        {
            luma[i] = (byte)i;
        }
        byte[] chroma = { 100, 101, 102, 103, 110, 111, 112, 113 };
        var view = new FrameView
        {
            Format = PixelFormat.Nv12,
            Width = width,
            Height = height,
            Plane0 = luma,
            Plane1 = chroma,
            RowStride0 = width,
            RowStride1 = width,
        };
        var rect = new Rect(2, 2, 2, 2);
        var output = new byte[PackedBytes(PixelFormat.Nv12, rect)];

        Assert.True(CopyRect(view, rect, output));

        byte[] expected = { 10, 11, 14, 15, 112, 113 };
        Assert.Equal(expected, output);
    }

    // 색차 격자에 맞지 않는 영역은 자르지 않습니다. U, V 순서가 바뀌기 때문입니다.
    [Fact]
    public void RefusesOddUyvyStart()
    {
        UyvyFrame frame = UyvyFrame.Make(8, 4, 0);
        var rect = new Rect(1, 0, 2, 1);
        var output = new byte[PackedBytes(PixelFormat.Uyvy, rect)];

        Assert.False(CopyRect(frame.View, rect, output));
    }

    // 출력 버퍼 크기가 맞지 않으면 쓰지 않습니다.
    [Fact]
    public void RefusesWrongOutputSize()
    {
        UyvyFrame frame = UyvyFrame.Make(8, 4, 0);
        var output = new byte[3];

        Assert.False(CopyRect(frame.View, new Rect(0, 0, 2, 1), output));
    }
}

public class SplitTests
{
    // 두 영역은 폭이 같고, 가운데 선에서 맞닿습니다.
    [Fact]
    public void KeepsMiddleLineAndEqualWidths()
    {
        var (left, right) = SplitLeftRight(new Rect(100, 50, 1000, 400), PixelFormat.Uyvy, 32);

        Assert.Equal(600, left.X + left.Width);
        Assert.Equal(600, right.X);
        Assert.Equal(right.Width, left.Width);
        Assert.Equal(0, left.Width % 32);
        Assert.True(left.X <= 600 - 480);
        Assert.True(left.X >= 100);
        Assert.Equal(50, right.Y);
        Assert.Equal(400, right.Height);
    }

    // 줄인 결과가 원래 영역을 벗어나지 않습니다.
    [Fact]
    public void StaysInsideOriginalRect()
    {
        var rect = new Rect(10, 0, 202, 10);

        var (left, right) = SplitLeftRight(rect, PixelFormat.Uyvy, 32);

        Assert.True(left.X >= rect.X);
        Assert.True(right.X + right.Width <= rect.X + rect.Width);
        Assert.Equal(96, left.Width);
    }

    // 반폭이 width_step보다 좁으면 나누지 않습니다.
    [Fact]
    public void ReturnsEmptyWhenTooNarrow()
    {
        var (left, right) = SplitLeftRight(new Rect(0, 0, 40, 10), PixelFormat.Uyvy, 32);

        Assert.Equal(new Rect(), left);
        Assert.Equal(new Rect(), right);
    }

    // width_step이 색차 격자의 배수가 아니면 나누지 않습니다.
    [Fact]
    public void RejectsStepOffChromaGrid()
    {
        var (left, _) = SplitLeftRight(new Rect(0, 0, 200, 10), PixelFormat.Uyvy, 31);

        Assert.Equal(new Rect(), left);
    }
}
