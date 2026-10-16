#pragma once

#include "KuroCaptchaDialog.hpp"
#include "KuroApi.hpp"
#include "ApiDefs.hpp"
#include <QHBoxLayout>
#include <QLineEdit>
#include <chrono>
#include <optional>

// Handlers dispatch through the owning window's pool and complete on the GUI thread.
class KuroScanSmsDialog final : public QDialog
{
    using Clock = std::chrono::steady_clock;
public:
    using Send = std::function<void(std::string, std::function<void(KuroResponse)>)>;
    using Confirm = std::function<void(std::string, std::function<void(ScanRet)>)>;
    explicit KuroScanSmsDialog(Send send, Confirm confirm, std::function<void(ScanRet)> result,
        QWidget* parent = nullptr, std::function<Clock::time_point()> now = [] { return Clock::now(); })
        : QDialog(parent), send_(std::move(send)), confirm_(std::move(confirm)), result_(std::move(result)), now_(std::move(now))
    {
        setObjectName(QStringLiteral("kuroScanSmsDialog"));
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QStringLiteral("鸣潮扫码 · 短信验证"));
        setWindowModality(Qt::WindowModal);
        setMinimumWidth(440);
        auto* layout = new QVBoxLayout(this);
        auto* tip = new QLabel(QStringLiteral("本次游戏登录需验证绑定手机号。\n点击发送并完成人机验证，再填写收到的短信验证码。"), this);
        tip->setWordWrap(true);
        layout->addWidget(tip);
        code_ = new QLineEdit(this);
        code_->setObjectName(QStringLiteral("kuroScanSmsCode"));
        code_->setPlaceholderText(QStringLiteral("游戏扫码二次验证短信验证码"));
        code_->setMaxLength(8);
        code_->setEchoMode(QLineEdit::Password);
        layout->addWidget(code_);
        auto* buttons = new QHBoxLayout;
        sendButton_ = new QPushButton(QStringLiteral("验证并发送短信"), this);
        sendButton_->setObjectName(QStringLiteral("kuroScanSendSms"));
        submit_ = new QPushButton(QStringLiteral("提交验证"), this);
        submit_->setObjectName(QStringLiteral("kuroScanSubmit"));
        auto* cancel = new QPushButton(QStringLiteral("取消"), this);
        buttons->addWidget(sendButton_); buttons->addWidget(submit_); buttons->addWidget(cancel);
        layout->addLayout(buttons);
        status_ = new QLabel(QStringLiteral("尚未发送短信，请先完成人机验证。"), this);
        status_->setObjectName(QStringLiteral("kuroScanSmsStatus"));
        status_->setWordWrap(true);
        layout->addWidget(status_);
        expiry_ = new QLabel(this);
        layout->addWidget(expiry_);
        deadline_ = now_() + std::chrono::seconds(90);
        timer_.setInterval(250);
        connect(&timer_, &QTimer::timeout, this, [this] { refresh(); });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(sendButton_, &QPushButton::clicked, this, [this] { verifyAndSend(); });
        connect(submit_, &QPushButton::clicked, this, [this] { submitCode(); });
        timer_.start();
        refresh();
    }

    void done(int value) override
    {
        closed_ = true;
        timer_.stop();
        if (captcha_) captcha_->reject();
        QDialog::done(value);
    }

