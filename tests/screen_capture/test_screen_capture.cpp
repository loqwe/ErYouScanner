#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "QRModelPaths.hpp"
#include "ScreenFrameCopy.hpp"
// Inject real WARP textures into the readback path without capturing a desktop.
#define private public
#include "ScreenShotDXGI.hpp"
#undef private

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void testFloatReadback()
{
    ScreenShotDXGI capture;
    require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &capture.m_Device, nullptr, &capture.m_Context)),
        "WARP device creation failed");
    // Half-float RGBA, including padding, out-of-range values, infinity and NaN.
    const std::array<std::uint16_t, 24> pixels{
        0x3c00, 0x3800, 0x0000, 0x0000, 0x0000, 0x3c00, 0x3c00, 0x3c00, 9, 9, 9, 9,
        0xbc00, 0x4000, 0x7c00, 0x0000, 0x7e00, 0x0000, 0x3c00, 0x0000, 9, 9, 9, 9
    };
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 2;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = pixels.data();
    data.SysMemPitch = 24;
    require(SUCCEEDED(capture.m_Device->CreateTexture2D(&desc, &data, &capture.m_Frame)),
        "Float texture creation failed");
    std::array<unsigned char, 18> output{};
    output.fill(42);
    capture.m_FrameHeld = true;
    const bool copied = capture.copyFrameToBuffer(output.data() + 1, 16);
    capture.m_FrameHeld = false;
    require(copied, "RGBA16F texture rejected by screen readback");
    const std::array<unsigned char, 16> expected{
        0, 188, 255, 255, 255, 255, 0, 255,
        255, 255, 0, 255, 255, 0, 0, 255
    };
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(output[i + 1] == expected[i], "RGBA16F conversion/channel order incorrect");
    require(output.front() == 42 && output.back() == 42, "Float readback crossed bounds");

    capture.m_FrameHeld = true;
    const auto beforeSmallCopy = output;
    const bool smallAccepted = capture.copyFrameToBuffer(output.data(), 15);
    capture.m_FrameHeld = false;
    require(!smallAccepted, "Float readback accepted undersized output");
    require(output == beforeSmallCopy, "Rejected float copy modified output");
    require(!CopyScreenRGBA16FToBGRA(output.data(), 16, pixels.data(), 15, 2, 2), "Short float pitch accepted");
    require(!CopyScreenRGBA16FToBGRA(nullptr, 16, pixels.data(), 24, 2, 2), "Null float output accepted");
    require(!CopyScreenRGBA16FToBGRA(output.data(), 16, nullptr, 24, 2, 2), "Null float input accepted");
    require(!CopyScreenRGBA16FToBGRA(output.data(), 16, pixels.data(), 24, 0, 2), "Zero float width accepted");
    const auto maximum = (std::numeric_limits<std::size_t>::max)();
    require(!CopyScreenRGBA16FToBGRA(output.data(), maximum, pixels.data(), maximum, maximum, 2),
        "Float width overflow accepted");
    require(!CopyScreenRGBA16FToBGRA(output.data(), maximum, pixels.data(), maximum, 1, 2),
        "Float pitch overflow accepted");

    // Same dimensions with a different format must recreate the staging texture.
    const std::array<unsigned char, 16> bgra{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    data.pSysMem = bgra.data();
    data.SysMemPitch = 8;
    capture.m_Frame.Reset();
    require(SUCCEEDED(capture.m_Device->CreateTexture2D(&desc, &data, &capture.m_Frame)),
        "BGRA texture creation failed");
    capture.m_FrameHeld = true;
    const bool switched = capture.copyFrameToBuffer(output.data() + 1, 16);
    capture.m_FrameHeld = false;
    require(switched, "Readback failed after switching from float to BGRA");
    for (std::size_t i = 0; i < bgra.size(); ++i)
        require(output[i + 1] == bgra[i], "Staging format change lost BGRA pixels");
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    data.pSysMem = pixels.data();
    data.SysMemPitch = 24;
    capture.m_Frame.Reset();
    require(SUCCEEDED(capture.m_Device->CreateTexture2D(&desc, &data, &capture.m_Frame)),
        "Second float texture creation failed");
    capture.m_FrameHeld = true;
    const bool switchedBack = capture.copyFrameToBuffer(output.data() + 1, 16);
    capture.m_FrameHeld = false;
    require(switchedBack, "Readback failed after switching back to float");
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(output[i + 1] == expected[i], "Staging format change lost float pixels");
}

int main(int argc, char** argv)
{
    try
    {
        testFloatReadback();
        const std::array<unsigned char, 24> source{
            1, 2, 3, 4, 5, 6, 7, 8, 99, 99, 99, 99,
            9, 10, 11, 12, 13, 14, 15, 16, 99, 99, 99, 99
        };
        std::array<unsigned char, 18> destination{};
        destination.fill(42);
        require(CopyScreenBGRA(destination.data() + 1, 16, source.data(), 12, 2, 2), "Padded copy failed");
        for (std::size_t i = 0; i < 16; ++i)
        {
            require(destination[i + 1] == i + 1, "RowPitch padding entered image");
        }
        require(destination.front() == 42 && destination.back() == 42, "Copy crossed destination bounds");
        require(!CopyScreenBGRA(destination.data(), 15, source.data(), 12, 2, 2), "Small capacity accepted");
        require(!CopyScreenBGRA(destination.data(), 18, source.data(), 7, 2, 2), "Small pitch accepted");
        require(!CopyScreenBGRA(nullptr, 18, source.data(), 12, 2, 2), "Null output accepted");
        require(!CopyScreenBGRA(destination.data(), 18, nullptr, 12, 2, 2), "Null input accepted");
        require(!CopyScreenBGRA(destination.data(), 18, source.data(), 12, 0, 2), "Zero width accepted");
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        require(!CopyScreenBGRA(destination.data(), 18, source.data(), maximum, maximum, 2), "Width overflow accepted");
        require(!CopyScreenBGRA(destination.data(), maximum, source.data(), maximum, 1, 2), "Pitch overflow accepted");

        ScreenShotDXGI capture;
        int width = 1;
        int height = 1;
        require(!capture.InitDupl(0, width, height), "Duplication accepted missing device");
        require(width == 0 && height == 0, "Failure left stale dimensions");
        require(capture.getFrame() == 1, "Acquisition accepted missing duplication");
        require(!capture.copyFrameToBuffer(destination.data(), destination.size()), "Copy accepted missing frame");
        require(capture.doneWithFrame(), "Release with no held frame failed");

        const auto modelDirectory = QRModelDirectory();
        const auto workingDirectory = std::filesystem::current_path();
        std::filesystem::current_path(modelDirectory.parent_path());
        require(QRModelDirectory() == modelDirectory, "Models depend on working directory");
        std::filesystem::current_path(workingDirectory);
        bool missingRejected = false;
        try
        {
            QRModelPaths(modelDirectory / "missing-test-models");
        }
        catch (const std::runtime_error&)
        {
            missingRejected = true;
        }
        require(missingRejected, "Missing models silently accepted");
        if (argc == 2)
        {
            require(QRModelPaths(std::filesystem::path(argv[1])).size() == 4, "Valid model files rejected");
        }
        std::cout << "Screen capture regressions passed: real WARP float/BGRA readback and format switches, gamma/channel conversion, padded rows, bounds, overflow, invalid DXGI state, EXE model path, missing models\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
