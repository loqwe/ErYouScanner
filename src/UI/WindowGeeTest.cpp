#include "WindowGeeTest.h"
#include <nlohmann/json.hpp>
#include <QCloseEvent>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QSaveFile>
#include <QVBoxLayout>
#include "ScannerLog.hpp"

namespace {
constexpr auto DocumentUrl = L"https://mhy-captcha.eryou.invalid/index.html";
constexpr auto DocumentHost = L"mhy-captcha.eryou.invalid";
}

WindowGeeTest::WindowGeeTest(QWidget* parent) : QWidget(parent)
{
    setWindowFlags(Qt::Window);
    setFixedSize(QSize(440, 500));
    setWindowTitle("请完成验证");
    auto* layout = new QVBoxLayout(this);
    status = new QLabel(this);
    status->setWordWrap(true);
    layout->addWidget(status);
    host = new QWidget(this);
    host->setAttribute(Qt::WA_NativeWindow);
    host->installEventFilter(this);
    layout->addWidget(host, 1);
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, this, [this] {
        WriteScannerLog("mhy captcha: stage=timeout");
        fail("人机验证加载超时，请检查网络后重试。短信尚未确认发送。");
    });
}

WindowGeeTest::~WindowGeeTest() { cleanup(); }

void WindowGeeTest::cleanup()
{
    active = false;
    ++generation;
    timeout.stop();
    if (webView) {
        if (messageRegistered) webView->remove_WebMessageReceived(messageToken);
        if (navigationRegistered) webView->remove_NavigationStarting(navigationToken);
        if (windowRegistered) webView->remove_NewWindowRequested(windowToken);
        if (completedRegistered) webView->remove_NavigationCompleted(completedToken);
    }
    messageRegistered = navigationRegistered = windowRegistered = completedRegistered = false;
    if (virtualHost) virtualHost->ClearVirtualHostNameToFolderMapping(DocumentHost);
    virtualHost.reset();
    settings.reset();
    webView.reset();
    if (webViewController) webViewController->Close();
    webViewController.reset();
}

void WindowGeeTest::fail(const QString& message)
{
    if (!active) return;
    WriteScannerLog("mhy captcha: stage=failed");
    cleanup();
    status->setText(message);
    emit failed(message);
}

void WindowGeeTest::closeEvent(QCloseEvent* event)
{
    cleanup();
    emit closed();
    QWidget::closeEvent(event);
}

void WindowGeeTest::resizeBrowser()
{
    // WebView2 requires physical pixels; Qt widget sizes use logical pixels.
    RECT bounds{};
    if (webViewController && GetClientRect(reinterpret_cast<HWND>(host->winId()), &bounds))
        webViewController->put_Bounds(bounds);
}

bool WindowGeeTest::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == host && event->type() == QEvent::Resize) resizeBrowser();
    return QWidget::eventFilter(watched, event);
}

void WindowGeeTest::diagnosePage(std::function<void(QString)> callback)
{
    if (!webView) { callback("WebView unavailable"); return; }
    // Exclude page text, request URLs and verification credentials.
    webView->ExecuteScript(LR"JS(JSON.stringify({origin:location.origin,ready:document.readyState,sdk:typeof initGeetest4,storage:(()=>{try{localStorage.getItem('diagnostic');return 'available';}catch(e){return e.name;}})(),challengeReady:!!window.__captchaReady,visibleCaptchaNodes:[...document.querySelectorAll('[class*="geetest"]')].filter(e=>{const r=e.getBoundingClientRect();return r.width>0&&r.height>0&&getComputedStyle(e).visibility!=='hidden';}).length,viewportWidth:innerWidth,errors:window.__diagnosticErrors||[]}))JS",
        Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [callback = std::move(callback)](HRESULT hr, LPCWSTR result) -> HRESULT {
                callback(result ? QString::fromWCharArray(result) : QString::number(hr));
                return S_OK;
            }).Get());
}

