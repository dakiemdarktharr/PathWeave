#pragma once
#include "IJobSource.h"
#include <memory>
namespace pw {
class PublicSource : public IJobSource {
  public:
    PublicSource(QString id, QString name, NetworkService& net)
        : id_(std::move(id)), name_(std::move(name)), net_(net) {}
    QString sourceId() const override {
        return id_;
    }
    QString displayName() const override {
        return name_;
    }
    QStringList capabilities() const override {
        return {"public_read", "search", "details_from_feed", "manual_sync"};
    }
    bool isConfigured() const override;
    void configure(const QJsonObject& c) override {
        config_ = c;
    }
    void search(const SearchQuery&, Callback) override;
    void fetchJobDetails(const QString&, Callback) override;
    void healthCheck(Callback callback) override {
        search({}, std::move(callback));
    }
    QString lastSync() const override {
        return lastSync_;
    }
    QString errorMessage() const override {
        return error_;
    }
    virtual QUrl endpoint(const SearchQuery&) const = 0;
    virtual QJsonArray parse(const QByteArray&) const = 0;

  protected:
    QString id_, name_, lastSync_, error_;
    NetworkService& net_;
    QJsonObject config_;
    QJsonArray lastJobs_;
};
#define PW_SOURCE(Class)                                                                                     \
    class Class : public PublicSource {                                                                      \
      public:                                                                                                \
        explicit Class(NetworkService&);                                                                     \
        QUrl endpoint(const SearchQuery&) const override;                                                    \
        QJsonArray parse(const QByteArray&) const override;                                                  \
    };
PW_SOURCE(AdzunaSource)
PW_SOURCE(RemotiveSource)
PW_SOURCE(GreenhouseSource)
PW_SOURCE(LeverSource)
PW_SOURCE(GenericJsonSource)
PW_SOURCE(GenericRssSource)
PW_SOURCE(UserProvidedUrlSource)
#undef PW_SOURCE
std::unique_ptr<IJobSource> makeSource(const QString&, NetworkService&);
QJsonArray jsonJobs(const QByteArray& bytes, const QString& root);
QString plain(const QString& html);
QString employment(QString value);
QString workplace(QString value);
QJsonValue atPath(QJsonValue value, const QString& path);
} // namespace pw
