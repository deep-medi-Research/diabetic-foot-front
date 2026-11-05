using System.Globalization;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform;

/// <summary>
/// 매니페스트에서 결과 창이 쓰는 값입니다. 파일 이름은 `files` 항목의 촬영 폴더 기준 이름이고, 없으면 빈 문자열입니다.
/// </summary>
public sealed record ShotManifest
{
    public string LeftVideo { get; init; } = "";

    public string LeftSidecar { get; init; } = "";

    public string RightVideo { get; init; } = "";

    public string RightSidecar { get; init; } = "";

    public string ThermalRaw { get; init; } = "";

    public string ThermalSidecar { get; init; } = "";

    /// <summary>`pixel_left_foot`: RGB와 열화상 프레임의 화소 왼쪽에 찍힌 발. 없으면 null입니다.</summary>
    public FootSide? PixelLeftFoot { get; init; }

    /// <summary>`criterion`: 촬영 때 입력한 검사 기준(커프 압력, 수축기 혈압 대비 %). 없거나 음수이면 null입니다.</summary>
    public int? Criterion { get; init; }

    /// <summary>`blood_pressure.ankle_level.name`: 촬영 때 고른 발목 혈압 단계(0, V, D, M, S). schema 6 전 매니페스트이면 비었습니다.</summary>
    public string AnkleLevel { get; init; } = "";

    /// <summary>`lighting.source`: "external"(외부 조명), "app"(앱이 켜는 조명), "unknown". 없으면 비었습니다.</summary>
    public string LightSource { get; init; } = "";

    /// <summary>`lighting.product`: 조명 제품 설명. 없으면 비었습니다.</summary>
    public string LightProduct { get; init; } = "";
}

/// <summary>RGB 사이드카의 `settings`: 녹화 때 카메라에 적용한 설정입니다. 기록에 없는 값은 null입니다.</summary>
public sealed record RecordedCameraSettings
{
    /// <summary>"manual" 또는 "auto". 없으면 비었습니다.</summary>
    public string Mode { get; init; } = "";

    /// <summary>노출 단계(See3CAM, 작을수록 짧음)입니다.</summary>
    public int? ExposureStep { get; init; }

    /// <summary>노출 시간(밀리초)입니다.</summary>
    public double? ExposureMs { get; init; }

    public int? Gain { get; init; }

    /// <summary>자동 화이트밸런스입니다.</summary>
    public bool? WhiteBalanceAuto { get; init; }

    /// <summary>고정 화이트밸런스 색온도(K)입니다.</summary>
    public int? WhiteBalanceK { get; init; }

    /// <summary>녹화 동안 설정을 잠갔는지입니다.</summary>
    public bool? Locked { get; init; }
}

/// <summary>사이드카에서 결과 창이 쓰는 값입니다.</summary>
public sealed record SidecarFrames
{
    /// <summary>format.width</summary>
    public int Width { get; init; }

    /// <summary>format.height</summary>
    public int Height { get; init; }

    /// <summary>format.frame_rate: 녹화 fps. 없으면 null입니다.</summary>
    public int? FrameRate { get; init; }

    /// <summary>settings: RGB 카메라 설정. 열화상 사이드카는 기록이 없어 모두 비었거나 null입니다.</summary>
    public RecordedCameraSettings Camera { get; init; } = new();

    /// <summary>per_frame.common_time_us: 파일에 쓴 프레임마다의 공통 시각입니다. 파일 안 순서와 같습니다.</summary>
    public IReadOnlyList<Timestamp> CommonTime { get; init; } = Array.Empty<Timestamp>();

    /// <summary>display_range: 열화상 확인용 영상의 표시 범위. RGB 사이드카와 값이 null인 열화상 사이드카는 null입니다.</summary>
    public Y16Range? DisplayRange { get; init; }
}

