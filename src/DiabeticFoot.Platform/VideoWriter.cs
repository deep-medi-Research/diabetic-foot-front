using DiabeticFoot.Core.Flow;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform;

/// <summary>영상 파일 하나를 씁니다. 구현은 무압축 AVI와 FFV1(MKV)입니다. 쓰기 스레드 한 곳에서만 씁니다.</summary>
public interface IVideoWriter : IDisposable
{
    /// <summary>
    /// 파일을 만듭니다.
    /// </summary>
    /// <param name="path">파일 경로</param>
    /// <param name="format">화소 형식과 크기와 fps. UYVY만 받습니다</param>
    /// <param name="maxFrames">쓸 최대 프레임 수</param>
    /// <returns>만들었으면 true</returns>
    bool Open(string path, VideoFormat format, long maxFrames);

    /// <summary>
    /// 프레임 하나를 씁니다.
    /// </summary>
    /// <param name="uyvy">행 사이 빈 바이트가 없는 UYVY 화소. 앞의 Width * Height * 2 바이트만 읽고, 그보다 짧으면 쓰지 않고 false입니다</param>
    /// <returns>썼으면 true</returns>
    bool WriteFrame(ReadOnlyMemory<byte> uyvy);

    /// <summary>남은 데이터를 쓰고 파일을 닫습니다. 일찍 멈춰도 그때까지 쓴 프레임으로 읽을 수 있는 파일이 됩니다. 열려 있지 않으면 false입니다.</summary>
    bool Close();

    /// <summary>쓴 프레임 수입니다.</summary>
    long FramesWritten { get; }
}

/// <summary>영상 쓰기 객체를 만듭니다.</summary>
public static class VideoWriters
{
    /// <summary>
    /// 코덱에 맞는 영상 쓰기 객체를 만듭니다.
    /// </summary>
    /// <param name="codec">코덱</param>
    /// <param name="ffmpegDirectory">FFmpeg 공유 라이브러리가 있는 폴더. null이면 운영체제의 기본 검색 경로입니다. 무압축 AVI는 쓰지 않습니다</param>
    /// <param name="error">실패하면 이유를 씁니다</param>
    /// <returns>쓰기 객체. FFV1인데 FFmpeg 7.1 공유 라이브러리를 불러오지 못하면 null입니다</returns>
    public static IVideoWriter? Create(VideoCodec codec, string? ffmpegDirectory, out string error)
    {
        error = "";
        switch (codec)
        {
            case VideoCodec.Uncompressed:
                return new AviFile();
            case VideoCodec.Ffv1:
                return FfmpegLibraries.TryLoad(ffmpegDirectory, out error) ? new Ffv1File() : null;
            default:
                error = "알 수 없는 코덱";
                return null;
        }
    }

    public static IVideoWriter? Create(VideoCodec codec, out string error) => Create(codec, null, out error);
}
