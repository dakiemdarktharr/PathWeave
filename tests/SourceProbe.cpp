#include "sources/Sources.h"
#include <QApplication>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QTextStream>
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 2) {
        QTextStream(stderr) << "Usage: pathweave_source_probe report.json\n";
        return 1;
    }
    pw::Database db(":memory:");
    db.setSetting("online_enabled", "true");
    pw::NetworkService network(db);
    pw::RemotiveSource source(network);
    source.configure({});
    source.search({"C++", {}, {}, 1}, [&](pw::SourceResult result) {
        QJsonObject report = {
            {"source", "Remotive"},
            {"timestamp", pw::now()},
            {"success", result.error.isEmpty()},
            {"job_count", result.jobs.size()},
            {"error", result.error},
            {"cached", result.cached},
            {"note", "Explicit isolated public HTTPS smoke probe; no user profile or credentials sent."}};
        QSaveFile file(QString::fromLocal8Bit(argv[1]));
        if (!file.open(QIODevice::WriteOnly)) {
            app.exit(1);
            return;
        }
        file.write(QJsonDocument(report).toJson());
        file.commit();
        QTextStream(stdout) << QJsonDocument(report).toJson();
        app.exit(result.error.isEmpty() ? 0 : 2);
    });
    return app.exec();
}
