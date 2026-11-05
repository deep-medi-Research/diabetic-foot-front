using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DiabeticFoot.App.Preferences;
using DiabeticFoot.App.Services;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.App.Shot;

/// <summary>
/// 환자 정보와 저장 위치 입력의 ViewModel입니다.
/// 입력한 글자를 그대로 보관하고, 검사는 core(<see cref="RecordFunctions.ValidateShot"/>)로 해서 무엇이 잘못됐는지 문장으로 내놓습니다.
/// 화면은 이 값을 그리고 편집한 글자를 속성에 쓰기만 합니다.
/// </summary>
/// <remarks>
/// 이름, 생년월일, 혈압은 다음 실행에 남기지 않습니다. 앞 환자의 정보로 다음 환자를 찍는 일을 막기 위함입니다(기존 앱과 같음).
/// 저장 위치만 남깁니다. 같은 이유로 이름이나 생년월일을 바꾸면 혈압 칸과 발목 혈압 단계를 비웁니다.
/// </remarks>
public sealed class ShotFormViewModel : ObservableObject
{
    public const string RootKey = "shot/root";

    private const string AnkleLevelHelp = "V 이완기 - 50, D 이완기, M (수축기 + 이완기) / 2, S 수축기";

    private readonly RememberedValues _remembered;
    private readonly IFolderPicker? _folderPicker;

    private string _root;
    private IReadOnlyList<RecentPatient> _recentPatients = [];
    private string _name = "";
    private string _birth = "";
    private string _armSystolic = "";
    private string _armDiastolic = "";
    private string _ankleSystolic = "";
    private string _ankleDiastolic = "";
    private string _ankleLevel = "";
    private string _ankleLevelText = "";
    private string _nameError = "";
    private string _birthError = "";
    private string _armPressureError = "";
    private string _anklePressureError = "";
    private string _ankleLevelError = "";
    private string _missing = "";
    private string _toFix = "";
    private bool _ready;
    private string _folderPreview = "";

    /// <param name="root">처음 저장 위치</param>
    /// <param name="remembered">화면에서 고른 저장 위치를 기억합니다</param>
    /// <param name="folderPicker">저장 위치 선택 창입니다. 없으면 <see cref="ChooseRootCommand"/>가 아무것도 하지 않습니다</param>
    public ShotFormViewModel(string root, RememberedValues remembered, IFolderPicker? folderPicker = null)
    {
        _root = root;
        _remembered = remembered;
        _folderPicker = folderPicker;
        ChooseRootCommand = new RelayCommand(ChooseRoot);
        PickPatientCommand = new RelayCommand<RecentPatient>(patient =>
        {
            if (patient is not null) PickPatient(patient.Name, patient.Birth);
        });
        Revalidate();
        RefreshRecent();
    }

    /// <summary>저장 위치 선택 창을 엽니다. 고르면 <see cref="SetRoot"/>와 같습니다.</summary>
    public IRelayCommand ChooseRootCommand { get; }

    /// <summary>최근 환자 하나를 고릅니다. 이름과 생년월일을 함께 채웁니다.</summary>
    public IRelayCommand<RecentPatient> PickPatientCommand { get; }

    /// <summary>발목 혈압 단계 목록입니다. 화면의 버튼 순서와 같습니다.</summary>
    public IReadOnlyList<string> AnkleLevels => BloodPressure.AnkleLevels;

    /// <summary>입력한 이름입니다. 앞뒤 공백은 폴더 이름을 만들 때 뺍니다.</summary>
    public string Name
    {
        get => _name;
        set
        {
            if (_name == value) return;
            if (value.Trim() != _name.Trim()) ClearPressures();
            SetProperty(ref _name, value);
            Revalidate();
        }
    }

    /// <summary>입력한 생년월일입니다. 0000-00-00 형식이고, 숫자 여덟 자리만 주면 형식에 맞춰 둡니다.</summary>
    public string Birth
    {
        get => _birth;
        set
        {
            var next = BirthText(value);
            if (_birth == next) return;
            SetProperty(ref _birth, next);
            ClearPressures();
            Revalidate();
        }
    }

    /// <summary>상완에서 잰 수축기 혈압(mmHg)입니다. 입력한 글자 그대로 받습니다.</summary>
    public string ArmSystolic
    {
        get => _armSystolic;
        set => SetPressureText(ref _armSystolic, value);
    }

    public string ArmDiastolic
    {
        get => _armDiastolic;
        set => SetPressureText(ref _armDiastolic, value);
    }

    public string AnkleSystolic
    {
        get => _ankleSystolic;
        set => SetPressureText(ref _ankleSystolic, value);
    }

