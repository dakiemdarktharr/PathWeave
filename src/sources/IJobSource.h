#pragma once
#include "networking/NetworkService.h"
#include <QJsonArray>
namespace pw {
struct SearchQuery {
    QString keyword, location, employmentType;
    int page = 1;
};
struct SourceResult {
    QJsonArray jobs;
    QString error;
    bool cached = false;
};
class IJobSource {
  public:
    using Callback = std::function<void(SourceResult)>;
    virtual ~IJobSource() = default;
    virtual QString sourceId() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList capabilities() const = 0;
    virtual bool isConfigured() const = 0;
    virtual void configure(const QJsonObject& config) = 0;
    virtual void search(const SearchQuery&, Callback) = 0;
    virtual void fetchJobDetails(const QString& id, Callback) = 0;
    virtual void healthCheck(Callback) = 0;
    virtual QString lastSync() const = 0;
    virtual QString errorMessage() const = 0;
};
} // namespace pw
