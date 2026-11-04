using System.Globalization;
using System.Numerics;
using System.Text;
using DiabeticFoot.Core.Pixel;

namespace DiabeticFoot.Core.Record;

/// <summary>분석 전 확인과 열화상 변환에 쓰는 사이드카 값입니다. <see cref="RecordFunctions.BuildSidecarJson"/>이 쓴 JSON에서 읽습니다.</summary>
public readonly record struct SidecarSummary
{
    public StreamKind Stream { get; init; }

    /// <summary>전체 화면이면 비웁니다.</summary>
    public FootSide? Foot { get; init; }

    /// <summary>format.width입니다.</summary>
    public int FrameWidth { get; init; }

    /// <summary>format.height입니다.</summary>
    public int FrameHeight { get; init; }

    /// <summary>영상에 저장한 영역입니다.</summary>
    public Rect Crop { get; init; }

    public long WrittenFrames { get; init; }

    public long WrittenInRgbWindow { get; init; }

    /// <summary>카메라 단 누락입니다.</summary>
    public long MissingFrames { get; init; }

    /// <summary>저장 누락입니다.</summary>
    public long DroppedFrames { get; init; }
}

/// <summary>분석 전 확인에서 찾은 문제의 종류입니다.</summary>
public enum InputProblem : byte
{
    /// <summary>필요한 역할의 파일이 촬영 폴더에 없습니다.</summary>
    MissingFile,

    /// <summary>사이드카 파일은 있으나 읽지 못했습니다.</summary>
    UnreadableSidecar,

    /// <summary>RGB 녹화 구간 안 열화상 장 수가 최솟값보다 적습니다.</summary>
    FewThermalFrames,

    /// <summary>열화상에 카메라 단 누락이나 저장 누락이 있습니다. 분석은 할 수 있는 경고입니다.</summary>
    ThermalGap,

    /// <summary>열화상 파일이 하나도 없습니다. 분석은 할 수 있는 경고입니다.</summary>
    ThermalAbsent,

    /// <summary>좌우 영상이 세로로 긴 영상이 아닙니다.</summary>
    NotPortrait,
}

/// <summary>분석 전 확인에서 찾은 문제 하나입니다.</summary>
/// <param name="Problem">문제의 종류입니다.</param>
/// <param name="Role">문제가 있는 파일의 역할입니다.</param>
/// <param name="Blocking">true면 분석을 시작하지 않습니다. 경고는 false입니다.</param>
/// <param name="Value">잰 값(장 수, 누락 수, 폭)입니다. 없으면 0입니다.</param>
/// <param name="Limit">기준값(최소 장 수, 높이)입니다. 없으면 0입니다.</param>
public sealed record InputFinding(
    InputProblem Problem = InputProblem.MissingFile,
    FileRole Role = FileRole.Manifest,
    bool Blocking = true,
    long Value = 0,
    long Limit = 0);

/// <summary>분석 전 확인의 입력입니다. 파일 입출력은 platform이 하고 결과만 넘깁니다.</summary>
public sealed record AnalysisFiles
{
    /// <summary>관류 분석이 요구하는 RGB 녹화 구간 안 열화상 장 수의 최솟값입니다(30초에 225장).</summary>
    public const long MinThermalFramesInRgbWindow = 225;

    /// <summary>촬영 폴더에 실제로 있는 파일의 역할입니다.</summary>
    public IReadOnlyList<FileRole> Present { get; init; } = Array.Empty<FileRole>();

    /// <summary>왼발 영상 사이드카입니다. 없거나 읽지 못하면 비웁니다.</summary>
    public SidecarSummary? Left { get; init; }

    /// <summary>오른발 영상 사이드카입니다.</summary>
    public SidecarSummary? Right { get; init; }

    /// <summary>열화상 사이드카입니다.</summary>
    public SidecarSummary? Thermal { get; init; }
}

