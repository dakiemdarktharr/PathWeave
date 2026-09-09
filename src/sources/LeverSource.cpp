#include "Sources.h"
namespace pw {
LeverSource::LeverSource(NetworkService& n) : PublicSource("lever", "Lever", n) {}
QUrl LeverSource::endpoint(const SearchQuery&) const {
    return QUrl("https://api.lever.co/v0/postings/" + config_["identifier"].toString() + "?mode=json");
}
QJsonArray LeverSource::parse(const QByteArray& b) const {
    QJsonArray out;
    for (const auto& v : jsonJobs(b, {})) {
        auto r = v.toObject(), cat = r["categories"].toObject();
        out.append(QJsonObject{{"external_id", r["id"]},
                               {"canonical_url", r["hostedUrl"]},
                               {"title", r["text"]},
                               {"company", config_["company"].toString(config_["identifier"].toString())},
                               {"location", cat["location"]},
                               {"employment_type", employment(cat["commitment"].toString())},
                               {"workplace_mode", workplace(r["workplaceType"].toString())},
                               {"description", r["descriptionPlain"].toString().isEmpty()
                                                   ? plain(r["description"].toString())
                                                   : r["descriptionPlain"].toString()}});
    }
    return out;
}
} // namespace pw
