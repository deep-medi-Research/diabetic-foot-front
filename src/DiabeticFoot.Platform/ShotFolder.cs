using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Text;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Flow;
using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform;

/// <summary>촬영에서 저장하는 RGB 영상입니다.</summary>
public enum SavedVideos : byte
{
    /// <summary>자르지 않은 전체 화면 영상 하나</summary>
    Full,

    /// <summary>좌우 분할한 왼발 영상과 오른발 영상</summary>
    Feet,

    /// <summary>전체 화면 영상과 좌우 분할 영상</summary>
    FullAndFeet,
}

/// <summary>
/// 녹화를 시작할 수 있게 준비한 촬영 폴더입니다. 경로는 모두 전체 경로입니다.
/// </summary>
/// <remarks>
/// 이번 촬영에 없는 역할의 경로는 빈 문자열입니다(열화상 장치가 없으면 thermal_로 시작하는 다섯 경로,
/// <see cref="SavedVideos.Full"/>이면 left_와 right_로 시작하는 네 경로).
/// </remarks>
public sealed record PreparedShot
{
    /// <summary>실제로 만든 폴더의 배치. 번호를 넣은 배치일 수 있습니다.</summary>
    public ShotLayout Layout { get; init; } = new();

    /// <summary>매니페스트에 쓴 화소 왼쪽 발입니다.</summary>
    public FootSide PixelLeftFoot { get; init; } = FootSide.Right;

    /// <summary>촬영 폴더입니다.</summary>
    public string Folder { get; init; } = "";

    /// <summary>매니페스트입니다.</summary>
    public string Manifest { get; init; } = "";

    /// <summary>전체 화면 영상입니다.</summary>
    public string Video { get; init; } = "";

    /// <summary>전체 화면 영상의 사이드카입니다.</summary>
    public string Sidecar { get; init; } = "";

    /// <summary>왼발 영상(환자 기준)입니다.</summary>
    public string LeftVideo { get; init; } = "";

    /// <summary>왼발 영상의 사이드카입니다.</summary>
    public string LeftSidecar { get; init; } = "";

    /// <summary>오른발 영상(환자 기준)입니다.</summary>
    public string RightVideo { get; init; } = "";

    /// <summary>오른발 영상의 사이드카입니다.</summary>
    public string RightSidecar { get; init; } = "";

    /// <summary>열화상 원시값입니다.</summary>
    public string ThermalRaw { get; init; } = "";

    /// <summary>열화상 사이드카입니다.</summary>
    public string ThermalSidecar { get; init; } = "";

    /// <summary>열화상을 볼 수 있게 옮긴 영상입니다.</summary>
    public string ThermalPreview { get; init; } = "";

    /// <summary>왼발 열화상 .npy(thermal_left.npy)입니다.</summary>
    public string ThermalLeftArray { get; init; } = "";

    /// <summary>오른발 열화상 .npy(thermal_right.npy)입니다.</summary>
    public string ThermalRightArray { get; init; } = "";
}

/// <summary>디스크 여유 판정과 화면에 보일 문장입니다. 문장은 낮음(Low)과 부족(Short)일 때만 있고, 나머지는 빈 문자열입니다.</summary>
public sealed record FreeSpaceNotice(FreeSpace Verdict = FreeSpace.Unknown, string Text = "");

/// <summary>촬영 폴더를 만들고, 매니페스트를 쓰고, 녹화 뒤 파일을 정리하고, 저장 위치의 한도를 검사합니다.</summary>
public static class ShotFolder
{
    /// <summary>Windows 경로 길이 상한입니다. MAX_PATH(260)에서 끝의 NUL을 뺀 UTF-16 코드 단위 수입니다.</summary>
    public const int WindowsMaxPathLength = 259;

    /// <summary>
    /// 촬영 뒤에도 디스크에 남겨 둘 바이트입니다(<see cref="RecordFunctions.JudgeFreeSpace"/>의 reserveBytes).
    /// 관류 분석 결과(요약, 보고서, 그림, GIF)와 OS 임시 파일 몫으로 정한 값이며 실측 근거는 없습니다.
    /// </summary>
    public const long FreeSpaceReserveBytes = 1_000_000_000;

