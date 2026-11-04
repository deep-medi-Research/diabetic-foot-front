namespace DiabeticFoot.Core.Record;

/// <summary>영상 한 장의 크기(화소)입니다.</summary>
public readonly record struct VideoSize(int Width = 0, int Height = 0);

/// <summary>
/// 촬영 한 번이 디스크에 쓸 양을 정하는 값입니다.
/// </summary>
/// <remarks>프레임 크기는 platform과 화면이 촬영 선언과 좌우 영역에서 정해 넘깁니다.</remarks>
public sealed record ShotSizePlan
{
    /// <summary>사이드카가 한 장마다 쓰는 바이트의 상한입니다. per_frame 배열 6개(정수 하나에 최대 20자와 구분자)입니다.</summary>
    public const long SidecarBytesPerFrame = 256;

    /// <summary>녹화할 RGB 장 수(촬영 선언의 target_frames)입니다.</summary>
    public long RgbFrames { get; init; }

    /// <summary>RGB fps입니다. 열화상 장 수를 녹화 시간으로 셀 때 씁니다.</summary>
    public int RgbFrameRate { get; init; }

    /// <summary>저장하는 RGB 영상마다 하나(전체 화면, 왼발, 오른발)입니다.</summary>
    public IReadOnlyList<VideoSize> RgbVideos { get; init; } = Array.Empty<VideoSize>();

    /// <summary>열화상을 녹화하는지입니다.</summary>
    public bool Thermal { get; init; }

    /// <summary>열화상 프레임 크기입니다.</summary>
    public VideoSize ThermalSize { get; init; }

    /// <summary>열화상 기록 주기입니다. 0이면 받은 프레임을 모두 기록합니다.</summary>
    public long ThermalRecordPeriodUs { get; init; }

    /// <summary>열화상 장치가 내는 최대 Hz입니다. 기록 주기가 0일 때 장 수의 상한으로 씁니다.</summary>
    public int ThermalMaxRateHz { get; init; }
}

/// <summary>디스크 여유 판정입니다.</summary>
public enum FreeSpace : byte
{
    /// <summary>여유가 필요한 양 이상이고 경고 기준 이상입니다.</summary>
    Enough,

    /// <summary>여유가 필요한 양 이상이지만 경고 기준 미만입니다. 촬영은 시작할 수 있으나 촬영 중 모자랄 수 있습니다.</summary>
    Low,

    /// <summary>여유가 모자랍니다. 촬영을 시작하지 않습니다.</summary>
    Short,

    /// <summary>여유를 읽지 못했습니다.</summary>
    Unknown,
}

/// <summary>디스크 여유 판정 결과입니다.</summary>
/// <param name="Verdict">판정입니다.</param>
/// <param name="RequiredBytes">촬영 추정량과 예비량의 합입니다.</param>
public sealed record FreeSpaceJudgement(FreeSpace Verdict = FreeSpace.Unknown, long RequiredBytes = 0);

public static partial class RecordFunctions
{
    // 관류 분석 결과는 촬영 폴더 안 이 폴더의 판 폴더(perfusion/<판>/)에 분석이름_*로 쓰고, results 폴더로 모을 때는 이름 앞부분
    // 뒤에 _<판>을 붙입니다. 결과 파일 중 가장 긴 뒷부분은 18자입니다(_pi_timecourse.png, _frame_windows.csv).
    private const string AnalysisFolder = "perfusion";
    private const string LongestVariantId = "v10";
    private const string LongestResultSuffix = "_pi_timecourse.png";

    /// <summary>
    /// 촬영 한 번이 쓸 바이트의 상한을 셉니다.
    /// </summary>
    /// <param name="plan">촬영 크기</param>
    /// <returns>바이트 수. 값이 음수이거나 0인 항목은 0바이트로 셉니다</returns>
    /// <remarks>
    /// RGB 영상은 코덱과 관계없이 무압축 UYVY(화소당 2바이트)로 셉니다. 열화상은 원시값(화소당 2바이트), 확인용 AVI
    /// (UYVY, 화소당 2바이트), 좌우 .npy(합해서 화소당 2바이트 이하)를 셉니다. 영상마다 사이드카(<see cref="ShotSizePlan.SidecarBytesPerFrame"/>)를 더합니다.
    /// FFV1은 영상 내용에 따라 압축률이 달라 상한으로 무압축 크기를 씁니다. 관류 분석 결과는 reserveBytes(<see cref="JudgeFreeSpace"/>)가 맡습니다.
    /// </remarks>
    public static long EstimateShotBytes(ShotSizePlan plan)
    {
        long bytes = 0;
        long rgbFrames = Math.Max(plan.RgbFrames, 0L);
        foreach (VideoSize video in plan.RgbVideos)
        {
            bytes += rgbFrames * (FrameBytes(video) + ShotSizePlan.SidecarBytesPerFrame);
        }
        if (plan.Thermal)
        {
            // 원시값, 확인용 AVI, 좌우 .npy가 각각 화소당 2바이트 이하이고 사이드카는 원시값에만 있습니다.
            long frames = ThermalFrames(plan);
            bytes += frames * (FrameBytes(plan.ThermalSize) * 3 + ShotSizePlan.SidecarBytesPerFrame);
        }
        return bytes;
    }