void WindowGeeTest::Init(const GeetestData& data)
{
    nlohmann::json config;
    if (data.use_v4) {
        config = {{"captchaId", data.gt}, {"product", "bind"}, {"language", "zho"},
            {"hideSuccess", true}, {"userInfo", nlohmann::json{{"session_id", data.session_id}}.dump()}};
        if (!data.risk_type.empty()) config["riskType"] = data.risk_type;
    } else {
        config = {{"protocol", "https://"}, {"gt", data.gt}, {"new_captcha", data.new_captcha},
            {"product", "bind"}, {"api_server", "apiv6.geetest.com"}, {"offline", data.offline}};
        if (!data.challenge.empty()) config["challenge"] = data.challenge;
    }
    auto html = QString::fromUtf8(R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>body{margin:0;background:white;font:14px sans-serif;text-align:center;color:#52606d}p{margin-top:30px}</style>
</head><body><p id="status">正在加载人机验证...</p><script>
const send=(event,data)=>chrome.webview.postMessage({event,data});
let finished=false;
window.__diagnosticErrors=[];
const failure=stage=>{if(!finished){finished=true;send('error',{stage});}};
window.addEventListener('error',e=>{if(window.__diagnosticErrors.length<8)window.__diagnosticErrors.push(e.error?.name==='SecurityError'?'SecurityError':'ScriptError');failure('script');});
window.addEventListener('unhandledrejection',()=>failure('promise'));
const script=document.createElement('script');
script.src='__SCRIPT__';
script.onerror=()=>failure('bootstrap');
script.onload=()=>{send('stage',{stage:'sdk'});try{
const useV4=__V4__;
(useV4?window.initGeetest4:window.initGeetest)(__CONFIG__,captcha=>{
captcha.onReady(()=>{window.__captchaReady=true;document.getElementById('status').textContent='请完成验证';send('stage',{stage:'ready'});if(useV4)captcha.showCaptcha();else captcha.verify();});
captcha.onSuccess(()=>{if(!finished){finished=true;send('success',captcha.getValidate());}});
captcha.onError(()=>failure('geetest'));
captcha.onClose(()=>send('cancel'));
});}catch(e){failure(e.name==='SecurityError'?'origin':'initialize');}};
send('stage',{stage:'page'});
document.head.appendChild(script);
</script></body></html>)HTML");
    html.replace("__SCRIPT__", data.use_v4 ? "https://static.geetest.com/v4/gt4.js" :
        "https://static.geetest.com/static/js/gt.0.5.2.js");
    html.replace("__V4__", data.use_v4 ? "true" : "false");
    auto json = QString::fromStdString(config.dump(-1, ' ', true));
    json.replace('<', "\\u003c");
    html.replace("__CONFIG__", json);
    indexHtml = html.toStdWString();
}

void WindowGeeTest::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (active) return;
    active = true;
    const auto ticket = ++generation;
    status->setText("正在加载验证，请按提示完成操作。");
    timeout.start(35000);
    const auto profile = QDir::currentPath() + "/Config/MhyCaptchaWebView";
    if (!QDir().mkpath(profile)) { fail("无法创建验证目录，请检查写入权限。"); return; }
    WriteScannerLog("mhy captcha: stage=environment");
    const QPointer<WindowGeeTest> guard(this);
    const auto result = CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.toStdWString().c_str(), nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [guard, ticket](HRESULT error, ICoreWebView2Environment* env) -> HRESULT {
                if (!guard || !guard->active || guard->generation != ticket) return S_OK;
                if (FAILED(error) || !env) {
                    guard->fail("无法初始化人机验证窗口，请检查 Microsoft Edge WebView2 Runtime。");
                    return S_OK;
                }
                const auto created = env->CreateCoreWebView2Controller(reinterpret_cast<HWND>(guard->host->winId()),
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [guard, ticket](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
                            if (!guard || !guard->active || guard->generation != ticket) {
                                if (controller) controller->Close();
                                return S_OK;
                            }
                            if (FAILED(error) || !controller) {
                                guard->fail("无法创建人机验证窗口，请检查 Microsoft Edge WebView2 Runtime。");
                                return S_OK;
                            }
                            guard->webViewController = controller;
                            if (FAILED(controller->get_CoreWebView2(guard->webView.put()))) {
                                guard->fail("无法初始化人机验证页面。");
                                return S_OK;
                            }
                            controller->put_IsVisible(TRUE);
                            guard->resizeBrowser();
                            guard->configurePage();
                            return S_OK;
                        }).Get());
                if (FAILED(created)) guard->fail("无法创建人机验证窗口。");
                return S_OK;
            }).Get());
    if (FAILED(result)) fail("无法启动人机验证组件，请检查 WebView2 Runtime。");
}

