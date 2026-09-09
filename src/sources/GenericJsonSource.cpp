#include "Sources.h"
namespace pw {
GenericJsonSource::GenericJsonSource(NetworkService& n) : PublicSource("json", "JSON công khai", n) {}
QUrl GenericJsonSource::endpoint(const SearchQuery&) const {
    return QUrl(config_["url"].toString());
}
QJsonArray GenericJsonSource::parse(const QByteArray& b) const {
    QJsonArray out;
    auto map = config_["mapping"].toObject();
    for (const auto& v : jsonJobs(b, config_["root"].toString())) {
        QJsonObject j;
        for (auto field : {"external_id", "canonical_url", "title", "company", "location", "employment_type",
                           "workplace_mode", "salary_min", "salary_max", "currency", "salary_period",
                           "description", "requirements", "skills", "posted_at"})
            j[field] = atPath(v, map[field].toString(field));
        j["employment_type"] = employment(j["employment_type"].toString());
        j["workplace_mode"] = workplace(j["workplace_mode"].toString());
        j["description"] = plain(j["description"].toString());
        out.append(j);
    }
    return out;
}
} // namespace pw
