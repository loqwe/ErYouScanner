#pragma once

#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>
#include <QCloseEvent>
#include <QDialog>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <utility>
#include "KuroCaptchaProof.hpp"
#include "ScannerLog.hpp"

// SMS/GeeTest flow adapted from DSVVA/KuRo_Scanner, tag 1.0.0 (GPL-3.0).
// The HTML is hosted directly by WebView2; no detached local HTTP server is needed.
class KuroCaptchaDialog final : public QDialog
{
public:
    explicit KuroCaptchaDialog(std::function<void(QString)> onProof, QWidget* parent = nullptr)
        : QDialog(parent), onProof_(std::move(onProof))
    {
        setObjectName(QStringLiteral("kuroCaptchaDialog"));
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QStringLiteral("库街区 · 人机验证"));
        setWindowModality(Qt::WindowModal);
        resize(440, 490);
        auto* layout = new QVBoxLayout(this);
        status_ = new QLabel(QStringLiteral("正在加载验证，请按提示完成操作。"), this);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        host_ = new QWidget(this);
        host_->setAttribute(Qt::WA_NativeWindow);
        host_->installEventFilter(this);
        layout->addWidget(host_, 1);
        auto* cancel = new QPushButton(QStringLiteral("取消验证"), this);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        layout->addWidget(cancel);
        timeout_.setSingleShot(true);
        connect(&timeout_, &QTimer::timeout, this, [this] { reject(); });
        timeout_.start(120000);
        QTimer::singleShot(0, this, [this] { initialize(); });
    }

    ~KuroCaptchaDialog() override { cleanup(); }

    void done(int result) override
    {
        cleanup();
        QDialog::done(result);
    }

    [[nodiscard]] bool bridgeReady() const { return active_ && ready_; }
    [[nodiscard]] bool challengeReady() const { return active_ && challengeReady_; }
    static QString documentUrl() { return QStringLiteral("https://kuro-captcha.eryou.invalid/index.html"); }

    void diagnosePage(std::function<void(QString)> callback)
    {
        if (!web_) { callback(QStringLiteral("WebView not available")); return; }
        web_->CallDevToolsProtocolMethod(L"Runtime.evaluate",
            LR"JSON({"expression":"JSON.stringify({url:location.href,ready:document.readyState,text:document.getElementById('status')?.textContent,sdk:typeof initGeetest4,hasBridge:!!window.chrome?.webview})","returnByValue":true})JSON",
            Microsoft::WRL::Callback<ICoreWebView2CallDevToolsProtocolMethodCompletedHandler>(
                [callback = std::move(callback)](HRESULT, LPCWSTR result) -> HRESULT {
                    callback(result ? QString::fromWCharArray(result) : QStringLiteral("No page result"));
                    return S_OK;
                }).Get());
    }

    // Both the WebView bridge and regression tests enter through this origin/shape check.
    void handleWebMessage(const QString& source, const QString& message)
    {
        if (!active_ || !ready_ || source != documentUrl() || message.size() > 65536) return;
        const auto document = QJsonDocument::fromJson(message.toUtf8());
        if (!document.isObject()) return;
        const auto envelope = document.object();
        if (envelope.value("type").toString() == QStringLiteral("ready")) {
            challengeReady_ = true;
            return;
        }
        if (envelope.value("type").toString() == QStringLiteral("error")) {
            const auto stage = envelope.value("data").toObject().value("stage").toString();
            for (const auto* allowed : {"bootstrap", "timeout", "geetest", "origin", "initialize"})
                if (stage == QLatin1String(allowed)) WriteScannerLog(std::string("kuro captcha: stage=") + allowed);
            fail(QStringLiteral("人机验证加载失败，请取消后重试，并检查网络。"));
            return;
        }
        if (envelope.value("type").toString() != QStringLiteral("verified") || !envelope.value("data").isObject()) return;
        const auto data = QJsonDocument(envelope.value("data").toObject()).toJson(QJsonDocument::Compact);
        const auto proof = KuroCaptchaProof::normalize(std::string_view(data.constData(), static_cast<size_t>(data.size())));
        if (proof.empty()) {
            fail(QStringLiteral("验证结果不完整，请取消后重新验证。"));
            return;
        }
        WriteScannerLog("kuro captcha: verification completed");
        const auto callback = std::move(onProof_);
        accept();
        if (callback) callback(QString::fromStdString(proof));
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == host_ && event->type() == QEvent::Resize && controller_)
            controller_->put_Bounds({0, 0, host_->width(), host_->height()});
        return QDialog::eventFilter(watched, event);
    }
    void closeEvent(QCloseEvent* event) override
    {
        cleanup();
        QDialog::closeEvent(event);
    }

