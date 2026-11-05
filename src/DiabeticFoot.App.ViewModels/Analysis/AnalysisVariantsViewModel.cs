using CommunityToolkit.Mvvm.ComponentModel;
using DiabeticFoot.App.Preferences;

namespace DiabeticFoot.App.Analysis;

/// <summary>
/// 연구 코드 판마다 하나씩 띄운 관류 분석(<see cref="AnalysisViewModel"/>)의 묶음입니다. 촬영 화면의 분석 칸과 판 고르기가 이 객체를 봅니다.
/// <list type="bullet">
/// <item>촬영 하나를 켜 둔 판 모두로 분석합니다. 판 순서대로 분석하고, 앞 판의 분석이 끝나야 다음 판에 보냅니다(<see cref="VariantHolds"/>)</item>
/// <item>녹화 중 미리 분석(첫 장 발 분할, 영상 미리 읽기)은 켜 둔 판 모두에 보냅니다</item>
/// <item>켜 둔 판과 미리 분석 스위치는 기억한 값에 남깁니다. 마지막으로 켜 둔 판은 끌 수 없습니다</item>
/// </list>
/// </summary>
public sealed class AnalysisVariantsViewModel : ObservableObject, IDisposable
{
    // 녹화 중 미리 분석 스위치와 판별 사용 여부를 기억하는 이름입니다. 미리 분석 이름은 판을 나누기 전과 같습니다.
    public const string PrestreamKey = "analysis/prestream";

    private readonly List<AnalysisViewModel> _lanes;
    private readonly bool _prestreamAvailable;
    private bool _prestream = true;
    private bool _recording;
    private RememberedValues? _remembered;
    private bool _blocksShot;
    private string _remainingText = "";
    private int _enabledCount;
    // 마지막 촬영의 판별 복사 문장입니다. 촬영이 바뀌면 비웁니다.
    private string _collectFolder = "";
    private readonly SortedDictionary<string, string> _collectTexts = new(StringComparer.Ordinal);

    /// <param name="lanes">판마다 하나씩 만든 관류 분석. 이 객체가 가집니다. 판 이름 순으로 줍니다</param>
    /// <param name="prestreamAvailable">관류 분석 프로그램을 `--presegment-threads`, `--prestream`과 함께 띄웠으면 true</param>
    public AnalysisVariantsViewModel(IEnumerable<AnalysisViewModel> lanes, bool prestreamAvailable)
    {
        _lanes = lanes.ToList();
        _prestreamAvailable = prestreamAvailable;
        foreach (var lane in _lanes)
        {
            lane.SetPrestream(_prestreamAvailable && _prestream);
            lane.PropertyChanged += OnLanePropertyChanged;
            lane.Finished += (_, e) => Finished?.Invoke(this, new VariantFinishedEventArgs(e.Folder, e.Ok, e.SummaryPath, lane.VariantId));
            lane.FiguresReady += (_, folder) => FiguresReady?.Invoke(this, new VariantFiguresEventArgs(folder, lane.VariantId));
            lane.Collected += (_, e) => OnLaneCollected(lane, e);
        }
        Refresh();
    }

    /// <summary>판 하나가 촬영 하나의 분석을 끝냈습니다.</summary>
    public event EventHandler<VariantFinishedEventArgs>? Finished;

    /// <summary>판 하나의 그림과 관류 지도 GIF를 다 그렸습니다(<see cref="AnalysisViewModel.FiguresReady"/>).</summary>
    public event EventHandler<VariantFiguresEventArgs>? FiguresReady;

    /// <summary>판 하나의 분석결과 폴더 복사가 끝났습니다. 문장은 이 촬영의 판별 복사 결과를 판 이름과 함께 이은 것입니다.</summary>
    public event EventHandler<VariantCollectedEventArgs>? Collected;

    /// <summary>판별 관류 분석 목록입니다. 판 이름 순입니다.</summary>
    public IReadOnlyList<AnalysisViewModel> Lanes => _lanes;

    /// <summary>켜 둔 판 중 하나라도 분석 중이거나 대기열에 촬영이 있으면 true입니다. 촬영 버튼을 막는 데 씁니다.</summary>
    public bool BlocksShot
    {
        get => _blocksShot;
        private set => SetProperty(ref _blocksShot, value);
    }

    /// <summary>분석 중인 판의 남은 시간 문장("v7 약 5초 남음")입니다. 분석 중인 판이 없으면 비어 있습니다.</summary>
    public string RemainingText
    {
        get => _remainingText;
        private set => SetProperty(ref _remainingText, value);
    }

    /// <summary>켜 둔 판 수입니다.</summary>
    public int EnabledCount
    {
        get => _enabledCount;
        private set => SetProperty(ref _enabledCount, value);
    }

    /// <summary>녹화 중 미리 분석을 쓸 수 있는지입니다. `--no-prestream`으로 띄우면 false이고 화면에서 스위치를 숨깁니다.</summary>
    public bool PrestreamAvailable => _prestreamAvailable;

