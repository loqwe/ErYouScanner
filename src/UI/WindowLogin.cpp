#include "WindowLogin.h"

#include <thread>
#include <ranges>

#include <Windows.h>

#include <Base64.hpp>
#include <QThread>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include "MhyApi.hpp"
#include "UtilMat.hpp"
#include "CookieParser.hpp"
#include "UrlQuery.hpp"

WindowLogin::WindowLogin(QWidget* parent) :
    QWidget(parent),
    tabWidget(new QTabWidget(this))
{
    setWindowFlags(Qt::Dialog);
    setWindowModality(Qt::WindowModal);
    setMinimumSize(QSize(500, 550));
    setMaximumSize(QSize(500, 550));
    setWindowTitle("添加账号");

    tabWidget->setMinimumSize(QSize(500, 550));
    tabWidget->setMaximumSize(QSize(500, 550));
    for (size_t i{}; i < tabs.size(); i++)
    {
        tabs[i] = new QWidget();
        tabWidget->addTab(tabs[i], tabsName[i].data());
        tabWidget->setFont(QFont("微软雅黑", 11));
    }
    InitTabs0();
    InitTabs1();
    InitTabs2();
    MainHLayout = new QHBoxLayout(this);
    MainHLayout->setContentsMargins(0, 0, 0, 0);
    MainHLayout->addWidget(tabWidget);

    Initconnect();
}

WindowLogin::~WindowLogin()
{
    QrcodePool.clear();
}

void WindowLogin::closeEvent(QCloseEvent* event)
{
    deleteLater();
}

void WindowLogin::InitTabs0()
{
    Tab0MainVLayout = new QVBoxLayout(tabs[0]);
    Tab0MainVLayout->setSpacing(0);
    Tab0MainVLayout->setContentsMargins(15, 15, 15, 15);

    Tab0HLayout0 = new QHBoxLayout();
    Tab0HLayout0->setSpacing(10);
    Tab0HLayout0->setContentsMargins(30, 60, 30, 0);

    AreaCodeLabel = new QLabel(tabs[0]);
    AreaCodeLabel->setAlignment(Qt::AlignCenter);
    AreaCodeLabel->setFont(QFont("微软雅黑", 14));
    AreaCodeLabel->setText("+86");
    Tab0HLayout0->addWidget(AreaCodeLabel);

    phoneNumberLineEdit = new QLineEdit(tabs[0]);
    phoneNumberLineEdit->setMinimumSize(QSize(0, 50));
    phoneNumberLineEdit->setFont(QFont("微软雅黑", 14));
    QRegularExpression regex("^[1][3-9][0-9]{9}$");
    QRegularExpressionValidator* validator = new QRegularExpressionValidator(regex, this);
    phoneNumberLineEdit->setValidator(validator);
    phoneNumberLineEdit->setPlaceholderText("请输入手机号码");
    phoneNumberLineEdit->setClearButtonEnabled(true);
    Tab0HLayout0->addWidget(phoneNumberLineEdit);

    pbtSend = new QPushButton(tabs[0]);
    QSizePolicy sizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    sizePolicy.setHorizontalStretch(0);
    sizePolicy.setVerticalStretch(0);
    sizePolicy.setHeightForWidth(pbtSend->sizePolicy().hasHeightForWidth());
    pbtSend->setSizePolicy(sizePolicy);
    pbtSend->setMinimumSize(QSize(50, 50));
    pbtSend->setFont(QFont("微软雅黑", 14));
    pbtSend->setText("发送");
    Tab0HLayout0->addWidget(pbtSend);

    Tab0MainVLayout->addLayout(Tab0HLayout0);

    Tab0HLayout1 = new QHBoxLayout();
    Tab0HLayout1->setSpacing(0);
    Tab0HLayout1->setContentsMargins(30, -1, 30, 30);
    verifyCodeLineEdit = new QLineEdit(tabs[0]);
    verifyCodeLineEdit->setMinimumSize(QSize(0, 50));
    verifyCodeLineEdit->setFont(QFont("微软雅黑", 14));
    verifyCodeLineEdit->setPlaceholderText("请输入收到的验证码");
    verifyCodeLineEdit->setClearButtonEnabled(true);
    Tab0HLayout1->addWidget(verifyCodeLineEdit);

    Tab0MainVLayout->addLayout(Tab0HLayout1);

    Tab0HLayout2 = new QHBoxLayout();
    Tab0HLayout2->setSpacing(50);
    Tab0HLayout2->setContentsMargins(30, -1, 30, 80);
    Tab0pbtConfirm = new QPushButton(tabs[0]);
    Tab0pbtConfirm->setMinimumSize(QSize(0, 50));
    Tab0pbtConfirm->setFont(QFont("微软雅黑", 14));
    Tab0pbtConfirm->setText("确认");
    Tab0HLayout2->addWidget(Tab0pbtConfirm);
    Tab0pbtCancel = new QPushButton(tabs[0]);
    Tab0pbtCancel->setFont(QFont("微软雅黑", 14));
    Tab0pbtCancel->setText("取消");
    Tab0pbtCancel->setMinimumSize(QSize(0, 50));
    Tab0HLayout2->addWidget(Tab0pbtCancel);

    Tab0MainVLayout->addLayout(Tab0HLayout2);

    Tabs0Timer = new QTimer(this);

    pbtSend->setEnabled(false);
    Tab0pbtConfirm->setEnabled(false);
}

