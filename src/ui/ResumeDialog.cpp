#include "ResumeDialog.h"
#include "SurveyDialog.h"
#include <QtWidgets>
#include <QPdfWriter>
#include <QAbstractTextDocumentLayout>
#include <QCryptographicHash>
namespace pw {
ResumeDialog::ResumeDialog(CareerService& s, qint64 jobId, qint64 applicationId, qint64 draftId,
                           QWidget* parent)
    : QDialog(parent), service_(s) {
    setWindowTitle("CV theo công việc · văn bản và ảnh");
    setObjectName("resumeDialog");
    resize(1160, 820);
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel("Chọn JD và minh chứng được phép chia sẻ. CV được sắp xếp cục bộ từ dữ liệu "
                             "thật; bạn kiểm tra trước khi xuất. Không tự nộp hồ sơ.");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto* tabs = new QTabWidget;
    layout->addWidget(tabs, 1);
    auto* input = new QWidget;
    auto* form = new QVBoxLayout(input);
    tabs->addTab(input, "1 · JD và dữ liệu");
    jobs_ = new QComboBox;
    jobs_->setObjectName("resumeJob");
    jobs_->addItem("Dán JD riêng", QVariant());
    for (auto v : s.db.all("job_listings")) {
        auto j = v.toObject();
        if (j["is_demo"].toInt())
            continue;
        jobs_->addItem(j["title"].toString() + " · " + j["company"].toString(), j["id"].toInteger());
    }
    form->addWidget(jobs_);
    title_ = new QLineEdit;
    title_->setObjectName("resumeTargetTitle");
    title_->setPlaceholderText("Chức danh ứng tuyển");
    form->addWidget(title_);
    jd_ = new QPlainTextEdit;
    jd_->setObjectName("resumeJobDescription");
    jd_->setPlaceholderText("Dán mô tả và yêu cầu công việc…");
    jd_->setMaximumHeight(180);
    form->addWidget(jd_);
    auto* profile = new QPushButton("Chỉnh sửa hồ sơ, chứng chỉ, học vấn và liên hệ");
    form->addWidget(profile);
    connect(profile, &QPushButton::clicked, this, [this] {
        SurveyDialog d(service_, this);
        d.exec();
    });
    auto* hint = new QLabel(
        "Tích các mục được phép đưa vào CV. Ghi chú riêng và dữ liệu demo không được nhập tự động.");
    hint->setWordWrap(true);
    form->addWidget(hint);
    evidence_ = new QListWidget;
    evidence_->setObjectName("resumeEvidence");
    form->addWidget(evidence_, 1);
    for (auto v : ResumeBuilder(s).availableSources()) {
        auto r = v.toObject();
        auto* item = new QListWidgetItem(r["label"].toString(), evidence_);
        item->setData(Qt::UserRole, r["key"].toString());
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(r["table"] == "current_roles" ? Qt::Checked : Qt::Unchecked);
    }
    auto* generateButton = new QPushButton("Tạo bản nháp theo JD");
    generateButton->setObjectName("generateResume");
    form->addWidget(generateButton);
    auto* draftPage = new QWidget;
    auto* dl = new QVBoxLayout(draftPage);
    tabs->addTab(draftPage, "2 · Biên tập và xem trước");
    name_ = new QLineEdit("CV theo công việc");
    name_->setAccessibleName("Tên phiên bản CV");
    dl->addWidget(name_);
    auto* split = new QSplitter;
    text_ = new QPlainTextEdit;
    text_->setObjectName("resumeText");
    preview_ = new QTextBrowser;
    preview_->setOpenExternalLinks(false);
    split->addWidget(text_);
    split->addWidget(preview_);
    dl->addWidget(split, 1);
    auto* photoBar = new QHBoxLayout;
    auto* photo = new QPushButton("Chọn ảnh của bạn…");
    auto* clear = new QPushButton("Bỏ ảnh");
    photoBar->addWidget(photo);
    photoBar->addWidget(clear);
    photoBar->addStretch();
    dl->addLayout(photoBar);
    connect(photo, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, "Chọn ảnh", {}, "Ảnh (*.png *.jpg *.jpeg)");
        if (path.isEmpty())
            return;
        try {
            setPhotoFromFile(path);
            reviewed_->setChecked(false);
            updatePreview();
        } catch (const std::exception& e) {
            diagnostics_->setText(QString::fromUtf8(e.what()));
        }
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        photo_.clear();
        reviewed_->setChecked(false);
        updatePreview();
    });
    diagnostics_ = new QLabel;
    diagnostics_->setWordWrap(true);
    diagnostics_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(diagnostics_);
    reviewed_ = new QCheckBox("Tôi đã kiểm tra nội dung, ảnh, số liệu và quyền chia sẻ trong CV này");
    reviewed_->setObjectName("resumeReviewed");
    layout->addWidget(reviewed_);
    auto* actions = new QHBoxLayout;
    applications_ = new QComboBox;
    applications_->setObjectName("resumeApplication");
    applications_->addItem("Chưa gắn ứng tuyển", QVariant());
    for (auto v : s.db.all("applications")) {
        auto a = v.toObject();
        if (!a["is_demo"].toInt())
            applications_->addItem(a["company"].toString() + " · " + a["title"].toString(),
                                   a["id"].toInteger());
    }
    applications_->setCurrentIndex(std::max(0, applications_->findData(applicationId)));
    actions->addWidget(applications_);
    auto* save = new QPushButton("Lưu phiên bản mới");
    save->setObjectName("saveResumeDraft");
    actions->addWidget(save);
    connect(save, &QPushButton::clicked, this, [this] {
        try {
            auto id = saveDraft();
            diagnostics_->setText("Đã lưu phiên bản #" + QString::number(id));
        } catch (const std::exception& e) {
            diagnostics_->setText(QString::fromUtf8(e.what()));
        }
    });
    for (QString format : {"PDF", "HTML", "TXT"}) {
        auto* b = new QPushButton("Xuất " + format);
        actions->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, format] {
            try {
                auto bytes = exportBytes(format);
                auto path = QFileDialog::getSaveFileName(this, "Xuất CV", "PathWeave-CV." + format.toLower(),
                                                         format + " (*." + format.toLower() + ")");
                if (path.isEmpty())
                    return;
                QSaveFile f(path);
                if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
                    throw std::runtime_error("Không ghi được CV.");
                diagnostics_->setText("Đã xuất: " + path);
            } catch (const std::exception& e) {
                diagnostics_->setText(QString::fromUtf8(e.what()));
            }
        });
    }
    auto* attachButton = new QPushButton("Gắn PDF vào ứng tuyển");
    attachButton->setObjectName("attachResume");
    actions->addWidget(attachButton);
    connect(attachButton, &QPushButton::clicked, this, &ResumeDialog::attach);
    layout->addLayout(actions);
    connect(text_, &QPlainTextEdit::textChanged, this, [this] {
        reviewed_->setChecked(false);
        updatePreview();
    });
    connect(jobs_, &QComboBox::currentIndexChanged, this, [this] {
        auto j = service_.db.get("job_listings", jobs_->currentData().toLongLong());
        title_->setText(j["title"].toString());
        jd_->setPlainText(j["description"].toString() + "\n" + j["requirements"].toString());
    });
    connect(generateButton, &QPushButton::clicked, this, [this, tabs] {
        try {
            generate();
            tabs->setCurrentIndex(1);
        } catch (const std::exception& e) {
            diagnostics_->setText(QString::fromUtf8(e.what()));
        }
    });
    connect(jd_, &QPlainTextEdit::textChanged, this, [this] { reviewed_->setChecked(false); });
    connect(title_, &QLineEdit::textChanged, this, [this] { reviewed_->setChecked(false); });
    if (!jobId && applicationId)
        jobId = s.db.get("applications", applicationId)["job_listing_id"].toInteger();
    jobs_->setCurrentIndex(std::max(0, jobs_->findData(jobId)));
    if (draftId) {
        auto d = s.db.get("resume_drafts", draftId);
        auto snap = QJsonDocument::fromJson(d["job_snapshot"].toString().toUtf8()).object();
        jobs_->setCurrentIndex(std::max(0, jobs_->findData(d["job_listing_id"].toInteger())));
        title_->setText(snap["title"].toString());
        jd_->setPlainText(snap["description"].toString());
        name_->setText(d["name"].toString());
        provenance_ = QJsonDocument::fromJson(d["sources"].toString().toUtf8()).object();
        photo_ = d["photo_base64"].toString();
        text_->setPlainText(d["body"].toString());
        reviewed_->setChecked(d["reviewed"].toInt());
        applications_->setCurrentIndex(std::max(0, applications_->findData(d["application_id"].toInteger())));
        for (int i = 0; i < evidence_->count(); ++i)
            evidence_->item(i)->setCheckState(
                provenance_.contains(evidence_->item(i)->data(Qt::UserRole).toString()) ? Qt::Checked
                                                                                        : Qt::Unchecked);
        tabs->setCurrentIndex(1);
        updatePreview();
        savedFingerprint_ = fingerprint();
    }
}
QJsonObject ResumeDialog::jobSnapshot() const {
    auto j = service_.db.get("job_listings", jobs_->currentData().toLongLong());
    j["title"] = title_->text();
    j["description"] = jd_->toPlainText();
    j["requirements"] = "";
    return j;
}
void ResumeDialog::setPhotoFromFile(const QString& path) {
    photo_ = ResumeBuilder::importPhoto(path);
    reviewed_->setChecked(false);
    updatePreview();
}
void ResumeDialog::generate() {
    QStringList selected;
    for (int i = 0; i < evidence_->count(); ++i)
        if (evidence_->item(i)->checkState() == Qt::Checked)
            selected << evidence_->item(i)->data(Qt::UserRole).toString();
    auto d = ResumeBuilder(service_).build(jobSnapshot(), selected);
    provenance_ = d.sources;
    text_->setPlainText(d.text);
    name_->setText("CV · " + title_->text());
    diagnostics_->setText(d.warnings.join('\n'));
    reviewed_->setChecked(false);
}
void ResumeDialog::updatePreview() {
    preview_->setHtml(ResumeBuilder::html(text_->toPlainText(), photo_));
}
qint64 ResumeDialog::saveDraft() {
    if (text_->toPlainText().trimmed().isEmpty())
        throw std::runtime_error("CV đang trống.");
    auto id = service_.db.save("resume_drafts",
                               {{"name", name_->text().trimmed().isEmpty() ? "CV" : name_->text()},
                                {"job_listing_id", QJsonValue::fromVariant(jobs_->currentData())},
                                {"application_id", QJsonValue::fromVariant(applications_->currentData())},
                                {"job_snapshot", jobSnapshot()},
                                {"body", text_->toPlainText()},
                                {"photo_base64", photo_},
                                {"sources", provenance_},
                                {"reviewed", reviewed_->isChecked() ? 1 : 0}});
    savedFingerprint_ = fingerprint();
    return id;
}
QByteArray ResumeDialog::fingerprint() const {
    auto content =
        QJsonDocument(QJsonObject{{"name", name_->text()},
                                  {"body", text_->toPlainText()},
                                  {"photo", photo_},
                                  {"reviewed", reviewed_->isChecked()},
                                  {"job", jobSnapshot()},
                                  {"application", QJsonValue::fromVariant(applications_->currentData())},
                                  {"sources", provenance_}})
            .toJson(QJsonDocument::Compact);
    return QCryptographicHash::hash(content, QCryptographicHash::Sha256);
}
void ResumeDialog::reject() {
    try {
        if (!text_->toPlainText().trimmed().isEmpty() && fingerprint() != savedFingerprint_)
            saveDraft();
        QDialog::reject();
    } catch (const std::exception& e) {
        diagnostics_->setText(QString::fromUtf8(e.what()));
    }
}
QByteArray ResumeDialog::exportBytes(const QString& format) const {
    if (!reviewed_->isChecked() || text_->toPlainText().trimmed().isEmpty())
        throw std::runtime_error("Kiểm tra CV và đánh dấu xác nhận trước khi xuất.");
    if (format == "TXT")
        return text_->toPlainText().toUtf8();
    auto html = ResumeBuilder::html(text_->toPlainText(), photo_);
    if (format == "HTML")
        return html.toUtf8();
    if (format != "PDF")
        throw std::runtime_error("Định dạng CV không được hỗ trợ.");
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    {
        QPdfWriter writer(&buffer);
        writer.setResolution(96);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(17, 15, 17, 15));
        writer.setTitle(name_->text());
        writer.setCreator("PathWeave");
        QTextDocument doc;
        doc.documentLayout()->setPaintDevice(&writer);
        doc.setHtml(html);
        doc.setDocumentMargin(0);
        doc.setPageSize(QSizeF(writer.width(), writer.height()));
        doc.print(&writer);
    }
    if (!bytes.startsWith("%PDF"))
        throw std::runtime_error("Không tạo được PDF.");
    return bytes;
}
void ResumeDialog::attach() {
    try {
        auto id = applications_->currentData().toLongLong();
        if (!id)
            throw std::runtime_error("Chọn hồ sơ ứng tuyển trước.");
        auto a = service_.db.get("applications", id);
        auto job = jobs_->currentData().toLongLong();
        if (job && a["job_listing_id"].toInteger() != job)
            throw std::runtime_error("CV và hồ sơ ứng tuyển đang chọn thuộc hai công việc khác nhau.");
        auto bytes = exportBytes("PDF");
        if (!service_.db.begin())
            throw std::runtime_error("Không bắt đầu được giao dịch.");
        try {
            saveDraft();
            auto doc = service_.db.save(
                "documents",
                {{"name", name_->text() + ".pdf"},
                 {"kind", "resume"},
                 {"content_base64", QString::fromLatin1(bytes.toBase64())},
                 {"sha256",
                  QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}});
            service_.db.save("application_documents", {{"application_id", id}, {"document_id", doc}});
            service_.db.save("applications", {{"resume_document_id", doc}}, id);
            if (!service_.db.commit())
                throw std::runtime_error("Không lưu được CV.");
        } catch (...) {
            service_.db.rollback();
            savedFingerprint_.clear();
            throw;
        }
        diagnostics_->setText("Đã gắn phiên bản PDF vào hồ sơ. Chưa gửi cho nhà tuyển dụng.");
    } catch (const std::exception& e) {
        diagnostics_->setText(QString::fromUtf8(e.what()));
    }
}
} // namespace pw
