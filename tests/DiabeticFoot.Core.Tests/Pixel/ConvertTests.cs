using DiabeticFoot.Core.Pixel;
using static DiabeticFoot.Core.Pixel.PixelFunctions;

namespace DiabeticFoot.Core.Tests.Pixel;

public class ConvertTests
{
    // 열화상 640x480처럼 화소 2바이트 리틀 엔디언인 원시값 프레임입니다.
    private sealed record Y16Frame(byte[] Bytes, FrameView View);

    private static Y16Frame MakeY16(int width, int height, IReadOnlyList<ushort> values)
    {
        var bytes = new List<byte>();
        foreach (ushort v in values)
        {
            bytes.Add((byte)(v & 0xFF));
            bytes.Add((byte)(v >> 8));
        }
        byte[] array = bytes.ToArray();
        var view = new FrameView
        {
            Format = PixelFormat.Y16,
            Width = width,
            Height = height,
            Plane0 = array,
            RowStride0 = width * 2,
        };
        return new Y16Frame(array, view);
    }

    // 원시값 범위를 찾고 그 구간을 0~255로 폅니다.
    [Fact]
    public void StretchesRangeToGray()
    {
        Y16Frame f = MakeY16(4, 1, new ushort[] { 7000, 7100, 7200, 7050 });

        Y16Range range = FindRange(f.View, new Rect(0, 0, 4, 1));
        var gray = new byte[4];
        Assert.True(Y16ToGray8(f.View, range, gray));

        Assert.Equal(7000, range.Low);
        Assert.Equal(7200, range.High);
        Assert.Equal(new byte[] { 0, 128, 255, 64 }, gray);
    }

    // 범위 밖 값은 양 끝으로 자릅니다.
    [Fact]
    public void ClampsOutsideRange()
    {
        Y16Frame f = MakeY16(2, 1, new ushort[] { 100, 60000 });
        var gray = new byte[2];

        Assert.True(Y16ToGray8(f.View, new Y16Range(1000, 2000), gray));

        Assert.Equal(new byte[] { 0, 255 }, gray);
    }

    // 모든 값이 같으면 중간 회색입니다.
    [Fact]
    public void FlatFrameIsMidGray()
    {
        Y16Frame f = MakeY16(2, 1, new ushort[] { 5000, 5000 });
        var gray = new byte[2];

        Assert.True(Y16ToGray8(f.View, FindRange(f.View, new Rect(0, 0, 2, 1)), gray));

        Assert.Equal(new byte[] { 128, 128 }, gray);
    }

    // 회색을 UYVY로 바꾸면 Y만 남고 색차는 128이라, 밝기 측정에서 같은 값이 나옵니다.
    [Fact]
    public void GrayToUyvyKeepsLuma()
    {
        byte[] gray = { 10, 20, 30, 40 };
        var uyvy = new byte[8];

        Assert.True(Gray8ToUyvy(gray, 2, 2, uyvy));

        Assert.Equal(new byte[] { 128, 10, 128, 20, 128, 30, 128, 40 }, uyvy);
        var v = new FrameView
        {
            Format = PixelFormat.Uyvy,
            Width = 2,
            Height = 2,
            Plane0 = uyvy,
            RowStride0 = 4,
        };
        Assert.Equal(100, MeasureBrightness(v, new Rect(0, 0, 2, 2), 255).Sum);
    }

    // raw16 저장은 기존 자르기로 행 사이 빈 바이트 없이 원시값을 그대로 담습니다.
    [Fact]
    public void Raw16CopyKeepsRawBytes()
    {
        Y16Frame f = MakeY16(2, 2, new ushort[] { 1, 2, 3, 0xABCD });
        var output = new byte[PackedBytes(PixelFormat.Y16, new Rect(0, 0, 2, 2))];

        Assert.True(CopyRect(f.View, new Rect(0, 0, 2, 2), output));

        Assert.Equal(f.Bytes, output);
    }

