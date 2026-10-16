// Offline latency benchmark for the screen scan path.
// Mirrors QRCodeForScreen::monitorScreen(): a full desktop frame is resized to
// 1280x720 and then handed to the same WeChatQRCode detector the application uses.
//
// Question under test: how long does "QR visible -> decoded (locked)" take, and
// which knob shortens it.
//
// Round 3 measures three candidate levers:
//   1. QRScanner::detector->setScaleFactor(...)  -- scales the image the detector
//      neural network actually consumes. The library default targets a 160000 px
//      area; QRScanner hard-codes 1.0, i.e. the full 1280x720 (921600 px).
//   2. Feeding a region of interest instead of the whole frame, located by the
//      classic cv::QRCodeDetector.
//   3. Locating on a downscaled copy and mapping the box back.
#include "QRScanner.h"
#include "QRModelPaths.hpp"
#include "qrcodegen.hpp"

#include <opencv2/wechat_qrcode.hpp>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}

void Report(const std::string& label, std::vector<double> values, const std::string& note)
{
    std::sort(values.begin(), values.end());
    double total = 0;
    for (const double value : values) total += value;
    std::cout << "  " << label << ": min=" << values.front()
              << "ms  median=" << values[values.size() / 2]
              << "ms  mean=" << (total / values.size())
              << "ms  max=" << values.back() << "ms  (n=" << values.size() << ")"
              << (note.empty() ? std::string() : "  [" + note + "]") << '\n';
}

cv::Mat Synthetic(std::string_view payload, int qrPixels, int width, int height, bool busy, bool hideQr)
{
    cv::Mat frame(height, width, CV_8UC3, cv::Scalar(30, 34, 40));
    if (busy)
    {
        // High-contrast clutter mimics a live game scene behind the login panel.
        cv::RNG rng(20261016);
        for (int i = 0; i < 4000; ++i)
        {
            const auto colour = rng.uniform(0, 255);
            cv::rectangle(frame, { rng.uniform(0, width), rng.uniform(0, height) },
                { rng.uniform(0, width), rng.uniform(0, height) },
                cv::Scalar(colour, colour, colour), cv::FILLED);
        }
    }
    if (hideQr) return frame;
    const auto code = qrcodegen::QrCode::encodeText(std::string(payload).c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int modules = code.getSize() + 8;
    const int scale = (std::max)(1, qrPixels / modules);
    const int size = modules * scale;
    cv::Mat qr(size, size, CV_8UC3, cv::Scalar(255, 255, 255));
    for (int y = 0; y < code.getSize(); ++y)
        for (int x = 0; x < code.getSize(); ++x)
            if (code.getModule(x, y))
                qr(cv::Rect((x + 4) * scale, (y + 4) * scale, scale, scale)).setTo(cv::Scalar(0, 0, 0));
    qr.copyTo(frame(cv::Rect((width - size) / 2, (height - size) / 2, size, size)));
    return frame;
}

// The application always feeds the detector a 1280x720 frame.
cv::Mat Prepare(const cv::Mat& frame)
{
    cv::Mat resized;
    cv::resize(frame, resized, { 1280, 720 });
    return resized;
}

struct SweepResult
{
    std::vector<double> cost;
    std::string payload;
    int boxes = 0;
};

SweepResult Sweep(const cv::Ptr<cv::wechat_qrcode::WeChatQRCode>& detector, const cv::Mat& image, int iterations)
{
    SweepResult result;
    for (int i = 0; i < iterations; ++i)
    {
        const auto from = Clock::now();
        const auto decoded = detector->detectAndDecode(image);
        result.cost.push_back(Ms(from, Clock::now()));
        if (i == 0)
        {
            result.boxes = static_cast<int>(decoded.size());
            if (!decoded.empty()) result.payload = decoded.front();
        }
    }
    return result;
}

std::string Note(const SweepResult& result)
{
    std::string note = "boxes=" + std::to_string(result.boxes);
    if (result.payload.empty()) note += ", NOT DECODED";
    else note += ", payload=" + result.payload.substr(0, 22);
    return note;
}
}

