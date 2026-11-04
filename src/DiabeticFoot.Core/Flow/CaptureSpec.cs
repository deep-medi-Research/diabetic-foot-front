using System.Globalization;
using System.Text;
using DiabeticFoot.Core.Timing;

namespace DiabeticFoot.Core.Flow;

/// <summary>영상 파일의 저장 방식입니다.</summary>
public enum VideoCodec : byte
{
    /// <summary>무손실 압축, MKV. 디스크가 무압축 속도를 버티지 못하는 PC를 위한 기본값</summary>
    Ffv1,

    /// <summary>무압축 UYVY, AVI(OpenDML)</summary>
    Uncompressed,
}

/// <summary>
/// 촬영 한 번의 선언입니다.
/// </summary>
/// <remarks>
/// 실행 순서는 이 값으로 core가 정하고, platform과 화면은 실행만 합니다. 보통 specs 폴더의 JSON 선언 파일에서 읽습니다(<c>FlowFunctions.ParseCaptureSpec</c>).
/// </remarks>
public sealed record CaptureSpec
{
    /// <summary>
    /// 촬영 선언의 폭과 높이 최댓값(화소)입니다.
    /// </summary>
    /// <remarks>장치의 한계가 아니라 UYVY 한 장의 바이트 수(폭 x 높이 x 2)가 int32 안에 들게 하는 값입니다.</remarks>
    public const int MaxFrameSide = 16384;

    /// <summary>
    /// 직접 지정에서 받는 노출 단계의 최솟값입니다.
    /// </summary>
    /// <remarks>UVC 노출 시간의 단위가 100us라 단계 -14(61us)부터는 카메라에 보낼 수 없습니다.</remarks>
    public const int MinExposureStep = -13;

    /// <summary>
    /// 직접 지정에서 받는 노출 단계의 최댓값입니다.
    /// </summary>
    /// <remarks>단계 0은 1초입니다. sdk_frame_rate는 1 이상이라 1초보다 긴 노출은 어떤 fps의 한 프레임에도 들지 않습니다.</remarks>
    public const int MaxExposureStep = 0;

    /// <summary>
    /// 게인의 최댓값입니다.
    /// </summary>
    /// <remarks>UVC 게인 제어 값이 부호 없는 2바이트라는 형식의 한계입니다. 장치마다 실제 범위는 이보다 좁습니다.</remarks>
    public const int MaxGain = 65535;

    /// <summary>
    /// 색온도(K)의 최댓값입니다. 최솟값은 1입니다.
    /// </summary>
    /// <remarks>UVC 색온도 제어 값이 부호 없는 2바이트라는 형식의 한계입니다. 장치마다 실제 범위는 이보다 좁습니다.</remarks>
    public const int MaxWhiteBalanceK = 65535;

    /// <summary>장치 이름에 들어 있는 문자열</summary>
    public string Device { get; init; } = "";

    public int Width { get; init; }

    public int Height { get; init; }

    public SettingsSpec Settings { get; init; }

    /// <summary>카메라마다 실측한 값(설계 문서 표 5)</summary>
    public SettleRule Settle { get; init; }

    public long TargetFrames { get; init; }

    /// <summary>이 밝기 이상을 포화 화소로 셉니다.</summary>
    public int ClipLevel { get; init; }

    /// <summary>쓰기가 밀릴 때 버틸 프레임 수</summary>
    public int BufferFrames { get; init; }

    public VideoCodec Codec { get; init; } = VideoCodec.Ffv1;

    /// <summary>
    /// 열화상 기록 주기(<c>RecordRateRule</c>). 0이면 받은 프레임을 모두 기록합니다. 선언 파일의 필수 키입니다.
    /// 관류 분석이 30초에 225장 이상을 요구하므로 111111(9.0Hz, 30Hz 장치에서 30초에 270장)을 씁니다.
    /// </summary>
    public long ThermalRecordPeriodUs { get; init; }
}

/// <summary>촬영 선언 검증에서 찾은 문제입니다.</summary>
public enum SpecProblem : byte
{
    NoDevice,

    /// <summary>폭과 높이는 1~<see cref="CaptureSpec.MaxFrameSide"/>이고 UYVY 색차 격자(폭 짝수)에 맞아야 합니다.</summary>
    BadSize,
    BadFrameRate,

    /// <summary>직접 지정의 노출 단계가 <see cref="CaptureSpec.MinExposureStep"/>~<see cref="CaptureSpec.MaxExposureStep"/> 밖</summary>
    BadExposure,

    /// <summary>0~<see cref="CaptureSpec.MaxGain"/> 밖</summary>
    BadGain,