/// <summary>결과 창이 촬영 폴더의 매니페스트, 사이드카, 열화상 원시값을 읽습니다.</summary>
public static class ShotReview
{
    /// <summary>
    /// 매니페스트 내용에서 영상, 사이드카, 열화상 원시값 파일 이름과 화소 왼쪽 발을 읽습니다.
    /// </summary>
    /// <param name="manifestJson">`&lt;폴더 이름&gt;_shot.json`의 내용</param>
    /// <param name="error">읽지 못하면 이유를 씁니다. 읽었으면 빈 문자열입니다</param>
    /// <returns>파일 이름. JSON 문법이 틀리거나 `files.left_video`, `files.right_video`가 없으면 null</returns>
    public static ShotManifest? ReadShotManifest(string manifestJson, out string error)
    {
        string Take(string key) => new JsonReader(manifestJson).FindString("files", key) ?? "";
        var foot = new JsonReader(manifestJson).FindString("pixel_left_foot");
        var criterion = Int32At(manifestJson, "criterion");
        var files = new ShotManifest
        {
            LeftVideo = Take("left_video"),
            LeftSidecar = Take("left_sidecar"),
            RightVideo = Take("right_video"),
            RightSidecar = Take("right_sidecar"),
            ThermalRaw = Take("thermal_raw"),
            ThermalSidecar = Take("thermal_sidecar"),
            PixelLeftFoot = foot switch { "left" => FootSide.Left, "right" => FootSide.Right, _ => null },
            Criterion = criterion is >= 0 ? criterion : null,
            AnkleLevel = new JsonReader(manifestJson).FindString("blood_pressure", "ankle_level", "name") ?? "",
            LightSource = new JsonReader(manifestJson).FindString("lighting", "source") ?? "",
            LightProduct = new JsonReader(manifestJson).FindString("lighting", "product") ?? "",
        };
        if (files.LeftVideo.Length == 0 || files.RightVideo.Length == 0)
        {
            error = "매니페스트에 files.left_video 또는 files.right_video가 없거나 JSON을 읽지 못했습니다";
            return null;
        }
        error = "";
        return files;
    }

    public static ShotManifest? ReadShotManifest(string manifestJson) => ReadShotManifest(manifestJson, out _);

    /// <summary>
    /// 사이드카 내용에서 프레임 크기, 프레임별 공통 시각, 표시 범위를 읽습니다.
    /// </summary>
    /// <param name="sidecarJson">사이드카 파일 내용</param>
    /// <param name="error">읽지 못하면 이유를 씁니다. 읽었으면 빈 문자열입니다</param>
    /// <returns>
    /// 읽은 값. JSON 문법이 틀리거나, format.width, format.height, per_frame.common_time_us가 없거나,
    /// 공통 시각에 정수가 아닌 값이 있으면 null
    /// </returns>
    public static SidecarFrames? ReadSidecarFrames(string sidecarJson, out string error)
    {
        error = "";
        if (IntAt(sidecarJson, "format", "width") is not { } width || IntAt(sidecarJson, "format", "height") is not { } height ||
            width <= 0 || height <= 0 || width > int.MaxValue || height > int.MaxValue)
        {
            error = "사이드카에 format.width 또는 format.height가 없거나 JSON을 읽지 못했습니다";
            return null;
        }
        var camera = new RecordedCameraSettings
        {
            Mode = new JsonReader(sidecarJson).FindString("settings", "mode") ?? "",
            ExposureStep = Int32At(sidecarJson, "settings", "exposure_step"),
            ExposureMs = DoubleAt(sidecarJson, "settings", "exposure_ms"),
            Gain = Int32At(sidecarJson, "settings", "gain"),
            WhiteBalanceAuto = BoolAt(sidecarJson, "settings", "white_balance_auto"),
            WhiteBalanceK = Int32At(sidecarJson, "settings", "white_balance_k"),
            Locked = BoolAt(sidecarJson, "settings", "locked"),
        };
        var times = new JsonReader(sidecarJson).FindArray("per_frame", "common_time_us");
        if (times == null)
        {
            error = "사이드카에 per_frame.common_time_us가 없습니다";
            return null;
        }
        var commonTime = new List<Timestamp>(times.Count);
        foreach (var t in times)
        {
            var us = t.Kind == JsonKind.Number ? ParseInt64(t.Text) : null;
            if (us is not { } value)
            {
                error = "사이드카 per_frame.common_time_us에 정수가 아닌 값이 있습니다";
                return null;
            }
            commonTime.Add(new Timestamp(value));
        }
        var low = IntAt(sidecarJson, "display_range", "low");
        var high = IntAt(sidecarJson, "display_range", "high");
        Y16Range? range = low is >= 0 && high >= low && high <= 65535 ? new Y16Range((ushort)low.Value, (ushort)high.Value) : null;
        return new SidecarFrames
        {
            Width = (int)width,
            Height = (int)height,
            FrameRate = Int32At(sidecarJson, "format", "frame_rate"),
            Camera = camera,
            CommonTime = commonTime,
            DisplayRange = range,
        };
    }