public static partial class RecordFunctions
{
    /// <summary>
    /// 사이드카 JSON에서 분석 전 확인에 쓰는 값을 읽습니다.
    /// </summary>
    /// <param name="json">사이드카 파일 내용</param>
    /// <param name="error">실패하면 이유를 씁니다. 성공하면 빈 문자열입니다</param>
    /// <returns>읽은 값. JSON 문법이 틀리거나, schema_version이 3보다 작거나, 필요한 키가 없으면 null입니다</returns>
    /// <remarks>
    /// 필요한 키는 stream, foot, format.width, format.height, crop, frames.written, frames.written_in_rgb_window,
    /// frames.missing, frames.dropped입니다. 다른 키는 읽지 않습니다.
    /// </remarks>
    public static SidecarSummary? ReadSidecarSummary(string json, out string error)
    {
        error = "";
        SidecarJsonValue? root = new SidecarJsonReader(Encoding.UTF8.GetBytes(json)).ReadDocument();
        if (root is null || root.Kind != SidecarJsonValue.ValueKind.Object)
        {
            error = "사이드카 JSON을 읽지 못함";
            return null;
        }
        // written_in_rgb_window는 판 3부터 있습니다.
        if (!ReadInt(root, "schema_version", out long version) || version < 3)
        {
            error = "사이드카 schema_version이 3보다 작음";
            return null;
        }

        var s = new SidecarSummary();
        SidecarJsonValue? stream = Member(root, "stream");
        if (stream is null || stream.Kind != SidecarJsonValue.ValueKind.String)
        {
            error = "사이드카에 stream이 없음";
            return null;
        }
        if (stream.Text == "rgb")
        {
            s = s with { Stream = StreamKind.Rgb };
        }
        else if (stream.Text == "thermal")
        {
            s = s with { Stream = StreamKind.Thermal };
        }
        else
        {
            error = "사이드카의 stream을 모름: " + stream.Text;
            return null;
        }
        SidecarJsonValue? foot = Member(root, "foot");
        if (foot is null)
        {
            error = "사이드카에 foot이 없음";
            return null;
        }
        if (foot.Kind == SidecarJsonValue.ValueKind.String && foot.Text == "left")
        {
            s = s with { Foot = FootSide.Left };
        }
        else if (foot.Kind == SidecarJsonValue.ValueKind.String && foot.Text == "right")
        {
            s = s with { Foot = FootSide.Right };
        }
        else if (foot.Kind != SidecarJsonValue.ValueKind.Null)
        {
            error = "사이드카의 foot을 모름";
            return null;
        }
        SidecarJsonValue? format = Member(root, "format");
        if (!ReadInt32(format, "width", out int width) || !ReadInt32(format, "height", out int height))
        {
            error = "사이드카에 format.width, format.height가 없음";
            return null;
        }
        s = s with { FrameWidth = width, FrameHeight = height };
        SidecarJsonValue? crop = Member(root, "crop");
        if (!ReadInt32(crop, "x", out int cropX) || !ReadInt32(crop, "y", out int cropY) ||
            !ReadInt32(crop, "width", out int cropWidth) || !ReadInt32(crop, "height", out int cropHeight))
        {
            error = "사이드카에 crop이 없음";
            return null;
        }
        s = s with { Crop = new Rect(cropX, cropY, cropWidth, cropHeight) };
        SidecarJsonValue? frames = Member(root, "frames");
        if (!ReadInt(frames, "written", out long written) ||
            !ReadInt(frames, "written_in_rgb_window", out long writtenInWindow) ||
            !ReadInt(frames, "missing", out long missing) || !ReadInt(frames, "dropped", out long dropped))
        {
            error = "사이드카에 frames의 장 수가 없음";
            return null;
        }
        return s with { WrittenFrames = written, WrittenInRgbWindow = writtenInWindow, MissingFrames = missing, DroppedFrames = dropped };
    }

    /// <summary>
    /// 열화상 프레임에서 환자 왼발과 오른발 영역을 정합니다.
    /// </summary>
    /// <param name="frameWidth">열화상 프레임 폭</param>
    /// <param name="frameHeight">열화상 프레임 높이</param>
    /// <param name="pixelRegions">직접 준 영역(화소 왼쪽, 화소 오른쪽). null이면 프레임 전체를 가운데 세로선에서 나눕니다</param>
    /// <param name="pixelLeftFoot">화소 왼쪽에 찍히는 발(<see cref="ShotInfo.PixelLeftFoot"/>)</param>
    /// <returns>왼발 영역과 오른발 영역. 프레임 폭이 2보다 작으면 둘 다 빈 영역입니다</returns>
    /// <remarks>
    /// 열화상은 RGB와 맞춘 보정값이 없어 RGB 영역을 옮겨 쓰지 않고 따로 정합니다.
    /// 가운데 세로선 분할에서 폭이 홀수이면 가운데 한 열을 버리고 두 영역의 폭을 같게 합니다.
    /// </remarks>
    public static (Rect LeftFoot, Rect RightFoot) ThermalFootRegions(
        int frameWidth, int frameHeight, (Rect PixelLeft, Rect PixelRight)? pixelRegions, FootSide pixelLeftFoot)
    {
        (Rect Left, Rect Right) byPixel = default;
        if (pixelRegions is { } given)
        {
            byPixel = given;
        }
        else if (frameWidth >= 2 && frameHeight > 0)
        {
            int half = frameWidth / 2;
            byPixel = (new Rect(0, 0, half, frameHeight), new Rect(frameWidth - half, 0, half, frameHeight));
        }
        if (pixelLeftFoot == FootSide.Left)
        {
            return byPixel;
        }
        return (byPixel.Right, byPixel.Left);
    }

