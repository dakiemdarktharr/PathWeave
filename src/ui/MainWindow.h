#include <QLineEdit>
#include <QVBoxLayout>
#pragma once
#include "services/CareerService.h"
#include "services/SecretStore.h"
#include "sources/Sources.h"
#include <QComboBox>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QTableWidget>
#include <QTextBrowser>
#include <memory>
#include <QTimer>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QQueue>
namespace pw {
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(CareerService& service, SecretStore& secrets, QWidget* parent = nullptr);
    void navigate(int page);
    void refresh();
    QWidget* entityPage(const QString& table);
    QJsonArray filteredJobs(const QString& employmentType = {}, const QString& mode = {},
                            const QString& query = {}) const;
    void edit(const QString& table, qint64 id = 0, QJsonObject initial = {});
    void applyTheme();
    ~MainWindow() override;

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    CareerService& service_;
    SecretStore& secrets_;
    NetworkService network_;
    QListWidget* nav_;
    QWidget* body_;
    QVBoxLayout* content_;
    QLineEdit* search_;
    QLabel* heading_;
    QLabel* netStatus_;
    int page_ = 0;
    std::vector<std::shared_ptr<IJobSource>> sources_;
    bool syncing_ = false;
    int syncEpoch_ = 0;
    struct SearchTask {
        std::shared_ptr<IJobSource> source;
        SearchQuery query;
    };
    QQueue<SearchTask> searchQueue_;
    QSystemTrayIcon* tray_ = nullptr;
    bool quitting_ = false;
    void nextSearch();
    void runSavedSearch(qint64 id);
    void cancelSync();
    QWidget* dashboard();
    QWidget* discover(bool saved = false);
    void ensureOnlineDefaults();
    QWidget* settings();
    QWidget* sources();
    void configureSource(QString sourceId);
    void syncSources();
    void exportData();
    void importData();
    void showError(const std::exception& e);
};
} // namespace pw
