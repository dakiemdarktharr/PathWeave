#include "Sources.h"
#include "networking/RobotsPolicy.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTimer>
#include <stdexcept>
namespace pw {
QString plain(const QString& html) {
    QTextDocument d;
    d.setHtml(html);
    return d.toPlainText();
}
QString employment(QString s) {
    s = s.toLower().trimmed();
    s.replace('-', '_').replace(' ', '_');
    if (s == "fulltime")
        s = "full_time";
    if (s == "parttime")
        s = "part_time";
    if (s == "freelance")
        s = "contract";
    return QStringList{"full_time", "part_time", "contract", "internship", "temporary"}.contains(s)
               ? s
               : "unknown";
}
QString workplace(QString s) {
    s = s.toLower().trimmed();
    if (s == "telecommute")
        s = "remote";
    if (s == "on-site" || s == "onsite")
        s = "on_site";
    return QStringList{"remote", "hybrid", "on_site", "flexible"}.contains(s) ? s : "unknown";
}
QJsonValue atPath(QJsonValue v, const QString& path) {
    if (path.isEmpty())
        return v;
    for (const auto& p : path.split('.'))
        v = v.toObject().value(p);
    return v;
}
QJsonArray jsonJobs(const QByteArray& bytes, const QString& root) {
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError)
        throw std::runtime_error("Nguồn trả về JSON không hợp lệ.");
    auto v = atPath(doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object()), root);
    if (!v.isArray())
        throw std::runtime_error("Không tìm thấy danh sách JSON; kiểm tra ánh xạ root.");
    return v.toArray();
}
bool PublicSource::isConfigured() const {
    if (id_ == "remotive")
        return true;
    if (id_ == "adzuna")
        return !config_["app_id"].toString().isEmpty() && !config_["app_key"].toString().isEmpty();
    if (id_ == "greenhouse" || id_ == "lever")
        return QRegularExpression("^[A-Za-z0-9_-]+$").match(config_["identifier"].toString()).hasMatch();
    return NetworkService::safeUrl(QUrl(config_["url"].toString()));
}
void PublicSource::search(const SearchQuery& query, Callback callback) {
    if (!isConfigured()) {
        error_ = "Nguồn chưa được cấu hình.";
        QTimer::singleShot(0, &net_, [callback, e = error_] { callback({{}, e}); });
        return;
    }
    QUrl url = endpoint(query);
    auto request = [this, url, callback](NetworkService::RedirectGuard guard = {}) {
        net_.get(
            url, id_, id_ == "remotive" ? 21600 : 30, id_ == "remotive" ? 21600 : 1800,
            [this, callback](Response r) {
                error_ = r.error;
                SourceResult result;
                result.error = r.error;
                result.cached = r.cached;
                if (r.error.isEmpty()) {
                    try {
                        result.jobs = parse(r.body);
                        QString hash = QString::fromLatin1(
                            QCryptographicHash::hash(r.body, QCryptographicHash::Sha256).toHex());
                        for (int i = 0; i < result.jobs.size(); ++i) {
                            auto j = result.jobs[i].toObject();
                            j["source_id"] = id_;
                            j["raw_payload_hash"] = hash;
                            j["fetched_at"] = now();
                            result.jobs[i] = j;
                        }
                        lastJobs_ = result.jobs;
                        lastSync_ = now();
                    } catch (const std::exception& e) {
                        result.error = QString::fromUtf8(e.what());
                        error_ = result.error;
                    }
                }
                callback(result);
            },
            15000, guard);
    };
    // Only known public APIs are fetched directly. Arbitrary pages/feeds use a
    // RFC 9309 robots policy, rechecked on each permitted same-origin redirect.
    if (id_ == "url" || id_ == "rss" || id_ == "json") {
        QUrl robots = url;
        robots.setPath("/robots.txt");
        robots.setQuery({});
        robots.setFragment({});
        net_.get(robots, id_ + "_robots", 1, 86400, [this, url, request, callback](Response r) {
            if (!r.error.isEmpty() && r.status != 404) {
                callback({{}, "Không xác minh được robots.txt; chỉ lưu thủ công."});
                return;
            }
            RobotsPolicy policy(r.status == 404 ? QByteArray() : r.body);
            bool blocked = !policy.allows(url);
            if (blocked) {
                error_ = "robots.txt có hạn chế; dùng Mở trong trình duyệt / Lưu thủ công.";
                callback({{}, error_});
                return;
            }
            request([policy](const QUrl& target) { return policy.allows(target); });
        });
    } else
        request();
}
void PublicSource::fetchJobDetails(const QString& id, Callback cb) {
    for (const auto& v : lastJobs_)
        if (v.toObject()["external_id"].toVariant().toString() == id) {
            cb({QJsonArray{v}, {}, true});
            return;
        }
    cb({{}, "Hãy tìm kiếm trước để lấy chi tiết từ nguồn."});
}
std::unique_ptr<IJobSource> makeSource(const QString& id, NetworkService& n) {
    if (id == "adzuna")
        return std::make_unique<AdzunaSource>(n);
    if (id == "remotive")
        return std::make_unique<RemotiveSource>(n);
    if (id == "greenhouse")
        return std::make_unique<GreenhouseSource>(n);
    if (id == "lever")
        return std::make_unique<LeverSource>(n);
    if (id == "json")
        return std::make_unique<GenericJsonSource>(n);
    if (id == "rss")
        return std::make_unique<GenericRssSource>(n);
    if (id == "url")
        return std::make_unique<UserProvidedUrlSource>(n);
    throw std::runtime_error("Unsupported job source");
}
} // namespace pw
