using DiabeticFoot.Core.Flow;
using static DiabeticFoot.Core.Flow.FlowFunctions;

namespace DiabeticFoot.Core.Tests.Flow;

public class SettingsPlanTests
{
    // 2026-10-16 실험 노트북 카메라에서 읽은 값입니다.
    private static readonly CameraState LaptopState = new()
    {
        ExposureAuto = false,
        ExposureStep = -5,
        Gain = 4,
        WhiteBalanceAuto = false,
        WhiteBalanceK = 4600,
        SdkFrameRate = 60,
        Locked = true,
    };

    private static readonly SettingsSpec Manual = new()
    {
        Mode = SettingMode.Manual,
        ExposureStep = -6,
        Gain = 8,
        WhiteBalanceK = 4600,
        SdkFrameRate = 30,
    };

    private static readonly SettingsSpec Auto = new()
    {
        Mode = SettingMode.Auto,
        ExposureStep = 0,
        Gain = 4,
        WhiteBalanceK = 4600,
        SdkFrameRate = 30,
    };

    // 2026-10-19 실험 노트북 카메라의 시작 전 상태입니다. 자동 모드이지만 저장값(-7, 4000K)을 함께 읽습니다.
    private static readonly CameraState AutoStored = new()
    {
        ExposureAuto = true,
        ExposureStep = -7,
        Gain = 1,
        WhiteBalanceAuto = true,
        WhiteBalanceK = 4000,
        SdkFrameRate = 60,
        Locked = false,
    };

    private static List<CommandKind> Kinds(IReadOnlyList<SettingCommand> commands)
    {
        var kinds = new List<CommandKind>();
        foreach (var c in commands)
        {
            kinds.Add(c.Kind);
        }
        return kinds;
    }

    // 같은 항목은 명령을 만들지 않고, 다른 항목만 정해진 순서로 바꿉니다. 잠긴 카메라라 값 앞뒤로 풀고 다시 잠급니다.
    [Fact]
    public void ChangesOnlyDifferentFieldsInOrder()
    {
        var commands = PlanCommands(LaptopState, PreviewTarget(Manual));

        var expected = new List<SettingCommand>
        {
            new(CommandKind.SdkFrameRate, 30),
            new(CommandKind.Unlock),
            new(CommandKind.ExposureManual, -6),
            new(CommandKind.Gain, 8),
            new(CommandKind.Lock),
        };
        Assert.Equal(expected, commands);
    }

    // 직접 지정 미리보기(잠금)에서 색온도만 바꿔도 풀고 쓴 뒤 다시 잠급니다. 잠긴 채 쓴 색온도는 영상에 반영되지 않았습니다.
    [Fact]
    public void LockedCameraUnlocksAroundValueChange()
    {
        var current = PreviewTarget(Manual);
        var warmer = Manual with { WhiteBalanceK = 3000 };

        var commands = PlanCommands(current, PreviewTarget(warmer));

        var expected = new List<SettingCommand>
        {
            new(CommandKind.Unlock),
            new(CommandKind.WhiteBalanceManual, 3000),
            new(CommandKind.Lock),
        };
        Assert.Equal(expected, commands);
    }

    // 잠긴 카메라에서 fps만 바꿀 때는 잠금을 건드리지 않습니다.
    [Fact]
    public void LockedCameraKeepsLockForFrameRateOnly()
    {
        var current = PreviewTarget(Manual);
        var faster = Manual with { SdkFrameRate = 60 };

        var kinds = Kinds(PlanCommands(current, PreviewTarget(faster)));

        var expected = new List<CommandKind> { CommandKind.SdkFrameRate };
        Assert.Equal(expected, kinds);
    }

    // 목표에 이미 있으면 명령이 없습니다.
    [Fact]
    public void ReachedTargetNeedsNoCommand()
    {
        var target = PreviewTarget(Manual);

        Assert.Empty(PlanCommands(target, target));
    }

    // 자동 맞춤 미리보기는 잠금을 노출보다 먼저 풉니다. 잠금 중에는 펌웨어가 노출을 바꾸지 않습니다.
    [Fact]
    public void AutoPreviewUnlocksBeforeAutoExposure()
    {
        var kinds = Kinds(PlanCommands(LaptopState, PreviewTarget(Auto)));

        var expected = new List<CommandKind>
        {
            CommandKind.SdkFrameRate,
            CommandKind.Unlock,
            CommandKind.ExposureAuto,
        };
        Assert.Equal(expected, kinds);
    }

