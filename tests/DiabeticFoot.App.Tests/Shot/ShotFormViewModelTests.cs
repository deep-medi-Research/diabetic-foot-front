using DiabeticFoot.App.Preferences;
using DiabeticFoot.App.Services;
using DiabeticFoot.App.Shot;
using DiabeticFoot.App.Tests.Support;

namespace DiabeticFoot.App.Tests.Shot;

public sealed class ShotFormViewModelTests : IDisposable
{
    private readonly TempFolder _root = new();
    private readonly RememberedValues _remembered = RememberedValues.ThisRunOnly();
    private readonly ShotFormViewModel _form;

    // 이름과 생년월일을 채운 입력입니다. 혈압 칸과 발목 혈압 단계는 비어 있습니다.
    public ShotFormViewModelTests()
    {
        _form = new ShotFormViewModel(_root.Path, _remembered) { Name = "시험", Birth = "19600101" };
    }

    public void Dispose() => _root.Dispose();

    private void FillPressures()
    {
        _form.ArmSystolic = "120";
        _form.ArmDiastolic = "80";
        _form.AnkleSystolic = "135";
        _form.AnkleDiastolic = "84";
        _form.AnkleLevel = "D";
    }

    private bool PressuresEmpty() =>
        _form.ArmSystolic.Length == 0 && _form.ArmDiastolic.Length == 0 && _form.AnkleSystolic.Length == 0 &&
        _form.AnkleDiastolic.Length == 0 && _form.AnkleLevel.Length == 0;

    // 상완과 발목 혈압, 발목 혈압 단계를 비우면 입력할 것에 나오고 촬영할 수 없습니다. 채우면 촬영 정보에 들어갑니다.
    [Fact]
    public void RequiresPressuresAndAnkleLevel()
    {
        Assert.Equal("입력할 것: 상완 혈압, 발목 혈압, 발목 혈압 단계", _form.Missing);
        Assert.False(_form.Ready);
        Assert.Equal("", _form.FolderPreview);

        FillPressures();
        _form.AnkleDiastolic = "84 "; // 입력 마스크가 비운 자리의 공백

        Assert.True(_form.Ready);
        var info = _form.InfoAt(new DateTime(2026, 10, 29, 13, 0, 0));
        Assert.Equal(120, info.ArmPressure.SystolicMmhg);
        Assert.Equal(80, info.ArmPressure.DiastolicMmhg);
        Assert.Equal(135, info.AnklePressure.SystolicMmhg);
        Assert.Equal(84, info.AnklePressure.DiastolicMmhg);
        Assert.Equal("D", info.AnkleLevel);
        Assert.Equal(2026, info.Year);
        Assert.Equal("19600101", info.BirthDate);
        Assert.Equal("시험_19600101/D/날짜_시각", _form.FolderPreview);
    }

    // 한 자리는 입력하는 중으로 보고 입력할 것에 넣습니다. 수축기가 이완기보다 크지 않거나 범위 밖이면 이유를 보입니다.
    [Fact]
    public void ExplainsBadPressure()
    {
        FillPressures();

        _form.ArmDiastolic = "8";
        Assert.Equal("입력할 것: 상완 이완기", _form.Missing);
        Assert.Equal("", _form.ArmPressureError);

        _form.ArmDiastolic = "130";
        Assert.Equal("수축기가 이완기보다 커야 합니다", _form.ArmPressureError);
        Assert.Equal("", _form.Missing);
        Assert.False(_form.Ready);

        _form.ArmSystolic = "310";
        Assert.Equal("혈압은 20~300mmHg입니다", _form.ArmPressureError);
        Assert.Equal("", _form.AnklePressureError);

        _form.AnkleSystolic = "1a0";
        Assert.Equal("혈압은 숫자로 씁니다", _form.AnklePressureError);
    }

    // 고르기 전에는 단계의 뜻을, 고르면 발목 혈압으로 계산한 값을 버튼 아래에 보입니다.
    [Fact]
    public void ShowsAnkleLevelValue()
    {
        Assert.Equal(["0", "V", "D", "M", "S"], _form.AnkleLevels);
        Assert.Equal("V 이완기 - 50, D 이완기, M (수축기 + 이완기) / 2, S 수축기", _form.AnkleLevelText);

        _form.AnkleLevel = "M";
        Assert.Equal("발목 혈압을 입력하면 계산합니다", _form.AnkleLevelText);
        _form.AnkleLevel = "0";
        Assert.Equal("0 mmHg", _form.AnkleLevelText);

        FillPressures();
        Assert.Equal("발목 이완기 = 84 mmHg", _form.AnkleLevelText);
        _form.AnkleLevel = "V";
        Assert.Equal("발목 이완기 - 50 = 34 mmHg", _form.AnkleLevelText);
        _form.AnkleLevel = "M";
        Assert.Equal("(발목 수축기 + 이완기) / 2 = 109.5 mmHg", _form.AnkleLevelText);
        _form.AnkleLevel = "S";
        Assert.Equal("발목 수축기 = 135 mmHg", _form.AnkleLevelText);
        Assert.True(_form.Ready);
    }