    /// <summary>
    /// 관류 분석을 시작해도 되는지 확인합니다.
    /// </summary>
    /// <param name="files">촬영 폴더에서 읽은 결과</param>
    /// <param name="minThermalFrames">RGB 녹화 구간 안 열화상 장 수의 최솟값. 보통 <see cref="AnalysisFiles.MinThermalFramesInRgbWindow"/></param>
    /// <returns>찾은 문제 목록(아래 순서). 비어 있거나 Blocking이 모두 false이면 분석을 시작해도 됩니다</returns>
    /// <remarks>
    /// 확인 순서: 필요한 역할(좌우 영상, 좌우 영상 사이드카, 열화상 사이드카, 좌우 열화상 .npy)의 파일이 있는지,
    /// 있는 사이드카를 읽었는지, 좌우 영상의 저장 영역이 세로로 긴지(높이 &gt; 폭), RGB 녹화 구간 안 열화상 장 수가
    /// minThermalFrames 이상인지, 열화상에 누락이 있는지입니다.
    /// 좌우 영상 문제는 분석을 막습니다(Blocking). 열화상 문제는 막지 않는 경고입니다. 커프 단계 추정은 RGB 영상만 쓰고,
    /// 이진 PAD 판정을 켠 분석도 열화상이 없거나 모자라면 열화상을 빼고 RGB만으로 판정하기 때문입니다.
    /// </remarks>
    public static IReadOnlyList<InputFinding> CheckAnalysisInputs(AnalysisFiles files, long minThermalFrames)
    {
        var findings = new List<InputFinding>();
        // 좌우 영상이 없으면 분석할 수 없습니다. 커프 단계 추정은 RGB 영상만 쓰므로 열화상 문제는 막지 않는 경고로 남깁니다.
        // 이진 PAD 판정을 켠 분석에서는 열화상을 빼고 RGB만으로 판정합니다.
        FileRole[] rgbRoles = { FileRole.LeftVideo, FileRole.LeftSidecar, FileRole.RightVideo, FileRole.RightSidecar };
        FileRole[] thermalRoles = { FileRole.ThermalSidecar, FileRole.ThermalLeftArray, FileRole.ThermalRightArray };
        foreach (FileRole role in rgbRoles)
        {
            if (!Has(files.Present, role))
            {
                findings.Add(new InputFinding(InputProblem.MissingFile, role, true, 0, 0));
            }
        }
        bool anyThermal = thermalRoles.Any(role => Has(files.Present, role));
        if (!anyThermal)
        {
            findings.Add(new InputFinding(InputProblem.ThermalAbsent, FileRole.ThermalSidecar, false, 0, 0));
        }
        else
        {
            foreach (FileRole role in thermalRoles)
            {
                if (!Has(files.Present, role))
                {
                    findings.Add(new InputFinding(InputProblem.MissingFile, role, false, 0, 0));
                }
            }
        }
        (FileRole Role, SidecarSummary? Summary)[] sidecars =
        {
            (FileRole.LeftSidecar, files.Left),
            (FileRole.RightSidecar, files.Right),
            (FileRole.ThermalSidecar, files.Thermal),
        };
        foreach (var (role, summary) in sidecars)
        {
            if (Has(files.Present, role) && !summary.HasValue)
            {
                findings.Add(new InputFinding(InputProblem.UnreadableSidecar, role, role != FileRole.ThermalSidecar, 0, 0));
            }
        }
        // 분석은 한 발씩 세로로 긴 영상만 받습니다. 저장 영역(crop)이 영상의 크기입니다.
        (FileRole Role, SidecarSummary? Summary)[] videos = { (FileRole.LeftVideo, files.Left), (FileRole.RightVideo, files.Right) };
        foreach (var (role, summary) in videos)
        {
            if (summary is { } v && v.Crop.Height <= v.Crop.Width)
            {
                findings.Add(new InputFinding(InputProblem.NotPortrait, role, true, v.Crop.Width, v.Crop.Height));
            }
        }
        if (files.Thermal is { } thermal)
        {
            if (thermal.WrittenInRgbWindow < minThermalFrames)
            {
                findings.Add(new InputFinding(InputProblem.FewThermalFrames, FileRole.ThermalSidecar, false, thermal.WrittenInRgbWindow, minThermalFrames));
            }
            long gaps = thermal.MissingFrames + thermal.DroppedFrames;
            if (gaps > 0)
            {
                findings.Add(new InputFinding(InputProblem.ThermalGap, FileRole.ThermalSidecar, false, gaps, 0));
            }
        }
        return findings;
    }

