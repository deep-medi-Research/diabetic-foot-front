namespace DiabeticFoot.Core.Pixel;

/// <summary>열화상 원시값의 범위입니다. 온도가 아니라 센서 원시값입니다.</summary>
public readonly record struct Y16Range(ushort Low = 0, ushort High = 0);

/// <summary>한 화소의 색입니다.</summary>
public readonly record struct Rgb8(byte R = 0, byte G = 0, byte B = 0);

/// <summary>
/// 열화상 미리보기의 색상표입니다. 화면 표시에만 씁니다.
/// </summary>
public enum Palette : byte
{
    /// <summary>회색. R, G, B가 모두 Y16ToGray8의 결과와 같습니다.</summary>
    Gray,

    /// <summary>검정, 보라, 빨강, 주황, 노랑, 흰색 순서입니다. 낮은 값이 검정, 높은 값이 흰색입니다.</summary>
    Iron,

    /// <summary>남색, 파랑, 청록, 노랑, 빨강, 검붉은색 순서입니다(MATLAB jet와 같은 기준점).</summary>
    Rainbow,

    /// <summary>검정, 파랑, 청록, 초록, 노랑, 빨강, 자홍, 흰색 순서입니다. 한 단계당 색 변화가 가장 큽니다.</summary>
    HighContrast,
}

public static partial class PixelFunctions
{
    /// <summary>영역 안 원시값의 최솟값과 최댓값을 찾습니다.</summary>
    /// <param name="y16">Y16 형식 프레임</param>
    /// <param name="rect">찾을 영역</param>
    /// <returns>최솟값과 최댓값. 영역이 비었거나 형식이 Y16이 아니면 (0, 0)입니다</returns>
    public static Y16Range FindRange(FrameView y16, Rect rect)
    {
        if (y16.Format != PixelFormat.Y16 || y16.Plane0.IsEmpty || !IsAligned(rect, y16.Format, y16.Width, y16.Height))
        {
            return default;
        }
        ReadOnlySpan<byte> plane = y16.Plane0.Span;
        ushort low = ushort.MaxValue;
        ushort high = 0;
        for (int y = rect.Y; y < rect.Y + rect.Height; ++y)
        {
            ReadOnlySpan<byte> row = plane[(y * y16.RowStride0)..];
            for (int x = rect.X; x < rect.X + rect.Width; ++x)
            {
                ushort v = ReadY16(row, x);
                low = Math.Min(low, v);
                high = Math.Max(high, v);
            }
        }
        return new Y16Range(low, high);
    }

    /// <summary>열화상 원시값을 8비트 회색으로 폅니다.</summary>
    /// <param name="y16">Y16 형식 프레임</param>
    /// <param name="range">펼 구간</param>
    /// <param name="output">결과. width * height 바이트</param>
    /// <returns>썼으면 true. 형식이 Y16이 아니거나 output 크기가 다르면 쓰지 않고 false입니다</returns>
    /// <remarks>
    /// [Low, High] 구간을 0~255에 맞추고 구간 밖 값은 양 끝으로 자릅니다. 구간이 한 값이면 모두 128입니다.
    /// 사람이 보는 확인용 영상에만 씁니다. 분석에는 원시값(raw16)을 씁니다.
    /// </remarks>
    public static bool Y16ToGray8(FrameView y16, Y16Range range, Span<byte> output)
    {
        if (y16.Format != PixelFormat.Y16 || y16.Plane0.IsEmpty || y16.Width <= 0 || y16.Height <= 0 ||
            output.Length != y16.Width * y16.Height)
        {
            return false;
        }
        // 범위가 한 값이면 모두 중간 회색으로 둡니다. 0으로 나누지 않기 위함입니다.
        int span = Math.Max(range.High - range.Low, 0);
        ReadOnlySpan<byte> plane = y16.Plane0.Span;
        for (int y = 0; y < y16.Height; ++y)
        {
            ReadOnlySpan<byte> row = plane[(y * y16.RowStride0)..];
            Span<byte> dst = output.Slice(y * y16.Width, y16.Width);
            for (int x = 0; x < y16.Width; ++x)
            {
                if (span == 0)
                {
                    dst[x] = 128;
                    continue;
                }
                dst[x] = ToLevel(ReadY16(row, x), range, span);
            }
        }
        return true;
    }

