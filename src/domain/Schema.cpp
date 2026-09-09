#include "Schema.h"
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <stdexcept>
namespace pw {
static Field f(QString n, QString l, QString t = "TEXT", QString o = {}, QString r = {}, bool req = false) {
    return {n, l, t, o, r, req};
}
static Field ref(QString n, QString l, QString r) {
    return f(n, l, "INTEGER", {}, r);
}
static Field title(QString n = "title", QString l = "Tiêu đề") {
    return f(n, l, "TEXT", {}, {}, true);
}
const QVector<Entity>& schema() {
    static const QVector<Entity> data = {
        {"user_profile",
         "Hồ sơ",
         {title("display_name", "Tên hiển thị"), f("answers", "Câu trả lời", "JSON"),
          f("completion", "Hoàn thành (%)", "INTEGER"), f("skipped", "Đã bỏ qua", "BOOL")}},
        {"survey_answers", "Khảo sát", {title("section", "Mục"), f("answers", "Câu trả lời", "JSON")}},
        {"current_roles",
         "Công việc hiện tại",
         {title("company_name", "Công ty"), title("job_title", "Chức danh"), f("department", "Phòng ban"),
          f("manager_name", "Quản lý"), f("manager_email", "Email quản lý"),
          f("employment_type", "Loại việc", "TEXT", "full_time|part_time|contract|internship|temporary"),
          f("workplace_mode", "Hình thức", "TEXT", "remote|hybrid|on_site|flexible|unknown"),
          f("start_date", "Ngày bắt đầu", "DATE"), f("end_date", "Ngày kết thúc", "DATE"),
          f("is_current", "Hiện tại", "BOOL"), f("cv_description", "Mô tả được phép đưa vào CV", "LONG"),
          f("notes", "Ghi chú riêng", "LONG")}},
        {"projects",
         "Dự án",
         {title("name", "Tên dự án"), f("description", "Mô tả", "LONG"),
          f("status", "Trạng thái", "TEXT", "planned|active|blocked|completed|archived"),
          f("start_date", "Bắt đầu", "DATE"), f("target_date", "Hạn dự án", "DATE"), f("project_url", "URL"),
          f("impact_statement", "Tác động", "LONG"),
          f("cv_description", "Mô tả được phép đưa vào CV", "LONG"),
          ref("current_role_id", "Công việc", "current_roles")}},
        {"tasks",
         "Nhiệm vụ",
         {title(), f("description", "Mô tả", "LONG"),
          f("status", "Trạng thái", "TEXT", "inbox|planned|in_progress|waiting|completed|cancelled"),
          f("priority", "Ưu tiên", "TEXT", "low|medium|high|urgent"), f("due_date", "Hạn chót", "DATE"),
          f("estimated_minutes", "Ước tính (phút)", "INTEGER"),
          f("completed_at", "Hoàn thành lúc", "DATETIME"), ref("project_id", "Dự án", "projects"),
          ref("current_role_id", "Công việc", "current_roles"),
          ref("application_id", "Hồ sơ ứng tuyển", "applications"), f("tags", "Thẻ")}},
        {"goals",
         "Mục tiêu",
         {title(), f("description", "Mô tả", "LONG"),
          f("status", "Trạng thái", "TEXT", "planned|active|at_risk|completed|archived"),
          f("target_date", "Hạn chót", "DATE"), f("progress", "Tiến độ (%)", "INTEGER"),
          ref("current_role_id", "Công việc", "current_roles")}},
        {"work_logs",
         "Nhật ký công việc",
         {title(),
          f("kind", "Loại", "TEXT", "achievement|challenge|blocker|feedback|learning|note|milestone"),
          f("body", "Nội dung", "LONG"), f("date", "Ngày", "DATE"), ref("project_id", "Dự án", "projects"),
          ref("goal_id", "Mục tiêu", "goals"), f("skills", "Kỹ năng (phân cách dấu phẩy)"),
          f("impact_level", "Mức tác động", "TEXT", "low|medium|high"),
          f("measurable_result", "Kết quả đo được"), f("evidence_url", "URL minh chứng"),
          f("context", "Bối cảnh quản lý/khách hàng", "LONG"), f("tags", "Thẻ")}},
        {"achievements",
         "Thành tựu",
         {title(), f("body", "Nội dung", "LONG"), f("date", "Ngày", "DATE"),
          ref("project_id", "Dự án", "projects"), ref("goal_id", "Mục tiêu", "goals"),
          ref("work_log_id", "Nhật ký gốc", "work_logs"), f("skills", "Kỹ năng"),
          f("impact_level", "Tác động", "TEXT", "low|medium|high"), f("measurable_result", "Kết quả đo được"),
          f("evidence_url", "URL minh chứng"), f("context", "Bối cảnh", "LONG"), f("tags", "Thẻ")}},
        {"career_evidence",
         "Minh chứng nghề nghiệp",
         {title(), ref("achievement_id", "Thành tựu gốc", "achievements"),
          f("polished_summary", "Tóm tắt", "LONG"), f("measurable_impact", "Tác động đo được"),
          f("skills", "Kỹ năng"), ref("project_id", "Dự án", "projects"), f("date", "Ngày", "DATE"),
          ref("source_work_log_id", "Nhật ký gốc", "work_logs"), f("linked_reports", "Báo cáo liên quan")}},
        {"skills",
         "Kỹ năng",
         {title("name", "Tên kỹ năng"), f("category", "Nhóm", "TEXT", "hard|soft|language|certification"),
          f("level", "Cấp độ"), f("last_used", "Dùng lần cuối", "DATE"), f("notes", "Ghi chú", "LONG")}},
        {"project_skills",
         "Kỹ năng dự án",
         {ref("project_id", "Dự án", "projects"), ref("skill_id", "Kỹ năng", "skills")}},
        {"achievement_skills",
         "Kỹ năng thành tựu",
         {ref("achievement_id", "Thành tựu", "achievements"), ref("skill_id", "Kỹ năng", "skills")}},
        {"job_sources",
         "Nguồn tuyển dụng",
         {title("source_id", "Mã nguồn"), f("display_name", "Tên nguồn"), f("enabled", "Bật", "BOOL"),
          f("config", "Cấu hình (không chứa khóa)", "JSON"), f("last_sync", "Đồng bộ cuối", "DATETIME"),
          f("error_message", "Lỗi", "LONG")}},
        {"job_listings",
         "Tin tuyển dụng",
         {f("source_id", "Nguồn"),
          f("external_id", "Mã bên ngoài"),
          f("canonical_url", "URL gốc"),
          title(),
          title("company", "Công ty"),
          f("location", "Địa điểm"),
          f("employment_type", "Loại việc", "TEXT",
            "unknown|full_time|part_time|contract|internship|temporary"),
          f("workplace_mode", "Hình thức", "TEXT", "unknown|remote|hybrid|on_site|flexible"),
          f("salary_min", "Lương tối thiểu", "REAL"),
          f("salary_max", "Lương tối đa", "REAL"),
          f("currency", "Tiền tệ"),
          f("salary_period", "Kỳ lương", "TEXT", "unknown|year|month|hour"),
          f("description", "Mô tả", "LONG"),
          f("requirements", "Yêu cầu", "LONG"),
          f("skills", "Kỹ năng"),
          f("seniority", "Cấp bậc"),
          f("required_years", "Kinh nghiệm tối thiểu (năm)", "REAL"),
          f("certifications", "Chứng chỉ yêu cầu"),
          f("visa_sponsorship", "Hỗ trợ visa (để trống nếu chưa rõ)", "TEXT", "yes|no|unknown"),
          f("posted_at", "Đăng lúc", "DATETIME"),
          f("updated_at_source", "Cập nhật từ nguồn", "DATETIME"),
          f("fetched_at", "Lấy dữ liệu lúc", "DATETIME"),
          f("raw_payload_hash", "Mã băm dữ liệu"),
          f("match_score", "Điểm phù hợp", "REAL"),
          f("status", "Trạng thái", "TEXT", "new|saved|dismissed|applied|archived"),
          f("normalized_key", "Khóa chống trùng")}},
        {"saved_jobs",
         "Việc đã lưu",
         {ref("job_listing_id", "Tin tuyển dụng", "job_listings"), f("saved_at", "Lưu lúc", "DATETIME")}},
        {"applications",
         "Ứng tuyển",
         {ref("job_listing_id", "Tin tuyển dụng", "job_listings"), title("company", "Công ty"), title(),
          f("status", "Trạng thái", "TEXT",
            "saved|preparing|applied|recruiter_screen|interview|offer|rejected|withdrawn|archived"),
          f("saved_at", "Lưu lúc", "DATETIME"), f("applied_at", "Ứng tuyển lúc", "DATETIME"),
          f("next_action", "Bước tiếp theo"), f("next_action_date", "Ngày theo dõi", "DATE"),
          f("excitement_score", "Mức quan tâm (0–10)", "INTEGER"), f("notes", "Ghi chú", "LONG"),
          ref("resume_document_id", "CV", "documents"),
          ref("cover_letter_document_id", "Thư ứng tuyển", "documents")}},
        {"application_activities",
         "Lịch sử ứng tuyển",
         {ref("application_id", "Ứng tuyển", "applications"),
          f("kind", "Hoạt động", "TEXT",
            "job_saved|application_started|application_submitted|recruiter_contacted|recruiter_reply|"
            "interview_scheduled|interview_completed|follow_up_sent|offer_received|status_changed|note_"
            "added"),
          f("from_status", "Trước"), f("to_status", "Sau"), f("notes", "Ghi chú", "LONG"),
          f("date", "Thời gian", "DATETIME")}},
        {"interviews",
         "Phỏng vấn",
         {ref("application_id", "Ứng tuyển", "applications"), title("round", "Vòng"),
          f("interview_type", "Hình thức", "TEXT", "phone|video|on_site|technical|other"),
          f("scheduled_at", "Lịch hẹn", "DATETIME"), f("meeting_url", "URL cuộc họp"),
          f("interviewer", "Người phỏng vấn"), f("notes", "Ghi chú", "LONG"), f("result", "Kết quả"),
          f("follow_up_date", "Ngày theo dõi", "DATE")}},
        {"contacts",
         "Liên hệ",
         {title("name", "Họ tên"), f("company", "Công ty"), f("role", "Vai trò"), f("email", "Email"),
          f("phone", "Điện thoại"), f("linkedin_url", "LinkedIn URL"), f("notes", "Ghi chú", "LONG"),
          f("relationship_type", "Quan hệ", "TEXT",
            "recruiter|hiring_manager|referral|colleague|former_colleague|other")}},
        {"application_contacts",
         "Liên hệ ứng tuyển",
         {ref("application_id", "Ứng tuyển", "applications"), ref("contact_id", "Liên hệ", "contacts")}},
        {"documents",
         "Tài liệu",
         {title("name", "Tên"), f("path", "Đường dẫn cục bộ"),
          f("content_base64", "Nội dung quản lý", "LONG"), f("sha256", "SHA-256"),
          f("kind", "Loại", "TEXT", "resume|cover_letter|portfolio|other"), f("notes", "Ghi chú", "LONG")}},
        {"application_documents",
         "Tài liệu ứng tuyển",
         {ref("application_id", "Ứng tuyển", "applications"), ref("document_id", "Tài liệu", "documents")}},
        {"application_evidence",
         "Minh chứng ứng tuyển",
         {ref("application_id", "Ứng tuyển", "applications"),
          ref("evidence_id", "Minh chứng", "career_evidence")}},
        {"saved_searches",
         "Tìm kiếm đã lưu",
         {title("name", "Tên"), f("query", "Từ khóa"), f("location", "Địa điểm"),
          f("enabled", "Chạy theo lịch", "BOOL"), f("interval_hours", "Khoảng cách chạy (giờ)", "INTEGER"),
          f("next_run", "Lần chạy tiếp", "DATETIME"), f("last_run", "Lần chạy cuối", "DATETIME"),
          f("employment_type", "Loại việc", "TEXT",
            "unknown|full_time|part_time|contract|internship|temporary"),
          f("workplace_mode", "Hình thức", "TEXT", "unknown|remote|hybrid|on_site|flexible")}},
        {"resume_drafts",
         "CV theo công việc",
         {title("name", "Tên phiên bản"), ref("job_listing_id", "Tin tuyển dụng", "job_listings"),
          ref("application_id", "Ứng tuyển", "applications"),
          f("job_snapshot", "JD tại thời điểm tạo", "JSON"), f("body", "Nội dung CV", "LONG"),
          f("photo_base64", "Ảnh PNG", "LONG"), f("sources", "Nguồn thông tin", "JSON"),
          f("reviewed", "Đã kiểm tra", "BOOL")}},
        {"match_preferences",
         "Trọng số phù hợp",
         {title("name", "Tiêu chí"), f("weight", "Trọng số", "REAL")}},
        {"sync_runs",
         "Lịch sử đồng bộ",
         {f("source_id", "Nguồn"), f("status", "Trạng thái"), f("job_count", "Số tin", "INTEGER"),
          f("message", "Thông báo"), f("started_at", "Bắt đầu", "DATETIME")}},
        {"settings", "Cài đặt", {title("key", "Khóa"), f("value", "Giá trị", "LONG")}},
        {"reminders", "Nhắc nhở", {title(), f("due_date", "Ngày", "DATE"), f("notes", "Ghi chú", "LONG")}}};
    return data;
}
const Entity& entity(const QString& name) {
    for (const auto& e : schema())
        if (e.name == name)
            return e;
    throw std::runtime_error("Unknown table");
}
QStringList tableNames() {
    QStringList out;
    for (const auto& e : schema())
        out << e.name;
    return out;
}
QString normalize(QString s) {
    s = s.normalized(QString::NormalizationForm_KC).toLower().simplified();
    return s;
}
QString canonicalUrl(const QString& text) {
    QUrl u(text.trimmed());
    if (!u.isValid() || u.host().isEmpty())
        return {};
    u.setFragment({});
    u.setHost(u.host().toLower());
    if (u.port() == 443)
        u.setPort(-1);
    QString path = u.path();
    while (path.endsWith('/') && path.size() > 1)
        path.chop(1);
    u.setPath(path);
    QUrlQuery q(u);
    auto items = q.queryItems();
    QUrlQuery clean;
    std::sort(items.begin(), items.end());
    for (const auto& p : items)
        if (!p.first.startsWith("utm_") && p.first != "fbclid" && p.first != "gclid")
            clean.addQueryItem(p.first, p.second);
    u.setQuery(clean);
    return u.toString(QUrl::FullyEncoded);
}
QString jobIdentityKey(const QJsonObject& job) {
    QString base = normalize(job["company"].toString()) + "|" + normalize(job["title"].toString());
    auto url = canonicalUrl(job["canonical_url"].toString());
    if (!url.isEmpty())
        return base + "|url:" + url;
    if (!job["source_id"].toString().isEmpty() && !job["external_id"].toString().isEmpty())
        return base + "|external:" + job["source_id"].toString() + ":" + job["external_id"].toString();
    return base + "|scope:" + normalize(job["location"].toString()) + "|" +
           job["employment_type"].toString() + "|" + job["workplace_mode"].toString();
}
QVector<Field> surveyFields() {
    return {
        title("display_name", "1 · Tên hiển thị"),
        f("email", "Email trên CV"),
        f("phone", "Điện thoại trên CV"),
        f("portfolio_url", "Portfolio"),
        f("professional_summary", "Giới thiệu nghề nghiệp trên CV", "LONG"),
        f("country", "Quốc gia"),
        f("city", "Thành phố / vùng"),
        f("timezone", "Múi giờ"),
        f("language", "Ngôn ngữ ưu tiên"),
        f("situation", "2 · Tình trạng", "TEXT",
          "employed|unemployed|student|freelancer|career_changer|searching_while_employed"),
        f("employment_types", "3 · Loại việc", "MULTI", "full_time|part_time|contract|internship|temporary"),
        f("workplace_modes", "4 · Hình thức làm việc", "MULTI", "remote|hybrid|on_site|flexible"),
        title("desired_titles", "5 · Chức danh mong muốn (dấu phẩy)"),
        f("alternative_titles", "Chức danh khác"),
        f("industries", "Ngành"),
        f("preferred_companies", "Công ty ưu tiên"),
        f("excluded_companies", "Công ty loại trừ"),
        f("years_experience", "6 · Số năm kinh nghiệm", "REAL"),
        f("seniority", "Cấp bậc", "TEXT", "junior|mid|senior|lead|executive"),
        f("hard_skills", "Kỹ năng chuyên môn"),
        f("soft_skills", "Kỹ năng mềm"),
        f("languages", "Ngoại ngữ"),
        f("certifications", "Chứng chỉ (mỗi dòng: tên, đơn vị, năm)", "LONG"),
        f("education", "Học vấn (mỗi dòng một mục)", "LONG"),
        f("preferred_countries", "7 · Quốc gia ưu tiên"),
        f("preferred_cities", "Thành phố ưu tiên"),
        f("remote_countries", "Quốc gia chấp nhận làm từ xa"),
        f("salary_min", "Lương tối thiểu / năm", "REAL"),
        f("salary_currency", "Tiền tệ"),
        f("hourly_rate", "Mức lương / giờ", "REAL"),
        f("min_hours", "Giờ tối thiểu / tuần", "INTEGER"),
        f("max_hours", "Giờ tối đa / tuần", "INTEGER"),
        f("earliest_start", "Ngày có thể bắt đầu", "DATE"),
        f("relocation", "Sẵn sàng chuyển nơi ở", "TEXT", "no|yes|negotiable"),
        f("visa_required", "Cần bảo lãnh visa", "BOOL"),
        f("preferred_sources", "8 · Nguồn tuyển dụng ưu tiên"),
        f("include_keywords", "Từ khóa bao gồm"),
        f("exclude_keywords", "Từ khóa loại trừ"),
        f("daily_limit", "Số gợi ý tối đa / ngày", "INTEGER"),
        f("frequency", "Tần suất mong muốn", "TEXT", "manual|daily|weekly")};
}
QStringList validateSurvey(const QJsonObject& a) {
    QStringList e;
    if (a.value("display_name").toString().trimmed().isEmpty())
        e << "Vui lòng nhập tên hiển thị.";
    if (a.value("desired_titles").toString().trimmed().isEmpty())
        e << "Vui lòng nhập chức danh mong muốn.";
    if (a.value("max_hours").toDouble() < a.value("min_hours").toDouble())
        e << "Giờ tối đa phải lớn hơn hoặc bằng giờ tối thiểu.";
    for (auto k : {"salary_min", "hourly_rate", "years_experience", "min_hours", "max_hours", "daily_limit"})
        if (a.value(k).toDouble() < 0)
            e << "Giá trị không được âm.";
    if (a.value("max_hours").toDouble() > 168)
        e << "Số giờ / tuần không được quá 168.";
    for (const auto& f : surveyFields())
        if (f.type == "MULTI")
            for (const auto& v : a.value(f.name).toString().split(',', Qt::SkipEmptyParts))
                if (!f.options.split('|').contains(v.trimmed()))
                    e << "Lựa chọn không hợp lệ: " + f.label;
    return e;
}
int profileCompletion(const QJsonObject& a) {
    int filled = 0;
    auto fields = surveyFields();
    for (const auto& f : fields) {
        auto v = a.value(f.name);
        if (!v.isNull() && !v.isUndefined() && !v.toVariant().toString().trimmed().isEmpty())
            ++filled;
    }
    return qRound(100.0 * filled / fields.size());
}
QString displayValue(const QString& v) {
    static const QMap<QString, QString> m = {{"full_time", "Toàn thời gian"},
                                             {"part_time", "Bán thời gian"},
                                             {"contract", "Hợp đồng"},
                                             {"internship", "Thực tập"},
                                             {"temporary", "Tạm thời"},
                                             {"remote", "Từ xa"},
                                             {"hybrid", "Kết hợp"},
                                             {"on_site", "Tại văn phòng"},
                                             {"flexible", "Linh hoạt"},
                                             {"unknown", "Chưa rõ"},
                                             {"planned", "Đã lên kế hoạch"},
                                             {"active", "Đang thực hiện"},
                                             {"blocked", "Bị chặn"},
                                             {"completed", "Hoàn thành"},
                                             {"archived", "Lưu trữ"},
                                             {"inbox", "Hộp việc"},
                                             {"in_progress", "Đang làm"},
                                             {"waiting", "Đang chờ"},
                                             {"cancelled", "Đã hủy"},
                                             {"low", "Thấp"},
                                             {"medium", "Vừa"},
                                             {"high", "Cao"},
                                             {"urgent", "Khẩn cấp"},
                                             {"saved", "Đã lưu"},
                                             {"preparing", "Đang chuẩn bị"},
                                             {"applied", "Đã ứng tuyển"},
                                             {"recruiter_screen", "Sàng lọc"},
                                             {"interview", "Phỏng vấn"},
                                             {"offer", "Đề nghị"},
                                             {"rejected", "Bị từ chối"},
                                             {"withdrawn", "Đã rút"},
                                             {"achievement", "Thành tựu"},
                                             {"challenge", "Thử thách"},
                                             {"blocker", "Vướng mắc"},
                                             {"feedback", "Phản hồi"},
                                             {"learning", "Học tập"},
                                             {"note", "Ghi chú"},
                                             {"milestone", "Cột mốc"},
                                             {"at_risk", "Có rủi ro"},
                                             {"employed", "Đang đi làm"},
                                             {"unemployed", "Chưa có việc"},
                                             {"student", "Sinh viên"},
                                             {"freelancer", "Làm tự do"},
                                             {"career_changer", "Chuyển nghề"},
                                             {"searching_while_employed", "Tìm việc khi đang đi làm"},
                                             {"yes", "Có"},
                                             {"no", "Không"},
                                             {"manual", "Thủ công"},
                                             {"daily", "Hàng ngày"},
                                             {"weekly", "Hàng tuần"}};
    return m.value(v, v);
}
} // namespace pw
