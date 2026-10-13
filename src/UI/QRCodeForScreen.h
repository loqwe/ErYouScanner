#pragma once

#include <atomic>
#include <functional>

#include <QMutex>
#include <QThread>

#include "ApiDefs.hpp"
#include "ConfigDate.h"
#include "ScannerBase.hpp"
#include "MhyApi.hpp"

class QRCodeForScreen final :
    public QThread,
    public ScannerBase
{
    Q_OBJECT
public:
    QRCodeForScreen(QObject* parent);
    ~QRCodeForScreen();
    Q_DISABLE_COPY_MOVE(QRCodeForScreen)

    void setLoginInfo1(const std::string& uid, const std::string& stoken, const std::string& mid);
    void continueLastLogin();
    void run();
    void stop();

signals:
    void loginResults(const ScanRet ret);
    void loginConfirm(const GameType gameType, bool b);

private:
    ConfigDate* m_config;
    void LoginOfficial();
    void monitorScreen();
    std::atomic<bool> m_stop;
    GameType m_gametype{ GameType::UNKNOW };
    ScanRet ret{ ScanRet::UNKNOW };
    const int threadNumber{ 1 };
};
