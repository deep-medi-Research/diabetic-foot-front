using FFmpeg.AutoGen;

namespace DiabeticFoot.Platform;

// FFV1(MKV) 영상에서 번호로 프레임을 읽습니다. libavcodec과 libavformat 타입은 이 파일 밖으로 나가지 않습니다.
internal sealed unsafe class Ffv1Reader : IVideoReader
{
    private AVFormatContext* _format;
    private AVCodecContext* _decoder;
    private AVPacket* _packet;
    private AVFrame* _frame;
    private int _streamIndex = -1;
    private AVRational _rate;
    private AVRational _timeBase;
    private long _start;
    private int _width;
    private int _height;
    private long _frames;
    private long _nextIndex;  // 이어서 읽으면 나올 번호. 모르면 -1

    public int Width => _width;

    public int Height => _height;

    public long FrameCount => _frames;

    public bool Open(string path, out string error)
    {
        Release();
        error = "";
        var why = OpenCore(path);
        if (why is null) return true;
        error = why + ": " + path;
        Release();
        return false;
    }

    public void Dispose() => Release();

    private string? OpenCore(string path)
    {
        AVFormatContext* format = null;
        if (ffmpeg.avformat_open_input(&format, path, null, null) < 0) return "영상 파일을 열지 못함";
        _format = format;
        if (ffmpeg.avformat_find_stream_info(_format, null) < 0) return "영상 정보를 읽지 못함";
        _streamIndex = ffmpeg.av_find_best_stream(_format, AVMediaType.AVMEDIA_TYPE_VIDEO, -1, -1, null, 0);
        if (_streamIndex < 0) return "영상 스트림이 없음";
        var stream = _format->streams[_streamIndex];
        if (stream->codecpar->format != (int)AVPixelFormat.AV_PIX_FMT_YUV422P) return "화소 형식이 4:2:2 8비트가 아님";
        var codec = ffmpeg.avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec == null) return "디코더가 없음";
        _decoder = ffmpeg.avcodec_alloc_context3(codec);
        if (_decoder == null || ffmpeg.avcodec_parameters_to_context(_decoder, stream->codecpar) < 0) return "디코더 준비 실패";
        // 프레임 단위 스레드는 출력이 몇 장 늦게 나오므로, 한 장씩 바로 받도록 슬라이스 스레드만 씁니다.
        _decoder->thread_count = 0;
        _decoder->thread_type = ffmpeg.FF_THREAD_SLICE;
        if (ffmpeg.avcodec_open2(_decoder, codec, null) < 0) return "디코더를 열지 못함";
        _rate = ffmpeg.av_guess_frame_rate(_format, stream, null);
        if (_rate.num <= 0 || _rate.den <= 0) return "fps를 알 수 없음";
        _timeBase = stream->time_base;
        _start = stream->start_time != ffmpeg.AV_NOPTS_VALUE ? stream->start_time : 0;
        _width = stream->codecpar->width;
        _height = stream->codecpar->height;
        if (_format->duration > 0)
        {
            var seconds = (double)_format->duration / ffmpeg.AV_TIME_BASE;
            _frames = (long)Math.Round(seconds * ffmpeg.av_q2d(_rate), MidpointRounding.AwayFromZero);
        }
        _packet = ffmpeg.av_packet_alloc();
        _frame = ffmpeg.av_frame_alloc();
        _nextIndex = 0;
        return null;
    }

    public bool ReadFrame(long index, ref byte[] uyvy)
    {
        if (_format == null || index < 0 || index >= _frames) return false;
        if (index != _nextIndex && !Seek(index)) return false;
        var rewound = false;
        while (ffmpeg.av_read_frame(_format, _packet) >= 0)
        {
            if (_packet->stream_index != _streamIndex)
            {
                ffmpeg.av_packet_unref(_packet);
                continue;
            }
            var got = IndexOf(_packet->pts);
            if (got < index)
            {
                ffmpeg.av_packet_unref(_packet);
                continue;
            }
            if (got > index)
            {
                ffmpeg.av_packet_unref(_packet);
                // 색인(Cues)이 없는 파일에서 탐색이 목표보다 뒤에 서면 처음부터 다시 읽습니다.
                if (rewound || !Seek(0)) break;
                rewound = true;
                continue;
            }
            var ok = Decode(ref uyvy);
            ffmpeg.av_packet_unref(_packet);
            _nextIndex = ok ? index + 1 : -1;
            return ok;
        }
        _nextIndex = -1;
        return false;
    }

    private long IndexOf(long pts)
    {
        if (pts == ffmpeg.AV_NOPTS_VALUE) return -1;
        return ffmpeg.av_rescale_q_rnd(pts - _start, _timeBase, ffmpeg.av_inv_q(_rate),
            AVRounding.AV_ROUND_NEAR_INF | AVRounding.AV_ROUND_PASS_MINMAX);
    }

    private bool Seek(long index)
    {
        var ts = _start + ffmpeg.av_rescale_q(index, ffmpeg.av_inv_q(_rate), _timeBase);
        if (ffmpeg.av_seek_frame(_format, _streamIndex, ts, ffmpeg.AVSEEK_FLAG_BACKWARD) < 0) return false;
        ffmpeg.avcodec_flush_buffers(_decoder);
        _nextIndex = -1;
        return true;
    }

    private bool Decode(ref byte[] uyvy)
    {
        if (ffmpeg.avcodec_send_packet(_decoder, _packet) < 0) return false;
        if (ffmpeg.avcodec_receive_frame(_decoder, _frame) < 0) return false;
        var ok = _frame->format == (int)AVPixelFormat.AV_PIX_FMT_YUV422P && _frame->width == _width && _frame->height == _height;
        if (ok)
        {
            var size = _width * _height * 2;
            if (uyvy.Length != size) uyvy = new byte[size];
            var o = 0;
            for (var r = 0; r < _height; r++)
            {
                var y = _frame->data[0] + (long)r * _frame->linesize[0];
                var u = _frame->data[1] + (long)r * _frame->linesize[1];
                var v = _frame->data[2] + (long)r * _frame->linesize[2];
                for (var c = 0; c < _width / 2; c++)
                {
                    uyvy[o++] = u[c];
                    uyvy[o++] = y[2 * c];
                    uyvy[o++] = v[c];
                    uyvy[o++] = y[2 * c + 1];
                }
            }
        }
        ffmpeg.av_frame_unref(_frame);
        return ok;
    }

    private void Release()
    {
        fixed (AVPacket** packet = &_packet) ffmpeg.av_packet_free(packet);
        fixed (AVFrame** frame = &_frame) ffmpeg.av_frame_free(frame);
        fixed (AVCodecContext** decoder = &_decoder) ffmpeg.avcodec_free_context(decoder);
        if (_format != null)
        {
            fixed (AVFormatContext** format = &_format) ffmpeg.avformat_close_input(format);
        }
        _width = 0;
        _height = 0;
        _frames = 0;
        _nextIndex = 0;
    }
}