private:
    void refresh()
    {
        if (closed_) return;
        const auto time = now_();
        if (time >= deadline_) {
            expired_ = true;
            busy_ = false;
            if (captcha_) captcha_->reject();
            status_->setText(QStringLiteral("扫码验证已超时，请取消后重新扫描新的二维码。"));
        }
        const auto seconds = cooldown_ && *cooldown_ > time
            ? (std::chrono::duration_cast<std::chrono::milliseconds>(*cooldown_ - time).count() + 999) / 1000 : 0;
        if (throttled_) sendButton_->setText(QStringLiteral("服务端限流，已停止重试"));
        else sendButton_->setText(seconds > 0 ? QStringLiteral("%1 秒后可重发").arg(seconds) : QStringLiteral("验证并发送短信"));
        sendButton_->setEnabled(!busy_ && !expired_ && !throttled_ && seconds == 0);
        submit_->setEnabled(!busy_ && !expired_);
        code_->setEnabled(!busy_ && !expired_);
        const auto left = (std::chrono::duration_cast<std::chrono::milliseconds>(deadline_ - time).count() + 999) / 1000;
        expiry_->setText(QStringLiteral("本次扫码验证剩余 %1 秒；过期后请重新扫码。").arg((std::max)(0LL, static_cast<long long>(left))));
    }

    void verifyAndSend()
    {
        refresh();
        if (!sendButton_->isEnabled()) return;
        busy_ = true;
        waitingCaptcha_ = true;
        status_->setText(QStringLiteral("请完成人机验证，验证通过后发送短信。"));
        refresh();
        QPointer<KuroScanSmsDialog> guard(this);
        auto* captcha = new KuroCaptchaDialog([guard](QString proof) {
            if (!guard || guard->closed_ || guard->expired_ || !guard->waitingCaptcha_) return;
            guard->waitingCaptcha_ = false;
            guard->cooldown_ = guard->now_() + std::chrono::seconds(60);
            guard->status_->setText(QStringLiteral("人机验证已通过，正在请求发送短信……"));
            guard->refresh();
            if (guard->expired_) return;
            WriteScannerLog("kuro scan SMS: captcha accepted; dispatching send");
            guard->send_(proof.toStdString(), [guard](KuroResponse response) {
                if (guard && !guard->closed_ && !guard->expired_) guard->sent(response);
            });
        }, this);
        captcha_ = captcha;
        connect(captcha, &QDialog::finished, this, [this](int value) {
            if (closed_ || expired_ || value == QDialog::Accepted) return;
            waitingCaptcha_ = false;
            busy_ = false;
            status_->setText(QStringLiteral("人机验证已取消或未完成，未发送短信。"));
            WriteScannerLog("kuro scan SMS: captcha cancelled; no send");
            refresh();
        });
        captcha->show();
    }

    void sent(const KuroResponse& response)
    {
        busy_ = false;
        if (response.success()) status_->setText(QStringLiteral("服务器已接受发送请求，请查收短信并填写验证码。"));
        else if (response.error == KuroError::Timeout || response.error == KuroError::Network)
            status_->setText(QStringLiteral("发送结果未知，未自动重试。若已收到短信可提交，否则等待倒计时后重新验证。"));
        else if (response.error == KuroError::Cooldown) {
            cooldown_ = now_() + std::chrono::milliseconds(response.retryAfterMs);
            status_->setText(QStringLiteral("本机发送间隔不足 60 秒，请等待倒计时后重新验证。"));
        }
        else if (response.error == KuroError::Expired || response.error == KuroError::Cancelled) {
            deadline_ = now_();
        }
        else if (response.error != KuroError::None)
            status_->setText(QStringLiteral("发送失败：%1。未自动重试，请检查网络或重新验证。").arg(QString::fromStdString(response.message)));
        else if (response.code == 242) {
            // Verified on 2026-10-16 against the live endpoint: 242 means the
            // service is throttling this account ("发送短信频繁"), and the real
            // window is far longer than the local 60-second cooldown. Retrying
            // inside the same scan session only renews the throttle, so stop
            // offering it instead of walking the user into the loop.
            throttled_ = true;
            cooldown_.reset();
            const auto reason = QString::fromStdString(response.serverMessage);
            status_->setText(QStringLiteral("服务端限流（业务码 242%1）。本次扫码会话内继续重试不会成功，请停止重试，等待较长时间后再重新扫码。")
                .arg(reason.isEmpty() ? QString() : QStringLiteral("：") + reason));
        }
        else {
            const auto reason = QString::fromStdString(response.serverMessage);
            status_->setText(QStringLiteral("服务器拒绝发送（业务码 %1%2）。请稍后重新验证；若仍失败，请反馈此错误码。")
                .arg(response.code).arg(reason.isEmpty() ? QString() : QStringLiteral("：") + reason));
        }
        WriteScannerLog("kuro scan SMS: result http=" + std::to_string(response.httpStatus) + " code=" +
            std::to_string(response.code) + " error=" + std::to_string(static_cast<int>(response.error)) +
            (response.serverMessage.empty() ? std::string() : " msg=" + response.serverMessage));
        refresh();
    }

    void submitCode()
    {
        refresh();
        if (!submit_->isEnabled()) return;
        const auto sms = code_->text().trimmed();
        if (sms.size() < 4 || sms.size() > 8 || !std::all_of(sms.begin(), sms.end(), [](QChar c) { return c >= '0' && c <= '9'; })) {
            status_->setText(QStringLiteral("请输入短信中的 4 至 8 位数字验证码。"));
            return;
        }
        busy_ = true;
        code_->clear();
        status_->setText(QStringLiteral("正在提交验证……"));
        refresh();
        QPointer<KuroScanSmsDialog> guard(this);
        confirm_(sms.toStdString(), [guard](ScanRet result) {
            if (!guard || guard->closed_ || guard->expired_) return;
            guard->busy_ = false;
            if (result == ScanRet::KURO_SMS_REQUIRED || result == ScanRet::FAILURE_2) {
                guard->status_->setText(QStringLiteral("验证未通过，请核对短信验证码后重新提交。"));
                guard->refresh();
                return;
            }
            const auto complete = guard->result_;
            guard->accept();
            complete(result);
        });
    }

    Send send_;
    Confirm confirm_;
    std::function<void(ScanRet)> result_;
    std::function<Clock::time_point()> now_;
    Clock::time_point deadline_;
    std::optional<Clock::time_point> cooldown_;
    QTimer timer_;
    QPointer<KuroCaptchaDialog> captcha_;
    QLineEdit* code_{};
    QPushButton* sendButton_{};
    QPushButton* submit_{};
    QLabel* status_{};
    QLabel* expiry_{};
    bool busy_{}, waitingCaptcha_{}, expired_{}, closed_{}, throttled_{};
};
