#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QVariant>
namespace pw {
class Database {
  public:
    explicit Database(const QString& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    void migrate();
    QJsonArray query(const QString& sql, const QVariantList& args = {}) const;
    void execute(const QString& sql, const QVariantList& args = {}) const;
    qint64 save(const QString& table, QJsonObject data, qint64 id = 0);
    QJsonObject get(const QString& table, qint64 id) const;
    QJsonArray all(const QString& table) const;
    void remove(const QString& table, qint64 id);
    int count(const QString& table) const;
    QString setting(const QString& key, const QString& fallback = {}) const;
    void setSetting(const QString& key, const QString& value);
    QJsonArray search(const QString& text) const;
    QJsonObject exportAll() const;
    void importAll(const QJsonObject& data);
    void deleteAll();
    void removeDemo();
    void enableFileEncryption();
    bool fileEncryptionEnabled() const;
    void purgeDeleted();
    bool begin();
    bool commit();
    void rollback();
    QString path() const {
        return db_.databaseName();
    }

  private:
    QSqlDatabase db_;
    QString connection_;
};
QString now();
QString csv(const QJsonArray& rows);
} // namespace pw