    // 미리보기 변환: 색차가 없는 Y 16과 235는 검정과 흰색이고, 2화소마다 하나씩 골라 크기가 절반이 됩니다.
    [Fact]
    public void UyvyPreviewMapsBlackAndWhite()
    {
        // 같은 행 두 줄입니다. 2화소마다 고르면 2x1이 됩니다.
        byte[] uyvy = { 128, 16, 128, 16, 128, 235, 128, 235, 128, 16, 128, 16, 128, 235, 128, 235 };
        var v = new FrameView
        {
            Format = PixelFormat.Uyvy,
            Width = 4,
            Height = 2,
            Plane0 = uyvy,
            RowStride0 = 8,
        };
        var rgb = new byte[2 * 1 * 3];

        Assert.True(UyvyToRgbPreview(v, 2, rgb));

        Assert.Equal(new byte[] { 0, 0, 0, 255, 255, 255 }, rgb);
    }

    // 평면 4:2:2로 옮기면 Y, U, V가 제자리에 가고, 평면의 행 간격이 넓어도 됩니다.
    [Fact]
    public void UyvyToPlanarSplitsComponents()
    {
        byte[] uyvy = { 10, 1, 20, 2, 11, 3, 21, 4 }; // U0 Y0 V0 Y1 U1 Y2 V1 Y3
        var v = new FrameView
        {
            Format = PixelFormat.Uyvy,
            Width = 4,
            Height = 1,
            Plane0 = uyvy,
            RowStride0 = 8,
        };
        var y = new byte[8];
        var u = new byte[4];
        var w = new byte[4];

        Assert.True(UyvyToPlanar422(v, y, 8, u, 4, w, 4));

        Assert.Equal(new byte[] { 1, 2, 3, 4, 0, 0, 0, 0 }, y);
        Assert.Equal(new byte[] { 10, 11, 0, 0 }, u);
        Assert.Equal(new byte[] { 20, 21, 0, 0 }, w);
    }

    // 형식이나 크기가 맞지 않으면 쓰지 않습니다.
    [Fact]
    public void RejectsWrongInput()
    {
        Y16Frame f = MakeY16(2, 1, new ushort[] { 1, 2 });
        var small = new byte[1];
        var uyvy = new byte[6];

        Assert.False(Y16ToGray8(f.View, new Y16Range(0, 10), small));
        Assert.False(Gray8ToUyvy(new byte[3], 3, 1, uyvy));
    }

    // 회색 색상표를 step 1로 쓰면 R, G, B가 모두 Y16ToGray8 결과와 같습니다.
    [Fact]
    public void GrayPreviewMatchesGray8()
    {
        var values = new List<ushort>();
        for (int i = 0; i < 6 * 4; ++i) values.Add((ushort)(6900 + i * 17));
        Y16Frame f = MakeY16(6, 4, values);
        var range = new Y16Range(7000, 7200);
        var gray = new byte[6 * 4];
        var rgb = new byte[6 * 4 * 3];

        Assert.True(Y16ToGray8(f.View, range, gray));
        Assert.True(Y16ToRgbPreview(f.View, range, Palette.Gray, 1, rgb));

        for (int i = 0; i < gray.Length; ++i)
        {
            Assert.True(gray[i] == rgb[i * 3 + 0], $"{i}");
            Assert.True(gray[i] == rgb[i * 3 + 1], $"{i}");
            Assert.True(gray[i] == rgb[i * 3 + 2], $"{i}");
        }
    }

    // Iron의 양 끝은 검정과 흰색이고, 구간이 한 값이면 0과 255 사이 색입니다.
    [Fact]
    public void IronPaletteEndsAreBlackAndWhite()
    {
        Y16Frame f = MakeY16(3, 1, new ushort[] { 7000, 7200, 9000 });
        var rgb = new byte[3 * 3];

        Assert.True(Y16ToRgbPreview(f.View, new Y16Range(7000, 7200), Palette.Iron, 1, rgb));

        Assert.Equal(new byte[] { 0, 0, 0, 255, 255, 255, 255, 255, 255 }, rgb);

        var flat = new byte[3 * 3];
        Assert.True(Y16ToRgbPreview(f.View, new Y16Range(7000, 7000), Palette.Iron, 1, flat));
        Assert.Equal(flat[3], flat[0]);
        Assert.NotEqual(new byte[9], flat);
        Assert.NotEqual(Enumerable.Repeat((byte)255, 9).ToArray(), flat);
    }