private:
    void cleanup()
    {
        active_ = false;
        ready_ = false;
        challengeReady_ = false;
        timeout_.stop();
        if (web_) {
            if (messageRegistered_) web_->remove_WebMessageReceived(messageToken_);
            if (navigationRegistered_) web_->remove_NavigationStarting(navigationToken_);
            if (windowRegistered_) web_->remove_NewWindowRequested(windowToken_);
        }
        messageRegistered_ = navigationRegistered_ = windowRegistered_ = false;
        if (virtualHost_) virtualHost_->ClearVirtualHostNameToFolderMapping(L"kuro-captcha.eryou.invalid");
        virtualHost_.Reset();
        web_.Reset();
        if (controller_) controller_->Close();
        controller_.Reset();
    }
    void fail(const QString& message)
    {
        WriteScannerLog("kuro captcha: initialization or verification failed");
        cleanup();
        status_->setText(message);
    }
    void initialize()
    {
        const auto profile = QDir::currentPath() + QStringLiteral("/Config/KuroCaptchaWebView");
        if (!QDir().mkpath(profile)) {
            fail(QStringLiteral("无法创建验证目录，请检查程序目录写入权限。"));
            return;
        }
        const QPointer<KuroCaptchaDialog> guard(this);
        const auto result = CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.toStdWString().c_str(), nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [guard](HRESULT error, ICoreWebView2Environment* environment) -> HRESULT {
                    if (!guard || !guard->active_) return S_OK;
                    if (FAILED(error) || !environment) {
                        guard->fail(QStringLiteral("无法启动验证组件，请安装或修复 Microsoft Edge WebView2 Runtime。"));
                        return S_OK;
                    }
                    Microsoft::WRL::ComPtr<ICoreWebView2Environment10> environment10;
                    Microsoft::WRL::ComPtr<ICoreWebView2ControllerOptions> options;
                    if (FAILED(environment->QueryInterface(IID_PPV_ARGS(&environment10))) ||
                        FAILED(environment10->CreateCoreWebView2ControllerOptions(&options)) ||
                        FAILED(options->put_ProfileName(L"KuroSmsCaptcha")) || FAILED(options->put_IsInPrivateModeEnabled(TRUE))) {
                        guard->fail(QStringLiteral("验证组件版本过旧，请更新 Microsoft Edge WebView2 Runtime。"));
                        return S_OK;
                    }
                    const auto created = environment10->CreateCoreWebView2ControllerWithOptions(
                        reinterpret_cast<HWND>(guard->host_->winId()), options.Get(),
                        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [guard](HRESULT error, ICoreWebView2Controller* controller) -> HRESULT {
                                if (!guard || !guard->active_) {
                                    if (controller) controller->Close();
                                    return S_OK;
                                }
                                if (FAILED(error) || !controller) {
                                    guard->fail(QStringLiteral("无法打开人机验证窗口，请重试。"));
                                    return S_OK;
                                }
                                guard->controller_ = controller;
                                if (FAILED(controller->get_CoreWebView2(&guard->web_))) {
                                    guard->fail(QStringLiteral("无法初始化人机验证页面。"));
                                    return S_OK;
                                }
                                controller->put_Bounds({0, 0, guard->host_->width(), guard->host_->height()});
                                guard->configurePage();
                                return S_OK;
                            }).Get());
                    if (FAILED(created)) guard->fail(QStringLiteral("无法创建人机验证窗口。"));
                    return S_OK;
                }).Get());
        if (FAILED(result)) fail(QStringLiteral("无法启动验证组件，请安装 Microsoft Edge WebView2 Runtime。"));
    }
    void configurePage()
    {
        const QPointer<KuroCaptchaDialog> guard(this);
        messageRegistered_ = SUCCEEDED(web_->add_WebMessageReceived(
            Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [guard](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    if (!guard || !guard->active_) return S_OK;
                    LPWSTR source = nullptr;
                    LPWSTR message = nullptr;
                    if (SUCCEEDED(args->get_Source(&source)) && SUCCEEDED(args->get_WebMessageAsJson(&message)) && source && message &&
                        wcslen(message) <= 65536)
                        guard->handleWebMessage(QString::fromWCharArray(source), QString::fromWCharArray(message));
                    CoTaskMemFree(source);
                    CoTaskMemFree(message);
                    return S_OK;
                }).Get(), &messageToken_));
        navigationRegistered_ = SUCCEEDED(web_->add_NavigationStarting(
            Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                [guard](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                    LPWSTR uri = nullptr;
                    const auto result = args->get_Uri(&uri);
                    const auto text = uri ? QString::fromWCharArray(uri) : QString();
                    if (!guard || !guard->active_ || FAILED(result) || !uri ||
                        text != documentUrl())
                        args->put_Cancel(TRUE);
                    CoTaskMemFree(uri);
                    return S_OK;
                }).Get(), &navigationToken_));
        windowRegistered_ = SUCCEEDED(web_->add_NewWindowRequested(
            Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                    args->put_Handled(TRUE);
                    return S_OK;
                }).Get(), &windowToken_));
        if (!messageRegistered_ || !navigationRegistered_ || !windowRegistered_) {
            fail(QStringLiteral("无法初始化验证消息通道，请重试。"));
            return;
        }
        auto html = QString::fromUtf8(R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>body{margin:0;background:#fff;font:14px 'Microsoft YaHei',sans-serif;color:#52606d;text-align:center}p{margin-top:32px}</style>
</head><body><p id="status">正在加载人机验证...</p><script>
const send=(type,data)=>chrome.webview.postMessage({type,data});
let completed=false;
const failure=stage=>{if(!completed){completed=true;send('error',{stage});}};
const deadline=setTimeout(()=>failure('timeout'),30000);
const script=document.createElement('script');
script.src='https://static.geetest.com/v4/gt4.js';
script.onerror=()=>failure('bootstrap');
script.onload=()=>{try{initGeetest4({captchaId:'__CAPTCHA_ID__',product:'bind',clientType:'android'},captcha=>{
captcha.onReady(()=>{clearTimeout(deadline);document.getElementById('status').textContent='请完成下方验证';captcha.showCaptcha();send('ready');})
.onSuccess(()=>{const proof=captcha.getValidate();if(proof&&!completed){completed=true;send('verified',proof);}})
.onError(()=>failure('geetest'));
});}catch(e){failure(e.name==='SecurityError'?'origin':'initialize');}};
document.head.appendChild(script);
</script></body></html>)HTML");
        html.replace(QStringLiteral("__CAPTCHA_ID__"), QString::fromLatin1(KuroCaptchaProof::captchaId.data(),
            static_cast<qsizetype>(KuroCaptchaProof::captchaId.size())));
        // Give the SDK a regular HTTPS origin (storage is unavailable on opaque about:blank).
        // WebView2 serves this host locally; it is never resolved over the network.
        if (!documentRoot_.isValid()) {
            fail(QStringLiteral("无法创建验证页面临时目录。"));
            return;
        }
        QSaveFile page(documentRoot_.filePath(QStringLiteral("index.html")));
        const auto bytes = html.toUtf8();
        if (!page.open(QIODevice::WriteOnly) || page.write(bytes) != bytes.size() || !page.commit() ||
            FAILED(web_.As(&virtualHost_)) || FAILED(virtualHost_->SetVirtualHostNameToFolderMapping(
                L"kuro-captcha.eryou.invalid", documentRoot_.path().toStdWString().c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS))) {
            fail(QStringLiteral("无法初始化验证页面，请检查目录权限及 WebView2 版本。"));
            return;
        }
        ready_ = true;
        if (FAILED(web_->Navigate(documentUrl().toStdWString().c_str())))
            fail(QStringLiteral("无法加载人机验证页面。"));
    }

    std::function<void(QString)> onProof_;
    QWidget* host_{};
    QLabel* status_{};
    QTimer timeout_;
    bool active_{true};
    bool ready_{};
    bool challengeReady_{};
    bool messageRegistered_{};
    bool navigationRegistered_{};
    bool windowRegistered_{};
    EventRegistrationToken messageToken_{};
    EventRegistrationToken navigationToken_{};
    EventRegistrationToken windowToken_{};
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> web_;
    Microsoft::WRL::ComPtr<ICoreWebView2_3> virtualHost_;
    QTemporaryDir documentRoot_;
};