    /// <summary>1~<see cref="CaptureSpec.MaxWhiteBalanceK"/> 밖. 자동 맞춤도 색온도는 이 값으로 고정합니다.</summary>
    BadWhiteBalance,
    BadTargetFrames,

    /// <summary>반영 프레임과 안정화 프레임은 1 이상</summary>
    BadSettleRule,

    /// <summary>1~255</summary>
    BadClipLevel,
    BadBufferFrames,

    /// <summary>음수</summary>
    BadThermalRecordPeriod,
}

public static partial class FlowFunctions
{
    /// <summary>
    /// 코덱에 맞는 영상 파일 확장자를 구합니다.
    /// </summary>
    /// <returns>"mkv"(FFV1) 또는 "avi"(무압축). 점은 넣지 않습니다</returns>
    public static string ExtensionOf(VideoCodec codec) => codec == VideoCodec.Ffv1 ? "mkv" : "avi";

    /// <summary>
    /// 촬영 선언에서 촬영 흐름의 규칙을 꺼냅니다.
    /// </summary>
    /// <returns>안정화 실측값, 목표 프레임 수, 설정 방식</returns>
    public static FlowRule RuleOf(CaptureSpec spec) => new()
    {
        Settle = spec.Settle,
        TargetFrames = spec.TargetFrames,
        Mode = spec.Settings.Mode,
    };

    /// <summary>
    /// 촬영 선언의 값 범위를 검증합니다.
    /// </summary>
    /// <param name="spec">검증할 선언</param>
    /// <returns>찾은 문제 목록. 비어 있으면 실행할 수 있는 선언입니다</returns>
    public static IReadOnlyList<SpecProblem> ValidateCaptureSpec(CaptureSpec spec)
    {
        var problems = new List<SpecProblem>();
        if (spec.Device.Length == 0)
        {
            problems.Add(SpecProblem.NoDevice);
        }
        if (spec.Width <= 0 || spec.Height <= 0 || spec.Width > CaptureSpec.MaxFrameSide ||
            spec.Height > CaptureSpec.MaxFrameSide || spec.Width % 2 != 0)
        {
            problems.Add(SpecProblem.BadSize);
        }
        SettingsSpec s = spec.Settings;
        if (s.SdkFrameRate <= 0 || s.SdkFrameRate > 255)
        {
            problems.Add(SpecProblem.BadFrameRate);
        }
        // 자동 맞춤은 노출 단계를 카메라에 보내지 않으므로 직접 지정일 때만 봅니다.
        if (s.Mode == SettingMode.Manual &&
            (s.ExposureStep < CaptureSpec.MinExposureStep || s.ExposureStep > CaptureSpec.MaxExposureStep))
        {
            problems.Add(SpecProblem.BadExposure);
        }
        if (s.Gain < 0 || s.Gain > CaptureSpec.MaxGain)
        {
            problems.Add(SpecProblem.BadGain);
        }
        if (s.WhiteBalanceK < 1 || s.WhiteBalanceK > CaptureSpec.MaxWhiteBalanceK)
        {
            problems.Add(SpecProblem.BadWhiteBalance);
        }
        if (spec.TargetFrames <= 0)
        {
            problems.Add(SpecProblem.BadTargetFrames);
        }
        SettleRule r = spec.Settle;
        if (r.ExposureAppliedFrame < 1 || r.GainAppliedFrame < 1 || r.WhiteBalanceAppliedFrame < 1 ||
            r.BlackLevelSettledFrame < 1)
        {
            problems.Add(SpecProblem.BadSettleRule);
        }
        if (spec.ClipLevel < 1 || spec.ClipLevel > 255)
        {
            problems.Add(SpecProblem.BadClipLevel);
        }
        if (spec.BufferFrames < 1)
        {
            problems.Add(SpecProblem.BadBufferFrames);
        }
        if (spec.ThermalRecordPeriodUs < 0)
        {
            problems.Add(SpecProblem.BadThermalRecordPeriod);
        }
        return problems;
    }