    // 발목 이완기가 50mmHg보다 낮아 v가 음수이면 이유를 보이고 촬영할 수 없습니다.
    [Fact]
    public void RejectsNegativeAnkleLevel()
    {
        FillPressures();
        _form.AnkleDiastolic = "45";
        _form.AnkleLevel = "V";

        Assert.Equal("V는 발목 이완기가 50mmHg 이상일 때 고를 수 있습니다", _form.AnkleLevelError);
        Assert.Equal("", _form.AnkleLevelText);
        Assert.False(_form.Ready);

        _form.AnkleLevel = "D";
        Assert.Equal("", _form.AnkleLevelError);
        Assert.True(_form.Ready);
    }

    // 빨간 글자가 뜬 칸은 고칠 것에 모여 촬영 버튼 위에 보입니다. 빈 칸은 입력할 것에만 나옵니다.
    [Fact]
    public void ListsFieldsToFix()
    {
        Assert.Equal("", _form.ToFix);

        FillPressures();
        _form.ArmDiastolic = "130";
        Assert.Equal("고칠 것: 상완 혈압", _form.ToFix);

        _form.AnkleDiastolic = "45";
        _form.AnkleLevel = "V";
        Assert.Equal("고칠 것: 상완 혈압, 발목 혈압 단계", _form.ToFix);
        Assert.Equal("", _form.Missing);

        _form.ArmDiastolic = "80";
        _form.AnkleLevel = "D";
        Assert.Equal("", _form.ToFix);
        Assert.True(_form.Ready);

        _form.Name = "시험/";
        Assert.Equal("고칠 것: 이름", _form.ToFix);
        Assert.Equal("입력할 것: 상완 혈압, 발목 혈압, 발목 혈압 단계", _form.Missing);
    }

    // 이름이나 생년월일을 바꾸면 혈압 칸과 발목 혈압 단계를 비웁니다. 같은 환자를 다시 고르거나 이름 앞뒤 공백만 바꾸면 그대로 둡니다.
    [Fact]
    public void ClearsPressureWhenPatientChanges()
    {
        FillPressures();

        _form.PickPatient("시험", "1960-01-01");
        _form.Name = "시험 ";
        Assert.Equal("120", _form.ArmSystolic);
        Assert.Equal("D", _form.AnkleLevel);
        Assert.True(_form.Ready);

        _form.Name = "시험2";
        Assert.True(PressuresEmpty());
        Assert.Equal("입력할 것: 상완 혈압, 발목 혈압, 발목 혈압 단계", _form.Missing);

        FillPressures();
        _form.Birth = "19600102";
        Assert.True(PressuresEmpty());

        FillPressures();
        _form.PickPatient("김철수", "1955-11-03");
        Assert.True(PressuresEmpty());
    }

    // 알림은 값이 바뀐 속성만 나갑니다(MVVM 규칙). 혈압을 비울 때 이미 빈 칸은 알리지 않습니다.
    [Fact]
    public void NotifiesOnlyChangedProperties()
    {
        _form.ArmSystolic = "120";
        var changed = new List<string?>();
        _form.PropertyChanged += (_, e) => changed.Add(e.PropertyName);

        _form.Name = "시험2";

        Assert.Contains(nameof(ShotFormViewModel.Name), changed);
        Assert.Contains(nameof(ShotFormViewModel.ArmSystolic), changed);
        Assert.DoesNotContain(nameof(ShotFormViewModel.ArmDiastolic), changed);
        Assert.DoesNotContain(nameof(ShotFormViewModel.BirthError), changed);
        Assert.Equal(changed.Count, changed.Distinct().Count());
    }

    // 생년월일은 숫자 여덟 자리를 형식에 맞춰 두고, 입력하는 중에는 남은 자리만 알립니다.
    [Fact]
    public void ExplainsBirthWhileTyping()
    {
        Assert.Equal("1960-01-01", _form.Birth);

        _form.Birth = "1960-0";
        Assert.Equal("", _form.BirthError);
        Assert.StartsWith("입력할 것: 생년월일 3자리", _form.Missing);

        _form.Birth = "1960-13-45";
        Assert.Equal("없는 날짜입니다: 1960-13-45", _form.BirthError);

        _form.Birth = "19a0-01-01";
        Assert.Equal("생년월일은 숫자 여덟 자리입니다", _form.BirthError);

        _form.Birth = "";
        Assert.StartsWith("입력할 것: 생년월일", _form.Missing);
    }

