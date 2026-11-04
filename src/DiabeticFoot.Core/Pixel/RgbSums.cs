namespace DiabeticFoot.Core.Pixel;

/// <summary>R, G, B 채널별 값입니다. <c>this[0]</c>이 R, <c>this[1]</c>이 G, <c>this[2]</c>가 B입니다.</summary>
public readonly record struct RgbChannels(long R = 0, long G = 0, long B = 0)
{
    public long this[int channel] => channel switch
    {
        0 => R,
        1 => G,
        2 => B,
        _ => throw new IndexOutOfRangeException(),
    };
}

/// <summary>영역에서 고른 화소들의 R, G, B 합계와 채널별 포화 화소 수입니다.</summary>
public readonly record struct RgbSums
{
    /// <summary>읽은 화소 수입니다.</summary>
    public long PixelCount { get; init; }

    /// <summary>채널별 값의 합계입니다.</summary>
    public RgbChannels Sum { get; init; }

    /// <summary>채널 값이 255인 화소 수입니다.</summary>
    public RgbChannels Saturated { get; init; }
}

public static partial class PixelFunctions
{
    /// <summary>UYVY 영역의 채널별 RGB 합계와 포화 화소 수를 잽니다.</summary>
    /// <param name="uyvy">UYVY 형식 프레임</param>
    /// <param name="rect">잴 영역. 프레임 안이어야 합니다. 색차 격자에 맞지 않아도 됩니다</param>
    /// <param name="step">가로세로 몇 화소마다 하나씩 읽을지. 1이면 모든 화소</param>
    /// <returns>화소 수, 합계, 포화 화소 수. 형식이 UYVY가 아니거나 영역이 비었거나 프레임을 벗어나거나 step이 1보다 작으면 화소 수가 0입니다</returns>
    /// <remarks>
    /// 영역 왼쪽 위 화소부터 가로세로 step 화소마다 하나씩 읽고, 각 화소를 <see cref="UyvyToRgbPreview"/>와 같은 정수 계산(BT.601 제한 범위)으로
    /// RGB로 바꿉니다. 그래서 화면에 보이는 색과 이 값이 같은 계산에서 나옵니다.
    /// 화면 표시용입니다. 포화는 RGB로 바꾼 값 기준이라 밝기(Y)의 포화 화소(<see cref="MeasureBrightness"/>)와 수가 다를 수 있습니다.
    /// </remarks>
    public static RgbSums MeasureRgb(FrameView uyvy, Rect rect, int step)
    {
        if (uyvy.Format != PixelFormat.Uyvy || uyvy.Plane0.IsEmpty || step < 1 || rect.IsEmpty || rect.X < 0 ||
            rect.Y < 0 || rect.X + rect.Width > uyvy.Width || rect.Y + rect.Height > uyvy.Height)
        {
            return default;
        }
        ReadOnlySpan<byte> plane = uyvy.Plane0.Span;
        long pixelCount = 0;
        long sumR = 0, sumG = 0, sumB = 0;
        long saturatedR = 0, saturatedG = 0, saturatedB = 0;
        for (int y = rect.Y; y < rect.Y + rect.Height; y += step)
        {
            ReadOnlySpan<byte> row = plane[(y * uyvy.RowStride0)..];
            // 행 합계를 32비트로 모읍니다. 1920화소 x 255도 32비트 안이고, 수신 스레드에서 부르므로 64비트 덧셈을 줄입니다.
            int rowR = 0, rowG = 0, rowB = 0;
            int rowSaturatedR = 0, rowSaturatedG = 0, rowSaturatedB = 0;
            int count = 0;
            for (int x = rect.X; x < rect.X + rect.Width; x += step)
            {
                Rgb8 rgb = UyvyPixelToRgb(row, x);
                rowR += rgb.R;
                rowG += rgb.G;
                rowB += rgb.B;
                rowSaturatedR += rgb.R == 255 ? 1 : 0;
                rowSaturatedG += rgb.G == 255 ? 1 : 0;
                rowSaturatedB += rgb.B == 255 ? 1 : 0;
                ++count;
            }
            sumR += rowR;
            sumG += rowG;
            sumB += rowB;
            saturatedR += rowSaturatedR;
            saturatedG += rowSaturatedG;
            saturatedB += rowSaturatedB;
            pixelCount += count;
        }
        return new RgbSums
        {
            PixelCount = pixelCount,
            Sum = new RgbChannels(sumR, sumG, sumB),
            Saturated = new RgbChannels(saturatedR, saturatedG, saturatedB),
        };
    }
}
