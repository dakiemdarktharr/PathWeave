#include <QtTest>
#include "FakeNetwork.h"
#include <QtWidgets>
#include "services/ResumeBuilder.h"
#include "services/BackupCrypto.h"
#include "services/DiscoveryPolicy.h"
#include "networking/RobotsPolicy.h"
#include "ui/ResumeDialog.h"
#include "ui/MainWindow.h"
#include "reports/Reports.h"
using namespace pw;
class TestEnhancements : public QObject {
    Q_OBJECT
  private slots:
    void cvUsesOnlySelectedFacts() {
        Database db(":memory:");
        CareerService s(db);
        s.saveProfile({{"display_name", "Nguyễn An"},
                       {"desired_titles", "Software Engineer"},
                       {"email", "an@example.test"},
                       {"hard_skills", "C++, Qt"},
                       {"certifications", "ISTQB Foundation — 2024"},
                       {"education", "Đại học A — CNTT"}},
                      true);
        auto role = s.save("current_roles", {{"company_name", "Công ty A"},
                                             {"job_title", "Lập trình viên"},
                                             {"start_date", "2022-01-01"},
                                             {"notes", "SECRET manager feedback"},
                                             {"cv_description", "Phát triển ứng dụng Qt."}});
        auto e = db.save("career_evidence", {{"title", "Tối ưu giao diện"},
                                             {"polished_summary", "Giảm thời gian mở màn hình"},
                                             {"measurable_impact", "Từ 4 giây xuống 2 giây"},
                                             {"skills", "Qt"}});
        db.save("career_evidence", {{"title", "Unselected confidential evidence"}});
        s.loadDemo();
        auto draft = ResumeBuilder(s).build(
            {{"title", "Senior Engineer"},
             {"description", "Must have AWS and 10 years experience; saved $999 million."},
             {"skills", "Qt, AWS"}},
            {"current_roles:" + QString::number(role), "career_evidence:" + QString::number(e)});
        QVERIFY(draft.text.contains("ISTQB"));
        QVERIFY(draft.text.contains("Từ 4 giây xuống 2 giây"));
        QVERIFY(draft.text.contains("Phát triển ứng dụng Qt."));
        for (QString bad : {"$999", "AWS", "10 years", "SECRET", "Demo Labs", "Unselected confidential"})
            QVERIFY2(!draft.text.contains(bad), qPrintable(bad));
        QVERIFY(draft.warnings.join(' ').contains("AWS"));
        QVERIFY(draft.sources.contains("career_evidence:" + QString::number(e)));
    }
    void draftCloseAutosavesOnlyChanges() {
        Database db(":memory:");
        CareerService service(db);
        service.saveProfile({{"display_name", "An"}, {"desired_titles", "Engineer"}}, true);
        ResumeDialog d(service);
        d.generate();
        d.reject();
        QCOMPARE(db.count("resume_drafts"), 1);
        auto id = db.all("resume_drafts")[0].toObject()["id"].toInteger();
        ResumeDialog reopened(service, 0, 0, id);
        reopened.reject();
        QCOMPARE(db.count("resume_drafts"), 1);
    }
    void cvEscapesMarkup() {
        auto html =
            ResumeBuilder::html("Người dùng\n<script>alert(1)</script>\n<img src='https://evil.test/pixel'>");
        QVERIFY(!html.contains("<script>"));
        QVERIFY(html.contains("&lt;script&gt;"));
        QVERIFY(!html.contains("<img src="));
    }
    void photoAndExport() {
        QTemporaryDir dir;
        QImage photo(400, 500, QImage::Format_RGB32);
        photo.fill(QColor("#467a9b"));
        auto path = dir.filePath("photo.png");
        QVERIFY(photo.save(path));
        auto encoded = ResumeBuilder::importPhoto(path);
        QVERIFY(!encoded.isEmpty());
        QVERIFY(ResumeBuilder::html("An", encoded).contains("data:image/png;base64,"));
        QVERIFY(!ResumeBuilder::html("An", encoded + "' onerror='attack").contains("<img"));
        Database db(":memory:");
        CareerService service(db);
        service.saveProfile({{"display_name", "Nguyễn An"},
                             {"desired_titles", "Kỹ sư phần mềm"},
                             {"hard_skills", "Qt, C++"},
                             {"email", "an@example.test"}},
                            true);
        auto profile = service.profile();
        profile["professional_summary"] = "Kỹ sư phần mềm phát triển ứng dụng desktop với C++ và Qt.";
        profile["certifications"] = "Chứng chỉ kiểm thử phần mềm — đơn vị QA minh họa, 2024";
        profile["education"] = "Cử nhân Công nghệ thông tin — Trường Đại học minh họa, 2022";
        service.saveProfile(profile, true);
        service.save("current_roles",
                     {{"company_name", "Công ty QA minh họa"},
                      {"job_title", "Kỹ sư phần mềm"},
                      {"start_date", "2022-06-01"},
                      {"is_current", 1},
                      {"cv_description", "Phát triển giao diện Qt Widgets và tích hợp SQLite.\nViết kiểm thử "
                                         "chức năng và xử lý lỗi ứng dụng Windows."}});
        ResumeDialog dialog(service);
        dialog.findChild<QLineEdit*>("resumeTargetTitle")->setText("Kỹ sư phần mềm");
        dialog.findChild<QPlainTextEdit*>("resumeJobDescription")->setPlainText("Phát triển Qt và C++");
        dialog.findChild<QPushButton*>("generateResume")->click();
        QVERIFY_EXCEPTION_THROWN(dialog.exportBytes("PDF"), std::runtime_error);
        dialog.findChild<QCheckBox*>("resumeReviewed")->setChecked(true);
        auto pdf = dialog.exportBytes("PDF");
        QVERIFY(pdf.startsWith("%PDF"));
        QVERIFY(pdf.size() > 1000);
        QVERIFY(dialog.exportBytes("TXT").contains("Qt"));
        QVERIFY(dialog.exportBytes("HTML").contains("<html>"));
        dialog.setPhotoFromFile(path);
        dialog.findChild<QCheckBox*>("resumeReviewed")->setChecked(true);
        QVERIFY(dialog.exportBytes("HTML").contains("data:image/png;base64,"));
        pdf = dialog.exportBytes("PDF");
        auto draftId = dialog.saveDraft();
        QVERIFY(draftId > 0);
        Database restored(":memory:");
        restored.importAll(db.exportAll());
        QCOMPARE(restored.get("resume_drafts", draftId)["body"], db.get("resume_drafts", draftId)["body"]);
        dialog.show();
        QTest::qWait(30);
        dialog.grab().save("cv-editor.png");
        QFile output("cv-example.pdf");
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(pdf);
        dialog.findChild<QPlainTextEdit*>("resumeText")->appendPlainText("Sửa bản nháp");
        QVERIFY(!dialog.findChild<QCheckBox*>("resumeReviewed")->isChecked());
    }
    void attachResumePreservesApplication() {
        Database db(":memory:");
        CareerService s(db);
        s.saveProfile({{"display_name", "An"}, {"desired_titles", "Engineer"}}, true);
        auto job = s.upsertJob({{"title", "Engineer"}, {"company", "A"}});
        auto app = s.apply(job);
        ResumeDialog dialog(s, job, app);
        dialog.generate();
        dialog.findChild<QCheckBox*>("resumeReviewed")->setChecked(true);
        dialog.findChild<QPushButton*>("attachResume")->click();
        auto a = db.get("applications", app);
        QVERIFY(a["resume_document_id"].toInteger() > 0);
        QCOMPARE(a["status"].toString(), QString("preparing"));
        auto doc = db.get("documents", a["resume_document_id"].toInteger());
        QVERIFY(QByteArray::fromBase64(doc["content_base64"].toString().toLatin1()).startsWith("%PDF"));
        QCOMPARE(db.count("application_activities"), 1);
        QVERIFY(db.query("PRAGMA foreign_key_check").isEmpty());
    }
    void distinctRequisitionsAreNotMerged() {
        Database db(":memory:");
        CareerService s(db);
        QJsonObject j{{"title", "Engineer"}, {"company", "A"}, {"canonical_url", "https://jobs.test/1"}};
        auto first = s.upsertJob(j);
        j["canonical_url"] = "https://jobs.test/2";
        auto second = s.upsertJob(j);
        QVERIFY(first != second);
        j.remove("canonical_url");
        j["location"] = "Hanoi";
        auto manual = s.upsertJob(j);
        QCOMPARE(s.upsertJob(j), manual);
        j["location"] = "Ho Chi Minh";
        QVERIFY(s.upsertJob(j) != manual);
    }
    void migrationFromV1() {
        QTemporaryDir dir;
        auto path = dir.filePath("legacy.sqlite3");
        qint64 role = 0;
        {
            Database db(path);
            role = db.save("current_roles", {{"company_name", "Preserved"}, {"job_title", "Developer"}});
            db.execute("DROP TABLE resume_drafts");
            db.execute("ALTER TABLE current_roles DROP COLUMN cv_description");
            db.execute("UPDATE schema_migrations SET version=1");
        }
        {
            Database db(path);
            QCOMPARE(db.get("current_roles", role)["company_name"].toString(), QString("Preserved"));
            db.save("current_roles", {{"cv_description", "Migrated"}}, role);
            QCOMPARE(db.count("resume_drafts"), 0);
            QCOMPARE(db.query("SELECT MAX(version) v FROM schema_migrations")[0].toObject()["v"].toInt(), 2);
            db.migrate();
        }
    }
    void partialStatusUpdates() {
        Database db(":memory:");
        CareerService s(db);
        auto task = s.save("tasks", {{"title", "Done"}, {"status", "completed"}});
        auto completed = db.get("tasks", task).value("completed_at");
        s.save("tasks", {{"description", "Edited"}}, task);
        QCOMPARE(db.get("tasks", task)["completed_at"], completed);
        auto app = s.save("applications", {{"company", "A"}, {"title", "E"}, {"status", "applied"}});
        auto count = db.count("application_activities");
        auto date = db.get("applications", app).value("applied_at");
        s.save("applications", {{"notes", "Follow up"}}, app);
        QCOMPARE(db.count("application_activities"), count);
        QCOMPARE(db.get("applications", app)["applied_at"], date);
    }
    void matchingUsesWorkSkills() {
        Database db(":memory:");
        CareerService s(db);
        s.saveProfile({{"display_name", "An"}, {"desired_titles", "Engineer"}}, true);
        s.save("achievements", {{"title", "Shipped"}, {"skills", "Qt, C++"}});
        QCOMPARE(s.matcher().score({{"skills", "Qt, C++"}}, s.matchingProfile()).skill, 100.0);
    }
    void vietnameseAndTokenBoundaries() {
        MatchingEngine e;
        QCOMPARE(e.score({{"title", "Software Developer"}}, {{"desired_titles", "Kỹ sư phần mềm"}}).title,
                 100.0);
        QVERIFY(MatchingEngine::containsTerm("làm việc tại TP. Hồ Chí Minh", "Ho Chi Minh"));
        QVERIFY(!MatchingEngine::containsTerm("JavaScript developer", "Java"));
        QVERIFY(MatchingEngine::containsTerm("C++ developer", "C++"));
        QVERIFY(!MatchingEngine::containsTerm("C++ developer", "C"));
    }
    void discoveryQueriesAndFilters() {
        auto q = DiscoveryPolicy::queries({{"desired_titles", "Engineer, Designer"}}, {}, "Vietnam",
                                          "part_time", 2);
        QCOMPARE(q.size(), 4);
        QCOMPARE(q[1].page, 2);
        QCOMPARE(q[0].location, QString("Vietnam"));
        QCOMPARE(q[0].employmentType, QString("part_time"));
        QJsonObject j{{"title", "Engineer"},
                      {"company", "A"},
                      {"employment_type", "full_time"},
                      {"workplace_mode", "unknown"},
                      {"posted_at", QDate::currentDate().addDays(-1).toString(Qt::ISODate)},
                      {"fetched_at", now()}};
        QVERIFY(!DiscoveryPolicy::visible(j, {}, {}, {}, "remote", false, 0, 100));
        QVERIFY(!DiscoveryPolicy::visible(j, {}, {}, {}, {}, true, 0, 100));
        QVERIFY(!DiscoveryPolicy::visible(j, {}, {}, {}, {}, false, 40, 39));
        QVERIFY(!DiscoveryPolicy::visible(j, {{"excluded_companies", "A"}}, {}, {}, {}, false, 0, 100));
    }
    void savedSearchRerunAndSavedDismissedQuery() {
        QTemporaryDir dir;
        Database db(":memory:");
        CareerService s(db);
        SecretStore secrets(dir.path());
        auto job = s.upsertJob({{"title", "Qt Engineer"},
                                {"company", "A"},
                                {"employment_type", "part_time"},
                                {"workplace_mode", "remote"}});
        s.saveJob(job);
        db.save("job_listings", {{"status", "dismissed"}}, job);
        db.save("saved_searches", {{"name", "Remote Qt"},
                                   {"query", "Qt"},
                                   {"employment_type", "part_time"},
                                   {"workplace_mode", "remote"}});
        MainWindow window(s, secrets);
        QCOMPARE(window.filteredJobs({}, {}, "Qt").size(), 1);
        QDialog chooser(&window);
        auto* layout = new QVBoxLayout(&chooser);
        layout->addWidget(window.entityPage("saved_searches"));
        chooser.setModal(true);
        chooser.show();
        chooser.findChild<QTableWidget*>("saved_searchesTable")->setCurrentCell(0, 0);
        chooser.findChild<QPushButton*>("runSavedSearch")->click();
        QTRY_COMPARE(chooser.result(), int(QDialog::Accepted));
        QTRY_VERIFY(window.findChild<QLineEdit*>("jobKeyword"));
        QCOMPARE(window.findChild<QLineEdit*>("jobKeyword")->text(), QString("Qt"));
        QCOMPARE(window.findChild<QComboBox*>("employmentFilter")->currentData().toString(),
                 QString("part_time"));
        window.navigate(2);
        QCOMPARE(window.findChild<QListWidget*>("jobList")->count(), 1);
    }
    void savedSearchSchedule() {
        auto t = QDateTime::fromString("2026-09-09T09:00:00Z", Qt::ISODate);
        QJsonObject search{{"enabled", 1}, {"interval_hours", 24}};
        QVERIFY(DiscoveryPolicy::due(search, t));
        search["next_run"] = DiscoveryPolicy::nextRun(search, t);
        QVERIFY(!DiscoveryPolicy::due(search, t));
        QVERIFY(DiscoveryPolicy::due(search, t.addDays(1)));
        search["enabled"] = 0;
        QVERIFY(!DiscoveryPolicy::due(search, t.addDays(2)));
    }
    void robotsGroupsAndPaths() {
        RobotsPolicy policy("User-agent: *\nDisallow: /private\nAllow: /private/public\nUser-agent: "
                            "PathWeave\nDisallow: /blocked\nAllow: /blocked/ok$\n");
        QVERIFY(policy.allows(QUrl("https://a.test/private")));
        QVERIFY(!policy.allows(QUrl("https://a.test/blocked/x")));
        QVERIFY(policy.allows(QUrl("https://a.test/blocked/ok")));
        QVERIFY(!policy.allows(QUrl("https://a.test/blocked/ok/x")));
        RobotsPolicy wildcard("User-agent: *\nDisallow: /*.pdf$\nDisallow: /secret\nAllow: /secret/public\n");
        QVERIFY(!wildcard.allows(QUrl("https://a.test/cv.pdf")));
        QVERIFY(wildcard.allows(QUrl("https://a.test/secret/public")));
        QVERIFY(!wildcard.allows(QUrl("https://a.test/%73ecret")));
    }
    void redirectsRespectOriginRobotsAndLoops() {
        Database db(":memory:");
        db.setSetting("online_enabled", "true");
        FakeNetwork fake;
        NetworkService net(db, nullptr, &fake);
        fake.redirects["/start"] = QUrl("/final");
        bool done = false;
        net.get(QUrl("https://fixture.test/start"), "one", 0, 60, [&](Response r) {
            QVERIFY(r.error.isEmpty());
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(fake.requests, 2);
        fake.redirects["/evil"] = QUrl("https://other.test/final");
        done = false;
        net.get(QUrl("https://fixture.test/evil"), "two", 0, 60, [&](Response r) {
            QVERIFY(!r.error.isEmpty());
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(fake.requests, 3);
        fake.redirects["/public"] = QUrl("/secret");
        RobotsPolicy robots("User-agent: *\nDisallow: /secret\n");
        done = false;
        net.get(
            QUrl("https://fixture.test/public"), "three", 0, 60,
            [&](Response r) {
                QVERIFY(!r.error.isEmpty());
                done = true;
            },
            15000, [robots](const QUrl& u) { return robots.allows(u); });
        QTRY_VERIFY(done);
        QCOMPARE(fake.requests, 4);
        fake.redirects["/loop"] = QUrl("/loop");
        done = false;
        net.get(QUrl("https://fixture.test/loop"), "four", 0, 60, [&](Response r) {
            QVERIFY(!r.error.isEmpty());
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(fake.requests, 10);
    }
    void skillRenameAndAttachmentCopy() {
        QTemporaryDir dir;
        Database db(":memory:");
        CareerService s(db);
        auto a = s.save("achievements", {{"title", "Work"}, {"skills", "Qt, C++"}});
        QCOMPARE(db.count("achievement_skills"), 2);
        auto skill = db.query("SELECT id FROM skills WHERE name=?", {"Qt"})[0].toObject()["id"].toInteger();
        s.save("skills", {{"name", "Qt Widgets"}}, skill);
        QVERIFY(db.get("achievements", a)["skills"].toString().contains("Qt Widgets"));
        auto path = dir.filePath("resume.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("CV content");
        file.close();
        auto id = s.save("documents", {{"name", "resume.txt"}, {"path", path}});
        QVERIFY(QFile::remove(path));
        QCOMPARE(QByteArray::fromBase64(db.get("documents", id)["content_base64"].toString().toLatin1()),
                 QByteArray("CV content"));
        Database restored(":memory:");
        restored.importAll(db.exportAll());
        QCOMPARE(restored.get("documents", id)["content_base64"], db.get("documents", id)["content_base64"]);
    }
    void explicitFeedbackIsBoundedAndOptional() {
        Database db(":memory:");
        CareerService s(db);
        auto id = s.upsertJob({{"title", "Qt"},
                               {"company", "A"},
                               {"employment_type", "full_time"},
                               {"workplace_mode", "remote"}});
        s.saveJob(id);
        auto j = db.get("job_listings", id);
        auto result = s.matcher().score(j, s.matchingProfile());
        QCOMPARE(result.feedback, 2.0);
        s.apply(id);
        QCOMPARE(s.matcher().score(j, s.matchingProfile()).feedback, 4.0);
        db.setSetting("explicit_feedback_enabled", "false");
        QCOMPARE(s.matcher().score(j, s.matchingProfile()).feedback, 0.0);
    }
    void encryptedBackup() {
        auto plain = QByteArray("{\"private\":\"career evidence and resume\"}");
        auto password = QString("correct horse 2026!");
        auto encoded = BackupCrypto::encrypt(plain, password);
        QVERIFY(!encoded.contains("career evidence"));
        QCOMPARE(BackupCrypto::decrypt(encoded, password), plain);
        QVERIFY(encoded != BackupCrypto::encrypt(plain, password));
        QVERIFY_EXCEPTION_THROWN(BackupCrypto::decrypt(encoded, "wrong password"), std::runtime_error);
        encoded[encoded.size() - 1] = char(encoded.back() ^ 1);
        QVERIFY_EXCEPTION_THROWN(BackupCrypto::decrypt(encoded, password), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(BackupCrypto::decrypt("PWBK1", password), std::runtime_error);
    }
    void backupAttachmentIntegrity() {
        Database db(":memory:");
        auto data = db.exportAll();
        auto tables = data["tables"].toObject();
        tables["documents"] = QJsonArray{
            QJsonObject{{"id", 1}, {"name", "broken.pdf"}, {"content_base64", "YWJj"}, {"sha256", "wrong"}}};
        data["tables"] = tables;
        QVERIFY_EXCEPTION_THROWN(db.importAll(data), std::runtime_error);
    }
    void reportsExcludeDemoAndUseEvents() {
        Database db(":memory:");
        CareerService s(db);
        s.loadDemo();
        QCOMPARE(Reports(db).funnel()["total"].toInt(), 0);
        auto app = s.save("applications", {{"company", "Real"}, {"title", "E"}, {"status", "preparing"}});
        s.transition(app, "applied");
        s.transition(app, "offer");
        auto c = Reports(db).cohort(QDate::currentDate().toString("yyyy-MM"));
        QCOMPARE(c["Tổng hồ sơ"].toInt(), 1);
        QCOMPARE(c["Đã có offer"].toInt(), 1);
        QCOMPARE(c["Đã phỏng vấn"].toInt(), 0);
    }
    void calendarIcs() {
        Database db(":memory:");
        db.save("interviews", {{"round", "Vòng 1, C++"}, {"scheduled_at", "2026-10-02T10:00:00+07:00"}});
        db.save("tasks", {{"title", "Ngày hạn"}, {"status", "planned"}, {"due_date", "2026-10-03"}});
        auto ics = Reports(db).ics();
        QVERIFY(ics.contains("DTSTART:20261002T030000Z"));
        QVERIFY(ics.contains("DTSTART;VALUE=DATE:20261003"));
        QVERIFY(ics.contains("Vòng 1\\, C++"));
    }
    void purgeDeleted() {
        Database db(":memory:");
        auto id = db.save("projects", {{"name", "Remove"}});
        db.save("tasks", {{"title", "Keep"}, {"project_id", id}});
        db.remove("projects", id);
        db.purgeDeleted();
        QCOMPARE(db.query("SELECT * FROM projects").size(), 0);
        QCOMPARE(db.count("tasks"), 1);
        QVERIFY(db.all("tasks")[0].toObject()["project_id"].isNull());
    }
};
QTEST_MAIN(TestEnhancements)
#include "TestEnhancements.moc"
