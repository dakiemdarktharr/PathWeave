#include "EntityEditor.h"
#include <QJsonDocument>
#include <QtWidgets>
#include <cmath>
#include <stdexcept>
namespace pw {
FieldForm::FieldForm(const QVector<Field>& fields, Database* db, const QJsonObject& data, QWidget* parent)
    : QWidget(parent), fields_(fields) {
    auto* layout = new QFormLayout(this);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setSpacing(12);
    for (const auto& f : fields) {
        if (f.name == "content_base64" || f.name == "sha256")
            continue;
        QWidget* w = nullptr;
        if (!f.reference.isEmpty() && db) {
            auto* c = new QComboBox;
            c->addItem("— Chưa liên kết —", QVariant());
            for (const auto& v : db->all(f.reference)) {
                auto row = v.toObject();
                QString name;
                for (auto key : {"name", "title", "company_name", "display_name", "round"})
                    if (!row[key].toString().isEmpty()) {
                        name = row[key].toString();
                        break;
                    }
                c->addItem(QString("#%1 · %2").arg(row["id"].toInteger()).arg(name), row["id"].toInteger());
            }
            w = c;
            connect(c, &QComboBox::currentIndexChanged, this, &FieldForm::changed);
        } else if (f.type == "BOOL") {
            auto* c = new QCheckBox("Có");
            w = c;
            connect(c, &QCheckBox::toggled, this, &FieldForm::changed);
        } else if (f.type == "MULTI") {
            auto* list = new QListWidget;
            list->setMaximumHeight(132);
            for (const auto& option : f.options.split('|')) {
                auto* item = new QListWidgetItem(displayValue(option), list);
                item->setData(Qt::UserRole, option);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
            }
            w = list;
            connect(list, &QListWidget::itemChanged, this, &FieldForm::changed);
        } else if (!f.options.isEmpty()) {
            auto* c = new QComboBox;
            c->addItem("— Chọn —", QVariant());
            for (auto v : f.options.split('|'))
                c->addItem(displayValue(v), v);
            w = c;
            connect(c, &QComboBox::currentIndexChanged, this, &FieldForm::changed);
        } else if (f.type == "LONG" || f.type == "JSON") {
            auto* e = new QPlainTextEdit;
            e->setMaximumHeight(110);
            w = e;
            connect(e, &QPlainTextEdit::textChanged, this, &FieldForm::changed);
        } else {
            auto* e = new QLineEdit;
            if (f.type == "DATE")
                e->setPlaceholderText("YYYY-MM-DD (để trống nếu chưa rõ)");
            if (f.type == "DATETIME")
                e->setPlaceholderText("YYYY-MM-DDThh:mm:ss+07:00");
            if (f.type == "INTEGER" || f.type == "REAL")
                e->setPlaceholderText("Không âm; để trống nếu chưa rõ");
            w = e;
            connect(e, &QLineEdit::textChanged, this, &FieldForm::changed);
        }
        w->setObjectName(f.name);
        w->setAccessibleName(f.label);
        widgets_[f.name] = w;
        layout->addRow(f.label + (f.required ? " *" : ""), w);
    }
    setValues(data);
}
void FieldForm::setValues(const QJsonObject& data) {
    for (const auto& f : fields_) {
        if (!widgets_.contains(f.name))
            continue;
        auto* w = widgets_[f.name];
        auto v = data[f.name];
        if (auto* e = qobject_cast<QLineEdit*>(w))
            e->setText(v.toVariant().toString());
        else if (auto* e = qobject_cast<QPlainTextEdit*>(w))
            e->setPlainText(v.isObject() ? QString::fromUtf8(QJsonDocument(v.toObject()).toJson())
                                         : v.toString());
        else if (auto* c = qobject_cast<QComboBox*>(w)) {
            int i = c->findData(v.toVariant());
            c->setCurrentIndex(i < 0 ? 0 : i);
        } else if (auto* c = qobject_cast<QCheckBox*>(w))
            c->setChecked(v.toVariant().toBool());
        else if (auto* l = qobject_cast<QListWidget*>(w))
            for (int i = 0; i < l->count(); ++i)
                l->item(i)->setCheckState(
                    v.toString().split(',').contains(l->item(i)->data(Qt::UserRole).toString())
                        ? Qt::Checked
                        : Qt::Unchecked);
    }
}
QJsonObject FieldForm::values() const {
    QJsonObject out;
    for (const auto& f : fields_) {
        if (!widgets_.contains(f.name))
            continue;
        QWidget* w = widgets_[f.name];
        QJsonValue v;
        if (auto* e = qobject_cast<QLineEdit*>(w)) {
            QString text = e->text().trimmed();
            if (!text.isEmpty()) {
                if (f.type == "REAL" || f.type == "INTEGER") {
                    bool ok = false;
                    double n = text.toDouble(&ok);
                    if (!ok || n < 0 || (f.type == "INTEGER" && n != std::floor(n)))
                        throw std::runtime_error((f.label + ": số không hợp lệ").toStdString());
                    v = n;
                } else {
                    if (f.type == "DATE" && !QDate::fromString(text, Qt::ISODate).isValid())
                        throw std::runtime_error((f.label + ": ngày không hợp lệ").toStdString());
                    if (f.type == "DATETIME" && !QDateTime::fromString(text, Qt::ISODate).isValid())
                        throw std::runtime_error((f.label + ": thời gian không hợp lệ").toStdString());
                    v = text;
                }
            }
        } else if (auto* e = qobject_cast<QPlainTextEdit*>(w)) {
            if (f.type == "JSON" && !e->toPlainText().trimmed().isEmpty()) {
                QJsonParseError err;
                auto d = QJsonDocument::fromJson(e->toPlainText().toUtf8(), &err);
                if (err.error != QJsonParseError::NoError || !d.isObject())
                    throw std::runtime_error("JSON object required");
                v = d.object();
            } else
                v = e->toPlainText();
        } else if (auto* c = qobject_cast<QComboBox*>(w))
            v = QJsonValue::fromVariant(c->currentData());
        else if (auto* c = qobject_cast<QCheckBox*>(w))
            v = c->isChecked() ? 1 : 0;
        else if (auto* l = qobject_cast<QListWidget*>(w)) {
            QStringList selected;
            for (int i = 0; i < l->count(); ++i)
                if (l->item(i)->checkState() == Qt::Checked)
                    selected << l->item(i)->data(Qt::UserRole).toString();
            v = selected.join(',');
        }
        if (f.required && (v.isNull() || v.toVariant().toString().trimmed().isEmpty()))
            throw std::runtime_error(("Vui lòng nhập " + f.label).toStdString());
        out[f.name] = v;
    }
    return out;
}
EntityEditor::EntityEditor(CareerService& service, QString table, qint64 id, QWidget* parent,
                           QJsonObject initial)
    : QDialog(parent), service_(service), table_(std::move(table)), id_(id) {
    setWindowTitle((id ? "Chỉnh sửa · " : "Thêm · ") + entity(table_).label);
    resize(680, 730);
    auto* l = new QVBoxLayout(this);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    form = new FieldForm(entity(table_).fields, &service.db, id ? service.db.get(table_, id) : initial);
    scroll->setWidget(form);
    l->addWidget(scroll);
    error_ = new QLabel;
    error_->setWordWrap(true);
    error_->setStyleSheet("color:#c34545");
    l->addWidget(error_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("Lưu");
    buttons->button(QDialogButtonBox::Save)->setObjectName("saveButton");
    buttons->button(QDialogButtonBox::Cancel)->setText("Hủy");
    l->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &EntityEditor::submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
void EntityEditor::submit() {
    try {
        savedId = service_.save(table_, form->values(), id_);
        accept();
    } catch (const std::exception& e) {
        errorMessage = QString::fromUtf8(e.what());
        error_->setText(QString::fromUtf8(e.what()));
    }
}
} // namespace pw