    // 자동 맞춤 미리보기에서 녹화로 넘어갈 때는 잠금 하나만 남습니다.
    [Fact]
    public void AutoRecordingAddsOnlyLock()
    {
        var preview = PreviewTarget(Auto);

        var commands = PlanCommands(preview, RecordingTarget(Auto));

        var command = Assert.Single(commands);
        Assert.Equal(CommandKind.Lock, command.Kind);
    }

    // 실험 뒤 되돌리기는 읽어 둔 값을 목표로 쓰면 됩니다.
    [Fact]
    public void RestoreReturnsToSnapshot()
    {
        var afterAutoTest = PreviewTarget(Auto) with { ExposureStep = -5 };

        var kinds = Kinds(PlanCommands(afterAutoTest, LaptopState));

        var expected = new List<CommandKind>
        {
            CommandKind.SdkFrameRate,
            CommandKind.ExposureManual,
            CommandKind.Lock,
        };
        Assert.Equal(expected, kinds);
    }

    // 읽지 못한 항목은 다르다고 보고 명령을 만듭니다.
    [Fact]
    public void UnknownCurrentValueIsSet()
    {
        CameraState unknown = default;

        var kinds = Kinds(PlanCommands(unknown, PreviewTarget(Manual)));

        Assert.Equal(5, kinds.Count);
        Assert.Equal(CommandKind.Lock, kinds[^1]);
    }

    // 자동 맞춤의 노출 단계는 실제 값을 읽을 수 없어 목표에서 비우므로 비교하지 않습니다.
    [Fact]
    public void AutoTargetIgnoresStaleExposureReadback()
    {
        var readback = RecordingTarget(Auto) with { ExposureStep = -5 };

        Assert.Empty(FindMismatches(readback, RecordingTarget(Auto)));
    }

    // 색온도를 자동으로 되돌린 뒤 카메라가 스스로 바꾼 값은 불일치가 아닙니다(2026-10-16 실기 복원에서 발견).
    [Fact]
    public void AutoWhiteBalanceIgnoresDriftingValue()
    {
        var snapshot = new CameraState
        {
            ExposureAuto = true,
            ExposureStep = null,
            Gain = 1,
            WhiteBalanceAuto = true,
            WhiteBalanceK = 4500,
            SdkFrameRate = 60,
            Locked = false,
        };
        var readback = snapshot with { WhiteBalanceK = 4700 };

        Assert.Empty(FindMismatches(readback, snapshot));
    }

    // 범위 밖 노출을 넣으면 카메라가 다른 값으로 설정하므로, 다시 읽어 찾습니다.
    [Fact]
    public void FindsClampedExposure()
    {
        var readback = PreviewTarget(Manual) with { ExposureStep = -14 };
        var target = PreviewTarget(Manual) with { ExposureStep = -15 };

        var fields = FindMismatches(readback, target);

        var field = Assert.Single(fields);
        Assert.Equal(SettingField.ExposureStep, field);
    }

    // 자동 모드 항목은 저장값을 수동 명령으로 쓴 뒤 자동으로 돌립니다(2026-10-19 실기: 자동 명령만으로는 저장값이 녹화 값으로 남음).
    [Fact]
    public void RestoreWritesStoredValueBeforeAutoMode()
    {
        var after = new CameraState
        {
            ExposureAuto = false,
            ExposureStep = -5,
            Gain = 1,
            WhiteBalanceAuto = false,
            WhiteBalanceK = 4600,
            SdkFrameRate = 30,
            Locked = true,
        };

        var commands = PlanRestore(after, AutoStored);

        var expected = new List<SettingCommand>
        {
            new(CommandKind.SdkFrameRate, 60),
            new(CommandKind.Unlock),
            new(CommandKind.ExposureManual, -7),
            new(CommandKind.ExposureAuto),
            new(CommandKind.WhiteBalanceManual, 4000),
            new(CommandKind.WhiteBalanceAuto),
        };
        Assert.Equal(expected, commands);
    }

    // 저장값이 이미 같으면 모드만 자동으로 돌립니다.
    [Fact]
    public void RestoreSwitchesOnlyModeWhenStoredValueMatches()
    {
        var after = AutoStored with { ExposureAuto = false, Gain = 8 };

        var kinds = Kinds(PlanRestore(after, AutoStored));

        var expected = new List<CommandKind> { CommandKind.ExposureAuto, CommandKind.Gain };
        Assert.Equal(expected, kinds);
    }

