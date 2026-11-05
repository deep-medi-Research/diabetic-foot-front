using DiabeticFoot.Core;
using DiabeticFoot.Core.Record;

namespace DiabeticFoot.Platform.Tests;

public class FakeThermalTests
{
    const int Width = 640;
    const int Height = 480;
    const long SecondUs = 1_000_000;

    internal static FakeThermalScript FastScript() => new() { WallPeriodUs = 0 };

    internal static ushort Y16At(FrameView frame, int x, int y)
    {
        ReadOnlySpan<byte> p = frame.Plane0.Span[(y * frame.RowStride0 + x * 2)..];
        return (ushort)(p[0] | (p[1] << 8));
    }

    // 비어 있는 시험 폴더를 만듭니다. 렌즈 셰이딩 보정 파일을 둡니다.
    static string EmptyLensShadingFolder(string name)
    {
        string dir = Path.Combine(Path.GetTempPath(), "hwlab_test_thermal_lens_shading", name);
        if (Directory.Exists(dir)) Directory.Delete(dir, recursive: true);
        return dir;
    }

    // 누락 시나리오: 누락된 장만큼 수신 시각이 벌어지고 화소는 누락 뒤의 장치 프레임 것입니다. 받은 순서 번호에는 빠진 번호가 없습니다.
    [Fact]
    public void SkipScriptWidensIntervalAndKeepsIndex()
    {
        FakeThermalScript script = FastScript() with { Skips = new Dictionary<long, int> { [3] = 2 } };
        var camera = new FakeThermalCamera(script);
        Assert.True(camera.Open(out _));
        Assert.Equal(Width, camera.Width);
        Assert.Equal(Height, camera.Height);
        var frames = new List<ReceivedFrame>();
        var deviceFrames = new List<long>();
        for (int i = 0; i < 5; ++i)
        {
            ReceivedFrame? frame = camera.ReadFrame(SecondUs);
            Assert.NotNull(frame);
            FrameView view = frame.Value.View;
            Assert.Equal(PixelFormat.Y16, view.Format);
            Assert.Equal(StreamKind.Thermal, view.Stream);
            Assert.Equal(new FrameIndex((ulong)i), view.Index);
            Assert.Equal(frame.Value.HostTime, view.Time);
            Assert.Equal(FakeThermalCamera.PixelValue(camera.LastDeviceFrame, 5, 4), Y16At(view, 5, 4));
            deviceFrames.Add(camera.LastDeviceFrame);
            frames.Add(frame.Value);
        }
        Assert.Equal(new long[] { 0, 1, 2, 5, 6 }, deviceFrames);
        long period = script.DevicePeriodUs;
        Assert.Equal(period, Timestamp.IntervalUs(frames[1].HostTime, frames[2].HostTime));
        Assert.Equal(3 * period, Timestamp.IntervalUs(frames[2].HostTime, frames[3].HostTime));
        Assert.Equal(period, Timestamp.IntervalUs(frames[3].HostTime, frames[4].HostTime));
    }

    // 다음 프레임이 기다릴 시간 안에 오지 않으면 null이고, 시간이 흘러 프레임이 오면 받습니다.
    [Fact]
    public void ShortTimeoutReturnsNothingUntilFrameArrives()
    {
        var camera = new FakeThermalCamera(FastScript());
        Assert.True(camera.Open(out _));
        Assert.Null(camera.ReadFrame(20_000));
        Assert.Equal(ThermalConnection.Waiting, camera.Connection);
        Assert.NotNull(camera.ReadFrame(20_000));
        Assert.Equal(ThermalConnection.Receiving, camera.Connection);
    }

    // 끊김 시나리오: 장치는 열린 상태로 남고 프레임만 오지 않으며, 프레임 주기 30배가 지나면 끊김입니다. 실패 횟수만큼 다시 열기에 실패한 뒤 다시 받습니다.
    [Fact]
    public void DisconnectScriptLostAfterStallThenReopens()
    {
        var camera = new FakeThermalCamera(FastScript() with { DisconnectAfter = 4, ReopenFailures = 2 });
        Assert.Equal(ThermalConnection.Closed, camera.Connection);
        Assert.True(camera.Open(out _));
        for (int i = 0; i < 4; ++i) Assert.NotNull(camera.ReadFrame(SecondUs));
        Timestamp last = camera.Now();
        // 한 프레임 주기씩 기다리며 끊김 판정 시간(30주기)이 지날 때까지 셉니다.
        const long period = ThermalCamera.ThermalFramePeriodUs;
        int emptyReads = 0;
        while (camera.Connection == ThermalConnection.Receiving)
        {
            Assert.Null(camera.ReadFrame(period));
            Assert.True(++emptyReads < 100);
        }
        Assert.Equal(ThermalConnection.Lost, camera.Connection);
        Assert.Equal(30 * period, Timestamp.IntervalUs(last, camera.Now()));
        Assert.Equal(30, emptyReads);

        camera.Close();
        Assert.Equal(ThermalConnection.Closed, camera.Connection);
        Assert.False(camera.Open(out string error));
        Assert.NotEqual("", error);
        Assert.False(camera.Open(out error));
        Assert.True(camera.Open(out error));
        Assert.Equal(ThermalConnection.Waiting, camera.Connection);
        ReceivedFrame? frame = camera.ReadFrame(SecondUs);
        Assert.NotNull(frame);
        Assert.Equal(new FrameIndex(4), frame.Value.View.Index);
        Assert.True(frame.Value.HostTime > last);
        Assert.Equal(ThermalConnection.Receiving, camera.Connection);
    }

