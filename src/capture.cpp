#include "capture.h"
#include <openvr_driver.h>
#include <d3d11.h>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

bool FrameCapture::Init(VideoStreamer* streamer, uint32_t width, uint32_t height) {
    m_streamer = streamer;
    m_width    = width;
    m_height   = height;
    m_readbackBuf.resize(width * height * 4); // RGBA

    // Create a D3D11 device for texture readback
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,                   // default adapter
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr, 0,
        D3D11_SDK_VERSION,
        &m_device, &featureLevel, &m_context);

    if (FAILED(hr)) {
        // Try WARP (software) as fallback
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP,
            nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
            &m_device, &featureLevel, &m_context);
        if (FAILED(hr)) return false;
    }

    return CreateStagingTexture(m_device, width, height);
}

bool FrameCapture::CreateStagingTexture(ID3D11Device* dev, uint32_t w, uint32_t h) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width              = w;
    desc.Height             = h;
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count   = 1;
    desc.Usage              = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

    HRESULT hr = dev->CreateTexture2D(&desc, nullptr, &m_stagingTex);
    return SUCCEEDED(hr);
}

void FrameCapture::CaptureFrame(ID3D11Device* device, ID3D11Texture2D* srcTexture) {
    if (!m_stagingTex || !m_context || !srcTexture) return;

    // Copy GPU texture → staging (CPU-readable)
    m_context->CopyResource(m_stagingTex, srcTexture);

    // Map staging texture to read pixels
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = m_context->Map(m_stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return;

    // Copy row by row (handles pitch/stride differences)
    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    uint8_t*       dst = m_readbackBuf.data();
    uint32_t       rowBytes = m_width * 4;

    for (uint32_t row = 0; row < m_height; row++) {
        memcpy(dst + row * rowBytes, src + row * mapped.RowPitch, rowBytes);
    }

    m_context->Unmap(m_stagingTex, 0);

    // Submit to encoder
    if (m_streamer) {
        m_streamer->SubmitFrame(m_readbackBuf.data(),
                                m_width, m_height, rowBytes);
    }
}

/**
 * PollMirrorTexture
 *
 * Uses IVRCompositor::GetMirrorTextureD3D11() — the easiest way to get
 * the combined stereo frame without any hooking.
 *
 * This is called from PhoneVRDriverProvider::RunFrame() every frame.
 */
void FrameCapture::PollMirrorTexture(vr::IVRCompositor* compositor) {
    if (!compositor || !m_device || !m_streamer) return;

    // GetMirrorTextureD3D11 gives us the left eye mirror (full stereo pair
    // side-by-side is available via vr::Eye_Left with full compositor width)
    void* texPtr = nullptr;
    vr::EVRCompositorError err = compositor->GetMirrorTextureD3D11(
        vr::Eye_Left, m_device, &texPtr);

    if (err != vr::VRCompositorError_None || !texPtr) return;

    auto* tex = static_cast<ID3D11Texture2D*>(texPtr);
    CaptureFrame(m_device, tex);

    // Release the mirror texture handle
    compositor->ReleaseMirrorTextureD3D11(texPtr);
}

void FrameCapture::Shutdown() {
    if (m_stagingTex) { m_stagingTex->Release(); m_stagingTex = nullptr; }
    if (m_context)    { m_context->Release();    m_context    = nullptr; }
    if (m_device)     { m_device->Release();     m_device     = nullptr; }
    m_streamer = nullptr;
}
