#include "WindowMain.h"
#include "WindowLoginKuro.h"
#include "KuroCaptchaDialog.hpp"
#include "KuroScanSmsDialog.hpp"
#include "AccountSecrets.hpp"
#include "AccountPlatform.hpp"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QMenu>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QToolButton>
#include <atomic>
#include <future>
#include <mutex>
#include <iostream>
#include <QtConcurrent/QtConcurrentRun>

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void events(int milliseconds = 100)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (elapsed.elapsed() < milliseconds) { QApplication::processEvents(); QThread::msleep(5); }
}

template<class Predicate> void until(Predicate predicate, const char* message)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < 10000) events(20);
    require(predicate(), message);
}

const QString proofMessage = QStringLiteral(R"({"type":"verified","data":{"lot_number":"offline-lot","captcha_output":"offline-output","pass_token":"offline-proof","gen_time":"123456"}})");

// Explicit opt-in diagnostic: one human-completed challenge and one SMS request; never part of CTest.
int liveScanSmsDiagnostic(QApplication& app, const QString& configPath)
{
    QFile file(configPath);
    require(file.open(QIODevice::ReadOnly), "diagnostic configuration unavailable");
    auto accounts = nlohmann::json::parse(file.readAll().toStdString());
    account_secrets::transformAccounts(accounts, false);
    std::string token;
    for (const auto& account : accounts["account"]) {
        if (accountPlatform(account) != AccountPlatform::Kuro) continue;
        require(token.empty(), "diagnostic requires exactly one Kuro account");
        token = account.value("access_key", std::string{});
    }
    require(!token.empty(), "diagnostic account unavailable");
    bool finished = false;
    bool started = false;
    nlohmann::json report;
    QPointer<KuroCaptchaDialog> captcha = new KuroCaptchaDialog([&](QString proof) {
        if (started) return;
        started = true;
        auto* watcher = new QFutureWatcher<nlohmann::json>(&app);
        QObject::connect(watcher, &QFutureWatcher<nlohmann::json>::finished, &app, [&, watcher] {
            report = watcher->result();
            watcher->deleteLater();
            finished = true;
        });
        watcher->setFuture(QtConcurrent::run([token, proof = proof.toStdString()] {
            nlohmann::json report = {{"captchaId", std::string(KuroCaptchaProof::captchaId)}, {"deviceCodePresent", false}};
            KuroApi api(token, [&](const KuroRequest& request) {
                require(request.path == "/user/sms/scanSms", "diagnostic refused unrelated endpoint");
                cpr::Payload form{};
                for (const auto& [key, value] : request.form) form.Add({key, value});
                const auto response = cpr::Post(cpr::Url{"https://api.kurobbs.com" + request.path},
                    cpr::Header(request.headers.begin(), request.headers.end()), form,
                    cpr::Proxies{{"http", ""}, {"https", ""}}, cpr::Timeout{10000}, cpr::ConnectTimeout{4000}, cpr::Redirect{false});
                const auto data = nlohmann::json::parse(response.text, nullptr, false);
                if (data.is_object() && data.contains("msg") && data["msg"].is_string()) {
                    auto text = QString::fromStdString(data["msg"].get<std::string>());
                    text.replace(QString::fromStdString(token), QStringLiteral("[redacted]"));
                    text.replace(QRegularExpression(QStringLiteral("[A-Za-z0-9_+/=\\-]+")), QStringLiteral("[redacted]"));
                    report["serverMessageRedacted"] = text.left(160).toStdString();
                }
                return KuroHttpResponse{response.status_code, response.text, response.error.code != cpr::ErrorCode::OK,
                    response.error.code == cpr::ErrorCode::OPERATION_TIMEDOUT};
            });
            const auto result = api.sendScanSms(proof);
            report["http"] = result.httpStatus;
            report["code"] = result.code;
            report["error"] = static_cast<int>(result.error);
            return report;
        }));
    });
    captcha->setWindowTitle(QStringLiteral("扫码短信诊断 · 请手动完成人机验证（仅请求一次）"));
    captcha->show();
    QElapsedTimer elapsed;
    elapsed.start();
    while (!finished && (captcha || started) && elapsed.elapsed() < 140000) events(30);
    if (captcha) captcha->reject();
    require(finished, "diagnostic cancelled or timed out; no completed result");
    std::cout << report.dump() << '\n';
    return 0;
}

