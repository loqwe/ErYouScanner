#include "QRScanner.h"
#include "qrcodegen.hpp"
#include "ScreenFrameCopy.hpp"

#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        const std::string expected = "https://example.invalid/login?ticket=regression-only";
        const auto code = qrcodegen::QrCode::encodeText(expected.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        constexpr int border = 4;
        constexpr int scale = 8;
        const int size = (code.getSize() + border * 2) * scale;
        cv::Mat image(size, size, CV_8UC3, cv::Scalar(255, 255, 255));
        for (int y = 0; y < code.getSize(); ++y)
        {
            for (int x = 0; x < code.getSize(); ++x)
            {
                if (code.getModule(x, y))
                {
                    image(cv::Rect((x + border) * scale, (y + border) * scale, scale, scale))
                        .setTo(cv::Scalar(0, 0, 0));
                }
            }
        }
        QRScanner scanner;
        std::string decoded;
        scanner.decodeSingle(image, decoded);
        if (decoded != expected) throw std::runtime_error("real QR decode failed");
        // Linear RGBA16F with padded rows and a white level below 1.0.
        const std::size_t pitch = static_cast<std::size_t>(size) * 8 + 16;
        std::vector<unsigned char> floatFrame(pitch * size + 1, 42);
        for (int y = 0; y < size; ++y)
        {
            for (int x = 0; x < size; ++x)
            {
                const std::uint16_t value = image.at<cv::Vec3b>(y, x)[0] ? 0x3266 : 0;
                const std::array<std::uint16_t, 4> pixel{value, value, value, 0};
                std::memcpy(floatFrame.data() + 1 + static_cast<std::size_t>(y) * pitch +
                    static_cast<std::size_t>(x) * 8, pixel.data(), 8);
            }
        }
        cv::Mat converted(size, size, CV_8UC4);
        if (!CopyScreenRGBA16FToBGRA(converted.data, converted.total() * converted.elemSize(),
            floatFrame.data() + 1, pitch, size, size))
            throw std::runtime_error("float QR frame conversion failed");
        scanner.decodeSingle(converted, decoded);
        if (decoded != expected) throw std::runtime_error("converted float QR decode failed");
        scanner.decodeSingle({}, decoded);
        if (!decoded.empty()) throw std::runtime_error("empty image retained old QR");
        scanner.decodeMultiple(image, decoded);
        if (decoded != expected) throw std::runtime_error("multiple QR decode failed");
        scanner.decodeMultiple({}, decoded);
        if (!decoded.empty()) throw std::runtime_error("empty multi image retained old QR");
        std::cout << "real model initialization, BGRA/RGBA16F QR decoding and stale-output clearing passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