    /// <summary>
    /// 저장 위치의 남은 공간이 이 값 미만이면 촬영 전에 미리 알립니다(<see cref="FreeSpace.Low"/>).
    /// 한 번 촬영이 약 1.08 GB(1280x720 60fps, 열화상, PPG 포함)를 쓰므로 여러 번 촬영할 공간을 기준으로 한 값입니다.
    /// </summary>
    public const long FreeSpaceLowBytes = 10_000_000_000;

    /// <summary>
    /// 촬영 폴더 안 파일 하나의 전체 경로를 만듭니다.
    /// </summary>
    /// <param name="root">저장 위치</param>
    /// <param name="layout">촬영 폴더 배치</param>
    /// <param name="role">파일 역할</param>
    /// <returns>저장 위치/폴더/파일 이름. 구분자는 이 OS의 것입니다. 배치에 그 역할이 없으면 빈 문자열</returns>
    public static string PathOfFile(string root, ShotLayout layout, FileRole role)
    {
        var name = layout.NameOf(role);
        return name.Length == 0 ? "" : Preferred(Path.Combine(root, layout.Folder, name));
    }

    /// <summary>
    /// 촬영 폴더를 만듭니다. 위 단계(이름_생년월일, 기준)는 없으면 만들고, 마지막 단계(날짜_시각)는 새로 만들어야 성공합니다.
    /// </summary>
    /// <param name="root">저장 위치. 없으면 만듭니다</param>
    /// <param name="layout">촬영 폴더 배치</param>
    /// <param name="error">실패하면 이유를 씁니다</param>
    /// <returns>새 폴더를 만들었으면 true. 같은 폴더가 이미 있으면 false(앞 촬영을 덮어쓰지 않기 위함)</returns>
    /// <remarks>
    /// 만든 뒤 맨 위 폴더(이름_생년월일)의 수정 시각을 지금으로 맞춥니다. 최근 환자 목록이 저장 위치 목록 한 번으로
    /// 환자마다 마지막 촬영 시각을 읽게 하기 위함입니다. 맞추지 못해도 true를 돌려줍니다.
    /// </remarks>
    public static bool CreateShotFolder(string root, ShotLayout layout, out string error)
    {
        error = "";
        var folder = Preferred(Path.Combine(root, layout.Folder));
        var parent = Path.GetDirectoryName(folder) ?? "";
        try
        {
            if (parent.Length > 0) Directory.CreateDirectory(parent);
        }
        catch (Exception e) when (IsFileError(e))
        {
            error = $"상위 폴더를 만들지 못함: {parent} ({e.Message})";
            return false;
        }
        // .NET에는 이미 있으면 실패하는 폴더 만들기가 없어 있는지 보고 만듭니다. 같은 초에 같은 환자의 촬영을 두 프로그램이
        // 동시에 시작하는 경우만 막지 못하고, 매니페스트는 새 파일로만 써서(WriteManifest) 앞 촬영의 파일은 덮어쓰지 않습니다.
        if (Directory.Exists(folder))
        {
            error = "촬영 폴더가 이미 있음: " + folder;
            return false;
        }
        try
        {
            Directory.CreateDirectory(folder);
        }
        catch (Exception e) when (IsFileError(e))
        {
            error = $"촬영 폴더를 만들지 못함: {folder} ({e.Message})";
            return false;
        }
        // 촬영 폴더는 맨 위 폴더의 두 단계 아래라 OS가 맨 위 폴더의 수정 시각을 바꾸지 않습니다. 맞추지 못해도 촬영은 이어 갑니다.
        var slash = layout.Folder.IndexOf('/');
        var top = slash < 0 ? layout.Folder : layout.Folder[..slash];
        try
        {
            Directory.SetLastWriteTimeUtc(Preferred(Path.Combine(root, top)), DateTime.UtcNow);
        }
        catch (Exception e) when (IsFileError(e))
        {
        }
        return true;
    }