void WindowLogin::InitTabs1()
{
    Tab1MainVLayout = new QVBoxLayout(tabs[1]);

    Prompt = new QLabel(tabs[1]);
    Prompt->setText("打开米游社APP，扫一扫登录");
    Prompt->setMaximumSize(QSize(100000, 50));
    Prompt->setFont(QFont("微软雅黑", 15));
    Prompt->setAlignment(Qt::AlignCenter);

    Tab1MainVLayout->addWidget(Prompt);

    QRCodelabel = new QLabel(tabs[1]);

    Tab1MainVLayout->addWidget(QRCodelabel);

    QRCodeQImage.fill(255);
    QRCodelabel->setPixmap(QPixmap::fromImage(QRCodeQImage));
    QRCodelabel->setAlignment(Qt::AlignCenter);
    QRCodelabel->setText("二维码加载中");
    QRCodelabel->setFont(QFont("微软雅黑", 15));
    //QRCodelabel->setStyleSheet("border: 2px solid black;");

    UpdateQrcodeButton = new QPushButton("二维码已过期\n点击刷新二维码", tabs[1]);
    UpdateQrcodeButton->setFont(QFont("微软雅黑", 13));
    UpdateQrcodeButton->setFixedSize(170, 170);
    UpdateQrcodeButton->setVisible(false);

    QrcodeTimer = new QTimer(this);

    QrcodePool.setMaxThreadCount(1);
}

void WindowLogin::InitTabs2()
{
    Tab2MainVLayout = new QVBoxLayout(tabs[2]);
    Tab2MainVLayout->setSpacing(0);
    Tab2MainVLayout->setObjectName(QString::fromUtf8("verticalLayout"));
    Tab2MainVLayout->setSizeConstraint(QLayout::SetDefaultConstraint);
    Tab2MainVLayout->setContentsMargins(30, 80, 30, 0);

    lineEditCookie = new QLineEdit(tabs[2]);
    lineEditCookie->setObjectName(QString::fromUtf8("lineEditCookie"));
    lineEditCookie->setMinimumSize(QSize(0, 50));
    lineEditCookie->setAlignment(Qt::AlignLeading | Qt::AlignLeft | Qt::AlignVCenter);
    lineEditCookie->setFont(QFont("微软雅黑", 14));
    lineEditCookie->setPlaceholderText("在这里粘贴包含SToken的Cookie...");
    lineEditCookie->setClearButtonEnabled(true);
    Tab2MainVLayout->addWidget(lineEditCookie);

    pBtofficialLogin = new QPushButton(tabs[2]);
    pBtofficialLogin->setMinimumSize(QSize(0, 50));
    pBtofficialLogin->setText("登录");
    pBtofficialLogin->setFont(QFont("微软雅黑", 13));
    pBtofficialreject = new QPushButton(tabs[2]);
    pBtofficialreject->setMinimumSize(QSize(0, 50));
    pBtofficialreject->setText("取消");
    pBtofficialreject->setFont(QFont("微软雅黑", 13));

    Tab2ButtonHLayout = new QHBoxLayout();
    Tab2ButtonHLayout->addWidget(pBtofficialLogin);
    Tab2ButtonHLayout->addWidget(pBtofficialreject);
    Tab2ButtonHLayout->setContentsMargins(10, 50, 10, 60);
    Tab2ButtonHLayout->setSpacing(50);
    Tab2MainVLayout->addLayout(Tab2ButtonHLayout);
}