    /// <summary>
    /// 문제 목록에 분석을 막는 문제가 있는지 봅니다.
    /// </summary>
    /// <param name="findings"><see cref="CheckAnalysisInputs"/>의 결과</param>
    /// <returns>Blocking인 문제가 하나라도 있으면 true</returns>
    public static bool HasBlockingFinding(IReadOnlyList<InputFinding> findings) => findings.Any(f => f.Blocking);

    private static bool Has(IReadOnlyList<FileRole> roles, FileRole role)
    {
        foreach (FileRole r in roles)
        {
            if (r == role)
            {
                return true;
            }
        }
        return false;
    }

    private static SidecarJsonValue? Member(SidecarJsonValue? obj, string key)
    {
        if (obj is null || obj.Kind != SidecarJsonValue.ValueKind.Object)
        {
            return null;
        }
        return obj.Members.TryGetValue(key, out SidecarJsonValue? value) ? value : null;
    }

    private static bool ReadInt(SidecarJsonValue? obj, string key, out long value)
    {
        value = 0;
        SidecarJsonValue? v = Member(obj, key);
        if (v is null || !v.Integer.HasValue)
        {
            return false;
        }
        value = v.Integer.Value;
        return true;
    }

    private static bool ReadInt32(SidecarJsonValue? obj, string key, out int value)
    {
        value = 0;
        if (!ReadInt(obj, key, out long v) || v < int.MinValue || v > int.MaxValue)
        {
            return false;
        }
        value = (int)v;
        return true;
    }
}

// 사이드카에는 소수(clock.rate)가 있어 정수만 받는 선언 파일 읽기와 따로 둡니다. 소수는 건너뛰고 정수만 값으로 남깁니다.
internal sealed class SidecarJsonValue
{
    public enum ValueKind : byte
    {
        Null,
        Bool,
        Number,
        String,
        Object,
        Array,
    }

    public ValueKind Kind { get; set; } = ValueKind.Null;

    public long? Integer { get; set; }

    public string Text { get; set; } = "";

    public Dictionary<string, SidecarJsonValue> Members { get; } = new();
}

// 원본이 바이트 위치로 읽으므로 UTF-8 바이트 위에서 같은 규칙으로 읽습니다.
internal sealed class SidecarJsonReader
{
    private const int MaxDepth = 32;
    private readonly byte[] _json;
    private int _pos;

    public SidecarJsonReader(byte[] json) => _json = json;

    public SidecarJsonValue? ReadDocument()
    {
        SidecarJsonValue? value = ReadValue(0);
        SkipSpace();
        if (value is not null && _pos != _json.Length)
        {
            return null;
        }
        return value;
    }

    private void SkipSpace()
    {
        while (_pos < _json.Length && (_json[_pos] == ' ' || _json[_pos] == '\n' || _json[_pos] == '\r' || _json[_pos] == '\t'))
        {
            _pos++;
        }
    }

    private bool Take(char c)
    {
        SkipSpace();
        if (_pos < _json.Length && _json[_pos] == c)
        {
            _pos++;
            return true;
        }
        return false;
    }

    private bool TakeWord(string word)
    {
        if (_pos + word.Length > _json.Length)
        {
            return false;
        }
        for (int i = 0; i < word.Length; i++)
        {
            if (_json[_pos + i] != word[i])
            {
                return false;
            }
        }
        _pos += word.Length;
        return true;
    }

