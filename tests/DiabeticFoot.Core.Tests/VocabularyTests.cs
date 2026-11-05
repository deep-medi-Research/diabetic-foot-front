using DiabeticFoot.Core;

namespace DiabeticFoot.Core.Tests;

public class VocabularyTests
{
    [Fact]
    public void TimestampComparesByMicroseconds()
    {
        var early = new Timestamp(10);
        var late = new Timestamp(20);

        Assert.True(early < late);
        Assert.True(late > early);
        Assert.True(early <= new Timestamp(10));
        Assert.True(late >= new Timestamp(20));
        Assert.Equal(new Timestamp(10), early);
        Assert.Equal(default, new Timestamp(0));
    }

    [Fact]
    public void IntervalIsNegativeWhenToIsEarlier()
    {
        Assert.Equal(7, Timestamp.IntervalUs(new Timestamp(3), new Timestamp(10)));
        Assert.Equal(-7, Timestamp.IntervalUs(new Timestamp(10), new Timestamp(3)));
    }

    [Fact]
    public void FrameIndexConvertsFromUnsignedInteger()
    {
        FrameIndex index = 41UL;

        Assert.Equal(41UL, index.Value);
        Assert.Equal(41UL, (ulong)index);
    }

    [Fact]
    public void DefaultFrameViewIsEmptyUyvyRgb()
    {
        var frame = default(FrameView);

        Assert.Equal(PixelFormat.Uyvy, frame.Format);
        Assert.Equal(StreamKind.Rgb, frame.Stream);
        Assert.True(frame.Plane0.IsEmpty);
    }

    [Fact]
    public void ExposureStepIsPowerOfTwoSeconds()
    {
        Assert.Equal(31.25, CameraSettings.ExposureStepMs(-5));
        Assert.Equal(1000.0, CameraSettings.ExposureStepMs(0));
        Assert.Equal(2000.0, CameraSettings.ExposureStepMs(1));
    }

    [Fact]
    public void DefaultCameraStateHasNoValues()
    {
        var state = default(CameraState);

        Assert.Null(state.ExposureAuto);
        Assert.Null(state.SdkFrameRate);
        Assert.Equal(new CameraState(), state);
        Assert.NotEqual(new CameraState { Gain = 0 }, state);
    }
}
