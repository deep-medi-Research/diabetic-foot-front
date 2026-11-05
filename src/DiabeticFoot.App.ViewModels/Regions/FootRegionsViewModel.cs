using CommunityToolkit.Mvvm.ComponentModel;
using DiabeticFoot.App.Preferences;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Pixel;

namespace DiabeticFoot.App.Regions;

/// <summary>
/// 미리보기 한 장 위의 좌우 발 영역 두 개의 ViewModel입니다. RGB와 열화상이 하나씩 갖습니다.
/// 좌표는 프레임 화소 기준이고, 이동과 크기 조절 결과를 core(<see cref="PixelFunctions.ShiftInside"/>, <see cref="PixelFunctions.FitToGrid"/>)로
/// 프레임 안과 색차 격자에 맞춥니다. 녹화에 쓸 수 있는지는 <see cref="PixelFunctions.CheckSideRegions"/>가 정하고 이 클래스는 그 결과를 문장으로 바꿉니다.
/// </summary>
/// <remarks>영역은 화소 기준(왼쪽 영역이 화소 왼쪽)입니다. 환자의 어느 발인지는 촬영 쪽이 정하고 <see cref="SetSideNames"/>로 알려 줍니다.</remarks>
public sealed class FootRegionsViewModel : ObservableObject
{
    // 상자를 줄일 수 있는 최소 크기(프레임 화소)입니다. 모서리 둘이 한 점에 모여 다시 잡을 수 없게 되는 것을 막습니다.
    private const int MinSide = 32;

    private readonly string _settingsKey;
    private readonly PixelFormat _format;
    private readonly int _defaultMaxWidth;
    private readonly RememberedValues _remembered;

    private int _frameWidth;
    private int _frameHeight;
    private SideRegions _regions;
    private string _leftName = "왼쪽 상자";
    private string _rightName = "오른쪽 상자";
    private bool _leftOk;
    private bool _rightOk;
    private bool _ready;
    private string _problem = "";

    /// <param name="settingsKey">이 영역을 기억하는 이름(예: "regions/rgb")</param>
    /// <param name="format">프레임 화소 형식. 색차 격자를 정합니다</param>
    /// <param name="defaultMaxWidth">기본 영역 하나의 최대 폭. 0이면 프레임 폭의 절반</param>
    /// <param name="remembered">지난 영역을 읽고 저장한 영역을 기억합니다</param>
    public FootRegionsViewModel(string settingsKey, PixelFormat format, int defaultMaxWidth, RememberedValues remembered)
    {
        _settingsKey = settingsKey;
        _format = format;
        _defaultMaxWidth = defaultMaxWidth;
        _remembered = remembered;
    }

    public int FrameWidth
    {
        get => _frameWidth;
        private set => SetProperty(ref _frameWidth, value);
    }

    public int FrameHeight
    {
        get => _frameHeight;
        private set => SetProperty(ref _frameHeight, value);
    }

    /// <summary>화소 왼쪽 영역입니다.</summary>
    public Rect Left => _regions.Left;

    /// <summary>화소 오른쪽 영역입니다.</summary>
    public Rect Right => _regions.Right;

    public bool LeftOk
    {
        get => _leftOk;
        private set => SetProperty(ref _leftOk, value);
    }

    public bool RightOk
    {
        get => _rightOk;
        private set => SetProperty(ref _rightOk, value);
    }

    /// <summary>프레임 크기를 알고 두 영역이 모두 검사를 통과했으면 true입니다.</summary>
    public bool Ready
    {
        get => _ready;
        private set => SetProperty(ref _ready, value);
    }

    /// <summary>영역이 녹화에 쓸 수 없는 이유입니다. 줄바꿈으로 나뉘고, 문제가 없으면 빈 문자열입니다.</summary>
    public string Problem
    {
        get => _problem;
        private set => SetProperty(ref _problem, value);
    }

    /// <summary>지금 영역입니다(화소 왼쪽, 화소 오른쪽).</summary>
    public SideRegions Regions => _regions;

    /// <summary>녹화에 쓸 영역입니다. 프레임 크기를 모르거나 검사를 통과하지 않으면 null입니다.</summary>
    public (Rect Left, Rect Right)? ReadyPair => Ready ? (_regions.Left, _regions.Right) : null;

    /// <summary>
    /// 프레임 크기를 정합니다. 크기가 바뀌면 저장해 둔 영역(같은 프레임 크기일 때)이나 기본 영역을 씁니다.
    /// </summary>
    public void SetFrameSize(int width, int height)
    {
        if (width == _frameWidth && height == _frameHeight) return;
        FrameWidth = width;
        FrameHeight = height;
        var regions = PixelFunctions.DefaultSideRegions(width, height, _defaultMaxWidth, _format);
        // 저장한 영역은 같은 프레임 크기에서만 씁니다. 해상도가 바뀌면 같은 숫자가 다른 위치를 가리키기 때문입니다.
        if (_remembered.GetSize(_settingsKey + "/frame") == (width, height))
        {
            var left = PixelFunctions.ShiftInside(_remembered.GetRect(_settingsKey + "/left") ?? default, _format, width, height);
            var right = PixelFunctions.ShiftInside(_remembered.GetRect(_settingsKey + "/right") ?? default, _format, width, height);
            if (!left.IsEmpty && !right.IsEmpty) regions = new SideRegions(left, right);
        }
        Apply(regions);
    }

    /// <summary>영역을 직접 정합니다. 명령줄 시험 옵션에 씁니다. 격자 맞춤과 저장은 하지 않고 검사만 합니다.</summary>
    public void SetRegions(SideRegions regions) => Apply(regions);