KuroCaptchaDialog* startCaptcha(WindowLoginKuro* login)
{
    login->findChild<QLineEdit*>("kuroPhone")->setText("13800000000");
    login->findChild<QPushButton*>("kuroSendSms")->click();
    auto* captcha = dynamic_cast<KuroCaptchaDialog*>(login->findChild<QDialog*>("kuroCaptchaDialog"));
    require(captcha, "SMS did not open captcha");
    until([&] { return captcha->bridgeReady(); }, "captcha WebView initialization failed");
    return captcha;
}

void smsFlow(QWidget* parent)
{
    std::atomic_int requests{0};
    std::mutex deviceMutex;
    std::string device;
    bool added = false;
    auto* login = new WindowLoginKuro(parent, [&](const KuroRequest& request) {
        ++requests;
        {
            std::lock_guard lock(deviceMutex);
            if (device.empty()) device = request.headers.at("devCode");
            require(!device.empty() && request.headers.at("devCode") == device, "device changed within SMS login");
        }
        if (request.path == "/user/getSmsCode") return KuroHttpResponse{200, R"({"success":true})"};
        if (request.path == "/user/sdkLogin") return KuroHttpResponse{200, R"({"code":200,"data":{"token":"offline-token","userId":"12345"}})"};
        if (request.path == "/user/getPrivateSet") return KuroHttpResponse{200, R"({"code":200,"data":{}})"};
        return KuroHttpResponse{200, R"({"code":200,"data":{"mine":{"userId":"12345","userName":"offline-user"}}})"};
    });
    QObject::connect(login, &WindowLoginKuro::AddUserInfo, parent,
        [&](const std::string& name, const std::string& token, const std::string& uid, const std::string&, const std::string&) {
            require(name == "offline-user" && token == "offline-token" && uid == "12345", "SMS login emitted incorrect account");
            added = true;
        });
    login->show();
    auto* captcha = startCaptcha(login);
    captcha->handleWebMessage("https://untrusted.invalid", proofMessage);
    events();
    require(requests == 0, "foreign origin sent an SMS");
    captcha->handleWebMessage(KuroCaptchaDialog::documentUrl(), proofMessage);
    captcha->handleWebMessage(KuroCaptchaDialog::documentUrl(), proofMessage);
    until([&] { return login->findChild<QPushButton*>("kuroSmsLogin")->isEnabled(); }, "SMS completion did not unlock UI");
    require(requests == 1, "captcha submitted SMS more than once");
    require(!login->findChild<QPushButton*>("kuroSendSms")->isEnabled(), "SMS cooldown absent");
    login->findChild<QLineEdit*>("kuroSmsCode")->setText("123456");
    login->findChild<QPushButton*>("kuroSmsLogin")->click();
    until([&] { return added; }, "SMS login did not validate and emit account");
    require(requests == 4, "SMS login request sequence incomplete");
    events();

    // Closing during environment initialization must never send an SMS.
    auto* cancelled = new WindowLoginKuro(parent, [&](const KuroRequest&) {
        ++requests;
        return KuroHttpResponse{200, R"({"success":true})"};
    });
    cancelled->show();
    cancelled->findChild<QLineEdit*>("kuroPhone")->setText("13800000000");
    cancelled->findChild<QPushButton*>("kuroSendSms")->click();
    cancelled->close();
    events(300);
    require(requests == 4, "cancelled captcha sent SMS");

    std::promise<void> release;
    const auto gate = release.get_future().share();
    std::atomic_bool started{false};
    bool lateAccount = false;
    auto* pending = new WindowLoginKuro(parent, [&](const KuroRequest&) {
        started = true;
        gate.wait_for(std::chrono::seconds(5));
        return KuroHttpResponse{200, R"({"success":true})"};
    });
    QObject::connect(pending, &WindowLoginKuro::AddUserInfo, parent,
        [&](const std::string&, const std::string&, const std::string&, const std::string&, const std::string&) { lateAccount = true; });
    pending->show();
    startCaptcha(pending)->handleWebMessage(KuroCaptchaDialog::documentUrl(), proofMessage);
    until([&] { return started.load(); }, "pending SMS request did not start");
    QPointer<WindowLoginKuro> pendingGuard(pending);
    pending->close();
    events();
    const bool closed = !pendingGuard;
    release.set_value();
    require(closed, "closing waits for network worker");
    require(QThreadPool::globalInstance()->waitForDone(10000), "SMS worker did not finish");
    events();
    require(!lateAccount, "closed window emitted an account");
}