    /// <summary>
    /// 디스크 여유로 촬영을 시작할 수 있는지 판정합니다.
    /// </summary>
    /// <param name="freeBytes">저장 위치가 있는 디스크의 남은 바이트. 읽지 못했으면 null</param>
    /// <param name="estimatedBytes"><see cref="EstimateShotBytes"/>의 결과</param>
    /// <param name="reserveBytes">촬영 뒤에도 남겨 둘 바이트(분석 결과, OS 임시 파일)</param>
    /// <param name="lowBytes">이 값 미만이면 미리 알리는 경고 기준. 0이면 경고하지 않습니다</param>
    /// <returns>
    /// freeBytes가 estimatedBytes + reserveBytes보다 작으면 Short, 필요한 양 이상이지만 lowBytes보다 작으면 Low,
    /// 그 밖에는 Enough, 읽지 못했으면 Unknown
    /// </returns>
    public static FreeSpaceJudgement JudgeFreeSpace(long? freeBytes, long estimatedBytes, long reserveBytes, long lowBytes)
    {
        long required = Math.Max(estimatedBytes, 0L) + Math.Max(reserveBytes, 0L);
        if (freeBytes is not long free)
        {
            return new FreeSpaceJudgement(FreeSpace.Unknown, required);
        }
        FreeSpace verdict = free < required ? FreeSpace.Short : (free < lowBytes ? FreeSpace.Low : FreeSpace.Enough);
        return new FreeSpaceJudgement(verdict, required);
    }

    /// <summary>
    /// 촬영 폴더 안에서 가장 긴 파일 경로의 길이를 셉니다.
    /// </summary>
    /// <param name="root">저장 위치. 끝에 '/'나 '\\'가 있으면 구분자를 더하지 않습니다</param>
    /// <param name="layout">촬영 폴더 배치(<see cref="LayoutOf"/> 또는 <see cref="NumberLayout"/>의 결과)</param>
    /// <param name="analysisName">분석 결과 파일 이름의 앞부분. 비면 분석 결과와 분석결과 폴더 경로는 세지 않습니다</param>
    /// <returns>가장 긴 경로의 길이. 끝의 NUL은 세지 않습니다</returns>
    /// <remarks>
    /// 배치의 모든 파일과 분석 결과 중 가장 긴 파일(perfusion/판/분석이름_pi_timecourse.png)의 경로를 저장 위치/폴더/파일 이름으로
    /// 이어 셉니다. 분석 이름이 있으면 분석결과 폴더로 모은 결과
    /// (저장 위치/results/CollectedResultPrefix(layout, MaxReanalysisNumber)_판_pi_timecourse.png)도 셉니다.
    /// 길이는 UTF-16 코드 단위입니다(Windows 경로 상한의 단위). 한글 한 글자는 1, U+10000 이상 문자는 2입니다.
    /// 상한(Windows는 259)은 platform이 정합니다. 폴더 경로는 파일 경로보다 짧으므로 따로 세지 않습니다.
    /// </remarks>
    public static int LongestShotPathLength(string root, ShotLayout layout, string analysisName)
    {
        // 파일 이름 앞까지의 길이입니다(저장 위치, 구분자, 폴더, 구분자).
        int folder = root.Length + (root.Length == 0 || EndsWithSeparator(root) ? 0 : 1) + layout.Folder.Length + 1;
        int longest = 0;
        foreach (ShotFile file in layout.Files)
        {
            longest = Math.Max(longest, folder + file.Name.Length);
        }
        if (analysisName.Length != 0)
        {
            int result = AnalysisFolder.Length + 1 + LongestVariantId.Length + 1 + analysisName.Length + LongestResultSuffix.Length;
            longest = Math.Max(longest, folder + result);
            // 분석결과 폴더로 모은 결과는 가장 큰 재분석 번호까지 셉니다.
            int collected = root.Length + (root.Length == 0 || EndsWithSeparator(root) ? 0 : 1) +
                            ShotLayout.CollectedResultFolder.Length + 1 +
                            CollectedResultPrefix(layout, ShotLayout.MaxReanalysisNumber).Length + 1 +
                            LongestVariantId.Length + LongestResultSuffix.Length;
            longest = Math.Max(longest, collected);
        }
        return longest;
    }

    private static long FrameBytes(VideoSize size)
    {
        if (size.Width <= 0 || size.Height <= 0)
        {
            return 0;
        }
        return (long)size.Width * size.Height * 2;
    }

    // 녹화 시간(RGB 장 수 / fps) 동안 기록할 열화상 장 수의 상한입니다. 첫 장을 위해 1을 더합니다.
    private static long ThermalFrames(ShotSizePlan plan)
    {
        if (plan.RgbFrames <= 0 || plan.RgbFrameRate <= 0)
        {
            return 0;
        }
        long durationUs = plan.RgbFrames * 1_000_000 / plan.RgbFrameRate;
        if (plan.ThermalRecordPeriodUs > 0)
        {
            return durationUs / plan.ThermalRecordPeriodUs + 1;
        }
        if (plan.ThermalMaxRateHz <= 0)
        {
            return 0;
        }
        return durationUs * plan.ThermalMaxRateHz / 1_000_000 + 1;
    }

    private static bool EndsWithSeparator(string path) => path.Length != 0 && (path[^1] == '/' || path[^1] == '\\');
}
