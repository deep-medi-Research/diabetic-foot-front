namespace DiabeticFoot.Core.Pixel;

public static partial class PixelFunctions
{
    /// <summary>UYVY 행에서 화소 하나를 RGB로 바꿉니다(BT.601 제한 범위, 정수 계산).</summary>
    /// <param name="row">행의 첫 바이트부터의 바이트</param>
    /// <param name="x">화소 위치. 짝수와 홀수 화소가 같은 U, V를 씁니다</param>
    /// <returns>0~255로 자른 R, G, B</returns>
    /// <remarks>
    /// 미리보기(<see cref="UyvyToRgbPreview"/>)와 영역 RGB 합계(<see cref="MeasureRgb"/>)가 이 함수 하나를 써서,
    /// 화면의 색과 패널의 숫자가 같은 계산에서 나옵니다.
    /// </remarks>
    internal static Rgb8 UyvyPixelToRgb(ReadOnlySpan<byte> row, int x)
    {
        int pair = (x / 2) * 4;
        int c = (x % 2 == 0 ? row[pair + 1] : row[pair + 3]) - 16;
        int d = row[pair] - 128;
        int e = row[pair + 2] - 128;
        return new Rgb8(
            (byte)Math.Clamp((298 * c + 409 * e + 128) >> 8, 0, 255),
            (byte)Math.Clamp((298 * c - 100 * d - 208 * e + 128) >> 8, 0, 255),
            (byte)Math.Clamp((298 * c + 516 * d + 128) >> 8, 0, 255));
    }
}
