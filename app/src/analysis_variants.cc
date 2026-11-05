#include "analysis_variants.h"

#include <QQmlEngine>
#include <QStringList>
#include <QVariant>

#include <utility>

#include "remembered_values.h"

namespace {

// 녹화 중 미리 분석 스위치와 판별 사용 여부를 기억하는 이름입니다. 미리 분석 이름은 판을 나누기 전과 같습니다.
constexpr char kPrestreamKey[] = "analysis/prestream";
QString EnabledKey(const QString& id) { return QStringLiteral("analysis/variant/%1/enabled").arg(id); }

}  // namespace

std::vector<bool> VariantHolds(bool recording, const std::vector<bool>& enabled, const std::vector<bool>& active) {
  std::vector<bool> holds(enabled.size(), recording);
  bool earlier_active = false;
  for (size_t i = 0; i < enabled.size(); ++i) {
    if (!recording) holds[i] = earlier_active;
    if (enabled[i] && i < active.size() && active[i]) earlier_active = true;
  }
  return holds;
}

AnalysisVariants::AnalysisVariants(std::vector<std::unique_ptr<AnalysisController>> lanes, bool prestream_available,
                                   QObject* parent)
    : QObject(parent), lanes_(std::move(lanes)), prestream_available_(prestream_available) {
  for (const auto& lane : lanes_) {
    // 부모 없는 객체를 Q_INVOKABLE(lane)로 QML에 넘기면 QML이 소유해 지웁니다. 이 객체가 가지므로 C++ 소유로 고정합니다.
    QQmlEngine::setObjectOwnership(lane.get(), QQmlEngine::CppOwnership);
    lane->SetPrestream(prestream_available_ && prestream_);
    connect(lane.get(), &AnalysisController::changed, this, &AnalysisVariants::OnLaneChanged);
    connect(lane.get(), &AnalysisController::enabledChanged, this, &AnalysisVariants::changed);
    const QString id = lane->variantId();
    connect(lane.get(), &AnalysisController::finished, this,
            [this, id](const QString& folder, bool ok, const QString& path) { emit finished(folder, ok, path, id); });
    connect(lane.get(), &AnalysisController::figuresReady, this,
            [this, id](const QString& folder) { emit figuresReady(folder, id); });
    connect(lane.get(), &AnalysisController::collected, this,
            [this, label = lane->variantLabel()](const QString& folder, bool, const QString& text, const QString&) {
              if (folder != collect_folder_) {
                collect_folder_ = folder;
                collect_texts_.clear();
              }
              collect_texts_[label] = text;
              QStringList parts;
              for (auto it = collect_texts_.cbegin(); it != collect_texts_.cend(); ++it) {
                parts.append(it.key() + QStringLiteral(": ") + it.value());
              }
              emit collected(folder, parts.join(QStringLiteral("\n")));
            });
  }
}

AnalysisVariants::~AnalysisVariants() {
  // 판의 신호가 이 객체의 람다를 부르므로, 판을 먼저 지웁니다.
  for (auto& lane : lanes_) lane->disconnect(this);
  lanes_.clear();
}

void AnalysisVariants::UseRemembered(RememberedValues* remembered) {
  remembered_ = remembered;
  if (remembered_ == nullptr) return;
  const QVariant prestream = remembered_->Value(QString::fromLatin1(kPrestreamKey));
  prestream_ = prestream.isValid() ? prestream.toBool() : true;
  for (const auto& lane : lanes_) {
    lane->SetPrestream(prestream_available_ && prestream_);
    const QVariant on = remembered_->Value(EnabledKey(lane->variantId()));
    lane->SetEnabled(!on.isValid() || on.toBool());
  }
  // 기억한 값이 모든 판을 끄고 있으면(판 목록이 바뀐 경우) 첫 판을 켭니다.
  if (enabledCount() == 0 && !lanes_.empty()) lanes_.front()->SetEnabled(true);
  emit prestreamChanged();
  emit changed();
}

void AnalysisVariants::Start() {
  for (const auto& lane : lanes_) lane->Start();
  UpdateHolds();
}

void AnalysisVariants::Shutdown() {
  for (const auto& lane : lanes_) lane->Shutdown();
}

