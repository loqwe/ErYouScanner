#pragma once

#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>
#include <utility>

class KuroWebLogin final : public QDialog
{
public:
    explicit KuroWebLogin(std::function<void(QString, QString, QString)> onToken, QWidget* parent = nullptr)
        : QDialog(parent), onToken_(std::move(onToken))
    {
        setWindowTitle(QStringLiteral("库街区官方网页登录"));
        resize(1000, 760);
        setAttribute(Qt::WA_DeleteOnClose);
        auto* layout = new QVBoxLayout(this);
        status_ = new QLabel(QStringLiteral("请在库街区官网完成登录，成功后将自动校验并添加账号。"), this);
        status_->setWordWrap(true);
        layout->addWidget(status_);
        host_ = new QWidget(this);
        host_->setAttribute(Qt::WA_NativeWindow);
        host_->installEventFilter(this);
        layout->addWidget(host_, 1);
        auto* cancel = new QPushButton(QStringLiteral("取消登录"), this);
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        layout->addWidget(cancel);
        timeout_.setSingleShot(true);
        connect(&timeout_, &QTimer::timeout, this, [this] {
            fail(QStringLiteral("网页登录已超时，请关闭窗口后重新登录。"));
        });
        timeout_.start(300000);
        QTimer::singleShot(0, this, [this] { initialize(); });
    }

    ~KuroWebLogin() override { cleanup(); }

    void done(int result) override
    {
        cleanup();
        QDialog::done(result);
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
        timeout_.stop();
        if (web2_ && responseRegistered_)
            web2_->remove_WebResourceResponseReceived(responseToken_);
        responseRegistered_ = false;
        web2_.Reset();
        web_.Reset();
        if (controller_) controller_->Close();
        controller_.Reset();
    }

    void fail(const QString& message)
    {
        cleanup();
        status_->setText(message);
    }