    /// <summary>
    /// 비어 있는 번호를 찾아 촬영 폴더를 만듭니다. 먼저 번호 없이, 있으면 2, 3, ... 순서로 시도합니다.
    /// </summary>
    /// <param name="root">저장 위치</param>
    /// <param name="layout"><see cref="RecordFunctions.LayoutOf"/>의 결과</param>
    /// <param name="maxNumber">시도할 가장 큰 번호</param>
    /// <param name="error">실패하면 이유를 씁니다</param>
    /// <returns>실제로 만든 폴더의 배치(<see cref="RecordFunctions.NumberLayout"/> 결과일 수 있음). 모두 있거나 만들 수 없으면 null</returns>
    public static ShotLayout? CreateFreshShotFolder(string root, ShotLayout layout, int maxNumber, out string error)
    {
        error = "";
        for (var number = 1; number <= maxNumber; ++number)
        {
            var candidate = RecordFunctions.NumberLayout(layout, number);
            var folder = Path.Combine(root, candidate.Folder);
            if (Exists(folder)) continue;
            if (CreateShotFolder(root, candidate, out error)) return candidate;
            // 확인과 생성 사이에 다른 촬영이 같은 폴더를 만든 경우만 다음 번호로 넘어갑니다.
            if (!Exists(folder)) return null;
        }
        error = $"번호 {maxNumber}까지 촬영 폴더가 모두 있음: {layout.Folder}";
        return null;
    }

    /// <summary>
    /// 촬영 폴더에 매니페스트를 씁니다. 바이트를 바꾸지 않고 그대로 씁니다(BOM 없는 UTF-8).
    /// </summary>
    /// <param name="root">저장 위치</param>
    /// <param name="layout">촬영 폴더 배치. 폴더가 이미 있어야 합니다</param>
    /// <param name="json"><see cref="RecordFunctions.BuildManifestJson"/>의 결과</param>
    /// <param name="error">실패하면 이유를 씁니다</param>
    /// <returns>썼으면 true. 같은 이름의 파일이 이미 있으면 덮어쓰지 않고 false</returns>
    public static bool WriteManifest(string root, ShotLayout layout, string json, out string error)
    {
        error = "";
        var path = PathOfFile(root, layout, FileRole.Manifest);
        if (path.Length == 0 || Exists(path))
        {
            error = "매니페스트를 덮어쓰지 않음: " + path;
            return false;
        }
        FileStream file;
        try
        {
            file = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
        }
        catch (Exception e) when (IsFileError(e))
        {
            error = (e is IOException && Exists(path) ? "매니페스트를 덮어쓰지 않음: " : "매니페스트를 열지 못함: ") + path;
            return false;
        }
        try
        {
            using (file) file.Write(new UTF8Encoding(false).GetBytes(json));
            return true;
        }
        catch (Exception e) when (IsFileError(e))
        {
            error = "매니페스트를 쓰지 못함: " + path;
            return false;
        }
    }

    /// <summary>
    /// <see cref="PrepareShot"/>이 만들 촬영 폴더 배치를 폴더를 만들지 않고 구합니다. 번호는 넣지 않습니다.
    /// </summary>
    /// <returns><see cref="PrepareShot"/>과 같은 역할로 만든 <see cref="RecordFunctions.LayoutOf"/>의 결과. 촬영 정보가 검증을 통과하지 않으면 null</returns>
    public static ShotLayout? PlanShotLayout(ShotInfo info, VideoCodec codec, SavedVideos videos, IReadOnlyList<ShotDevice> devices)
    {
        var roles = new List<FileRole>();
        if (videos != SavedVideos.Feet) roles.AddRange([FileRole.FullVideo, FileRole.FullSidecar]);
        if (videos != SavedVideos.Full)
        {
            roles.AddRange([FileRole.LeftVideo, FileRole.LeftSidecar, FileRole.RightVideo, FileRole.RightSidecar]);
        }
        if (devices.Any(d => d.Stream == StreamKind.Thermal))
        {
            roles.AddRange([FileRole.ThermalRaw, FileRole.ThermalSidecar, FileRole.ThermalPreview,
                            FileRole.ThermalLeftArray, FileRole.ThermalRightArray]);
        }
        return RecordFunctions.LayoutOf(info, FlowFunctions.ExtensionOf(codec), roles);
    }

