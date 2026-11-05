#ifndef HWLAB_APP_READBACK_MODEL_H_
#define HWLAB_APP_READBACK_MODEL_H_

#include <QAbstractListModel>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <vector>

/**
 * 카메라에서 다시 읽은 설정값 목록입니다. 화면의 '다시 읽은 값' 표가 씁니다.
 *
 * 역할 이름(name, value, mismatch, target)이 QML 부품 ReadbackRow의 required property와 같아 Repeater가 행마다 채웁니다.
 */
class ReadbackModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("CaptureController가 만듭니다")

 public:
  struct Row {
    QString name;
    QString value;
    bool mismatch = false;
    QString target;

    bool operator==(const Row&) const = default;
  };

  enum Role { kName = Qt::UserRole + 1, kValue, kMismatch, kTarget };

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  /** 행을 바꿉니다. 행 수가 같으면 바뀐 행만 화면에 알립니다. */
  void Update(const std::vector<Row>& rows);

 private:
  std::vector<Row> rows_;
};

#endif  // HWLAB_APP_READBACK_MODEL_H_