    public string AnkleDiastolic
    {
        get => _ankleDiastolic;
        set => SetPressureText(ref _ankleDiastolic, value);
    }

    /// <summary>고른 발목 혈압 단계입니다. <see cref="AnkleLevels"/> 중 하나이고, 빈 문자열은 고르지 않음입니다.</summary>
    public string AnkleLevel
    {
        get => _ankleLevel;
        set
        {
            if (_ankleLevel == value) return;
            SetProperty(ref _ankleLevel, value);
            Revalidate();
        }
    }

    /// <summary>발목 혈압 단계 버튼 아래의 작은 글자입니다. 고르기 전에는 단계의 뜻을, 고른 뒤에는 계산식과 값(mmHg)을 보입니다.</summary>
    public string AnkleLevelText
    {
        get => _ankleLevelText;
        private set => SetProperty(ref _ankleLevelText, value);
    }

    /// <summary>화면에 보이는 저장 위치입니다(OS 경로 구분자).</summary>
    public string Root => _root.Replace('/', Path.DirectorySeparatorChar);

    /// <summary>저장 위치입니다. 촬영 폴더를 만드는 쪽이 씁니다.</summary>
    public string RootPath => _root;

    public string NameError
    {
        get => _nameError;
        private set => SetProperty(ref _nameError, value);
    }

    public string BirthError
    {
        get => _birthError;
        private set => SetProperty(ref _birthError, value);
    }

    public string ArmPressureError
    {
        get => _armPressureError;
        private set => SetProperty(ref _armPressureError, value);
    }

    public string AnklePressureError
    {
        get => _anklePressureError;
        private set => SetProperty(ref _anklePressureError, value);
    }

    public string AnkleLevelError
    {
        get => _ankleLevelError;
        private set => SetProperty(ref _ankleLevelError, value);
    }

    /// <summary>아직 입력하지 않은 칸 이름입니다(예: "입력할 것: 상완 혈압, 발목 혈압 단계"). 없으면 빈 문자열입니다.</summary>
    public string Missing
    {
        get => _missing;
        private set => SetProperty(ref _missing, value);
    }

    /// <summary>잘못 입력해 빨간 글자가 뜬 칸 이름입니다(예: "고칠 것: 상완 혈압"). 없으면 빈 문자열입니다.</summary>
    public string ToFix
    {
        get => _toFix;
        private set => SetProperty(ref _toFix, value);
    }

    /// <summary>빈 칸도 잘못된 칸도 없으면 true입니다. 촬영 버튼을 켤 수 있습니다.</summary>
    public bool Ready
    {
        get => _ready;
        private set => SetProperty(ref _ready, value);
    }

    public string FolderPreview
    {
        get => _folderPreview;
        private set => SetProperty(ref _folderPreview, value);
    }

    /// <summary>
    /// 저장 위치에 이미 있는 촬영의 환자 목록입니다. 최근에 찍은 순서입니다.
    /// 저장 위치의 환자 폴더에서 읽으므로 따로 저장하는 정보가 없습니다.
    /// </summary>
    public IReadOnlyList<RecentPatient> RecentPatients
    {
        get => _recentPatients;
        private set => SetProperty(ref _recentPatients, value);
    }

    /// <summary>화면에서 마지막으로 고른 저장 위치입니다. 없으면 빈 문자열입니다.</summary>
    public static string RememberedRoot(RememberedValues remembered) => remembered.GetString(RootKey) ?? "";

    /// <summary>최근 환자 하나를 고릅니다. 이름과 생년월일을 함께 채웁니다.</summary>
    public void PickPatient(string name, string birth)
    {
        var nextBirth = BirthText(birth);
        if (name.Trim() != _name.Trim() || nextBirth != _birth) ClearPressures();
        SetProperty(ref _name, name, nameof(Name));
        SetProperty(ref _birth, nextBirth, nameof(Birth));
        Revalidate();
    }

    /// <summary>저장 위치를 다시 읽어 최근 환자 목록을 만듭니다. 촬영을 저장한 뒤와 저장 위치를 바꾼 뒤 부릅니다.</summary>
    public void RefreshRecent()
    {
        var list = RecentPatient.Scan(_root);
        if (!list.SequenceEqual(_recentPatients)) RecentPatients = list;
    }

    /// <summary>저장 위치를 정하고 기억합니다.</summary>
    public void SetRoot(string path)
    {
        if (path.Length == 0 || path == _root) return;
        _root = path;
        _remembered.SetString(RootKey, _root);
        OnPropertyChanged(nameof(Root));
        OnPropertyChanged(nameof(RootPath));
        Revalidate();
        RefreshRecent();
    }