    // 다시 연결되지 않는 시나리오에서는 Open이 계속 실패합니다.
    [Fact]
    public void NeverReconnectsWhenScripted()
    {
        var camera = new FakeThermalCamera(FastScript() with { DisconnectAfter = 1 });
        Assert.True(camera.Open(out _));
        Assert.NotNull(camera.ReadFrame(SecondUs));
        Assert.Null(camera.ReadFrame(SecondUs));
        camera.Close();
        for (int i = 0; i < 5; ++i) Assert.False(camera.Open(out _));
        Assert.Null(camera.ReadFrame(SecondUs));
    }

    // 수신 간격 변동: 수신 간격이 주기에서 지연 최대값 범위 안으로 달라지고, 시각은 늘 증가하며, 같은 시드면 같은 순서입니다.
    [Fact]
    public void JitterStaysWithinBoundAndRepeats()
    {
        FakeThermalScript script = FastScript() with { Width = 8, Height = 4, JitterUs = 8_000 };
        List<long> Arrivals()
        {
            var camera = new FakeThermalCamera(script);
            Assert.True(camera.Open(out _));
            var times = new List<long>();
            for (int i = 0; i < 200; ++i) times.Add(camera.ReadFrame(SecondUs)!.Value.HostTime.Us);
            return times;
        }
        List<long> times = Arrivals();
        bool varied = false;
        for (int i = 1; i < times.Count; ++i)
        {
            long gap = times[i] - times[i - 1];
            Assert.True(gap >= script.DevicePeriodUs - script.JitterUs, $"{i}: {gap}");
            Assert.True(gap <= script.DevicePeriodUs + script.JitterUs, $"{i}: {gap}");
            Assert.True(gap > 0, $"{i}: {gap}");
            varied = varied || gap != script.DevicePeriodUs;
            // 지연은 장치가 프레임을 낸 시각 뒤로만 붙습니다.
            long ideal = (i + 1) * script.DevicePeriodUs;
            Assert.True(times[i] >= ideal, $"{i}: {times[i]}");
            Assert.True(times[i] <= ideal + script.JitterUs, $"{i}: {times[i]}");
        }
        Assert.True(varied);
        Assert.Equal(times, Arrivals());
    }

    // 셔터 교정 제어: 열기 전에는 실패하고, 연 뒤에는 교정 횟수와 멈춤 상태가 남으며, Close가 멈춤을 되돌립니다.
    [Fact]
    public void CalibrationControlNeedsOpenAndCloseReleasesHold()
    {
        var camera = new FakeThermalCamera(new FakeThermalScript());
        Assert.False(camera.CalibrateNow(out string error));
        Assert.NotEqual("", error);
        Assert.False(camera.HoldAutoCalibration(true, out error));
        Assert.Equal("", camera.AutoCalibrationSetting);

        Assert.True(camera.Open(out error));
        Assert.True(camera.CalibrateNow(out error));
        Assert.True(camera.CalibrateNow(out error));
        Assert.Equal(2, camera.Calibrations);
        Assert.True(camera.HoldAutoCalibration(true, out error));
        Assert.True(camera.AutoCalibrationHeld);
        Assert.Equal("fake, held", camera.AutoCalibrationSetting);
        Assert.True(camera.HoldAutoCalibration(false, out error));
        Assert.False(camera.AutoCalibrationHeld);

        Assert.True(camera.HoldAutoCalibration(true, out error));
        camera.Close();
        Assert.False(camera.AutoCalibrationHeld);
    }

    // 원래 셔터 설정을 모르는 장치: 교정과 멈춤 모두 실패하고 이유를 남기며, 상태를 바꾸지 않습니다.
    [Fact]
    public void UnsupportedCalibrationLeavesStateUnchanged()
    {
        var camera = new FakeThermalCamera(new FakeThermalScript { CalibrationSupported = false });
        Assert.True(camera.Open(out string error));
        Assert.False(camera.CalibrateNow(out error));
        Assert.NotEqual("", error);
        Assert.False(camera.HoldAutoCalibration(true, out error));
        Assert.NotEqual("", error);
        Assert.Equal(0, camera.Calibrations);
        Assert.False(camera.AutoCalibrationHeld);
    }

