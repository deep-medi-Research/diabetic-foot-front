namespace DiabeticFoot.Core.Pixel;

/// <summary>
/// 한 프레임 안에서 따로 저장할 좌우 영역 두 개입니다. 화면에서 사용자가 발 하나씩 정합니다.
/// </summary>
/// <remarks>
/// Left와 Right는 화소 기준(x가 작은 쪽이 Left)이고 환자의 발과는 무관합니다. 환자의 어느 발인지는
/// <c>Record.ShotInfo.PixelLeftFoot</c>가 정합니다.
/// </remarks>
/// <param name="Left">화소 왼쪽 영역입니다.</param>
/// <param name="Right">화소 오른쪽 영역입니다.</param>
public readonly record struct SideRegions(Rect Left = default, Rect Right = default);

/// <summary>좌우 영역 중 어느 쪽인지입니다.</summary>
public enum Side : byte
{
    /// <summary>화소 왼쪽입니다.</summary>
    Left,

    /// <summary>화소 오른쪽입니다.</summary>
    Right,
}

/// <summary>좌우 영역 검사에서 찾은 문제입니다.</summary>
public enum SideProblem : byte
{
    /// <summary>비었거나, 프레임 밖이거나, 색차 격자에 맞지 않습니다(<see cref="PixelFunctions.IsAligned"/>가 false).</summary>
    NotAligned,

    /// <summary>높이가 폭보다 크지 않습니다. 관류 분석은 세로로 긴 영상만 받습니다.</summary>
    NotPortrait,

    /// <summary>화소 왼쪽 영역의 가운데가 화소 오른쪽 영역의 가운데보다 왼쪽에 있지 않습니다.</summary>
    WrongOrder,

    /// <summary>두 영역이 같은 화소를 포함합니다.</summary>
    Intersecting,
}

/// <summary>좌우 영역 검사에서 찾은 문제 하나입니다.</summary>
/// <param name="Problem">문제의 종류입니다.</param>
/// <param name="Side">문제가 있는 영역입니다. 두 영역 사이의 문제(순서, 교차)는 Left와 Right를 하나씩 넣습니다.</param>
public readonly record struct SideFinding(SideProblem Problem = SideProblem.NotAligned, Side Side = Side.Left);

public static partial class PixelFunctions
{
    /// <summary>프레임 가운데 세로선에서 나눈 기본 좌우 영역을 만듭니다.</summary>
    /// <param name="frameWidth">프레임 폭</param>
    /// <param name="frameHeight">프레임 높이</param>
    /// <param name="maxWidth">영역 하나의 최대 폭. 0 이하이면 프레임 폭의 절반을 씁니다</param>
    /// <param name="format">화소 형식</param>
    /// <returns>좌우 영역. 프레임이 너무 작아 나눌 수 없으면 둘 다 빈 영역입니다</returns>
    /// <remarks>
    /// 두 영역은 가운데 세로선에 맞닿고 폭은 maxWidth와 프레임 폭의 절반 중 작은 값을 색차 격자에 맞춰 내린 값입니다.
    /// 높이는 프레임 높이 전체입니다. 1920x1200 UYVY에 maxWidth 640이면 (320, 0, 640, 1200)과 (960, 0, 640, 1200)입니다.
    /// </remarks>
    public static SideRegions DefaultSideRegions(int frameWidth, int frameHeight, int maxWidth, PixelFormat format)
    {
        ChromaGrid grid = ChromaGridOf(format);
        // 가운데 세로선이 격자에 맞아야 두 영역 모두 격자에 맞습니다.
        int center = RoundDown(frameWidth / 2, grid.X);
        int limit = maxWidth > 0 ? Math.Min(maxWidth, center) : center;
        int width = RoundDown(Math.Min(limit, frameWidth - center), grid.X);
        int height = RoundDown(frameHeight, grid.Y);
        if (width <= 0 || height <= 0) return default;
        return new SideRegions(new Rect(center - width, 0, width, height), new Rect(center, 0, width, height));
    }