    /// <summary>
    /// 지금 입력과 촬영 시각으로 촬영 정보를 만듭니다.
    /// </summary>
    /// <param name="when">촬영 시각. 한 번만 읽어 넘겨야 자정 앞뒤에서 폴더 날짜와 파일 시각이 갈리지 않습니다</param>
    public ShotInfo InfoAt(DateTime when) => new()
    {
        PatientName = _name.Trim(),
        BirthDate = DigitsOf(_birth),
        ArmPressure = new BloodPressure(PressureMmhg(_armSystolic), PressureMmhg(_armDiastolic)),
        AnklePressure = new BloodPressure(PressureMmhg(_ankleSystolic), PressureMmhg(_ankleDiastolic)),
        AnkleLevel = _ankleLevel,
        Year = when.Year,
        Month = when.Month,
        Day = when.Day,
        Hour = when.Hour,
        Minute = when.Minute,
        Second = when.Second,
    };

    private void ChooseRoot()
    {
        var picked = _folderPicker?.Pick("저장 위치", _root);
        if (picked is not null) SetRoot(picked);
    }

    private void SetPressureText(ref string field, string text, [System.Runtime.CompilerServices.CallerMemberName] string? property = null)
    {
        if (field == text) return;
        SetProperty(ref field, text, property);
        Revalidate();
    }

    // 이름이나 생년월일이 바뀌면 앞 환자의 혈압이 남지 않게 비웁니다. 알림은 값이 바뀐 속성만 나갑니다.
    private void ClearPressures()
    {
        SetProperty(ref _armSystolic, "", nameof(ArmSystolic));
        SetProperty(ref _armDiastolic, "", nameof(ArmDiastolic));
        SetProperty(ref _ankleSystolic, "", nameof(AnkleSystolic));
        SetProperty(ref _ankleDiastolic, "", nameof(AnkleDiastolic));
        SetProperty(ref _ankleLevel, "", nameof(AnkleLevel));
    }

