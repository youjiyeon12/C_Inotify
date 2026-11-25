#pragma once
#include <QMainWindow>
#include <QSet>
#include <optional>
#include <string>

#include "Tracker.h"
#include "Watcher.h"

class CharacterStage;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSocketNotifier;
class QStackedWidget;
class QTextEdit;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    void openFolder(const QString& path);

private slots:
    void onInotifyReady();

private:
    QWidget* buildHeader();
    QWidget* buildBar();
    void showPage(int page);

    void chooseFolder();
    void toggleWatch();
    void startWatch();
    void stopWatch();
    bool watching() const { return watcher_.fd() >= 0; }
    void addEventRow(qint64 when, int kind, bool isDir, const QString& text, const QString& pill);
    void applyFilter();

    void refreshStatus();
    void showChangeDiff();
    void resetBaseline();
    void renderDiff(const std::string& rel, char status, const std::optional<std::string>& before,
                    const std::optional<std::string>& after);
    void updateWatchButton();

    Tracker tracker_;
    Watcher watcher_;
    QSocketNotifier* notifier_ = nullptr;
    QTimer* statusTimer_ = nullptr;
    qint64 baseTime_ = 0;
    QSet<QString> deletedSinceBase_, recreated_;  // 지웠다 다시 만든 파일 표시용

    CharacterStage* stage_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* baseLabel_ = nullptr;
    QPushButton* watchBtn_ = nullptr;
    QPushButton* segLive_ = nullptr;
    QPushButton* segChanges_ = nullptr;
    QStackedWidget* pages_ = nullptr;     // 실시간 목록 / 변경사항
    QStackedWidget* controls_ = nullptr;  // 오른쪽 위 버튼들 (페이지마다 다름)
    QComboBox* filter_ = nullptr;
    QListWidget* events_ = nullptr;
    QListWidget* changes_ = nullptr;
    QTextEdit* diffView_ = nullptr;
};