    // 자동 모드인데 저장값만 다르면 수동으로 써서 바꾼 뒤 다시 자동으로 둡니다.
    [Fact]
    public void RestoreRewritesStoredValueInAutoMode()
    {
        var after = AutoStored with { WhiteBalanceK = 4600 };

        var kinds = Kinds(PlanRestore(after, AutoStored));

        var expected = new List<CommandKind> { CommandKind.WhiteBalanceManual, CommandKind.WhiteBalanceAuto };
        Assert.Equal(expected, kinds);
    }

    // 시작 전 상태와 같으면 명령이 없습니다.
    [Fact]
    public void RestoreAtSavedStateNeedsNoCommand()
    {
        Assert.Empty(PlanRestore(AutoStored, AutoStored));
    }

    // 수동 모드였던 시작 전 상태는 PlanCommands와 같은 명령으로 되돌립니다.
    [Fact]
    public void RestoreManualSnapshotMatchesPlanCommands()
    {
        var afterAutoTest = PreviewTarget(Auto) with { ExposureStep = -5 };

        Assert.Equal(PlanCommands(afterAutoTest, LaptopState), PlanRestore(afterAutoTest, LaptopState));
    }

    // 복원 판정은 자동 모드 항목의 저장값도 비교합니다. 모드만 돌아오고 값이 남은 경우를 찾습니다.
    [Fact]
    public void RestoreMismatchComparesStoredValuesInAutoMode()
    {
        var readback = AutoStored with { ExposureStep = -5, WhiteBalanceK = 4600 };

        var fields = FindRestoreMismatches(readback, AutoStored);

        var expected = new List<SettingField> { SettingField.ExposureStep, SettingField.WhiteBalanceK };
        Assert.Equal(expected, fields);
        Assert.Empty(FindMismatches(readback, AutoStored));
    }

    // 시작 전에 읽지 못한 항목은 복원 판정에서 보지 않습니다.
    [Fact]
    public void RestoreMismatchSkipsUnreadSavedField()
    {
        var saved = AutoStored with { SdkFrameRate = null };
        var readback = AutoStored with { SdkFrameRate = 30 };

        Assert.Empty(FindRestoreMismatches(readback, saved));
    }

    // 실패 항목 출력에 쓰는 이름은 사이드카 키와 같습니다.
    [Fact]
    public void FieldAndCommandNames()
    {
        Assert.Equal("exposure_step", NameOf(SettingField.ExposureStep));
        Assert.Equal("white_balance_k", NameOf(SettingField.WhiteBalanceK));
        Assert.Equal("white_balance_manual", NameOf(CommandKind.WhiteBalanceManual));
    }

    // USB 재연결 직후 읽은 fps 0은 실행 전 값을 모르는 것으로 보고, fps 0 명령을 만들지 않습니다.
    [Fact]
    public void RestoreSkipsZeroSavedFrameRate()
    {
        var saved = AutoStored with { SdkFrameRate = 0 };
        var after = AutoStored with { SdkFrameRate = 30, Gain = 8 };

        var commands = PlanRestore(after, saved);

        var expected = new List<SettingCommand> { new(CommandKind.Gain, 1) };
        Assert.Equal(expected, commands);
        foreach (int fps in new[] { 0, -1 })
        {
            saved = saved with { SdkFrameRate = fps };
            foreach (SettingCommand c in PlanRestore(after, saved))
            {
                Assert.True(c.Kind != CommandKind.SdkFrameRate, $"fps {fps}");
            }
        }
    }

    // fps 0으로 읽어 둔 상태는 복원 판정에서 fps를 비교하지 않고, 값 모름 항목으로 따로 알립니다.
    [Fact]
    public void RestoreReportsZeroSavedFrameRateAsUnknown()
    {
        var saved = AutoStored with { SdkFrameRate = 0 };
        var readback = AutoStored with { SdkFrameRate = 30 };

        Assert.Empty(FindRestoreMismatches(readback, saved));
        Assert.Equal(new[] { SettingField.SdkFrameRate }, FindUnknownSaved(saved));
    }

    // 모든 항목을 읽어 둔 상태에는 값 모름 항목이 없고, 빈 항목은 선언 순서대로 알립니다.
    [Fact]
    public void UnknownSavedListsEmptyFieldsInOrder()
    {
        Assert.Empty(FindUnknownSaved(AutoStored));

        var saved = AutoStored with { Locked = null, Gain = null };

        var expected = new List<SettingField> { SettingField.Gain, SettingField.Locked };
        Assert.Equal(expected, FindUnknownSaved(saved));
        Assert.Equal(7, FindUnknownSaved(new CameraState()).Count);
    }
}