    /// <summary>영역의 크기를 유지한 채 프레임 안으로 옮기고 색차 격자에 맞춥니다. 화면에서 영역을 끌어 옮길 때 씁니다.</summary>
    /// <param name="rect">옮길 영역</param>
    /// <param name="format">화소 형식</param>
    /// <param name="frameWidth">프레임 폭</param>
    /// <param name="frameHeight">프레임 높이</param>
    /// <returns>
    /// 프레임 안의 영역. 크기가 프레임보다 크면 프레임 크기로 줄이고, 시작과 크기는 격자의 배수로 내립니다.
    /// rect가 비었거나 프레임이 비면 빈 영역입니다
    /// </returns>
    public static Rect ShiftInside(Rect rect, PixelFormat format, int frameWidth, int frameHeight)
    {
        if (rect.IsEmpty || frameWidth <= 0 || frameHeight <= 0) return default;
        ChromaGrid grid = ChromaGridOf(format);
        int maxW = RoundDown(frameWidth, grid.X);
        int maxH = RoundDown(frameHeight, grid.Y);
        int width = RoundDown(Math.Min(rect.Width, maxW), grid.X);
        int height = RoundDown(Math.Min(rect.Height, maxH), grid.Y);
        if (width <= 0 || height <= 0) return default;
        // 끝을 먼저 프레임 안으로 당긴 뒤 시작을 격자에 맞춰 내리므로 크기는 바뀌지 않고 프레임 밖으로 나가지 않습니다.
        int x = RoundDown(Math.Clamp(rect.X, 0, maxW - width), grid.X);
        int y = RoundDown(Math.Clamp(rect.Y, 0, maxH - height), grid.Y);
        return new Rect(x, y, width, height);
    }

    /// <summary>좌우 영역을 녹화에 쓸 수 있는지 검사합니다.</summary>
    /// <param name="regions">좌우 영역</param>
    /// <param name="format">화소 형식</param>
    /// <param name="frameWidth">프레임 폭</param>
    /// <param name="frameHeight">프레임 높이</param>
    /// <returns>찾은 문제 목록. 비어 있으면 녹화에 쓸 수 있습니다</returns>
    /// <remarks>
    /// 검사 순서: 영역마다 격자 맞춤(<see cref="SideProblem.NotAligned"/>)과 세로형(<see cref="SideProblem.NotPortrait"/>),
    /// 그 다음 두 영역의 순서(<see cref="SideProblem.WrongOrder"/>)와 교차(<see cref="SideProblem.Intersecting"/>)입니다.
    /// </remarks>
    public static IReadOnlyList<SideFinding> CheckSideRegions(SideRegions regions, PixelFormat format, int frameWidth, int frameHeight)
    {
        var findings = new List<SideFinding>();
        foreach (var (side, rect) in new[] { (Side.Left, regions.Left), (Side.Right, regions.Right) })
        {
            if (!IsAligned(rect, format, frameWidth, frameHeight)) findings.Add(new SideFinding(SideProblem.NotAligned, side));
            if (!rect.IsPortrait) findings.Add(new SideFinding(SideProblem.NotPortrait, side));
        }
        if (regions.Left.IsEmpty || regions.Right.IsEmpty) return findings;
        // 가운데를 비교하는 이유: 영역 크기가 달라도 화면에서 보이는 좌우 순서와 같은 결과를 내기 위함입니다.
        if (2 * regions.Left.X + regions.Left.Width >= 2 * regions.Right.X + regions.Right.Width)
        {
            findings.Add(new SideFinding(SideProblem.WrongOrder, Side.Left));
            findings.Add(new SideFinding(SideProblem.WrongOrder, Side.Right));
        }
        if (Intersects(regions.Left, regions.Right))
        {
            findings.Add(new SideFinding(SideProblem.Intersecting, Side.Left));
            findings.Add(new SideFinding(SideProblem.Intersecting, Side.Right));
        }
        return findings;
    }

    private static bool Intersects(Rect a, Rect b) =>
        a.X < b.X + b.Width && b.X < a.X + a.Width && a.Y < b.Y + b.Height && b.Y < a.Y + a.Height;
}
