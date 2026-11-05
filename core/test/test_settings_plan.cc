#include <gtest/gtest.h>

#include <vector>

#include "hwlab/flow/settings_plan.h"

namespace {

using hwlab::CameraState;
using hwlab::SettingMode;
using hwlab::flow::CommandKind;
using hwlab::flow::FindMismatches;
using hwlab::flow::FindRestoreMismatches;
using hwlab::flow::FindUnknownSaved;
using hwlab::flow::NameOf;
using hwlab::flow::PlanCommands;
using hwlab::flow::PlanRestore;
using hwlab::flow::PreviewTarget;
using hwlab::flow::RecordingTarget;
using hwlab::flow::SettingCommand;
using hwlab::flow::SettingField;
using hwlab::flow::SettingsSpec;

// 2026-10-16 실험 노트북 카메라에서 읽은 값입니다.
const CameraState kLaptopState{false, -5, 4, false, 4600, 60, true};

const SettingsSpec kManual{SettingMode::kManual, -6, 8, 4600, 30};
const SettingsSpec kAuto{SettingMode::kAuto, 0, 4, 4600, 30};

std::vector<CommandKind> Kinds(const std::vector<SettingCommand>& commands) {
  std::vector<CommandKind> kinds;
  for (const auto& c : commands) {
    kinds.push_back(c.kind);
  }
  return kinds;
}

// 같은 항목은 명령을 만들지 않고, 다른 항목만 정해진 순서로 바꿉니다. 잠긴 카메라라 값 앞뒤로 풀고 다시 잠급니다.
TEST(SettingsPlan, ChangesOnlyDifferentFieldsInOrder) {
  const auto commands = PlanCommands(kLaptopState, PreviewTarget(kManual));

  const std::vector<SettingCommand> expected{{CommandKind::kSdkFrameRate, 30},
                                             {CommandKind::kUnlock},
                                             {CommandKind::kExposureManual, -6},
                                             {CommandKind::kGain, 8},
                                             {CommandKind::kLock}};
  EXPECT_EQ(commands, expected);
}

// 직접 지정 미리보기(잠금)에서 색온도만 바꿔도 풀고 쓴 뒤 다시 잠급니다. 잠긴 채 쓴 색온도는 영상에 반영되지 않았습니다.
TEST(SettingsPlan, LockedCameraUnlocksAroundValueChange) {
  const CameraState current = PreviewTarget(kManual);
  SettingsSpec warmer = kManual;
  warmer.white_balance_k = 3000;

  const auto commands = PlanCommands(current, PreviewTarget(warmer));

  const std::vector<SettingCommand> expected{{CommandKind::kUnlock},
                                             {CommandKind::kWhiteBalanceManual, 3000},
                                             {CommandKind::kLock}};
  EXPECT_EQ(commands, expected);
}

// 잠긴 카메라에서 fps만 바꿀 때는 잠금을 건드리지 않습니다.
TEST(SettingsPlan, LockedCameraKeepsLockForFrameRateOnly) {
  const CameraState current = PreviewTarget(kManual);
  SettingsSpec faster = kManual;
  faster.sdk_frame_rate = 60;

  const auto kinds = Kinds(PlanCommands(current, PreviewTarget(faster)));

  const std::vector<CommandKind> expected{CommandKind::kSdkFrameRate};
  EXPECT_EQ(kinds, expected);
}

// 목표에 이미 있으면 명령이 없습니다.
TEST(SettingsPlan, ReachedTargetNeedsNoCommand) {
  const CameraState target = PreviewTarget(kManual);

  EXPECT_TRUE(PlanCommands(target, target).empty());
}

// 자동 맞춤 미리보기는 잠금을 노출보다 먼저 풉니다. 잠금 중에는 펌웨어가 노출을 바꾸지 않습니다.
TEST(SettingsPlan, AutoPreviewUnlocksBeforeAutoExposure) {
  const auto kinds = Kinds(PlanCommands(kLaptopState, PreviewTarget(kAuto)));

  const std::vector<CommandKind> expected{CommandKind::kSdkFrameRate, CommandKind::kUnlock,
                                          CommandKind::kExposureAuto};
  EXPECT_EQ(kinds, expected);
}

// 자동 맞춤 미리보기에서 녹화로 넘어갈 때는 잠금 하나만 남습니다.
TEST(SettingsPlan, AutoRecordingAddsOnlyLock) {
  const CameraState preview = PreviewTarget(kAuto);

  const auto commands = PlanCommands(preview, RecordingTarget(kAuto));

  ASSERT_EQ(commands.size(), 1u);
  EXPECT_EQ(commands[0].kind, CommandKind::kLock);
}

// 실험 뒤 되돌리기는 읽어 둔 값을 목표로 쓰면 됩니다.
TEST(SettingsPlan, RestoreReturnsToSnapshot) {
  CameraState after_auto_test = PreviewTarget(kAuto);
  after_auto_test.exposure_step = -5;

  const auto kinds = Kinds(PlanCommands(after_auto_test, kLaptopState));

  const std::vector<CommandKind> expected{CommandKind::kSdkFrameRate, CommandKind::kExposureManual,
                                          CommandKind::kLock};
  EXPECT_EQ(kinds, expected);
}

// 읽지 못한 항목은 다르다고 보고 명령을 만듭니다.
TEST(SettingsPlan, UnknownCurrentValueIsSet) {
  CameraState unknown;

  const auto kinds = Kinds(PlanCommands(unknown, PreviewTarget(kManual)));

  EXPECT_EQ(kinds.size(), 5u);
  EXPECT_EQ(kinds.back(), CommandKind::kLock);
}

// 자동 맞춤의 노출 단계는 실제 값을 읽을 수 없어 목표에서 비우므로 비교하지 않습니다.
TEST(SettingsPlan, AutoTargetIgnoresStaleExposureReadback) {
  CameraState readback = RecordingTarget(kAuto);
  readback.exposure_step = -5;

  EXPECT_TRUE(FindMismatches(readback, RecordingTarget(kAuto)).empty());
}

// 색온도를 자동으로 되돌린 뒤 카메라가 스스로 바꾼 값은 불일치가 아닙니다(2026-10-16 실기 복원에서 발견).
TEST(SettingsPlan, AutoWhiteBalanceIgnoresDriftingValue) {
  CameraState snapshot{true, std::nullopt, 1, true, 4500, 60, false};
  CameraState readback = snapshot;
  readback.white_balance_k = 4700;

  EXPECT_TRUE(FindMismatches(readback, snapshot).empty());
}

// 범위 밖 노출을 넣으면 카메라가 다른 값으로 설정하므로, 다시 읽어 찾습니다.
TEST(SettingsPlan, FindsClampedExposure) {
  CameraState readback = PreviewTarget(kManual);
  readback.exposure_step = -14;
  CameraState target = PreviewTarget(kManual);
  target.exposure_step = -15;

  const auto fields = FindMismatches(readback, target);

  ASSERT_EQ(fields.size(), 1u);
  EXPECT_EQ(fields[0], SettingField::kExposureStep);
}

// 2026-10-19 실험 노트북 카메라의 시작 전 상태입니다. 자동 모드이지만 저장값(-7, 4000K)을 함께 읽습니다.
const CameraState kAutoStored{true, -7, 1, true, 4000, 60, false};

// 자동 모드 항목은 저장값을 수동 명령으로 쓴 뒤 자동으로 돌립니다(2026-10-19 실기: 자동 명령만으로는 저장값이 녹화 값으로 남음).
TEST(SettingsPlan, RestoreWritesStoredValueBeforeAutoMode) {
  const CameraState after{false, -5, 1, false, 4600, 30, true};

  const auto commands = PlanRestore(after, kAutoStored);

  const std::vector<SettingCommand> expected{{CommandKind::kSdkFrameRate, 60},
                                             {CommandKind::kUnlock},
                                             {CommandKind::kExposureManual, -7},
                                             {CommandKind::kExposureAuto},
                                             {CommandKind::kWhiteBalanceManual, 4000},
                                             {CommandKind::kWhiteBalanceAuto}};
  EXPECT_EQ(commands, expected);
}

// 저장값이 이미 같으면 모드만 자동으로 돌립니다.
TEST(SettingsPlan, RestoreSwitchesOnlyModeWhenStoredValueMatches) {
  CameraState after = kAutoStored;
  after.exposure_auto = false;
  after.gain = 8;

  const auto kinds = Kinds(PlanRestore(after, kAutoStored));

  const std::vector<CommandKind> expected{CommandKind::kExposureAuto, CommandKind::kGain};
  EXPECT_EQ(kinds, expected);
}

// 자동 모드인데 저장값만 다르면 수동으로 써서 바꾼 뒤 다시 자동으로 둡니다.
TEST(SettingsPlan, RestoreRewritesStoredValueInAutoMode) {
  CameraState after = kAutoStored;
  after.white_balance_k = 4600;

  const auto kinds = Kinds(PlanRestore(after, kAutoStored));

  const std::vector<CommandKind> expected{CommandKind::kWhiteBalanceManual, CommandKind::kWhiteBalanceAuto};
  EXPECT_EQ(kinds, expected);
}

// 시작 전 상태와 같으면 명령이 없습니다.
TEST(SettingsPlan, RestoreAtSavedStateNeedsNoCommand) {
  EXPECT_TRUE(PlanRestore(kAutoStored, kAutoStored).empty());
}

// 수동 모드였던 시작 전 상태는 PlanCommands와 같은 명령으로 되돌립니다.
TEST(SettingsPlan, RestoreManualSnapshotMatchesPlanCommands) {
  CameraState after_auto_test = PreviewTarget(kAuto);
  after_auto_test.exposure_step = -5;

  EXPECT_EQ(PlanRestore(after_auto_test, kLaptopState), PlanCommands(after_auto_test, kLaptopState));
}

// 복원 판정은 자동 모드 항목의 저장값도 비교합니다. 모드만 돌아오고 값이 남은 경우를 찾습니다.
TEST(SettingsPlan, RestoreMismatchComparesStoredValuesInAutoMode) {
  CameraState readback = kAutoStored;
  readback.exposure_step = -5;
  readback.white_balance_k = 4600;

  const auto fields = FindRestoreMismatches(readback, kAutoStored);

  const std::vector<SettingField> expected{SettingField::kExposureStep, SettingField::kWhiteBalanceK};
  EXPECT_EQ(fields, expected);
  EXPECT_TRUE(FindMismatches(readback, kAutoStored).empty());
}

// 시작 전에 읽지 못한 항목은 복원 판정에서 보지 않습니다.
TEST(SettingsPlan, RestoreMismatchSkipsUnreadSavedField) {
  CameraState saved = kAutoStored;
  saved.sdk_frame_rate.reset();
  CameraState readback = kAutoStored;
  readback.sdk_frame_rate = 30;

  EXPECT_TRUE(FindRestoreMismatches(readback, saved).empty());
}

// 실패 항목 출력에 쓰는 이름은 사이드카 키와 같습니다.
TEST(SettingsPlan, FieldAndCommandNames) {
  EXPECT_STREQ(NameOf(SettingField::kExposureStep), "exposure_step");
  EXPECT_STREQ(NameOf(SettingField::kWhiteBalanceK), "white_balance_k");
  EXPECT_STREQ(NameOf(CommandKind::kWhiteBalanceManual), "white_balance_manual");
}

// USB 재연결 직후 읽은 fps 0은 실행 전 값을 모르는 것으로 보고, fps 0 명령을 만들지 않습니다.
TEST(SettingsPlan, RestoreSkipsZeroSavedFrameRate) {
  CameraState saved = kAutoStored;
  saved.sdk_frame_rate = 0;
  CameraState after = kAutoStored;
  after.sdk_frame_rate = 30;
  after.gain = 8;

  const auto commands = PlanRestore(after, saved);

  const std::vector<SettingCommand> expected{{CommandKind::kGain, 1}};
  EXPECT_EQ(commands, expected);
  for (int32_t fps : {0, -1}) {
    saved.sdk_frame_rate = fps;
    for (const SettingCommand& c : PlanRestore(after, saved)) EXPECT_NE(c.kind, CommandKind::kSdkFrameRate) << fps;
  }
}

// fps 0으로 읽어 둔 상태는 복원 판정에서 fps를 비교하지 않고, 값 모름 항목으로 따로 알립니다.
TEST(SettingsPlan, RestoreReportsZeroSavedFrameRateAsUnknown) {
  CameraState saved = kAutoStored;
  saved.sdk_frame_rate = 0;
  CameraState readback = kAutoStored;
  readback.sdk_frame_rate = 30;

  EXPECT_TRUE(FindRestoreMismatches(readback, saved).empty());
  EXPECT_EQ(FindUnknownSaved(saved), std::vector<SettingField>{SettingField::kSdkFrameRate});
}

// 모든 항목을 읽어 둔 상태에는 값 모름 항목이 없고, 빈 항목은 선언 순서대로 알립니다.
TEST(SettingsPlan, UnknownSavedListsEmptyFieldsInOrder) {
  EXPECT_TRUE(FindUnknownSaved(kAutoStored).empty());

  CameraState saved = kAutoStored;
  saved.locked.reset();
  saved.gain.reset();

  const std::vector<SettingField> expected{SettingField::kGain, SettingField::kLocked};
  EXPECT_EQ(FindUnknownSaved(saved), expected);
  EXPECT_EQ(FindUnknownSaved(CameraState{}).size(), 7u);
}

}  // namespace
