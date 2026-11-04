using DiabeticFoot.Core.Pixel;
using static DiabeticFoot.Core.Pixel.PixelFunctions;

namespace DiabeticFoot.Core.Tests.Pixel;

public class SideRegionsTests
{
    // 1920x1200(센서 전체) 기본 영역은 높이만 1200이고 위치와 폭은 1080p와 같습니다.
    [Fact]
    public void DefaultsAtFullSensor()
    {
        SideRegions regions = DefaultSideRegions(1920, 1200, 640, PixelFormat.Uyvy);
        Assert.Equal(new Rect(320, 0, 640, 1200), regions.Left);
        Assert.Equal(new Rect(960, 0, 640, 1200), regions.Right);
        Assert.Empty(CheckSideRegions(regions, PixelFormat.Uyvy, 1920, 1200));
    }

    // 1080p 기본 영역은 가운데 세로선에 맞닿은 폭 640, 높이 1080의 세로형 두 칸입니다.
    [Fact]
    public void DefaultSplitsAtCenterWithMaxWidth()
    {
        SideRegions regions = DefaultSideRegions(1920, 1080, 640, PixelFormat.Uyvy);

        Assert.Equal(new Rect(320, 0, 640, 1080), regions.Left);
        Assert.Equal(new Rect(960, 0, 640, 1080), regions.Right);
        Assert.Empty(CheckSideRegions(regions, PixelFormat.Uyvy, 1920, 1080));
    }

    // 최대 폭을 주지 않으면 프레임을 가운데에서 반씩 나눕니다(열화상 기본값).
    [Fact]
    public void DefaultWithoutMaxWidthUsesHalves()
    {
        SideRegions regions = DefaultSideRegions(640, 480, 0, PixelFormat.Y16);

        Assert.Equal(new Rect(0, 0, 320, 480), regions.Left);
        Assert.Equal(new Rect(320, 0, 320, 480), regions.Right);
    }

    // UYVY에서 가운데 세로선과 폭은 짝수로 내립니다.
    [Fact]
    public void DefaultAlignsCenterToChromaGrid()
    {
        SideRegions regions = DefaultSideRegions(1922, 1080, 641, PixelFormat.Uyvy);

        Assert.Equal(new Rect(320, 0, 640, 1080), regions.Left);
        Assert.Equal(new Rect(960, 0, 640, 1080), regions.Right);
    }

    // 나눌 수 없는 프레임은 빈 영역입니다.
    [Fact]
    public void DefaultOfTinyFrameIsEmpty()
    {
        Assert.Equal(new SideRegions(), DefaultSideRegions(1, 10, 0, PixelFormat.Y16));
    }

    // 프레임 밖으로 끈 영역은 크기를 유지한 채 안으로 들어오고, UYVY의 x는 짝수로 내립니다.
    [Fact]
    public void ShiftInsideKeepsSizeAndAlignment()
    {
        Assert.Equal(new Rect(0, 0, 640, 1080), ShiftInside(new Rect(-50, -3, 640, 1080), PixelFormat.Uyvy, 1920, 1080));
        Assert.Equal(new Rect(1280, 0, 640, 1080), ShiftInside(new Rect(1500, 10, 640, 1080), PixelFormat.Uyvy, 1920, 1080));
        Assert.Equal(new Rect(100, 7, 300, 500), ShiftInside(new Rect(101, 7, 301, 500), PixelFormat.Uyvy, 1920, 1080));
    }

    // 프레임보다 큰 영역은 프레임 크기로 줄이고, 빈 영역은 빈 영역으로 둡니다.
    [Fact]
    public void ShiftInsideClampsOversizeAndEmpty()
    {
        Assert.Equal(new Rect(0, 0, 1920, 1080), ShiftInside(new Rect(0, 0, 4000, 2000), PixelFormat.Uyvy, 1920, 1080));
        Assert.Equal(new Rect(), ShiftInside(new Rect(5, 5, 0, 10), PixelFormat.Uyvy, 1920, 1080));
    }

    // 가로로 긴 영역은 세로형이 아니라는 문제로 알립니다.
    [Fact]
    public void LandscapeRegionIsReported()
    {
        var regions = new SideRegions(new Rect(0, 0, 800, 600), new Rect(960, 0, 640, 1080));

        var findings = CheckSideRegions(regions, PixelFormat.Uyvy, 1920, 1080);

        Assert.Equal(new[] { new SideFinding(SideProblem.NotPortrait, Side.Left) }, findings);
    }

    // x가 홀수이거나 프레임 밖인 영역은 격자 문제로 알립니다.
    [Fact]
    public void MisalignedOrOutsideRegionIsReported()
    {
        var regions = new SideRegions(new Rect(1, 0, 640, 1080), new Rect(1300, 0, 640, 1080));

        var findings = CheckSideRegions(regions, PixelFormat.Uyvy, 1920, 1080);

        Assert.Equal(
            new[]
            {
                new SideFinding(SideProblem.NotAligned, Side.Left),
                new SideFinding(SideProblem.NotAligned, Side.Right),
            },
            findings);
    }

    // 화소 왼쪽 영역이 오른쪽 영역보다 오른쪽에 있으면 순서 문제이고, 같은 화소를 포함하면 교차 문제입니다.
    [Fact]
    public void OrderAndIntersectionAreReported()
    {
        var swapped = new SideRegions(new Rect(960, 0, 640, 1080), new Rect(320, 0, 640, 1080));
        Assert.Equal(
            new[]
            {
                new SideFinding(SideProblem.WrongOrder, Side.Left),
                new SideFinding(SideProblem.WrongOrder, Side.Right),
            },
            CheckSideRegions(swapped, PixelFormat.Uyvy, 1920, 1080));

        var crossing = new SideRegions(new Rect(320, 0, 642, 1080), new Rect(960, 0, 640, 1080));
        Assert.Equal(
            new[]
            {
                new SideFinding(SideProblem.Intersecting, Side.Left),
                new SideFinding(SideProblem.Intersecting, Side.Right),
            },
            CheckSideRegions(crossing, PixelFormat.Uyvy, 1920, 1080));
    }

    // 맞닿은 두 영역(기본값)은 교차가 아닙니다.
    [Fact]
    public void TouchingRegionsDoNotIntersect()
    {
        var regions = new SideRegions(new Rect(0, 0, 320, 480), new Rect(320, 0, 320, 480));

        Assert.Empty(CheckSideRegions(regions, PixelFormat.Y16, 640, 480));
    }
}