void WindowLogin::requestSms(const std::string& aigis)
{
    if (smsPending || Tabs0Timer->isActive() || !phoneNumberLineEdit->hasAcceptableInput()) return;
    const auto mobile = phoneNumberLineEdit->text();
    smsPending = true;
    phoneNumberLineEdit->setEnabled(false);
    pbtSend->setEnabled(false);
    pbtSend->setText("请求中");
    Tab0pbtConfirm->setEnabled(false);
    if (aigis.empty()) GeeTestInfo = {};
    auto* watcher = new QFutureWatcher<MhySmsResult>(this);
    connect(watcher, &QFutureWatcher<MhySmsResult>::finished, this, [this, watcher, mobile] {
        const auto result = watcher->result();
        watcher->deleteLater();
        smsPending = false;
        if (result.retcode == 0)
        {
            GeeTestInfo = result.data;
            smsMobile = mobile;
            remainingSeconds = result.countdown;
            pbtSend->setText(QString::number(remainingSeconds) + "秒");
            Tabs0Timer->start(1000);
            phoneNumberLineEdit->setEnabled(true);
            Tab0pbtConfirm->setEnabled(true);
            verifyCodeLineEdit->setFocus();
            return;
        }
        if (result.retcode == -3101 && result.data.mmt_type)
        {
            GeeTestInfo = result.data;
            pbtSend->setText("待验证");
            m_WindowGeeTest.Init(GeeTestInfo);
            m_WindowGeeTest.show();
            return;
        }
        phoneNumberLineEdit->setEnabled(true);
        pbtSend->setEnabled(phoneNumberLineEdit->hasAcceptableInput());
        pbtSend->setText("发送");
        if (result.retcode == MhySmsTransportError)
            emit showMessagebox("短信请求结果未知，请先查看手机是否收到短信，避免重复发送。详细信息见 Config/scanner.log。");
        else if (result.retcode == -3008)
            emit showMessagebox("手机号格式错误，请检查后重试。");
        else if (result.retcode == -3006)
            emit showMessagebox("请求过于频繁，请稍后再试。");
        else if (result.retcode == -3101)
            emit showMessagebox("服务器要求人机验证，但未返回可用的验证参数。请稍后重试。");
        else if (result.retcode == MhySmsInvalidResponse)
            emit showMessagebox("平台响应格式异常，暂时无法确认短信发送状态，请先查看手机。详细信息见 Config/scanner.log。");
        else
            emit showMessagebox(QString("申请短信验证码失败（返回码 %1），详细信息见 Config/scanner.log。").arg(result.retcode));
    });
    watcher->setFuture(QtConcurrent::run([mobile = mobile.toStdString(), aigis] {
        return CreateLoginCaptcha(mobile, aigis);
    }));
}