    /// <summary>
    /// 촬영 폴더를 만들고 매니페스트를 써서 녹화를 시작할 수 있게 준비합니다.
    /// </summary>
    /// <param name="root">저장 위치. 없으면 만듭니다</param>
    /// <param name="info">촬영 정보. 시각은 호출하는 쪽이 한 번만 읽어 넘깁니다</param>
    /// <param name="codec">촬영 선언의 코덱. 영상 확장자를 정합니다</param>
    /// <param name="videos">저장하는 RGB 영상</param>
    /// <param name="devices">이번 촬영에 쓰는 장치. 매니페스트의 장치 목록이 됩니다</param>
    /// <param name="maxNumber">같은 폴더가 이미 있을 때 폴더 이름 뒤에 넣는 번호의 최대값</param>
    /// <param name="error">실패하면 이유를 씁니다</param>
    /// <returns>준비한 촬영. 촬영 정보가 검증을 통과하지 않거나, 폴더를 만들지 못하거나, 매니페스트를 쓰지 못하면 null</returns>
    /// <remarks>
    /// 영상 역할은 videos가 정합니다(전체 화면, 좌우 분할, 둘 다). 영상마다 사이드카가 하나씩 붙습니다.
    /// devices에 열화상 장치가 있으면 열화상 원시값, 열화상 사이드카, 열화상 확인용 영상, 좌우 열화상 .npy를 더합니다.
    /// 파일 이름은 <see cref="RecordFunctions.LayoutOf"/>가 정합니다.
    /// 같은 폴더가 이미 있으면 번호를 넣은 폴더(날짜_시각_2, 날짜_시각_3, ...)를 만들어 앞 촬영을 덮어쓰지 않습니다.
    /// 매니페스트를 쓰지 못하면 이번에 만든 촬영 폴더를 지웁니다. 상위 폴더(이름_생년월일, 발목 혈압 단계)는 남깁니다.
    /// </remarks>
    public static PreparedShot? PrepareShot(string root, ShotInfo info, VideoCodec codec, SavedVideos videos,
                                            IReadOnlyList<ShotDevice> devices, int maxNumber, out string error)
    {
        var layout = PlanShotLayout(info, codec, videos, devices);
        if (layout == null)
        {
            error = "촬영 정보가 검증을 통과하지 않음";
            return null;
        }
        var fresh = CreateFreshShotFolder(root, layout, maxNumber, out error);
        if (fresh == null) return null;

        var shot = new PreparedShot
        {
            Layout = fresh,
            PixelLeftFoot = info.PixelLeftFoot,
            Folder = Preferred(Path.Combine(root, fresh.Folder)),
            Manifest = PathOfFile(root, fresh, FileRole.Manifest),
            Video = PathOfFile(root, fresh, FileRole.FullVideo),
            Sidecar = PathOfFile(root, fresh, FileRole.FullSidecar),
            ThermalRaw = PathOfFile(root, fresh, FileRole.ThermalRaw),
            ThermalSidecar = PathOfFile(root, fresh, FileRole.ThermalSidecar),
            ThermalPreview = PathOfFile(root, fresh, FileRole.ThermalPreview),
            LeftVideo = PathOfFile(root, fresh, FileRole.LeftVideo),
            LeftSidecar = PathOfFile(root, fresh, FileRole.LeftSidecar),
            RightVideo = PathOfFile(root, fresh, FileRole.RightVideo),
            RightSidecar = PathOfFile(root, fresh, FileRole.RightSidecar),
            ThermalLeftArray = PathOfFile(root, fresh, FileRole.ThermalLeftArray),
            ThermalRightArray = PathOfFile(root, fresh, FileRole.ThermalRightArray),
        };
        if (!WriteManifest(root, fresh, RecordFunctions.BuildManifestJson(info, fresh, devices), out error))
        {
            // 방금 만든 폴더라 매니페스트는 이번 호출이 쓰다 만 파일뿐입니다. 매니페스트 없는 촬영 폴더를 남기지 않습니다.
            Remove(shot.Manifest);
            Remove(shot.Folder);
            return null;
        }
        return shot;
    }