    // 보정 파일 폴더를 정하지 않은 카메라는 보정 기능이 없고 보정 요청도 받지 않습니다.
    [Fact]
    public void LensShadingNeedsFolder()
    {
        var camera = new FakeThermalCamera(new FakeThermalScript());
        Assert.True(camera.Open(out string error));
        Assert.Null(camera.LensShading);
        Assert.False(camera.CorrectLensShading(out error));
        Assert.NotEqual("", error);
    }

    // 보정 파일이 없으면 None이고, 보정하면 파일을 쓰며, 닫았다 다시 열면 그 파일을 불러옵니다.
    [Fact]
    public void LensShadingSavesAndLoadsFile()
    {
        string folder = EmptyLensShadingFolder("save_load");
        var camera = new FakeThermalCamera(new FakeThermalScript());
        camera.SetLensShadingFolder(folder);
        Assert.False(camera.CorrectLensShading(out string error));  // 열기 전
        Assert.True(camera.Open(out error));
        LensShadingStatus? initial = camera.LensShading;
        Assert.NotNull(initial);
        Assert.Equal(LensShadingSource.None, initial.Record.Source);
        Assert.Null(initial.Record.FileUnixS);

        Assert.True(camera.CorrectLensShading(out error), error);
        LensShadingStatus? corrected = camera.LensShading;
        Assert.NotNull(corrected);
        Assert.NotNull(corrected.Record.FileUnixS);
        Assert.Equal(LensShadingSource.Corrected, corrected.Record.Source);
        Assert.True(File.Exists(Path.Combine(folder, "lens_shading_FakeThermal.cal")));

        camera.Close();
        Assert.Null(camera.LensShading);
        Assert.True(camera.Open(out error));
        LensShadingStatus? loaded = camera.LensShading;
        Assert.NotNull(loaded);
        Assert.Equal(LensShadingSource.Loaded, loaded.Record.Source);
        Assert.Equal(corrected.Record.FileUnixS, loaded.Record.FileUnixS);
    }

    // 보정이 실패하면 Failed와 이유를 남기고 파일을 쓰지 않습니다.
    [Fact]
    public void LensShadingFailureKeepsNoFile()
    {
        string folder = EmptyLensShadingFolder("failure");
        var camera = new FakeThermalCamera(new FakeThermalScript { LensShadingFails = true });
        camera.SetLensShadingFolder(folder);
        Assert.True(camera.Open(out string error));
        Assert.False(camera.CorrectLensShading(out error));
        Assert.NotEqual("", error);
        LensShadingStatus? failed = camera.LensShading;
        Assert.NotNull(failed);
        Assert.Equal(LensShadingSource.Failed, failed.Record.Source);
        Assert.NotEqual("", failed.Error);
        Assert.False(File.Exists(Path.Combine(folder, "lens_shading_FakeThermal.cal")));
    }
}

public class ThermalConnectionRuleTests
{
    // 연결 판정: 기본값은 첫 프레임 전과 받던 중 모두 프레임 주기의 30배(1초)이고, 그 안이면 대기와 수신입니다.
    [Fact]
    public void LostAfterThirtyFramePeriods()
    {
        var rule = new ThermalLossRule();
        const long limit = 30 * ThermalCamera.ThermalFramePeriodUs;
        Assert.Equal(limit, rule.FirstFrameUs);
        Assert.Equal(limit, rule.StallUs);
        var opened = new Timestamp(1_000);
        Assert.Equal(ThermalConnection.Closed, ThermalCamera.JudgeThermalConnection(false, opened, null, new Timestamp(2_000), rule));
        Assert.Equal(ThermalConnection.Waiting,
            ThermalCamera.JudgeThermalConnection(true, opened, null, new Timestamp(opened.Us + limit - 1), rule));
        Assert.Equal(ThermalConnection.Lost,
            ThermalCamera.JudgeThermalConnection(true, opened, null, new Timestamp(opened.Us + limit), rule));
        var last = new Timestamp(opened.Us + 3 * 1_000_000);
        Assert.Equal(ThermalConnection.Receiving,
            ThermalCamera.JudgeThermalConnection(true, opened, last, new Timestamp(last.Us + limit - 1), rule));
        Assert.Equal(ThermalConnection.Lost, ThermalCamera.JudgeThermalConnection(true, opened, last, new Timestamp(last.Us + limit), rule));
    }
}

public class LensShadingFileTests
{
    // 보정 파일 이름에는 장치 이름(모델과 코어 번호)을 넣어 카메라마다 따로 둡니다.
    [Fact]
    public void NameHasDevice()
    {
        Assert.Equal("lens_shading_TE-EV1-001234.cal", ThermalCamera.LensShadingFileName("TE-EV1-001234"));
    }
}