    /// <summary>
    /// JSON 선언 파일의 내용을 읽습니다.
    /// </summary>
    /// <param name="json">선언 파일 내용</param>
    /// <param name="error">실패하면 이유와 위치를 씁니다. 성공하면 빈 문자열입니다</param>
    /// <returns>읽은 선언. 모르는 키, 빠진 키, 형식이 다른 값, int32 범위를 벗어난 값이 있으면 null</returns>
    /// <remarks>
    /// 오타로 생긴 모르는 키를 조용히 넘기지 않고 거부합니다. 숫자는 정수만 받습니다.
    /// int32 칸에 들지 않는 정수는 잘라 넣지 않고 거부합니다.
    /// 값 범위는 보지 않습니다. 읽은 뒤 <see cref="ValidateCaptureSpec"/>으로 검증합니다.
    /// 오류 문장의 "n번째 바이트"는 <paramref name="json"/>을 UTF-8로 바꾼 바이트 위치입니다.
    /// </remarks>
    public static CaptureSpec? ParseCaptureSpec(string json, out string error) =>
        ParseCaptureSpec(Encoding.UTF8.GetBytes(json), out error);

    /// <summary>
    /// JSON 선언 파일의 바이트를 읽습니다. 파일을 읽은 바이트를 그대로 넘기면 UTF-8이 아닌 바이트와 BOM도 원본처럼
    /// 바이트 단위로 다뤄, 오류 문장의 "n번째 바이트"가 원본과 같은 숫자입니다.
    /// </summary>
    /// <param name="json">선언 파일의 바이트(UTF-8)</param>
    /// <param name="error">실패하면 이유와 위치를 씁니다. 성공하면 빈 문자열입니다</param>
    /// <returns>읽은 선언. 문자열 값에 든 UTF-8이 아닌 바이트는 U+FFFD로 바뀝니다</returns>
    public static CaptureSpec? ParseCaptureSpec(ReadOnlySpan<byte> json, out string error)
    {
        var errors = new SpecError();
        Json? root = new Reader(json.ToArray()).ReadDocument(errors);
        if (root is null)
        {
            error = errors.Message;
            return null;
        }
        if (root.Kind != JsonKind.Object)
        {
            error = "최상위 값은 객체여야 합니다";
            return null;
        }
        var top = new Fields(root, "", errors);
        string device = top.String("device");
        int width = top.Int32("width");
        int height = top.Int32("height");
        long targetFrames = top.Int("target_frames");
        int clipLevel = top.Int32("clip_level");
        int bufferFrames = top.Int32("buffer_frames");
        long thermalRecordPeriodUs = top.Int("thermal_record_period_us");
        string codec = top.String("codec");
        if (codec != "ffv1" && codec != "uncompressed")
        {
            errors.Fail("codec는 ffv1 또는 uncompressed입니다");
        }
        var settings = new SettingsSpec();
        if (top.Get("settings", JsonKind.Object) is { } settingsObject)
        {
            var f = new Fields(settingsObject, "settings.", errors);
            string mode = f.String("mode");
            if (mode != "auto" && mode != "manual")
            {
                errors.Fail("settings.mode는 auto 또는 manual입니다");
            }
            // 자동 맞춤에서는 노출 단계를 쓰지 않아 빠져도 됩니다.
            int exposureStep = 0;
            if (mode == "manual" || settingsObject.Members.ContainsKey("exposure_step"))
            {
                exposureStep = f.Int32("exposure_step");
            }
            int gain = f.Int32("gain");
            int whiteBalanceK = f.Int32("white_balance_k");
            int sdkFrameRate = f.Int32("sdk_frame_rate");
            settings = new SettingsSpec
            {
                Mode = mode == "auto" ? SettingMode.Auto : SettingMode.Manual,
                ExposureStep = exposureStep,
                Gain = gain,
                WhiteBalanceK = whiteBalanceK,
                SdkFrameRate = sdkFrameRate,
            };
            f.Finish();
        }
        var settle = new SettleRule();
        if (top.Get("settle", JsonKind.Object) is { } settleObject)
        {
            var f = new Fields(settleObject, "settle.", errors);
            int exposureAppliedFrame = f.Int32("exposure_applied_frame");
            int gainAppliedFrame = f.Int32("gain_applied_frame");
            int whiteBalanceAppliedFrame = f.Int32("white_balance_applied_frame");
            int blackLevelSettledFrame = f.Int32("black_level_settled_frame");
            settle = new SettleRule
            {
                ExposureAppliedFrame = exposureAppliedFrame,
                GainAppliedFrame = gainAppliedFrame,
                WhiteBalanceAppliedFrame = whiteBalanceAppliedFrame,
                BlackLevelSettledFrame = blackLevelSettledFrame,
            };
            f.Finish();
        }
        top.Finish();
        if (errors.Message.Length != 0)
        {
            error = errors.Message;
            return null;
        }
        error = "";
        return new CaptureSpec
        {
            Device = device,
            Width = width,
            Height = height,
            Settings = settings,
            Settle = settle,
            TargetFrames = targetFrames,
            ClipLevel = clipLevel,
            BufferFrames = bufferFrames,
            Codec = codec == "uncompressed" ? VideoCodec.Uncompressed : VideoCodec.Ffv1,
            ThermalRecordPeriodUs = thermalRecordPeriodUs,
        };
    }