void WindowLogin::Initconnect()
{
    connect(this, &WindowLogin::showMessagebox, this, [this](const QString& Message) {
        QMessageBox::information(this, "提示", Message, QMessageBox::Yes);
    });

    connect(this, &WindowLogin::showWindowGeeTest, this, [this](const bool show) {
        if (show)
        {
            m_WindowGeeTest.show();
        }
        else
        {
            m_WindowGeeTest.close();
        }
    });

    connect(this, &WindowLogin::QrcodeLoginResult, this, [this](bool result) {
        if (result)
        {
            QrcodeTimer->stop();
        }
        else
        {
            QRCodeQImage = CV_8UC1_MatToQImage(QrcodeMat - cv::Scalar(200));
            QRCodelabel->setPixmap(QPixmap::fromImage(QRCodeQImage));
            UpdateQrcodeButton->setVisible(true);
        }
    });

    connect(tabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        QrcodeTimer->stop();
        QRCodeQImage.fill(255);
        QRCodelabel->setText("二维码加载中");
        switch (index)
        {
        case 0:
        {
            break;
        }
        case 1:
        {
            StartQRCodeLogin();
            break;
        }
        case 2:
        {
            break;
        }
        default:
            __assume(0);
        }
    });

    connect(phoneNumberLineEdit, &QLineEdit::textChanged, this, [this] {
        pbtSend->setEnabled(!smsPending && !Tabs0Timer->isActive() && phoneNumberLineEdit->hasAcceptableInput());
        Tab0pbtConfirm->setEnabled(!smsPending && phoneNumberLineEdit->text() == smsMobile &&
            !GeeTestInfo.action_type.empty());
    });

    connect(&m_WindowGeeTest, &WindowGeeTest::postMessage, this, [this](const QString& Message) {
        if (GeeTestInfo.GeeTestType == ServerType::Official && !smsPending)
        {
            const auto aigis = GeeTestInfo.session_id + ";" + Message.toUtf8().toBase64().toStdString();
            m_WindowGeeTest.close();
            requestSms(aigis);
        }
    });

    connect(&m_WindowGeeTest, &WindowGeeTest::closed, this, [this] {
        if (smsPending) return;
        phoneNumberLineEdit->setEnabled(true);
        pbtSend->setText("发送");
        pbtSend->setEnabled(!Tabs0Timer->isActive() && phoneNumberLineEdit->hasAcceptableInput());
    });
    connect(&m_WindowGeeTest, &WindowGeeTest::failed, this, [this](const QString& message) {
        m_WindowGeeTest.close();
        emit showMessagebox(message);
    });

    connect(Tabs0Timer, &QTimer::timeout, this, [this]() {
        remainingSeconds--;
        pbtSend->setText(QString::number(remainingSeconds) + "秒");
        if (remainingSeconds <= 0)
        {
            Tabs0Timer->stop();
            pbtSend->setEnabled(!smsPending && phoneNumberLineEdit->hasAcceptableInput());
            pbtSend->setText("发送");
        }
    });

    connect(pbtSend, &QPushButton::clicked, this, [this] {
        requestSms();
    });

    connect(Tab0pbtConfirm, &QPushButton::clicked, this, [this] {
        const auto mobile = phoneNumberLineEdit->text();
        if (smsPending || mobile != smsMobile || GeeTestInfo.action_type.empty()) return;
        const auto captcha = verifyCodeLineEdit->text().toStdString();
        if (captcha.empty()) { emit showMessagebox("请输入收到的短信验证码"); return; }
        smsPending = true;
        phoneNumberLineEdit->setEnabled(false);
        pbtSend->setEnabled(false);
        Tab0pbtConfirm->setEnabled(false);
        using LoginResult = std::pair<MhyMobileLoginResult, std::string>;
        auto* watcher = new QFutureWatcher<LoginResult>(this);
        connect(watcher, &QFutureWatcher<LoginResult>::finished, this, [this, watcher] {
            auto [result, name] = watcher->result();
            watcher->deleteLater();
            smsPending = false;
            phoneNumberLineEdit->setEnabled(true);
            pbtSend->setEnabled(!Tabs0Timer->isActive() && phoneNumberLineEdit->hasAcceptableInput());
            Tab0pbtConfirm->setEnabled(phoneNumberLineEdit->text() == smsMobile);
            if (result.retcode == -3205)
            {
                emit showMessagebox("短信验证码错误");
            }
            else if (result.retcode == 0)
            {
                emit AddUserInfo(name, result.data.V2Token, result.data.aid, result.data.mid, "官服");
            }
            else if (result.retcode == MhySmsTransportError)
                emit showMessagebox("登录请求结果未知，请检查网络后再操作。程序未自动重试。");
            else if (result.retcode == MhySmsInvalidResponse)
                emit showMessagebox("登录响应格式异常，未添加账号。详细信息见 Config/scanner.log。");
            else
                emit showMessagebox(QString("短信登录失败（返回码 %1），详细信息见 Config/scanner.log。").arg(result.retcode));
        });
        watcher->setFuture(QtConcurrent::run([action = GeeTestInfo.action_type, mobile = mobile.toStdString(), captcha] {
            auto result = LoginByMobileCaptcha(action, mobile, captcha);
            std::string name;
            if (result.retcode == 0)
            {
                try { name = getMysUserName(result.data.aid); }
                catch (...) { WriteScannerLog("mhy sms: username lookup failed"); }
                if (name.empty()) name = result.data.aid;
            }
            return LoginResult{std::move(result), std::move(name)};
        }));
    });

    connect(pBtofficialLogin, &QPushButton::clicked, this, [this] {
        QThreadPool::globalInstance()->start([this] {
            std::string CookieString{ lineEditCookie->text().toStdString() };
            CookieParser cp(CookieString);
            std::string uid{}, stoken{}, mid{};
            static constinit const std::array<std::string_view, 3> keys{ "stuid", "ltuid", "account_id" };
            auto result = std::ranges::find_if(keys, [&cp, &uid](const std::string_view key) {
                if (auto value = cp[key]; value.has_value())
                {
                    uid = *value;
                    return true;
                }
                return false;
            });
            if (result == keys.end())
            {
                Q_EMIT showMessagebox("Cookie格式错误!");
                return;
            }
            if (auto value = cp["stoken"]; value.has_value())
            {
                stoken = *value;
            }
            else
            {
                Q_EMIT showMessagebox("Cookie格式错误!");
                return;
            }
            if (auto value = cp["mid"]; value.has_value())
            {
                mid = *value;
            }
            else
            {
                Q_EMIT showMessagebox("Cookie格式错误!");
                return;
            }
            const std::string name{ getMysUserName(uid) };
            Q_EMIT AddUserInfo(name, stoken, uid, mid, "官服");
        });
    });

    connect(UpdateQrcodeButton, &QPushButton::clicked, this, &WindowLogin::StartQRCodeLogin);

    connect(QrcodeTimer, &QTimer::timeout, this, [this]() {
        QThreadPool::globalInstance()->start([this]() {
            CheckQRCodeLoginState();
        });
    });

    connect(this, &WindowLogin::StartQRCodeTimer, this, [this]() {
        if (!QrcodeTimer->isActive())
        {
            QrcodeTimer->start(1000);
        }
    });

    connect(Tab0pbtCancel, &QPushButton::clicked, this, [this]() {
        phoneNumberLineEdit->clear();
        verifyCodeLineEdit->clear();
    });

    connect(pBtofficialreject, &QPushButton::clicked, this, [this]() {
        lineEditCookie->clear();
    });

}