    /// <summary>
    /// 녹화 중 미리 분석 스위치입니다. 켜면 녹화가 시작될 때 켜 둔 판 모두에 첫 장 발 분할과 영상 미리 읽기를 요청합니다.
    /// 기본은 켬이고 기억한 값에 남깁니다. 다음 녹화부터 적용합니다.
    /// </summary>
    /// <remarks>노트북 실측(2026-10-27, 촬영 13건, 판 하나): 녹화가 끝난 뒤 분석 완료까지 5.3~14.7초, 끄면 약 27초입니다.</remarks>
    public bool Prestream
    {
        get => _prestream;
        set
        {
            if (!_prestreamAvailable || value == _prestream) return;
            _prestream = value;
            foreach (var lane in _lanes) lane.SetPrestream(value);
            _remembered?.SetBool(PrestreamKey, value);
            OnPropertyChanged();
        }
    }

    /// <summary>
    /// 판마다 분석 프로그램을 보류할지 정합니다.
    /// </summary>
    /// <param name="recording">안정화나 녹화 중이면 true. 이때는 모든 판을 보류합니다</param>
    /// <param name="enabled">판마다 켜 두었는지(판 순서)</param>
    /// <param name="active">판마다 분석 중이거나 대기열에 촬영이 있는지(판 순서)</param>
    /// <returns>판마다 보류 여부. 앞쪽에 켜 둔 판 중 분석 중이거나 대기 중인 판이 있으면 그 뒤의 판을 보류합니다</returns>
    /// <remarks>판을 차례로 돌려, 녹화가 끝난 뒤 첫 판의 결과가 두 판을 함께 돌릴 때보다 먼저 나오게 합니다.</remarks>
    public static IReadOnlyList<bool> VariantHolds(bool recording, IReadOnlyList<bool> enabled, IReadOnlyList<bool> active)
    {
        var holds = new bool[enabled.Count];
        var earlierActive = false;
        for (var i = 0; i < enabled.Count; i++)
        {
            holds[i] = recording || earlierActive;
            if (enabled[i] && i < active.Count && active[i]) earlierActive = true;
        }
        return holds;
    }

    /// <summary>판 이름 목록(판 순서)입니다.</summary>
    public IReadOnlyList<string> Ids => _lanes.Select(l => l.VariantId).ToList();

    /// <summary>판 이름으로 찾은 관류 분석입니다. 없으면 null입니다.</summary>
    public AnalysisViewModel? Lane(string id) => _lanes.Find(l => l.VariantId == id);

    /// <summary>판 이름의 화면 이름입니다. 없으면 id를 그대로 돌려줍니다.</summary>
    public string LabelOf(string id) => Lane(id)?.VariantLabel ?? id;

    /// <summary>이 촬영 폴더를 켜 둔 판 모두가 분석했는지입니다. 결과 창을 "분석 중"으로 열지 정할 때 씁니다.</summary>
    public bool IsFinished(string folder)
    {
        var any = false;
        foreach (var lane in _lanes)
        {
            if (!lane.Enabled) continue;
            if (!lane.IsFinished(folder)) return false;
            any = true;
        }
        return any;
    }

    /// <summary>
    /// 켜 둔 판과 미리 분석 스위치를 기억한 값에서 읽고, 바뀌면 남깁니다. 기억한 값이 없으면 모든 판과 스위치를 켭니다.
    /// </summary>
    /// <param name="remembered">값을 읽고 쓰는 곳. null이면 기억하지 않습니다</param>
    public void UseRemembered(RememberedValues? remembered)
    {
        _remembered = remembered;
        if (remembered is null) return;
        _prestream = remembered.GetBool(PrestreamKey) ?? true;
        foreach (var lane in _lanes)
        {
            lane.SetPrestream(_prestreamAvailable && _prestream);
            lane.SetEnabled(remembered.GetBool(EnabledKey(lane.VariantId)) ?? true);
        }
        // 기억한 값이 모든 판을 끄고 있으면(판 목록이 바뀐 경우) 첫 판을 켭니다.
        if (_lanes.Count > 0 && _lanes.All(l => !l.Enabled)) _lanes[0].SetEnabled(true);
        OnPropertyChanged(nameof(Prestream));
        Refresh();
    }

    /// <summary>판마다 관류 분석 프로그램을 띄웁니다.</summary>
    public void Start()
    {
        foreach (var lane in _lanes) lane.Start();
        UpdateHolds();
    }

    /// <summary>앱을 닫을 때 부릅니다. 판마다 진행 중인 분석과 대기열을 취소하고 관류 분석 프로그램을 끝냅니다.</summary>
    public void Shutdown()
    {
        foreach (var lane in _lanes) lane.Shutdown();
    }

    /// <summary>
    /// 판을 켜거나 끕니다. 마지막으로 켜 둔 판은 끄지 않습니다.
    /// </summary>
    /// <param name="id">판 이름</param>
    /// <param name="on">켤지</param>
    /// <returns>바꿨으면 true</returns>
    public bool SetLaneEnabled(string id, bool on)
    {
        foreach (var lane in _lanes)
        {
            if (lane.VariantId != id || lane.Enabled == on) continue;
            if (!on && _enabledCount <= 1) return false;
            lane.SetEnabled(on);
            _remembered?.SetBool(EnabledKey(id), on);
            UpdateHolds();
            return true;
        }
        return false;
    }