    private enum JsonKind : byte
    {
        Null,
        Bool,
        Int,
        String,
        Object,
        Array,
    }

    // 선언 파일에 필요한 만큼만 읽는 JSON 값입니다. 정수, 문자열, 참거짓, 객체, 배열을 다룹니다.
    private sealed class Json
    {
        public JsonKind Kind { get; set; } = JsonKind.Null;

        public bool Boolean { get; set; }

        public long Integer { get; set; }

        public string Text { get; set; } = "";

        public SortedDictionary<string, Json> Members { get; } = new(Utf8ByteOrder.Instance);

        public List<Json> Items { get; } = new();
    }

    // std::map의 키 순서(바이트 순서)와 같게 둡니다. 모르는 키가 여럿이면 가장 앞선 키를 오류 문장에 씁니다.
    private sealed class Utf8ByteOrder : IComparer<string>
    {
        public static readonly Utf8ByteOrder Instance = new();

        public int Compare(string? a, string? b) =>
            new ReadOnlySpan<byte>(Encoding.UTF8.GetBytes(a!)).SequenceCompareTo(Encoding.UTF8.GetBytes(b!));
    }

    // 첫 오류만 남깁니다.
    private sealed class SpecError
    {
        public string Message { get; private set; } = "";

        public void Fail(string message)
        {
            if (Message.Length == 0)
            {
                Message = message;
            }
        }
    }

    // 문서를 UTF-8 바이트로 읽으므로 오류 위치가 바이트 위치입니다. std::isspace는 C 로캘의 공백 6개(' ', \t, \n, \v, \f, \r)입니다.
    private sealed class Reader
    {
        private readonly byte[] text;
        private int pos;
        private string error = "";

        public Reader(byte[] json)
        {
            text = json;
        }

        public Json? ReadDocument(SpecError errors)
        {
            Json? value = ReadValue();
            SkipSpace();
            if (value is not null && pos != text.Length)
            {
                error = "문서 끝 뒤에 값이 더 있습니다";
                value = null;
            }
            if (value is null)
            {
                errors.Fail(error + " (" + pos.ToString(CultureInfo.InvariantCulture) + "번째 바이트)");
            }
            return value;
        }

        private static bool IsSpace(byte b) => b == ' ' || (b >= '\t' && b <= '\r');

        private static bool IsDigit(byte b) => b >= '0' && b <= '9';

        private void SkipSpace()
        {
            while (pos < text.Length && IsSpace(text[pos]))
            {
                ++pos;
            }
        }

        private bool Consume(string word)
        {
            if (pos + word.Length > text.Length)
            {
                return false;
            }
            for (int i = 0; i < word.Length; ++i)
            {
                if (text[pos + i] != word[i])
                {
                    return false;
                }
            }
            pos += word.Length;
            return true;
        }

        private Json? Fail(string message)
        {
            error = message;
            return null;
        }

