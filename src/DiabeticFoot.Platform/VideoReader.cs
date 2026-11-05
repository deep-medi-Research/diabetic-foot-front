namespace DiabeticFoot.Platform;

/// <summary>
/// 녹화한 영상 파일에서 번호로 프레임을 읽습니다. 구현은 FFV1(MKV)입니다. 한 스레드에서만 씁니다.
/// FFV1은 GOP 1로 써서 모든 장이 독립 프레임이므로, 번호 하나를 읽을 때 그 장만 디코딩합니다.
/// </summary>
public interface IVideoReader : IDisposable
{
    /// <summary>
    /// 파일을 엽니다. 영상 스트림이 없거나 화소 형식이 4:2:2 8비트가 아니면 false이고 error에 이유를 씁니다.
    /// </summary>
    bool Open(string path, out string error);

    /// <summary>프레임 폭입니다. 열기 전에는 0입니다.</summary>
    int Width { get; }

    /// <summary>프레임 높이입니다. 열기 전에는 0입니다.</summary>
    int Height { get; }

    /// <summary>
    /// 파일의 프레임 수입니다. 컨테이너 길이와 fps로 셉니다. 녹화 앱이 마지막 장까지 길이를 넣어 쓰므로 쓴 장 수와 같습니다.
    /// 결과 창은 사이드카의 장 수를 함께 봅니다.
    /// </summary>
    long FrameCount { get; }

    /// <summary>
    /// 번호 index의 프레임을 UYVY로 읽습니다. 행 사이 빈 바이트 없이 Width * Height * 2 바이트가 되도록 uyvy의 길이를 맞춥니다.
    /// 번호가 범위 밖이거나 디코딩에 실패하면 false입니다. 바로 앞에 읽은 번호의 다음 번호는 탐색하지 않고 이어서 읽습니다.
    /// </summary>
    /// <param name="index">프레임 번호(0부터, 파일 안 순서)</param>
    /// <param name="uyvy">결과. 길이가 다르면 새 배열로 바꿉니다</param>
    bool ReadFrame(long index, ref byte[] uyvy);
}

/// <summary>영상 읽기 객체를 만듭니다.</summary>
public static class VideoReaders
{
    /// <summary>
    /// 영상 읽기 객체를 만듭니다. FFmpeg 공유 라이브러리(libavcodec 63, libavformat 63, libavutil 61)를 불러오지 못하면 null이고 error에 이유를 씁니다.
    /// </summary>
    /// <param name="ffmpegDirectory">공유 라이브러리가 있는 폴더. null이면 운영체제의 기본 검색 경로(Windows는 실행 파일 폴더)입니다</param>
    public static IVideoReader? Create(string? ffmpegDirectory, out string error)
    {
        if (!FfmpegLibraries.TryLoad(ffmpegDirectory, out error)) return null;
        return new Ffv1Reader();
    }

    public static IVideoReader? Create(out string error) => Create(null, out error);
}
