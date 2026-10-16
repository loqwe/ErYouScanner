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
    void setKuroLoginInfo(const std::string& uid, const std::string& token);
    void continueLastLogin();
    void run();
    void stop();
    void setScanInterval(int intervalMs);

signals:
    void loginResults(const ScanRet ret, quint64 generation);
    void loginConfirm(const GameType gameType, bool b, quint64 generation);

private:
    ConfigDate* m_config;
    void LoginOfficial();
    void monitorScreen();
    std::atomic<bool> m_stop;
    std::atomic<int> m_scanIntervalMs{ 200 };
    GameType m_gametype{ GameType::UNKNOW };
    ScanRet ret{ ScanRet::UNKNOW };
    const int threadNumber{ 1 };
};
