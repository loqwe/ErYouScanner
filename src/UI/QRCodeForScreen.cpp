#include "QRCodeForScreen.h"

#include <chrono>
#include <limits>
#include <thread>
#include <vector>

#include <QThreadPool>

#include "QRScanner.h"
#include "ScannerTask.hpp"
#include "ScreenShotDXGI.hpp"

namespace
{
constexpr auto CaptureDelay = std::chrono::milliseconds(200);

struct WaitForScreenWorkers
{
    QThreadPool& pool;
    std::atomic<bool>& running;
    ~WaitForScreenWorkers()
    {
        running.store(false);
        pool.waitForDone();
    }
};
}

QRCodeForScreen::QRCodeForScreen(QObject* parent) : QThread(parent), m_stop(false)
{
    m_config = &ConfigDate::getInstance();
}

QRCodeForScreen::~QRCodeForScreen()
{
    stop();
    requestInterruption();
    wait();
}

void QRCodeForScreen::setLoginInfo1(const std::string& uid, const std::string& stoken, const std::string& mid)
{
    this->uid = uid;
    this->gameToken = stoken;
    this->mid = mid;
}

void QRCodeForScreen::LoginOfficial()
{
    monitorScreen();
}

void QRCodeForScreen::monitorScreen()
{
    // One worker owns decoder and login state; it is drained before either is destroyed.
    QRScanner scanner;
    ScreenShotDXGI capture;
    int width = 0;
    int height = 0;
    if (!capture.InitDevice() || !capture.InitDupl(0, width, height))
    {
        WriteScannerLog("screen: initialization failed hr=" + std::to_string(capture.lastError()));
        stop();
        Q_EMIT loginResults(ScanRet::STREAMERROR);
        return;
    }
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (width <= 0 || height <= 0 || static_cast<std::size_t>(width) > maximum / 4 ||
        static_cast<std::size_t>(height) > maximum / (static_cast<std::size_t>(width) * 4))
    {
        throw std::runtime_error("Invalid desktop frame size");
    }
    std::vector<unsigned char> buffer(static_cast<std::size_t>(width) * height * 4);
    DXGI_FORMAT lastFormat = DXGI_FORMAT_UNKNOWN;
    cv::Mat lastGoodFrame;
    QThreadPool pool;
    pool.setMaxThreadCount(threadNumber);
    WaitForScreenWorkers waitForWorkers{pool, m_stop};
    bool captureFailed = false;
    while (m_stop.load() && !isInterruptionRequested())
    {
        const int result = capture.getFrame(100);
        if (result == 1)
        {
            WriteScannerLog("screen: acquisition failed hr=" + std::to_string(capture.lastError()));
            captureFailed = true;
            break;
        }
        cv::Mat image;
        if (result == 0)
        {
            const auto desc = capture.frameDescription();
            if (!desc.Width || !desc.Height || desc.Width > INT_MAX || desc.Height > INT_MAX ||
                desc.Width > maximum / 4 || desc.Height > maximum / (static_cast<std::size_t>(desc.Width) * 4))
            {
                WriteScannerLog("screen: invalid acquired frame dimensions");
                capture.doneWithFrame();
                captureFailed = true;
                break;
            }
            if (lastFormat != desc.Format || width != static_cast<int>(desc.Width) || height != static_cast<int>(desc.Height))
            {
                WriteScannerLog("screen: frame width=" + std::to_string(desc.Width) +
                    " height=" + std::to_string(desc.Height) + " format=" + std::to_string(desc.Format));
                lastFormat = desc.Format;
            }
            width = static_cast<int>(desc.Width);
            height = static_cast<int>(desc.Height);
            buffer.resize(static_cast<std::size_t>(width) * height * 4);
            if (!capture.copyFrameToBuffer(buffer.data(), buffer.size()))
            {
                WriteScannerLog("screen: frame copy failed hr=" + std::to_string(capture.lastError()) +
                    " stage=" + capture.lastCopyStage() + " format=" + std::to_string(desc.Format) +
                    " width=" + std::to_string(width) + " height=" + std::to_string(height) +
                    " rowPitch=" + std::to_string(capture.lastRowPitch()) + " capacity=" + std::to_string(buffer.size()));
                capture.doneWithFrame();
                captureFailed = true;
                break;
            }
            // Release the DXGI frame before resize/decode, including exception paths.
            if (!capture.doneWithFrame())
            {
                WriteScannerLog("screen: frame release failed hr=" + std::to_string(capture.lastError()));
                captureFailed = true;
                break;
            }
            cv::resize(cv::Mat(height, width, CV_8UC4, buffer.data()), image, {1280, 720});
            lastGoodFrame = image;
        }
        else if (!lastGoodFrame.empty())
        {
            image = lastGoodFrame;
        }
        if (!image.empty())
        {
#ifndef SHOW
            cv::imshow("Video_Stream", image);
            cv::waitKey(1);
#endif
            pool.tryStart([this, &scanner, image = std::move(image)]() {
                try
                {
                    if (!m_stop.load() || isInterruptionRequested())
                    {
                        return;
                    }
                    std::string decoded;
                    scanner.decodeSingle(image, decoded);
                    std::string ticket;
                    if (!m_stop.load() || isInterruptionRequested() ||
                        !parseOfficialQRCode(decoded, ticket) ||
                        lastTicket == ticket)
                    {
                        return;
                    }
                    const std::string passportQrUrl = PandaScanQRCode(scanUrl, ticket, gameType);
                    const bool accepted = !passportQrUrl.empty();
                    if (accepted)
                    {
                        lastQrCode = passportQrUrl;
                    }
                    if (!m_stop.load() || isInterruptionRequested())
                    {
                        return;
                    }
                    if (accepted)
                    {
                        lastTicket = ticket;
                        // Stop capture before publishing a confirmation to the UI.
                        stop();
                        Q_EMIT loginConfirm(gameType, true);
                    }
                    else
                    {
                        stop();
                        Q_EMIT loginResults(ScanRet::FAILURE_1);
                    }
                }
                catch (...)
                {
                    LogScannerException("screen: QR worker");
                    stop();
                    Q_EMIT loginResults(ScanRet::STREAMERROR);
                }
            });
        }
        std::this_thread::sleep_for(CaptureDelay);
    }
    const bool reportCaptureFailure = captureFailed && m_stop.exchange(false);
    pool.waitForDone();
    if (reportCaptureFailure)
    {
        Q_EMIT loginResults(ScanRet::STREAMERROR);
    }
}

void QRCodeForScreen::continueLastLogin()
{
    const bool loggedIn = ScanPassportQRLogin(lastQrCode, gameToken, mid) &&
                          ConfirmPassportQRLogin(lastQrCode, gameToken, mid);
    Q_EMIT loginResults(loggedIn ? ScanRet::SUCCESS : ScanRet::FAILURE_2);
}

void QRCodeForScreen::run()
{
    ret = ScanRet::UNKNOW;
    m_stop.store(true);
    try
    {
#ifndef SHOW
        cv::namedWindow("Video_Stream", cv::WINDOW_AUTOSIZE);
#endif
        LoginOfficial();
    }
    catch (...)
    {
        LogScannerException("screen: model or capture initialization");
        Q_EMIT loginResults(ScanRet::STREAMERROR);
    }
    m_stop.store(false);
#ifndef SHOW
    try
    {
        cv::destroyWindow("Video_Stream");
    }
    catch (...)
    {
        LogScannerException("screen: preview cleanup");
    }
#endif
}

void QRCodeForScreen::stop()
{
    requestInterruption();
    m_stop.store(false);
}
