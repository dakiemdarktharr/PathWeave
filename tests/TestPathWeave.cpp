#include "FakeNetwork.h"
#include "database/Database.h"
#include "reports/Reports.h"
#include "services/CareerService.h"
#include "sources/Sources.h"
#include "ui/MainWindow.h"
#include "ui/SurveyDialog.h"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <QtWidgets>
using namespace pw;
#include "TestPathWeave.h"
void TestPathWeave::migrations() {
    Database db(":memory:");
    QCOMPARE(db.query("PRAGMA user_version").size(), 1);
    QCOMPARE(db.query("SELECT version FROM schema_migrations")[0].toObject()["version"].toInt(), 2);
    db.migrate();
    QCOMPARE(db.query("SELECT * FROM schema_migrations").size(), 1);
    QVERIFY(db.query("PRAGMA foreign_key_check").isEmpty());
    QVERIFY(tableNames().size() >= 28);
}
void TestPathWeave::surveyValidation() {
    QVERIFY(!validateSurvey({}).isEmpty());
    QJsonObject p = {
        {"display_name", "A"}, {"desired_titles", "Developer"}, {"min_hours", 40}, {"max_hours", 20}};
    QVERIFY(!validateSurvey(p).isEmpty());
    p["max_hours"] = 40;
    QVERIFY(validateSurvey(p).isEmpty());
    p["employment_types"] = "remote";
    QVERIFY(!validateSurvey(p).isEmpty());
}
void TestPathWeave::surveyCompletion() {
    QCOMPARE(profileCompletion({}), 0);
    QJsonObject p;
    for (auto f : surveyFields())
        p[f.name] = "value";
    QCOMPARE(profileCompletion(p), 100);
}
void TestPathWeave::separation() {
    Database db(":memory:");
    CareerService s(db);
    auto id = s.upsertJob({{"title", "Engineer"},
                           {"company", "A"},
                           {"employment_type", "part_time"},
                           {"workplace_mode", "remote"}});
    auto j = db.get("job_listings", id);
    QCOMPARE(j["employment_type"].toString(), QString("part_time"));
    QCOMPARE(j["workplace_mode"].toString(), QString("remote"));
    QVERIFY_EXCEPTION_THROWN(db.save("job_listings", {{"employment_type", "remote"}}, id),
                             std::runtime_error);
}
void TestPathWeave::matchScore() {
    MatchingEngine e;
    QJsonObject p = {{"desired_titles", "Software Engineer"},
                     {"hard_skills", "C++, Qt"},
                     {"employment_types", "full_time"},
                     {"workplace_modes", "remote"},
                     {"preferred_countries", "Vietnam"},
                     {"seniority", "senior"},
                     {"salary_min", 50000},
                     {"salary_currency", "USD"}};
    QJsonObject j = {
        {"title", "Software Engineer"}, {"skills", "C++, Qt"},   {"employment_type", "full_time"},
        {"workplace_mode", "remote"},   {"location", "Vietnam"}, {"seniority", "senior"},
        {"salary_max", 60000},          {"currency", "USD"},     {"salary_period", "year"}};
    QCOMPARE(e.score(j, p).total, 100.0);
}
void TestPathWeave::excludedKeywords() {
    MatchingEngine e;
    QJsonObject j = {{"title", "Engineer"}, {"description", "Unpaid position"}},
                p = {{"desired_titles", "Engineer"}};
    double baseline = e.score(j, p).total;
    p["exclude_keywords"] = "unpaid";
    QCOMPARE(e.score(j, p).total, std::max(0.0, baseline - 30));
}
void TestPathWeave::excludedCompanies() {
    MatchingEngine e;
    QCOMPARE(e.score({{"company", "ACME"}}, {{"excluded_companies", "acme"}}).total, 0.0);
}
void TestPathWeave::salaryMatching() {
    MatchingEngine e;
    QJsonObject p = {{"salary_min", 100000}, {"salary_currency", "USD"}};
    QCOMPARE(e.score({}, p).salary, 50.0);
    QCOMPARE(e.score({{"salary_max", 100000}, {"currency", "USD"}, {"salary_period", "year"}}, p).salary,
             100.0);
    QCOMPARE(e.score({{"salary_max", 25000}, {"currency", "USD"}, {"salary_period", "year"}}, p).salary,
             25.0);
    QCOMPARE(e.score({{"salary_max", 100000}, {"currency", "EUR"}, {"salary_period", "year"}}, p).salary,
             50.0);
}
void TestPathWeave::remoteMatching() {
    MatchingEngine e;
    QJsonObject p = {{"workplace_modes", "remote"}};
    QCOMPARE(e.score({{"workplace_mode", "unknown"}}, p).workplaceMode, 50.0);
    QCOMPARE(e.score({{"workplace_mode", "remote"}}, p).workplaceMode, 100.0);
    QCOMPARE(e.score({{"workplace_mode", "hybrid"}}, p).workplaceMode, 0.0);
}
void TestPathWeave::titleSynonyms() {
    MatchingEngine e;
    QCOMPARE(
        e.score({{"title", "Software Developer"}}, {{"desired_titles", "Designer, Software Engineer"}}).title,
        100.0);
}
void TestPathWeave::duplicateDetection() {
    Database db(":memory:");
    CareerService s(db);
    QJsonObject j = {{"title", "Engineer"},
                     {"company", "ACME"},
                     {"canonical_url", "https://example.com/job/?utm_source=test#top"},
                     {"external_id", "123"},
                     {"source_id", "demo"}};
    auto id = s.upsertJob(j);
    j["canonical_url"] = "https://example.com/job";
    QCOMPARE(s.upsertJob(j), id);
    j["canonical_url"] = "https://example.com/another";
    j["title"] = " engineer ";
    QCOMPARE(s.upsertJob(j), id);
    QCOMPARE(db.count("job_listings"), 1);
}
void TestPathWeave::crud() {
    Database db(":memory:");
    auto id = db.save("projects", {{"name", "Alpha ' SQL"}, {"status", "active"}});
    QCOMPARE(db.get("projects", id)["name"].toString(), QString("Alpha ' SQL"));
    db.save("projects", {{"name", "Beta"}}, id);
    QCOMPARE(db.get("projects", id)["name"].toString(), QString("Beta"));
    db.remove("projects", id);
    QVERIFY(db.get("projects", id).isEmpty());
    QVERIFY_EXCEPTION_THROWN(db.save("projects;DROP TABLE tasks", {}), std::runtime_error);
}
void TestPathWeave::foreignKeys() {
    Database db(":memory:");
    QVERIFY_EXCEPTION_THROWN(db.save("tasks", {{"title", "Bad"}, {"project_id", 99999}}), std::runtime_error);
}
void TestPathWeave::statusTransitions() {
    try {
        Database db(":memory:");
        CareerService s(db);
        auto a = s.save("applications", {{"company", "A"}, {"title", "Engineer"}, {"status", "saved"}});
        s.transition(a, "applied");
        s.transition(a, "interview");
        QCOMPARE(db.count("application_activities"), 3);
        QVERIFY(!db.get("applications", a)["applied_at"].toString().isEmpty());
        s.transition(a, "archived");
        QVERIFY_EXCEPTION_THROWN(s.transition(a, "offer"), std::runtime_error);
        QCOMPARE(db.get("applications", a)["status"].toString(), QString("archived"));
    } catch (const std::exception& e) {
        QFAIL(e.what());
    }
}
void TestPathWeave::evidenceNoFabrication() {
    Database db(":memory:");
    CareerService s(db);
    auto a = s.save("achievements",
                    {{"title", "Shipped feature"}, {"body", "Exactly user text"}, {"measurable_result", ""}});
    auto e = db.get("career_evidence", s.convertEvidence(a));
    QCOMPARE(e["polished_summary"].toString(), QString("Exactly user text"));
    QVERIFY(e["measurable_impact"].toString().isEmpty());
}
void TestPathWeave::reportCalculations() {
    Database db(":memory:");
    Reports r(db);
    QVERIFY(r.funnel()["rejection_rate"].isNull());
    CareerService s(db);
    s.save("applications", {{"company", "A"}, {"title", "X"}, {"status", "rejected"}});
    s.save("applications", {{"company", "B"}, {"title", "Y"}, {"status", "offer"}});
    QCOMPARE(r.funnel()["rejection_rate"].toDouble(), 50.0);
    QCOMPARE(r.funnel()["total"].toInt(), 2);
}
void TestPathWeave::jsonRoundTrip() {
    Database a(":memory:"), b(":memory:");
    CareerService s(a);
    s.loadDemo();
    a.setSetting("online_enabled", "false");
    auto data = a.exportAll();
    b.importAll(data);
    for (auto t : tableNames())
        QCOMPARE(b.count(t), a.count(t));
    QVERIFY(b.query("PRAGMA foreign_key_check").isEmpty());
    QCOMPARE(b.setting("online_enabled"), QString("false"));
}
void TestPathWeave::importRollback() {
    Database db(":memory:");
    db.save("projects", {{"name", "Keep me"}});
    auto data = db.exportAll();
    auto tables = data["tables"].toObject();
    tables["tasks"] = QJsonArray{QJsonObject{
        {"id", 1}, {"title", "Broken"}, {"project_id", 999}, {"created_at", now()}, {"updated_at", now()}}};
    data["tables"] = tables;
    QVERIFY_EXCEPTION_THROWN(db.importAll(data), std::runtime_error);
    QCOMPARE(db.count("projects"), 1);
    QCOMPARE(db.count("tasks"), 0);
}
void TestPathWeave::csvExport() {
    QString data = csv(QJsonArray{QJsonObject{{"title", "=CMD()"}, {"notes", "quoted \"text\", new\nline"}}});
    QVERIFY(data.contains("\"'=CMD()\""));
    QVERIFY(data.contains("\"\"text\"\""));
    QVERIFY(data.endsWith("\r\n"));
}
void TestPathWeave::ftsSearch() {
    Database db(":memory:");
    auto id = db.save("achievements", {{"title", "Tối ưu SQLite"}, {"body", "Fast query"}});
    QCOMPARE(db.search("SQLite").size(), 1);
    db.save("achievements", {{"title", "Revised"}, {"body", "Changed"}}, id);
    QCOMPARE(db.search("SQLite").size(), 0);
    QCOMPARE(db.search("Revised").size(), 1);
    db.remove("achievements", id);
    QCOMPARE(db.search("Revised").size(), 0);
}
void TestPathWeave::sourceErrors() {
    QVERIFY(!NetworkService::responseError(403, QNetworkReply::ContentAccessDenied, false).isEmpty());
    QVERIFY(!NetworkService::responseError(503, QNetworkReply::ServiceUnavailableError, false).isEmpty());
    QVERIFY(!NetworkService::responseError(302, QNetworkReply::NoError, false).isEmpty());
    QVERIFY(NetworkService::responseError(200, QNetworkReply::NoError, false).isEmpty());
}
void TestPathWeave::timeoutHandling() {
    QVERIFY(
        NetworkService::responseError(0, QNetworkReply::OperationCanceledError, true).contains("thời gian"));
}
void TestPathWeave::rateLimitHandling() {
    QVERIFY(NetworkService::responseError(429, QNetworkReply::UnknownContentError, false).contains("429"));
    Database db(":memory:");
    db.setSetting("online_enabled", "true");
    db.setSetting("rate_fixture", QString::number(QDateTime::currentSecsSinceEpoch() + 500));
    NetworkService n(db);
    bool called = false;
    n.get(QUrl("https://example.com/jobs"), "fixture", 30, 60, [&](Response r) {
        called = true;
        QVERIFY(r.error.contains("tần suất"));
    });
    QTRY_VERIFY(called);
}
void TestPathWeave::cacheExpiration() {
    QVERIFY(NetworkService::cacheValid(101, 100));
    QVERIFY(!NetworkService::cacheValid(100, 100));
    QVERIFY(!NetworkService::cacheValid(99, 100));
}
void TestPathWeave::offlineNetwork() {
    Database db(":memory:");
    NetworkService n(db);
    bool called = false;
    n.get(QUrl("https://example.com"), "test", 30, 60, [&](Response r) {
        called = true;
        QVERIFY(r.error.contains("ngoại tuyến"));
    });
    QTRY_VERIFY(called);
}
void TestPathWeave::urlSafety() {
    QVERIFY(NetworkService::safeUrl(QUrl("https://example.com/jobs")));
    for (auto u : {"http://example.com", "https://user:key@example.com", "https://127.0.0.1",
                   "https://localhost/jobs", "https://example.com:8080"})
        QVERIFY(!NetworkService::safeUrl(QUrl(u)));
}
void TestPathWeave::connectors() {
    Database db(":memory:");
    NetworkService n(db);
    RemotiveSource r(n);
    auto jobs = r.parse(
        R"({"jobs":[{"id":1,"title":"C++","company_name":"A","url":"https://remotive.com/job/1","job_type":"part_time","candidate_required_location":"Worldwide","description":"<p>Qt</p>"}]})");
    QCOMPARE(jobs[0].toObject()["workplace_mode"].toString(), QString("remote"));
    QCOMPARE(jobs[0].toObject()["employment_type"].toString(), QString("part_time"));
    QVERIFY_EXCEPTION_THROWN(r.parse("not JSON"), std::runtime_error);
    GreenhouseSource g(n);
    g.configure({{"identifier", "acme"}});
    QVERIFY(g.isConfigured());
    auto gj = g.parse(
        R"({"jobs":[{"id":1,"title":"Remote Engineer","location":{"name":"Remote"},"content":"Hi"}]})");
    QCOMPARE(gj[0].toObject()["workplace_mode"].toString(), QString("unknown"));
    LeverSource lever(n);
    auto lj = lever.parse(
        R"([{"id":"1","text":"Engineer","categories":{"commitment":"Full-time"},"workplaceType":"hybrid"}])");
    QCOMPARE(lj[0].toObject()["employment_type"].toString(), QString("full_time"));
    QCOMPARE(lj[0].toObject()["workplace_mode"].toString(), QString("hybrid"));
    GenericJsonSource json(n);
    json.configure({{"root", "data"}, {"mapping", QJsonObject{{"title", "position.name"}}}});
    QCOMPARE(json.parse(R"({"data":[{"position":{"name":"Developer"}}]})")[0].toObject()["title"].toString(),
             QString("Developer"));
    GenericRssSource rss(n);
    QCOMPARE(rss.parse("<rss><channel><item><title>Engineer</title><link>https://example.com/job</link></"
                       "item></channel></rss>")
                 .size(),
             1);
    QVERIFY_EXCEPTION_THROWN(rss.parse("<rss>"), std::runtime_error);
    UserProvidedUrlSource url(n);
    QCOMPARE(
        url.parse(
               R"(<script type="application/ld+json">{"@type":"JobPosting","title":"Engineer","hiringOrganization":{"name":"A"},"jobLocationType":"TELECOMMUTE"}</script>)")
            [0]
                .toObject()["workplace_mode"]
                .toString(),
        QString("remote"));
}
void TestPathWeave::sourceNotConfigured() {
    Database db(":memory:");
    NetworkService n(db);
    AdzunaSource a(n);
    QVERIFY(!a.isConfigured());
    bool called = false;
    a.search({}, [&](SourceResult r) {
        called = true;
        QVERIFY(!r.error.isEmpty());
    });
    QTRY_VERIFY(called);
}
void TestPathWeave::demoCounts() {
    Database db(":memory:");
    CareerService s(db);
    s.loadDemo();
    QCOMPARE(db.count("current_roles"), 1);
    QCOMPARE(db.count("projects"), 2);
    QCOMPARE(db.count("tasks"), 8);
    QCOMPARE(db.count("achievements"), 5);
    QCOMPARE(db.count("career_evidence"), 4);
    QCOMPARE(db.count("goals"), 3);
    QCOMPARE(db.count("job_listings"), 5);
    QCOMPARE(db.count("applications"), 4);
    QCOMPARE(db.count("interviews"), 2);
    QCOMPARE(db.count("contacts"), 4);
    QCOMPARE(db.count("skills"), 8);
    s.loadDemo();
    QCOMPARE(db.count("tasks"), 8);
    db.save("projects", {{"name", "Real"}});
    db.removeDemo();
    QCOMPARE(db.count("projects"), 1);
    QCOMPARE(db.count("tasks"), 0);
    QVERIFY(db.query("PRAGMA foreign_key_check").isEmpty());
}
void TestPathWeave::dpapi() {
    QTemporaryDir dir;
    SecretStore s(dir.path());
    s.put("key", "fixture-secret");
    QCOMPARE(s.get("key"), QString("fixture-secret"));
    for (auto name : QDir(dir.path()).entryList(QDir::Files)) {
        QFile f(dir.filePath(name));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(!f.readAll().contains("fixture-secret"));
    }
    s.clear();
    QVERIFY(s.get("key").isEmpty());
}
void TestPathWeave::endToEndNativeUi() {
    QTemporaryDir dir;
    QString file = dir.filePath("test.sqlite3");
    {
        Database db(file);
        CareerService s(db);
        SecretStore secrets(dir.filePath("keys"));
        MainWindow window(s, secrets);
        window.show();
        QTest::qWait(30);
        SurveyDialog survey(s, &window);
        survey.show();
        survey.form->setValues({{"display_name", "QA Person"},
                                {"desired_titles", "Software Engineer"},
                                {"employment_types", "full_time,part_time"},
                                {"workplace_modes", "remote"}});
        QTest::mouseClick(survey.findChild<QPushButton*>("surveySave"), Qt::LeftButton);
        QCOMPARE(survey.result(), int(QDialog::Accepted));
        auto create = [&](QString table, QJsonObject row) {
            EntityEditor editor(s, table, 0, &window);
            editor.show();
            editor.form->setValues(row);
            QTest::mouseClick(editor.findChild<QPushButton*>("saveButton"), Qt::LeftButton);
            return editor.savedId;
        };
        auto role =
            create("current_roles", {{"company_name", "QA"}, {"job_title", "Engineer"}, {"is_current", 1}});
        QVERIFY(role > 0);
        auto project =
            create("projects", {{"name", "QA Project"}, {"current_role_id", role}, {"status", "active"}});
        QVERIFY(project > 0);
        QVERIFY(create("tasks", {{"title", "QA Task"}, {"project_id", project}, {"status", "planned"}}) > 0);
        auto achievement =
            create("achievements", {{"title", "QA Win"}, {"body", "User entered"}, {"project_id", project}});
        QVERIFY(achievement > 0);
        QVERIFY(s.convertEvidence(achievement) > 0);
        s.loadDemo();
        window.navigate(1);
        QCoreApplication::processEvents();
        auto* type = window.findChild<QComboBox*>("employmentFilter");
        auto* mode = window.findChild<QComboBox*>("workplaceFilter");
        QVERIFY(type);
        QVERIFY(mode);
        type->setCurrentIndex(type->findData("full_time"));
        QCOMPARE(window.findChild<QListWidget*>("jobList")->count(), 3);
        type->setCurrentIndex(type->findData("part_time"));
        QCOMPARE(window.findChild<QListWidget*>("jobList")->count(), 1);
        type->setCurrentIndex(0);
        mode->setCurrentIndex(mode->findData("remote"));
        QCOMPARE(window.findChild<QListWidget*>("jobList")->count(), 2);
        QTest::mouseClick(window.findChild<QPushButton*>("saveJob"), Qt::LeftButton);
        auto job = db.all("job_listings")[0].toObject()["id"].toInteger();
        auto a = s.apply(job);
        s.save("applications", {{"next_action", "Follow up"}, {"next_action_date", "2026-10-01"}}, a);
        QVERIFY(create("interviews", {{"application_id", a},
                                      {"round", "QA Round"},
                                      {"scheduled_at", "2026-10-02T10:00:00+07:00"}}) > 0);
        auto backup = db.exportAll();
        Database imported(":memory:");
        imported.importAll(backup);
        QCOMPARE(imported.count("applications"), db.count("applications"));
        for (int page = 0; page < 14; ++page) {
            window.navigate(page);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QCoreApplication::processEvents();
        }
        window.close();
    }
    Database reopened(file);
    QCOMPARE(CareerService(reopened).profile()["display_name"].toString(), QString("QA Person"));
    QVERIFY(reopened.count("interviews") >= 3);
    QVERIFY(reopened.count("career_evidence") >= 5);
    QVERIFY(reopened.query("PRAGMA foreign_key_check").isEmpty());
}

void TestPathWeave::actualTimeout() {
    Database db(":memory:");
    db.setSetting("online_enabled", "true");
    FakeNetwork manager;
    manager.delay = 1000;
    NetworkService n(db, nullptr, &manager);
    bool done = false;
    n.get(
        QUrl("https://fixture.example/jobs"), "fixture", 1, 60,
        [&](Response r) {
            done = true;
            QVERIFY(r.error.contains("thời gian"));
        },
        20);
    QTRY_VERIFY_WITH_TIMEOUT(done, 500);
    QCOMPARE(manager.requests, 1);
}
void TestPathWeave::actualCancellation() {
    Database db(":memory:");
    db.setSetting("online_enabled", "true");
    FakeNetwork manager;
    manager.delay = 1000;
    NetworkService n(db, nullptr, &manager);
    bool done = false;
    n.get(QUrl("https://fixture.example/jobs"), "fixture", 1, 60, [&](Response r) {
        done = true;
        QVERIFY(r.error.contains("hủy"));
    });
    n.cancelAll();
    QVERIFY(done);
}
void TestPathWeave::actualRateLimit() {
    Database db(":memory:");
    db.setSetting("online_enabled", "true");
    FakeNetwork manager;
    manager.status = 429;
    NetworkService n(db, nullptr, &manager);
    bool done = false;
    n.get(QUrl("https://fixture.example/jobs"), "fixture", 1, 60, [&](Response r) {
        done = true;
        QCOMPARE(r.status, 429);
        QVERIFY(!r.error.isEmpty());
    });
    QTRY_VERIFY(done);
    QVERIFY(db.setting("rate_fixture").toLongLong() >= QDateTime::currentSecsSinceEpoch() + 110);
}
void TestPathWeave::actualCache() {
    Database db(":memory:");
    db.setSetting("online_enabled", "true");
    FakeNetwork manager;
    NetworkService n(db, nullptr, &manager);
    int count = 0;
    QUrl url("https://fixture.example/jobs");
    n.get(url, "fixture", 1, 60, [&](Response r) {
        ++count;
        QVERIFY(!r.cached);
        QVERIFY(r.error.isEmpty());
    });
    QTRY_COMPARE(count, 1);
    n.get(url, "fixture", 1, 60, [&](Response r) {
        ++count;
        QVERIFY(r.cached);
        QCOMPARE(r.body, manager.payload);
    });
    QTRY_COMPARE(count, 2);
    QCOMPARE(manager.requests, 1);
    db.execute("UPDATE response_cache SET expires_at=?", {QDateTime::currentSecsSinceEpoch() - 1});
    db.setSetting("rate_fixture", "0");
    NetworkService another(db, nullptr, &manager);
    another.get(url, "fixture", 1, 60, [&](Response r) {
        ++count;
        QVERIFY(!r.cached);
    });
    QTRY_COMPARE(count, 3);
    QCOMPARE(manager.requests, 2);
}

void TestPathWeave::backupWithDeletedParent() {
    Database original(":memory:"), restored(":memory:");
    auto parent = original.save("projects", {{"name", "Deleted project"}});
    original.save("tasks", {{"title", "Surviving task"}, {"project_id", parent}});
    original.remove("projects", parent);
    restored.importAll(original.exportAll());
    QCOMPARE(restored.count("projects"), 0);
    QCOMPARE(restored.count("tasks"), 1);
    QVERIFY(restored.query("PRAGMA foreign_key_check").isEmpty());
}
QTEST_MAIN(TestPathWeave)
