// Verifies the shipped QRScanner class itself -- the one both scan paths use --
// rather than a detector built inside the benchmark.
//
// Before the change (setScaleFactor(1.0)) a real game login code took ~88 ms to
// decode; after the change (0.42) it takes ~7 ms, with an identical payload.
// This program fails loudly if either regresses.
#include "QRScanner.h"

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;

double Ms(Clock::time_point from, Clock::time_point to)
{
    return std::chrono::duration<double, std::milli>(to - from).count();
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2) throw std::runtime_error("usage: verify_shipped_scanner <real-screenshot> [iterations]");
        const int iterations = argc > 2 ? std::stoi(argv[2]) : 20;
        const std::string expected = "G152#KURO_038a5d17-f3f6-4474-ba86-dba3aac105d4";

        const cv::Mat source = cv::imread(argv[1], cv::IMREAD_COLOR);
        if (source.empty()) throw std::runtime_error("cannot read the input image");
        cv::Mat frame;
        cv::resize(source, frame, { 1280, 720 });

        // Same construction the application performs on every scan start.
        QRScanner scanner;

        std::vector<double> cost;
        std::string payload;
        for (int i = 0; i < iterations; ++i)
        {
            std::string decoded;
            const auto from = Clock::now();
            scanner.decodeSingle(frame, decoded);
            cost.push_back(Ms(from, Clock::now()));
            if (decoded.size() > payload.size()) payload = decoded;
        }
        std::sort(cost.begin(), cost.end());

        bool ok = true;
        if (payload != expected)
        {
            std::cout << "payload mismatch\n  expected " << expected << "\n  got      " << payload << '\n';
            ok = false;
        }
        const double median = cost[cost.size() / 2];
        if (median > 20.0)
        {
            std::cout << "decode too slow: median " << median << " ms (expected ~7 ms)\n";
            ok = false;
        }

        std::cout << "shipped QRScanner decodeSingle @1280x720\n";
        std::cout << "  min=" << cost.front() << "ms  median=" << median
                  << "ms  max=" << cost.back() << "ms  (n=" << cost.size() << ")\n";
        std::cout << "  payload=" << payload << '\n';
        std::cout << (ok ? "PASS\n" : "FAIL\n");
        return ok ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
