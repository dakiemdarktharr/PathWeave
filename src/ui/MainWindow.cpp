#include "MainWindow.h"
#include "EntityEditor.h"
#include "SurveyDialog.h"
#include "ResumeDialog.h"
#include "StatusBoard.h"
#include "services/BackupCrypto.h"
#include "services/DiscoveryPolicy.h"
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUrlQuery>
#include <QtWidgets>
namespace pw {
static QStringList pages = {
    "Tổng quan", "Khám phá việc làm", "Việc đã lưu", "Ứng tuyển", "Công việc hiện tại",
    "Nguồn tuyển dụng", "Cài đặt"};
static QPushButton* button(QString label, QBoxLayout* l, std::function<void()> action) {
    auto* b = new QPushButton(label);
    b->setCursor(Qt::PointingHandCursor);
    l->addWidget(b);
    QObject::connect(b, &QPushButton::clicked, b, std::move(action));
    return b;
}
static QTextBrowser* browser() {
    auto* b = new QTextBrowser;
    b->setOpenExternalLinks(false);
    b->setOpenLinks(false);
    b->setFrameShape(QFrame::NoFrame);
    return b;
}
static QString esc(const QJsonValue& v) {
    return v.toVariant().toString().toHtmlEscaped();
}
MainWindow::MainWindow(CareerService& s, SecretStore& secrets, QWidget* parent)
    : QMainWindow(parent), service_(s), secrets_(secrets), network_(s.db, this) {
    setWindowTitle("PathWeave · Turn today’s work into tomorrow’s opportunity.");
    resize(1420, 920);
    setMinimumSize(1050, 700);
    setWindowIcon(QIcon(":/icon.svg"));
    auto* root = new QWidget;
    setCentralWidget(root);
    auto* outer = new QHBoxLayout(root);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto* sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(230);
    auto* sl = new QVBoxLayout(sidebar);
    sl->setContentsMargins(18, 26, 18, 20);
    auto* logo = new QLabel("◈  PathWeave");
    logo->setObjectName("logo");
    sl->addWidget(logo);
    auto* tag = new QLabel("CÔNG VIỆC HÔM NAY\nCƠ HỘI NGÀY MAI");
    tag->setObjectName("tagline");
    sl->addWidget(tag);
    sl->addSpacing(24);
    nav_ = new QListWidget;
    nav_->setObjectName("navigation");
    nav_->addItems(pages);
    sl->addWidget(nav_, 1);
    auto* privateLabel = new QLabel("◉  Dữ liệu trên máy của bạn\nKhông tài khoản · Không telemetry");
    privateLabel->setWordWrap(true);
    privateLabel->setObjectName("privacyLabel");
    sl->addWidget(privateLabel);
    outer->addWidget(sidebar);
    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(30, 22, 30, 22);
    rl->setSpacing(16);
    auto* top = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setObjectName("globalSearch");
    search_->setPlaceholderText("Tìm việc, công ty hoặc ghi chú…  Ctrl+K");
    top->addWidget(search_, 1);
    netStatus_ = new QLabel;
    top->addWidget(netStatus_);
    rl->addLayout(top);
    heading_ = new QLabel;
    heading_->setObjectName("pageHeading");
    rl->addWidget(heading_);
    body_ = new QWidget;
    content_ = new QVBoxLayout(body_);
    content_->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(body_, 1);
    outer->addWidget(right, 1);
    auto* shortcut = new QShortcut(QKeySequence("Ctrl+K"), this);
    connect(shortcut, &QShortcut::activated, search_, qOverload<>(&QWidget::setFocus));
    connect(nav_, &QListWidget::currentRowChanged, this, &MainWindow::navigate);
    connect(search_, &QLineEdit::returnPressed, this, [this] {
        try {
            auto rows = service_.db.search(search_->text());
            QDialog d(this);
            d.setWindowTitle("Kết quả tìm kiếm cục bộ");
            d.resize(850, 600);
            auto* l = new QVBoxLayout(&d);
            auto* list = new QListWidget;
            l->addWidget(list);
            for (auto v : rows) {
                auto r = v.toObject();
                auto* item = new QListWidgetItem(
                    entity(r["entity"].toString()).label + " · " + r["content"].toString().left(180), list);
                item->setData(Qt::UserRole, r);
            }
            if (rows.isEmpty())
                list->addItem("Không tìm thấy. Thử một từ hoặc cụm từ khác.");
            connect(list, &QListWidget::itemDoubleClicked, &d, [&](QListWidgetItem* item) {
                auto r = item->data(Qt::UserRole).toJsonObject();
                if (r.isEmpty())
                    return;
                edit(r["entity"].toString(), r["entity_id"].toInteger());
            });
            d.exec();
        } catch (const std::exception& e) {
            showError(e);
        }
    });
    connect(&network_, &NetworkService::activity, this, [this](QString m) { netStatus_->setText(m); });
    tray_ = new QSystemTrayIcon(windowIcon(), this);
    auto* trayMenu = new QMenu(this);
    auto* show = trayMenu->addAction("Mở PathWeave");
    auto* quit = trayMenu->addAction("Thoát PathWeave");
    connect(show, &QAction::triggered, this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    connect(quit, &QAction::triggered, this, [this] {
        quitting_ = true;
        close();
    });
    tray_->setContextMenu(trayMenu);
    tray_->setToolTip("PathWeave");
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick)
            showNormal();
    });
    if (QSystemTrayIcon::isSystemTrayAvailable())
        tray_->show();
    ensureOnlineDefaults();
    applyTheme();
    nav_->setCurrentRow(0);
}
MainWindow::~MainWindow() {
    cancelSync();
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (!quitting_ && service_.db.setting("background_enabled") == "true" &&
        QSystemTrayIcon::isSystemTrayAvailable()) {
        hide();
        event->ignore();
        return;
    }
    cancelSync();
    event->accept();
}
void MainWindow::showError(const std::exception& e) {
    QMessageBox::warning(this, "PathWeave", QString::fromUtf8(e.what()));
}
void MainWindow::applyTheme() {
    bool dark = service_.db.setting("theme", "light") == "dark";
    QString bg = dark ? "#131c2b" : "#f3f6fa", panel = dark ? "#1d293b" : "#ffffff",
            fg = dark ? "#ecf2fa" : "#17263a", line = dark ? "#34435c" : "#dbe3ed";
    qApp->setStyle("Fusion");
    QPalette p;
    p.setColor(QPalette::Window, QColor(bg));
    p.setColor(QPalette::WindowText, QColor(fg));
    p.setColor(QPalette::Base, QColor(panel));
    p.setColor(QPalette::AlternateBase, QColor(bg));
    p.setColor(QPalette::Text, QColor(fg));
    p.setColor(QPalette::Button, QColor(panel));
    p.setColor(QPalette::ButtonText, QColor(fg));
    p.setColor(QPalette::Highlight, QColor("#167e80"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    qApp->setPalette(p);
    qApp->setStyleSheet(
        QString(
            "QWidget{font-family:'Segoe UI';font-size:10pt;color:%1;} QMainWindow,QDialog{background:%2;} "
            "QWidget#sidebar{background:#142a3a;} QLabel#logo{color:#f5fbff;font-size:24px;font-weight:700;} "
            "QLabel#tagline,QLabel#privacyLabel{color:#a1bdcc;font-size:10px;line-height:1.5;} "
            "QListWidget#navigation{background:transparent;border:0;color:#c6d7e2;outline:0;} "
            "QListWidget#navigation::item{padding:11px 9px;border-radius:6px;margin-bottom:3px;} "
            "QListWidget#navigation::item:selected{background:#245969;color:#ffffff;font-weight:600;} "
            "QLabel#pageHeading{font-size:28px;font-weight:700;} QPushButton{background:%3;border:1px solid "
            "%4;border-radius:6px;padding:9px 13px;} QPushButton:hover{border-color:#168886;} "
            "QPushButton:focus{border:2px solid #168886;} "
            "QPushButton#primary{background:#137c7e;color:white;border:0;} "
            "QLineEdit,QPlainTextEdit,QComboBox,QSpinBox{background:%3;border:1px solid "
            "%4;border-radius:5px;padding:7px;selection-background-color:#167e80;} "
            "QTableWidget,QTextBrowser,QListWidget,QScrollArea{background:%3;border:1px solid "
            "%4;border-radius:7px;} QTableWidget::item{padding:7px;} "
            "QHeaderView::section{background:%2;padding:9px;border:0;border-bottom:1px solid "
            "%4;font-weight:600;} QTabWidget::pane{border:0;} QTabBar::tab{padding:10px 14px;} "
            "QTabBar::tab:selected{color:#148481;border-bottom:3px solid #148481;} "
            "QGroupBox{background:%3;border:1px solid %4;border-radius:8px;margin-top:14px;padding:18px;} "
            "QGroupBox::title{subcontrol-origin:margin;left:14px;padding:0 5px;font-weight:600;} "
            "QProgressBar{border:0;background:%4;border-radius:4px;text-align:center;min-height:18px;} "
            "QProgressBar::chunk{background:#39a69b;border-radius:4px;}")
            .arg(fg, bg, panel, line));
}
void MainWindow::navigate(int page) {
    if (page < 0)
        return;
    page_ = page;
    refresh();
    if (page == 1)
        QTimer::singleShot(0, this, [this] {
            if (page_ == 1 && !syncing_)
                syncSources();
        });
}
void MainWindow::refresh() {
    while (auto* item = content_->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->setParent(nullptr);
            item->widget()->deleteLater();
        }
        delete item;
    }
    heading_->setText(pages.value(page_));
    netStatus_->setText(service_.db.setting("online_enabled") == "true" ? "◉ Trực tuyến · nguồn do bạn chọn"
                                                                        : "◉ Ngoại tuyến · dữ liệu cục bộ");
    try {
        QWidget* w = nullptr;
        switch (page_) {
        case 0:
            w = dashboard();
            break;
        case 1:
            w = discover();
            break;
        case 2:
            w = discover(true);
            break;
        case 3: {
            auto* t = new QTabWidget;
            t->addTab(entityPage("applications"), "Hồ sơ");
            t->addTab(entityPage("interviews"), "Phỏng vấn");
            t->addTab(entityPage("documents"), "Tài liệu");
            t->addTab(entityPage("resume_drafts"), "CV theo công việc");
            t->addTab(entityPage("application_documents"), "Gắn tài liệu");
            w = t;
            break;
        }
        case 4:
            w = entityPage("current_roles");
            break;
        case 5:
            w = sources();
            break;
        case 6:
            w = settings();
            break;}
        if (w)
            content_->addWidget(w);
    } catch (const std::exception& e) {
        content_->addWidget(new QLabel(QString::fromUtf8(e.what())));
    }
}
void MainWindow::edit(const QString& table, qint64 id, QJsonObject initial) {
    if (table == "resume_drafts") {
        ResumeDialog d(service_, 0, 0, id, this);
        d.exec();
        refresh();
        return;
    }
    EntityEditor d(service_, table, id, this, initial);
    if (d.exec() == QDialog::Accepted) {
        if (table == "job_listings" && !id)
            service_.saveJob(d.savedId);
        refresh();
    }
}
QWidget* MainWindow::entityPage(const QString& name) {
    auto* page = new QWidget;
    auto* l = new QVBoxLayout(page);
    l->setContentsMargins(0, 10, 0, 0);
    auto* bar = new QHBoxLayout;
    button("+ Thêm " + entity(name).label.toLower(), bar, [this, name] {
        edit(name);
    })->setObjectName("primary");
    auto* search = new QLineEdit;
    search->setPlaceholderText("Lọc danh sách…");
    bar->addWidget(search, 1);
    auto* mode = new QComboBox;
    mode->addItems({"Danh sách", "Bảng trạng thái", "Lịch hạn chót"});
    if (name == "tasks" || name == "projects")
        bar->addWidget(mode);
    else
        mode->hide();
    l->addLayout(bar);
    auto* filters = new QHBoxLayout;
    auto* date = new QComboBox;
    date->addItems({"Mọi ngày", "Hôm nay", "Quá hạn", "Tuần này"});
    auto* status = new QComboBox;
    status->addItem("Mọi trạng thái", "");
    auto* priority = new QComboBox;
    priority->addItem("Mọi ưu tiên", "");
    auto* project = new QComboBox;
    project->addItem("Mọi dự án", 0);
    auto* role = new QComboBox;
    role->addItem("Mọi công việc", 0);
    auto* application = new QComboBox;
    application->addItem("Mọi hồ sơ", 0);
    for (const auto& f : entity(name).fields)
        if (f.name == "status")
            for (const auto& v : f.options.split('|'))
                status->addItem(displayValue(v), v);
    for (auto v : {"low", "medium", "high", "urgent"})
        priority->addItem(displayValue(v), v);
    for (auto v : service_.db.all("projects")) {
        auto r = v.toObject();
        project->addItem(r["name"].toString(), r["id"].toInt());
    }
    for (auto v : service_.db.all("current_roles")) {
        auto r = v.toObject();
        role->addItem(r["company_name"].toString(), r["id"].toInt());
    }
    for (auto v : service_.db.all("applications")) {
        auto r = v.toObject();
        application->addItem(r["company"].toString() + " · " + r["title"].toString(), r["id"].toInt());
    }
    if (name == "tasks") {
        for (auto* c : {date, status, priority, project, role, application})
            filters->addWidget(c);
        l->addLayout(filters);
    } else {
        delete filters;
        for (auto* c : {date, status, priority, project, role, application})
            c->setParent(page);
        for (auto* c : {date, status, priority, project, role, application})
            c->hide();
    }
    auto* tabs = new QStackedWidget;
    auto* table = new QTableWidget;
    table->setObjectName(name + "Table");
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    tabs->addWidget(table);
    auto* board = new QWidget;
    auto* boardLayout = new QHBoxLayout(board);
    tabs->addWidget(board);
    auto* cal = new QCalendarWidget;
    tabs->addWidget(cal);
    l->addWidget(tabs, 1);
    auto populate = [=, this] {
        auto rows = service_.db.all(name);
        QJsonArray filtered;
        QString text = search->text();
        QString dateField = name == "projects" ? "target_date" : "due_date";
        for (auto v : rows) {
            auto r = v.toObject();
            if (!QString::fromUtf8(QJsonDocument(r).toJson()).contains(text, Qt::CaseInsensitive))
                continue;
            if (name == "tasks") {
                QString day = r[dateField].toString();
                QDate d = QDate::fromString(day, Qt::ISODate), today = QDate::currentDate();
                if (date->currentIndex() == 1 && d != today)
                    continue;
                if (date->currentIndex() == 2 &&
                    (!d.isValid() || d >= today || r["status"] == "completed" || r["status"] == "cancelled"))
                    continue;
                if (date->currentIndex() == 3 && (!d.isValid() || d < today.addDays(1 - today.dayOfWeek()) ||
                                                  d > today.addDays(7 - today.dayOfWeek())))
                    continue;
                if (!status->currentData().toString().isEmpty() &&
                    r["status"] != status->currentData().toString())
                    continue;
                if (!priority->currentData().toString().isEmpty() &&
                    r["priority"] != priority->currentData().toString())
                    continue;
                for (auto pair :
                     {qMakePair(project, QString("project_id")), qMakePair(role, QString("current_role_id")),
                      qMakePair(application, QString("application_id"))})
                    if (pair.first->currentData().toInt() &&
                        r[pair.second].toInt() != pair.first->currentData().toInt())
                        goto skip;
            }
            filtered.append(r);
        skip:;
        }
        QVector<Field> cols;
        for (const auto& f : entity(name).fields)
            if (f.type != "LONG" && f.type != "JSON" && f.name != "normalized_key" &&
                f.name != "raw_payload_hash" && cols.size() < 8)
                cols << f;
        table->setColumnCount(cols.size() + 1);
        QStringList labels = {"ID"};
        for (const auto& f : cols)
            labels << f.label;
        table->setHorizontalHeaderLabels(labels);
        table->setRowCount(filtered.size());
        for (int i = 0; i < filtered.size(); ++i) {
            auto r = filtered[i].toObject();
            table->setItem(i, 0, new QTableWidgetItem(QString::number(r["id"].toInteger())));
            for (int c = 0; c < cols.size(); ++c) {
                auto f = cols[c];
                QString value = r[f.name].toVariant().toString();
                if (!f.reference.isEmpty() && !r[f.name].isNull()) {
                    auto related = service_.db.get(f.reference, r[f.name].toInteger());
                    value = related["name"].toString(
                        related["title"].toString(related["company_name"].toString("#" + value)));
                } else
                    value = displayValue(value);
                table->setItem(i, c + 1, new QTableWidgetItem(value));
            }
            table->setRowHeight(i, 42);
        }
        table->resizeColumnsToContents();
        while (auto* item = boardLayout->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        QMap<QString, QListWidget*> lists;
        if (name == "tasks" || name == "projects") {
            for (const auto& f : entity(name).fields)
                if (f.name == "status")
                    for (auto key : f.options.split('|')) {
                        auto* group = new QGroupBox(displayValue(key));
                        auto* gl = new QVBoxLayout(group);
                        auto* list = new StatusColumn(service_, name, key);
                        gl->addWidget(list);
                        boardLayout->addWidget(group);
                        lists[key] = list;
                        connect(list, &QListWidget::itemDoubleClicked, page,
                                [this, name](QListWidgetItem* item) {
                                    edit(name, item->data(Qt::UserRole).toLongLong());
                                });
                    }
        }
        for (auto v : filtered) {
            auto r = v.toObject();
            QString key = r["status"].toString("unknown");
            if (!lists.contains(key)) {
                auto* group = new QGroupBox(displayValue(key));
                auto* gl = new QVBoxLayout(group);
                auto* list = new QListWidget;
                gl->addWidget(list);
                boardLayout->addWidget(group);
                lists[key] = list;
                connect(list, &QListWidget::itemDoubleClicked, page, [this, name](QListWidgetItem* item) {
                    edit(name, item->data(Qt::UserRole).toLongLong());
                });
            }
            auto* item = new QListWidgetItem(
                r["title"].toString(r["name"].toString()) + "\n" + r[dateField].toString(), lists[key]);
            item->setData(Qt::UserRole, r["id"].toInteger());
            item->setSizeHint(QSize(180, 72));
            QDate d = QDate::fromString(r[dateField].toString(), Qt::ISODate);
            if (d.isValid()) {
                QTextCharFormat fmt;
                fmt.setBackground(QColor("#41b4a6"));
                fmt.setForeground(Qt::black);
                cal->setDateTextFormat(d, fmt);
            }
        }
        if (filtered.isEmpty())
            table->setToolTip("Chưa có dữ liệu. Dùng nút Thêm để bắt đầu.");
    };
    populate();
    connect(search, &QLineEdit::textChanged, page, populate);
    for (auto* c : {date, status, priority, project, role, application})
        connect(c, &QComboBox::currentIndexChanged, page, populate);
    connect(mode, &QComboBox::currentIndexChanged, tabs, &QStackedWidget::setCurrentIndex);
    connect(table, &QTableWidget::cellDoubleClicked, page,
            [this, table, name](int r, int) { edit(name, table->item(r, 0)->text().toLongLong()); });
    connect(cal, &QCalendarWidget::activated, page, [=, this](QDate d) {
        for (auto v : service_.db.all(name)) {
            auto r = v.toObject();
            if (r[name == "projects" ? "target_date" : "due_date"].toString() == d.toString(Qt::ISODate)) {
                edit(name, r["id"].toInteger());
                break;
            }
        }
    });
    auto* actions = new QHBoxLayout;
    auto selected = [table]() -> qint64 {
        return table->currentRow() < 0 ? 0 : table->item(table->currentRow(), 0)->text().toLongLong();
    };
    if (name == "applications")
        button("Tạo CV theo JD", actions, [=, this] {
            if (auto id = selected()) {
                ResumeDialog d(service_, 0, id, 0, this);
                d.exec();
                refresh();
            }
        })->setObjectName("applicationResume");
    if (name == "saved_searches")
        button("Chạy tìm kiếm này", actions, [=, this] {
            if (auto id = selected())
                runSavedSearch(id);
        })->setObjectName("runSavedSearch");
    if (name == "documents") {
        button("Nhập tệp vào kho", actions, [this] {
            auto path = QFileDialog::getOpenFileName(this, "Nhập tài liệu", {},
                                                     "Tài liệu (*.pdf *.txt *.docx *.html *.png *.jpg)");
            if (path.isEmpty())
                return;
            try {
                QFile f(path);
                if (!f.open(QIODevice::ReadOnly) || f.size() > 20 * 1024 * 1024)
                    throw std::runtime_error("Tệp không đọc được hoặc quá 20 MiB.");
                auto data = f.readAll();
                service_.db.save(
                    "documents",
                    {{"name", QFileInfo(path).fileName()},
                     {"kind", "other"},
                     {"content_base64", QString::fromLatin1(data.toBase64())},
                     {"sha256", QString::fromLatin1(
                                    QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())}});
                refresh();
            } catch (const std::exception& e) {
                showError(e);
            }
        });
        button("Xuất tệp đã chọn", actions, [=, this] {
            if (auto id = selected()) {
                auto doc = service_.db.get("documents", id);
                auto data = QByteArray::fromBase64(doc["content_base64"].toString().toLatin1());
                if (data.isEmpty()) {
                    QMessageBox::information(
                        this, "Tài liệu",
                        "Tài liệu cũ chỉ có đường dẫn. Nhập tệp vào kho để sao lưu được nội dung.");
                    return;
                }
                if (QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex()) !=
                    doc["sha256"].toString()) {
                    QMessageBox::warning(this, "Tài liệu", "Kiểm tra toàn vẹn tệp thất bại.");
                    return;
                }
                auto path = QFileDialog::getSaveFileName(this, "Xuất tài liệu",
                                                         QFileInfo(doc["name"].toString()).fileName());
                if (path.isEmpty())
                    return;
                QSaveFile f(path);
                if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
                    QMessageBox::warning(this, "Tài liệu", "Không ghi được tệp.");
            }
        });
    }
    button("Chỉnh sửa", actions, [=, this] {
        if (auto id = selected())
            edit(name, id);
    });
    button("Xóa", actions, [=, this] {
        if (auto id = selected())
            if (QMessageBox::question(this, "Xóa bản ghi",
                                      "Xóa bản ghi đã chọn? Bản ghi sẽ được đánh dấu xóa.") ==
                QMessageBox::Yes) {
                service_.db.remove(name, id);
                refresh();
            }
    });
    if (name == "achievements")
        button("✦ Chuyển thành minh chứng nghề nghiệp", actions, [=, this] {
            if (auto id = selected()) {
                auto evidence = service_.convertEvidence(id);
                edit("career_evidence", evidence);
                refresh();
            }
        })->setObjectName("primary");
    if (name == "applications")
        button("Lịch sử trạng thái", actions, [=, this] {
            if (auto id = selected()) {
                QDialog d(this);
                d.setWindowTitle("Lịch sử hồ sơ #" + QString::number(id));
                d.resize(700, 450);
                auto* layout = new QVBoxLayout(&d);
                auto* b = browser();
                layout->addWidget(b);
                auto rows = service_.db.query("SELECT * FROM application_activities WHERE application_id=? "
                                              "AND deleted_at IS NULL ORDER BY date,id",
                                              {id});
                QString html = "<h2>Lịch sử ứng tuyển</h2>";
                for (auto v : rows) {
                    auto r = v.toObject();
                    html += "<p>" + esc(r["date"]) + " · " + esc(r["kind"]) + " · " + esc(r["from_status"]) +
                            " → " + esc(r["to_status"]) + "</p>";
                }
                if (rows.isEmpty())
                    html += "<p>Chưa có lịch sử.</p>";
                b->setHtml(html);
                d.exec();
            }
        });
    actions->addStretch();
    button("Xuất CSV", actions, [=, this] {
        QString path = QFileDialog::getSaveFileName(this, "Xuất CSV", name + ".csv", "CSV (*.csv)");
        if (path.isEmpty())
            return;
        QSaveFile f(path);
        if (f.open(QIODevice::WriteOnly)) {
            f.write("\xEF\xBB\xBF");
            f.write(csv(service_.db.all(name)).toUtf8());
            if (!f.commit())
                QMessageBox::warning(this, "Xuất CSV", "Không ghi được tệp.");
        }
    });
    l->addLayout(actions);
    return page;
}
QWidget* MainWindow::dashboard() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    scroll->setWidget(page);
    auto* l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 6, 0);
    l->setSpacing(18);

    const auto profile = service_.profile();
    const auto profileRows = service_.db.all("user_profile");
    const int completion = profileRows.isEmpty() ? 0 : profileRows[0].toObject()["completion"].toInt();
    auto* intro = new QLabel(
        "<h2>Chào " + profile["display_name"].toString("bạn").toHtmlEscaped() +
        ", sẵn sàng cho bước tiếp theo?</h2><p>Quản lý hồ sơ, tìm việc và ứng tuyển trong một không gian riêng tư.</p>");
    intro->setWordWrap(true);
    l->addWidget(intro);

    if (completion < 100) {
        auto* incomplete = new QHBoxLayout;
        auto* label = new QLabel(QString("Hồ sơ hoàn thành %1% · bổ sung thông tin để đề xuất việc phù hợp hơn.")
                                     .arg(completion));
        label->setWordWrap(true);
        incomplete->addWidget(label, 1);
        button("Hoàn thiện hồ sơ", incomplete, [this] {
            SurveyDialog d(service_, this);
            d.exec();
            refresh();
        });
        l->addLayout(incomplete);
    }

    auto scalar = [this](const QString& sql, const QVariantList& args = {}) {
        const auto rows = service_.db.query(sql, args);
        return rows.isEmpty() ? 0 : rows[0].toObject()["n"].toInt();
    };
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    auto* cards = new QHBoxLayout;
    auto card = [&](const QString& heading, const QString& text) {
        auto* group = new QGroupBox(heading);
        auto* layout = new QVBoxLayout(group);
        auto* label = new QLabel(text);
        label->setWordWrap(true);
        label->setTextFormat(Qt::RichText);
        layout->addWidget(label);
        cards->addWidget(group, 1);
    };

    const auto roles = service_.db.query(
        "SELECT * FROM current_roles WHERE is_current=1 AND deleted_at IS NULL ORDER BY id DESC");
    const QString role = roles.isEmpty()
                             ? "Chưa có công việc hiện tại"
                             : esc(roles[0].toObject()["job_title"]) + "<br>" +
                                   esc(roles[0].toObject()["company_name"]);
    card("01  CÔNG VIỆC HIỆN TẠI",
         "<h3>" + role + "</h3><p>Vai trò hiện tại là nền tảng để cá nhân hóa CV và đối chiếu việc làm.</p>");
    card("02  CƠ HỘI TIẾP THEO",
         QString("<h1>%1</h1><p>%2 tin mới · %3 việc đã lưu</p><p>%4 hồ sơ đang tiến triển · %5 phỏng vấn sắp tới</p>")
             .arg(scalar("SELECT COUNT(*) n FROM job_listings WHERE status='new' AND deleted_at IS NULL"))
             .arg(service_.db.count("saved_jobs"))
             .arg(scalar("SELECT COUNT(*) n FROM applications WHERE status NOT IN "
                         "('rejected','withdrawn','archived') AND deleted_at IS NULL"))
             .arg(scalar("SELECT COUNT(*) n FROM interviews WHERE scheduled_at>=? AND deleted_at IS NULL", {today})));
    const int skillCount = profile["hard_skills"].toString().split(QRegularExpression("[,;\\n]"),
                                                                     Qt::SkipEmptyParts)
                               .size() +
                           profile["soft_skills"].toString().split(QRegularExpression("[,;\\n]"),
                                                                     Qt::SkipEmptyParts)
                               .size();
    card("03  CV THEO JD",
         QString("<h1>%1</h1><p>%2 phiên bản CV đã lưu</p><p>CV được tạo từ dữ liệu hồ sơ bạn nhập và JD cụ thể.</p>")
             .arg(skillCount)
             .arg(service_.db.count("resume_drafts")));
    l->addLayout(cards);

    auto* quick = new QHBoxLayout;
    button("+ Công việc hiện tại", quick, [this] { edit("current_roles"); });
    button("Tìm việc trực tuyến", quick, [this] { nav_->setCurrentRow(1); });
    button("Lưu việc thủ công", quick, [this] { edit("job_listings"); });
    button("+ Ứng tuyển", quick, [this] { edit("applications"); });
    quick->addStretch();
    l->addLayout(quick);

    auto* applications = browser();
    QString html = "<h2>Ứng tuyển gần đây</h2>";
    int count = 0;
    for (auto v : service_.db.all("applications")) {
        if (count++ >= 6)
            break;
        const auto a = v.toObject();
        html += "<p><b>" + esc(a["title"]) + "</b> · " + esc(a["company"]) + " · " +
                displayValue(a["status"].toString()) + "</p>";
    }
    if (count == 0)
        html += "<p>Chưa có hồ sơ ứng tuyển. Hãy lưu một việc rồi tạo hồ sơ.</p>";
    applications->setHtml(html);
    applications->setMinimumHeight(240);
    l->addWidget(applications);

    auto* demo = new QHBoxLayout;
    auto* note = new QLabel("Dữ liệu minh họa được gắn nhãn DEMO và có thể xóa trong Cài đặt.");
    demo->addWidget(note, 1);
    button("Tải dữ liệu mẫu", demo, [this] {
        try {
            service_.loadDemo();
            refresh();
        } catch (const std::exception& e) {
            showError(e);
        }
    })->setObjectName("loadDemo");
    l->addLayout(demo);
    return scroll;
}
QJsonArray MainWindow::filteredJobs(const QString& type, const QString& mode, const QString& query) const {
    QJsonArray out;
    for (auto v : service_.db.all("job_listings")) {
        auto j = v.toObject();
        if (!type.isEmpty() && j["employment_type"] != type)
            continue;
        if (!mode.isEmpty() && j["workplace_mode"] != mode)
            continue;
        if (!query.trimmed().isEmpty() && !DiscoveryPolicy::visible(
                                              [&] {
                                                  auto candidate = j;
                                                  candidate["status"] = "new";
                                                  return candidate;
                                              }(),
                                              {}, query, {}, {}, false, 0, 100))
            continue;
        out.append(j);
    }
    return out;
}
QWidget* MainWindow::discover(bool saved) {
    auto* page = new QWidget;
    auto* l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabBar;
    tabs->addTab("Gợi ý");
    tabs->addTab("Toàn thời gian");
    tabs->addTab("Bán thời gian");
    tabs->addTab("Từ xa");
    tabs->addTab("Kết hợp");
    tabs->addTab("Mới hôm nay");
    tabs->setObjectName("jobViews");
    l->addWidget(tabs);
    auto* bar = new QHBoxLayout;
    auto* keyword = new QLineEdit;
    keyword->setObjectName("jobKeyword");
    keyword->setText(service_.db.setting("search_keyword"));
    keyword->setPlaceholderText("Chức danh, công ty hoặc từ khóa…");
    bar->addWidget(keyword, 2);
    auto* type = new QComboBox;
    type->setObjectName("employmentFilter");
    type->addItem("Mọi loại việc", "");
    for (auto s : {"full_time", "part_time", "contract", "internship", "temporary"})
        type->addItem(displayValue(s), s);
    bar->addWidget(type);
    auto* mode = new QComboBox;
    mode->setObjectName("workplaceFilter");
    mode->addItem("Mọi hình thức", "");
    for (auto s : {"remote", "hybrid", "on_site", "unknown"})
        mode->addItem(displayValue(s), s);
    bar->addWidget(mode);
    type->setCurrentIndex(std::max(0, type->findData(service_.db.setting("search_type"))));
    mode->setCurrentIndex(std::max(0, mode->findData(service_.db.setting("search_mode"))));
    auto* location = new QLineEdit(service_.db.setting("search_location"));
    location->setObjectName("jobLocation");
    location->setPlaceholderText("Địa điểm tìm trực tuyến");
    bar->addWidget(location);
    button("Tìm trực tuyến", bar, [this, keyword, type, mode, location] {
        service_.db.setSetting("search_type", type->currentData().toString());
        service_.db.setSetting("search_mode", mode->currentData().toString());
        service_.db.setSetting("search_location", location->text());
        service_.db.setSetting("search_keyword", keyword->text());
        syncSources();
    })->setObjectName("primary");
    button("Hủy tìm", bar, [this] { cancelSync(); });
    l->addLayout(bar);
    auto* splitter = new QSplitter;
    auto* list = new QListWidget;
    list->setObjectName("jobList");
    auto* details = browser();
    splitter->addWidget(list);
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    l->addWidget(splitter, 1);
    auto populate = [=, this] {
        list->clear();
        auto jobs =
            filteredJobs(type->currentData().toString(), mode->currentData().toString(), keyword->text());
        QList<QJsonObject> ranked;
        auto engine = service_.matcher();
        auto profile = service_.matchingProfile();
        QHash<qint64, MatchScore> scores;
        for (auto v : jobs) {
            auto j = v.toObject();
            auto id = j["id"].toInteger();
            if (saved) {
                auto links = service_.db.query(
                    "SELECT id FROM saved_jobs WHERE job_listing_id=? AND deleted_at IS NULL", {id});
                if (links.isEmpty())
                    continue;
            }
            auto score = engine.score(j, profile);
            scores[id] = score;
            double minimum = (!saved && tabs->currentIndex() == 0)
                                 ? service_.db.setting("minimum_match_score", "40").toDouble()
                                 : 0;
            if (!saved && !DiscoveryPolicy::visible(j, profile, {}, {}, {}, tabs->currentIndex() == 5,
                                                    minimum, score.total))
                continue;
            ranked << j;
        }
        std::stable_sort(ranked.begin(), ranked.end(), [&](const auto& a, const auto& b) {
            return scores.value(a["id"].toInteger()).total > scores.value(b["id"].toInteger()).total;
        });
        int maximum = service_.profile()["daily_limit"].toInt(50);
        if (maximum <= 0)
            maximum = 50;
        int n = 0;
        for (auto j : ranked) {
            if (!saved && tabs->currentIndex() == 0 && n++ >= maximum)
                break;
            auto score = scores.value(j["id"].toInteger());
            auto* item = new QListWidgetItem(QString("%1  ·  %2/100\n%3 · %4\n%5  /  %6\nNguồn: %7%8")
                                                 .arg(j["title"].toString())
                                                 .arg(qRound(score.total))
                                                 .arg(j["company"].toString(), j["location"].toString(),
                                                      displayValue(j["employment_type"].toString()),
                                                      displayValue(j["workplace_mode"].toString()),
                                                      j["source_id"].toString(),
                                                      j["is_demo"].toInt() ? " · DEMO" : ""),
                                             list);
            item->setData(Qt::UserRole, j);
            item->setSizeHint(QSize(370, 116));
        }
        if (list->count())
            list->setCurrentRow(0);
        else
            details->setHtml(
                "<h2>Chưa có việc phù hợp</h2><p>Thay đổi bộ lọc, lưu việc thủ công hoặc bật một nguồn công "
                "khai.</p><p>Nguồn chưa xác nhận hình thức từ xa sẽ hiển thị “Chưa rõ”.</p>");
    };
    connect(list, &QListWidget::currentItemChanged, page, [=, this](QListWidgetItem* item) {
        if (!item)
            return;
        auto j = item->data(Qt::UserRole).toJsonObject();
        auto s = service_.matcher().score(j, service_.matchingProfile());
        QString html = "<h1>" + esc(j["title"]) + "</h1><h3>" + esc(j["company"]) + " · " +
                       esc(j["location"]) + "</h3><p>" + displayValue(j["employment_type"].toString()) +
                       " · " + displayValue(j["workplace_mode"].toString()) +
                       "</p><p><b>Nguồn: " + esc(j["source_id"]) + "</b> · " + esc(j["canonical_url"]) +
                       "</p><p>Đăng: " + esc(j["posted_at"]) +
                       "<br>Cập nhật từ nguồn: " + esc(j["updated_at_source"]) +
                       "<br>Lấy dữ liệu: " + esc(j["fetched_at"]) + "</p>";
        auto fetched = QDateTime::fromString(j["fetched_at"].toString(), Qt::ISODate);
        if (fetched.isValid() && fetched.daysTo(QDateTime::currentDateTimeUtc()) > 1)
            html += "<p><b>Bản lưu có thể đã cũ. Xác minh tin trên nguồn gốc.</b></p>";
        html += "<p>Lương: " +
                (j["salary_min"].toDouble() > 0 ? esc(j["salary_min"]) + " – " + esc(j["salary_max"]) + " " +
                                                      esc(j["currency"]) + " / " + esc(j["salary_period"])
                                                : QString("Chưa có số liệu có cấu trúc")) +
                "</p><h2>Phù hợp " + QString::number(s.total, 'f', 0) +
                "/100</h2><p>Hỗ trợ đối chiếu sở thích; không dự đoán khả năng được tuyển.</p><table "
                "cellpadding='7'><tr><td>Chức danh</td><td>" +
                QString::number(s.title, 'f', 0) + "</td><td>Kỹ năng</td><td>" +
                QString::number(s.skill, 'f', 0) + "</td></tr><tr><td>Loại việc</td><td>" +
                QString::number(s.employmentType, 'f', 0) + "</td><td>Hình thức</td><td>" +
                QString::number(s.workplaceMode, 'f', 0) + "</td></tr><tr><td>Địa điểm</td><td>" +
                QString::number(s.location, 'f', 0) + "</td><td>Cấp bậc</td><td>" +
                QString::number(s.seniority, 'f', 0) + "</td></tr><tr><td>Lương</td><td>" +
                QString::number(s.salary, 'f', 0) + "</td><td>Độ mới (không tính mặc định)</td><td>" +
                QString::number(s.freshness, 'f', 0) + "</td></tr></table><h3>Lý do</h3>";
        for (auto reason : s.explanations)
            html += "<p>✓ " + reason.toHtmlEscaped() + "</p>";
        html += "<h3>Cần đối chiếu</h3>";
        for (auto reason : s.missingPreferences)
            html += "<p>· " + reason.toHtmlEscaped() + "</p>";
        html += "<hr><h3>Mô tả từ nguồn</h3><p>" + esc(j["description"]).replace('\n', "<br>") + "</p><p>" +
                esc(j["requirements"]).replace('\n', "<br>") + "</p>";
        details->setHtml(html);
    });
    connect(keyword, &QLineEdit::textChanged, page, populate);
    connect(type, &QComboBox::currentIndexChanged, page, populate);
    connect(mode, &QComboBox::currentIndexChanged, page, populate);
    connect(tabs, &QTabBar::currentChanged, page, [=, this](int i) {
        type->setCurrentIndex(0);
        mode->setCurrentIndex(0);
        if (i == 1 || i == 2)
            type->setCurrentIndex(i);
        if (i == 3 || i == 4)
            mode->setCurrentIndex(i - 2);
        populate();
    });
    auto* actions = new QHBoxLayout;
    auto selected = [list] {
        return list->currentItem() ? list->currentItem()->data(Qt::UserRole).toJsonObject() : QJsonObject();
    };
    button("Tạo CV theo JD", actions, [=, this] {
        auto j = selected();
        if (!j.isEmpty()) {
            ResumeDialog d(service_, j["id"].toInteger(), 0, 0, this);
            d.exec();
        }
    })->setObjectName("jobResume");
    button("Mở tin gốc", actions, [=] {
        auto j = selected();
        QUrl url(j["canonical_url"].toString());
        if (url.scheme() == "https" || url.scheme() == "http")
            QDesktopServices::openUrl(url);
    });
    button("♡ Lưu việc", actions, [=, this] {
        auto j = selected();
        if (!j.isEmpty()) {
            service_.saveJob(j["id"].toInteger());
            populate();
        }
    })->setObjectName("saveJob");
    button("Bỏ qua", actions, [=, this] {
        auto j = selected();
        if (!j.isEmpty()) {
            service_.db.save("job_listings", {{"status", "dismissed"}}, j["id"].toInteger());
            populate();
        }
    });
    button("Ẩn công ty", actions, [=, this] {
        auto j = selected();
        if (j.isEmpty())
            return;
        auto p = service_.profile();
        QString excluded = p["excluded_companies"].toString();
        p["excluded_companies"] = excluded + (excluded.isEmpty() ? "" : ",") + j["company"].toString();
        service_.saveProfile(p, true);
        for (auto v : service_.db.all("job_listings"))
            if (v.toObject()["company"] == j["company"])
                service_.db.save("job_listings", {{"status", "dismissed"}}, v.toObject()["id"].toInteger());
        populate();
    });
    button("+ Hồ sơ ứng tuyển", actions, [=, this] {
        auto j = selected();
        if (!j.isEmpty())
            edit("applications", service_.apply(j["id"].toInteger()));
    })->setObjectName("applyJob");
    l->addLayout(actions);
    auto* bottom = new QHBoxLayout;
    button("Lưu việc thủ công", bottom, [this] { edit("job_listings"); });
    button("Hủy kết nối", bottom, [this] { cancelSync(); });
    bottom->addStretch();
    l->addLayout(bottom);
    populate();
    return page;
}
void MainWindow::exportData() {
    auto path = QFileDialog::getSaveFileName(this, "Sao lưu gồm CV và tài liệu; không có khóa API",
                                             "PathWeave-backup.pwbackup",
                                             "Bản sao mã hóa (*.pwbackup);;JSON không mã hóa (*.json)");
    if (path.isEmpty())
        return;
    try {
        QByteArray bytes = QJsonDocument(service_.db.exportAll()).toJson();
        if (!path.endsWith(".json", Qt::CaseInsensitive)) {
            bool ok = false;
            auto pass = QInputDialog::getText(
                this, "Mật khẩu bản sao", "Ít nhất 12 ký tự. Cần giữ mật khẩu để khôi phục trên máy khác.",
                QLineEdit::Password, {}, &ok);
            if (!ok)
                return;
            auto confirm = QInputDialog::getText(this, "Nhập lại mật khẩu", "Nhập lại mật khẩu",
                                                 QLineEdit::Password, {}, &ok);
            if (!ok)
                return;
            if (pass != confirm)
                throw std::runtime_error("Hai mật khẩu không khớp.");
            bytes = BackupCrypto::encrypt(bytes, pass);
        }
        QSaveFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
            throw std::runtime_error("Không ghi được bản sao.");
        statusBar()->showMessage("Đã sao lưu: " + path, 7000);
    } catch (const std::exception& e) {
        showError(e);
    }
}
void MainWindow::importData() {
    auto path = QFileDialog::getOpenFileName(this, "Khôi phục bản sao", {}, "PathWeave (*.pwbackup *.json)");
    if (path.isEmpty())
        return;
    try {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly) || f.size() > 512 * 1024 * 1024 + 49)
            throw std::runtime_error("Không đọc được bản sao hoặc quá 512 MiB.");
        auto bytes = f.readAll();
        if (bytes.startsWith("PWBK1")) {
            bool ok = false;
            auto pass =
                QInputDialog::getText(this, "Giải mã bản sao", "Mật khẩu", QLineEdit::Password, {}, &ok);
            if (!ok)
                return;
            bytes = BackupCrypto::decrypt(bytes, pass);
        }
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            throw std::runtime_error("Bản sao JSON không hợp lệ.");
        if (QMessageBox::question(
                this, "Khôi phục",
                "Bản sao sẽ thay thế dữ liệu hiện tại. Đã sao lưu dữ liệu hiện tại và muốn tiếp tục?") !=
            QMessageBox::Yes)
            return;
        cancelSync();
        service_.db.importAll(doc.object());
        refresh();
    } catch (const std::exception& e) {
        showError(e);
    }
}
QWidget* MainWindow::settings() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* p = new QWidget;
    scroll->setWidget(p);
    auto* l = new QVBoxLayout(p);
    auto* local = new QGroupBox("Hồ sơ và giao diện");
    auto* ll = new QHBoxLayout(local);
    button("Chỉnh sửa khảo sát", ll, [this] {
        SurveyDialog d(service_, this);
        d.exec();
        refresh();
    });
    button("Sáng / Tối", ll, [this] {
        service_.db.setSetting("theme", service_.db.setting("theme") == "dark" ? "light" : "dark");
        applyTheme();
    });
    ll->addStretch();
    l->addWidget(local);
    auto* privacy = new QGroupBox("Quyền riêng tư");
    auto* pl = new QVBoxLayout(privacy);
    auto* explanation =
        new QLabel("Tìm việc trực tuyến được bật sẵn để nút Tìm trực tuyến hoạt động ngay. Chỉ từ khóa và địa "
                   "điểm tìm kiếm được gửi đến nguồn đã bật; CV, ghi chú công việc và hồ sơ ứng tuyển luôn ở trên máy. "
                   "Bạn có thể tắt mạng bất cứ lúc nào.");
    explanation->setWordWrap(true);
    pl->addWidget(explanation);
    auto* online = new QCheckBox("Bật tìm việc trực tuyến");
    online->setChecked(service_.db.setting("online_enabled", "true") == "true");
    pl->addWidget(online);
    connect(online, &QCheckBox::toggled, this, [this](bool enabled) {
        service_.db.setSetting("online_enabled", enabled ? "true" : "false");
        if (!enabled)
            cancelSync();
        netStatus_->setText(enabled ? "◉ Trực tuyến" : "◉ Ngoại tuyến");
    });
    auto* encryption =
        new QLabel(service_.db.fileEncryptionEnabled() ? "Cơ sở dữ liệu: Windows EFS đang bật"
                                                       : "Cơ sở dữ liệu: chưa bật Windows EFS");
    pl->addWidget(encryption);
    auto* encrypt = new QPushButton("Bật mã hóa dữ liệu bằng Windows EFS");
    pl->addWidget(encrypt);
    connect(encrypt, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "Windows EFS",
                                  "EFS cần Windows/ổ đĩa hỗ trợ và chứng chỉ của tài khoản Windows. Hãy giữ "
                                  "bản sao mã hóa có mật khẩu và sao lưu chứng chỉ EFS để tránh mất quyền "
                                  "đọc khi cài lại Windows. Bật cho thư mục dữ liệu?") != QMessageBox::Yes)
            return;
        try {
            cancelSync();
            service_.db.enableFileEncryption();
            refresh();
        } catch (const std::exception& e) {
            showError(e);
        }
    });
    l->addWidget(privacy);
    auto* weights = new QGroupBox("Trọng số đối chiếu · tự chuẩn hóa về 100%");
    auto* wl = new QFormLayout(weights);
    auto engine = service_.matcher();
    QMap<QString, QDoubleSpinBox*> inputs;
    QMap<QString, QString> labels = {{"title", "Chức danh"},      {"skill", "Kỹ năng"},
                                     {"employment", "Loại việc"}, {"workplace", "Hình thức làm việc"},
                                     {"location", "Địa điểm"},    {"seniority", "Cấp bậc"},
                                     {"salary", "Lương"},         {"freshness", "Độ mới (mặc định 0)"}};
    for (auto it = labels.begin(); it != labels.end(); ++it) {
        auto* spin = new QDoubleSpinBox;
        spin->setRange(0, 100);
        spin->setValue(engine.weights.value(it.key(), 0));
        inputs[it.key()] = spin;
        wl->addRow(it.value(), spin);
    }
    auto* save = new QPushButton("Lưu trọng số");
    wl->addRow(save);
    connect(save, &QPushButton::clicked, this, [=, this] {
        double total = 0;
        for (auto* spin : inputs)
            total += spin->value();
        if (total <= 0) {
            QMessageBox::warning(this, "Trọng số", "Tổng trọng số phải lớn hơn 0.");
            return;
        }
        for (auto it = inputs.begin(); it != inputs.end(); ++it) {
            auto rows = service_.db.query(
                "SELECT id FROM match_preferences WHERE name=? AND deleted_at IS NULL", {it.key()});
            service_.db.save("match_preferences", {{"name", it.key()}, {"weight", it.value()->value()}},
                             rows.isEmpty() ? 0 : rows[0].toObject()["id"].toInteger());
        }
        statusBar()->showMessage("Đã lưu trọng số.", 5000);
    });
    l->addWidget(weights);
    auto* discovery = new QGroupBox("Gợi ý việc làm");
    auto* discoveryForm = new QFormLayout(discovery);
    auto* threshold = new QSpinBox;
    threshold->setRange(0, 100);
    threshold->setValue(service_.db.setting("minimum_match_score", "40").toInt());
    discoveryForm->addRow("Điểm tối thiểu ở tab Gợi ý", threshold);
    connect(threshold, &QSpinBox::valueChanged, this,
            [this](int v) { service_.db.setSetting("minimum_match_score", QString::number(v)); });
    auto* pages = new QSpinBox;
    pages->setRange(1, 3);
    pages->setValue(service_.db.setting("search_pages", "1").toInt());
    discoveryForm->addRow("Số trang mỗi từ khóa (Adzuna)", pages);
    connect(pages, &QSpinBox::valueChanged, this,
            [this](int v) { service_.db.setSetting("search_pages", QString::number(v)); });
    l->addWidget(discovery);
    auto* data = new QGroupBox("Dữ liệu và sao lưu");
    auto* dl = new QVBoxLayout(data);
    auto* row = new QHBoxLayout;
    button("Sao lưu dữ liệu và tài liệu", row, [this] { exportData(); });
    button("Nhập / khôi phục bản sao", row, [this] { importData(); });
    button("Tải dữ liệu mẫu", row, [this] {
        service_.loadDemo();
        refresh();
    });
    dl->addLayout(row);
    auto* maintenance = new QHBoxLayout;
    auto confirm = [this](QString text, std::function<void()> action) {
        if (QMessageBox::question(this, "Xác nhận", text) == QMessageBox::Yes) {
            try {
                action();
                refresh();
            } catch (const std::exception& e) {
                showError(e);
            }
        }
    };
    button("Xóa dữ liệu mẫu", maintenance, [=, this] {
        confirm("Xóa toàn bộ bản ghi DEMO và bỏ liên kết đến chúng?", [this] { service_.db.removeDemo(); });
    });
    button("Xóa bộ nhớ đệm nguồn", maintenance, [this] {
        network_.clearCache();
        statusBar()->showMessage("Đã xóa phản hồi nguồn được lưu đệm.", 4000);
    });
    button("Xóa khóa API", maintenance, [=, this] {
        confirm("Xóa thông tin xác thực đã lưu trên máy?", [this] {
            cancelSync();
            secrets_.clear();
        });
    });
    dl->addLayout(maintenance);
    auto* destructive = new QHBoxLayout;
    button("Đặt lại hồ sơ", destructive, [=, this] {
        confirm("Xóa câu trả lời khảo sát và hồ sơ?", [this] {
            for (auto v : service_.db.all("user_profile"))
                service_.db.remove("user_profile", v.toObject()["id"].toInteger());
            for (auto v : service_.db.all("survey_answers"))
                service_.db.remove("survey_answers", v.toObject()["id"].toInteger());
            service_.db.setSetting("onboarding_seen", "false");
        });
    });
    button("Xóa toàn bộ dữ liệu", destructive, [=, this] {
        confirm("Xóa vĩnh viễn toàn bộ dữ liệu PathWeave và khóa API? Hãy xuất bản sao trước.", [this] {
            cancelSync();
            secrets_.clear();
            service_.db.deleteAll();
        });
    });
    destructive->addStretch();
    button("Xóa vĩnh viễn bản ghi trong thùng rác", destructive, [=, this] {
        confirm("Xóa vĩnh viễn bản ghi đã đánh dấu xóa và bỏ các liên kết đến chúng? Các bản sao đã xuất "
                "trước đó không thay đổi.",
                [this] {
                    cancelSync();
                    service_.db.purgeDeleted();
                });
    });
    dl->addLayout(destructive);
    auto* path =
        new QLabel("SQLite: " + service_.db.path() +
                   "\nBản xuất không chứa khóa DPAPI. Bản xuất có dữ liệu riêng tư — hãy giữ an toàn.");
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dl->addWidget(path);
    l->addWidget(data);
    l->addStretch();
    return scroll;
}
QWidget* MainWindow::sources() {
    auto* p = new QWidget;
    auto* l = new QVBoxLayout(p);
    auto* intro =
        new QLabel("Chỉ đọc API công khai hoặc URL bạn cung cấp. Không vượt CAPTCHA, đăng nhập hay hạn chế "
                   "robots. Nguồn có thể khác nhau theo quốc gia. Một số nguồn cần khóa API.");
    intro->setWordWrap(true);
    l->addWidget(intro);
    for (auto id : {"adzuna", "remotive", "greenhouse", "lever", "json", "rss", "url"}) {
        auto src = makeSource(id, network_);
        auto rows =
            service_.db.query("SELECT * FROM job_sources WHERE source_id=? AND deleted_at IS NULL", {id});
        auto data = rows.isEmpty() ? QJsonObject() : rows[0].toObject();
        auto* group = new QGroupBox(src->displayName());
        auto* gl = new QHBoxLayout(group);
        auto* enabled = new QCheckBox("Bật nguồn");
        enabled->setChecked(data["enabled"].toInt() != 0);
        gl->addWidget(enabled);
        auto* label = new QLabel("Đồng bộ: " + data["last_sync"].toString("chưa có") + "  " +
                                 data["error_message"].toString());
        label->setWordWrap(true);
        gl->addWidget(label, 1);
        button("Cấu hình", gl, [this, id] { configureSource(id); });
        l->addWidget(group);
        connect(enabled, &QCheckBox::toggled, this, [this, id, name = src->displayName()](bool on) {
            if (!on)
                cancelSync();
            auto rows = service_.db.query(
                "SELECT id FROM job_sources WHERE source_id=? AND deleted_at IS NULL", {id});
            service_.db.save("job_sources",
                             {{"source_id", id}, {"display_name", name}, {"enabled", on ? 1 : 0}},
                             rows.isEmpty() ? 0 : rows[0].toObject()["id"].toInteger());
        });
    }
    auto* bar = new QHBoxLayout;
    button("Đồng bộ nguồn đã bật", bar, [this] { syncSources(); })->setObjectName("primary");
    button("Hủy", bar, [this] { cancelSync(); });
    bar->addStretch();
    l->addLayout(bar);
    l->addStretch();
    return p;
}
void MainWindow::configureSource(QString id) {
    auto rows = service_.db.query("SELECT * FROM job_sources WHERE source_id=? AND deleted_at IS NULL", {id});
    auto row = rows.isEmpty() ? QJsonObject() : rows[0].toObject();
    auto config = QJsonDocument::fromJson(row["config"].toString().toUtf8()).object();
    QDialog d(this);
    d.setWindowTitle("Cấu hình " + id);
    d.resize(620, 680);
    auto* l = new QVBoxLayout(&d);
    auto* description =
        new QLabel(id == "remotive" ? "Remotive không cần khóa. Giới hạn một lần tải mỗi 6 giờ. Ghi nguồn và "
                                      "liên kết về Remotive; dữ liệu có độ trễ khoảng 24 giờ."
                                    : "Thông tin chỉ lưu cục bộ. Không nhập khóa bí mật vào URL / ánh xạ "
                                      "JSON. Khóa Adzuna được bảo vệ bằng Windows DPAPI.");
    description->setWordWrap(true);
    l->addWidget(description);
    QVector<Field> fields;
    if (id == "adzuna")
        fields = {{"country", "Mã quốc gia (gb, us, …)"},
                  {"currency", "Tiền tệ nguồn (GBP, USD, …)"},
                  {"app_id", "Adzuna app_id"},
                  {"app_key", "Adzuna app_key"}};
    else if (id == "greenhouse" || id == "lever")
        fields = {{"identifier", id == "greenhouse" ? "Board token hoặc URL Greenhouse" : "Mã site Lever"},
                  {"company", "Tên công ty"}};
    else if (id != "remotive")
        fields = {{"url", "URL HTTPS bạn cung cấp"}, {"company", "Tên công ty / nguồn"}};
    if (id == "json") {
        fields << Field{"root", "Đường dẫn mảng JSON (vd: jobs; trống = mảng gốc)"};
        for (auto key : {"title", "company", "canonical_url", "external_id", "location", "employment_type",
                         "workplace_mode", "description", "salary_min", "salary_max", "currency",
                         "salary_period", "skills", "posted_at"}) {
            fields << Field{"map_" + QString(key), "Ánh xạ " + QString(key)};
            config["map_" + QString(key)] = config["mapping"].toObject()[key].toString(key);
        }
    }
    if (id == "adzuna") {
        config["app_id"] = secrets_.get("adzuna_app_id");
        config["app_key"] = secrets_.get("adzuna_app_key");
    }
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* form = new FieldForm(fields, nullptr, config);
    scroll->setWidget(form);
    l->addWidget(scroll);
    if (auto* key = form->findChild<QLineEdit*>("app_key"))
        key->setEchoMode(QLineEdit::Password);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    l->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &d, [&] {
        try {
            auto c = form->values();
            if (id == "adzuna") {
                secrets_.put("adzuna_app_id", c["app_id"].toString());
                secrets_.put("adzuna_app_key", c["app_key"].toString());
                c.remove("app_id");
                c.remove("app_key");
            }
            if (id == "greenhouse" && c["identifier"].toString().startsWith("https://")) {
                QUrl u(c["identifier"].toString());
                if (u.host() == "boards.greenhouse.io" || u.host() == "job-boards.greenhouse.io")
                    c["identifier"] = u.path().split('/', Qt::SkipEmptyParts).value(0);
                else {
                    QMessageBox::warning(&d, "Nguồn",
                                         "URL phải là board Greenhouse công khai, hoặc nhập board token.");
                    return;
                }
            }
            if (id == "json") {
                QJsonObject mapping;
                auto keys = c.keys();
                for (auto k : keys)
                    if (k.startsWith("map_")) {
                        mapping[k.mid(4)] = c[k];
                        c.remove(k);
                    }
                c["mapping"] = mapping;
            }
            auto test = makeSource(id, network_);
            auto testConfig = c;
            if (id == "adzuna") {
                testConfig["app_id"] = secrets_.get("adzuna_app_id");
                testConfig["app_key"] = secrets_.get("adzuna_app_key");
            }
            test->configure(testConfig);
            if (!test->isConfigured()) {
                QMessageBox::warning(&d, "Nguồn", "Cấu hình chưa hợp lệ. Kiểm tra URL, mã nguồn hoặc khóa.");
                return;
            }
            service_.db.save("job_sources",
                             {{"source_id", id},
                              {"display_name", test->displayName()},
                              {"config", c},
                              {"enabled", row["enabled"].toInt()}},
                             row["id"].toInteger());
            d.accept();
        } catch (const std::exception& e) {
            QMessageBox::warning(&d, "Nguồn", QString::fromUtf8(e.what()));
        }
    });
    if (d.exec() == QDialog::Accepted)
        refresh();
}
void MainWindow::runSavedSearch(qint64 id) {
    auto r = service_.db.get("saved_searches", id);
    if (r.isEmpty())
        return;
    service_.db.setSetting("search_keyword", r["query"].toString());
    service_.db.setSetting("search_location", r["location"].toString());
    service_.db.setSetting("search_type", r["employment_type"].toString());
    service_.db.setSetting("search_mode", r["workplace_mode"].toString());
    if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        dialog->accept();
    QTimer::singleShot(0, this, [this] {
        navigate(1);
        if (service_.db.setting("online_enabled") == "true")
            syncSources();
        else
            statusBar()->showMessage("Đã áp dụng bộ lọc vào dữ liệu cục bộ. Bật trực tuyến để lấy tin mới.",
                                     6000);
    });
}
void MainWindow::cancelSync() {
    ++syncEpoch_;
    syncing_ = false;
    searchQueue_.clear();
    network_.cancelAll();
}
void MainWindow::ensureOnlineDefaults() {
    // A fresh profile starts ready to search. Users can still turn networking off in Settings.
    if (service_.db.setting("online_enabled").isEmpty())
        service_.db.setSetting("online_enabled", "true");

    const auto rows = service_.db.query(
        "SELECT id FROM job_sources WHERE source_id=? AND deleted_at IS NULL", {"remotive"});
    if (rows.isEmpty()) {
        service_.db.save("job_sources",
                         {{"source_id", "remotive"},
                          {"display_name", "Remotive"},
                          {"enabled", 1},
                          {"config", QJsonObject()}});
    }
}
void MainWindow::syncSources() {
    ensureOnlineDefaults();
    if (syncing_) {
        statusBar()->showMessage("Đang đồng bộ. Chờ hoàn tất hoặc hủy.", 4000);
        return;
    }
    if (service_.db.setting("online_enabled") != "true") {
        QMessageBox::information(
            this, "Đang ngoại tuyến",
            "Bật tìm việc trực tuyến trong Cài đặt trước. Các tính năng cục bộ luôn hoạt động.");
        return;
    }
    sources_.clear();
    for (auto v : service_.db.all("job_sources")) {
        auto row = v.toObject();
        if (!row["enabled"].toInt() || row["source_id"] == "demo")
            continue;
        auto id = row["source_id"].toString();
        try {
            auto source = std::shared_ptr<IJobSource>(makeSource(id, network_));
            auto config = QJsonDocument::fromJson(row["config"].toString().toUtf8()).object();
            if (id == "adzuna") {
                config["app_id"] = secrets_.get("adzuna_app_id");
                config["app_key"] = secrets_.get("adzuna_app_key");
            }
            source->configure(config);
            sources_.push_back(source);
        } catch (const std::exception& e) {
            showError(e);
        }
    }
    if (sources_.empty()) {
        QMessageBox::information(this, "Nguồn",
                                 "Không có nguồn trực tuyến đang bật. Remotive được bật mặc định; bạn có thể quản lý thêm nguồn trong Nguồn tuyển dụng.");
        return;
    }
    syncing_ = true;
    statusBar()->showMessage("Đang tìm việc trực tuyến…", 5000);
    auto queries = DiscoveryPolicy::queries(
        service_.profile(), service_.db.setting("search_keyword"), service_.db.setting("search_location"),
        service_.db.setting("search_type"), service_.db.setting("search_pages", "1").toInt());
    for (const auto& source : sources_) {
        if (source->sourceId() == "adzuna")
            for (auto q : queries)
                searchQueue_.enqueue({source, q});
        else {
            // Public company boards/feeds are fetched once, then all desired roles are ranked locally.
            SearchQuery q = queries.front();
            if (service_.db.setting("search_keyword").trimmed().isEmpty())
                q.keyword.clear();
            searchQueue_.enqueue({source, q});
        }
    }
    nextSearch();
}
void MainWindow::nextSearch() {
    if (!syncing_)
        return;
    if (searchQueue_.isEmpty()) {
        syncing_ = false;
        if (!QApplication::activeModalWidget())
            refresh();
        return;
    }
    auto task = searchQueue_.head();
    auto source = task.source;
    const int epoch = syncEpoch_;
    auto wait = service_.db.setting("rate_" + source->sourceId(), "0").toLongLong() -
                QDateTime::currentSecsSinceEpoch();
    if (wait > 0 && wait <= 30) {
        netStatus_->setText("Chờ giới hạn nguồn: " + QString::number(wait) + " giây");
        QTimer::singleShot(int(wait * 1000 + 100), this, [this, epoch] {
            if (epoch == syncEpoch_)
                nextSearch();
        });
        return;
    }
    searchQueue_.dequeue();
    source->search(task.query, [this, source, epoch](SourceResult r) {
        if (epoch != syncEpoch_)
            return;
        int count = 0;
        QString error = r.error;
        try {
            if (error.isEmpty()) {
                if (!service_.db.begin())
                    throw std::runtime_error("Không mở được giao dịch đồng bộ.");
                try {
                    for (auto v : r.jobs) {
                        service_.upsertJob(v.toObject());
                        ++count;
                    }
                    if (!service_.db.commit())
                        throw std::runtime_error("Không lưu được kết quả đồng bộ.");
                } catch (...) {
                    service_.db.rollback();
                    throw;
                }
            }
            auto rows = service_.db.query(
                "SELECT id FROM job_sources WHERE source_id=? AND deleted_at IS NULL", {source->sourceId()});
            if (!rows.isEmpty()) {
                QJsonObject u{{"error_message", error}};
                if (error.isEmpty())
                    u["last_sync"] = now();
                service_.db.save("job_sources", u, rows[0].toObject()["id"].toInteger());
            }
            service_.db.save("sync_runs", {{"source_id", source->sourceId()},
                                           {"status", error.isEmpty() ? "success" : "error"},
                                           {"job_count", count},
                                           {"message", error},
                                           {"started_at", now()}});
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        }
        statusBar()->showMessage(source->displayName() + ": " +
                                     (error.isEmpty() ? QString::number(count) + " tin" : error),
                                 15000);
        QTimer::singleShot(0, this, [this, epoch] {
            if (epoch == syncEpoch_)
                nextSearch();
        });
    });
}
} // namespace pw
