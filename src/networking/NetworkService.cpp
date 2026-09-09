#include "NetworkService.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QHostAddress>
#include <QTimer>
namespace pw {
NetworkService::NetworkService(Database& db, QObject* parent, QNetworkAccessManager* testManager)
    : QObject(parent), db_(db), manager_(this), activeManager_(testManager ? testManager : &manager_) {}
bool NetworkService::safeUrl(const QUrl& u) {
    if (u.scheme() != "https" || !u.isValid() || u.host().isEmpty() || !u.userInfo().isEmpty() ||
        (u.port() != -1 && u.port() != 443))
        return false;
    auto h = u.host().toLower();
    QHostAddress address(h);
    return h != "localhost" && !h.endsWith(".local") && !h.endsWith(".localhost") && address.isNull();
}
QString NetworkService::responseError(int status, QNetworkReply::NetworkError error, bool timedOut) {
    if (timedOut)
        return "Hết thời gian chờ; hãy thử lại sau.";
    if (status == 429)
        return "Nguồn giới hạn tần suất (429). Vui lòng chờ.";
    if (status == 401 || status == 403)
        return "Nguồn từ chối truy cập. Kiểm tra cấu hình; không vượt qua hạn chế.";
    if (status >= 300 && status < 400)
        return "Chuyển hướng bị chặn; hãy cấu hình URL HTTPS cuối cùng.";
    if (status >= 400)
        return "Nguồn tạm thời không khả dụng (HTTP " + QString::number(status) + ").";
    if (error == QNetworkReply::OperationCanceledError)
        return "Đã hủy yêu cầu.";
    if (error != QNetworkReply::NoError)
        return "Lỗi mạng hoặc TLS. Kiểm tra kết nối / chứng chỉ.";
    return {};
}
bool NetworkService::cacheValid(qint64 expiry, qint64 time) {
    return expiry > time;
}
void NetworkService::get(const QUrl& url, const QString& source, int interval, int ttl, Callback callback,
                         int timeoutMs, RedirectGuard guard) {
    auto fail = [this, callback](QString error) {
        QTimer::singleShot(0, this, [callback, error] { callback({{}, error}); });
    };
    if (db_.setting("online_enabled") != "true") {
        fail("Đang ngoại tuyến. Bật tìm việc trực tuyến trong Cài đặt.");
        return;
    }
    if (!safeUrl(url)) {
        fail("Chỉ cho phép URL HTTPS công khai, không chứa thông tin đăng nhập.");
        return;
    }
    QString key =
        QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex());
    qint64 time = QDateTime::currentSecsSinceEpoch();
    auto cached = db_.query("SELECT payload,expires_at FROM response_cache WHERE cache_key=?", {key});
    if (!cached.isEmpty() && cacheValid(cached[0].toObject()["expires_at"].toInteger(), time)) {
        auto body = QByteArray::fromBase64(cached[0].toObject()["payload"].toString().toLatin1());
        QTimer::singleShot(0, this, [callback, body] { callback({body, {}, 200, true}); });
        return;
    }
    qint64 persisted = db_.setting("rate_" + source, "0").toLongLong();
    if (std::max(nextAllowed_.value(source), persisted) > time) {
        fail("Nguồn giới hạn tần suất. Chờ đến lần đồng bộ tiếp theo.");
        return;
    }
    nextAllowed_[source] = time + interval;
    db_.setSetting("rate_" + source, QString::number(time + interval));
    request(url, source, interval, ttl, callback, QDateTime::currentMSecsSinceEpoch() + timeoutMs, key, 5,
            guard);
}
void NetworkService::request(const QUrl& url, const QString& source, int interval, int ttl, Callback callback,
                             qint64 deadline, const QString& key, int redirects, RedirectGuard guard) {
    auto timeoutMs = int(deadline - QDateTime::currentMSecsSinceEpoch());
    if (timeoutMs <= 0 || db_.setting("online_enabled") != "true") {
        QTimer::singleShot(0, this, [callback] { callback({{}, "Đã hủy hoặc hết thời gian chờ."}); });
        return;
    }
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "PathWeave/1.1 (local career organizer)");
    req.setRawHeader("Accept",
                     "application/json, application/rss+xml, application/atom+xml, text/html;q=0.8");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setTransferTimeout(timeoutMs);
    auto* reply = activeManager_->get(req);
    pending_ << reply;
    emit activity("Đang kết nối: " + source);
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [reply] {
        reply->setProperty("timed_out", true);
        reply->abort();
    });
    timer->start(timeoutMs);
    connect(reply, &QNetworkReply::readyRead, reply, [reply] {
        if (reply->bytesAvailable() > 8 * 1024 * 1024) {
            reply->setProperty("too_large", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [=, this] {
        timer->stop();
        pending_.removeAll(reply);
        Response r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r.status >= 300 && r.status < 400 && reply->error() == QNetworkReply::NoError &&
            !reply->property("timed_out").toBool()) {
            auto target = url.resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            bool approved = redirects > 0 && safeUrl(target) && target.host() == url.host() &&
                            target.port(443) == url.port(443) && (!guard || guard(target));
            reply->deleteLater();
            if (!approved) {
                callback({{},
                          "Chuyển hướng bị chặn: khác nguồn, không an toàn, robots hoặc quá 5 lần.",
                          r.status});
                return;
            }
            request(target, source, interval, ttl, callback, deadline, key, redirects - 1, guard);
            return;
        }
        r.error = responseError(r.status, reply->error(), reply->property("timed_out").toBool());
        if (reply->property("too_large").toBool())
            r.error = "Phản hồi vượt giới hạn 8 MiB.";
        if (r.status == 429) {
            bool ok = false;
            int retry = reply->rawHeader("Retry-After").toInt(&ok);
            nextAllowed_[source] = QDateTime::currentSecsSinceEpoch() +
                                   std::max(interval, ok ? std::clamp(retry, 60, 86400) : 300);
            db_.setSetting("rate_" + source, QString::number(nextAllowed_[source]));
        }
        if (r.error.isEmpty()) {
            r.body = reply->readAll();
            db_.execute(
                "INSERT INTO response_cache(cache_key,payload,expires_at) VALUES(?,?,?) ON "
                "CONFLICT(cache_key) DO UPDATE SET payload=excluded.payload,expires_at=excluded.expires_at",
                {key, QString::fromLatin1(r.body.toBase64()), QDateTime::currentSecsSinceEpoch() + ttl});
        }
        emit activity(r.error.isEmpty() ? "Đã nhận dữ liệu: " + source : r.error);
        reply->deleteLater();
        callback(r);
    });
}
void NetworkService::cancelAll() {
    auto copy = pending_;
    for (auto r : copy)
        if (r)
            r->abort();
}
void NetworkService::clearCache() {
    db_.execute("DELETE FROM response_cache");
}
} // namespace pw
