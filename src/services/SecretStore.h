#pragma once
#include <QString>
namespace pw {
class SecretStore {
  public:
    explicit SecretStore(QString directory) : directory_(std::move(directory)) {}
    void put(const QString& key, const QString& value);
    QString get(const QString& key) const;
    void clear();

  private:
    QString directory_;
    QString file(const QString& key) const;
};
} // namespace pw
