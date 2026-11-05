using System.Diagnostics;
using System.Globalization;
using DiabeticFoot.Core;
using DiabeticFoot.Core.Pixel;
using DiabeticFoot.Core.Record;
using FFmpeg.AutoGen;

namespace DiabeticFoot.Platform;

// FFV1 무손실 압축을 MKV에 담습니다. libavcodec과 libavformat 타입은 이 파일 밖으로 나가지 않습니다.
// 인코딩 설정은 2026-10-16 실험 노트북 실측과 같습니다: level 3, 슬라이스 16, 슬라이스 CRC, 슬라이스 스레드.
internal sealed unsafe class Ffv1File : IVideoWriter
{
    // 압축한 패킷을 이만큼 메모리에 모아 두고 파일 쓰기는 전용 스레드가 합니다.
    // 실험 노트북 SSD가 쓰기를 10~27초 멈춰도(2026-11-02 실측) 압축과 수신이 멈추지 않게 합니다. DIABETICFOOT_WRITE_QUEUE_MB로 파일당 한도를 바꿉니다.
    private static readonly long MaxQueueBytes = QueueLimitBytes();

    // 맥은 EAGAIN이 35이고 Windows와 리눅스는 11입니다. FFmpeg는 AVERROR(EAGAIN)을 운영체제의 errno로 만듭니다.
    private static readonly int Eagain = OperatingSystem.IsMacOS() ? 35 : 11;

    private readonly object _gate = new();
    private readonly Queue<nint> _queue = new();
    private VideoFormat _formatInfo;
    private AVFormatContext* _format;
    private AVCodecContext* _encoder;
    private AVStream* _stream;
    private AVFrame* _frame;
    private AVPacket* _packet;
    private long _frames;
    private string _path = "";
    private Thread? _mux;
    private long _queuedBytes;
    private long _maxQueuedBytes;
    private long _producerWaitMs;
    private bool _closing;
    private volatile bool _muxFailed;
    private bool _closeFailed;

    public long FramesWritten => _frames;

    public void Dispose()
    {
        if (_format != null) Close();
    }