        private Json? ReadValue()
        {
            SkipSpace();
            if (pos >= text.Length)
            {
                return Fail("값이 없습니다");
            }
            var value = new Json();
            byte c = text[pos];
            if (c == '{')
            {
                return ReadObject();
            }
            if (c == '[')
            {
                return ReadArray();
            }
            if (c == '"')
            {
                value.Kind = JsonKind.String;
                string? s = ReadString();
                if (s is null)
                {
                    return null;
                }
                value.Text = s;
                return value;
            }
            if (Consume("true"))
            {
                value.Kind = JsonKind.Bool;
                value.Boolean = true;
                return value;
            }
            if (Consume("false"))
            {
                value.Kind = JsonKind.Bool;
                return value;
            }
            if (Consume("null"))
            {
                return value;
            }
            if (c == '-' || IsDigit(c))
            {
                int start = pos;
                if (c == '-')
                {
                    ++pos;
                }
                while (pos < text.Length && IsDigit(text[pos]))
                {
                    ++pos;
                }
                if (pos < text.Length && (text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E'))
                {
                    return Fail("선언 파일의 숫자는 정수만 씁니다");
                }
                string digits = Encoding.ASCII.GetString(text, start, pos - start);
                if (digits == "-" || digits.Length > 18)
                {
                    return Fail("정수 형식이 아닙니다");
                }
                value.Kind = JsonKind.Int;
                value.Integer = long.Parse(digits, CultureInfo.InvariantCulture);
                return value;
            }
            return Fail("알 수 없는 값입니다");
        }

        private string? ReadString()
        {
            ++pos; // 여는 따옴표
            var bytes = new List<byte>();
            while (pos < text.Length)
            {
                byte c = text[pos++];
                if (c == '"')
                {
                    return Encoding.UTF8.GetString(bytes.ToArray());
                }
                if (c == '\\')
                {
                    if (pos >= text.Length)
                    {
                        break;
                    }
                    switch ((char)text[pos++])
                    {
                        case '"':
                            bytes.Add((byte)'"');
                            break;
                        case '\\':
                            bytes.Add((byte)'\\');
                            break;
                        case '/':
                            bytes.Add((byte)'/');
                            break;
                        case 'n':
                            bytes.Add((byte)'\n');
                            break;
                        case 't':
                            bytes.Add((byte)'\t');
                            break;
                        default:
                            error = "지원하지 않는 이스케이프입니다";
                            return null;
                    }
                    continue;
                }
                bytes.Add(c);
            }
            error = "문자열이 닫히지 않았습니다";
            return null;
        }

        private Json? ReadArray()
        {
            ++pos; // [
            var array = new Json { Kind = JsonKind.Array };
            SkipSpace();
            if (Consume("]"))
            {
                return array;
            }
            for (; ; )
            {
                Json? value = ReadValue();
                if (value is null)
                {
                    return null;
                }
                array.Items.Add(value);
                SkipSpace();
                if (Consume(","))
                {
                    continue;
                }
                if (Consume("]"))
                {
                    return array;
                }
                return Fail("배열이 ,나 ]로 이어지지 않습니다");
            }
        }

        private Json? ReadObject()
        {
            ++pos; // {
            var obj = new Json { Kind = JsonKind.Object };
            SkipSpace();
            if (pos < text.Length && text[pos] == '}')
            {
                ++pos;
                return obj;
            }
            for (; ; )
            {
                SkipSpace();
                if (pos >= text.Length || text[pos] != '"')
                {
                    return Fail("키는 문자열이어야 합니다");
                }
                string? key = ReadString();
                if (key is null)
                {
                    return null;
                }
                SkipSpace();
                if (!Consume(":"))
                {
                    return Fail("키 뒤에 :가 없습니다");
                }
                Json? value = ReadValue();
                if (value is null)
                {
                    return null;
                }
                if (obj.Members.ContainsKey(key))
                {
                    return Fail("같은 키가 두 번 나옵니다");
                }
                obj.Members[key] = value;
                SkipSpace();
                if (Consume(","))
                {
                    continue;
                }
                if (Consume("}"))
                {
                    return obj;
                }
                return Fail("객체가 ,나 }로 이어지지 않습니다");
            }
        }
    }

    // 객체 하나에서 키를 꺼내며, 꺼내지 않은 키가 남으면 모르는 키로 봅니다.
    // 꺼내지 못한 값은 0이나 빈 문자열이고, 이유는 SpecError에 남습니다.
    private sealed class Fields
    {
        private readonly Json obj;
        private readonly string path;
        private readonly SpecError errors;
        private readonly List<string> used = new();

        public Fields(Json obj, string path, SpecError errors)
        {
            this.obj = obj;
            this.path = path;
            this.errors = errors;
        }

        public Json? Get(string key, JsonKind kind)
        {
            used.Add(key);
            if (!obj.Members.TryGetValue(key, out Json? member))
            {
                errors.Fail("키가 없습니다: " + path + key);
                return null;
            }
            if (member.Kind != kind)
            {
                errors.Fail("형식이 다릅니다: " + path + key);
                return null;
            }
            return member;
        }

        public long Int(string key) => Get(key, JsonKind.Int)?.Integer ?? 0;

        public int Int32(string key)
        {
            Json? v = Get(key, JsonKind.Int);
            if (v is null)
            {
                return 0;
            }
            // 그대로 자르면 4294969216이 1920이 되어 검증을 통과하므로 범위 밖 값은 거부합니다.
            if (v.Integer < int.MinValue || v.Integer > int.MaxValue)
            {
                errors.Fail("int32 범위를 벗어났습니다: " + path + key);
                return 0;
            }
            return (int)v.Integer;
        }

        public string String(string key) => Get(key, JsonKind.String)?.Text ?? "";

        public void Finish()
        {
            foreach (string key in obj.Members.Keys)
            {
                if (!used.Contains(key))
                {
                    errors.Fail("모르는 키입니다: " + path + key);
                }
            }
        }
    }
}
