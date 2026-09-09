#include "MatchingEngine.h"
#include "domain/Schema.h"
#include <QDateTime>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
namespace pw {
static QStringList terms(QString s) {
    QStringList out;
    for (auto v : s.split(QRegularExpression("[,;\\n]"), Qt::SkipEmptyParts)) {
        v = MatchingEngine::synonyms(v);
        if (!v.isEmpty())
            out << v;
    }
    return out;
}
QString MatchingEngine::synonyms(QString s) {
    s = normalize(s).normalized(QString::NormalizationForm_D);
    s.remove(QRegularExpression("[\\p{M}]"));
    s.replace(QChar(0x0111), 'd');
    const QList<QPair<QString, QString>> aliases = {{"lap trinh vien phan mem", "software engineer"},
                                                    {"ky su phan mem", "software engineer"},
                                                    {"lap trinh vien", "software engineer"},
                                                    {"software developer", "software engineer"},
                                                    {"programmer", "software engineer"},
                                                    {"front-end", "frontend"},
                                                    {"front end", "frontend"},
                                                    {"back-end", "backend"},
                                                    {"back end", "backend"},
                                                    {"sr.", "senior"},
                                                    {"jr.", "junior"},
                                                    {"viet nam", "vietnam"},
                                                    {"tp. ho chi minh", "ho chi minh"},
                                                    {"tp ho chi minh", "ho chi minh"},
                                                    {"hcmc", "ho chi minh"},
                                                    {"sai gon", "ho chi minh"},
                                                    {"ha noi", "hanoi"},
                                                    {"ky nang giao tiep", "communication"},
                                                    {"giao tiep", "communication"},
                                                    {"lam viec nhom", "teamwork"}};
    for (const auto& pair : aliases)
        s.replace(QRegularExpression("(?<![\\p{L}\\p{N}_])" + QRegularExpression::escape(pair.first) +
                                     "(?![\\p{L}\\p{N}_])"),
                  pair.second);
    return s.simplified();
}
bool MatchingEngine::containsTerm(const QString& text, const QString& term) {
    auto t = synonyms(term);
    if (t.isEmpty())
        return false;
    return QRegularExpression("(?<![\\p{L}\\p{N}_+#])" + QRegularExpression::escape(t) +
                              "(?![\\p{L}\\p{N}_+#])")
        .match(synonyms(text))
        .hasMatch();
}
static double similarity(QString a, QString b) {
    a = MatchingEngine::synonyms(a);
    b = MatchingEngine::synonyms(b);
    if (a == b)
        return 100;
    auto aa = a.split(' ', Qt::SkipEmptyParts), bb = b.split(' ', Qt::SkipEmptyParts);
    QSet<QString> x(aa.begin(), aa.end()), y(bb.begin(), bb.end());
    int size = x.size() + y.size();
    if (!size)
        return 50;
    return 200.0 * x.intersect(y).size() / size;
}
QJsonObject MatchScore::toJson() const {
    return {{"total", total},
            {"title", title},
            {"skill", skill},
            {"employment_type", employmentType},
            {"workplace_mode", workplaceMode},
            {"location", location},
            {"seniority", seniority},
            {"salary", salary},
            {"freshness", freshness},
            {"feedback", feedback},
            {"explanations", QJsonArray::fromStringList(explanations)},
            {"missing_preferences", QJsonArray::fromStringList(missingPreferences)}};
}
MatchScore MatchingEngine::score(const QJsonObject& j, const QJsonObject& p) const {
    MatchScore s;
    auto value = [](const QJsonObject& o, const QString& k) { return o.value(k).toString(); };
    auto titles = terms(value(p, "desired_titles") + "," + value(p, "alternative_titles"));
    s.title = titles.isEmpty() ? 50 : 0;
    for (const auto& t : titles)
        s.title = std::max(s.title, similarity(t, value(j, "title")));
    auto wantedSkills = terms(value(p, "hard_skills") + "," + value(p, "soft_skills"));
    auto required = terms(value(j, "skills"));
    QString text =
        normalize(value(j, "title") + " " + value(j, "description") + " " + value(j, "requirements"));
    if (!required.isEmpty()) {
        int found = 0;
        for (const auto& skill : required) {
            if (wantedSkills.contains(skill))
                ++found;
            else
                s.missingPreferences << "Kỹ năng: " + skill;
        }
        s.skill = 100.0 * found / required.size();
    } else if (!wantedSkills.isEmpty()) {
        int found = 0;
        for (const auto& skill : wantedSkills)
            if (containsTerm(text, skill))
                ++found;
        s.skill = 100.0 * found / wantedSkills.size();
        s.explanations << "Kỹ năng suy ra từ văn bản; nguồn chưa có danh sách yêu cầu có cấu trúc.";
    } else
        s.skill = 50;
    auto category = [&](QString pk, QString jk, QString label) {
        auto prefs = terms(value(p, pk));
        auto v = synonyms(value(j, jk));
        if (prefs.isEmpty())
            return 50.0;
        if (v.isEmpty() || v == "unknown") {
            s.missingPreferences << label + ": nguồn chưa xác nhận";
            return 50.0;
        }
        if (prefs.contains(v)) {
            s.explanations << label + ": " + displayValue(v);
            return 100.0;
        }
        s.missingPreferences << label + ": " + displayValue(v);
        return 0.0;
    };
    s.employmentType = category("employment_types", "employment_type", "Loại việc");
    s.workplaceMode = category("workplace_modes", "workplace_mode", "Hình thức");
    auto locations =
        terms(value(p, "preferred_countries") + "," + value(p, "preferred_cities") +
              (value(j, "workplace_mode") == "remote" ? "," + value(p, "remote_countries") : QString()));
    QString loc = synonyms(value(j, "location"));
    s.location = locations.isEmpty() ? 50 : 0;
    if (loc.isEmpty()) {
        s.location = 50;
        s.missingPreferences << "Địa điểm: chưa rõ";
    } else if (!locations.isEmpty()) {
        for (const auto& v : locations)
            if (containsTerm(loc, v) || loc == "worldwide" || loc == "anywhere")
                s.location = 100;
        if (s.location == 0)
            s.missingPreferences << "Địa điểm / quyền làm việc cần kiểm tra: " + loc;
    }
    auto senior = normalize(value(p, "seniority")), js = normalize(value(j, "seniority"));
    s.seniority = senior.isEmpty() || js.isEmpty() ? 50 : senior == js ? 100 : 0;
    double minimum = p["salary_min"].toDouble();
    if (value(j, "salary_period") == "hour")
        minimum = p["hourly_rate"].toDouble();
    double amount = j["salary_max"].toDouble();
    if (amount <= 0)
        amount = j["salary_min"].toDouble();
    QString period = value(j, "salary_period");
    if (period == "month")
        amount *= 12;
    if (amount <= 0 || minimum <= 0 || value(j, "currency").isEmpty() ||
        value(p, "salary_currency").compare(value(j, "currency"), Qt::CaseInsensitive) != 0 ||
        period.isEmpty() || period == "unknown") {
        s.salary = 50;
        s.explanations << "Lương: trung lập (thiếu số liệu, kỳ lương hoặc khác tiền tệ).";
    } else {
        s.salary = std::clamp(100.0 * amount / minimum, 0.0, 100.0);
        if (amount < minimum)
            s.missingPreferences << "Lương dưới mức mong muốn";
    }
    auto date = QDateTime::fromString(value(j, "posted_at"), Qt::ISODate);
    s.freshness = date.isValid()
                      ? std::clamp(100.0 - date.daysTo(QDateTime::currentDateTimeUtc()) * 3.0, 0.0, 100.0)
                      : 50;
    QMap<QString, double> values = {{"title", s.title},
                                    {"skill", s.skill},
                                    {"employment", s.employmentType},
                                    {"workplace", s.workplaceMode},
                                    {"location", s.location},
                                    {"seniority", s.seniority},
                                    {"salary", s.salary},
                                    {"freshness", s.freshness}};
    double denom = 0;
    for (auto it = weights.begin(); it != weights.end(); ++it) {
        double w = std::max(0.0, it.value());
        denom += w;
        s.total += w * values.value(it.key(), 50);
    }
    s.total = denom > 0 ? s.total / denom : 50;
    auto feedback = p["_explicit_feedback"].toObject();
    for (QString field : {"employment_type", "workplace_mode"}) {
        auto key = field + ":" + value(j, field);
        int votes = feedback[key].toInt();
        s.feedback += std::clamp(double(votes), -2.0, 2.0);
    }
    if (s.feedback != 0) {
        s.total += s.feedback;
        s.explanations
            << QString("Phản hồi cục bộ từ Lưu / Tạo hồ sơ / Bỏ qua: %1 điểm (tối đa ±4).").arg(s.feedback);
    }
    for (const auto& k : terms(value(p, "exclude_keywords")))
        if (containsTerm(text, k)) {
            s.total -= 30;
            s.missingPreferences << "Từ khóa loại trừ: " + k;
        }
    for (const auto& c : terms(value(p, "excluded_companies")))
        if (synonyms(value(j, "company")) == c) {
            s.total = 0;
            s.missingPreferences << "Công ty đã loại trừ";
        }
    for (const auto& k : terms(value(p, "include_keywords")))
        if (!containsTerm(text, k))
            s.missingPreferences << "Thiếu từ khóa ưu tiên: " + k;
    if (p["years_experience"].isDouble() && j["required_years"].isDouble() &&
        p["years_experience"].toDouble() < j["required_years"].toDouble())
        s.missingPreferences << "Số năm kinh nghiệm dưới yêu cầu được nguồn khai báo";
    if (p["visa_required"].toVariant().toBool()) {
        if (j["visa_sponsorship"].isNull() || j["visa_sponsorship"].isUndefined() ||
            j["visa_sponsorship"] == "unknown")
            s.missingPreferences << "Chưa xác nhận bảo lãnh visa";
        else if (j["visa_sponsorship"].toString() == "no")
            s.missingPreferences << "Nguồn không hỗ trợ bảo lãnh visa";
    }
    for (const auto& cert : terms(value(j, "certifications")))
        if (!containsTerm(value(p, "certifications"), cert))
            s.missingPreferences << "Chứng chỉ chưa có trong hồ sơ: " + cert;
    s.total = std::clamp(s.total, 0.0, 100.0);
    s.explanations << QString("Chức danh %1/100 · Kỹ năng %2/100").arg(qRound(s.title)).arg(qRound(s.skill));
    return s;
}
} // namespace pw
