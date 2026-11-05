using DiabeticFoot.Core.Pixel;

namespace DiabeticFoot.Platform;

/// <summary>결과 창이 그리는 RGBA 영상입니다. 한 화소는 4바이트이고 줄 사이 여백이 없습니다. 투명 화소는 발 틀 밖입니다.</summary>
public sealed record RgbaImage
{
    public int Width { get; init; }

    public int Height { get; init; }

    /// <summary>화소 바이트(R, G, B, A 순서)입니다.</summary>
    public byte[] Rgba { get; init; } = [];
}

/// <summary>표준 발 틀 PI 지도를 그립니다. 관류 분석 프로그램 그림(`_pi_map.png`)과 같은 배치와 색표입니다.</summary>
public static class TemplateMap
{
    // 계산하지 않는 칸(발가락 사이, 발 둘레)의 색입니다. 결과 창 바탕보다 조금 밝습니다.
    private static readonly Rgb8 GapColor = new(34, 38, 44);

    private static readonly Rgb8 Gray = new(128, 128, 128);

    /// <summary>
    /// PI 값을 색으로 바꿉니다. 관류 분석 프로그램 그림(`_pi_map.png`)과 같은 turbo 색표입니다.
    /// </summary>
    /// <param name="value">PI(%)</param>
    /// <param name="top">색표 위끝 PI(%). 0과 top 사이를 파랑에서 빨강으로 칠합니다</param>
    /// <returns>value나 top이 유한하지 않거나 top이 0 이하면 회색입니다</returns>
    public static Rgb8 TurboColor(double value, double top)
    {
        if (!double.IsFinite(value) || !double.IsFinite(top) || top <= 0) return Gray;
        var x = Math.Clamp(value / top, 0.0, 1.0);
        // Google turbo 색표의 다항식 근사입니다(Mikhailov 2019). matplotlib turbo와 눈으로 구분되지 않습니다.
        var r = 0.13572138 + x * (4.61539260 + x * (-42.66032258 + x * (132.13108234 + x * (-152.94239396 + x * 59.28637943))));
        var g = 0.09140261 + x * (2.19418839 + x * (4.84296658 + x * (-14.18503333 + x * (4.27729857 + x * 2.82956604))));
        var b = 0.10667330 + x * (12.64194608 + x * (-60.58204836 + x * (110.36276771 + x * (-89.90310912 + x * 27.34824973))));
        return new Rgb8(ToByte(r), ToByte(g), ToByte(b));
    }

    /// <summary>
    /// 표준 발 틀 PI 지도의 색표 위끝입니다. 모든 시간 창과 두 발의 계산 칸 값 중 95번째 백분위수입니다.
    /// </summary>
    /// <param name="detail">결과 배열(<see cref="PerfusionDetail.HasTemplateMap"/>이 true)</param>
    /// <returns>PI(%). 값이 없으면 null</returns>
    /// <remarks>녹화 하나 안에서 시간 창이 바뀌어도 같은 색이 같은 값이 되도록 시간 창마다 따로 정하지 않습니다.</remarks>
    public static double? TemplateMapTop(PerfusionDetail detail)
    {
        if (!detail.HasTemplateMap) return null;
        var perMap = (long)detail.MapRows * detail.MapCols;
        var values = new List<double>();
        for (var i = 0; i < detail.Maps.Count; ++i)
        {
            if (detail.MapValid[(int)(i % perMap)] != 0 && double.IsFinite(detail.Maps[i])) values.Add(detail.Maps[i]);
        }
        if (values.Count == 0) return null;
        var at = (values.Count - 1) * 95 / 100;
        values.Sort();
        return values[at] > 0 ? values[at] : null;
    }

    /// <summary>
    /// 시간 창 하나의 표준 발 틀 PI 지도를 그립니다. 왼발을 왼쪽에, 오른발을 좌우반전해 오른쪽에 둡니다.
    /// </summary>
    /// <param name="detail">결과 배열(<see cref="PerfusionDetail.HasTemplateMap"/>이 true)</param>
    /// <param name="window">시간 창 번호(0부터)</param>
    /// <param name="footHeight">발 그림 높이(화소). 발 길이가 이 높이입니다</param>
    /// <param name="top">색표 위끝 PI(%)(<see cref="TemplateMapTop"/>)</param>
    /// <returns>그림. 틀 지도 값이 없거나 창 번호가 범위 밖이거나 footHeight가 2 미만이면 null</returns>
    /// <remarks>
    /// 칸 사이 값은 이웃 계산 칸에서 쌍선형 보간합니다. 계산하지 않는 칸(발가락 사이 등)은 어두운 회색입니다.
    /// 관류 분석 프로그램 그림(`_pi_map.png`)과 같은 배치입니다.
    /// </remarks>
    public static RgbaImage? DrawTemplateMap(PerfusionDetail detail, int window, int footHeight, double top)
    {
        if (!detail.HasTemplateMap || footHeight < 2 || window < 0 || window >= detail.WindowCentersS.Count) return null;
        var xMin = detail.OutlineLeft.Min();
        var xMax = detail.OutlineRight.Max();
        var footWidth = (int)Math.Ceiling((xMax - xMin) * (footHeight - 1)) + 1;
        // 두 발 사이는 발 높이의 1/15입니다. 관류 분석 프로그램 그림과 비슷한 간격입니다.
        var gap = Math.Max(4, footHeight / 15);
        var width = footWidth * 2 + gap;
        var pixels = new byte[(long)width * footHeight * 4];
        DrawFoot(detail, 0, window, top, footHeight, xMin, footWidth, mirror: false, xOffset: 0, width, pixels);
        DrawFoot(detail, 1, window, top, footHeight, xMin, footWidth, mirror: true, xOffset: footWidth + gap, width, pixels);
        return new RgbaImage { Width = width, Height = footHeight, Rgba = pixels };
    }

