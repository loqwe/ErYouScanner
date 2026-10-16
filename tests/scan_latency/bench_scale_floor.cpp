// Robustness companion to bench_scan_latency.cpp.
//
// bench_scan_latency showed that QRScanner::detector->setScaleFactor(1.0) forces
// the detector network to eat the whole 1280x720 frame (921600 px), while the
// library's own default targets 160000 px. Lowering it cut the decode of a real
// game QR from ~88 ms to ~7 ms with an identical payload.
//
// This program answers the follow-up question: how far can the scale factor be
// lowered before detection starts failing?
//
// Note on method: the scan frame is always normalised to 1280x720, so shrinking
// a screenshot and re-normalising does NOT make the code smaller. What matters
// is the QR's size *inside the 1280x720 frame*, so that is what is swept here.
// The frame-side reference is the real Wuthering Waves login code, which
// measures 149 px inside the scan frame.
#include "QRModelPaths.hpp"
#include "qrcodegen.hpp"

#include <opencv2/wechat_qrcode.hpp>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

cv::Mat Prepare(const cv::Mat& frame)
{
    cv::Mat resized;
    cv::resize(frame, resized, { 1280, 720 });
    return resized;
}

// A cluttered 1920x1080 desktop carrying one QR of roughly `framePixels` in the
// 1280x720 scan frame.
cv::Mat Synthetic(std::string_view payload, int framePixels)
{
    constexpr int width = 1920;
    constexpr int height = 1080;
    cv::Mat frame(height, width, CV_8UC3, cv::Scalar(30, 34, 40));
    cv::RNG rng(20261016);
    for (int i = 0; i < 4000; ++i)
    {
        const auto colour = rng.uniform(0, 255);
        cv::rectangle(frame, { rng.uniform(0, width), rng.uniform(0, height) },
            { rng.uniform(0, width), rng.uniform(0, height) },
            cv::Scalar(colour, colour, colour), cv::FILLED);
    }
    const auto code = qrcodegen::QrCode::encodeText(std::string(payload).c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int modules = code.getSize() + 8;
    const int desktopPixels = static_cast<int>(framePixels * width / 1280.0);
    const int scale = (std::max)(1, desktopPixels / modules);
    const int size = modules * scale;
    cv::Mat qr(size, size, CV_8UC3, cv::Scalar(255, 255, 255));
    for (int y = 0; y < code.getSize(); ++y)
        for (int x = 0; x < code.getSize(); ++x)
            if (code.getModule(x, y))
                qr(cv::Rect((x + 4) * scale, (y + 4) * scale, scale, scale)).setTo(cv::Scalar(0, 0, 0));
    qr.copyTo(frame(cv::Rect((width - size) / 2, (height - size) / 2, size, size)));
    return frame;
}
}

int main(int argc, char** argv)
{
    try
    {
        const int iterations = argc > 1 ? std::stoi(argv[1]) : 10;
        const auto modelPaths = QRModelPaths(QRModelDirectory());
        const std::string payload = "G152#KURO_038a5d17-f3f6-4474-ba86-dba3aac105d4";

        auto make = [&modelPaths](float factor)
        {
            auto detector = cv::makePtr<cv::wechat_qrcode::WeChatQRCode>(
                modelPaths[0].string(), modelPaths[1].string(), modelPaths[2].string(), modelPaths[3].string());
            detector->setScaleFactor(factor);
            return detector;
        };

        std::cout << "payload: " << payload << "\n";
        std::cout << "scan frame is always 1280x720; the real game login QR measures 149px there\n\n";

        const std::vector<float> factors{ 1.00f, 0.50f, 0.42f, 0.35f, 0.25f };
        std::cout << std::left << std::setw(18) << "QR in scan frame";
        for (const float factor : factors) std::cout << std::left << std::setw(15) << ("sf=" + std::to_string(factor).substr(0, 4));
        std::cout << "\n";

        for (const int framePixels : { 400, 260, 200, 149, 120, 100, 80, 60, 45, 35 })
        {
            const cv::Mat prepared = Prepare(Synthetic(payload, framePixels));

            // Report the size actually achieved after module quantisation.
            std::vector<cv::Mat> quads;
            const auto probe = make(1.0f)->detectAndDecode(prepared, quads);
            std::string achieved = "?";
            if (!probe.empty() && !quads.empty() && quads.front().total() >= 4)
            {
                const cv::Mat quad = quads.front().reshape(1, 1);
                achieved = std::to_string(static_cast<int>(cv::norm(
                    cv::Point2f(quad.at<float>(0, 0), quad.at<float>(0, 1)) - cv::Point2f(quad.at<float>(0, 2), quad.at<float>(0, 3))))) + "px";
            }
            else
            {
                achieved = "no quad";
            }

            std::cout << std::left << std::setw(18) << (std::to_string(framePixels) + "px -> " + achieved);

            for (const float factor : factors)
            {
                auto detector = make(factor);
                int hits = 0;
                bool exact = false;
                std::vector<double> cost;
                for (int i = 0; i < iterations; ++i)
                {
                    const auto from = Clock::now();
                    const auto decoded = detector->detectAndDecode(prepared);
                    cost.push_back(Ms(from, Clock::now()));
                    if (!decoded.empty())
                    {
                        ++hits;
                        if (decoded.front() == payload) exact = true;
                    }
                }
                std::sort(cost.begin(), cost.end());
                std::ostringstream cell;
                if (hits == 0) cell << "MISS";
                else if (hits < iterations) cell << hits << "/" << iterations;
                else if (!exact) cell << "WRONG";
                else cell << std::fixed << std::setprecision(1) << cost[cost.size() / 2] << "ms OK";
                std::cout << std::left << std::setw(15) << cell.str();
            }
            std::cout << "\n";
        }
        std::cout << "\nMISS = code not found at all.  a/b = found in only some attempts.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
