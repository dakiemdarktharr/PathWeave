#pragma once
#include <QJsonObject>
#include <QMap>
#include <QStringList>
namespace pw {
struct MatchScore {
    double total = 0, title = 0, skill = 0, employmentType = 0, workplaceMode = 0, location = 0,
           seniority = 0, salary = 0, freshness = 0, feedback = 0;
    QStringList explanations, missingPreferences;
    QJsonObject toJson() const;
};
class MatchingEngine {
  public:
    QMap<QString, double> weights = {{"title", 25},     {"skill", 25},    {"employment", 15},
                                     {"workplace", 15}, {"location", 10}, {"seniority", 5},
                                     {"salary", 5}};
    MatchScore score(const QJsonObject& job, const QJsonObject& profile) const;
    static QString synonyms(QString text);
    static bool containsTerm(const QString& text, const QString& term);
};
} // namespace pw