    /// <summary>안정화와 녹화 중에는 true. 판 모두에 새 요청을 보내지 않습니다.</summary>
    public void SetHold(bool hold)
    {
        _recording = hold;
        UpdateHolds();
    }

    /// <summary>RGB 영상이 저장되었습니다. 켜 둔 판 모두에 넘기고, 뒤쪽 판은 앞 판이 끝날 때까지 보류합니다.</summary>
    public void OnRgbSaved(string folder, bool ready)
    {
        // 뒤쪽 판이 앞 판보다 먼저 요청을 보내지 않도록, 넣기 전에 보류를 겁니다.
        if (ready) UpdateHolds(pending: true);
        foreach (var lane in _lanes) lane.OnRgbSaved(folder, ready);
        UpdateHolds();
    }

    /// <summary>촬영 폴더가 저장되었습니다. 켜 둔 판 모두에 넘깁니다.</summary>
    public void OnShotSaved(string folder, bool ready)
    {
        if (ready) UpdateHolds(pending: true);
        foreach (var lane in _lanes) lane.OnShotSaved(folder, ready);
        UpdateHolds();
    }

    /// <summary>녹화 중인 발 영상의 첫 장이 파일에 쓰였습니다. 켜 둔 판 모두에 미리 분할을 요청합니다.</summary>
    public void OnFirstFramesWritten(string leftVideo, string rightVideo)
    {
        foreach (var lane in _lanes) lane.OnFirstFramesWritten(leftVideo, rightVideo);
    }

    /// <summary>판의 분석이 끝나기를 기다린 뒤 관류 분석 프로그램을 거둡니다.</summary>
    public void Dispose()
    {
        // 판의 알림이 이 객체를 부르므로, 판을 먼저 떼어 냅니다.
        foreach (var lane in _lanes)
        {
            lane.PropertyChanged -= OnLanePropertyChanged;
            lane.Dispose();
        }
    }

    private static string EnabledKey(string id) => $"analysis/variant/{id}/enabled";

    // 앞 판이 분석 중이면 뒤 판을 보류합니다. pending이면 아직 넣지 않은 촬영을 앞 판이 곧 받는다고 봅니다.
    private void UpdateHolds(bool pending = false)
    {
        var enabled = _lanes.Select(l => l.Enabled).ToList();
        var active = _lanes.Select(l => pending || l.Active).ToList();
        var holds = VariantHolds(_recording, enabled, active);
        var earlier = ""; // 이 판보다 앞에서 분석 중이거나 대기 중인 판
        for (var i = 0; i < _lanes.Count; i++)
        {
            _lanes[i].SetHoldReason(_recording ? "" : earlier);
            _lanes[i].SetHold(holds[i]);
            if (earlier.Length == 0 && enabled[i] && active[i]) earlier = _lanes[i].VariantLabel;
        }
    }

    private void OnLanePropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(AnalysisViewModel.Enabled)) UpdateHolds();
        Refresh();
    }

    private void OnLaneCollected(AnalysisViewModel lane, AnalysisCollectedEventArgs e)
    {
        if (e.Folder != _collectFolder)
        {
            _collectFolder = e.Folder;
            _collectTexts.Clear();
        }
        _collectTexts[lane.VariantLabel] = e.Text;
        var text = string.Join("\n", _collectTexts.Select(pair => $"{pair.Key}: {pair.Value}"));
        Collected?.Invoke(this, new VariantCollectedEventArgs(e.Folder, text));
    }

    // 판 상태에서 구하는 값을 다시 읽고, 달라진 것만 알립니다.
    private void Refresh()
    {
        EnabledCount = _lanes.Count(l => l.Enabled);
        BlocksShot = _lanes.Any(l => l.Enabled && (l.State == AnalysisState.Busy || l.Queued > 0));
        var busy = _lanes.Find(l => l.State == AnalysisState.Busy);
        RemainingText = busy is null ? "" : busy.RemainingText.Length == 0 ? busy.VariantLabel + " 분석 중" : busy.VariantLabel + " " + busy.RemainingText;
    }
}

/// <summary>판 하나가 촬영 하나의 분석을 끝냈습니다.</summary>
/// <param name="Folder">촬영 폴더입니다.</param>
/// <param name="Ok">성공이면 true입니다.</param>
/// <param name="SummaryPath">요약 JSON 경로입니다.</param>
/// <param name="Variant">판 이름입니다.</param>
public sealed record VariantFinishedEventArgs(string Folder, bool Ok, string SummaryPath, string Variant);

/// <param name="Folder">촬영 폴더입니다.</param>
/// <param name="Variant">판 이름입니다.</param>
public sealed record VariantFiguresEventArgs(string Folder, string Variant);

/// <param name="Folder">촬영 폴더입니다.</param>
/// <param name="Text">이 촬영의 판별 복사 결과를 판 이름과 함께 이은 문장입니다.</param>
public sealed record VariantCollectedEventArgs(string Folder, string Text);