    // step 화소마다 하나씩 골라 출력 크기가 (width / step) x (height / step)입니다. 나머지 화소는 버립니다.
    [Fact]
    public void Y16PreviewStepSetsSize()
    {
        // 5x3에서 step 2이면 2x1이고, 고르는 화소는 (0, 0)과 (2, 0)입니다.
        Y16Frame f = MakeY16(5, 3, new ushort[] { 0, 1, 100, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 });
        var rgb = new byte[2 * 1 * 3];

        Assert.True(Y16ToRgbPreview(f.View, new Y16Range(0, 100), Palette.Gray, 2, rgb));

        Assert.Equal(new byte[] { 0, 0, 0, 255, 255, 255 }, rgb);

        var by3 = new byte[1 * 1 * 3];
        Assert.True(Y16ToRgbPreview(f.View, new Y16Range(0, 100), Palette.Iron, 3, by3));
    }

    // 출력 크기, 형식, step이 맞지 않으면 쓰지 않습니다.
    [Fact]
    public void Y16PreviewRejectsWrongInput()
    {
        Y16Frame f = MakeY16(4, 2, Enumerable.Repeat((ushort)5, 8).ToArray());
        var full = Enumerable.Repeat((byte)7, 4 * 2 * 3).ToArray();
        var small = Enumerable.Repeat((byte)7, 4 * 2 * 3 - 1).ToArray();
        var half = Enumerable.Repeat((byte)7, 2 * 1 * 3).ToArray();

        Assert.False(Y16ToRgbPreview(f.View, new Y16Range(0, 10), Palette.Gray, 1, small));
        Assert.False(Y16ToRgbPreview(f.View, new Y16Range(0, 10), Palette.Gray, 1, half));
        Assert.False(Y16ToRgbPreview(f.View, new Y16Range(0, 10), Palette.Gray, 0, full));
        Assert.False(Y16ToRgbPreview(f.View, new Y16Range(0, 10), Palette.Gray, 5, full));
        FrameView uyvy = f.View with { Format = PixelFormat.Uyvy };
        Assert.False(Y16ToRgbPreview(uyvy, new Y16Range(0, 10), Palette.Gray, 1, full));
        Assert.Equal(Enumerable.Repeat((byte)7, small.Length), small);
        Assert.Equal(Enumerable.Repeat((byte)7, full.Length), full);
    }

    // 천분위 범위는 값 순서의 양 끝 일부 화소를 뺍니다. 1000화소 중 이상값 화소 3개를 뺀 범위입니다.
    [Fact]
    public void PercentileRangeSkipsOutliers()
    {
        // 40x25 = 1000화소. 값은 7000~7099가 10번씩이고, 3화소는 60000(위쪽 이상값), 3화소는 100(아래쪽 이상값)입니다.
        var values = new List<ushort>();
        for (int i = 0; i < 1000; ++i) values.Add((ushort)(7000 + i % 100));
        values[10] = 60000;
        values[500] = 60000;
        values[999] = 60000;
        values[20] = 100;
        values[400] = 100;
        values[800] = 100;
        Y16Frame f = MakeY16(40, 25, values);
        var all = new Rect(0, 0, 40, 25);

        Y16Range full = FindPercentileRange(f.View, all, 0, 1000);
        Assert.Equal(FindRange(f.View, all).Low, full.Low);
        Assert.Equal(FindRange(f.View, all).High, full.High);

        // 순위 round(999 * p / 1000): 995는 순위 994, 5는 순위 5라 양쪽 3화소가 빠집니다.
        Y16Range trimmed = FindPercentileRange(f.View, all, 5, 995);
        Assert.Equal(7000, trimmed.Low);
        Assert.Equal(7099, trimmed.High);

        // 999는 가장 큰 1화소만 뺍니다. 이상값 화소가 3개면 남은 2개가 위 끝입니다.
        Assert.Equal(60000, FindPercentileRange(f.View, all, 1, 999).High);
    }

    // 이상값 화소가 1개이면 999천분위가 그 값을 뺍니다.
    [Fact]
    public void PercentileRangeSkipsSingleHotPixel()
    {
        var values = new ushort[1000];
        Array.Fill(values, (ushort)7000);
        for (int i = 0; i < 1000; ++i) values[i] = (ushort)(7000 + i % 50);
        values[123] = 60000;
        Y16Frame f = MakeY16(40, 25, values);

        Y16Range range = FindPercentileRange(f.View, new Rect(0, 0, 40, 25), 0, 999);

        Assert.Equal(7000, range.Low);
        Assert.Equal(7049, range.High);
    }