    /// <summary>
    /// 영상 파일 없이 끝난 촬영의 매니페스트와 빈 촬영 폴더를 지웁니다.
    /// </summary>
    /// <param name="shot"><see cref="PrepareShot"/>의 결과</param>
    /// <returns>
    /// 촬영 폴더를 지웠으면 true. 영상 파일(전체 화면, 왼발, 오른발 중 하나라도)이 있거나, 매니페스트 말고
    /// 다른 파일이 있거나, 지우지 못하면 false
    /// </returns>
    /// <remarks>
    /// 녹화 전에 중지하면 매니페스트만 있는 폴더가 남고, 이 폴더는 촬영한 것처럼 보이므로 지웁니다.
    /// 매니페스트 말고 다른 파일(예: 열화상 원시값)이 있으면 매니페스트도 지우지 않습니다. 매니페스트가 그 파일의 환자 정보를 갖기 때문입니다.
    /// </remarks>
    public static bool DiscardUnrecordedShot(PreparedShot shot)
    {
        if (shot.Folder.Length == 0) return false;
        foreach (var video in new[] { shot.Video, shot.LeftVideo, shot.RightVideo })
        {
            if (video.Length > 0 && Exists(video)) return false;
        }
        var manifestName = Path.GetFileName(shot.Manifest);
        try
        {
            foreach (var entry in Directory.EnumerateFileSystemEntries(shot.Folder))
            {
                if (Path.GetFileName(entry) != manifestName) return false;
            }
            File.Delete(shot.Manifest);
            // Directory.Delete는 빈 폴더만 지우므로, 확인과 삭제 사이에 생긴 파일이 있으면 폴더가 남습니다.
            Directory.Delete(shot.Folder, recursive: false);
            return true;
        }
        catch (Exception e) when (IsFileError(e))
        {
            return false;
        }
    }

