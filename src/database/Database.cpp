#include "Database.h"
#include "domain/Schema.h"
#include <QDateTime>
#include <QDebug>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <stdexcept>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace pw {
QString now() {
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}
static void fail(const QString& error) {
    qWarning().noquote() << error;
    throw std::runtime_error(error.toStdString());
}
Database::Database(const QString& path) : connection_(QUuid::createUuid().toString()) {
    if (path != ":memory:")
        QDir().mkpath(QFileInfo(path).absolutePath());
    db_ = QSqlDatabase::addDatabase("QSQLITE", connection_);
    db_.setDatabaseName(path);
    if (!db_.open())
        fail(db_.lastError().text());
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA busy_timeout=5000");
    execute("PRAGMA secure_delete=ON");
    execute("PRAGMA journal_mode=WAL");
    migrate();
}
Database::~Database() {
    db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_);
}
QJsonArray Database::query(const QString& sql, const QVariantList& args) const {
    QSqlQuery q(db_);
    if (!q.prepare(sql))
        fail(q.lastError().text());
    for (const auto& a : args)
        q.addBindValue(a);
    if (!q.exec())
        fail(q.lastError().text());
    QJsonArray out;
    auto r = q.record();
    while (q.next()) {
        QJsonObject obj;
        for (int i = 0; i < r.count(); ++i)
            obj[r.fieldName(i)] = q.isNull(i) ? QJsonValue() : QJsonValue::fromVariant(q.value(i));
        out.append(obj);
    }
    return out;
}
void Database::execute(const QString& sql, const QVariantList& args) const {
    query(sql, args);
}
bool Database::begin() {
    return db_.transaction();
}
bool Database::commit() {
    bool ok = db_.commit();
    if (!ok)
        qWarning() << db_.lastError().text();
    return ok;
}
void Database::rollback() {
    db_.rollback();
}
void Database::migrate() {
    execute("CREATE TABLE IF NOT EXISTS schema_migrations(version INTEGER PRIMARY KEY, applied_at TEXT NOT "
            "NULL)");
    int version =
        query("SELECT COALESCE(MAX(version),0) AS v FROM schema_migrations")[0].toObject()["v"].toInt();
    if (version > 2)
        fail("Database version is newer than this application.");
    if (version == 2)
        return;
    if (!begin())
        fail("Cannot start migration");
    try {
        for (const auto& e : schema()) {
            QStringList columns = {"id INTEGER PRIMARY KEY AUTOINCREMENT"};
            for (const auto& f : e.fields) {
                QString type = (f.type == "INTEGER" || f.type == "BOOL") ? "INTEGER"
                               : f.type == "REAL"                        ? "REAL"
                                                                         : "TEXT";
                QString c = '"' + f.name + "\" " + type;
                if (f.required)
                    c += " NOT NULL CHECK(length(trim(\"" + f.name + "\"))>0)";
                if (!f.options.isEmpty() && f.type != "MULTI") {
                    QStringList options;
                    for (auto v : f.options.split('|'))
                        options << "'" + v + "'";
                    c += " CHECK(\"" + f.name + "\" IS NULL OR \"" + f.name + "\" IN (" + options.join(',') +
                         "))";
                }
                if (!f.reference.isEmpty())
                    c += " REFERENCES \"" + f.reference +
                         "\"(id) ON DELETE SET NULL DEFERRABLE INITIALLY DEFERRED";
                columns << c;
            }
            columns << "created_at TEXT NOT NULL" << "updated_at TEXT NOT NULL" << "deleted_at TEXT"
                    << "is_demo INTEGER NOT NULL DEFAULT 0";
            execute("CREATE TABLE IF NOT EXISTS \"" + e.name + "\" (" + columns.join(',') + ")");
            if (version == 1) {
                QStringList existing;
                for (auto v : query("PRAGMA table_info(\"" + e.name + "\")"))
                    existing << v.toObject()["name"].toString();
                for (const auto& f : e.fields)
                    if (!existing.contains(f.name)) {
                        QString type = (f.type == "INTEGER" || f.type == "BOOL") ? "INTEGER"
                                       : f.type == "REAL"                        ? "REAL"
                                                                                 : "TEXT";
                        QString reference = f.reference.isEmpty() ? QString()
                                                                  : " REFERENCES \"" + f.reference +
                                                                        "\"(id) ON DELETE SET NULL";
                        execute("ALTER TABLE \"" + e.name + "\" ADD COLUMN \"" + f.name + "\" " + type +
                                reference);
                    }
            }
            for (const auto& f : e.fields)
                if (!f.reference.isEmpty() || f.name == "status" || f.type == "DATE" || f.type == "DATETIME")
                    execute("CREATE INDEX IF NOT EXISTS idx_" + e.name + "_" + f.name + " ON \"" + e.name +
                            "\"(\"" + f.name + "\")");
        }
        execute("CREATE UNIQUE INDEX IF NOT EXISTS job_url_unique ON job_listings(canonical_url) WHERE "
                "canonical_url IS "
                "NOT NULL AND canonical_url<>'' AND deleted_at IS NULL");
        execute("CREATE UNIQUE INDEX IF NOT EXISTS job_external_unique ON "
                "job_listings(source_id,external_id) WHERE "
                "external_id IS NOT NULL AND external_id<>'' AND deleted_at IS NULL");
        execute("CREATE UNIQUE INDEX IF NOT EXISTS job_key_unique ON job_listings(normalized_key) WHERE "
                "normalized_key IS "
                "NOT NULL AND normalized_key<>'' AND deleted_at IS NULL");
        execute("CREATE UNIQUE INDEX IF NOT EXISTS settings_unique ON settings(key)");
        execute("CREATE UNIQUE INDEX IF NOT EXISTS source_unique ON job_sources(source_id) WHERE deleted_at "
                "IS NULL");
        execute("CREATE UNIQUE INDEX IF NOT EXISTS saved_job_unique ON saved_jobs(job_listing_id) WHERE "
                "deleted_at IS NULL");
        execute("CREATE UNIQUE INDEX IF NOT EXISTS evidence_link_unique ON "
                "application_evidence(application_id,evidence_id) WHERE deleted_at IS NULL");
        execute("CREATE VIRTUAL TABLE IF NOT EXISTS search_index USING fts5(entity UNINDEXED, entity_id "
                "UNINDEXED, "
                "content, tokenize='unicode61 remove_diacritics 2')");
        for (auto name : {"job_listings", "achievements", "projects", "work_logs", "tasks", "applications",
                          "contacts", "career_evidence"}) {
            const auto& e = entity(name);
            QStringList parts;
            for (const auto& f : e.fields)
                if (f.type == "TEXT" || f.type == "LONG")
                    parts << "COALESCE(new.\"" + f.name + "\",'')";
            QString content = parts.join(" || ' ' || ");
            QString insert = "INSERT INTO search_index(entity,entity_id,content) SELECT '" + e.name +
                             "',new.id," + content + " WHERE new.deleted_at IS NULL;";
            execute("CREATE TRIGGER IF NOT EXISTS fts_" + e.name + "_insert AFTER INSERT ON " + e.name +
                    " BEGIN " + insert + " END");
            execute("CREATE TRIGGER IF NOT EXISTS fts_" + e.name + "_update AFTER UPDATE ON " + e.name +
                    " BEGIN DELETE FROM search_index WHERE entity='" + e.name + "' AND entity_id=old.id; " +
                    insert + " END");
            execute("CREATE TRIGGER IF NOT EXISTS fts_" + e.name + "_delete AFTER DELETE ON " + e.name +
                    " BEGIN DELETE FROM search_index WHERE entity='" + e.name +
                    "' AND entity_id=old.id; END");
        }
        execute("CREATE TABLE IF NOT EXISTS response_cache(cache_key TEXT PRIMARY KEY, payload BLOB NOT "
                "NULL, expires_at "
                "INTEGER NOT NULL)");
        if (version == 1)
            for (auto v : query("SELECT * FROM job_listings")) {
                auto j = v.toObject();
                execute("UPDATE job_listings SET normalized_key=? WHERE id=?",
                        {jobIdentityKey(j), j["id"].toInteger()});
            }
        execute("INSERT INTO schema_migrations(version,applied_at) VALUES(?,?)", {2, now()});
        if (!commit())
            fail("Migration commit failed");
    } catch (...) {
        rollback();
        throw;
    }
}
qint64 Database::save(const QString& table, QJsonObject data, qint64 id) {
    const auto& e = entity(table);
    QStringList allowed = {"created_at", "updated_at", "deleted_at", "is_demo"};
    for (const auto& f : e.fields)
        allowed << f.name;
    QStringList names, marks, sets;
    QVariantList args;
    if (!id && !data.contains("created_at"))
        data["created_at"] = now();
    data["updated_at"] = now();
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (it.key() == "id")
            continue;
        if (!allowed.contains(it.key()))
            fail("Unknown field: " + it.key());
        names << '"' + it.key() + '"';
        marks << "?";
        sets << '"' + it.key() + "\"=?";
        auto v = it.value();
        if (v.isObject() || v.isArray())
            args << QString::fromUtf8(v.isObject()
                                          ? QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact)
                                          : QJsonDocument(v.toArray()).toJson(QJsonDocument::Compact));
        else
            args << v.toVariant();
    }
    if (id) {
        args << id;
        execute("UPDATE \"" + table + "\" SET " + sets.join(',') + " WHERE id=?", args);
        return id;
    }
    QSqlQuery q(db_);
    q.prepare("INSERT INTO \"" + table + "\" (" + names.join(',') + ") VALUES(" + marks.join(',') + ")");
    for (const auto& a : args)
        q.addBindValue(a);
    if (!q.exec())
        fail(q.lastError().text());
    return q.lastInsertId().toLongLong();
}
QJsonObject Database::get(const QString& table, qint64 id) const {
    entity(table);
    auto a = query("SELECT * FROM \"" + table + "\" WHERE id=? AND deleted_at IS NULL", {id});
    return a.isEmpty() ? QJsonObject() : a[0].toObject();
}
QJsonArray Database::all(const QString& table) const {
    entity(table);
    return query("SELECT * FROM \"" + table + "\" WHERE deleted_at IS NULL ORDER BY id DESC");
}
void Database::remove(const QString& table, qint64 id) {
    save(table, {{"deleted_at", now()}}, id);
}
int Database::count(const QString& table) const {
    return all(table).size();
}
QString Database::setting(const QString& key, const QString& fallback) const {
    auto a = query("SELECT value FROM settings WHERE key=? AND deleted_at IS NULL", {key});
    return a.isEmpty() ? fallback : a[0].toObject()["value"].toString();
}
void Database::setSetting(const QString& key, const QString& value) {
    execute("INSERT INTO settings(key,value,created_at,updated_at) VALUES(?,?,?,?) ON CONFLICT(key) DO "
            "UPDATE SET value=excluded.value,updated_at=excluded.updated_at,deleted_at=NULL",
            {key, value, now(), now()});
}
QJsonArray Database::search(const QString& text) const {
    QString safe = text;
    safe.replace('"', "\"\"");
    if (safe.trimmed().isEmpty())
        return {};
    return query("SELECT entity,entity_id,content FROM search_index WHERE search_index MATCH ? ORDER BY rank "
                 "LIMIT 100",
                 {'"' + safe + '"'});
}
QJsonObject Database::exportAll() const {
    QJsonObject tables;
    for (const auto& e : schema())
        tables[e.name] = query("SELECT * FROM \"" + e.name + "\" ORDER BY id");
    return {{"format", "PathWeave"}, {"version", 1}, {"exported_at", now()}, {"tables", tables}};
}
void Database::importAll(const QJsonObject& input) {
    if (input["format"] != "PathWeave" || input["version"].toInt() != 1 || !input["tables"].isObject())
        fail("Invalid PathWeave backup");
    auto tables = input["tables"].toObject();
    for (auto it = tables.begin(); it != tables.end(); ++it)
        if (!tableNames().contains(it.key()) || !it.value().isArray())
            fail("Unknown backup table");
    for (auto v : tables["documents"].toArray()) {
        auto d = v.toObject();
        auto encoded = d["content_base64"].toString();
        if (encoded.isEmpty())
            continue;
        auto bytes = QByteArray::fromBase64(encoded.toLatin1());
        if (bytes.size() > 20 * 1024 * 1024 ||
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) !=
                d["sha256"].toString())
            fail("Backup attachment integrity check failed");
    }
    if (!begin())
        fail("Cannot start import");
    try {
        execute("PRAGMA defer_foreign_keys=ON");
        for (const auto& e : schema())
            execute("DELETE FROM \"" + e.name + "\"");
        for (const auto& e : schema())
            for (const auto& row : tables[e.name].toArray()) {
                if (!row.isObject())
                    fail("Invalid backup row");
                auto obj = row.toObject();
                QStringList allowed = {"id", "created_at", "updated_at", "deleted_at", "is_demo"};
                for (const auto& f : e.fields)
                    allowed << f.name;
                QStringList cols, marks;
                QVariantList args;
                for (auto it = obj.begin(); it != obj.end(); ++it) {
                    if (!allowed.contains(it.key()))
                        fail("Unknown backup field");
                    cols << '"' + it.key() + '"';
                    marks << "?";
                    args << it.value().toVariant();
                }
                execute("INSERT INTO \"" + e.name + "\" (" + cols.join(',') + ") VALUES(" + marks.join(',') +
                            ")",
                        args);
            }
        // A backup must never silently opt a new installation into networking.
        setSetting("online_enabled", "false");
        if (!setting("automatic_search_enabled").isEmpty())
            setSetting("automatic_search_enabled", "false");
        execute("DELETE FROM response_cache");
        if (!query("PRAGMA foreign_key_check").isEmpty())
            fail("Backup contains broken references");
        if (!commit())
            fail("Import commit failed");
    } catch (...) {
        rollback();
        throw;
    }
}
void Database::deleteAll() {
    if (!begin())
        fail("Cannot start deletion");
    try {
        execute("PRAGMA defer_foreign_keys=ON");
        for (const auto& e : schema())
            execute("DELETE FROM \"" + e.name + "\"");
        execute("DELETE FROM response_cache");
        if (!commit())
            fail("Delete failed");
    } catch (...) {
        rollback();
        throw;
    }
}
void Database::removeDemo() {
    if (!begin())
        fail("Cannot start removal");
    try {
        execute("PRAGMA defer_foreign_keys=ON");
        for (const auto& e : schema())
            execute("DELETE FROM \"" + e.name + "\" WHERE is_demo=1");
        if (!commit())
            fail("Demo removal failed");
    } catch (...) {
        rollback();
        throw;
    }
}
bool Database::fileEncryptionEnabled() const {
#ifdef Q_OS_WIN
    auto attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path().utf16()));
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_ENCRYPTED);
#else
    return false;