    private void Revalidate()
    {
        var info = InfoAt(DateTime.Now);
        var problems = RecordFunctions.ValidateShot(info);
        bool Has(ShotProblem problem) => problems.Contains(problem);

        var missing = new List<string>();
        var nameError = "";
        var birthError = "";

        if (Has(ShotProblem.EmptyName)) missing.Add("이름");
        if (Has(ShotProblem.BadNameCharacter))
        {
            var found = ForbiddenCharacters(_name.Trim());
            nameError = found.Length == 0 ? "이름 끝에 점을 쓸 수 없습니다" : $"폴더 이름에 쓸 수 없는 글자: {found}";
        }

        var digits = DigitsOf(_birth);
        if (digits.Length == 0)
        {
            missing.Add("생년월일");
        }
        else if (!IsAllDigits(digits))
        {
            birthError = "생년월일은 숫자 여덟 자리입니다";
        }
        else if (digits.Length < 8)
        {
            // 입력하는 중에는 빨간 글자를 띄우지 않고 남은 입력으로만 알립니다.
            missing.Add($"생년월일 {8 - digits.Length}자리");
        }
        else if (Has(ShotProblem.BadBirthDate))
        {
            birthError = $"없는 날짜입니다: {_birth}";
        }

        string CheckPressure(string site, string systolicText, string diastolicText, bool bad)
        {
            var systolic = PressureDigits(systolicText);
            var diastolic = PressureDigits(diastolicText);
            if (systolic.Length == 0 && diastolic.Length == 0)
            {
                missing.Add($"{site} 혈압");
            }
            else if (!IsAllDigits(systolic) || !IsAllDigits(diastolic))
            {
                return "혈압은 숫자로 씁니다";
            }
            else if (systolic.Length < 2 || diastolic.Length < 2)
            {
                // 혈압은 두 자리 이상이므로 한 자리 이하는 입력하는 중으로 보고 빨간 글자를 띄우지 않습니다.
                missing.Add($"{site} {(systolic.Length < 2 ? "수축기" : "이완기")}");
            }
            else if (bad && ToInt(systolic) <= ToInt(diastolic))
            {
                return "수축기가 이완기보다 커야 합니다";
            }
            else if (bad)
            {
                return $"혈압은 {BloodPressure.MinMmhg}~{BloodPressure.MaxMmhg}mmHg입니다";
            }
            return "";
        }
        var armError = CheckPressure("상완", _armSystolic, _armDiastolic, Has(ShotProblem.BadArmPressure));
        var ankleError = CheckPressure("발목", _ankleSystolic, _ankleDiastolic, Has(ShotProblem.BadAnklePressure));

        var levelError = "";
        string levelText;
        var formula = AnkleLevelFormula(_ankleLevel);
        if (_ankleLevel.Length == 0)
        {
            missing.Add("발목 혈압 단계");
            levelText = AnkleLevelHelp;
        }
        else if (Has(ShotProblem.UnknownAnkleLevel))
        {
            levelError = $"발목 혈압 단계 {_ankleLevel}은 목록에 없습니다";
            levelText = "";
        }
        else if (Has(ShotProblem.BadAnklePressure) && formula.Length > 0)
        {
            levelText = "발목 혈압을 입력하면 계산합니다";
        }
        else if (Has(ShotProblem.NegativeAnkleLevel))
        {
            levelError = "V는 발목 이완기가 50mmHg 이상일 때 고를 수 있습니다";
            levelText = "";
        }
        else
        {
            var mmhg = (RecordFunctions.AnkleLevelMmhg(info.AnkleLevel, info.AnklePressure) ?? 0).ToString("G6", CultureInfo.InvariantCulture);
            levelText = formula.Length == 0 ? $"{mmhg} mmHg" : $"{formula} = {mmhg} mmHg";
        }

        NameError = nameError;
        BirthError = birthError;
        ArmPressureError = armError;
        AnklePressureError = ankleError;
        AnkleLevelError = levelError;
        AnkleLevelText = levelText;
        Missing = missing.Count == 0 ? "" : $"입력할 것: {string.Join(", ", missing)}";
        // 촬영 버튼 옆에서 꺼진 이유를 한 번에 보이기 위해, 빨간 글자가 뜬 칸 이름을 모읍니다.
        var toFix = new List<string>();
        if (nameError.Length > 0) toFix.Add("이름");
        if (birthError.Length > 0) toFix.Add("생년월일");
        if (armError.Length > 0) toFix.Add("상완 혈압");
        if (ankleError.Length > 0) toFix.Add("발목 혈압");
        if (levelError.Length > 0) toFix.Add("발목 혈압 단계");
        ToFix = toFix.Count == 0 ? "" : $"고칠 것: {string.Join(", ", toFix)}";
        Ready = missing.Count == 0 && toFix.Count == 0;
        // 폴더 규칙은 core(LayoutOf)의 결과를 그대로 보여 주고, 아직 정해지지 않은 마지막 단계만 글자로 둡니다.
        var layout = RecordFunctions.LayoutOf(info, "mkv", []);
        FolderPreview = layout is null ? "" : layout.Folder[..(layout.Folder.Length - layout.Stem.Length)] + "날짜_시각";
    }

    // Windows가 폴더 이름에 받지 않는 글자입니다. 화면에 그 글자를 그대로 보여 주려고 찾습니다.
    private static string ForbiddenCharacters(string name)
    {
        var found = new List<string>();
        foreach (var c in name)
        {
            var control = c < 0x20 || c == 0x7F;
            var bad = control || "<>:\"/\\|?*".Contains(c);
            if (!bad) continue;
            var text = control ? "제어 문자" : c.ToString();
            if (!found.Contains(text)) found.Add(text);
        }
        return string.Join(" ", found);
    }

    private static string DigitsOf(string text) => string.Concat(text.Where(c => c != '-' && c != ' '));

    private static bool IsAllDigits(string text) => text.All(char.IsDigit);

    // 명령줄에서 숫자 여덟 자리로 받으면 화면 입력 형식(0000-00-00)에 맞춥니다.
    private static string BirthText(string text) =>
        text.Length == 8 && IsAllDigits(text) ? $"{text[..4]}-{text.Substring(4, 2)}-{text.Substring(6, 2)}" : text;

    // 입력 마스크가 비운 자리를 공백으로 채울 수 있으므로 공백을 뺍니다.
    private static string PressureDigits(string text) => text.Replace(" ", "");

    private static int ToInt(string text) =>
        int.TryParse(text, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out var value) ? value : 0;

    private static int PressureMmhg(string text) => ToInt(PressureDigits(text));

    // 버튼 아래에 값과 함께 보여 어떤 혈압으로 계산했는지 알 수 있게 합니다. 0은 혈압과 관계없어 비웁니다.
    private static string AnkleLevelFormula(string level) => level switch
    {
        "V" => "발목 이완기 - 50",
        "D" => "발목 이완기",
        "M" => "(발목 수축기 + 이완기) / 2",
        "S" => "발목 수축기",
        _ => "",
    };
}