    // 폴더 이름에 쓸 수 없는 글자는 그 글자를 그대로 보이고, 이름이 비면 입력할 것에 넣습니다.
    [Fact]
    public void ExplainsBadName()
    {
        _form.Name = "시험/:";
        Assert.Equal("폴더 이름에 쓸 수 없는 글자: / :", _form.NameError);

        _form.Name = "시험.";
        Assert.Equal("이름 끝에 점을 쓸 수 없습니다", _form.NameError);

        _form.Name = "시\u0001험";
        Assert.Equal("폴더 이름에 쓸 수 없는 글자: 제어 문자", _form.NameError);

        _form.Name = "   ";
        Assert.Equal("", _form.NameError);
        Assert.StartsWith("입력할 것: 이름", _form.Missing);
    }

    // 저장 위치를 고르면 기억하고, 그 위치의 환자 폴더에서 최근 환자를 읽습니다.
    [Fact]
    public void RemembersRootAndReadsRecentPatients()
    {
        using var other = new TempFolder();
        Directory.CreateDirectory(Path.Combine(other.Path, "홍길동_19551103", "D", "20261022_101454"));
        Directory.CreateDirectory(Path.Combine(other.Path, "김_철수_19700202"));
        Directory.CreateDirectory(Path.Combine(other.Path, "results"));
        Directory.SetLastWriteTime(Path.Combine(other.Path, "홍길동_19551103"), new DateTime(2026, 10, 22, 10, 14, 0));
        Directory.SetLastWriteTime(Path.Combine(other.Path, "김_철수_19700202"), new DateTime(2026, 11, 3, 9, 5, 0));

        Assert.Empty(_form.RecentPatients);
        Assert.Equal("", ShotFormViewModel.RememberedRoot(RememberedValues.ThisRunOnly()));

        _form.SetRoot(other.Path);

        Assert.Equal(other.Path, ShotFormViewModel.RememberedRoot(_remembered));
        Assert.Equal(
            [new RecentPatient("김_철수", "1970-02-02", "11/3 09:05"), new RecentPatient("홍길동", "1955-11-03", "10/22 10:14")],
            _form.RecentPatients);

        _form.PickPatientCommand.Execute(_form.RecentPatients[1]);
        Assert.Equal("홍길동", _form.Name);
        Assert.Equal("1955-11-03", _form.Birth);
    }

    // 이전 배치(날짜 폴더 아래 환자 폴더)도 읽고, 같은 환자는 가장 늦은 촬영 시각 하나로 합칩니다.
    [Fact]
    public void ReadsOlderDayLayoutAndMergesSamePatient()
    {
        Directory.CreateDirectory(Path.Combine(_root.Path, "20261001", "홍길동_19551103"));
        Directory.SetLastWriteTime(Path.Combine(_root.Path, "20261001", "홍길동_19551103"), new DateTime(2026, 10, 1, 8, 0, 0));
        Directory.CreateDirectory(Path.Combine(_root.Path, "홍길동_19551103"));
        Directory.SetLastWriteTime(Path.Combine(_root.Path, "홍길동_19551103"), new DateTime(2026, 10, 22, 10, 14, 0));

        var list = RecentPatient.Scan(_root.Path);

        Assert.Equal([new RecentPatient("홍길동", "1955-11-03", "10/22 10:14")], list);
    }

    // 최근 환자는 8명까지만 보입니다.
    [Fact]
    public void ListsAtMostEightRecentPatients()
    {
        for (var i = 0; i < 12; i++)
        {
            var folder = Path.Combine(_root.Path, $"환자{i:D2}_1950010{i % 10}");
            Directory.CreateDirectory(folder);
            Directory.SetLastWriteTime(folder, new DateTime(2026, 10, 1, 8, i, 0));
        }

        var list = RecentPatient.Scan(_root.Path);

        Assert.Equal(RecentPatient.MaxCount, list.Count);
        Assert.Equal("환자11", list[0].Name);
    }

    // 저장 위치 선택 창에서 고르면 그 위치를 쓰고, 취소하면 그대로 둡니다.
    [Fact]
    public void ChooseRootUsesPickerResult()
    {
        var picker = new FakePicker();
        var form = new ShotFormViewModel(_root.Path, _remembered, picker);

        picker.Next = null;
        form.ChooseRootCommand.Execute(null);
        Assert.Equal(_root.Path, form.RootPath);

        var next = Path.Combine(_root.Path, "다른 곳");
        picker.Next = next;
        form.ChooseRootCommand.Execute(null);
        Assert.Equal(next, form.RootPath);
        Assert.Equal(next, ShotFormViewModel.RememberedRoot(_remembered));
        Assert.Equal(_root.Path, picker.LastInitial);
    }

    private sealed class FakePicker : IFolderPicker
    {
        public string? Next { get; set; }

        public string? LastInitial { get; private set; }

        public string? Pick(string title, string initialFolder)
        {
            LastInitial = initialFolder;
            return Next;
        }
    }
}
