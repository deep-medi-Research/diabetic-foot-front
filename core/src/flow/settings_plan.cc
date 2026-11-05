#include "hwlab/flow/settings_plan.h"

#include <algorithm>

namespace hwlab::flow {

namespace {

template <typename T>
bool Differs(const std::optional<T>& current, const std::optional<T>& target) {
  return target.has_value() && current != target;
}

// 재연결 직후 SDK가 fps를 0으로 돌려준 실측이 있어, 0 이하는 실행 전 값을 읽지 못한 것으로 봅니다.
bool KnownFrameRate(const std::optional<int32_t>& fps) { return fps.has_value() && *fps > 0; }

// 복원 목표에서 실행 전 값을 모르는 항목을 비웁니다. 빈 항목은 명령도 비교도 하지 않습니다.
CameraState KnownPart(const CameraState& saved) {
  CameraState known = saved;
  if (!KnownFrameRate(known.sdk_frame_rate)) known.sdk_frame_rate.reset();
  return known;
}

std::vector<SettingField> Mismatches(const CameraState& readback, const CameraState& target,
                                     bool compare_auto_values) {
  std::vector<SettingField> fields;
  if (Differs(readback.exposure_auto, target.exposure_auto)) {
    fields.push_back(SettingField::kExposureAuto);
  }
  // 목표가 자동인 항목은 카메라가 값을 스스로 바꾸므로 값을 비교하지 않습니다. 복원은 저장값을 비교합니다.
  if ((compare_auto_values || target.exposure_auto != true) &&
      Differs(readback.exposure_step, target.exposure_step)) {
    fields.push_back(SettingField::kExposureStep);
  }
  if (Differs(readback.gain, target.gain)) {
    fields.push_back(SettingField::kGain);
  }
  if (Differs(readback.white_balance_auto, target.white_balance_auto)) {
    fields.push_back(SettingField::kWhiteBalanceAuto);
  }
  if ((compare_auto_values || target.white_balance_auto != true) &&
      Differs(readback.white_balance_k, target.white_balance_k)) {
    fields.push_back(SettingField::kWhiteBalanceK);
  }
  if (Differs(readback.sdk_frame_rate, target.sdk_frame_rate)) {
    fields.push_back(SettingField::kSdkFrameRate);
  }
  if (Differs(readback.locked, target.locked)) {
    fields.push_back(SettingField::kLocked);
  }
  return fields;
}

}  // namespace

CameraState PreviewTarget(const SettingsSpec& spec) {
  CameraState target;
  target.gain = spec.gain;
  target.white_balance_auto = false;
  target.white_balance_k = spec.white_balance_k;
  target.sdk_frame_rate = spec.sdk_frame_rate;
  if (spec.mode == SettingMode::kAuto) {
    target.exposure_auto = true;
    target.locked = false;
  } else {
    target.exposure_auto = false;
    target.exposure_step = spec.exposure_step;
    target.locked = true;
  }
  return target;
}

CameraState RecordingTarget(const SettingsSpec& spec) {
  CameraState target = PreviewTarget(spec);
  target.locked = true;
  return target;
}

std::vector<SettingCommand> PlanCommands(const CameraState& current, const CameraState& target) {
  std::vector<SettingCommand> commands;
  if (Differs(current.sdk_frame_rate, target.sdk_frame_rate)) {
    commands.push_back({CommandKind::kSdkFrameRate, *target.sdk_frame_rate});
  }
  if (target.locked == false && current.locked != false) {
    commands.push_back({CommandKind::kUnlock});
  }
  if (target.exposure_auto == true) {
    if (current.exposure_auto != true) {
      commands.push_back({CommandKind::kExposureAuto});
    }
  } else if (target.exposure_auto == false || target.exposure_step.has_value()) {
    // 수동 노출은 값과 함께 설정합니다. 목표 값이 없으면 지금 값을 그대로 수동으로 둡니다.
    const std::optional<int32_t> step =
        target.exposure_step.has_value() ? target.exposure_step : current.exposure_step;
    if (step.has_value() &&
        (current.exposure_auto != false || current.exposure_step != step)) {
      commands.push_back({CommandKind::kExposureManual, *step});
    }
  }
  if (Differs(current.gain, target.gain)) {
    commands.push_back({CommandKind::kGain, *target.gain});
  }
  if (target.white_balance_auto == true) {
    if (current.white_balance_auto != true) {
      commands.push_back({CommandKind::kWhiteBalanceAuto});
    }
  } else if (target.white_balance_k.has_value() &&
             (current.white_balance_auto != false ||
              current.white_balance_k != target.white_balance_k)) {
    commands.push_back({CommandKind::kWhiteBalanceManual, *target.white_balance_k});
  }
  if (target.locked == true && current.locked != true) {
    commands.push_back({CommandKind::kLock});
  }
  // 잠긴 카메라에 쓴 색온도는 다시 읽으면 새 값인데 영상에는 반영되지 않았습니다(2026-10-27 실기, 재연결하면 반영).
  // 잠금을 유지하는 목표에서 노출, 게인, 색온도를 바꾸면 풀고 쓴 뒤 다시 잠급니다.
  const bool changes_value = std::any_of(commands.begin(), commands.end(), [](const SettingCommand& c) {
    return c.kind != CommandKind::kSdkFrameRate && c.kind != CommandKind::kUnlock && c.kind != CommandKind::kLock;
  });
  if (target.locked == true && current.locked == true && changes_value) {
    const auto first_value = std::find_if(commands.begin(), commands.end(), [](const SettingCommand& c) {
      return c.kind != CommandKind::kSdkFrameRate;
    });
    commands.insert(first_value, {CommandKind::kUnlock});
    commands.push_back({CommandKind::kLock});
  }
  return commands;
}

std::vector<SettingField> FindMismatches(const CameraState& readback, const CameraState& target) {
  return Mismatches(readback, target, false);
}

std::vector<SettingCommand> PlanRestore(const CameraState& current, const CameraState& saved_state) {
  const CameraState saved = KnownPart(saved_state);
  // 자동 모드 항목은 목표에서 빼고 PlanCommands에 맡긴 뒤, 저장값 쓰기와 자동 전환을 그 자리에 넣습니다.
  CameraState rest = saved;
  const bool exposure_auto = saved.exposure_auto == true;
  const bool balance_auto = saved.white_balance_auto == true;
  if (exposure_auto) {
    rest.exposure_auto.reset();
    rest.exposure_step.reset();
  }
  if (balance_auto) {
    rest.white_balance_auto.reset();
    rest.white_balance_k.reset();
  }
  std::vector<SettingCommand> planned = PlanCommands(current, rest);
  std::vector<SettingCommand> exposure;
  if (exposure_auto) {
    const bool value_differs = saved.exposure_step.has_value() && current.exposure_step != saved.exposure_step;
    if (value_differs) exposure.push_back({CommandKind::kExposureManual, *saved.exposure_step});
    if (value_differs || current.exposure_auto != true) exposure.push_back({CommandKind::kExposureAuto});
  }
  std::vector<SettingCommand> balance;
  if (balance_auto) {
    const bool value_differs =
        saved.white_balance_k.has_value() && current.white_balance_k != saved.white_balance_k;
    if (value_differs) balance.push_back({CommandKind::kWhiteBalanceManual, *saved.white_balance_k});
    if (value_differs || current.white_balance_auto != true) balance.push_back({CommandKind::kWhiteBalanceAuto});
  }
  // PlanCommands의 순서(fps, 잠금 풀기, 노출, 게인, 색온도, 잠금)를 유지합니다.
  std::vector<SettingCommand> commands;
  auto it = planned.begin();
  auto take_while = [&](auto pred) {
    while (it != planned.end() && pred(it->kind)) commands.push_back(*it++);
  };
  take_while([](CommandKind k) { return k == CommandKind::kSdkFrameRate || k == CommandKind::kUnlock; });
  commands.insert(commands.end(), exposure.begin(), exposure.end());
  take_while([](CommandKind k) {
    return k == CommandKind::kExposureAuto || k == CommandKind::kExposureManual || k == CommandKind::kGain;
  });
  commands.insert(commands.end(), balance.begin(), balance.end());
  commands.insert(commands.end(), it, planned.end());
  return commands;
}

std::vector<SettingField> FindRestoreMismatches(const CameraState& readback, const CameraState& saved) {
  return Mismatches(readback, KnownPart(saved), true);
}

std::vector<SettingField> FindUnknownSaved(const CameraState& saved) {
  std::vector<SettingField> fields;
  if (!saved.exposure_auto) fields.push_back(SettingField::kExposureAuto);
  if (!saved.exposure_step) fields.push_back(SettingField::kExposureStep);
  if (!saved.gain) fields.push_back(SettingField::kGain);
  if (!saved.white_balance_auto) fields.push_back(SettingField::kWhiteBalanceAuto);
  if (!saved.white_balance_k) fields.push_back(SettingField::kWhiteBalanceK);
  if (!KnownFrameRate(saved.sdk_frame_rate)) fields.push_back(SettingField::kSdkFrameRate);
  if (!saved.locked) fields.push_back(SettingField::kLocked);
  return fields;
}

const char* NameOf(SettingField field) {
  switch (field) {
    case SettingField::kExposureAuto: return "exposure_auto";
    case SettingField::kExposureStep: return "exposure_step";
    case SettingField::kGain: return "gain";
    case SettingField::kWhiteBalanceAuto: return "white_balance_auto";
    case SettingField::kWhiteBalanceK: return "white_balance_k";
    case SettingField::kSdkFrameRate: return "sdk_frame_rate";
    case SettingField::kLocked: return "locked";
  }
  return "";
}

const char* NameOf(CommandKind kind) {
  switch (kind) {
    case CommandKind::kSdkFrameRate: return "sdk_frame_rate";
    case CommandKind::kUnlock: return "unlock";
    case CommandKind::kExposureAuto: return "exposure_auto";
    case CommandKind::kExposureManual: return "exposure_manual";
    case CommandKind::kGain: return "gain";
    case CommandKind::kWhiteBalanceManual: return "white_balance_manual";
    case CommandKind::kWhiteBalanceAuto: return "white_balance_auto";
    case CommandKind::kLock: return "lock";
  }
  return "";
}

}  // namespace hwlab::flow
