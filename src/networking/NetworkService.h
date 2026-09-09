#pragma once
#include "database/Database.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <functional>
namespace pw {
struct Response {
    QByteArray body;
    QString error;
    int status = 0;
    bool cached = false;
};
class NetworkService : public QObject {
    Q_OBJECT
  public:
    using Callback = std::function<void(Response)>;
    using RedirectGuard = std::function<bool(const QUrl&)>;
    explicit NetworkService(Database& db, QObject* parent = nullptr,
                            QNetworkAccessManager* testManager = nullptr);
    void get(const QUrl& url, const QString& source, int intervalSeconds, int cacheSeconds, Callback callback,
             int timeoutMs = 15000, RedirectGuard redirectGuard = {});
    void cancelAll();
    void clearCache();
    static bool safeUrl(const QUrl& url);
    static QString responseError(int status, QNetworkReply::NetworkError error, bool timedOut);
    static bool cacheValid(qint64 expiry, qint64 now);
  signals:
    void activity(const QString& message);

  private:
    void request(const QUrl& url, const QString& source, int interval, int ttl, Callback callback,
                 qint64 deadline, const QString& key, int redirects, RedirectGuard guard);
    Database& db_;
    QNetworkAccessManager manager_;
    QNetworkAccessManager* activeManager_;
    QList<QPointer<QNetworkReply>> pending_;
    QMap<QString, qint64> nextAllowed_;
};
} // namespace pw
