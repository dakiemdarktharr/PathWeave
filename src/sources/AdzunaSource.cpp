#include "Sources.h"
#include <QUrlQuery>
namespace pw {
AdzunaSource::AdzunaSource(NetworkService& n) : PublicSource("adzuna", "Adzuna", n) {}
QUrl AdzunaSource::endpoint(const SearchQuery& s) const {
    QString country = config_["country"].toString("gb").toLower();
    if (country.size() != 2)
        country = "gb";
    QUrl u("https://api.adzuna.com/v1/api/jobs/" + country + "/search/" +
           QString::number(std::max(1, s.page)));
    QUrlQuery q;
    q.addQueryItem("app_id", config_["app_id"].toString());
    q.addQueryItem("app_key", config_["app_key"].toString());
    q.addQueryItem("what", s.keyword);
    q.addQueryItem("where", s.location);
    q.addQueryItem("results_per_page", "50");
    if (s.employmentType == "full_time" || s.employmentType == "part_time")
        q.addQueryItem(s.employmentType, "1");
    u.setQuery(q);
    return u;
}
QJsonArray AdzunaSource::parse(const QByteArray& b) const {
    QJsonArray out;
    for (const auto& v : jsonJobs(b, "results")) {
        auto r = v.toObject();
        out.append(QJsonObject{{"external_id", r["id"].toVariant().toString()},
                               {"canonical_url", r["redirect_url"]},
                               {"title", plain(r["title"].toString())},
                               {"company", r["company"].toObject()["display_name"]},
                               {"location", r["location"].toObject()["display_name"]},
                               {"employment_type", employment(r["contract_time"].toString())},
                               {"workplace_mode", "unknown"},
                               {"description", plain(r["description"].toString())},
                               {"salary_min", r["salary_min"]},
                               {"salary_max", r["salary_max"]},
                               {"salary_period", "year"},
                               {"currency", config_["currency"].toString()},
                               {"posted_at", r["created"]}});
    }
    return out;
}
} // namespace pw
