#pragma once

#include <vector>

#include <QRunnable>
#include <QMainWindow>
#include <QPointer>
#include <QDialog>

#include "ApiDefs.hpp"
#include "ConfigDate.h"
#include "LiveStreamLink.h"
#include "QRCodeForScreen.h"
#include "QRCodeForStream.h"
#include "ui_WindowMain.h"
#include "WindowLogin.h"
#include "WindowAbout.h"
#include "ConfirmationDelay.hpp"

class configInitLoad :
    public QThread
{
    Q_OBJECT
public:
    configInitLoad() = default;
    void run();
    ~configInitLoad();
signals:
    void userinfoTrue();

private:
    ConfigDate* m_config = &ConfigDate::getInstance();
};

class WindowLogin;
class WindowLoginKuro;

class WindowMain :
    public QMainWindow
{
    Q_OBJECT

public:
    WindowMain(QWidget* parent = nullptr);
    ~WindowMain();
    void insertTableItems(QString uid, QString userName, QString type, QString notes);
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
public Q_SLOTS:
    void AddAccount();
    void SetDefaultAccount();
    void DeleteAccount();

    void pBtstartScreen(bool clicked);
    void pBtStream(bool clicked);
    void pBtStop();

    void checkBoxAutoScreen(bool clicked);
    void checkBoxAutoExit(bool clicked);
    void checkBoxAutoLogin(bool clicked);

    void islogin(const ScanRet ret);
    void loginConfirmTip(const GameType gameType, bool b);
    void getInfo(int x, int y);
    void configInitUpdate();
    void updateNote(QTableWidgetItem* item);
Q_SIGNALS:
    void StopScanner();
    void AccountError();
    void LiveStreamLinkError(LiveStreamStatus status);
    void AccountNotSelected();
    void AccountUnsupported();
    void BackgroundError();
    void StartScanScreen();
    void StartScanLive();

private:
    void scheduleConfirmation(bool screen);
    void showKuroSms(bool screen);
    bool accountControlsBusy() const;
    bool saveUserInfo();
    void dispatchStartup(quint64 generation, std::function<void()> action);
    std::atomic_uint64_t scanGeneration{0};
    bool startupActive{false};
    bool confirmationScreen{true};
    QPointer<QDialog> kuroSmsDialog;
    QThreadPool backgroundTasks;
    std::atomic_bool closing{false};
    ConfirmationDelay confirmationDelay{this};
    QFuture<void> confirmationJob;
    void failure();
    int countA = -1;
    Ui::WindowMain ui;
    ConfigDate* m_config = &ConfigDate::getInstance();
    nlohmann::json userinfo;
    QRCodeForScreen t1;
    QRCodeForStream t2;
    void liveIdError(const LiveStreamStatus status);
    int getSelectedRowIndex();
    bool checkDuplicates(const std::string uid);
    bool GetStreamLink(LivePlatform platform, const std::string& roomid, std::string& url, std::map<std::string, std::string>& heards, quint64 generation);
    configInitLoad configinitload;
    void SetWindowToFront() const;
    WindowLogin* windowLogin{};
    void copyEntireRow(int row);
    void onTableRightClicked(const QPoint& pos);
    void reauthenticateAccount(int row);
    void onAccountReauthenticated(const nlohmann::json& identity, const std::string& name,
        const std::string& token, const std::string& uid, const std::string& mid, const std::string& type);
    QPointer<QWidget> accountLoginDialog;
    void refreshLiveRoomIds();
    void saveLiveRoomId(const QString& roomId);
    void startAccountSelfCheck();
    void setAccountCheckStatus(int row, bool valid, const QString& status);
    QFrame* pillContainer{ nullptr };
    QToolButton* btnMhy{ nullptr };
    QToolButton* btnKuro{ nullptr };
    QToolButton* btnAddAccount{ nullptr };
    QButtonGroup* platformGroup{ nullptr };
    QString currentPlatformFilter{ "mhy" };
    WindowLoginKuro* windowLoginKuro{ nullptr };
    void initCornerSwitcher();
    void onPlatformChanged(const QString& platform);
    void applyPlatformFilter();
    void onAccountAdded(const std::string& name, const std::string& token, const std::string& uid, const std::string& mid, const std::string& type);
};