    /// <summary>
    /// 녹화가 끝난 열화상 원시값을 좌우 발로 잘라 thermal_left.npy와 thermal_right.npy로 씁니다.
    /// </summary>
    /// <param name="shot">열화상 장치로 준비한 <see cref="PrepareShot"/>의 결과</param>
    /// <param name="pixelRegions">직접 정한 열화상 영역(화소 왼쪽, 화소 오른쪽). null이면 가운데 세로선에서 나눕니다</param>
    /// <param name="error">실패하면 이유를 씁니다</param>
    /// <returns>두 파일을 모두 썼으면 true</returns>
    /// <remarks>
    /// 열화상 사이드카에서 프레임 크기와 기록한 장 수를 읽고, <see cref="RecordFunctions.ThermalFootRegions"/>로 왼발과 오른발 영역을 정해
    /// <see cref="ThermalWriter.WriteNpy"/>로 씁니다. 화소 왼쪽 발은 <see cref="PreparedShot.PixelLeftFoot"/>입니다.
    /// 영역이 세로로 길지 않으면(높이 &lt;= 폭) 쓰지 않고 실패합니다. 관류 분석은 한 발씩 세로로 긴 영상만 받습니다.
    /// 실패하면 이 호출이 쓴 .npy를 지우고, 원시값 파일은 어떤 경우에도 지우지 않습니다(.npy를 다시 만드는 원본).
    /// </remarks>
    public static bool WriteThermalArrays(PreparedShot shot, (Rect PixelLeft, Rect PixelRight)? pixelRegions, out string error)
    {
        error = "";
        if (shot.ThermalRaw.Length == 0 || shot.ThermalLeftArray.Length == 0 || shot.ThermalRightArray.Length == 0)
        {
            error = "열화상 장치 없이 준비한 촬영";
            return false;
        }
        var thermal = ReadSummaryFile(shot.ThermalSidecar, out error);
        if (thermal is not { } sidecar) return false;
        if (sidecar.Stream != StreamKind.Thermal)
        {
            error = "열화상 사이드카가 아님: " + shot.ThermalSidecar;
            return false;
        }
        var (left, right) = RecordFunctions.ThermalFootRegions(sidecar.FrameWidth, sidecar.FrameHeight, pixelRegions, shot.PixelLeftFoot);
        foreach (var rect in new[] { left, right })
        {
            if (rect.Height <= rect.Width)
            {
                error = $"열화상 발 영역이 세로로 길지 않음: {rect.Width}x{rect.Height}";
                return false;
            }
        }
        foreach (var (rect, path) in new[] { (left, shot.ThermalLeftArray), (right, shot.ThermalRightArray) })
        {
            var frames = ThermalWriter.WriteNpy(shot.ThermalRaw, sidecar.FrameWidth, sidecar.FrameHeight, rect, path, out error);
            // 사이드카의 장 수와 다르면 원시값 파일이 사이드카와 다른 녹화의 것이거나 잘린 파일입니다.
            if (frames is { } counted && counted != sidecar.WrittenFrames)
            {
                error = $"원시값 장 수 {counted}가 사이드카의 {sidecar.WrittenFrames}와 다름";
            }
            if (frames != sidecar.WrittenFrames)
            {
                Remove(shot.ThermalLeftArray);
                Remove(shot.ThermalRightArray);
                return false;
            }
        }
        return true;
    }

    /// <summary>
    /// 분석 전 확인(<see cref="RecordFunctions.CheckAnalysisInputs"/>)에 넘길 값을 촬영 폴더에서 읽습니다.
    /// </summary>
    /// <param name="shot"><see cref="PrepareShot"/>의 결과</param>
    /// <returns>있는 파일의 역할과 읽은 사이드카. 사이드카가 없거나 읽지 못하면 그 값은 비웁니다</returns>
    public static AnalysisFiles ReadAnalysisFiles(PreparedShot shot)
    {
        var present = new List<FileRole>();
        foreach (var file in shot.Layout.Files)
        {
            if (File.Exists(Path.Combine(shot.Folder, file.Name))) present.Add(file.Role);
        }
        SidecarSummary? ReadIfPresent(string path) =>
            path.Length == 0 || !Exists(path) ? null : ReadSummaryFile(path, out _);
        return new AnalysisFiles
        {
            Present = present,
            Left = ReadIfPresent(shot.LeftSidecar),
            Right = ReadIfPresent(shot.RightSidecar),
            Thermal = ReadIfPresent(shot.ThermalSidecar),
        };
    }

    /// <summary>
    /// 이 OS에서 파일 경로 길이의 상한입니다.
    /// </summary>
    /// <returns>Windows는 <see cref="WindowsMaxPathLength"/>. 그 밖의 OS는 null(검사하지 않음)</returns>
    /// <remarks>앱이 긴 경로(\\?\ 접두나 longPathAware 매니페스트)를 쓰지 않으므로 Windows API가 이 상한에서 실패합니다.</remarks>
    public static int? MaxPathLength() => OperatingSystem.IsWindows() ? WindowsMaxPathLength : null;

    /// <summary>
    /// 저장 위치가 있는 디스크의 남은 바이트를 읽습니다.
    /// </summary>
    /// <param name="root">저장 위치. 아직 없으면 가장 가까운 상위 폴더로 읽습니다</param>
    /// <returns>남은 바이트. 읽지 못하면 null</returns>
    public static long? FreeBytesAt(string root)
    {
        string path;
        try
        {
            path = Path.GetFullPath(root);
        }
        catch (Exception e) when (IsFileError(e))
        {
            return null;
        }
        // 저장 위치는 첫 촬영에서 만들어지므로, 아직 없으면 있는 상위 폴더의 디스크를 읽습니다.
        while (!Directory.Exists(path))
        {
            var parent = Path.GetDirectoryName(path);
            if (string.IsNullOrEmpty(parent) || parent == path) return null;
            path = parent;
        }
        try
        {
            return OperatingSystem.IsWindows() ? WindowsDisk.AvailableBytes(path) : new DriveInfo(path).AvailableFreeSpace;
        }
        catch (Exception e) when (IsFileError(e))
        {
            return null;
        }
    }