    // 천분위가 범위 밖이거나 영역이 프레임 밖이면 (0, 0)입니다. 영역 일부만 셉니다.
    [Fact]
    public void PercentileRangeChecksInput()
    {
        Y16Frame f = MakeY16(4, 1, new ushort[] { 10, 20, 30, 40 });

        Assert.Equal(0, FindPercentileRange(f.View, new Rect(0, 0, 4, 1), -1, 1000).High);
        Assert.Equal(0, FindPercentileRange(f.View, new Rect(0, 0, 4, 1), 0, 1001).High);
        Assert.Equal(0, FindPercentileRange(f.View, new Rect(0, 0, 4, 1), 600, 400).High);
        Assert.Equal(0, FindPercentileRange(f.View, new Rect(0, 0, 5, 1), 0, 1000).High);
        Y16Range part = FindPercentileRange(f.View, new Rect(1, 0, 2, 1), 0, 1000);
        Assert.Equal(20, part.Low);
        Assert.Equal(30, part.High);
    }

    // 색상표 4종의 양 끝 색은 각 기준점 출처의 첫 색과 마지막 색입니다.
    [Fact]
    public void PaletteEndColors()
    {
        (Palette Palette, Rgb8 Low, Rgb8 High)[] cases =
        {
            (Palette.Gray, new Rgb8(0, 0, 0), new Rgb8(255, 255, 255)),
            (Palette.Iron, new Rgb8(0, 0, 0), new Rgb8(255, 255, 255)),
            (Palette.Rainbow, new Rgb8(0, 0, 128), new Rgb8(128, 0, 0)),
            (Palette.HighContrast, new Rgb8(0, 0, 0), new Rgb8(255, 255, 255)),
        };
        foreach (var c in cases)
        {
            Assert.True(c.Low == PaletteColor(c.Palette, 0), $"{(int)c.Palette}");
            Assert.True(c.High == PaletteColor(c.Palette, 255), $"{(int)c.Palette}");
        }
        // 레인보우의 3/8 지점은 청록, 7/8 지점은 빨강입니다(MATLAB jet).
        Assert.Equal(new Rgb8(0, 255, 255), PaletteColor(Palette.Rainbow, 96));
        Assert.Equal(new Rgb8(255, 0, 0), PaletteColor(Palette.Rainbow, 223));
    }

    // 미리보기는 PaletteColor와 같은 표를 씁니다. 구간 아래 끝, 위 끝, 구간 밖 값을 칠해 확인합니다.
    [Fact]
    public void PreviewUsesPaletteTable()
    {
        Y16Frame f = MakeY16(4, 1, new ushort[] { 6000, 7000, 7200, 9000 });
        foreach (Palette palette in new[] { Palette.Gray, Palette.Iron, Palette.Rainbow, Palette.HighContrast })
        {
            var rgb = new byte[4 * 3];
            Assert.True(Y16ToRgbPreview(f.View, new Y16Range(7000, 7200), palette, 1, rgb));
            Rgb8 low = PaletteColor(palette, 0);
            Rgb8 high = PaletteColor(palette, 255);
            byte[] expected =
            {
                low.R, low.G, low.B, low.R, low.G, low.B,
                high.R, high.G, high.B, high.R, high.G, high.B,
            };
            Assert.True(expected.SequenceEqual(rgb), $"{(int)palette}");
        }
    }

    // 고대비 색상표는 256단계에서 같은 색이 두 번 나오지 않습니다.
    [Fact]
    public void HighContrastColorsAreDistinct()
    {
        var seen = new List<Rgb8>();
        for (int level = 0; level < 256; ++level)
        {
            Rgb8 color = PaletteColor(Palette.HighContrast, (byte)level);
            foreach (Rgb8 other in seen) Assert.False(other == color, $"{level}");
            seen.Add(color);
        }
    }

    // 영상을 누를 때 아이언, 흑백, 레인보우, 고대비 순서로 바뀌고 고대비 다음은 아이언입니다.
    [Fact]
    public void NextPaletteCyclesInOrder()
    {
        Assert.Equal(Palette.Gray, NextPalette(Palette.Iron));
        Assert.Equal(Palette.Rainbow, NextPalette(Palette.Gray));
        Assert.Equal(Palette.HighContrast, NextPalette(Palette.Rainbow));
        Assert.Equal(Palette.Iron, NextPalette(Palette.HighContrast));
    }
}
