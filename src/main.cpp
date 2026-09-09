#include "reports/Reports.h"
#include "ui/MainWindow.h"
#include "ui/SurveyDialog.h"
#include "ui/ResumeDialog.h"
#include "services/BackupCrypto.h"
#include <QCheckBox>
#include <QPushButton>
#include <QApplication>
#include <QCommandLineParser>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMessageBox>
#include <QPrinter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextDocument>
#include <QTimer>
#include <stdexcept>
using namespace pw;
static void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
static void writeFile(QString path, QByteArray bytes) {
    QSaveFile f(path);
    check(f.open(QIODevice::WriteOnly), "Cannot open artifact");
    check(f.write(bytes) == bytes.size(), "Artifact write failed");
    check(f.commit(), "Artifact commit failed");
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("PathWeave");
    QCoreApplication::setApplicationName("PathWeave");
    QCoreApplication::setApplicationVersion(PATHWEAVE_VERSION);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", "Use an isolated local data directory.", "path"});
    parser.addOption({"smoke-test", "Run deterministic installed-app workflow checks, then exit."});
    parser.addOption({"verify-data", "Verify persisted smoke workflow in a second process, then exit."});
    parser.addOption({"artifact-dir", "Smoke report and screenshot output directory.", "path"});
    parser.addOption({"demo-screenshot", "Capture the demo dashboard and exit."});
    parser.process(app);
    QString dir = parser.value("data-dir");
    if (dir.isEmpty())
        dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    QString artifacts = parser.value("artifact-dir");
    if (artifacts.isEmpty())
        artifacts = dir + "/verification";
    QDir().mkpath(artifacts);
    try {
        Database db(dir + "/pathweave.sqlite3");
        CareerService service(db);
        SecretStore secrets(dir + "/credentials");
        MainWindow window(service, secrets);
        window.show();
        if (parser.isSet("smoke-test") || parser.isSet("verify-data") || parser.isSet("demo-screenshot")) {
            QTimer::singleShot(150, &app, [&] {
                try {
                    QJsonArray steps;
                    if (parser.isSet("verify-data")) {
                        check(db.count("applications") >= 1, "Persistence: application missing");
                        check(db.count("resume_drafts") >= 1, "Persistence: CV draft missing");
                        check(db.count("documents") >= 1, "Persistence: CV PDF missing");
                        check(db.count("career_evidence") >= 1, "Persistence: evidence missing");
                        check(db.count("interviews") >= 1, "Persistence: interview missing");
                        check(service.profile()["display_name"] == "QA Local", "Persistence: survey missing");
                        steps.append(
                            "Second process: survey, work, evidence, application and interview persisted");
                        writeFile(artifacts + "/restart-verification.json",
                                  QJsonDocument(QJsonObject{{"passed", true}, {"steps", steps}}).toJson());
                    } else if (parser.isSet("demo-screenshot")) {
                        service.loadDemo();
                        window.refresh();
                        app.processEvents();
                        check(window.grab().save(artifacts + "/dashboard.png"), "Screenshot failed");
                        window.navigate(1);
                        app.processEvents();
                        check(window.grab().save(artifacts + "/discover.png"), "Screenshot failed");
                    } else {
                        check(db.count("current_roles") == 0,
                              "Smoke test requires empty isolated data directory");
                        SurveyDialog survey(service, &window);
                        survey.show();
                        survey.form->setValues({{"display_name", "QA Local"},
                                                {"country", "Vietnam"},
                                                {"desired_titles", "Software Engineer"},
                                                {"employment_types", "full_time,part_time"},
                                                {"workplace_modes", "remote"},
                                                {"hard_skills", "C++, Qt"},
                                                {"timezone", "Asia/Ho_Chi_Minh"}});
                        survey.submit();
                        check(survey.result() == QDialog::Accepted, "Survey failed");
                        steps.append("Native survey saved");
                        auto create = [&](QString table, QJsonObject row) {
                            EntityEditor editor(service, table, 0, &window);
                            editor.show();
                            editor.form->setValues(row);
                            editor.submit();
                            check(editor.result() == QDialog::Accepted,
                                  ("Native form failed: " + table + " · " + editor.errorMessage)
                                      .toUtf8()
                                      .constData());
                            return editor.savedId;
                        };
                        auto role = create("current_roles", {{"company_name", "QA Company"},
                                                             {"job_title", "Software Engineer"},
                                                             {"employment_type", "full_time"},
                                                             {"workplace_mode", "hybrid"},
                                                             {"is_current", 1}});
                        auto project =
                            create("projects",
                                   {{"name", "QA Project"}, {"status", "active"}, {"current_role_id", role}});
                        create("tasks", {{"title", "QA Task"},
                                         {"status", "planned"},
                                         {"priority", "high"},
                                         {"project_id", project},
                                         {"due_date", QDate::currentDate().toString(Qt::ISODate)}});
                        auto achievement =
                            create("achievements", {{"title", "QA Achievement"},
                                                    {"body", "Entered evidence, no invented metrics."},
                                                    {"skills", "C++, Qt"},
                                                    {"project_id", project},
                                                    {"date", QDate::currentDate().toString(Qt::ISODate)}});
                        auto evidence = service.convertEvidence(achievement);
                        check(evidence > 0, "Evidence failed");
                        steps.append("Native forms: role, project, task, achievement; converted evidence");
                        service.loadDemo();
                        check(!db.query("SELECT id FROM job_sources WHERE source_id='demo'").isEmpty(),
                              "Demo source missing");
                        check(window.filteredJobs("full_time").size() == 3, "Full-time filter failed");
                        check(window.filteredJobs("part_time").size() == 1, "Part-time filter failed");
                        check(window.filteredJobs({}, "remote").size() == 2, "Remote filter failed");
                        steps.append(
                            "Demo source configured; full-time, part-time and remote filters passed");
                        auto job = db.all("job_listings")[0].toObject()["id"].toInteger();
                        service.saveJob(job);
                        auto application = service.apply(job);
                        service.save("applications",
                                     {{"next_action", "QA Follow up"},
                                      {"next_action_date", QDate::currentDate().toString(Qt::ISODate)}},
                                     application);
                        create("interviews",
                               {{"application_id", application},
                                {"round", "QA Interview"},
                                {"interview_type", "video"},
                                {"scheduled_at",
                                 QDateTime::currentDateTime().addDays(1).toString(Qt::ISODate)}});
                        create("application_evidence",
                               {{"application_id", application}, {"evidence_id", evidence}});
                        steps.append(
                            "Saved job; application, follow-up, interview and evidence link persisted");
                        auto realJob = service.upsertJob({{"title", "Qt Software Engineer"},
                                                          {"company", "QA CV Employer"},
                                                          {"description", "Qt and C++ desktop work"},
                                                          {"skills", "Qt, C++"}});
                        auto realApplication = service.apply(realJob);
                        ResumeDialog cv(service, realJob, realApplication, 0, &window);
                        cv.show();
                        cv.findChild<QPushButton*>("generateResume")->click();
                        cv.findChild<QCheckBox*>("resumeReviewed")->setChecked(true);
                        writeFile(artifacts + "/cv.pdf", cv.exportBytes("PDF"));
                        writeFile(artifacts + "/cv.html", cv.exportBytes("HTML"));
                        writeFile(artifacts + "/cv.txt", cv.exportBytes("TXT"));
                        cv.findChild<QPushButton*>("attachResume")->click();
                        check(db.get("applications", realApplication)["resume_document_id"].toInteger() > 0,
                              "CV PDF attachment failed");
                        check(db.get("applications", realApplication)["status"] == "preparing",
                              "CV attachment changed application status");
                        app.processEvents();
                        cv.grab().save(artifacts + "/installed-cv-editor.png");
                        cv.close();
                        steps.append("CV editor: JD, local generation, review, PDF/HTML/TXT, version and PDF "
                                     "attachment");
                        auto backup = db.exportAll();
                        auto encrypted =
                            BackupCrypto::encrypt(QJsonDocument(backup).toJson(), "QA fixture password 2026");
                        writeFile(artifacts + "/backup.pwbackup", encrypted);
                        check(BackupCrypto::decrypt(encrypted, "QA fixture password 2026") ==
                                  QJsonDocument(backup).toJson(),
                              "Encrypted backup roundtrip failed");
                        steps.append("Password-encrypted portable backup roundtrip passed");
                        writeFile(artifacts + "/backup.json", QJsonDocument(backup).toJson());
                        Database imported(artifacts + "/roundtrip.sqlite3");
                        imported.importAll(backup);
                        check(imported.count("applications") == db.count("applications"),
                              "JSON import mismatch");
                        writeFile(artifacts + "/applications.csv", csv(db.all("applications")).toUtf8());
                        Reports report(db);
                        writeFile(artifacts + "/evidence.md",
                                  report.markdown(1, QDate::currentDate().toString("yyyy-MM")).toUtf8());
                        writeFile(artifacts + "/evidence.html",
                                  report.html(1, QDate::currentDate().toString("yyyy-MM")).toUtf8());
                        QTextDocument doc;
                        doc.setHtml(report.html(1, QDate::currentDate().toString("yyyy-MM")));
                        QPrinter printer(QPrinter::HighResolution);
                        printer.setOutputFormat(QPrinter::PdfFormat);
                        printer.setOutputFileName(artifacts + "/evidence.pdf");
                        doc.print(&printer);
                        check(QFileInfo(artifacts + "/evidence.pdf").size() > 100, "PDF export failed");
                        steps.append("JSON roundtrip, CSV, Markdown, HTML and PDF exports passed");
                        window.refresh();
                        app.processEvents();
                        window.grab().save(artifacts + "/installed-dashboard.png");
                        writeFile(artifacts + "/smoke-verification.json",
                                  QJsonDocument(
                                      QJsonObject{
                                          {"passed", true},
                                          {"version", PATHWEAVE_VERSION},
                                          {"executable", QCoreApplication::applicationFilePath()},
                                          {"steps", steps},
                                          {"note",
                                           "Native widgets and service workflow; automated form submission, "
                                           "not manual mouse testing. Restart verified separately."}})
                                      .toJson());
                    }
                    app.exit(0);
                } catch (const std::exception& e) {
                    writeFile(artifacts + "/failure.txt", e.what());
                    app.exit(1);
                }
            });
        } else if (db.setting("onboarding_seen") != "true") {
            QTimer::singleShot(0, &window, [&] {
                SurveyDialog d(service, &window);
                d.exec();
                window.refresh();
            });
        }
        return app.exec();
    } catch (const std::exception& e) {
        if (parser.isSet("smoke-test") || parser.isSet("verify-data")) {
            writeFile(artifacts + "/startup-failure.txt", e.what());
            return 1;
        }
        QMessageBox::critical(nullptr, "PathWeave — erreur / lỗi", QString::fromUtf8(e.what()));
        return 1;
    }
}