    /// <summary>
    /// 영역 안 원시값에서 아래쪽과 위쪽 일부 화소를 뺀 범위를 찾습니다.
    /// </summary>
    /// <param name="y16">Y16 형식 프레임</param>
    /// <param name="rect">찾을 영역</param>
    /// <param name="lowPermille">아래 끝의 천분위. 0~1000</param>
    /// <param name="highPermille">위 끝의 천분위. lowPermille~1000</param>
    /// <returns>고른 두 값. 영역이 비었거나 형식이 Y16이 아니거나 천분위가 범위 밖이면 (0, 0)입니다</returns>
    /// <remarks>
    /// 영역의 화소를 값 순서로 세운 뒤 순위 round((n - 1) * permille / 1000)의 값을 고릅니다(0부터 셈).
    /// lowPermille 0과 highPermille 1000이면 <see cref="FindRange"/>와 같습니다.
    /// 1000화소에서 highPermille 999는 가장 큰 1화소를, 995는 가장 큰 5화소를 뺍니다.
    /// 결함 화소나 화면 안 고온 물체 몇 개가 표시 범위를 넓혀 발 영역의 대비가 낮아지는 것을 막기 위한 것입니다.
    /// 화면 표시용이고 분석에는 쓰지 않습니다.
    /// </remarks>
    public static Y16Range FindPercentileRange(FrameView y16, Rect rect, int lowPermille, int highPermille)
    {
        if (y16.Format != PixelFormat.Y16 || y16.Plane0.IsEmpty || !IsAligned(rect, y16.Format, y16.Width, y16.Height) ||
            lowPermille < 0 || highPermille > 1000 || lowPermille > highPermille)
        {
            return default;
        }
        // 값이 16비트라 정렬 대신 도수분포로 순위를 찾습니다. 화소 수와 관계없이 메모리가 일정합니다.
        var histogram = new uint[65536];
        ReadOnlySpan<byte> plane = y16.Plane0.Span;
        for (int y = rect.Y; y < rect.Y + rect.Height; ++y)
        {
            ReadOnlySpan<byte> row = plane[(y * y16.RowStride0)..];
            for (int x = rect.X; x < rect.X + rect.Width; ++x) ++histogram[ReadY16(row, x)];
        }
        long last = (long)rect.Width * rect.Height - 1;
        long lowRank = (last * lowPermille + 500) / 1000;
        long highRank = (last * highPermille + 500) / 1000;
        ushort low = 0;
        ushort high = 0;
        long seen = 0;
        bool lowFound = false;
        for (int v = 0; v < 65536; ++v)
        {
            seen += histogram[v];
            if (!lowFound && seen > lowRank)
            {
                low = (ushort)v;
                lowFound = true;
            }
            if (seen > highRank)
            {
                high = (ushort)v;
                break;
            }
        }
        return new Y16Range(low, high);
    }

    /// <summary>화면에서 영상을 누를 때 다음으로 쓸 색상표입니다.</summary>
    /// <param name="palette">지금 색상표</param>
    /// <returns>아이언, 흑백, 레인보우, 고대비 순서의 다음 색상표. 고대비 다음은 아이언입니다</returns>
    public static Palette NextPalette(Palette palette) => palette switch
    {
        Palette.Iron => Palette.Gray,
        Palette.Gray => Palette.Rainbow,
        Palette.Rainbow => Palette.HighContrast,
        Palette.HighContrast => Palette.Iron,
        _ => Palette.Iron,
    };