    /// <summary>문제 문장에 쓸 상자 이름입니다(예: "오른발 상자"). 화소 왼쪽 발이 바뀔 때 부릅니다.</summary>
    public void SetSideNames(string left, string right)
    {
        if (left == _leftName && right == _rightName) return;
        _leftName = left;
        _rightName = right;
        Apply(_regions);
    }

    /// <summary>상자를 크기 그대로 옮깁니다. 프레임 밖으로 나가면 안으로 당기고 격자에 맞춥니다.</summary>
    /// <param name="side">0이면 화소 왼쪽 상자, 1이면 화소 오른쪽 상자</param>
    /// <param name="x">원하는 왼쪽 위 x(프레임 화소)</param>
    /// <param name="y">원하는 왼쪽 위 y(프레임 화소)</param>
    public void MoveBox(int side, int x, int y)
    {
        if (_frameWidth <= 0 || side is not (0 or 1)) return;
        var rect = side == 0 ? _regions.Left : _regions.Right;
        var moved = PixelFunctions.ShiftInside(new Rect(x, y, rect.Width, rect.Height), _format, _frameWidth, _frameHeight);
        if (moved == rect || moved.IsEmpty) return;
        Apply(Replace(side, moved));
    }

    /// <summary>상자의 모서리 하나를 옮겨 크기를 바꿉니다. 맞은편 모서리는 그대로 둡니다.</summary>
    /// <param name="side">0이면 화소 왼쪽 상자, 1이면 화소 오른쪽 상자</param>
    /// <param name="corner">0 왼쪽 위, 1 오른쪽 위, 2 왼쪽 아래, 3 오른쪽 아래</param>
    /// <param name="x">모서리의 새 x(프레임 화소). 프레임 밖이면 경계로 당깁니다</param>
    /// <param name="y">모서리의 새 y(프레임 화소)</param>
    public void ResizeBox(int side, int corner, int x, int y)
    {
        if (_frameWidth <= 0 || side is not (0 or 1) || corner is < 0 or > 3) return;
        var rect = side == 0 ? _regions.Left : _regions.Right;
        var movesLeft = corner is 0 or 2;
        var movesTop = corner is 0 or 1;
        // 맞은편 모서리를 고정하고, 움직이는 모서리는 프레임 안과 최소 크기 안으로 당깁니다.
        var fixedX = movesLeft ? rect.X + rect.Width : rect.X;
        var fixedY = movesTop ? rect.Y + rect.Height : rect.Y;
        var newX = movesLeft ? Math.Clamp(x, 0, Math.Max(0, fixedX - MinSide))
                             : Math.Clamp(x, Math.Min(_frameWidth, fixedX + MinSide), _frameWidth);
        var newY = movesTop ? Math.Clamp(y, 0, Math.Max(0, fixedY - MinSide))
                            : Math.Clamp(y, Math.Min(_frameHeight, fixedY + MinSide), _frameHeight);
        var wanted = new Rect(Math.Min(fixedX, newX), Math.Min(fixedY, newY), Math.Abs(newX - fixedX), Math.Abs(newY - fixedY));
        var fitted = PixelFunctions.FitToGrid(wanted, _format, _frameWidth, _frameHeight);
        if (fitted == rect || fitted.IsEmpty) return;
        Apply(Replace(side, fitted));
    }

    /// <summary>지금 영역을 다음 실행에 남깁니다. 끌기를 마칠 때 부릅니다.</summary>
    public void Save()
    {
        if (_frameWidth <= 0) return;
        _remembered.SetSize(_settingsKey + "/frame", _frameWidth, _frameHeight);
        _remembered.SetRect(_settingsKey + "/left", _regions.Left);
        _remembered.SetRect(_settingsKey + "/right", _regions.Right);
    }

    private SideRegions Replace(int side, Rect rect) => side == 0 ? _regions with { Left = rect } : _regions with { Right = rect };

    // 영역과 검사 결과를 한 번에 바꾸고, 값이 달라진 속성만 알립니다.
    private void Apply(SideRegions regions)
    {
        var leftChanged = regions.Left != _regions.Left;
        var rightChanged = regions.Right != _regions.Right;
        _regions = regions;
        if (leftChanged) OnPropertyChanged(nameof(Left));
        if (rightChanged) OnPropertyChanged(nameof(Right));

        var findings = PixelFunctions.CheckSideRegions(_regions, _format, _frameWidth, _frameHeight);
        bool Has(SideProblem problem, Side side) => findings.Contains(new SideFinding(problem, side));
        var lines = new List<string>();
        foreach (var (side, name, rect) in new[] { (Side.Left, _leftName, _regions.Left), (Side.Right, _rightName, _regions.Right) })
        {
            if (Has(SideProblem.NotAligned, side))
            {
                lines.Add($"{name}가 프레임 밖이거나 비어 있습니다");
            }
            else if (Has(SideProblem.NotPortrait, side))
            {
                lines.Add($"{name}가 세로로 길지 않습니다(폭 {rect.Width}, 높이 {rect.Height})");
            }
        }
        if (Has(SideProblem.WrongOrder, Side.Left)) lines.Add($"{_leftName}가 {_rightName}보다 오른쪽에 있습니다");
        if (Has(SideProblem.Intersecting, Side.Left)) lines.Add("두 상자가 같은 화소를 포함합니다");
        LeftOk = !findings.Any(f => f.Side == Side.Left);
        RightOk = !findings.Any(f => f.Side == Side.Right);
        Problem = _frameWidth > 0 ? string.Join("\n", lines) : "";
        Ready = _frameWidth > 0 && LeftOk && RightOk && Problem.Length == 0;
    }
}
