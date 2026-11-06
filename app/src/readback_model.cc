#include "readback_model.h"

#include <algorithm>

int ReadbackModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant ReadbackModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= static_cast<int>(rows_.size())) return {};
  const Row& r = rows_[static_cast<size_t>(index.row())];
  switch (role) {
    case kName: return r.name;
    case kValue: return r.value;
    case kMismatch: return r.mismatch;
    case kTarget: return r.target;
    default: return {};
  }
}

QHash<int, QByteArray> ReadbackModel::roleNames() const {
  return {{kName, "name"}, {kValue, "value"}, {kMismatch, "mismatch"}, {kTarget, "target"}};
}

bool ReadbackModel::anyMismatch() const {
  return std::any_of(rows_.begin(), rows_.end(), [](const Row& r) { return r.mismatch; });
}

void ReadbackModel::Update(const std::vector<Row>& rows) {
  const bool had_mismatch = anyMismatch();
  if (rows.size() != rows_.size()) {
    beginResetModel();
    rows_ = rows;
    endResetModel();
  } else {
    for (size_t i = 0; i < rows.size(); ++i) {
      if (rows[i] == rows_[i]) continue;
      rows_[i] = rows[i];
      const QModelIndex at = index(static_cast<int>(i));
      emit dataChanged(at, at);
    }
  }
  if (anyMismatch() != had_mismatch) emit anyMismatchChanged();
}