    /// <summary>색상표에서 한 단계의 색을 고릅니다. <see cref="Y16ToRgbPreview"/>가 쓰는 것과 같은 표입니다.</summary>
    /// <param name="palette">색상표</param>
    /// <param name="level">0~255 단계. 0이 표시 구간의 아래 끝, 255가 위 끝입니다</param>
    /// <returns>그 단계의 색</returns>
    public static Rgb8 PaletteColor(Palette palette, byte level) => TableOf(palette)[level];

    /// <summary>열화상 원시값을 색상표로 칠한 미리보기용 RGB 24비트 영상을 만듭니다.</summary>
    /// <param name="y16">Y16 형식 프레임</param>
    /// <param name="range">펼 구간. 구간 밖 값은 양 끝 색이고, 구간이 한 값이면 모두 128단계 색입니다</param>
    /// <param name="palette">색상표</param>
    /// <param name="step">가로세로 step 화소마다 하나씩 고릅니다. 640x480에서 2이면 320x240</param>
    /// <param name="output">결과. (width / step) * (height / step) * 3 바이트</param>
    /// <returns>썼으면 true. 형식이 Y16이 아니거나 step이 1보다 작거나 output 크기가 다르면 쓰지 않고 false입니다</returns>
    /// <remarks>
    /// 각 값을 <see cref="Y16ToGray8"/>과 같은 계산으로 0~255 단계로 바꾼 뒤 색상표에서 색을 고릅니다. 바이트 순서는 R, G, B입니다.
    /// 화면에만 씁니다. 색은 표시 구간에 따라 달라져 온도를 나타내지 않으므로 분석이나 저장에는 원시값(raw16)을 씁니다.
    /// </remarks>
    public static bool Y16ToRgbPreview(FrameView y16, Y16Range range, Palette palette, int step, Span<byte> output)
    {
        if (y16.Format != PixelFormat.Y16 || y16.Plane0.IsEmpty || step < 1) return false;
        int w = y16.Width / step;
        int h = y16.Height / step;
        if (w <= 0 || h <= 0 || output.Length != w * h * 3) return false;
        int span = Math.Max(range.High - range.Low, 0);
        Rgb8[] table = TableOf(palette);
        ReadOnlySpan<byte> plane = y16.Plane0.Span;
        for (int oy = 0; oy < h; ++oy)
        {
            ReadOnlySpan<byte> row = plane[(oy * step * y16.RowStride0)..];
            Span<byte> dst = output.Slice(oy * w * 3, w * 3);
            for (int ox = 0; ox < w; ++ox)
            {
                byte level = span == 0 ? (byte)128 : ToLevel(ReadY16(row, ox * step), range, span);
                Rgb8 color = table[level];
                dst[ox * 3 + 0] = color.R;
                dst[ox * 3 + 1] = color.G;
                dst[ox * 3 + 2] = color.B;
            }
        }
        return true;
    }

    /// <summary>회색 영상을 색차가 없는 UYVY로 바꿉니다(U와 V는 128). 열화상 확인용 영상을 영상 파일로 쓸 때 씁니다.</summary>
    /// <param name="gray">회색 화소. width * height 바이트</param>
    /// <param name="width">폭. 짝수</param>
    /// <param name="height">높이</param>
    /// <param name="output">결과. width * height * 2 바이트</param>
    /// <returns>썼으면 true. 폭이 홀수이거나 크기가 맞지 않으면 false입니다</returns>
    public static bool Gray8ToUyvy(ReadOnlySpan<byte> gray, int width, int height, Span<byte> output)
    {
        if (width <= 0 || height <= 0 || width % 2 != 0 || gray.Length != width * height || output.Length != gray.Length * 2)
        {
            return false;
        }
        for (int i = 0; i < gray.Length; ++i)
        {
            output[i * 2] = 128;
            output[i * 2 + 1] = gray[i];
        }
        return true;
    }

