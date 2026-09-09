#include "DiscoveryPolicy.h"
#include "matching/MatchingEngine.h"
#include <QDateTime>
#include <QRegularExpression>
namespace pw {
QList<SearchQuery> DiscoveryPolicy::queries(const QJsonObject& p, const QString& keyword,
                                            const QString& location, const QString& employment, int pages) {
    QStringList titles;
    if (!keyword.trimmed().isEmpty())
        titles << keyword.trimmed();
    else
        titles = (p["desired_titles"].toString() + "," + p["alternative_titles"].toString())
                     .split(QRegularExpression("[,;\\n]"), Qt::SkipEmptyParts);
    if (titles.isEmpty())
        titles << "";
    QStringList unique;
    for (auto t : titles) {
        t = t.trimmed();
        if (!unique.contains(t, Qt::CaseInsensitive))
            unique << t;
    }
    QList<SearchQuery> out;
    for (auto t : unique.mid(0, 5))
        for (int page = 1; page <= std::clamp(pages, 1, 3); ++page)
            out << SearchQuery{t, location, employment, page};
    return out;
}
bool DiscoveryPolicy::visible(const QJsonObject& j, const QJsonObject& p, const QString& query,
                              const QString& type, const QString& mode, bool today, double minimum,
                              double score) {
    if (j["status"] == "dismissed" || j["status"] == "archived")
        return false;
    if (!type.isEmpty() && j["employment_type"] != type)
        return false;
    if (!mode.isEmpty() && j["workplace_mode"] != mode)
        return false;
    for (auto c : p["excluded_companies"].toString().split(QRegularExpression("[,;\\n]"), Qt::SkipEmptyParts))
        if (MatchingEngine::synonyms(c) == MatchingEngine::synonyms(j["company"].toString()))
            return false;
    QString text = j["title"].toString() + " " + j["company"].toString() + " " + j["description"].toString() +
                   " " + j["requirements"].toString() + " " + j["skills"].toString();
    for (auto term : query.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts))
        if (!MatchingEngine::containsTerm(text, term))
            return false;
    if (today) {
        auto d = QDateTime::fromString(j["posted_at"].toString(), Qt::ISODate);
        QDate date =
            d.isValid() ? d.toLocalTime().date() : QDate::fromString(j["posted_at"].toString(), Qt::ISODate);
        if (date != QDate::currentDate())
            return false;
    }
    return score >= minimum;
}
bool DiscoveryPolicy::due(const QJsonObject& search, const QDateTime& time) {
    if (!search["enabled"].toVariant().toBool())
        return false;
    auto next = QDateTime::fromString(search["next_run"].toString(), Qt::ISODate);
    return !next.isValid() || next <= time;
}
QString DiscoveryPolicy::nextRun(const QJsonObject& search, const QDateTime& time) {
    return time.addSecs(qint64(std::clamp(search["interval_hours"].toInt(24), 1, 720)) * 3600)
        .toUTC()
        .toString(Qt::ISODate);
}
} // namespace pw
