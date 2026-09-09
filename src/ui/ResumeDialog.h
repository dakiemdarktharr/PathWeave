#pragma once
#include "services/ResumeBuilder.h"
#include <QDialog>
class QPlainTextEdit;
class QTextBrowser;
class QComboBox;
class QCheckBox;
class QListWidget;
class QLabel;
class QLineEdit;
namespace pw {
class ResumeDialog : public QDialog {
    Q_OBJECT
  public:
    ResumeDialog(CareerService& service, qint64 jobId = 0, qint64 applicationId = 0, qint64 draftId = 0,
                 QWidget* parent = nullptr);
    void generate();
    void setPhotoFromFile(const QString& path);
    qint64 saveDraft();
    QByteArray exportBytes(const QString& format) const;
    void reject() override;

  private:
    CareerService& service_;
    QComboBox *jobs_, *applications_;
    QLineEdit *title_, *name_;
    QPlainTextEdit *jd_, *text_;
    QListWidget* evidence_;
    QTextBrowser* preview_;
    QLabel* diagnostics_;
    QCheckBox* reviewed_;
    QString photo_;
    QByteArray savedFingerprint_;
    QByteArray fingerprint() const;
    QJsonObject provenance_;
    QJsonObject jobSnapshot() const;
    void updatePreview();
    void attach();
};
} // namespace pw