    private static byte ToByte(double v) => (byte)Math.Round(Math.Clamp(v, 0.0, 1.0) * 255.0, MidpointRounding.AwayFromZero);

    // 길이 위치 u에서 윤곽 한쪽 끝의 가로 위치입니다. outline_u는 0에서 1로 늘어납니다.
    private static double Interpolate(IReadOnlyList<double> u, IReadOnlyList<double> x, double at)
    {
        if (at <= u[0]) return x[0];
        if (at >= u[^1]) return x[^1];
        // 처음으로 at보다 큰 원소(upper_bound)를 이분 탐색으로 찾습니다.
        var low = 0;
        var high = u.Count;
        while (low < high)
        {
            var middle = low + (high - low) / 2;
            if (u[middle] <= at) low = middle + 1;
            else high = middle;
        }
        var t = (at - u[low - 1]) / (u[low] - u[low - 1]);
        return x[low - 1] + t * (x[low] - x[low - 1]);
    }

    private static int RoundToInt(double value) => (int)Math.Round(value, MidpointRounding.AwayFromZero);

    // 발 하나를 그립니다. 영상 왼쪽이 윤곽의 왼쪽 끝(outline_left)입니다.
    private static void DrawFoot(PerfusionDetail d, int foot, int window, double top, int height, double xMin, int width,
                                 bool mirror, int xOffset, int imageWidth, byte[] pixels)
    {
        var rows = d.MapRows;
        var cols = d.MapCols;
        double Cell(int r, int c) =>
            r < 0 || r >= rows || c < 0 || c >= cols || d.MapValid[r * cols + c] == 0 ? double.NaN : d.MapAt(foot, window, r, c);
        for (var y = 0; y < height; ++y)
        {
            var u = (double)y / (height - 1);
            var a = Interpolate(d.OutlineU, d.OutlineLeft, u);
            var b = Interpolate(d.OutlineU, d.OutlineRight, u);
            for (var x = 0; x < width; ++x)
            {
                var across = xMin + (double)x / (height - 1);
                if (across < a || across > b || b <= a) continue;
                var fr = u * rows - 0.5;
                var fc = (across - a) / (b - a) * cols - 0.5;
                var nearR = Math.Clamp(RoundToInt(fr), 0, rows - 1);
                var nearC = Math.Clamp(RoundToInt(fc), 0, cols - 1);
                var color = GapColor;
                if (double.IsFinite(Cell(nearR, nearC)))
                {
                    // 이웃 네 칸 중 계산 칸만 무게를 다시 나눠 섞습니다. 계산하지 않는 칸 쪽으로 색이 번지지 않습니다.
                    var r0 = (int)Math.Floor(fr);
                    var c0 = (int)Math.Floor(fc);
                    var tr = fr - r0;
                    var tc = fc - c0;
                    double sum = 0;
                    double weight = 0;
                    foreach (var (r, c, w) in new[]
                             {
                                 (r0, c0, (1 - tr) * (1 - tc)),
                                 (r0, c0 + 1, (1 - tr) * tc),
                                 (r0 + 1, c0, tr * (1 - tc)),
                                 (r0 + 1, c0 + 1, tr * tc),
                             })
                    {
                        var v = Cell(r, c);
                        if (!double.IsFinite(v) || w <= 0) continue;
                        sum += v * w;
                        weight += w;
                    }
                    color = TurboColor(weight > 0 ? sum / weight : Cell(nearR, nearC), top);
                }
                var px = xOffset + (mirror ? width - 1 - x : x);
                var at = ((long)y * imageWidth + px) * 4;
                pixels[at] = color.R;
                pixels[at + 1] = color.G;
                pixels[at + 2] = color.B;
                pixels[at + 3] = 255;
            }
        }
    }
}
