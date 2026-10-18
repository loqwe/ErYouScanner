#pragma once

#include <string>
#include <string_view>
#include <functional>
#include "ApiDefs.hpp"

#include <Windows.h>

#include <QWidget>
#include <QTemporaryDir>
#include <QTimer>

class QLabel;
#include <WebView2.h>
#include <wil/com.h>
#include <wrl.h>

class WindowGeeTest : public QWidget
{
    Q_OBJECT
public:
    WindowGeeTest(QWidget* parent = nullptr);
    ~WindowGeeTest();
    void Init(const GeetestData& data);
    void diagnosePage(std::function<void(QString)> callback);

signals:
    void postMessage(const QString& Message);
    void closed();
    void failed(const QString& message);

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void cleanup();
    void fail(const QString& message);
    void resizeBrowser();
    void configurePage();
    wil::com_ptr<ICoreWebView2Controller> webViewController{};
    wil::com_ptr<ICoreWebView2> webView{};
    wil::com_ptr<ICoreWebView2_3> virtualHost{};
    QTemporaryDir documentRoot;
    wil::com_ptr<ICoreWebView2Settings> settings;
    EventRegistrationToken messageToken{}, navigationToken{}, windowToken{}, completedToken{};
    bool messageRegistered{}, navigationRegistered{}, windowRegistered{}, completedRegistered{};
    QWidget* host{};
    QLabel* status{};
    QTimer timeout;
    bool active{};
    unsigned generation{};
    std::wstring indexHtml{};
};
