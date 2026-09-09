#pragma once
#include "CareerService.h"
namespace pw {
struct ResumeDraft {
    QString text;
    QStringList warnings;
    QJsonObject sources;
};
class ResumeBuilder {
  public:
    explicit ResumeBuilder(CareerService& service) : service_(service) {}
    QJsonArray availableSources() const;
    ResumeDraft build(const QJsonObject& job, const QStringList& selectedSources) const;
    static QString html(const QString& text, const QString& photoBase64 = {});
    static QString importPhoto(const QString& file);

  private:
    CareerService& service_;
};
} // namespace pw
