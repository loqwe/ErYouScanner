#include <QApplication>
#include <QTimer>
#include <iostream>
#include <nlohmann/json.hpp>
#include "WindowGeeTest.h"
#include "KuroCaptchaProof.hpp"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    WindowGeeTest window;
    bool verified = false;
    QObject::connect(&window, &WindowGeeTest::postMessage, [&verified](const QString&) { verified = true; });
    GeetestData data{};
    data.use_v4 = true;
    data.gt = KuroCaptchaProof::captchaId;
    window.Init(data);
    QObject::connect(&window, &WindowGeeTest::failed, [&app](const QString&) { std::cout << "page failure" << std::endl; app.exit(1); });
    window.show();
    int pass = 0;
    QTimer check;
    check.setInterval(5000);
    QObject::connect(&check, &QTimer::timeout, &window, [&] {
        window.diagnosePage([&](QString result) {
            std::cout << result.toStdString() << std::endl;
            try {
                const auto json = nlohmann::json::parse(nlohmann::json::parse(result.toStdString()).get<std::string>());
                if (json.at("storage") != "available" || !json.at("challengeReady").get<bool>() ||
                    (json.at("visibleCaptchaNodes").get<int>() < 1 && !verified) || !json.at("errors").empty() ||
                    json.at("viewportWidth").get<int>() < 390) { app.exit(1); return; }
                if (++pass == 2) { std::cout << "PASS: real SDK ready, storage, DPI bounds, close/reopen" << std::endl; app.quit(); }
                else { window.close(); verified = false; window.Init(data); window.show(); }
            } catch (...) { app.exit(1); }
        });
    });
    check.start();
    QTimer::singleShot(40000, &app, [&app] { app.exit(1); });
    return app.exec();
}
