#include "Reports.h"
#include "domain/Schema.h"
#include "services/CareerService.h"
#include <QDateTime>
#include <QTextDocument>
namespace pw {
QJsonObject Reports::funnel() const {
    auto rows = db_.query("SELECT * FROM applications WHERE deleted_at IS NULL AND is_demo=0");
    QJsonObject counts;
    int rejected = 0;
    for (auto v : rows) {
        auto s = v.toObject()["status"].toString();
        counts[s] = counts[s].toInt() + 1;
        if (s == "rejected")
            ++rejected;
    }
    counts["total"] = rows.size();
    counts["saved_jobs"] =
        db_.query("SELECT COUNT(*) n FROM saved_jobs WHERE deleted_at IS NULL AND is_demo=0")[0]
            .toObject()["n"];
    counts["rejection_rate"] = rows.isEmpty() ? QJsonValue() : QJsonValue(100.0 * rejected / rows.size());
    return counts;
}
static QString safe(QString s) {
    s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('|', "\\|");
    s.replace('\n', " ");
    return s;
}
QString Reports::markdown(int type, const QString& month) const {
    QStringList names = {"Báo cáo công việc tháng", "Minh chứng nghề nghiệp", "Phễu ứng tuyển",
                         "Báo cáo độ phù hợp"};
    QString out = "# " + names.value(type) + "\n\nKỳ: " + month + " · Tạo: " + now() +
                  "\n\nChỉ dùng dữ liệu cục bộ. Dữ liệu DEMO bị loại khỏi báo cáo.\n\n";
    auto list = [&](QString table, QString heading, QString dateField = QString(), bool monthOnly = false) {
        out += "## " + heading + "\n\n";
        int n = 0;
        for (auto v : db_.all(table)) {
            auto r = v.toObject();
            if (r["is_demo"].toInt())
                continue;
            if (monthOnly && !r[dateField].toString().startsWith(month))
                continue;
            if (table == "tasks" && r["status"] != "completed")
                continue;
            ++n;
            QString name = r["title"].toString(r["name"].toString());
            out += "- " + safe(name) + (r["is_demo"].toInt() ? " **[DEMO]**" : "") + " — " +
                   safe(r["body"].toString(r["polished_summary"].toString(r["description"].toString())));
            auto impact = r["measurable_result"].toString(r["measurable_impact"].toString());
            if (!impact.isEmpty())
                out += " · Kết quả: " + safe(impact);
            out += "\n";
        }
        if (!n)
            out += "Chưa có dữ liệu trong kỳ.\n";
        out += "\n";
    };
    if (type == 0) {
        list("projects", "Dự án (tất cả hiện có)");
        list("tasks", "Nhiệm vụ hoàn thành", "completed_at", true);
        list("achievements", "Thành tựu và kết quả", "date", true);
        out += "## Vướng mắc và kỹ năng đã dùng\n\n";
        for (auto v : db_.all("work_logs")) {
            auto r = v.toObject();
            if (r["is_demo"].toInt())
                continue;
            if (r["date"].toString().startsWith(month) && r["kind"] == "blocker")
                out += "- Vướng mắc: " + safe(r["title"].toString()) + "\n";
        }
        QMap<QString, int> used;
        for (auto v : db_.all("achievements")) {
            auto r = v.toObject();
            if (r["is_demo"].toInt())
                continue;
            if (r["date"].toString().startsWith(month))
                for (auto skill : r["skills"].toString().split(',', Qt::SkipEmptyParts))
                    ++used[skill.trimmed()];
        }
        for (auto it = used.begin(); it != used.end(); ++it)
            out += "- " + safe(it.key()) + ": " + QString::number(it.value()) + " thành tựu\n";
    }
    if (type == 1) {
        list("career_evidence", "Toàn bộ minh chứng");
        QMap<QString, QStringList> skills, projects;
        for (auto v : db_.all("career_evidence")) {
            auto r = v.toObject();
            if (r["is_demo"].toInt())
                continue;
            for (auto s : r["skills"].toString().split(',', Qt::SkipEmptyParts))
                skills[s.trimmed()] << r["title"].toString();
            auto p = db_.get("projects", r["project_id"].toInteger());
            projects[p["name"].toString("Chưa liên kết dự án")] << r["title"].toString();
        }
        auto group = [&](QString heading, const QMap<QString, QStringList>& m) {
            out += "## " + heading + "\n\n";
            for (auto it = m.begin(); it != m.end(); ++it)
                out += "- **" + safe(it.key()) + "**: " + safe(it.value().join("; ")) + "\n";
            out += "\n";
        };
        group("Theo kỹ năng", skills);
        group("Theo dự án", projects);
        out += "## Minh chứng chưa dùng trong ứng tuyển\n\n";
        for (auto v :
             db_.query("SELECT e.title FROM career_evidence e WHERE e.deleted_at IS NULL AND e.is_demo=0 AND "
                       "NOT EXISTS(SELECT 1 "
                       "FROM application_evidence a WHERE a.evidence_id=e.id AND a.deleted_at IS NULL)"))
            out += "- " + safe(v.toObject()["title"].toString()) + "\n";
    }
    if (type == 2) {
        auto c = cohort(month);
        out += "## Nhóm hồ sơ tạo trong tháng đã chọn\n\n";
        for (auto it = c.begin(); it != c.end(); ++it)
            out += "- " + safe(it.key()) + ": " + QString::number(it.value().toInt()) + "\n";
        out += "\nMỗi hồ sơ chỉ được đếm một lần ở mỗi giai đoạn có lịch sử xác nhận. Không suy đoán giai "
               "đoạn bị bỏ qua.\n\n";
        auto f = funnel();
        out += "## Trạng thái hiện tại\n\n";
        for (auto it = f.begin(); it != f.end(); ++it)
            out += "- " + displayValue(it.key()) + ": " +
                   (it.value().isNull()
                        ? "Chưa đủ dữ liệu"
                        : QString::number(it.value().toDouble(), 'f', it.key() == "rejection_rate" ? 1 : 0)) +
                   (it.key() == "rejection_rate" ? "%" : "") + "\n";
        out += "\nTỷ lệ từ chối = số hồ sơ đang ở trạng thái rejected / tổng hồ sơ chưa xóa. Các số trên là "
               "trạng thái hiện tại, không phải chuyển đổi theo đoàn hệ.\n\n## Thời gian trung bình ở trạng "
               "thái (các khoảng đã kết thúc)\n\n";
        auto durations = db_.query(
            "SELECT from_status,AVG((julianday(date)-julianday(previous_date))*24) AS hours,COUNT(*) AS "
            "samples FROM (SELECT from_status,date,LAG(date) OVER(PARTITION BY application_id ORDER BY "
            "date,id) AS previous_date FROM application_activities WHERE deleted_at IS NULL AND is_demo=0 "
            "AND application_id IN (SELECT id FROM applications WHERE deleted_at IS NULL AND is_demo=0) AND "
            "kind IN "
            "('status_changed','application_started')) WHERE previous_date IS NOT NULL AND "
            "julianday(date)>=julianday(previous_date) AND from_status IS "
            "NOT NULL AND from_status<>'' GROUP BY from_status");
        if (durations.isEmpty())
            out += "Chưa đủ lịch sử chuyển trạng thái.\n";
        for (auto v : durations) {
            auto r = v.toObject();
            if (r["is_demo"].toInt())
                continue;
            out += "- " + displayValue(r["from_status"].toString()) + ": " +
                   QString::number(r["hours"].toDouble(), 'f', 1) +
                   " giờ (n=" + QString::number(r["samples"].toInt()) + ")\n";
        }
    }
    if (type == 3) {
        CareerService service(db_);
        auto engine = service.matcher();
        QMap<QString, int> missing, types;
        out += "## Tin tuyển dụng và giải thích\n\n";
        for (auto v : db_.all("job_listings")) {
            auto j = v.toObject();
            if (j["is_demo"].toInt())
                continue;
            auto score = engine.score(j, service.matchingProfile());
            out += "- " + safe(j["title"].toString()) + " · " + displayValue(j["status"].toString()) + " · " +
                   QString::number(score.total, 'f', 0) + "/100\n";
            for (auto x : score.missingPreferences)
                if (x.startsWith("Kỹ năng:"))
                    ++missing[x];
            ++types[j["employment_type"].toString()];
        }
        out += "\n## Kỹ năng thiếu thường gặp\n\n";
        for (auto it = missing.begin(); it != missing.end(); ++it)
            out += "- " + safe(it.key()) + ": " + QString::number(it.value()) + "\n";
        out += "\n## Loại việc trong dữ liệu cục bộ\n\n";
        for (auto it = types.begin(); it != types.end(); ++it)
            out += "- " + displayValue(it.key()) + ": " + QString::number(it.value()) + "\n";
        out += "\n## Hoạt động theo nguồn\n\n";
        for (auto v : db_.query(
                 "SELECT source_id,COUNT(*) AS discovered,SUM(status='saved') AS "
                 "saved,SUM(status='dismissed') AS dismissed,(SELECT COUNT(*) FROM "
                 "applications a JOIN job_listings x ON x.id=a.job_listing_id WHERE "
                 "a.deleted_at IS NULL AND a.is_demo=0 AND x.source_id=j.source_id) AS applications FROM "
                 "job_listings j WHERE deleted_at IS NULL AND is_demo=0 GROUP BY source_id")) {
            auto r = v.toObject();
            if (r["is_demo"].toInt())
                continue;
            out += "- " + safe(r["source_id"].toString()) + ": " + QString::number(r["discovered"].toInt()) +
                   " tin; " + QString::number(r["saved"].toInt()) + " đang lưu; " +
                   QString::number(r["dismissed"].toInt()) + " bỏ qua; " +
                   QString::number(r["applications"].toInt()) + " hồ sơ\n";
        }
        out += "\nĐiểm phù hợp hỗ trợ đối chiếu sở thích; không dự đoán khả năng được tuyển.\n";
    }
    return out;
}
QJsonObject Reports::cohort(const QString& month) const {
    QJsonObject result{{"Tổng hồ sơ", 0},   {"Đã ứng tuyển", 0}, {"Đã qua recruiter screen", 0},
                       {"Đã phỏng vấn", 0}, {"Đã có offer", 0},  {"Đã bị từ chối", 0}};
    const QMap<QString, QString> labels = {{"applied", "Đã ứng tuyển"},
                                           {"recruiter_screen", "Đã qua recruiter screen"},
                                           {"interview", "Đã phỏng vấn"},
                                           {"offer", "Đã có offer"},
                                           {"rejected", "Đã bị từ chối"}};
    for (auto v : db_.query("SELECT id FROM applications WHERE deleted_at IS NULL AND is_demo=0 AND "
                            "substr(created_at,1,7)=?",
                            {month})) {
        result["Tổng hồ sơ"] = result["Tổng hồ sơ"].toInt() + 1;
        for (auto event :
             db_.query("SELECT DISTINCT to_status FROM application_activities WHERE application_id=? AND "
                       "deleted_at IS NULL AND kind IN ('application_started','status_changed')",
                       {v.toObject()["id"].toInteger()})) {
            auto label = labels.value(event.toObject()["to_status"].toString());
            if (!label.isEmpty())
                result[label] = result[label].toInt() + 1;
        }
    }
    return result;
}
QString Reports::ics() const {
    auto escape = [](QString s) {
        s.replace("\\", "\\\\")
            .replace("\r", "")
            .replace("\n", "\\n")
            .replace(",", "\\,")
            .replace(";", "\\;");
        return s;
    };
    QString out =
        "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//PathWeave//Calendar//VI\r\nCALSCALE:GREGORIAN\r\n";
    QString stamp = QDateTime::currentDateTimeUtc().toString("yyyyMMdd'T'HHmmss'Z'");
    for (auto v : calendar()) {
        auto r = v.toObject();
        QString raw = r["date"].toString();
        if (raw.isEmpty())
            continue;
        auto dt = QDateTime::fromString(raw, Qt::ISODate);
        auto day = QDate::fromString(raw, Qt::ISODate);
        QString start;
        if (!raw.contains('T') && day.isValid())
            start = "DTSTART;VALUE=DATE:" + day.toString("yyyyMMdd");
        else if (dt.isValid())
            start = "DTSTART:" + dt.toUTC().toString("yyyyMMdd'T'HHmmss'Z'");
        else
            continue;
        out += "BEGIN:VEVENT\r\nUID:" + r["entity"].toString() + "-" + QString::number(r["id"].toInteger()) +
               "@pathweave\r\nDTSTAMP:" + stamp + "\r\n" + start +
               "\r\nSUMMARY:" + escape(r["title"].toString()) + "\r\nEND:VEVENT\r\n";
    }
    out += "END:VCALENDAR\r\n";
    // Fold at <=75 octets without breaking UTF-8 codepoints.
    QByteArray folded;
    for (auto line : out.split("\r\n", Qt::SkipEmptyParts)) {
        auto bytes = line.toUtf8();
        while (bytes.size() > 75) {
            int n = 75;
            while (n > 0 && (static_cast<unsigned char>(bytes[n]) & 0xc0) == 0x80)
                --n;
            folded += bytes.left(n) + "\r\n";
            bytes = " " + bytes.mid(n);
        }
        folded += bytes + "\r\n";
    }
    return QString::fromUtf8(folded);
}
QString Reports::html(int type, const QString& month) const {
    QTextDocument d;
    d.setMarkdown(markdown(type, month));
    return d.toHtml();
}
QJsonArray Reports::calendar() const {
    return db_.query(
        "SELECT 'tasks' AS entity,id,title,due_date AS date FROM tasks WHERE deleted_at IS NULL AND status "
        "NOT IN ('completed','cancelled') UNION ALL SELECT 'projects',id,name,target_date FROM projects "
        "WHERE deleted_at IS NULL UNION ALL SELECT 'goals',id,title,target_date FROM goals WHERE deleted_at "
        "IS NULL UNION ALL SELECT 'applications',id,company||' · '||next_action,next_action_date FROM "
        "applications WHERE deleted_at IS NULL UNION ALL SELECT 'interviews',id,round,scheduled_at FROM "
        "interviews WHERE deleted_at IS NULL UNION ALL SELECT 'reminders',id,title,due_date FROM reminders "
        "WHERE deleted_at IS NULL UNION ALL SELECT 'work_logs',id,title,date FROM work_logs WHERE deleted_at "
        "IS NULL AND kind='milestone' ORDER BY date");
}
QJsonArray Reports::timeline() const {
    return db_.query(
        "SELECT 'work_logs' AS entity,id,title,date FROM work_logs WHERE deleted_at IS NULL UNION ALL SELECT "
        "'tasks',id,title,completed_at FROM tasks WHERE deleted_at IS NULL AND status='completed' UNION ALL "
        "SELECT 'achievements',id,title,date FROM achievements WHERE deleted_at IS NULL UNION ALL SELECT "
        "'saved_jobs',s.id,j.title,s.saved_at FROM saved_jobs s JOIN job_listings j ON j.id=s.job_listing_id "
        "WHERE s.deleted_at IS NULL UNION ALL SELECT 'application_activities',id,kind||' · "
        "'||COALESCE(to_status,''),date FROM application_activities WHERE deleted_at IS NULL UNION ALL "
        "SELECT 'interviews',id,round,scheduled_at FROM interviews WHERE deleted_at IS NULL UNION ALL SELECT "
        "'applications',id,next_action,next_action_date FROM applications WHERE deleted_at IS NULL AND "
        "next_action_date IS NOT NULL ORDER BY date DESC LIMIT 40");
}
} // namespace pw
