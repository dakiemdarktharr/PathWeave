#include "Sources.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <stdexcept>
namespace pw {
UserProvidedUrlSource::UserProvidedUrlSource(NetworkService& n)
    : PublicSource("url", "URL do bạn cung cấp", n) {}
QUrl UserProvidedUrlSource::endpoint(const SearchQuery&) const {
    return QUrl(config_["url"].toString());
}
QJsonArray UserProvidedUrlSource::parse(const QByteArray& bytes) const {
    QString html = QString::fromUtf8(bytes);
    if (html.contains("captcha", Qt::CaseInsensitive) ||
        html.contains("verify you are human", Qt::CaseInsensitive))
        throw std::runtime_error("Anti-bot page detected; use manual save.");
    QRegularExpression re(
        "<script[^>]*type\\s*=\\s*[\"']application/ld\\+json[\"'][^>]*>([\\s\\S]*?)</script>",
        QRegularExpression::CaseInsensitiveOption);
    auto matches = re.globalMatch(html);
    QJsonArray nodes;
    while (matches.hasNext()) {
        auto d = QJsonDocument::fromJson(matches.next().captured(1).toUtf8());
        if (d.isArray())
            for (auto v : d.array())
                nodes.append(v);
        else if (d.isObject()) {
            nodes.append(d.object());
            for (auto v : d.object()["@graph"].toArray())
                nodes.append(v);
        }
    }
    QJsonArray out;
    for (auto v : nodes) {
        auto o = v.toObject();
        if (o["@type"] != "JobPosting")
            continue;
        auto loc = o["jobLocation"].toObject()["address"].toObject();
        out.append(QJsonObject{
            {"title", o["title"]},
            {"company", o["hiringOrganization"].toObject()["name"]},
            {"description", plain(o["description"].toString())},
            {"canonical_url", config_["url"]},
            {"location", loc["addressLocality"].toString() + ", " + loc["addressCountry"].toString()},
            {"employment_type", employment(o["employmentType"].toString())},
            {"workplace_mode",
             o["jobLocationType"].toString().compare("TELECOMMUTE", Qt::CaseInsensitive) == 0 ? "remote"
                                                                                              : "unknown"},
            {"posted_at", o["datePosted"]}});
    }
    if (out.isEmpty())
        throw std::runtime_error("Không có JobPosting JSON-LD. Mở trình duyệt và lưu thủ công.");
    return out;
}
} // namespace pw
