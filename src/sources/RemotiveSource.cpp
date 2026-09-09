#include "Sources.h"
#include <QUrlQuery>
namespace pw {
RemotiveSource::RemotiveSource(NetworkService& n) : PublicSource("remotive", "Remotive", n) {}
QUrl RemotiveSource::endpoint(const SearchQuery& s) const {
    QUrl u("https://remotive.com/api/remote-jobs");
    QUrlQuery q;
    q.addQueryItem("search", s.keyword);
    q.addQueryItem("limit", "100");
    u.setQuery(q);
    return u;
}
QJsonArray RemotiveSource::parse(const QByteArray& b) const {
    QJsonArray out;
    for (const auto& v : jsonJobs(b, "jobs")) {
        auto r = v.toObject();
        QStringList tags;
        for (auto t : r["tags"].toArray())
            tags << t.toString();
        out.append(QJsonObject{{"external_id", r["id"].toVariant().toString()},
                               {"canonical_url", r["url"]},
                               {"title", r["title"]},
                               {"company", r["company_name"]},
                               {"location", r["candidate_required_location"]},
                               {"employment_type", employment(r["job_type"].toString())},
                               {"workplace_mode", "remote"},
                               {"description", plain(r["description"].toString()) +
                                                   "\nLương từ nguồn: " + r["salary"].toString()},
                               {"skills", tags.join(", ")},
                               {"posted_at", r["publication_date"]}});
    }
    return out;
}
} // namespace pw