    /// <summary>
    /// 디스크 여유를 판정하고 낮음(<see cref="FreeSpace.Low"/>)과 부족(<see cref="FreeSpace.Short"/>)이면 그 문장을 만듭니다.
    /// </summary>
    /// <param name="freeBytes"><see cref="FreeBytesAt"/>의 결과</param>
    /// <param name="estimatedBytes"><see cref="RecordFunctions.EstimateShotBytes"/>의 결과</param>
    /// <returns>판정과 문장. 예비량은 <see cref="FreeSpaceReserveBytes"/>, 경고 기준은 <see cref="FreeSpaceLowBytes"/></returns>
    public static FreeSpaceNotice DescribeFreeSpace(long? freeBytes, long estimatedBytes)
    {
        var space = RecordFunctions.JudgeFreeSpace(freeBytes, estimatedBytes, FreeSpaceReserveBytes, FreeSpaceLowBytes);
        var text = "";
        if (space.Verdict == FreeSpace.Short)
        {
            text = $"디스크 여유 부족: 남은 {FormatBytes(freeBytes!.Value)}, 필요 {FormatBytes(space.RequiredBytes)}" +
                   $"(촬영 추정 {FormatBytes(estimatedBytes)}, 예비 {FormatBytes(FreeSpaceReserveBytes)}). 다른 저장 위치를 고르거나 공간을 비워 주세요.";
        }
        else if (space.Verdict == FreeSpace.Low)
        {
            text = $"저장 공간이 {FormatBytes(freeBytes!.Value)} 남았습니다. {FormatBytes(FreeSpaceLowBytes)} 미만이면 촬영 중 공간이 부족할 수 있습니다. " +
                   "다른 위치로 옮기거나 공간을 비워 주세요.";
        }
        return new FreeSpaceNotice(space.Verdict, text);
    }

    /// <summary>
    /// 촬영을 끝까지 저장하지 못했을 때 화면에 보일 문장을 만듭니다.
    /// </summary>
    /// <param name="freeBytes">실패를 안 시점에 다시 읽은 남은 바이트. 예비량(<see cref="FreeSpaceReserveBytes"/>) 미만이면 공간 부족을 원인으로 적습니다</param>
    /// <param name="folder">저장된 부분이 남아 있는 촬영 폴더</param>
    /// <param name="detail">공간 부족이 아닐 때 덧붙일 파일 이름이나 오류 문장. 비어도 됩니다</param>
    public static string DescribeSaveFailure(long? freeBytes, string folder, string detail)
    {
        var text = "저장 실패: ";
        if (freeBytes is { } free && free < FreeSpaceReserveBytes)
        {
            text += $"저장 공간이 부족해 이 촬영을 끝까지 저장하지 못했습니다(남은 {FormatBytes(free)}).";
        }
        else
        {
            text += "파일을 끝까지 쓰지 못해 이 촬영을 끝까지 저장하지 못했습니다";
            text += detail.Length == 0 ? "." : $"({detail}).";
        }
        return text + $" 저장된 부분은 {folder}에 그대로 있습니다.";
    }