    public static SidecarFrames? ReadSidecarFrames(string sidecarJson) => ReadSidecarFrames(sidecarJson, out _);

    /// <summary>
    /// 원시값 파일(`.raw16`)에서 번호로 프레임 하나를 읽습니다.
    /// </summary>
    /// <param name="path">원시값 파일 경로</param>
    /// <param name="width">프레임 폭(사이드카 format.width)</param>
    /// <param name="height">프레임 높이(사이드카 format.height)</param>
    /// <param name="index">프레임 번호(0부터, 파일 안 순서)</param>
    /// <param name="values">결과 Y16 값. width * height개로 맞춥니다(길이가 다르면 새 배열로 바꿉니다)</param>
    /// <returns>읽었으면 true. 파일을 열지 못하거나 번호가 파일 범위 밖이면 false</returns>
    /// <remarks>원시값 파일은 머리글 없이 리틀 엔디언 uint16을 이어 쓴 파일이라 번호 i의 위치는 i * width * height * 2입니다.</remarks>
    public static bool ReadRaw16Frame(string path, int width, int height, long index, ref ushort[] values)
    {
        if (width <= 0 || height <= 0 || index < 0) return false;
        var count = (long)width * height;
        var bytes = new byte[count * 2];
        try
        {
            using var file = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            file.Seek(index * count * 2, SeekOrigin.Begin);
            file.ReadExactly(bytes);
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException)
        {
            return false;
        }
        if (values.Length != count) values = new ushort[count];
        for (var i = 0; i < count; ++i) values[i] = (ushort)(bytes[2 * i] | (bytes[2 * i + 1] << 8));
        return true;
    }

    // 부호와 숫자만 있는 정수를 읽습니다. 공통 시각은 마이크로초 정수이므로 소수와 지수 표기는 받지 않습니다.
    private static long? ParseInt64(string text)
    {
        if (text.Length == 0) return null;
        var i = text[0] == '-' ? 1 : 0;
        if (i == text.Length) return null;
        long value = 0;
        for (; i < text.Length; ++i)
        {
            var c = text[i];
            if (c < '0' || c > '9' || value > (long.MaxValue - (c - '0')) / 10) return null;
            value = value * 10 + (c - '0');
        }
        return text[0] == '-' ? -value : value;
    }

    private static long? IntAt(string json, params string[] path)
    {
        var scalar = new JsonReader(json).FindScalar(path);
        return scalar is { Kind: JsonKind.Number } number ? ParseInt64(number.Text) : null;
    }

    private static int? Int32At(string json, params string[] path)
    {
        var value = IntAt(json, path);
        return value is >= int.MinValue and <= int.MaxValue ? (int)value : null;
    }

    // 앱이 문화권을 바꿔도 소수점을 '.'로 읽도록 고정 문화권으로 읽습니다.
    private static double? DoubleAt(string json, params string[] path)
    {
        var scalar = new JsonReader(json).FindScalar(path);
        if (scalar is not { Kind: JsonKind.Number } number) return null;
        return double.TryParse(number.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var value) && double.IsFinite(value) ? value : null;
    }

    private static bool? BoolAt(string json, params string[] path) => new JsonReader(json).FindScalar(path)?.Kind switch
    {
        JsonKind.True => true,
        JsonKind.False => false,
        _ => null,
    };
}
