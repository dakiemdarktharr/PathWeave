#include "SurveyDialog.h"
#include <QtWidgets>
namespace pw {
SurveyDialog::SurveyDialog(CareerService& s, QWidget* parent) : QDialog(parent), service_(s) {
    setWindowTitle("Chào mừng đến PathWeave");
    resize(760, 780);
    auto* l = new QVBoxLayout(this);
    auto* title =
        new QLabel("<h1>Con đường tiếp theo, bắt đầu từ hôm nay.</h1><p>Hồ sơ chỉ lưu trên máy. Không cần "
                   "tài khoản. Bạn có thể bỏ qua và chỉnh sửa sau.</p><p><b>Loại việc</b> và <b>hình thức "
                   "làm việc</b> độc lập — ví dụ bán thời gian + từ xa.</p>");
    title->setWordWrap(true);
    l->addWidget(title);
    progress_ = new QProgressBar;
    l->addWidget(progress_);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    form = new FieldForm(surveyFields(), nullptr, s.profile());
    scroll->setWidget(form);
    l->addWidget(scroll);
    error_ = new QLabel;
    error_->setWordWrap(true);
    l->addWidget(error_);
    auto* buttons = new QDialogButtonBox;
    auto* save = buttons->addButton("Lưu hồ sơ", QDialogButtonBox::AcceptRole);
    save->setObjectName("surveySave");
    auto* skipButton = buttons->addButton("Bỏ qua lúc này", QDialogButtonBox::RejectRole);
    skipButton->setObjectName("surveySkip");
    l->addWidget(buttons);
    connect(save, &QPushButton::clicked, this, &SurveyDialog::submit);
    connect(skipButton, &QPushButton::clicked, this, &SurveyDialog::skip);
    connect(form, &FieldForm::changed, this, [this] {
        try {
            progress_->setValue(profileCompletion(form->values()));
        } catch (...) {
        }
    });
    progress_->setValue(profileCompletion(s.profile()));
}
void SurveyDialog::submit() {
    try {
        auto data = form->values();
        auto errors = validateSurvey(data);
        if (!errors.isEmpty()) {
            error_->setText(errors.join('\n'));
            return;
        }
        service_.saveProfile(data);
        accept();
    } catch (const std::exception& e) {
        error_->setText(QString::fromUtf8(e.what()));
    }
}
void SurveyDialog::skip() {
    service_.saveProfile(service_.profile(), true);
    reject();
}
} // namespace pw