    /// <summary>
    /// 촬영 폴더를 만들기 전에 경로 길이와 디스크 여유를 검사합니다.
    /// </summary>
    /// <param name="root">저장 위치</param>
    /// <param name="layout">검사할 배치. 같은 초 재촬영 번호까지 넣은 배치(<see cref="RecordFunctions.NumberLayout"/>)를 주면 가장 긴 경우를 검사합니다</param>
    /// <param name="estimatedBytes"><see cref="RecordFunctions.EstimateShotBytes"/>의 결과</param>
    /// <param name="freeBytes"><see cref="FreeBytesAt"/>의 결과. null이면 디스크 여유는 검사하지 않습니다</param>
    /// <param name="maxPathLength"><see cref="MaxPathLength"/>의 결과. null이면 경로 길이는 검사하지 않습니다</param>
    /// <returns>촬영을 시작하면 안 되는 이유(한국어). 통과하면 빈 문자열</returns>
    /// <remarks>
    /// 경로 길이는 분석 결과(perfusion/판/폴더 이름_pi_timecourse.png)와 results 폴더로 모은 결과까지 셉니다
    /// (<see cref="RecordFunctions.LongestShotPathLength"/>).
    /// </remarks>
    public static string CheckShotLimits(string root, ShotLayout layout, long estimatedBytes, long? freeBytes, int? maxPathLength)
    {
        if (maxPathLength is { } limit)
        {
            var longest = RecordFunctions.LongestShotPathLength(root, layout, layout.Stem);
            if (longest > limit)
            {
                return $"저장 경로가 너무 김: 가장 긴 파일 경로 {longest}자(상한 {limit}자). 저장 위치를 짧은 폴더로 바꾸십시오";
            }
        }
        var space = DescribeFreeSpace(freeBytes, estimatedBytes);
        return space.Verdict == FreeSpace.Short ? space.Text : "";
    }

    // 사이드카를 바이트 그대로 읽습니다. File.ReadAllText는 BOM을 지우고 잘못된 바이트를 바꿔 원본이 거부하는 파일을 통과시킵니다.
    private static SidecarSummary? ReadSummaryFile(string path, out string error)
    {
        byte[] json;
        try
        {
            if (path.Length == 0) throw new FileNotFoundException();
            json = File.ReadAllBytes(path);
        }
        catch (Exception e) when (IsFileError(e))
        {
            error = "사이드카를 열지 못함: " + path;
            return null;
        }
        return RecordFunctions.ReadSidecarSummary(json, out error);
    }

    private static string FormatBytes(long bytes)
    {
        if (bytes >= 1_000_000_000)
        {
            // 10 GB처럼 소수 자리가 0이면 뺍니다.
            var gb = bytes / 1e9;
            return CFormat.Fixed(gb, gb == Math.Floor(gb) ? 0 : 1) + " GB";
        }
        return CFormat.Fixed(Math.Max(bytes, 0L) / 1e6, 0) + " MB";
    }

    // 이 OS에서 쓰는 구분자로 바꿉니다. Windows는 '/'를 '\'로, 그 밖의 OS는 그대로입니다.
    private static string Preferred(string path) => path.Replace(Path.AltDirectorySeparatorChar, Path.DirectorySeparatorChar);

    private static bool Exists(string path) => File.Exists(path) || Directory.Exists(path);

    private static void Remove(string path)
    {
        try
        {
            if (File.Exists(path)) File.Delete(path);
            else if (Directory.Exists(path)) Directory.Delete(path, recursive: false);
        }
        catch (Exception e) when (IsFileError(e))
        {
        }
    }

    private static bool IsFileError(Exception e) =>
        e is IOException or UnauthorizedAccessException or ArgumentException or NotSupportedException;

    [SupportedOSPlatform("windows")]
    private static class WindowsDisk
    {
        // 마운트한 드라이브와 네트워크 경로(\\서버\공유)의 남은 바이트를 같은 호출로 읽습니다(원본 std::filesystem::space와 같은 API).
        public static long? AvailableBytes(string directory)
        {
            var path = directory.EndsWith('\\') ? directory : directory + "\\";
            return GetDiskFreeSpaceExW(path, out var available, out _, out _) ? (long)available : null;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetDiskFreeSpaceExW(string directory, out ulong freeBytesAvailable, out ulong totalBytes, out ulong totalFreeBytes);
    }
}