void WindowGeeTest::configurePage()
{
    const QPointer<WindowGeeTest> guard(this);
    const auto ticket = generation;
    if (SUCCEEDED(webView->get_Settings(settings.put()))) settings->put_IsStatusBarEnabled(FALSE);
    messageRegistered = SUCCEEDED(webView->add_WebMessageReceived(
        Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
            [guard, ticket](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                if (!guard || !guard->active || guard->generation != ticket) return S_OK;
                LPWSTR source = nullptr, message = nullptr;
                const auto sourceResult = args->get_Source(&source);
                const auto messageResult = args->get_WebMessageAsJson(&message);
                QJsonObject json;
                if (SUCCEEDED(sourceResult) && SUCCEEDED(messageResult) && source && message &&
                    wcscmp(source, DocumentUrl) == 0 && wcslen(message) <= 65536)
                    json = QJsonDocument::fromJson(QString::fromWCharArray(message).toUtf8()).object();
                CoTaskMemFree(source);
                CoTaskMemFree(message);
                const auto event = json.value("event").toString();
                if (event == "stage" || event == "error") {
                    const auto stage = json.value("data").toObject().value("stage").toString();
                    for (const auto* allowed : {"page", "sdk", "ready", "bootstrap", "geetest", "origin", "initialize", "script", "promise"})
                        if (stage == QLatin1String(allowed)) WriteScannerLog(std::string("mhy captcha: stage=") + allowed);
                    if (stage == "ready") {
                        guard->timeout.stop();
                        guard->status->setText("请在下方完成验证。");
                    }
                }
                if (event == "success" && json.value("data").isObject()) {
                    guard->timeout.stop();
                    WriteScannerLog("mhy captcha: stage=verified");
                    emit guard->postMessage(QString::fromUtf8(QJsonDocument(json.value("data").toObject()).toJson(QJsonDocument::Compact)));
                } else if (event == "cancel") guard->close();
                else if (event == "error") guard->fail("人机验证加载失败，请检查网络后重试。短信尚未确认发送，详情见 Config/scanner.log。");
                return S_OK;
            }).Get(), &messageToken));
    navigationRegistered = SUCCEEDED(webView->add_NavigationStarting(
        Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                if (FAILED(args->get_Uri(&uri)) || !uri || wcscmp(uri, DocumentUrl) != 0) args->put_Cancel(TRUE);
                CoTaskMemFree(uri);
                return S_OK;
            }).Get(), &navigationToken));
    completedRegistered = SUCCEEDED(webView->add_NavigationCompleted(
        Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
            [guard, ticket](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                if (!guard || !guard->active || guard->generation != ticket) return S_OK;
                BOOL success = FALSE;
                args->get_IsSuccess(&success);
                WriteScannerLog(success ? "mhy captcha: stage=navigation-complete" : "mhy captcha: stage=navigation-failed");
                if (!success) guard->fail("人机验证页面加载失败，请检查网络后重试。");
                return S_OK;
            }).Get(), &completedToken));
    windowRegistered = SUCCEEDED(webView->add_NewWindowRequested(
        Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE);
                return S_OK;
            }).Get(), &windowToken));
    if (!messageRegistered || !navigationRegistered || !completedRegistered || !windowRegistered) {
        fail("无法初始化人机验证消息通道。");
        return;
    }
    QSaveFile page(documentRoot.filePath("index.html"));
    const auto bytes = QString::fromStdWString(indexHtml).toUtf8();
    // A local HTTPS virtual host supplies a normal origin and working SDK storage.
    if (!documentRoot.isValid() || !page.open(QIODevice::WriteOnly) ||
        page.write(bytes) != bytes.size() || !page.commit() ||
        FAILED(webView->QueryInterface(IID_PPV_ARGS(virtualHost.put()))) ||
        FAILED(virtualHost->SetVirtualHostNameToFolderMapping(DocumentHost,
            documentRoot.path().toStdWString().c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) {
        fail("无法初始化验证页面，请检查目录权限及 WebView2 版本。");
        return;
    }
    if (FAILED(webView->Navigate(DocumentUrl))) fail("无法加载人机验证页面。");
}
