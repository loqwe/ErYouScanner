#include "WindowLoginKuro.h"
#include "KuroCaptchaDialog.hpp"
#include "ScannerLog.hpp"

#include <QCloseEvent>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QUuid>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace {
struct AccountResult
{
    bool valid{};
    QString name;
    QString token;
    QString uid;
    QString error;
};

bool validPhone(const QString& phone)
{
    return QRegularExpression(QStringLiteral("^1[0-9]{10}$")).match(phone).hasMatch();
}

void logKuroResult(const KuroResponse& result)
{
    WriteScannerLog("kuro account: stage=" + result.stage + " http=" + std::to_string(result.httpStatus)
        + " code=" + std::to_string(result.code) + " error=" + std::to_string(static_cast<int>(result.error)));
}
}

WindowLoginKuro::WindowLoginKuro(QWidget* parent, KuroApi::Transport transport)
    : QWidget(parent), deviceCode_(QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString()), transport_(std::move(transport))
{
    setWindowFlags(Qt::Dialog);
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedSize(480, 500);
    setWindowTitle(QStringLiteral("添加库街区账号"));
    setFont(QFont("Microsoft YaHei", 11));
    tabSms = new QWidget(this);
    InitTabSms();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 24);
    layout->addWidget(tabSms);
    statusLabel = new QLabel(this);
    statusLabel->setObjectName(QStringLiteral("kuroLoginStatus"));
    statusLabel->setWordWrap(true);
    statusLabel->setContentsMargins(32, 0, 32, 0);
    layout->addWidget(statusLabel);
    cooldownTimer_.setInterval(250);
    connect(&cooldownTimer_, &QTimer::timeout, this, &WindowLoginKuro::updateSendButton);
}

WindowLoginKuro::~WindowLoginKuro() = default;

void WindowLoginKuro::setReauthentication(const QString& uid, const QString& name)
{
    expectedUid_ = uid;
    setWindowTitle(QStringLiteral("重新登录库街区账号"));
    findChild<QLabel*>(QStringLiteral("kuroLoginTitle"))->setText(QStringLiteral("重新登录"));
    findChild<QLabel*>(QStringLiteral("kuroLoginTip"))->setText(
        QStringLiteral("请使用 UID %1 对应的手机号登录，成功后更新原账号，保留备注与默认设置。").arg(uid));
    nameEdit->setText(name);
    btnSmsConfirm->setText(QStringLiteral("登录并更新账号"));
}

void WindowLoginKuro::closeEvent(QCloseEvent* event)
{
    closing_ = true;
    ++generation_;
    cooldownTimer_.stop();
    if (captchaDialog_) captchaDialog_->close();
    QWidget::closeEvent(event);
}

