#pragma once
#include "sources/IJobSource.h"
#include <QDateTime>
namespace pw {
class DiscoveryPolicy {
  public:
    static QList<SearchQuery> queries(const QJsonObject& profile, const QString& keyword,
                                      const QString& location, const QString& employment, int pages = 1);
    static bool visible(const QJsonObject& job, const QJsonObject& profile, const QString& query,
                        const QString& type, const QString& mode, bool today, double minimumScore,
                        double score);
    static bool due(const QJsonObject& search, const QDateTime& now);
    static QString nextRun(const QJsonObject& search, const QDateTime& now);
};
} // namespace pw
