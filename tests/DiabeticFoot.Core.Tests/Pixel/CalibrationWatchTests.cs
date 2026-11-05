using DiabeticFoot.Core.Pixel;
using static DiabeticFoot.Core.Pixel.PixelFunctions;

namespace DiabeticFoot.Core.Tests.Pixel;

public class CalibrationWatchTests
{
    private static readonly CalibrationRule Rule = new()
    {
        FreezeRepeats = 3,
        FreezeWaitFrames = 10,
        FreezeMaxFrames = 100,
        DiscardFrames = 5,
    };

    // 서명 열(같은 숫자는 같은 프레임)을 차례로 넣은 상태입니다.
    private static CalibrationWatch Feed(IEnumerable<ulong> signatures, CalibrationRule? rule = null)
    {
        var watch = new CalibrationWatch();
        foreach (ulong s in signatures) watch = AfterCalibrationFrame(watch, s, rule ?? Rule);
        return watch;
    }

    // 반복 구간 뒤 새 프레임을 1로 세어 discard_frames장째에 끝나고, 구간 길이와 기다린 장 수를 남깁니다.
    [Fact]
    public void DetectsFreezeThenDiscardsFreshFrames()
    {
        ulong[] s = { 1, 2, 3, 3, 3, 3, 3, 3 }; // 3이 6장(반복 5)
        CalibrationWatch w = Feed(s);
        Assert.Equal(CalibrationPhase.Frozen, w.Phase);
        Assert.Equal(2, w.WaitedFrames);
        Assert.Equal(6, w.FrozenFrames);
        for (ulong v = 10; v < 14; ++v) w = AfterCalibrationFrame(w, v, Rule);
        Assert.Equal(CalibrationPhase.Discarding, w.Phase);
        Assert.Equal(4, w.Discarded);
        w = AfterCalibrationFrame(w, 14, Rule);
        Assert.Equal(CalibrationPhase.Done, w.Phase);
        Assert.Equal(5, w.Discarded);
        Assert.True(w.IsCalibrationFinished);
        // 끝난 뒤의 프레임은 상태를 바꾸지 않습니다.
        Assert.Equal(w.Received, AfterCalibrationFrame(w, 14, Rule).Received);
    }

    // 교정 밖에서 보이는 짧은 반복(실측 최대 1회)은 교정으로 보지 않습니다.
    [Fact]
    public void ShortRepeatIsNotFreeze()
    {
        CalibrationWatch w = Feed(new ulong[] { 1, 2, 2, 3, 4, 4, 4, 5 });
        Assert.Equal(CalibrationPhase.WaitingFreeze, w.Phase);
        Assert.Equal(0, w.Repeats);
    }

    // 기다리는 장 수 안에 반복이 없으면 교정 없음으로 끝납니다.
    [Fact]
    public void EndsAsNotSeenAfterWaitFrames()
    {
        var s = new List<ulong>();
        for (ulong v = 0; v < 10; ++v) s.Add(v);
        CalibrationWatch w = Feed(s);
        Assert.Equal(CalibrationPhase.NotSeen, w.Phase);
        Assert.True(w.IsCalibrationFinished);
    }

    // 같은 프레임이 상한을 넘으면 끝나지 않는 교정으로 보고 끝냅니다.
    [Fact]
    public void EndsAsStuckWhenFreezeTooLong()
    {
        var rule = new CalibrationRule { FreezeRepeats = 3, FreezeWaitFrames = 10, FreezeMaxFrames = 8, DiscardFrames = 5 };
        CalibrationWatch w = Feed(Enumerable.Repeat(7UL, 12), rule);
        Assert.Equal(CalibrationPhase.Stuck, w.Phase);
        Assert.Equal(9, w.FrozenFrames);
    }

    // 버릴 장 수가 0이면 교정 뒤 첫 새 프레임에서 끝납니다.
    [Fact]
    public void ZeroDiscardEndsAtFirstFreshFrame()
    {
        var rule = new CalibrationRule { FreezeRepeats = 3, FreezeWaitFrames = 10, FreezeMaxFrames = 100, DiscardFrames = 0 };
        CalibrationWatch w = Feed(new ulong[] { 5, 5, 5, 5, 6 }, rule);
        Assert.Equal(CalibrationPhase.Done, w.Phase);
        Assert.Equal(0, w.Discarded);
        Assert.Equal(0, w.WaitedFrames);
        Assert.Equal(4, w.FrozenFrames);
    }
}

public class FrameSignatureTests
{
    // 서명은 화소가 같으면 같고, 한 화소가 다르면 다르며, 행 끝 여백은 넣지 않습니다.
    [Fact]
    public void IgnoresStridePaddingAndSeesOnePixel()
    {
        var tight = new byte[4 * 2 * 2];
        var padded = new byte[2 * 12];
        Array.Fill(padded, (byte)0xEE);
        for (int y = 0; y < 2; ++y)
        {
            for (int i = 0; i < 8; ++i)
            {
                tight[y * 8 + i] = (byte)(y * 8 + i);
                padded[y * 12 + i] = (byte)(y * 8 + i);
            }
        }
        var a = new FrameView
        {
            Format = PixelFormat.Y16,
            Width = 4,
            Height = 2,
            Plane0 = tight,
            RowStride0 = 8,
        };
        FrameView b = a with { Plane0 = padded, RowStride0 = 12 };
        Assert.Equal(FrameSignature(b), FrameSignature(a));
        tight[5] ^= 1;
        Assert.NotEqual(FrameSignature(b), FrameSignature(a));
    }
}