void WindowLoginKuro::InitTabSms()
{
    auto* layout = new QVBoxLayout(tabSms);
    layout->setContentsMargins(32, 24, 32, 12);
    layout->setSpacing(14);
    auto* title = new QLabel(QStringLiteral("手机号登录"), tabSms);
    title->setObjectName(QStringLiteral("kuroLoginTitle"));
    title->setFont(QFont("Microsoft YaHei", 17, QFont::Bold));
    layout->addWidget(title);
    auto* tip = new QLabel(QStringLiteral("完成安全验证后发送短信，登录成功后自动添加账号。"), tabSms);
    tip->setObjectName(QStringLiteral("kuroLoginTip"));
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral("color: #747b82; font-size: 12px;"));
    layout->addWidget(tip);
    nameEdit = new QLineEdit(tabSms);
    nameEdit->setObjectName(QStringLiteral("kuroAccountRemark"));
    nameEdit->setPlaceholderText(QStringLiteral("账号备注（可选）"));
    nameEdit->setMaxLength(64);
    nameEdit->setMinimumHeight(42);
    layout->addWidget(nameEdit);
    auto* phoneLayout = new QHBoxLayout;
    phoneLayout->addWidget(new QLabel(QStringLiteral("+86"), tabSms));
    phoneEdit = new QLineEdit(tabSms);
    phoneEdit->setObjectName(QStringLiteral("kuroPhone"));
    phoneEdit->setPlaceholderText(QStringLiteral("请输入手机号码"));
    phoneEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,11}")), phoneEdit));
    phoneEdit->setMaxLength(11);
    phoneEdit->setMinimumHeight(44);
    phoneLayout->addWidget(phoneEdit);
    layout->addLayout(phoneLayout);
    auto* codeLayout = new QHBoxLayout;
    codeEdit = new QLineEdit(tabSms);
    codeEdit->setObjectName(QStringLiteral("kuroSmsCode"));
    codeEdit->setPlaceholderText(QStringLiteral("短信验证码"));
    codeEdit->setMaxLength(8);
    codeEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,8}")), codeEdit));
    codeEdit->setEchoMode(QLineEdit::Password);
    codeEdit->setMinimumHeight(44);
    codeLayout->addWidget(codeEdit, 1);
    btnSendCode = new QPushButton(QStringLiteral("获取验证码"), tabSms);
    btnSendCode->setObjectName(QStringLiteral("kuroSendSms"));
    btnSendCode->setMinimumHeight(44);
    btnSendCode->setFixedWidth(140);
    connect(btnSendCode, &QPushButton::clicked, this, &WindowLoginKuro::startSmsVerification);
    codeLayout->addWidget(btnSendCode);
    layout->addLayout(codeLayout);
    btnSmsConfirm = new QPushButton(QStringLiteral("登录并添加账号"), tabSms);
    btnSmsConfirm->setObjectName(QStringLiteral("kuroSmsLogin"));
    btnSmsConfirm->setMinimumHeight(46);
    connect(btnSmsConfirm, &QPushButton::clicked, this, [this] {
        const auto phone = phoneEdit->text().trimmed();
        const auto code = codeEdit->text().trimmed();
        if (!validPhone(phone)) {
            statusLabel->setText(QStringLiteral("请输入有效的 11 位手机号码。"));
            return;
        }
        if (phone != smsPhone_) {
            statusLabel->setText(QStringLiteral("请先为当前手机号获取验证码。"));
            return;
        }
        if (!QRegularExpression(QStringLiteral("^[0-9]{4,8}$")).match(code).hasMatch()) {
            statusLabel->setText(QStringLiteral("请输入 4 至 8 位数字短信验证码。"));
            return;
        }
        verifyAccount(phone, code);
    });
    layout->addWidget(btnSmsConfirm);
    auto* footer = new QLabel(QStringLiteral("此处用于登录库街区账号，游戏扫码时的二次短信验证会另行提示。"), tabSms);
    footer->setWordWrap(true);
    footer->setStyleSheet(QStringLiteral("color: #747b82; font-size: 12px;"));
    layout->addWidget(footer);
    layout->addStretch();
}

void WindowLoginKuro::startSmsVerification()
{
    if (busy_ || closing_) return;
    if (cooldown_.isValid() && cooldown_.elapsed() < 60000) return;
    const auto phone = phoneEdit->text().trimmed();
    if (!validPhone(phone)) {
        statusLabel->setText(QStringLiteral("请输入有效的 11 位手机号码。"));
        return;
    }
    setBusy(true);
    statusLabel->setText(QStringLiteral("请在人机验证窗口完成验证。"));
    const auto generation = ++generation_;
    const QPointer<WindowLoginKuro> guard(this);
    auto* dialog = new KuroCaptchaDialog([guard, phone, generation](QString proof) {
        if (guard && !guard->closing_ && guard->generation_ == generation)
            guard->sendSms(phone, std::move(proof), generation);
    }, this);
    captchaDialog_ = dialog;
    connect(dialog, &QDialog::finished, this, [this, generation](int result) {
        if (closing_ || generation_ != generation || result == QDialog::Accepted) return;
        ++generation_;
        setBusy(false);
        statusLabel->setText(QStringLiteral("验证已取消或超时，未发送短信。"));
        WriteScannerLog("kuro captcha: cancelled or expired");
    });
    dialog->show();
}

