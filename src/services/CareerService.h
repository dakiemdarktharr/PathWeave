#pragma once
#include "database/Database.h"
#include "matching/MatchingEngine.h"
namespace pw {
class CareerService {
  public:
    explicit CareerService(Database& db) : db(db) {}
    Database& db;
    qint64 save(const QString& table, QJsonObject row, qint64 id = 0);
    qint64 upsertJob(QJsonObject job);
    qint64 saveJob(qint64 id);
    qint64 apply(qint64 jobId);
    qint64 convertEvidence(qint64 achievementId);
    void transition(qint64 applicationId, const QString& status, const QString& note = {});
    static bool validTransition(const QString& from, const QString& to);
    void saveProfile(const QJsonObject& profile, bool skipped = false);
    QJsonObject profile() const;
    QJsonObject matchingProfile() const;
    MatchingEngine matcher() const;
    void loadDemo();
};
} // namespace pw