#endif
}
void Database::enableFileEncryption() {
#ifdef Q_OS_WIN
    if (path() == ":memory:")
        throw std::runtime_error("Cơ sở dữ liệu đang ở bộ nhớ.");
    auto filename = path();
    auto folder = QFileInfo(filename).absolutePath();
    db_.close();
    bool ok = EncryptFileW(reinterpret_cast<LPCWSTR>(folder.utf16())) &&
              EncryptFileW(reinterpret_cast<LPCWSTR>(filename.utf16()));
    auto error = GetLastError();
    if (!db_.open())
        fail("Không mở lại được dữ liệu sau thao tác EFS.");
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA journal_mode=WAL");
    execute("PRAGMA secure_delete=ON");
    execute("PRAGMA busy_timeout=5000");
    if (!ok || !fileEncryptionEnabled())
        throw std::runtime_error(
            ("Windows EFS chưa bật được. Kiểm tra phiên bản Windows, NTFS và quyền truy cập. Mã lỗi: " +
             QString::number(error))
                .toStdString());
#else
    throw std::runtime_error("EFS chỉ hỗ trợ Windows.");
#endif
}
void Database::purgeDeleted() {
    if (!begin())
        fail("Cannot begin purge");
    try {
        execute("PRAGMA defer_foreign_keys=ON");
        for (const auto& e : schema())
            execute("DELETE FROM \"" + e.name + "\" WHERE deleted_at IS NOT NULL");
        if (!commit())
            fail("Purge failed");
    } catch (...) {
        rollback();
        throw;
    }
    execute("PRAGMA wal_checkpoint(TRUNCATE)");
    execute("VACUUM");
}
QString csv(const QJsonArray& rows) {
    if (rows.isEmpty())
        return {};
    QStringList keys = rows[0].toObject().keys();
    auto escape = [](QString s) {
        if (s.startsWith('=') || s.startsWith('+') || s.startsWith('-') || s.startsWith('@') ||
            s.startsWith('\t') || s.startsWith('\r'))
            s.prepend('\'');
        s.replace('"', "\"\"");
        return '"' + s + '"';
    };
    QStringList out;
    QStringList header;
    for (const auto& k : keys)
        header << escape(k);
    out << header.join(',');
    for (const auto& v : rows) {
        QStringList fields;
        for (const auto& k : keys)
            fields << escape(v.toObject()[k].toVariant().toString());
        out << fields.join(',');
    }
    return out.join("\r\n") + "\r\n";
}
} // namespace pw
