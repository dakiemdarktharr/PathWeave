#pragma once
#include "database/Database.h"
namespace pw {
class Reports {
  public:
    explicit Reports(Database& db) : db_(db) {}
    QJsonObject funnel() const;
    QString markdown(int type, const QString& month) const;
    QString html(int type, const QString& month) const;
    QJsonArray calendar() const;
    QJsonArray timeline() const;
    QString ics() const;
    QJsonObject cohort(const QString& month) const;

  private:
    Database& db_;
};
} // namespace pw