    /// <summary>UYVY를 Y, U, V 평면(4:2:2 planar)으로 옮깁니다. FFV1 인코더가 평면 형식을 받기 때문입니다.</summary>
    /// <param name="uyvy">UYVY 형식 프레임. 폭은 짝수</param>
    /// <param name="y">Y 평면. 행마다 width 바이트</param>
    /// <param name="yStride">Y 평면의 행 간격</param>
    /// <param name="u">U 평면. 행마다 width / 2 바이트</param>
    /// <param name="uStride">U 평면의 행 간격</param>
    /// <param name="v">V 평면. 행마다 width / 2 바이트</param>
    /// <param name="vStride">V 평면의 행 간격</param>
    /// <returns>옮겼으면 true. 형식이 UYVY가 아니거나 폭이 홀수이거나 평면이 없으면 false입니다</returns>
    /// <remarks>바이트 위치만 바뀌어 무손실이고, 되돌리면 원래 바이트와 같습니다.</remarks>
    public static bool UyvyToPlanar422(FrameView uyvy, Span<byte> y, int yStride, Span<byte> u, int uStride, Span<byte> v, int vStride)
    {
        if (uyvy.Format != PixelFormat.Uyvy || uyvy.Plane0.IsEmpty || uyvy.Width <= 0 || uyvy.Width % 2 != 0 ||
            uyvy.Height <= 0 || y.IsEmpty || u.IsEmpty || v.IsEmpty)
        {
            return false;
        }
        int pairs = uyvy.Width / 2;
        ReadOnlySpan<byte> plane = uyvy.Plane0.Span;
        for (int row = 0; row < uyvy.Height; ++row)
        {
            ReadOnlySpan<byte> src = plane[(row * uyvy.RowStride0)..];
            Span<byte> dy = y[(row * yStride)..];
            Span<byte> du = u[(row * uStride)..];
            Span<byte> dv = v[(row * vStride)..];
            for (int p = 0; p < pairs; ++p)
            {
                du[p] = src[p * 4];
                dy[p * 2] = src[p * 4 + 1];
                dv[p] = src[p * 4 + 2];
                dy[p * 2 + 1] = src[p * 4 + 3];
            }
        }
        return true;
    }

    /// <summary>미리보기용 RGB 24비트 영상을 만듭니다(BT.601 제한 범위).</summary>
    /// <param name="uyvy">UYVY 형식 프레임</param>
    /// <param name="step">가로세로 step 화소마다 하나씩 고릅니다. 1920x1200에서 4이면 480x300</param>
    /// <param name="output">결과. (width / step) * (height / step) * 3 바이트</param>
    /// <returns>썼으면 true. 형식이 UYVY가 아니거나 output 크기가 다르면 false입니다</returns>
    /// <remarks>화면에만 씁니다. 분석이나 저장에는 쓰지 않습니다.</remarks>
    public static bool UyvyToRgbPreview(FrameView uyvy, int step, Span<byte> output)
    {
        if (uyvy.Format != PixelFormat.Uyvy || uyvy.Plane0.IsEmpty || step < 1) return false;
        int w = uyvy.Width / step;
        int h = uyvy.Height / step;
        if (w <= 0 || h <= 0 || output.Length != w * h * 3) return false;
        ReadOnlySpan<byte> plane = uyvy.Plane0.Span;
        for (int oy = 0; oy < h; ++oy)
        {
            ReadOnlySpan<byte> row = plane[(oy * step * uyvy.RowStride0)..];
            Span<byte> dst = output.Slice(oy * w * 3, w * 3);
            for (int ox = 0; ox < w; ++ox)
            {
                Rgb8 rgb = UyvyPixelToRgb(row, ox * step);
                dst[ox * 3 + 0] = rgb.R;
                dst[ox * 3 + 1] = rgb.G;
                dst[ox * 3 + 2] = rgb.B;
            }
        }
        return true;
    }

    private static ushort ReadY16(ReadOnlySpan<byte> row, int x) => (ushort)(row[x * 2] | (row[x * 2 + 1] << 8));

    // 원시값을 [low, high] 구간에서 0~255 단계로 바꿉니다. span이 0이면 호출하지 않습니다.
    private static byte ToLevel(ushort value, Y16Range range, int span)
    {
        int v = Math.Clamp((int)value, range.Low, range.High) - range.Low;
        return (byte)((v * 255 + span / 2) / span);
    }

