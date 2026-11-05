#include "remembered_values.h"

#include <QSettings>

QVariant RememberedValues::Value(const QString& key) const {
  if (!saved_) return this_run_.value(key);
  return QSettings().value(key);
}

void RememberedValues::SetValue(const QString& key, const QVariant& value) {
  if (!saved_) {
    this_run_.insert(key, value);
    return;
  }
  QSettings().setValue(key, value);
}