void WindowLogin::ResultByLoginOfficial(const auto& result)
{
}

void WindowLogin::StartQRCodeLogin()
{
    QrcodeTimer->stop();
    UpdateQrcodeButton->hide();
    UpdateQrcodeButton->move(QRCodelabel->x() + (QRCodelabel->width() - 170) / 2, QRCodelabel->y() + (QRCodelabel->height() - 170) / 2);
    AllowDrawQRCode.store(true);
    QrcodePool.start([this]() {
        QrcodePool.clear();
        QRCodeQImage.fill(255);
        QRCodelabel->setText("二维码加载中");
        AllowDrawQRCode.store(false);
        auto [code, qrcodeString, qrcodeTicket] = GetLoginQrcodeUrl();
        ticket = qrcodeTicket;
        if (code != 0 || ticket.empty() || qrcodeString.empty())
        {
            emit QrcodeLoginResult(false);
            return;
        }
        QrcodeMat = createQrCodeToCvMat(qrcodeString);
        QRCodeQImage = CV_8UC1_MatToQImage(QrcodeMat);
        if (AllowDrawQRCode.load())
        {
            return;
        }
        QRCodelabel->setPixmap(QPixmap::fromImage(QRCodeQImage));
        emit StartQRCodeTimer();
    });
}

void WindowLogin::CheckQRCodeLoginState()
{
    auto [state, stoken, mid, uid] = GetQRCodeState(ticket);
    switch (state)
    {
    case LoginQRCodeState::Init:
    {
    }
    break;
    case LoginQRCodeState::Scanned:
    {
        QRCodelabel->setText("正在登录\n\n请在手机上点击「确认登录」");
    }
    break;
    case LoginQRCodeState::Confirmed:
    {
        std::string name{ getMysUserName(uid) };
        emit AddUserInfo(name, stoken, uid, mid, "官服");
        QRCodelabel->setText("登录成功！");
        emit QrcodeLoginResult(true);
        return;
    }
    break;
    case LoginQRCodeState::Expired:
    {
        emit QrcodeLoginResult(false);
        return;
    }
    break;
    default:
        __assume(0);
    }
}