    private static long QueueLimitBytes()
    {
        var text = Environment.GetEnvironmentVariable("DIABETICFOOT_WRITE_QUEUE_MB");
        var mb = long.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var value) ? value : 0;
        return (mb > 0 ? mb : 768) << 20;
    }

    public bool Open(string path, VideoFormat format, long maxFrames)
    {
        _formatInfo = format;
        if (format.Format != PixelFormat.Uyvy || format.Width % 2 != 0 || format.FrameRate <= 0) return false;
        AVFormatContext* output = null;
        if (ffmpeg.avformat_alloc_output_context2(&output, null, "matroska", path) < 0) return false;
        _format = output;
        var codec = ffmpeg.avcodec_find_encoder(AVCodecID.AV_CODEC_ID_FFV1);
        if (codec == null) return Fail();
        _encoder = ffmpeg.avcodec_alloc_context3(codec);
        _encoder->width = format.Width;
        _encoder->height = format.Height;
        _encoder->pix_fmt = AVPixelFormat.AV_PIX_FMT_YUV422P;
        _encoder->time_base = new AVRational { num = 1, den = format.FrameRate };
        _encoder->framerate = new AVRational { num = format.FrameRate, den = 1 };
        _encoder->gop_size = 1;
        _encoder->level = 3;
        _encoder->thread_count = 0;  // CPU 수에 맞춤
        _encoder->thread_type = ffmpeg.FF_THREAD_SLICE;
        if ((_format->oformat->flags & ffmpeg.AVFMT_GLOBALHEADER) != 0) _encoder->flags |= ffmpeg.AV_CODEC_FLAG_GLOBAL_HEADER;
        AVDictionary* options = null;
        ffmpeg.av_dict_set(&options, "slices", "16", 0);
        ffmpeg.av_dict_set(&options, "slicecrc", "1", 0);
        var opened = ffmpeg.avcodec_open2(_encoder, codec, &options);
        ffmpeg.av_dict_free(&options);
        if (opened < 0) return Fail();

        _stream = ffmpeg.avformat_new_stream(_format, null);
        if (_stream == null || ffmpeg.avcodec_parameters_from_context(_stream->codecpar, _encoder) < 0) return Fail();
        _stream->time_base = _encoder->time_base;
        // MKV에 장당 길이(DefaultDuration)를 적습니다. 없으면 읽는 쪽(OpenCV)이 앞 몇 장의 시각으로 fps를 짐작하는데,
        // 장이 크면 짐작에 쓰는 장 수가 줄어 29.97로 읽고 장 수를 899로 셉니다(2026-10-20 실기, 440MB 이상 파일 2건).
        _stream->avg_frame_rate = _encoder->framerate;
        _stream->r_frame_rate = _encoder->framerate;
        if (ffmpeg.avio_open(&_format->pb, path, ffmpeg.AVIO_FLAG_WRITE) < 0) return Fail();
        if (ffmpeg.avformat_write_header(_format, null) < 0) return Fail();

        _frame = ffmpeg.av_frame_alloc();
        _frame->format = (int)AVPixelFormat.AV_PIX_FMT_YUV422P;
        _frame->width = format.Width;
        _frame->height = format.Height;
        if (ffmpeg.av_frame_get_buffer(_frame, 0) < 0) return Fail();
        _packet = ffmpeg.av_packet_alloc();
        _frames = 0;
        _path = path;
        _closing = false;
        _muxFailed = false;
        _closeFailed = false;
        _queuedBytes = 0;
        _maxQueuedBytes = 0;
        _producerWaitMs = 0;
        _mux = new Thread(MuxLoop) { IsBackground = true, Name = "ffv1-mux" };
        _mux.Start();
        return true;
    }

    public bool WriteFrame(ReadOnlyMemory<byte> uyvy)
    {
        var rowBytes = _formatInfo.Width * 2;
        if (_encoder == null || uyvy.Length < (long)rowBytes * _formatInfo.Height || ffmpeg.av_frame_make_writable(_frame) < 0) return false;
        var view = new FrameView
        {
            Format = PixelFormat.Uyvy,
            Width = _formatInfo.Width,
            Height = _formatInfo.Height,
            Plane0 = uyvy,
            RowStride0 = rowBytes,
        };
        var height = _formatInfo.Height;
        var y = new Span<byte>(_frame->data[0], _frame->linesize[0] * height);
        var u = new Span<byte>(_frame->data[1], _frame->linesize[1] * height);
        var v = new Span<byte>(_frame->data[2], _frame->linesize[2] * height);
        if (!PixelFunctions.UyvyToPlanar422(view, y, _frame->linesize[0], u, _frame->linesize[1], v, _frame->linesize[2])) return false;
        // 실제 촬영 시각은 사이드카에 있고, 파일 안 시각은 고정 fps로 매깁니다.
        _frame->pts = _frames;
        if (ffmpeg.avcodec_send_frame(_encoder, _frame) < 0 || !Drain()) return false;
        ++_frames;
        return true;
    }

    public bool Close()
    {
        if (_format == null) return false;
        var ok = true;
        var closeStart = Stopwatch.GetTimestamp();
        if (_encoder != null && ffmpeg.avcodec_is_open(_encoder) != 0)
        {
            ok = ffmpeg.avcodec_send_frame(_encoder, null) >= 0 && Drain();
        }
        StopMux();
        if (_encoder != null && ffmpeg.avcodec_is_open(_encoder) != 0)
        {
            ok = !_muxFailed && ffmpeg.av_write_trailer(_format) >= 0 && ok;
        }
        var closeMs = (long)Stopwatch.GetElapsedTime(closeStart).TotalMilliseconds;
        WriteQueueReport(closeMs);
        Release();
        return ok && !_closeFailed;
    }

    private void WriteQueueReport(long closeMs)
    {
        try
        {
            var maxQueuedMb = _maxQueuedBytes / (double)(1 << 20);
            File.WriteAllText(_path + ".queue.txt",
                $"frames={_frames} limit_mb={MaxQueueBytes >> 20} max_queued_mb={maxQueuedMb.ToString("F1", CultureInfo.InvariantCulture)} " +
                $"producer_wait_ms={_producerWaitMs} close_wait_ms={closeMs} mux_failed={(_muxFailed ? 1 : 0)}\n");
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
        }
    }

    private bool Drain()
    {
        for (;;)
        {
            var got = ffmpeg.avcodec_receive_packet(_encoder, _packet);
            if (got == -Eagain || got == ffmpeg.AVERROR_EOF) return true;
            if (got < 0) return false;
            // 패킷 길이가 0이면 MKV의 전체 길이에 마지막 장의 길이가 빠져, 길이와 fps로 장 수를 구하는 OpenCV가 한 장 적게 셉니다.
            if (_packet->duration <= 0) _packet->duration = 1;
            ffmpeg.av_packet_rescale_ts(_packet, _encoder->time_base, _stream->time_base);
            _packet->stream_index = _stream->index;
            var queued = ffmpeg.av_packet_alloc();
            if (queued == null) return false;
            ffmpeg.av_packet_move_ref(queued, _packet);
            if (!Enqueue(queued)) return false;
        }
    }

    // 대기열이 한도에 닿으면 자리가 날 때까지 기다립니다. 그동안은 수신 쪽 원본 대기열이 저장 누락을 셉니다.
    private bool Enqueue(AVPacket* packet)
    {
        var bytes = Math.Max(packet->size, 0);
        lock (_gate)
        {
            if (_queuedBytes + bytes > MaxQueueBytes && !_muxFailed)
            {
                var waitStart = Stopwatch.GetTimestamp();
                while (_queuedBytes + bytes > MaxQueueBytes && !_muxFailed) Monitor.Wait(_gate);
                _producerWaitMs += (long)Stopwatch.GetElapsedTime(waitStart).TotalMilliseconds;
            }
            if (_muxFailed)
            {
                ffmpeg.av_packet_free(&packet);
                return false;
            }
            _queue.Enqueue((nint)packet);
            _queuedBytes += bytes;
            _maxQueuedBytes = Math.Max(_maxQueuedBytes, _queuedBytes);
            Monitor.PulseAll(_gate);
            return true;
        }
    }

    private void MuxLoop()
    {
        for (;;)
        {
            AVPacket* packet;
            lock (_gate)
            {
                while (_queue.Count == 0 && !_closing) Monitor.Wait(_gate);
                if (_queue.Count == 0) return;
                packet = (AVPacket*)_queue.Dequeue();
                _queuedBytes -= Math.Max(packet->size, 0);
                Monitor.PulseAll(_gate);
            }
            if (!_muxFailed && ffmpeg.av_interleaved_write_frame(_format, packet) < 0)
            {
                lock (_gate)
                {
                    _muxFailed = true;
                    Monitor.PulseAll(_gate);
                }
            }
            ffmpeg.av_packet_free(&packet);
        }
    }

    private void StopMux()
    {
        if (_mux == null) return;
        lock (_gate)
        {
            _closing = true;
            Monitor.PulseAll(_gate);
        }
        _mux.Join();
        _mux = null;
    }

    private bool Fail()
    {
        Release();
        return false;
    }

    private void Release()
    {
        StopMux();
        lock (_gate)
        {
            while (_queue.Count > 0)
            {
                var packet = (AVPacket*)_queue.Dequeue();
                ffmpeg.av_packet_free(&packet);
            }
            _queuedBytes = 0;
        }
        fixed (AVPacket** packet = &_packet) ffmpeg.av_packet_free(packet);
        fixed (AVFrame** frame = &_frame) ffmpeg.av_frame_free(frame);
        fixed (AVCodecContext** encoder = &_encoder) ffmpeg.avcodec_free_context(encoder);
        if (_format != null)
        {
            // 닫을 때 남은 버퍼를 쓰다 실패하면(디스크 가득 참) 파일이 잘린 것이므로 Close가 실패로 알립니다.
            if (_format->pb != null && ffmpeg.avio_closep(&_format->pb) < 0) _closeFailed = true;
            ffmpeg.avformat_free_context(_format);
            _format = null;
        }
        _stream = null;
    }
}