    private readonly record struct PaletteStop(int Level, byte R, byte G, byte B);

    // 첫 기준점은 0단계, 마지막 기준점은 255단계여야 합니다. 기준점 사이는 반올림한 선형 보간입니다.
    private static Rgb8[] MakeTable(PaletteStop[] stops)
    {
        var table = new Rgb8[256];
        for (int i = 0; i + 1 < stops.Length; ++i)
        {
            PaletteStop a = stops[i];
            PaletteStop b = stops[i + 1];
            int d = b.Level - a.Level;
            for (int level = a.Level; level <= b.Level; ++level)
            {
                int wa = b.Level - level;
                int wb = level - a.Level;
                table[level] = new Rgb8(
                    (byte)((a.R * wa + b.R * wb + d / 2) / d),
                    (byte)((a.G * wa + b.G * wb + d / 2) / d),
                    (byte)((a.B * wa + b.B * wb + d / 2) / d));
            }
        }
        return table;
    }

    // Gray 기준점: 단계 값을 R, G, B에 그대로 씁니다. Y16ToGray8과 같은 값입니다.
    private static readonly PaletteStop[] GrayStops = { new(0, 0, 0, 0), new(255, 255, 255, 255) };

    // Iron 기준점: 열화상 표시에서 흔한 검정, 보라, 빨강, 주황, 노랑, 흰색 순서를 이 레포에서 직접 정한 값입니다.
    // 공개된 표준 표가 없고, 제조사 SDK 색상표(ApplyColorMap)의 값은 쓰지 않았습니다.
    private static readonly PaletteStop[] IronStops =
    {
        new(0, 0, 0, 0), new(50, 90, 0, 150), new(110, 210, 20, 40),
        new(160, 250, 120, 0), new(210, 255, 220, 0), new(255, 255, 255, 255),
    };

    // Rainbow 기준점: MATLAB jet(OpenCV COLORMAP_JET도 같음)의 정의입니다. 남색(0, 0, 0.5)에서 시작해 1/8, 3/8, 5/8, 7/8
    // 지점에 파랑, 청록, 노랑, 빨강을 두고 검붉은색(0.5, 0, 0)으로 끝납니다. 지점은 255배 해 가장 가까운 단계로 반올림했습니다.
    private static readonly PaletteStop[] RainbowStops =
    {
        new(0, 0, 0, 128), new(32, 0, 0, 255), new(96, 0, 255, 255),
        new(159, 255, 255, 0), new(223, 255, 0, 0), new(255, 128, 0, 0),
    };

    // HighContrast 기준점: RGB 정육면체의 꼭짓점 8개를 한 번에 한 채널만 바뀌는 순서로 같은 간격(255 * i / 7 반올림)에 둔
    // 값으로, 이 레포에서 정했습니다. 약 36단계마다 한 채널이 0과 255 사이를 오가고, 같은 색이 두 번 나오지 않습니다.
    private static readonly PaletteStop[] HighContrastStops =
    {
        new(0, 0, 0, 0), new(36, 0, 0, 255), new(73, 0, 255, 255), new(109, 0, 255, 0),
        new(146, 255, 255, 0), new(182, 255, 0, 0), new(219, 255, 0, 255), new(255, 255, 255, 255),
    };

    private static readonly Rgb8[] GrayTable = MakeTable(GrayStops);
    private static readonly Rgb8[] IronTable = MakeTable(IronStops);
    private static readonly Rgb8[] RainbowTable = MakeTable(RainbowStops);
    private static readonly Rgb8[] HighContrastTable = MakeTable(HighContrastStops);

    private static Rgb8[] TableOf(Palette palette) => palette switch
    {
        Palette.Gray => GrayTable,
        Palette.Iron => IronTable,
        Palette.Rainbow => RainbowTable,
        Palette.HighContrast => HighContrastTable,
        _ => GrayTable,
    };
}