void scanSmsFlow(QWidget* parent, const QString& output)
{
    using Clock = std::chrono::steady_clock;
    auto time = Clock::time_point{};
    const std::string qr = "G152#KURO#offline-ui-scan-session";
    int requests = 0, smsRequests = 0;
    int sendCode = 242;
    bool completed = false;
    KuroScanSession session([&](const KuroRequest& request) {
        ++requests;
        require(request.headers.at("token") == "offline-scan-account", "scan SMS changed account");
        if (request.path == "/user/auth/roleInfos") return KuroHttpResponse{200, R"({"code":200})"};
        if (request.path == "/user/sms/scanSms") {
            ++smsRequests;
            require(request.form.empty() && request.rawBody == "geeTestData=", "UI scan SMS wire body differs from upstream");
            return KuroHttpResponse{200, nlohmann::json{{"code", sendCode}}.dump()};
        }
        require(request.form.at("qrCode") == qr, "SMS submitted a different QR");
        return KuroHttpResponse{200, request.form.at("verifyCode").empty() ? R"({"code":2240})" : R"({"code":200})"};
    }, [&] { return time; });
    session.setAccount("offline-scan-account", 71);
    require(session.prepare(qr, 71) == ScanRet::SUCCESS && session.confirm({}, 71) == ScanRet::KURO_SMS_REQUIRED, "scan fixture did not request SMS");
    auto* dialog = new KuroScanSmsDialog(
        [&](std::string proof, auto done) { done(session.sendSms(proof, 71)); },
        [&](std::string sms, auto done) { done(session.confirm(sms, 71)); },
        [&](ScanRet result) { completed = result == ScanRet::SUCCESS; }, parent, [&] { return time; });
    dialog->show();
    auto begin = [](KuroScanSmsDialog* owner) {
        owner->findChild<QPushButton*>("kuroScanSendSms")->click();
        KuroCaptchaDialog* captcha = nullptr;
        for (auto* child : owner->findChildren<QDialog*>("kuroCaptchaDialog"))
            if (child->isVisible()) captcha = dynamic_cast<KuroCaptchaDialog*>(child);
        require(captcha, "scan send did not open human verification");
        until([&] { return captcha->bridgeReady(); }, "scan captcha WebView did not initialize");
        return captcha;
    };
    begin(dialog)->reject();
    events();
    require(smsRequests == 0 && dialog->findChild<QPushButton*>("kuroScanSendSms")->isEnabled(), "cancelled scan captcha sent SMS or started cooldown");
    auto* captcha = begin(dialog);
    captcha->handleWebMessage("https://untrusted.invalid", proofMessage);
    require(smsRequests == 0, "foreign origin dispatched scan SMS");
    captcha->handleWebMessage(KuroCaptchaDialog::documentUrl(), proofMessage);
    captcha->handleWebMessage(KuroCaptchaDialog::documentUrl(), proofMessage);
    require(smsRequests == 1 && requests == 3, "verified scan SMS not sent exactly once");
    auto* status = dialog->findChild<QLabel*>("kuroScanSmsStatus");
    require(status->text().contains(QStringLiteral("限流")) && status->text().contains("242") &&
        !status->text().contains(QStringLiteral("不足")), "242 not described as server-side throttling");
    require(!dialog->findChild<QPushButton*>("kuroScanSendSms")->isEnabled(), "server throttling still offered a retry");
    require(dialog->grab().save(output + "/scan-sms-refused-v" MHY_Scanner_VERSION ".png"), "scan SMS failure screenshot unavailable");
    dialog->close();
    events();
    // 242 is a server-side throttle that outlives the local cooldown, so the
    // acceptance path is exercised from a fresh scan session rather than a retry.
    time += std::chrono::seconds(60);
    events(300);
    sendCode = 200;
    auto* accepted = new KuroScanSmsDialog(
        [&](std::string proof, auto done) { done(session.sendSms(proof, 71)); },
        [&](std::string sms, auto done) { done(session.confirm(sms, 71)); },
        [&](ScanRet result) { completed = result == ScanRet::SUCCESS; }, parent, [&] { return time; });
    accepted->show();
    auto* acceptedStatus = accepted->findChild<QLabel*>("kuroScanSmsStatus");
    begin(accepted)->handleWebMessage(KuroCaptchaDialog::documentUrl(), proofMessage);
    require(smsRequests == 2 && acceptedStatus->text().contains(QStringLiteral("服务器已接受")), "verified scan SMS acceptance absent");
    require(accepted->grab().save(output + "/scan-sms-accepted-v" MHY_Scanner_VERSION ".png"), "scan SMS success screenshot unavailable");
    accepted->findChild<QLineEdit*>("kuroScanSmsCode")->setText("123456");
    accepted->findChild<QPushButton*>("kuroScanSubmit")->click();
    require(completed && requests == 5, "scan SMS did not resume the original QR");
    events();
    accepted->close();
    events();

    std::function<void(KuroResponse)> pending;
    auto* delayed = new KuroScanSmsDialog([&](std::string, auto done) { pending = std::move(done); },
        [](std::string, auto) {}, [](ScanRet) {}, parent, [&] { return time; });
    delayed->show();
    begin(delayed)->handleWebMessage(KuroCaptchaDialog::documentUrl(), proofMessage);
    require(static_cast<bool>(pending), "delayed scan send not dispatched");
    KuroResponse timeout;
    timeout.error = KuroError::Timeout;
    pending(timeout);
    require(delayed->findChild<QLabel*>("kuroScanSmsStatus")->text().contains(QStringLiteral("结果未知")) &&
        !delayed->findChild<QPushButton*>("kuroScanSendSms")->isEnabled(), "scan timeout was retried or misreported");
    delayed->close();
    events();
    pending(KuroResponse{});

    int expiredRequests = 0;
    auto* expired = new KuroScanSmsDialog([&](std::string, auto) { ++expiredRequests; },
        [](std::string, auto) {}, [](ScanRet) {}, parent, [&] { return time; });
    expired->show();
    QPointer<KuroCaptchaDialog> expiredCaptcha = begin(expired);
    time += std::chrono::seconds(91);
    events(300);
    if (expiredCaptcha) expiredCaptcha->handleWebMessage(KuroCaptchaDialog::documentUrl(), proofMessage);
    require(expiredRequests == 0 && !expired->findChild<QPushButton*>("kuroScanSendSms")->isEnabled(), "expired captcha sent a scan SMS");
    expired->close();
    events();
}

