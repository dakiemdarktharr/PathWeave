#include "ResumeBuilder.h"
#include "domain/Schema.h"
#include <QBuffer>
#include <QPainter>
#include <QSet>
#include <QFile>
#include <QImageReader>
#include <QRegularExpression>
#include <algorithm>
#include <stdexcept>
namespace pw {
static QStringList lines(const QString& text) {
    QStringList result;
    for (auto s : text.split(QRegularExpression("[,;\\n]"), Qt::SkipEmptyParts)) {
        s = s.trimmed();
        if (!s.isEmpty() && !result.contains(s, Qt::CaseInsensitive))
            result << s;
    }
    return result;
}
QJsonArray ResumeBuilder::availableSources() const {
    QJsonArray result;
    for (QString table : {"current_roles", "projects", "career_evidence", "achievements"})
        for (auto v : service_.db.all(table)) {
            auto r = v.toObject();
            if (r["is_demo"].toInt())
                continue;
            QString title = r["title"].toString(r["name"].toString());
            if (table == "current_roles")
                title = r["job_title"].toString() + " · " + r["company_name"].toString();
            r["key"] = table + ":" + QString::number(r["id"].toInteger());
            r["table"] = table;
            r["label"] = entity(table).label + " · " + title;
            result << r;
        }
    return result;
}
ResumeDraft ResumeBuilder::build(const QJsonObject& job, const QStringList& selected) const {
    ResumeDraft draft;
    auto profile = service_.profile();
    QString jd = job["title"].toString() + " " + job["description"].toString() + " " +
                 job["requirements"].toString() + " " + job["skills"].toString();
    if (jd.size() > 200000)
        throw std::runtime_error("JD quá dài (tối đa 200.000 ký tự). Hãy giữ phần mô tả và yêu cầu chính.");
    QStringList targetTerms = lines(profile["hard_skills"].toString() + "," + job["skills"].toString());
    QStringList words;
    QSet<QString> known;
    for (auto token : MatchingEngine::synonyms(jd).split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
        if (token.size() > 2 && !known.contains(token)) {
            known << token;
            words << token;
            if (words.size() >= 128)
                break;
        }
    }
    QHash<QString, int> relevanceCache;
    auto relevance = [&](const QString& text) {
        if (relevanceCache.contains(text))
            return relevanceCache.value(text);
        int score = 0;
        for (const auto& term : targetTerms)
            if (MatchingEngine::containsTerm(jd, term) && MatchingEngine::containsTerm(text, term))
                score += 10;
        for (const auto& token : words)
            if (MatchingEngine::containsTerm(text, token))
                ++score;
        relevanceCache[text] = score;
        return score;
    };
    QStringList out;
    auto name = profile["display_name"].toString().trimmed();
    if (name.isEmpty())
        draft.warnings << "Chưa có họ tên: bổ sung trong Khảo sát trước khi xuất.";
    out << name;
    QStringList contact;
    for (QString field : {"email", "phone", "city", "country", "portfolio_url"})
        if (!profile[field].toString().isEmpty())
            contact << profile[field].toString();
    out << contact.join(" · ");
    if (profile["email"].toString().isEmpty() && profile["phone"].toString().isEmpty())
        draft.warnings << "Chưa có email hoặc số điện thoại liên hệ.";
    if (!job["title"].toString().isEmpty())
        out << "Mục tiêu ứng tuyển: " + job["title"].toString();
    if (profile["years_experience"].toDouble() > 0)
        out << "Kinh nghiệm: " + QString::number(profile["years_experience"].toDouble(), 'g', 4) + " năm";
    if (!profile["professional_summary"].toString().trimmed().isEmpty())
        out << "" << "GIỚI THIỆU" << profile["professional_summary"].toString();
    QStringList skills = lines(profile["hard_skills"].toString() + "," + profile["soft_skills"].toString());
    for (auto v : service_.db.all("skills")) {
        auto r = v.toObject();
        if (!r["is_demo"].toInt() && (r["category"] == "hard" || r["category"] == "soft"))
            skills << r["name"].toString();
    }
    QList<QJsonObject> chosen;
    QSet<qint64> representedAchievements;
    for (auto v : availableSources()) {
        auto r = v.toObject();
        if (!selected.contains(r["key"].toString()))
            continue;
        chosen << r;
        if (r["table"] == "career_evidence")
            representedAchievements << r["achievement_id"].toInteger();
        skills << lines(r["skills"].toString());
    }
    QStringList uniqueSkills;
    QSet<QString> seen;
    for (auto s : skills)
        if (!s.trimmed().isEmpty() && !seen.contains(MatchingEngine::synonyms(s))) {
            uniqueSkills << s.trimmed();
            seen << MatchingEngine::synonyms(s);
        }
    std::stable_sort(uniqueSkills.begin(), uniqueSkills.end(),
                     [&](const auto& a, const auto& b) { return relevance(a) > relevance(b); });
    if (!uniqueSkills.isEmpty())
        out << "" << "KỸ NĂNG" << uniqueSkills.join(" · ");
    for (QString table : {"current_roles", "projects", "career_evidence", "achievements"}) {
        QList<QJsonObject> records;
        for (auto r : chosen) {
            if (r["table"] != table)
                continue;
            if (table == "achievements" && representedAchievements.contains(r["id"].toInteger()))
                continue;
            records << r;
        }
        if (records.isEmpty())
            continue;
        std::stable_sort(records.begin(), records.end(), [&](const auto& a, const auto& b) {
            if (table == "current_roles")
                return a["start_date"].toString() > b["start_date"].toString();
            return relevance(a["label"].toString() + " " + a["skills"].toString() + " " +
                             a["polished_summary"].toString()) >
                   relevance(b["label"].toString() + " " + b["skills"].toString() + " " +
                             b["polished_summary"].toString());
        });
        out << ""
            << (table == "current_roles"     ? "KINH NGHIỆM"
                : table == "projects"        ? "DỰ ÁN"
                : table == "career_evidence" ? "MINH CHỨNG NGHỀ NGHIỆP"
                                             : "THÀNH TÍCH");
        for (auto r : records) {
            QString title = r["title"].toString(r["name"].toString());
            QString body, date;
            if (table == "current_roles") {
                title = r["job_title"].toString() + " — " + r["company_name"].toString();
                body = r["cv_description"].toString();
                date = r["start_date"].toString();
                if (!date.isEmpty())
                    date += " – " + (r["is_current"].toInt() ? "Hiện tại" : r["end_date"].toString());
            } else if (table == "projects") {
                body = r["cv_description"].toString();
                date = r["start_date"].toString();
            } else {
                body = r[table == "career_evidence" ? "polished_summary" : "body"].toString();
                auto impact =
                    r[table == "career_evidence" ? "measurable_impact" : "measurable_result"].toString();
                if (!impact.isEmpty() && !body.contains(impact))
                    body += (body.isEmpty() ? "" : "\n") + impact;
                date = r["date"].toString();
            }
            out << title + (date.isEmpty() ? "" : " | " + date);
            if (!body.isEmpty())
                out << body;
            out << "";
            draft.sources[r["key"].toString()] =
                QJsonObject{{"title", title}, {"text", body}, {"date", date}};
        }
    }
    for (auto pair : QList<QPair<QString, QString>>{
             {"certifications", "CHỨNG CHỈ"}, {"education", "HỌC VẤN"}, {"languages", "NGOẠI NGỮ"}}) {
        QStringList items = profile[pair.first].toString().split('\n', Qt::SkipEmptyParts);
        for (auto v : service_.db.all("skills")) {
            auto r = v.toObject();
            if (r["is_demo"].toInt())
                continue;
            if ((pair.first == "certifications" && r["category"] == "certification") ||
                (pair.first == "languages" && r["category"] == "language"))
                items << r["name"].toString() +
                             (r["level"].toString().isEmpty() ? "" : " — " + r["level"].toString());
        }
        items.removeDuplicates();
        std::stable_sort(items.begin(), items.end(),
                         [&](const auto& a, const auto& b) { return relevance(a) > relevance(b); });
        if (!items.isEmpty())
            out << "" << pair.second << items.join('\n');
    }
    draft.sources["profile_snapshot"] = profile;
    auto match = service_.matcher().score(job, profile);
    for (auto warning : match.missingPreferences)
        if (warning.startsWith("Chứng chỉ") || warning.startsWith("Số năm") || warning.contains("visa"))
            draft.warnings << warning;
    for (auto requirement : lines(job["skills"].toString()))
        if (!seen.contains(MatchingEngine::synonyms(requirement)))
            draft.warnings << "Chưa có kỹ năng trong dữ liệu được chọn: " + requirement;
    if (jd.trimmed().isEmpty())
        draft.warnings << "Chưa có JD: đây là CV tổng quát.";
    if (chosen.isEmpty())
        draft.warnings << "Chưa chọn kinh nghiệm hoặc minh chứng.";
    draft.warnings << "Đối chiếu lại JD, chứng chỉ, thời gian và mọi số liệu. JD không được dùng để tạo thêm "
                      "thành tích.";
    draft.text = out.join('\n').trimmed();
    return draft;
}
QString ResumeBuilder::html(const QString& text, const QString& photo) {
    QString out =
        "<!doctype html><html><head><meta charset='utf-8'><title>CV</title><style>body{font-family:'Segoe "
        "UI',Arial,sans-serif;color:#172b3a;font-size:11pt;line-height:1.35;}h1{font-size:23pt;}h2{font-size:"
        "12pt;color:#17666c;margin-top:18px;}p{margin:4px 0;}img{margin:0 0 12px 12px;}</style></head><body>";
    if (!photo.isEmpty() && photo.size() <= 2 * 1024 * 1024 &&
        QRegularExpression("^[A-Za-z0-9+/]*={0,2}$").match(photo).hasMatch()) {
        auto imageBytes = QByteArray::fromBase64(photo.toLatin1());
        QBuffer imageBuffer(&imageBytes);
        imageBuffer.open(QIODevice::ReadOnly);
        QImageReader imageReader(&imageBuffer, "PNG");
        auto imageSize = imageReader.size();
        QImage img;
        if (imageSize.isValid() && qint64(imageSize.width()) * imageSize.height() <= 4000000)
            img = imageReader.read();
        if (!img.isNull())
            out += "<img style='float:right' width='90' height='" +
                   QString::number(qRound(90.0 * img.height() / img.width())) +
                   "' src='data:image/png;base64," + photo + "'>";
    }
    const QStringList headings = {
        "GIỚI THIỆU", "KỸ NĂNG",   "KINH NGHIỆM", "DỰ ÁN",    "MINH CHỨNG NGHỀ NGHIỆP",
        "THÀNH TÍCH", "CHỨNG CHỈ", "HỌC VẤN",     "NGOẠI NGỮ"};
    bool first = true;
    for (auto line : text.split('\n')) {
        if (line.trimmed().isEmpty()) {
            out += "<p style='font-size:4pt;margin:2px 0'>&nbsp;</p>";
            continue;
        }
        QString tag = first ? "h1" : headings.contains(line.trimmed()) ? "h2" : "p";
        out += "<" + tag + ">" + line.toHtmlEscaped() + "</" + tag + ">";
        first = false;
    }
    return out + "</body></html>";
}
QString ResumeBuilder::importPhoto(const QString& file) {
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 10 * 1024 * 1024)
        throw std::runtime_error("Ảnh không đọc được hoặc lớn hơn 10 MiB.");
    QImageReader reader(&input);
    auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 24000000)
        throw std::runtime_error("Ảnh vượt 24 megapixel hoặc không hợp lệ.");
    reader.setAutoTransform(true);
    auto img = reader.read();
    if (img.isNull())
        throw std::runtime_error("Không đọc được ảnh. Chọn PNG hoặc JPEG.");
    img = img.scaled(360, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage clean(img.size(), QImage::Format_RGB32);
    clean.fill(Qt::white);
    {
        QPainter painter(&clean);
        painter.drawImage(0, 0, img);
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!clean.save(&buffer, "PNG"))
        throw std::runtime_error("Không lưu được ảnh CV.");
    return QString::fromLatin1(bytes.toBase64());
}
} // namespace pw
