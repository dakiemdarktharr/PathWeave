#pragma once
#include "domain/Schema.h"
#include "services/CareerService.h"
#include <QDialog>
#include <QMap>
class QFormLayout;
class QLabel;
namespace pw {
class FieldForm : public QWidget {
    Q_OBJECT
  public:
    FieldForm(const QVector<Field>& fields, Database* db, const QJsonObject& data = {},
              QWidget* parent = nullptr);
    QJsonObject values() const;
    void setValues(const QJsonObject&);
  signals:
    void changed();

  private:
    QVector<Field> fields_;
    QMap<QString, QWidget*> widgets_;
};
class EntityEditor : public QDialog {
    Q_OBJECT
  public:
    EntityEditor(CareerService& service, QString table, qint64 id = 0, QWidget* parent = nullptr,
                 QJsonObject initial = {});
    qint64 savedId = 0;
    QString errorMessage;
    FieldForm* form;
  public slots:
    void submit();

  private:
    CareerService& service_;
    QString table_;
    qint64 id_;
    QLabel* error_;
};
} // namespace pw
