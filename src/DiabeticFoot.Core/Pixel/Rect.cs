namespace DiabeticFoot.Core.Pixel;

/// <summary>프레임 안의 사각 영역입니다. 화소 단위이고 좌상단이 원점입니다.</summary>
public readonly record struct Rect(int X = 0, int Y = 0, int Width = 0, int Height = 0)
{
    /// <summary>폭이나 높이가 0 이하이면 true입니다.</summary>
    public bool IsEmpty => Width <= 0 || Height <= 0;

    /// <summary>비어 있지 않고 높이가 폭보다 크면 true입니다.</summary>
    /// <remarks>관류 분석은 발 하나씩 세로로 긴 영상만 받습니다. 좌우 분할 녹화 전에 두 영역을 이 속성으로 확인합니다.</remarks>
    public bool IsPortrait => !IsEmpty && Height > Width;
}

/// <summary>색차를 공유하는 화소 묶음의 크기입니다. 영역의 시작과 크기가 이 배수여야 성분이 밀리지 않습니다.</summary>
public sealed record ChromaGrid(int X = 1, int Y = 1);

public static partial class PixelFunctions
{
    /// <summary>화소 형식의 색차 격자를 구합니다.</summary>
    /// <param name="format">화소 형식</param>
    /// <returns>UYVY는 가로 2, NV12는 가로 2 세로 2, Y16은 1입니다</returns>
    public static ChromaGrid ChromaGridOf(PixelFormat format) => format switch
    {
        PixelFormat.Uyvy => new ChromaGrid(2, 1),
        PixelFormat.Nv12 => new ChromaGrid(2, 2),
        PixelFormat.Y16 => new ChromaGrid(1, 1),
        _ => new ChromaGrid(1, 1),
    };

    /// <summary>
    /// 영역을 프레임 안으로 줄이고 색차 격자에 맞춥니다.
    /// </summary>
    /// <param name="rect">원래 영역</param>
    /// <param name="format">화소 형식</param>
    /// <param name="frameWidth">프레임 폭</param>
    /// <param name="frameHeight">프레임 높이</param>
    /// <returns>맞춘 영역. 남는 것이 없으면 빈 영역입니다</returns>
    /// <remarks>시작은 올리고 끝은 내려서 원래 영역보다 커지지 않습니다.</remarks>
    public static Rect FitToGrid(Rect rect, PixelFormat format, int frameWidth, int frameHeight)
    {
        if (rect.IsEmpty || frameWidth <= 0 || frameHeight <= 0)
        {
            return default;
        }
        ChromaGrid grid = ChromaGridOf(format);
        // 프레임 경계로 먼저 자른 뒤 시작은 올리고 끝은 내려, 원래 영역 밖의 화소가 들어오지 않게 합니다.
        int left = RoundUp(Math.Clamp(rect.X, 0, frameWidth), grid.X);
        int top = RoundUp(Math.Clamp(rect.Y, 0, frameHeight), grid.Y);
        int right = RoundDown(Math.Clamp(rect.X + rect.Width, 0, frameWidth), grid.X);
        int bottom = RoundDown(Math.Clamp(rect.Y + rect.Height, 0, frameHeight), grid.Y);
        if (right <= left || bottom <= top)
        {
            return default;
        }
        return new Rect(left, top, right - left, bottom - top);
    }

    /// <summary>영역이 프레임 안에 있고 색차 격자에 맞는지 판정합니다.</summary>
    /// <returns>그대로 자를 수 있으면 true. 빈 영역이면 false입니다</returns>
    public static bool IsAligned(Rect rect, PixelFormat format, int frameWidth, int frameHeight) =>
        !rect.IsEmpty && FitToGrid(rect, format, frameWidth, frameHeight) == rect;

    private static int RoundUp(int value, int grid) => (value + grid - 1) / grid * grid;

    private static int RoundDown(int value, int grid) => value / grid * grid;
}
