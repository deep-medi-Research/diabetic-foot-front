#include "hwlab/platform/rgb_camera.h"

namespace hwlab::platform {

ReconcileResult Reconcile(RgbCamera& camera, const CameraState& target) {
  ReconcileResult result;
  for (const flow::SettingCommand& command : flow::PlanCommands(camera.ReadState(), target)) {
    (camera.Execute(command) ? result.executed : result.failed).push_back(command);
  }
  result.readback = camera.ReadState();
  result.mismatches = flow::FindMismatches(result.readback, target);
  return result;
}

ReconcileResult Restore(RgbCamera& camera, const CameraState& saved) {
  ReconcileResult result;
  for (const flow::SettingCommand& command : flow::PlanRestore(camera.ReadStoredState(), saved)) {
    (camera.Execute(command) ? result.executed : result.failed).push_back(command);
  }
  result.readback = camera.ReadStoredState();
  result.mismatches = flow::FindRestoreMismatches(result.readback, saved);
  result.unknown_saved = flow::FindUnknownSaved(saved);
  return result;
}

}  // namespace hwlab::platform