int main(int argc, char** argv)
{
    try
    {
        const int iterations = argc > 2 ? std::stoi(argv[2]) : 20;

        const auto modelStart = Clock::now();
        const auto modelPaths = QRModelPaths(QRModelDirectory());
        std::cout << "ScanModel resolved in " << Ms(modelStart, Clock::now()) << " ms: "
                  << modelPaths[0].string() << '\n';

        auto make = [&modelPaths](bool callScaleFactor, float factor)
        {
            auto detector = cv::makePtr<cv::wechat_qrcode::WeChatQRCode>(
                modelPaths[0].string(), modelPaths[1].string(), modelPaths[2].string(), modelPaths[3].string());
            if (callScaleFactor) detector->setScaleFactor(factor);
            return detector;
        };

        {
            const auto from = Clock::now();
            auto probe = make(true, 1.0f);
            std::cout << "QRScanner-equivalent construction (4 caffe models): " << Ms(from, Clock::now()) << " ms\n";
            std::cout << "  getScaleFactor() after setScaleFactor(1.0) = " << probe->getScaleFactor() << '\n';
            probe = make(false, 0.0f);
            std::cout << "  getScaleFactor() when never called          = " << probe->getScaleFactor() << '\n';
        }
        std::cout << '\n';

        std::vector<std::pair<std::string, cv::Mat>> frames;
        if (argc > 1 && std::string(argv[1]) != "-")
        {
            const cv::Mat source = cv::imread(argv[1], cv::IMREAD_COLOR);
            if (source.empty()) throw std::runtime_error("cannot read the input image");
            frames.emplace_back("real screenshot " + std::to_string(source.cols) + "x" + std::to_string(source.rows) + " (busy)",
                source);
        }
        frames.emplace_back("synthetic 260px QR, cluttered 1920x1080 (game-like)",
            Synthetic("G152#KURO#benchmark-payload-0123456789abcdef", 260, 1920, 1080, true, false));
        frames.emplace_back("synthetic 260px QR, flat 1920x1080 (clean)",
            Synthetic("G152#KURO#benchmark-payload-0123456789abcdef", 260, 1920, 1080, false, false));
        frames.emplace_back("no QR at all, flat 1920x1080 (detect-only cost)",
            Synthetic("", 260, 1920, 1080, false, true));

        // Lever 1: the scale factor the detector network actually consumes.
        struct Variant
        {
            std::string label;
            bool call;
            float factor;
            std::string networkInput;
        };
        const std::vector<Variant> variants{
            { "scaleFactor(1.00) == QRScanner today", true, 1.0f, "net input 1280x720 = 921600 px" },
            { "scaleFactor(0.71)", true, 0.71f, "net input 909x511 = 464000 px" },
            { "scaleFactor(0.50)", true, 0.50f, "net input 640x360 = 230400 px" },
            { "scaleFactor(0.42)", true, 0.42f, "net input 538x302 = 162500 px" },
            { "scaleFactor(0.35)", true, 0.35f, "net input 448x252 = 112900 px" },
            { "scaleFactor(0.25)", true, 0.25f, "net input 320x180 = 57600 px" },
            { "library default (never called)", false, 0.0f, "net input targets 160000 px" },
        };

        for (const auto& [label, frame] : frames)
        {
            const cv::Mat image = Prepare(frame);
            std::cout << "=== " << label << " ===\n";

            for (const auto& variant : variants)
            {
                auto detector = make(variant.call, variant.factor);
                const auto result = Sweep(detector, image, iterations);
                Report(variant.label + "  [" + variant.networkInput + "]", result.cost, Note(result));
            }

            // Lever 2/3: locate cheaply, then decode only the region.
            cv::QRCodeDetector locator;
            for (const double factor : { 1.0, 0.5, 0.25 })
            {
                cv::Mat reduced;
                cv::resize(image, reduced, {}, factor, factor, cv::INTER_AREA);
                std::vector<cv::Point2f> corners;
                std::vector<double> locateCost;
                bool located = false;
                for (int i = 0; i < iterations; ++i)
                {
                    const auto from = Clock::now();
                    located = locator.detect(reduced, corners);
                    locateCost.push_back(Ms(from, Clock::now()));
                }
                const std::string scaleTag = factor == 1.0 ? "1/1" : (factor == 0.5 ? "1/2" : "1/4");
                Report("locate cv::QRCodeDetector @ " + scaleTag + " (" + std::to_string(reduced.cols) + "x" +
                    std::to_string(reduced.rows) + ")", locateCost, located ? "found" : "NOT FOUND");
                if (!located || corners.empty()) continue;

                cv::Rect box = cv::boundingRect(corners);
                box.x = static_cast<int>(box.x / factor);
                box.y = static_cast<int>(box.y / factor);
                box.width = static_cast<int>(box.width / factor);
                box.height = static_cast<int>(box.height / factor);

                for (const double pad : { 1.0, 1.25 })
                {
                    const int side = static_cast<int>((std::max)(box.width, box.height) * pad);
                    const cv::Point centre(box.x + box.width / 2, box.y + box.height / 2);
                    cv::Rect roi(centre.x - side / 2, centre.y - side / 2, side, side);
                    roi &= cv::Rect(0, 0, image.cols, image.rows);
                    if (roi.width < 24 || roi.height < 24) continue;

                    auto detector = make(true, 1.0f);
                    const auto result = Sweep(detector, image(roi), iterations);
                    Report("decode ROI (locate " + scaleTag + ", pad " + std::to_string(pad).substr(0, 4) + ", " +
                        std::to_string(roi.width) + "x" + std::to_string(roi.height) + ")", result.cost, Note(result));
                }
            }
            std::cout << '\n';
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
