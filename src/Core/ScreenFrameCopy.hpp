#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

inline bool CopyScreenBGRA(void* destination, std::size_t capacity, const void* source,
    std::size_t sourcePitch, std::size_t width, std::size_t height)
{
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (!destination || !source || !width || !height || width > maximum / 4)
    {
        return false;
    }
    const std::size_t rowBytes = width * 4;
    if (sourcePitch < rowBytes || height > maximum / rowBytes ||
        capacity < rowBytes * height || height > maximum / sourcePitch)
    {
        return false;
    }
    auto* output = static_cast<unsigned char*>(destination);
    const auto* input = static_cast<const unsigned char*>(source);
    for (std::size_t row = 0; row < height; ++row)
    {
        std::memcpy(output + row * rowBytes, input + row * sourcePitch, rowBytes);
    }
    return true;
}

inline unsigned char ScreenHalfFloatToSRGB(std::uint16_t half)
{
    if (half & 0x8000) return 0;
    const int exponent = (half >> 10) & 31;
    const int mantissa = half & 1023;
    if (exponent == 31) return mantissa ? 0 : 255;
    const float linear = exponent == 0 ? std::ldexp(static_cast<float>(mantissa), -24) :
        std::ldexp(static_cast<float>(1024 + mantissa), exponent - 25);
    if (linear >= 1.0f) return 255;
    const float encoded = linear <= 0.0031308f ? 12.92f * linear :
        1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<unsigned char>(encoded * 255.0f + 0.5f);
}

inline bool CopyScreenRGBA16FToBGRA(void* destination, std::size_t capacity, const void* source,
    std::size_t sourcePitch, std::size_t width, std::size_t height)
{
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    if (!destination || !source || !width || !height || width > maximum / 8)
        return false;
    const std::size_t outputRow = width * 4;
    if (sourcePitch < width * 8 || height > maximum / outputRow ||
        capacity < outputRow * height || height > maximum / sourcePitch)
        return false;

    // A LUT avoids three gamma calculations per pixel on every captured frame.
    static const auto srgb = [] {
        std::array<unsigned char, 65536> table{};
        for (std::size_t i = 0; i < table.size(); ++i)
            table[i] = ScreenHalfFloatToSRGB(static_cast<std::uint16_t>(i));
        return table;
    }();
    auto* output = static_cast<unsigned char*>(destination);
    const auto* input = static_cast<const unsigned char*>(source);
    for (std::size_t row = 0; row < height; ++row)
    {
        for (std::size_t column = 0; column < width; ++column)
        {
            std::array<std::uint16_t, 4> rgba{};
            std::memcpy(rgba.data(), input + row * sourcePitch + column * 8, 8);
            auto* pixel = output + row * outputRow + column * 4;
            pixel[0] = srgb[rgba[2]];
            pixel[1] = srgb[rgba[1]];
            pixel[2] = srgb[rgba[0]];
            pixel[3] = 255;
        }
    }
    return true;
}
