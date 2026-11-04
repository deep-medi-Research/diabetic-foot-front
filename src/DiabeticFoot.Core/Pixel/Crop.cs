namespace DiabeticFoot.Core.Pixel;

public static partial class PixelFunctions
{
    /// <summary>영역을 행 사이 빈 바이트 없이 담을 때의 크기를 구합니다.</summary>
    /// <param name="format">화소 형식</param>
    /// <param name="rect">영역</param>
    /// <returns>바이트 수. NV12는 Y 평면 뒤에 UV 평면이 붙은 크기입니다. 영역이 비었으면 0입니다</returns>
    public static int PackedBytes(PixelFormat format, Rect rect)
    {
        if (rect.IsEmpty)
        {
            return 0;
        }
        int pixels = rect.Width * rect.Height;
        if (format == PixelFormat.Nv12)
        {
            return pixels + pixels / 2;
        }
        return pixels * BytesPerPixel(format);
    }

    /// <summary>영역을 잘라 행 사이 빈 바이트 없이 복사합니다.</summary>
    /// <param name="frame">원본 프레임</param>
    /// <param name="rect">자를 영역. <see cref="IsAligned"/>를 통과해야 합니다</param>
    /// <param name="output">결과. 크기가 <see cref="PackedBytes"/>와 같아야 합니다</param>
    /// <returns>복사했으면 true. 영역이 격자에 맞지 않거나 output 크기가 다르면 쓰지 않고 false입니다</returns>
    /// <remarks>색차 격자에 맞지 않는 UYVY 영역을 자르면 U와 V가 뒤바뀌므로 거부합니다.</remarks>
    public static bool CopyRect(FrameView frame, Rect rect, Span<byte> output)
    {
        if (frame.Plane0.IsEmpty || !IsAligned(rect, frame.Format, frame.Width, frame.Height) ||
            output.Length != PackedBytes(frame.Format, rect))
        {
            return false;
        }
        int rowBytes = rect.Width * BytesPerPixel(frame.Format);
        int xBytes = rect.X * BytesPerPixel(frame.Format);
        if (frame.Format != PixelFormat.Nv12)
        {
            CopyRows(frame.Plane0.Span, frame.RowStride0, rect.Y, xBytes, rect.Height, rowBytes, output);
            return true;
        }
        if (frame.Plane1.IsEmpty)
        {
            return false;
        }
        CopyRows(frame.Plane0.Span, frame.RowStride0, rect.Y, xBytes, rect.Height, rowBytes, output);
        // UV 평면은 2x2 화소마다 U, V 한 쌍이라 행 수는 절반이고 한 행의 바이트 수는 Y와 같습니다.
        CopyRows(frame.Plane1.Span, frame.RowStride1, rect.Y / 2, xBytes, rect.Height / 2, rowBytes, output[(rowBytes * rect.Height)..]);
        return true;
    }

    /// <summary>
    /// 영역을 가운데 세로선에서 폭이 같은 두 영역으로 나눕니다. 두 발을 따로 저장하기 위한 것입니다.
    /// </summary>
    /// <param name="rect">나눌 영역. <see cref="FitToGrid"/>를 거친 값입니다</param>
    /// <param name="format">화소 형식</param>
    /// <param name="widthStep">두 영역의 폭이 맞춰야 할 배수. 색차 격자의 배수여야 합니다</param>
    /// <returns>왼쪽과 오른쪽 영역. 나눌 수 없으면 둘 다 빈 영역입니다</returns>
    /// <remarks>가운데 선은 옮기지 않고 폭을 안쪽으로만 줄입니다. 사용자가 정한 영역 밖 화소가 저장 파일에 들어가지 않게 하기 위해서입니다.</remarks>
    public static (Rect Left, Rect Right) SplitLeftRight(Rect rect, PixelFormat format, int widthStep)
    {
        ChromaGrid grid = ChromaGridOf(format);
        if (rect.IsEmpty || widthStep <= 0 || widthStep % grid.X != 0)
        {
            return default;
        }
        // 가운데 선이 두 발의 경계라, 폭을 줄일 때도 가운데 선은 옮기지 않습니다.
        int middle = (rect.X + rect.Width / 2) / grid.X * grid.X;
        int room = Math.Min(middle - rect.X, rect.X + rect.Width - middle);
        int halfWidth = room / widthStep * widthStep;
        if (halfWidth <= 0)
        {
            return default;
        }
        return (new Rect(middle - halfWidth, rect.Y, halfWidth, rect.Height),
                new Rect(middle, rect.Y, halfWidth, rect.Height));
    }

    private static int BytesPerPixel(PixelFormat format) => format switch
    {
        PixelFormat.Uyvy => 2,
        PixelFormat.Y16 => 2,
        PixelFormat.Nv12 => 1,
        _ => 0,
    };

    private static void CopyRows(
        ReadOnlySpan<byte> source, int sourceStride, int firstRow, int byteOffset, int rows, int rowBytes, Span<byte> output)
    {
        for (int r = 0; r < rows; ++r)
        {
            source.Slice((firstRow + r) * sourceStride + byteOffset, rowBytes).CopyTo(output[(r * rowBytes)..]);
        }
    }
}
