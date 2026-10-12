#pragma once

#include <climits>
#include <cstddef>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "ScreenFrameCopy.hpp"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class ScreenShotDXGI
{
public:
    ScreenShotDXGI() = default;
    ScreenShotDXGI(const ScreenShotDXGI&) = delete;
    ScreenShotDXGI& operator=(const ScreenShotDXGI&) = delete;
    ~ScreenShotDXGI() { doneWithFrame(); }

    bool InitDevice()
    {
        doneWithFrame();
        m_DeskDupl.Reset();
        m_Staging.Reset();
        m_Context.Reset();
        m_Device.Reset();
        m_Valid = false;
        if (!ChooseAdapter())
        {
            return false;
        }
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_1
        };
        D3D_FEATURE_LEVEL level{};
        m_LastError = D3D11CreateDevice(m_Adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
            nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
            D3D11_SDK_VERSION, m_Device.GetAddressOf(), &level, m_Context.GetAddressOf());
        return SUCCEEDED(m_LastError) && m_Device && m_Context;
    }

    bool InitDupl(UINT monitorIdx, int& width, int& height)
    {
        width = height = 0;
        doneWithFrame();
        m_DeskDupl.Reset();
        m_Staging.Reset();
        m_Valid = false;
        if (!m_Device || !m_Adapter)
        {
            m_LastError = E_UNEXPECTED;
            return false;
        }
        Microsoft::WRL::ComPtr<IDXGIOutput> output;
        m_LastError = m_Adapter->EnumOutputs(monitorIdx, output.GetAddressOf());
        if (FAILED(m_LastError))
        {
            return false;
        }
        Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
        m_LastError = output.As(&output1);
        if (FAILED(m_LastError))
        {
            return false;
        }
        m_LastError = output1->DuplicateOutput(m_Device.Get(), m_DeskDupl.GetAddressOf());
        if (FAILED(m_LastError))
        {
            return false;
        }
        DXGI_OUTDUPL_DESC desc{};
        m_DeskDupl->GetDesc(&desc);
        if (!desc.ModeDesc.Width || !desc.ModeDesc.Height ||
            desc.ModeDesc.Width > INT_MAX || desc.ModeDesc.Height > INT_MAX)
        {
            m_LastError = E_INVALIDARG;
            return false;
        }
        width = static_cast<int>(desc.ModeDesc.Width);
        height = static_cast<int>(desc.ModeDesc.Height);
        m_Valid = true;
        return true;
    }

    // 0: acquired; 1: duplication failure; 2: unchanged desktop/timeout.
    int getFrame(int timeout = 100)
    {
        if (!m_Valid || !m_DeskDupl || !doneWithFrame())
        {
            return 1;
        }
        Microsoft::WRL::ComPtr<IDXGIResource> resource;
        DXGI_OUTDUPL_FRAME_INFO info{};
        m_LastError = m_DeskDupl->AcquireNextFrame(static_cast<UINT>(timeout < 0 ? 0 : timeout),
            &info, resource.GetAddressOf());
        if (m_LastError == DXGI_ERROR_WAIT_TIMEOUT)
        {
            return 2;
        }
        if (FAILED(m_LastError))
        {
            m_Valid = false;
            return 1;
        }
        m_FrameHeld = true;
        m_LastError = resource.As(&m_Frame);
        if (FAILED(m_LastError))
        {
            doneWithFrame();
            return 1;
        }
        return 0;
    }

    bool copyFrameToBuffer(BYTE* buffer, std::size_t bufferSize)
    {
        m_CopyStage = "state";
        m_RowPitch = 0;
        if (!m_Frame || !m_FrameHeld || !m_Context || !buffer)
        {
            m_LastError = E_INVALIDARG;
            return false;
        }
        D3D11_TEXTURE2D_DESC desc{};
        m_Frame->GetDesc(&desc);
        m_CopyStage = "texture-format";
        if ((desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
             desc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT) ||
            desc.SampleDesc.Count != 1 || desc.MipLevels != 1 || desc.ArraySize != 1)
        {
            m_LastError = E_INVALIDARG;
            return false;
        }
        if (!m_Staging || m_Width != desc.Width || m_Height != desc.Height || m_Format != desc.Format)
        {
            m_Staging.Reset();
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = desc.MiscFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            m_CopyStage = "create-staging";
            m_LastError = m_Device->CreateTexture2D(&desc, nullptr, m_Staging.GetAddressOf());
            if (FAILED(m_LastError))
            {
                return false;
            }
            m_Width = desc.Width;
            m_Height = desc.Height;
            m_Format = desc.Format;
        }
        m_Context->CopyResource(m_Staging.Get(), m_Frame.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        m_CopyStage = "map-staging";
        m_LastError = m_Context->Map(m_Staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(m_LastError))
        {
            return false;
        }
        m_RowPitch = mapped.RowPitch;
        m_CopyStage = "convert-buffer";
        const bool copied = m_Format == DXGI_FORMAT_R16G16B16A16_FLOAT ?
            CopyScreenRGBA16FToBGRA(buffer, bufferSize, mapped.pData, mapped.RowPitch, m_Width, m_Height) :
            CopyScreenBGRA(buffer, bufferSize, mapped.pData, mapped.RowPitch, m_Width, m_Height);
        m_Context->Unmap(m_Staging.Get(), 0);
        if (!copied)
        {
            m_LastError = E_INVALIDARG;
        }
        return copied;
    }

    D3D11_TEXTURE2D_DESC frameDescription() const
    {
        D3D11_TEXTURE2D_DESC desc{};
        if (m_Frame) m_Frame->GetDesc(&desc);
        return desc;
    }

    const char* lastCopyStage() const { return m_CopyStage; }
    UINT lastRowPitch() const { return m_RowPitch; }

    bool doneWithFrame()
    {
        m_Frame.Reset();
        if (!m_FrameHeld)
        {
            return true;
        }
        m_FrameHeld = false;
        m_LastError = m_DeskDupl->ReleaseFrame();
        if (FAILED(m_LastError))
        {
            m_Valid = false;
            return false;
        }
        return true;
    }

    HRESULT lastError() const { return m_LastError; }

private:
    bool ChooseAdapter()
    {
        m_Adapter.Reset();
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        m_LastError = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
            reinterpret_cast<void**>(factory.GetAddressOf()));
        if (FAILED(m_LastError))
        {
            return false;
        }
        for (UINT i = 0;; ++i)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            m_LastError = factory->EnumAdapters1(i, adapter.GetAddressOf());
            if (FAILED(m_LastError))
            {
                return false;
            }
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
            {
                continue;
            }
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            DXGI_OUTPUT_DESC outputDesc{};
            if (SUCCEEDED(adapter->EnumOutputs(0, output.GetAddressOf())) &&
                SUCCEEDED(output->GetDesc(&outputDesc)) && outputDesc.AttachedToDesktop)
            {
                m_Adapter = adapter;
                return true;
            }
        }
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter1> m_Adapter;
    Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_Context;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> m_DeskDupl;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Frame;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Staging;
    UINT m_Width = 0;
    UINT m_Height = 0;
    DXGI_FORMAT m_Format = DXGI_FORMAT_UNKNOWN;
    const char* m_CopyStage = "none";
    UINT m_RowPitch = 0;
    HRESULT m_LastError = S_OK;
    bool m_FrameHeld = false;
    bool m_Valid = false;
};