void accountManagement(const QString& output)
{
    auto& config = ConfigDate::getInstance();
    auto data = nlohmann::json::parse(config.defaultConfig());
    data["account"] = nlohmann::json::array();
    for (int i = 0; i < 4; ++i) {
        data["account"].push_back({{"account_id", "offline-id-" + std::to_string(i)},
            {"uid", std::to_string(10001 + i)}, {"access_key", ""}, {"mid", ""},
            {"name", "offline-name-" + std::to_string(i)}, {"note", "keep-note"},
            {"provider", i % 2 ? "kuro" : "mhy"}, {"type", i % 2 ? "库街区官服" : "官服"}});
    }
    data["num"] = 4;
    data["last_account"] = 2;
    config.updateConfig(data.dump());
    WindowMain window;
    window.show();
    auto* table = window.findChild<QTableWidget*>("tableWidget");
    until([&] { return table && table->rowCount() == 4 && table->item(3, 0); }, "account fixtures did not load");
    events();
    auto switchPlatform = [&](const QString& text) {
        for (auto* button : window.findChildren<QToolButton*>())
            if (button->text() == text) { button->click(); events(); return; }
        throw std::runtime_error("platform button missing");
    };
    auto checkNumber = [&](int row, int number) {
        if (table->isRowHidden(row) || table->item(row, 0)->text() != QString::number(number))
            throw std::runtime_error("platform numbering row=" + std::to_string(row) + " expected=" + std::to_string(number) +
                " actual=" + table->item(row, 0)->text().toStdString() + " hidden=" + std::to_string(table->isRowHidden(row)));
        require(!(table->item(row, 0)->flags() & Qt::ItemIsEditable), "serial number became editable");
    };
    checkNumber(1, 1);
    checkNumber(3, 2);
    switchPlatform(QStringLiteral("米哈游"));
    checkNumber(0, 1);
    checkNumber(2, 2);
    switchPlatform(QStringLiteral("库街区"));

    QTimer messages;
    QObject::connect(&messages, &QTimer::timeout, &window, [&] {
        for (auto* message : window.findChildren<QMessageBox*>())
            if (message->isVisible()) {
                if (auto* yes = message->button(QMessageBox::Yes)) yes->click();
                else message->accept();
            }
    });
    messages.start(10);
    auto relogin = [&](int row) {
        bool found = false;
        QTimer::singleShot(0, &window, [&] {
            auto* menu = window.findChild<QMenu*>("accountContextMenu");
            auto* action = menu ? menu->findChild<QAction*>("reauthenticateAccount") : nullptr;
            found = action && action->isEnabled();
            if (menu) menu->close();
            if (found) action->trigger();
        });
        table->customContextMenuRequested(table->visualItemRect(table->item(row, 2)).center());
        events();
        require(found, "right-click relogin absent or disabled");
    };
    const auto original = nlohmann::json::parse(config.getConfig());
    relogin(3);
    auto* login = window.findChild<WindowLoginKuro*>();
    require(login && login->isVisible(), "relogin opened wrong provider");
    require(login->findChild<QPushButton*>("kuroSmsLogin")->text() == QStringLiteral("登录并更新账号"), "relogin UI still adds accounts");
    require(login->grab().save(output + "/app-relogin-v" MHY_Scanner_VERSION ".png"), "relogin screenshot failed");
    login->AddUserInfo("wrong-account", "offline-new-token", "99999", "", "库街区官服");
    require(nlohmann::json::parse(config.getConfig()) == original, "different UID replaced saved account");
    login->close();
    login->AddUserInfo("late-account", "offline-new-token", "10004", "", "库街区官服");
    require(nlohmann::json::parse(config.getConfig()) == original, "cancelled login updated account");
    events();
    relogin(3);
    login = window.findChild<WindowLoginKuro*>();
    login->AddUserInfo("updated-name", "offline-new-token", "10004", "", "库街区官服");
    auto updated = nlohmann::json::parse(config.getConfig());
    auto expected = original;
    expected["account"][3]["access_key"] = "offline-new-token";
    expected["account"][3]["name"] = "updated-name";
    require(updated == expected, "relogin added a duplicate or changed unrelated configuration");
    require(table->item(3, 4)->text() == QStringLiteral("有效"), "relogin status not refreshed");
    login->close();
    events();
    require(window.grab().save(output + "/app-accounts-v" MHY_Scanner_VERSION ".png"), "accounts screenshot failed");

    // A lower physical row can be deleted while login is pending; match the stable account identity.
    relogin(3);
    login = window.findChild<WindowLoginKuro*>();
    table->setCurrentCell(1, 0);
    window.getInfo(1, 0);
    window.DeleteAccount();
    checkNumber(2, 1);
    login->AddUserInfo("updated-again", "offline-next-token", "10004", "", "库街区官服");
    updated = nlohmann::json::parse(config.getConfig());
    require(updated["num"] == 3 && updated["account"][2]["account_id"] == "offline-id-3" &&
        updated["account"][2]["access_key"] == "offline-next-token", "row shift updated wrong account");
    login->close();
    events();
    relogin(2);
    login = window.findChild<WindowLoginKuro*>();
    table->setCurrentCell(2, 0);
    window.getInfo(2, 0);
    window.DeleteAccount();
    const auto deleted = config.getConfig();
    login->AddUserInfo("removed-account", "offline-late-token", "10004", "", "库街区官服");
    require(config.getConfig() == deleted, "relogin resurrected deleted account");
    login->close();
    events();
    switchPlatform(QStringLiteral("米哈游"));
    checkNumber(0, 1);
    checkNumber(1, 2);
    relogin(0);
    auto* mhyLogin = window.findChild<WindowLogin*>();
    require(mhyLogin && mhyLogin->isVisible(), "Mhy relogin opened wrong provider");
    mhyLogin->AddUserInfo("mhy-refreshed", "offline-mhy-token", "10001", "offline-mid", "官服");
    updated = nlohmann::json::parse(config.getConfig());
    require(updated["num"] == 2 && updated["account"][0]["access_key"] == "offline-mhy-token" &&
        updated["account"][0]["mid"] == "offline-mid", "Mhy relogin did not update existing account");
    mhyLogin->close();
    window.close();
    events();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    const auto output = QDir::currentPath();
    QTemporaryDir config;
    try
    {
        if (argc > 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--scan-sms-live-diagnostic"))
            return liveScanSmsDiagnostic(app, QString::fromLocal8Bit(argv[2]));
        require(config.isValid() && QDir::setCurrent(config.path()), "isolated config unavailable");
        if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--captcha-live-check")) {
            bool verified = false;
            int finished = -1;
            QPointer<KuroCaptchaDialog> captcha = new KuroCaptchaDialog([&](QString) { verified = true; });
            QObject::connect(captcha, &QDialog::finished, &app, [&](int result) { finished = result; });
            captcha->show();
            QElapsedTimer elapsed;
            elapsed.start();
            while (!verified && captcha && !captcha->challengeReady() && elapsed.elapsed() < 40000) events(50);
            if (!verified && (!captcha || !captcha->challengeReady())) {
                std::cerr << "verification finished=" << finished << " elapsed_ms=" << elapsed.elapsed() << '\n';
                if (captcha) {
                    captcha->diagnosePage([](QString result) { std::cerr << result.toStdString() << '\n'; });
                    events(1000);
                    QFile log(QStringLiteral("Config/scanner.log"));
                    if (log.open(QIODevice::ReadOnly)) {
                        for (const auto& line : QString::fromUtf8(log.readAll()).split('\n'))
                            if (line.contains(QStringLiteral("kuro captcha:"))) std::cerr << line.toStdString() << '\n';
                    }
                    captcha->grab().save(output + "/kuro-captcha-load-failure.png");
                    for (const auto* label : captcha->findChildren<QLabel*>())
                        std::cerr << label->text().toStdString() << '\n';
                    captcha->close();
                }
                throw std::runtime_error("live GeeTest challenge failed to load");
            }
            events(500);
            if (captcha) {
                require(captcha->grab().save(output + "/kuro-captcha-v" MHY_Scanner_VERSION ".png"), "captcha screenshot failed");
                captcha->close();
            }
            events(300);
            std::cout << "live WebView2 GeeTest challenge loaded or completed; no phone/SMS submitted\n";
            return 0;
        }
        WindowMain window;
        window.show();
        events(300);
        require(window.windowTitle() == QStringLiteral("ErYouScanner"), "old product title");
        QToolButton* kuro = nullptr;
        for (auto* button : window.findChildren<QToolButton*>())
            if (button->text() == QStringLiteral("库街区")) kuro = button;
        require(kuro, "Kuro switch absent");
        kuro->click();
        events();
        require(window.grab().save(output + "/app-main-v" MHY_Scanner_VERSION ".png"), "main screenshot failed");
        window.AddAccount();
        events();
        auto* login = window.findChild<WindowLoginKuro*>();
        require(login && login->isVisible(), "Kuro account window absent");
        require(!login->findChild<QTabWidget*>(), "other Kuro login tabs remain");
        require(!login->findChild<QTextEdit*>(), "Token input remains");
        require(login->grab().save(output + "/app-kuro-login-v" MHY_Scanner_VERSION ".png"), "login screenshot failed");
        for (auto* button : login->findChildren<QPushButton*>())
            require(!button->text().contains(QStringLiteral("官网")), "official web login remains");
        login->findChild<QPushButton*>("kuroSendSms")->click();
        login->findChild<QPushButton*>("kuroSmsLogin")->click();
        events();
        require(!login->findChild<QDialog*>("kuroCaptchaDialog"), "invalid phone opened captcha");
        require(nlohmann::json::parse(ConfigDate::getInstance().getConfig())["account"].empty(), "invalid phone was saved");
        login->close();
        events();
        window.pBtStop();
        auto* scanner = window.findChild<QRCodeForScreen*>();
        require(scanner, "screen scanner absent");
        scanner->loginResults(ScanRet::FAILURE_1, 0);
        scanner->loginConfirm(GameType::WutheringWaves, true, 0);
        events();
        require(window.findChildren<QMessageBox*>().empty(), "stale scanner generation affected UI");
        smsFlow(&window);
        scanSmsFlow(&window, output);
        window.close();
        events();
        accountManagement(output);
        std::cout << "isolated SMS UI, platform numbering, relogin replacement, identity mismatch, cancellation, row shifts and deletion passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
