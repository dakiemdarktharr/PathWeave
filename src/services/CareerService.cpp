#include "CareerService.h"
#include "domain/Schema.h"
#include <QDate>
#include <QJsonDocument>
#include <QFile>
#include <QSet>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <stdexcept>
namespace pw {
qint64 CareerService::save(const QString& table, QJsonObject row, qint64 id) {
    auto old = id ? db.get(table, id) : QJsonObject();
    QString status = row.value("status").toString();
    if (table == "applications" && id && !status.isEmpty() && old["status"] != status &&
        !validTransition(old["status"].toString(), status))
        throw std::runtime_error("Invalid application status transition");
    if (table == "tasks" && status == "completed" && old["status"] != "completed")
        row["completed_at"] = now();
    if (table == "tasks" && row.contains("status") && status != "completed" && old["status"] == "completed")
        row["completed_at"] = QJsonValue();
    if (table == "job_listings" && !id)
        return upsertJob(row);
    if (table == "applications" && status == "applied" && old["applied_at"].toString().isEmpty() &&
        row["applied_at"].toString().isEmpty())
        row["applied_at"] = now();
    if (table == "documents" && !row["path"].toString().isEmpty()) {
        QFile file(row["path"].toString());
        if (!file.open(QIODevice::ReadOnly) || file.size() > 20 * 1024 * 1024)
            throw std::runtime_error("Tệp tài liệu không đọc được hoặc quá 20 MiB.");
        auto bytes = file.readAll();
        row["content_base64"] = QString::fromLatin1(bytes.toBase64());
        row["sha256"] =
            QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        row["path"] = QJsonValue();
    }
    if (!db.begin())
        throw std::runtime_error("Cannot begin edit transaction");
    try {
        qint64 result = db.save(table, row, id);
        if (table == "achievements" && row.contains("skills")) {
            db.execute("DELETE FROM achievement_skills WHERE achievement_id=?", {result});
            QSet<qint64> linked;
            for (auto label :
                 row["skills"].toString().split(QRegularExpression("[,;\\n]"), Qt::SkipEmptyParts)) {
                label = label.trimmed();
                qint64 skill = 0;
                for (auto v : db.all("skills")) {
                    auto candidate = v.toObject()["name"].toString();
                    if (v.toObject()["is_demo"].toInt())
                        candidate.remove(" [DEMO]");
                    if (MatchingEngine::synonyms(candidate) == MatchingEngine::synonyms(label) &&
                        v.toObject()["is_demo"].toInt() ==
                            row.value("is_demo").toInt(old["is_demo"].toInt())) {
                        skill = v.toObject()["id"].toInteger();
                        break;
                    }
                }
                if (!skill)
                    skill =
                        db.save("skills", {{"name", label},
                                           {"category", "hard"},
                                           {"last_used", row["date"]},
                                           {"is_demo", row.value("is_demo").toInt(old["is_demo"].toInt())}});
                if (!linked.contains(skill)) {
                    db.save("achievement_skills",
                            {{"achievement_id", result},
                             {"skill_id", skill},
                             {"is_demo", row.value("is_demo").toInt(old["is_demo"].toInt())}});
                    linked << skill;
                }
            }
        }
        if (table == "skills" && id && row.contains("name") && old["name"] != row["name"]) {
            for (QString owner : {"achievements", "career_evidence", "work_logs"})
                for (auto v : db.all(owner)) {
                    auto r = v.toObject();
                    auto labels =
                        r["skills"].toString().split(QRegularExpression("[,;\\n]"), Qt::SkipEmptyParts);
                    bool changed = false;
                    for (auto& label : labels)
                        if (MatchingEngine::synonyms(label) ==
                            MatchingEngine::synonyms(old["name"].toString())) {
                            label = row["name"].toString();
                            changed = true;
                        }
                    if (changed)
                        db.save(owner, {{"skills", labels.join(", ")}}, r["id"].toInteger());
                }
        }
        if (table == "applications" && (!id || (row.contains("status") && old["status"] != status)))
            db.save("application_activities", {{"application_id", result},
                                               {"kind", id ? "status_changed" : "application_started"},
                                               {"from_status", old["status"]},
                                               {"to_status", status},
                                               {"date", now()},
                                               {"is_demo", row.value("is_demo").toInt()}});
        if (table == "interviews" && !id)
            db.save("application_activities", {{"application_id", row["application_id"]},
                                               {"kind", "interview_scheduled"},
                                               {"date", now()},
                                               {"is_demo", row.value("is_demo").toInt()}});
        if (!db.commit())
            throw std::runtime_error("Edit commit failed");
        return result;
    } catch (...) {
        db.rollback();
        throw;
    }
}
qint64 CareerService::upsertJob(QJsonObject j) {
    if (j["title"].toString().trimmed().isEmpty() || j["company"].toString().trimmed().isEmpty())
        throw std::runtime_error("Job title and company required");
    auto url = canonicalUrl(j["canonical_url"].toString());
    QString key = jobIdentityKey(j);
    j["canonical_url"] = url;
    j["normalized_key"] = key;
    auto found = db.query(
        "SELECT * FROM job_listings WHERE deleted_at IS NULL AND ((canonical_url=? AND canonical_url<>'') OR "
        "normalized_key=? OR (source_id=? AND external_id=? AND external_id<>'')) ORDER BY id LIMIT 1",
        {url, key, j["source_id"].toString(), j["external_id"].toString()});
    j["fetched_at"] = now();
    j["match_score"] = matcher().score(j, matchingProfile()).total;
    if (found.isEmpty()) {
        if (j["status"].toString().isEmpty())
            j["status"] = "new";
        return db.save("job_listings", j);
    }
    auto existing = found[0].toObject();
    j["status"] = existing["status"];
    return db.save("job_listings", j, existing["id"].toInteger());
}
qint64 CareerService::saveJob(qint64 id) {
    auto j = db.get("job_listings", id);
    if (j.isEmpty())
        throw std::runtime_error("Job not found");
    auto rows = db.query("SELECT id FROM saved_jobs WHERE job_listing_id=? AND deleted_at IS NULL", {id});
    if (!rows.isEmpty())
        return rows[0].toObject()["id"].toInteger();
    if (!db.begin())
        throw std::runtime_error("Transaction failed");
    try {
        auto saved =
            db.save("saved_jobs", {{"job_listing_id", id}, {"saved_at", now()}, {"is_demo", j["is_demo"]}});
        db.save("job_listings", {{"status", "saved"}}, id);
        if (!db.commit())
            throw std::runtime_error("Save failed");
        return saved;
    } catch (...) {
        db.rollback();
        throw;
    }
}
qint64 CareerService::apply(qint64 id) {
    auto j = db.get("job_listings", id);
    if (j.isEmpty())
        throw std::runtime_error("Job not found");
    auto existing = db.query("SELECT id FROM applications WHERE job_listing_id=? AND deleted_at IS NULL AND "
                             "status NOT IN ('archived','withdrawn','rejected')",
                             {id});
    if (!existing.isEmpty())
        return existing[0].toObject()["id"].toInteger();
    saveJob(id);
    return save("applications", {{"job_listing_id", id},
                                 {"company", j["company"]},
                                 {"title", j["title"]},
                                 {"status", "preparing"},
                                 {"saved_at", now()},
                                 {"is_demo", j["is_demo"]}});
}
qint64 CareerService::convertEvidence(qint64 id) {
    auto a = db.get("achievements", id);
    if (a.isEmpty())
        throw std::runtime_error("Achievement not found");
    return db.save("career_evidence", {{"title", a["title"]},
                                       {"achievement_id", id},
                                       {"polished_summary", a["body"]},
                                       {"measurable_impact", a["measurable_result"]},
                                       {"skills", a["skills"]},
                                       {"project_id", a["project_id"]},
                                       {"date", a["date"]},
                                       {"source_work_log_id", a["work_log_id"]},
                                       {"is_demo", a["is_demo"]}});
}
bool CareerService::validTransition(const QString& from, const QString& to) {
    QStringList statuses = {"saved", "preparing", "applied",   "recruiter_screen", "interview",
                            "offer", "rejected",  "withdrawn", "archived"};
    if (!statuses.contains(to))
        return false;
    if (from == to)
        return true;
    if (from == "archived")
        return to == "saved";
    if (from == "rejected" || from == "withdrawn")
        return to == "archived" || to == "preparing";
    return statuses.contains(from);
}
void CareerService::transition(qint64 id, const QString& status, const QString& note) {
    auto a = db.get("applications", id);
    if (a.isEmpty())
        throw std::runtime_error("Application not found");
    a.remove("id");
    a["status"] = status;
    if (!note.isEmpty())
        a["notes"] = note;
    save("applications", a, id);
}
QJsonObject CareerService::profile() const {
    auto rows = db.all("user_profile");
    return rows.isEmpty()
               ? QJsonObject()
               : QJsonDocument::fromJson(rows[0].toObject()["answers"].toString().toUtf8()).object();
}
QJsonObject CareerService::matchingProfile() const {
    auto p = profile();
    QStringList skills;
    skills << p["hard_skills"].toString();
    for (auto v : db.all("skills")) {
        auto r = v.toObject();
        if (!r["is_demo"].toInt() && (r["category"] == "hard" || r["category"] == "soft"))
            skills << r["name"].toString();
    }
    for (QString table : {"achievements", "career_evidence", "work_logs"})
        for (auto v : db.all(table)) {
            auto r = v.toObject();
            if (!r["is_demo"].toInt())
                skills << r["skills"].toString();
        }
    p["hard_skills"] = skills.join(',');
    if (db.setting("explicit_feedback_enabled", "true") == "true") {
        QJsonObject votes;
        for (auto v : db.query(
                 "SELECT j.employment_type,j.workplace_mode,CASE WHEN j.status='dismissed' THEN -1 WHEN "
                 "EXISTS(SELECT 1 FROM applications a WHERE a.job_listing_id=j.id AND a.deleted_at IS NULL "
                 "AND a.is_demo=0) THEN 2 ELSE 1 END AS vote FROM job_listings j WHERE j.deleted_at IS NULL "
                 "AND j.is_demo=0 AND (j.status='dismissed' OR EXISTS(SELECT 1 FROM saved_jobs s WHERE "
                 "s.job_listing_id=j.id AND s.deleted_at IS NULL AND s.is_demo=0) OR EXISTS(SELECT 1 FROM "
                 "applications a WHERE a.job_listing_id=j.id AND a.deleted_at IS NULL AND a.is_demo=0))")) {
            auto r = v.toObject();
            for (QString field : {"employment_type", "workplace_mode"}) {
                auto type = r[field].toString();
                if (type.isEmpty() || type == "unknown")
                    continue;
                auto key = field + ":" + type;
                votes[key] = votes[key].toInt() + r["vote"].toInt();
            }
        }
        p["_explicit_feedback"] = votes;
    }

    return p;
}
void CareerService::saveProfile(const QJsonObject& p, bool skipped) {
    if (!skipped && !validateSurvey(p).isEmpty())
        throw std::runtime_error(validateSurvey(p).join('\n').toStdString());
    auto rows = db.all("user_profile");
    db.save("user_profile",
            {{"display_name", p["display_name"].toString().isEmpty() ? "Bạn" : p["display_name"]},
             {"answers", p},
             {"completion", profileCompletion(p)},
             {"skipped", skipped}},
            rows.isEmpty() ? 0 : rows[0].toObject()["id"].toInteger());
    db.setSetting("onboarding_seen", "true");
    auto surveys = db.all("survey_answers");
    db.save("survey_answers", {{"section", "profile"}, {"answers", p}},
            surveys.isEmpty() ? 0 : surveys[0].toObject()["id"].toInteger());
}
MatchingEngine CareerService::matcher() const {
    MatchingEngine e;
    for (const auto& v : db.all("match_preferences")) {
        auto r = v.toObject();
        e.weights[r["name"].toString()] = r["weight"].toDouble();
    }
    return e;
}
void CareerService::loadDemo() {
    if (!db.query("SELECT id FROM current_roles WHERE is_demo=1 AND deleted_at IS NULL").isEmpty())
        return;
    auto add = [&](QString t, QJsonObject row) {
        row["is_demo"] = 1;
        return save(t, row);
    };
    QString day = QDate::currentDate().toString(Qt::ISODate);
    auto role = add("current_roles", {{"company_name", "Demo Labs"},
                                      {"job_title", "Software Engineer"},
                                      {"employment_type", "full_time"},
                                      {"workplace_mode", "hybrid"},
                                      {"is_current", 1},
                                      {"start_date", day},
                                      {"notes", "DEMO · Dữ liệu minh họa, không phải thành tích của bạn."}});
    QList<qint64> projects;
    for (auto name : {"Desktop Platform [DEMO]", "Career Insights [DEMO]"})
        projects << add("projects",
                        {{"name", name},
                         {"status", "active"},
                         {"current_role_id", role},
                         {"start_date", day},
                         {"target_date", QDate::currentDate().addDays(20).toString(Qt::ISODate)}});
    QStringList skills = {"C++",           "Qt",         "SQLite",         "Testing",
                          "Communication", "Networking", "Product design", "SQL"};
    for (const auto& name : skills)
        add("skills", {{"name", name + " [DEMO]"}, {"category", "hard"}, {"last_used", day}});
    for (int i = 0; i < 3; ++i)
        add("goals", {{"title", QString("Mục tiêu học tập %1 [DEMO]").arg(i + 1)},
                      {"status", i == 2 ? "at_risk" : "active"},
                      {"progress", i * 20},
                      {"target_date", QDate::currentDate().addDays(15).toString(Qt::ISODate)},
                      {"current_role_id", role}});
    QStringList taskTitles = {"Thiết kế mô hình dữ liệu", "Viết migration", "Kiểm tra bàn phím",
                              "Tối ưu truy vấn",          "Viết tài liệu",  "Rà soát giao diện",
                              "Chuẩn bị bản phát hành",   "Tổng kết sprint"};
    for (int i = 0; i < 8; ++i)
        add("tasks", {{"title", taskTitles[i] + " [DEMO]"},
                      {"status", i < 2   ? "completed"
                                 : i < 4 ? "in_progress"
                                         : "planned"},
                      {"priority", i % 2 ? "medium" : "high"},
                      {"due_date", QDate::currentDate().addDays(i - 3).toString(Qt::ISODate)},
                      {"project_id", projects[i % 2]},
                      {"current_role_id", role}});
    for (int i = 0; i < 5; ++i) {
        auto a = add("achievements", {{"title", QString("Cải tiến sản phẩm %1 [DEMO]").arg(i + 1)},
                                      {"body", "Ví dụ minh họa: hoàn thành một cải tiến được nhóm đánh giá."},
                                      {"date", day},
                                      {"skills", skills[i]},
                                      {"project_id", projects[i % 2]},
                                      {"impact_level", "medium"},
                                      {"measurable_result", "Chưa nhập số liệu — không tự tạo kết quả."}});
        if (i < 4)
            convertEvidence(a);
    }
    QStringList types = {"full_time", "part_time", "full_time", "full_time", "contract"},
                modes = {"remote", "remote", "hybrid", "on_site", "unknown"};
    for (int i = 0; i < 5; ++i) {
        auto j = add("job_listings",
                     {{"source_id", "demo"},
                      {"external_id", QString::number(i + 1)},
                      {"canonical_url", QString("https://example.com/pathweave-demo/%1").arg(i + 1)},
                      {"title", i == 1 ? "Part-time C++ Developer [DEMO]"
                                       : "Software Engineer " + QString::number(i + 1) + " [DEMO]"},
                      {"company", "Demo Employer " + QString::number(i + 1)},
                      {"employment_type", types[i]},
                      {"workplace_mode", modes[i]},
                      {"location", "Vietnam"},
                      {"description", "DEMO · Tin minh họa, không phải vị trí đang tuyển. C++ Qt SQLite."},
                      {"skills", "C++, Qt, SQLite"},
                      {"posted_at", now()},
                      {"status", "new"}});
        if (i < 4) {
            auto a = apply(j);
            if (i > 0)
                transition(a, QStringList{"preparing", "applied", "interview", "offer"}[i]);
            db.save("applications", {{"next_action", "Theo dõi [DEMO]"}, {"next_action_date", day}}, a);
            if (i >= 2)
                add("interviews",
                    {{"application_id", a},
                     {"round", "Vòng kỹ thuật [DEMO]"},
                     {"interview_type", "video"},
                     {"scheduled_at", QDateTime::currentDateTime().addDays(i).toString(Qt::ISODate)},
                     {"interviewer", "Demo Interviewer"}});
        }
    }
    for (int i = 0; i < 4; ++i)
        add("contacts", {{"name", QString("Demo Contact %1").arg(i + 1)},
                         {"company", "Demo Labs"},
                         {"email", QString("demo%1@example.com").arg(i + 1)},
                         {"relationship_type", i ? "colleague" : "recruiter"}});
    add("job_sources", {{"source_id", "demo"},
                        {"display_name", "Demo · Không kết nối mạng"},
                        {"enabled", 1},
                        {"config", QJsonObject()}});
}
} // namespace pw