QList<QObject*> AnalysisVariants::lanes() const {
  QList<QObject*> out;
  for (const auto& lane : lanes_) out.append(lane.get());
  return out;
}

bool AnalysisVariants::blocksShot() const {
  for (const auto& lane : lanes_) {
    if (lane->enabled() && (lane->state() == QStringLiteral("busy") || lane->queued() > 0)) return true;
  }
  return false;
}

QString AnalysisVariants::remainingText() const {
  for (const auto& lane : lanes_) {
    if (lane->state() == QStringLiteral("busy")) {
      const QString left = lane->remainingText();
      return left.isEmpty() ? lane->variantLabel() + QStringLiteral(" 분석 중") : lane->variantLabel() + QStringLiteral(" ") + left;
    }
  }
  return {};
}

int AnalysisVariants::enabledCount() const {
  int n = 0;
  for (const auto& lane : lanes_) n += lane->enabled() ? 1 : 0;
  return n;
}

void AnalysisVariants::setPrestream(bool on) {
  if (!prestream_available_ || on == prestream_) return;
  prestream_ = on;
  for (const auto& lane : lanes_) lane->SetPrestream(on);
  if (remembered_ != nullptr) remembered_->SetValue(QString::fromLatin1(kPrestreamKey), on);
  emit prestreamChanged();
}

bool AnalysisVariants::setLaneEnabled(const QString& id, bool on) {
  for (const auto& lane : lanes_) {
    if (lane->variantId() != id || lane->enabled() == on) continue;
    if (!on && enabledCount() <= 1) return false;
    lane->SetEnabled(on);
    if (remembered_ != nullptr) remembered_->SetValue(EnabledKey(id), on);
    UpdateHolds();
    return true;
  }
  return false;
}

QObject* AnalysisVariants::lane(const QString& id) const {
  for (const auto& lane : lanes_) {
    if (lane->variantId() == id) return lane.get();
  }
  return nullptr;
}

QStringList AnalysisVariants::Ids() const {
  QStringList ids;
  for (const auto& lane : lanes_) ids.append(lane->variantId());
  return ids;
}

QString AnalysisVariants::LabelOf(const QString& id) const {
  for (const auto& lane : lanes_) {
    if (lane->variantId() == id) return lane->variantLabel();
  }
  return id;
}

bool AnalysisVariants::Finished(const QString& folder) const {
  bool any = false;
  for (const auto& lane : lanes_) {
    if (!lane->enabled()) continue;
    if (!lane->Finished(folder)) return false;
    any = true;
  }
  return any;
}

void AnalysisVariants::setHold(bool hold) {
  recording_ = hold;
  UpdateHolds();
}

void AnalysisVariants::onRgbSaved(const QString& folder, bool ready) {
  // 뒤쪽 판이 앞 판보다 먼저 요청을 보내지 않도록, 넣기 전에 보류를 겁니다.
  if (ready) UpdateHolds(true);
  for (const auto& lane : lanes_) lane->onRgbSaved(folder, ready);
  UpdateHolds();
}

void AnalysisVariants::onShotSaved(const QString& folder, bool ready) {
  if (ready) UpdateHolds(true);
  for (const auto& lane : lanes_) lane->onShotSaved(folder, ready);
  UpdateHolds();
}

void AnalysisVariants::onFirstFramesWritten(const QString& left_video, const QString& right_video) {
  for (const auto& lane : lanes_) lane->onFirstFramesWritten(left_video, right_video);
}

void AnalysisVariants::UpdateHolds(bool pending) {
  std::vector<bool> enabled;
  std::vector<bool> active;
  for (const auto& lane : lanes_) {
    enabled.push_back(lane->enabled());
    active.push_back(pending || lane->Active());
  }
  const std::vector<bool> holds = VariantHolds(recording_, enabled, active);
  QString earlier;  // 이 판보다 앞에서 분석 중이거나 대기 중인 판
  for (size_t i = 0; i < lanes_.size(); ++i) {
    lanes_[i]->SetHoldReason(recording_ ? QString() : earlier);
    lanes_[i]->setHold(holds[i]);
    if (earlier.isEmpty() && enabled[i] && active[i]) earlier = lanes_[i]->variantLabel();
  }
}

void AnalysisVariants::OnLaneChanged() {
  UpdateHolds();
  emit changed();
}
