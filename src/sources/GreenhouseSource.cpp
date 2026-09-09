#include "Sources.h"
namespace pw {
GreenhouseSource::GreenhouseSource(NetworkService& n) : PublicSource("greenhouse", "Greenhouse", n) {}
QUrl GreenhouseSource::endpoint(const SearchQuery&) const {
    return QUrl("https://boards-api.greenhouse.io/v1/boards/" + config_["identifier"].toString() +
                "/jobs?content=true");
}
QJsonArray GreenhouseSource::parse(const QByteArray& b) const {
    QJsonArray out;
    for (const auto& v : jsonJobs(b, "jobs")) {
        auto r = v.toObject();
        out.append(QJsonObject{{"external_id", r["id"].toVariant().toString()},
                               {"canonical_url", r["absolute_url"]},
                               {"title", r["title"]},
                               {"company", config_["company"].toString(config_["identifier"].toString())},
                               {"location", r["location"].toObject()["name"]},
                               {"employment_type", "unknown"},
                               {"workplace_mode", "unknown"},
                               {"description", plain(r["content"].toString())},
                               {"updated_at_source", r["updated_at"]}});
    }
    return out;
}
} // namespace pw
