#pragma once
#include "EntityEditor.h"
class QProgressBar;
namespace pw {
class SurveyDialog : public QDialog {
    Q_OBJECT
  public:
    explicit SurveyDialog(CareerService&, QWidget* parent = nullptr);
    FieldForm* form;
  public slots:
    void submit();
    void skip();

  private:
    CareerService& service_;
    QProgressBar* progress_;
    QLabel* error_;
};
} // namespace pw