void WindowLoginKuro::sendSms(QString phone, QString proof, quint64 generation)
{
    if (closing_ || generation_ != generation) return;
    // Throttle even an ambiguous timeout; the server may already have sent the SMS.
    cooldown_.start();
    cooldownTimer_.start();
    smsPhone_ = phone;
    codeEdit->clear();
    statusLabel->setText(QStringLiteral("正在发送验证码，请稍候……"));
    auto* watcher = new QFutureWatcher<KuroResponse>(this);
    connect(watcher, &QFutureWatcher<KuroResponse>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (closing_ || generation_ != generation) return;
        logKuroResult(result);
        setBusy(false);
        if (result.success())
            statusLabel->setText(QStringLiteral("验证码已发送，请填写收到的短信验证码。"));
        else if (result.error == KuroError::Timeout || result.error == KuroError::Network)
            statusLabel->setText(QStringLiteral("发送结果未知。若已收到短信可继续登录；否则请等待倒计时后重试。"));
        else {
            smsPhone_.clear();
            statusLabel->setText(QStringLiteral("发送失败：%1。").arg(QString::fromStdString(result.message)));
        }
    });
    watcher->setFuture(QtConcurrent::run([phone = std::move(phone), proof = std::move(proof), transport = transport_, deviceCode = deviceCode_] {
        try {
            return KuroApi({}, transport, deviceCode).sendLoginSms(phone.toStdString(), proof.toStdString());
        } catch (...) {
            WriteScannerLog("kuro account: unexpected exception sending login SMS");
            KuroResponse result;
            result.stage = "getSmsCode";
            result.error = KuroError::Network;
            result.message = "短信请求异常";
            return result;
        }
    }));
    updateSendButton();
}

void WindowLoginKuro::setBusy(bool busy)
{
    busy_ = busy;
    btnSmsConfirm->setEnabled(!busy);
    phoneEdit->setEnabled(!busy);
    codeEdit->setEnabled(!busy);
    nameEdit->setEnabled(!busy);
    updateSendButton();
}

void WindowLoginKuro::updateSendButton()
{
    const auto remaining = cooldown_.isValid() ? (60000 - cooldown_.elapsed() + 999) / 1000 : 0;
    if (remaining <= 0) cooldownTimer_.stop();
    btnSendCode->setEnabled(!busy_ && remaining <= 0);
    btnSendCode->setText(remaining > 0 ? QStringLiteral("%1 秒后重发").arg(remaining) : QStringLiteral("获取验证码"));
}

void WindowLoginKuro::verifyAccount(QString phone, QString code)
{
    if (busy_ || closing_) return;
    setBusy(true);
    const auto generation = ++generation_;
    statusLabel->setText(QStringLiteral("正在登录并校验账号，请稍候……"));
    const auto remark = nameEdit->text().trimmed();
    auto* watcher = new QFutureWatcher<AccountResult>(this);
    connect(watcher, &QFutureWatcher<AccountResult>::finished, this, [this, watcher, remark, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (closing_ || generation_ != generation) return;
        setBusy(false);
        codeEdit->clear();
        if (!result.valid) {
            statusLabel->setText(result.error);
            return;
        }
        if (!expectedUid_.isEmpty() && result.uid != expectedUid_) {
            statusLabel->setText(QStringLiteral("登录账号与原账号不一致，请使用原账号的手机号重新登录。原账号未修改。"));
            return;
        }
        const auto name = remark.isEmpty() ? result.name : remark;
        emit AddUserInfo(name.toStdString(), result.token.toStdString(), result.uid.toStdString(), "", "库街区官服");
        close();
    });
    watcher->setFuture(QtConcurrent::run([phone = std::move(phone), code = std::move(code), transport = transport_, deviceCode = deviceCode_] {
        AccountResult result;
        try {
            WriteScannerLog("kuro account: SMS login verification begin");
            const auto login = KuroApi({}, transport, deviceCode).sdkLogin(phone.toStdString(), code.toStdString());
            logKuroResult(login);
            if (!login.success() || login.token.empty()) {
                result.error = QStringLiteral("手机号登录失败：%1。请检查验证码或稍后重试。").arg(QString::fromStdString(login.message));
                return result;
            }
            const auto account = KuroApi(login.token, transport, deviceCode).checkToken(login.uid);
            logKuroResult(account);
            if (!account.success()) {
                result.error = QStringLiteral("账号校验失败：%1。请检查网络或重新登录。").arg(QString::fromStdString(account.message));
                return result;
            }
            result.valid = true;
            result.token = QString::fromStdString(login.token);
            const auto name = account.name.empty() ? login.name : account.name;
            const auto uid = account.uid.empty() ? login.uid : account.uid;
            result.name = name.empty() ? QStringLiteral("库街区用户") : QString::fromStdString(name);
            result.uid = uid.empty() ? QStringLiteral("kuro:") + QUuid::createUuid().toString(QUuid::WithoutBraces)
                                    : QString::fromStdString(uid);
        } catch (...) {
            WriteScannerLog("kuro account: unexpected exception during SMS login verification");
            result.error = QStringLiteral("登录校验发生异常，请稍后重试。");
        }
        return result;
    }));
}
