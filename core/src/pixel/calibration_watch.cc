#include "hwlab/pixel/calibration_watch.h"

#include <cstddef>

namespace hwlab::pixel {

uint64_t FrameSignature(const FrameView& frame) {
  uint64_t hash = 1469598103934665603ull;
  if (frame.width <= 0 || frame.height <= 0 || frame.plane[0] == nullptr) return hash;
  const int32_t bytes_per_pixel = frame.format == PixelFormat::kY16 || frame.format == PixelFormat::kUyvy ? 2 : 1;
  const size_t row_bytes = static_cast<size_t>(frame.width) * static_cast<size_t>(bytes_per_pixel);
  for (int32_t y = 0; y < frame.height; ++y) {
    const uint8_t* row = frame.plane[0] + static_cast<ptrdiff_t>(y) * frame.row_stride[0];
    for (size_t i = 0; i < row_bytes; ++i) {
      hash ^= row[i];
      hash *= 1099511628211ull;
    }
  }
  return hash;
}

bool IsCalibrationFinished(const CalibrationWatch& watch) {
  return watch.phase == CalibrationPhase::kDone || watch.phase == CalibrationPhase::kNotSeen ||
         watch.phase == CalibrationPhase::kStuck;
}

CalibrationWatch AfterCalibrationFrame(CalibrationWatch watch, uint64_t signature, const CalibrationRule& rule) {
  if (IsCalibrationFinished(watch)) return watch;
  const bool same = watch.has_last && watch.last_signature == signature;
  watch.has_last = true;
  watch.last_signature = signature;
  ++watch.received;
  switch (watch.phase) {
    case CalibrationPhase::kWaitingFreeze:
      watch.repeats = same ? watch.repeats + 1 : 0;
      if (watch.repeats >= rule.freeze_repeats && rule.freeze_repeats > 0) {
        watch.phase = CalibrationPhase::kFrozen;
        watch.frozen_frames = watch.repeats + 1;
        watch.waited_frames = watch.received - watch.frozen_frames;
      } else if (watch.received >= rule.freeze_wait_frames) {
        watch.phase = CalibrationPhase::kNotSeen;
      }
      break;
    case CalibrationPhase::kFrozen:
      if (same) {
        ++watch.repeats;
        ++watch.frozen_frames;
        if (watch.frozen_frames > rule.freeze_max_frames) watch.phase = CalibrationPhase::kStuck;
        break;
      }
      // 반복이 끊긴 이 프레임이 교정 뒤 첫 새 프레임입니다.
      watch.repeats = 0;
      if (rule.discard_frames <= 0) {
        watch.phase = CalibrationPhase::kDone;
        break;
      }
      watch.discarded = 1;
      watch.phase = watch.discarded >= rule.discard_frames ? CalibrationPhase::kDone : CalibrationPhase::kDiscarding;
      break;
    case CalibrationPhase::kDiscarding:
      ++watch.discarded;
      if (watch.discarded >= rule.discard_frames) watch.phase = CalibrationPhase::kDone;
      break;
    case CalibrationPhase::kDone:
    case CalibrationPhase::kNotSeen:
    case CalibrationPhase::kStuck:
      break;
  }
  return watch;
}

}  // namespace hwlab::pixel