    void initialize()
    {
        // This profile belongs only to this login window; credentials use an InPrivate session.
        const auto profile = QCoreApplication::applicationDirPath() + QStringLiteral("/Config/KuroWebView");
        if (!QDir().mkpath(profile)) {
            fail(QStringLiteral("无法创建官方网页登录目录，请检查程序目录写入权限。"));
            return;
        }
        const QPointer<KuroWebLogin> guard(this);
        const HRESULT result = CreateCoreWebView2EnvironmentWithOptions(nullptr,
            profile.toStdWString().c_str(), nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [guard](HRESULT error, ICoreWebView2Environment* environment) -> HRESULT {
                    if (!guard || !guard->active_) return S_OK;
                    if (FAILED(error) || !environment) {
                        guard->fail(QStringLiteral("无法启动网页组件，请安装或修复 Microsoft Edge WebView2 Runtime。"));
                        return S_OK;
                    }
                    Microsoft::WRL::ComPtr<ICoreWebView2Environment10> environment10;
                    Microsoft::WRL::ComPtr<ICoreWebView2ControllerOptions> options;
                    if (FAILED(environment->QueryInterface(IID_PPV_ARGS(&environment10))) ||
                        FAILED(environment10->CreateCoreWebView2ControllerOptions(&options)) ||
                        FAILED(options->put_ProfileName(L"KuroLogin")) ||
                        FAILED(options->put_IsInPrivateModeEnabled(TRUE))) {
                        guard->fail(QStringLiteral("网页组件版本过旧，请更新 Microsoft Edge WebView2 Runtime。"));
                        return S_OK;
                    }
                    const HRESULT created = environment10->CreateCoreWebView2ControllerWithOptions(
                        reinterpret_cast<HWND>(guard->host_->winId()), options.Get(),
                        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [guard](HRESULT controllerError, ICoreWebView2Controller* controller) -> HRESULT {
                                if (!guard || !guard->active_) {
                                    if (controller) controller->Close();
                                    return S_OK;
                                }
                                if (FAILED(controllerError) || !controller) {
                                    guard->fail(QStringLiteral("无法打开库街区登录页面，请重试。"));
                                    return S_OK;
                                }
                                guard->controller_ = controller;
                                if (FAILED(controller->get_CoreWebView2(&guard->web_)) ||
                                    FAILED(guard->web_.As(&guard->web2_))) {
                                    guard->fail(QStringLiteral("网页组件不支持登录响应读取，请更新 WebView2 Runtime。"));
                                    return S_OK;
                                }
                                controller->put_Bounds({0, 0, guard->host_->width(), guard->host_->height()});
                                const HRESULT registered = guard->web2_->add_WebResourceResponseReceived(
                                    Microsoft::WRL::Callback<ICoreWebView2WebResourceResponseReceivedEventHandler>(
                                        [guard](ICoreWebView2*, ICoreWebView2WebResourceResponseReceivedEventArgs* args) -> HRESULT {
                                            if (guard && guard->active_) guard->readLoginResponse(args);
                                            return S_OK;
                                        }).Get(), &guard->responseToken_);
                                if (FAILED(registered)) {
                                    guard->fail(QStringLiteral("无法监听本窗口的登录结果，请重试。"));
                                    return S_OK;
                                }
                                guard->responseRegistered_ = true;
                                if (FAILED(guard->web_->Navigate(L"https://www.kurobbs.com")))
                                    guard->fail(QStringLiteral("无法打开库街区官网，请检查网络后重试。"));
                                return S_OK;
                            }).Get());
                    if (FAILED(created)) guard->fail(QStringLiteral("无法创建官方登录窗口，请重试。"));
                    return S_OK;
                }).Get());
        if (FAILED(result)) fail(QStringLiteral("无法启动网页组件，请安装或修复 Microsoft Edge WebView2 Runtime。"));
    }

    void readLoginResponse(ICoreWebView2WebResourceResponseReceivedEventArgs* args)
    {
        Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
        Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponseView> response;
        if (FAILED(args->get_Request(&request)) || FAILED(args->get_Response(&response))) return;
        LPWSTR rawUri = nullptr;
        if (FAILED(request->get_Uri(&rawUri)) || !rawUri) return;
        const QUrl uri(QString::fromWCharArray(rawUri));
        CoTaskMemFree(rawUri);
        if (uri.scheme() != QStringLiteral("https") || uri.host() != QStringLiteral("api.kurobbs.com") ||
            uri.path() != QStringLiteral("/user/sdkLogin") || !uri.userInfo().isEmpty() ||
            (uri.port() != -1 && uri.port() != 443)) return;
        int status = 0;
        if (FAILED(response->get_StatusCode(&status)) || status != 200) return;
        const QPointer<KuroWebLogin> guard(this);
        response->GetContent(Microsoft::WRL::Callback<ICoreWebView2WebResourceResponseViewGetContentCompletedHandler>(
            [guard](HRESULT error, IStream* content) -> HRESULT {
                if (!guard || !guard->active_ || guard->received_ || FAILED(error) || !content) return S_OK;
                QByteArray bytes;
                char buffer[4096];
                ULONG read = 0;
                HRESULT result;
                do {
                    result = content->Read(buffer, sizeof(buffer), &read);
                    if (FAILED(result) || bytes.size() + read > 256 * 1024) return S_OK;
                    bytes.append(buffer, static_cast<qsizetype>(read));
                } while (result == S_OK && read > 0);
                const auto json = QJsonDocument::fromJson(bytes).object();
                const auto data = json.value(QStringLiteral("data")).toObject();
                const auto token = data.value(QStringLiteral("token")).toString().trimmed();
                if (json.value(QStringLiteral("code")).toInt() != 200 || token.isEmpty()) return S_OK;
                const auto uid = data.value(QStringLiteral("userId")).toVariant().toString();
                const auto name = data.value(QStringLiteral("userName")).toString();
                guard->received_ = true;
                QTimer::singleShot(0, guard.data(), [guard, token, uid, name] {
                    if (!guard || !guard->active_) return;
                    auto callback = std::move(guard->onToken_);
                    guard->accept();
                    if (callback) callback(token, uid, name);
                });
                return S_OK;
            }).Get());
    }

    QWidget* host_{};
    QLabel* status_{};
    QTimer timeout_;
    std::function<void(QString, QString, QString)> onToken_;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
    Microsoft::WRL::ComPtr<ICoreWebView2> web_;
    Microsoft::WRL::ComPtr<ICoreWebView2_2> web2_;
    EventRegistrationToken responseToken_{};
    bool responseRegistered_{};
    bool active_{true};
    bool received_{};
};
