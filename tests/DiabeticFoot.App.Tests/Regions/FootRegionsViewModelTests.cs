using DiabeticFoot.App.Preferences;
using DiabeticFoot.App.Regions;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Pixel;

namespace DiabeticFoot.App.Tests.Regions;

public sealed class FootRegionsViewModelTests
{
    private readonly RememberedValues _remembered = RememberedValues.ThisRunOnly();

    private FootRegionsViewModel Rgb(int maxWidth = 0) => new("regions/rgb", PixelFormat.Uyvy, maxWidth, _remembered);

    // 프레임 크기를 알려 주기 전에는 쓸 수 없고, 알려 주면 가운데 세로선을 기준으로 같은 폭의 두 영역을 만듭니다.
    [Fact]
    public void DefaultsToHalvesOfTheFrame()
    {
        var regions = Rgb();
        Assert.False(regions.Ready);
        Assert.Null(regions.ReadyPair);
        Assert.Equal("", regions.Problem);

        regions.SetFrameSize(1280, 720);

        Assert.Equal(new Rect(0, 0, 640, 720), regions.Left);
        Assert.Equal(new Rect(640, 0, 640, 720), regions.Right);
        Assert.True(regions.Ready);
        Assert.True(regions.LeftOk);
        Assert.True(regions.RightOk);
        Assert.Equal((new Rect(0, 0, 640, 720), new Rect(640, 0, 640, 720)), regions.ReadyPair);
    }

    // 기본 영역 하나의 최대 폭을 주면 가운데에서 그만큼만 씁니다.
    [Fact]
    public void LimitsDefaultWidth()
    {
        var regions = Rgb(maxWidth: 400);

        regions.SetFrameSize(1280, 720);

        Assert.Equal(new Rect(240, 0, 400, 720), regions.Left);
        Assert.Equal(new Rect(640, 0, 400, 720), regions.Right);
    }

    // 상자는 크기를 지킨 채 프레임 안으로 당기고 색차 격자(UYVY는 가로 2화소)에 맞춥니다.
    [Fact]
    public void MovesBoxInsideFrameAndOntoGrid()
    {
        var regions = Rgb();
        regions.SetFrameSize(1280, 720);
        regions.SetRegions(new SideRegions(new Rect(0, 0, 300, 600), new Rect(600, 0, 300, 600)));

        regions.MoveBox(0, 101, 10);
        Assert.Equal(new Rect(100, 10, 300, 600), regions.Left);

        regions.MoveBox(1, 5000, 5000);
        Assert.Equal(new Rect(980, 120, 300, 600), regions.Right);

        regions.MoveBox(1, 5000, 5000);
        Assert.Equal(new Rect(980, 120, 300, 600), regions.Right);
        Assert.True(regions.Ready);
    }

    // 알림은 값이 바뀐 속성만 나갑니다. 왼쪽 상자만 옮기면 오른쪽 상자와 문제 문장은 알리지 않습니다.
    [Fact]
    public void NotifiesOnlyChangedProperties()
    {
        var regions = Rgb();
        regions.SetFrameSize(1280, 720);
        regions.SetRegions(new SideRegions(new Rect(0, 0, 300, 600), new Rect(600, 0, 300, 600)));
        var changed = new List<string?>();
        regions.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

        regions.MoveBox(0, 100, 10);

        Assert.Equal([nameof(FootRegionsViewModel.Left)], changed);
    }

    // 모서리를 옮기면 맞은편 모서리는 그대로이고, 최소 크기(32화소)보다 작아지지 않으며 격자에 맞춥니다.
    [Fact]
    public void ResizesFromCornerKeepingOppositeCorner()
    {
        var regions = Rgb();
        regions.SetFrameSize(1280, 720);
        regions.SetRegions(new SideRegions(new Rect(100, 100, 200, 400), new Rect(500, 100, 200, 400)));

        regions.ResizeBox(0, 3, 400, 500);
        Assert.Equal(new Rect(100, 100, 300, 400), regions.Left);

        regions.ResizeBox(0, 1, 301, 50);
        Assert.Equal(new Rect(100, 50, 200, 450), regions.Left);

        regions.ResizeBox(0, 0, 399, 499);
        Assert.Equal(new Rect(268, 468, 32, 32), regions.Left);
    }

    // 프레임 밖으로 끄는 모서리는 경계로 당깁니다.
    [Fact]
    public void ClampsDraggedCornerToFrame()
    {
        var regions = Rgb();
        regions.SetFrameSize(1280, 720);
        regions.SetRegions(new SideRegions(new Rect(100, 100, 200, 400), new Rect(500, 100, 200, 400)));

        regions.ResizeBox(1, 3, 9000, 9000);

        Assert.Equal(new Rect(500, 100, 780, 620), regions.Right);
    }