    private SidecarJsonValue? ReadValue(int depth)
    {
        if (depth > MaxDepth)
        {
            return null;
        }
        SkipSpace();
        if (_pos >= _json.Length)
        {
            return null;
        }
        char c = (char)_json[_pos];
        if (c == '{')
        {
            return ReadObject(depth);
        }
        var value = new SidecarJsonValue();
        if (c == '[')
        {
            value.Kind = SidecarJsonValue.ValueKind.Array;
            return ReadArray(depth) ? value : null;
        }
        if (c == '"')
        {
            value.Kind = SidecarJsonValue.ValueKind.String;
            string? text = ReadString();
            if (text is null)
            {
                return null;
            }
            value.Text = text;
            return value;
        }
        if (TakeWord("null"))
        {
            return value;
        }
        if (TakeWord("true") || TakeWord("false"))
        {
            value.Kind = SidecarJsonValue.ValueKind.Bool;
            return value;
        }
        return ReadNumber(value) ? value : null;
    }

    private bool ReadNumber(SidecarJsonValue value)
    {
        int start = _pos;
        if (_pos < _json.Length && _json[_pos] == '-')
        {
            _pos++;
        }
        bool fraction = false;
        while (_pos < _json.Length)
        {
            char c = (char)_json[_pos];
            if (c >= '0' && c <= '9')
            {
                _pos++;
            }
            else if (c == '.' || c == 'e' || c == 'E' || c == '+' || (c == '-' && _pos > start))
            {
                fraction = true;
                _pos++;
            }
            else
            {
                break;
            }
        }
        string text = Encoding.ASCII.GetString(_json, start, _pos - start);
        if (text.Length == 0 || text == "-")
        {
            return false;
        }
        value.Kind = SidecarJsonValue.ValueKind.Number;
        if (!fraction)
        {
            value.Integer = ParseSaturating(text);
        }
        return true;
    }

    // strtoll처럼 범위를 넘는 값은 오류 없이 가장 큰(작은) 값으로 맞춥니다.
    private static long ParseSaturating(string text)
    {
        var parsed = BigInteger.Parse(text, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture);
        if (parsed > long.MaxValue)
        {
            return long.MaxValue;
        }
        if (parsed < long.MinValue)
        {
            return long.MinValue;
        }
        return (long)parsed;
    }

    private string? ReadString()
    {
        _pos++;  // 여는 따옴표
        var output = new List<byte>();
        while (_pos < _json.Length)
        {
            byte c = _json[_pos++];
            if (c == '"')
            {
                return Encoding.UTF8.GetString(output.ToArray());
            }
            if (c != '\\')
            {
                output.Add(c);
                continue;
            }
            if (_pos >= _json.Length)
            {
                return null;
            }
            byte e = _json[_pos++];
            switch ((char)e)
            {
                case '"':
                case '\\':
                case '/':
                    output.Add(e);
                    break;
                case 'n':
                    output.Add((byte)'\n');
                    break;
                case 'r':
                    output.Add((byte)'\r');
                    break;
                case 't':
                    output.Add((byte)'\t');
                    break;
                case 'b':
                    output.Add((byte)'\b');
                    break;
                case 'f':
                    output.Add((byte)'\f');
                    break;
                case 'u':
                    // 사이드카는 제어 문자만 이 형식으로 씁니다. 필요한 키의 값에는 나오지 않으므로 자리만 건너뜁니다.
                    if (_pos + 4 > _json.Length)
                    {
                        return null;
                    }
                    _pos += 4;
                    output.Add((byte)'?');
                    break;
                default:
                    return null;
            }
        }
        return null;
    }

    private bool ReadArray(int depth)
    {
        _pos++;
        if (Take(']'))
        {
            return true;
        }
        do
        {
            if (ReadValue(depth + 1) is null)
            {
                return false;
            }
        }
        while (Take(','));
        return Take(']');
    }

    private SidecarJsonValue? ReadObject(int depth)
    {
        var obj = new SidecarJsonValue { Kind = SidecarJsonValue.ValueKind.Object };
        _pos++;
        if (!Take('}'))
        {
            do
            {
                SkipSpace();
                if (_pos >= _json.Length || _json[_pos] != '"')
                {
                    return null;
                }
                string? key = ReadString();
                if (key is null || !Take(':'))
                {
                    return null;
                }
                SidecarJsonValue? member = ReadValue(depth + 1);
                if (member is null)
                {
                    return null;
                }
                obj.Members[key] = member;
            }
            while (Take(','));
            if (!Take('}'))
            {
                return null;
            }
        }
        return obj;
    }
}