    // 잘못된 side나 corner, 프레임 크기를 모를 때의 호출은 아무것도 바꾸지 않습니다.
    [Fact]
    public void IgnoresInvalidArguments()
    {
        var regions = Rgb();
        regions.MoveBox(0, 10, 10);
        regions.ResizeBox(0, 0, 10, 10);
        regions.SetFrameSize(1280, 720);
        var before = regions.Regions;

        regions.MoveBox(2, 10, 10);
        regions.ResizeBox(0, 4, 10, 10);
        regions.ResizeBox(-1, 0, 10, 10);

        Assert.Equal(before, regions.Regions);
    }

    // 쓸 수 없는 영역은 이유를 문장으로 보입니다. 상자 이름은 환자의 어느 발인지에 따라 바뀝니다.
    [Fact]
    public void ExplainsEachProblemWithBoxNames()
    {
        var regions = Rgb();
        regions.SetFrameSize(1280, 720);
        regions.SetSideNames("오른발 상자", "왼발 상자");

        regions.SetRegions(new SideRegions(new Rect(640, 0, 640, 720), new Rect(0, 0, 640, 720)));
        Assert.Equal("오른발 상자가 왼발 상자보다 오른쪽에 있습니다", regions.Problem);
        Assert.False(regions.Ready);

        regions.SetRegions(new SideRegions(new Rect(0, 0, 640, 720), new Rect(600, 0, 640, 720)));
        Assert.Equal("두 상자가 같은 화소를 포함합니다", regions.Problem);
        Assert.False(regions.LeftOk);
        Assert.False(regions.RightOk);

        regions.SetRegions(new SideRegions(new Rect(0, 0, 400, 300), new Rect(600, 0, 640, 720)));
        Assert.Equal("오른발 상자가 세로로 길지 않습니다(폭 400, 높이 300)", regions.Problem);
        Assert.False(regions.LeftOk);
        Assert.True(regions.RightOk);

        regions.SetRegions(new SideRegions(new Rect(-10, 0, 640, 720), default));
        Assert.Equal("오른발 상자가 프레임 밖이거나 비어 있습니다\n왼발 상자가 프레임 밖이거나 비어 있습니다", regions.Problem);
    }

    // 상자 이름을 바꾸면 문제 문장도 바뀝니다.
    [Fact]
    public void RewritesProblemWhenNamesChange()
    {
        var regions = Rgb();
        regions.SetFrameSize(1280, 720);
        regions.SetRegions(new SideRegions(default, new Rect(640, 0, 640, 720)));
        Assert.Equal("왼쪽 상자가 프레임 밖이거나 비어 있습니다", regions.Problem);

        regions.SetSideNames("왼발 상자", "오른발 상자");

        Assert.Equal("왼발 상자가 프레임 밖이거나 비어 있습니다", regions.Problem);
    }

    // 저장한 영역은 같은 프레임 크기에서만 씁니다. 해상도가 바뀌면 같은 숫자가 다른 위치를 가리키므로 기본 영역을 씁니다.
    [Fact]
    public void ReusesSavedRegionsOnlyForSameFrameSize()
    {
        var first = Rgb();
        first.SetFrameSize(1280, 720);
        first.SetRegions(new SideRegions(new Rect(100, 50, 300, 600), new Rect(700, 50, 300, 600)));
        first.Save();

        var sameSize = Rgb();
        sameSize.SetFrameSize(1280, 720);
        Assert.Equal(new Rect(100, 50, 300, 600), sameSize.Left);
        Assert.Equal(new Rect(700, 50, 300, 600), sameSize.Right);

        var otherSize = Rgb();
        otherSize.SetFrameSize(1920, 1080);
        Assert.Equal(new Rect(0, 0, 960, 1080), otherSize.Left);
    }

    // 저장한 영역이 프레임 밖이면 안으로 당겨 쓰고, 영역이 비면 기본 영역으로 돌아갑니다.
    [Fact]
    public void ShiftsSavedRegionsInsideOrFallsBack()
    {
        _remembered.SetSize("regions/rgb/frame", 1280, 720);
        _remembered.SetRect("regions/rgb/left", new Rect(-50, 0, 300, 600));
        _remembered.SetRect("regions/rgb/right", new Rect(2000, 0, 300, 600));
        var shifted = Rgb();
        shifted.SetFrameSize(1280, 720);
        Assert.Equal(new Rect(0, 0, 300, 600), shifted.Left);
        Assert.Equal(new Rect(980, 0, 300, 600), shifted.Right);

        _remembered.SetRect("regions/rgb/left", new Rect(0, 0, 0, 0));
        var fallback = Rgb();
        fallback.SetFrameSize(1280, 720);
        Assert.Equal(new Rect(0, 0, 640, 720), fallback.Left);
    }

    // 열화상(Y16)은 격자가 1화소라 홀수 좌표도 그대로 씁니다.
    [Fact]
    public void ThermalGridKeepsOddCoordinates()
    {
        var regions = new FootRegionsViewModel("regions/thermal", PixelFormat.Y16, 0, _remembered);
        regions.SetFrameSize(640, 480);

        regions.MoveBox(0, 101, 0);

        Assert.Equal(new Rect(101, 0, 320, 480), regions.Left);
    }
}
