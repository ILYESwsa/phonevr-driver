#pragma once
#include "streamer.h"
#include <d3d11.h>
#include <functional>

/**
 * FrameCapture
 *
 * Hooks into the SteamVR compositor's D3D11 Present call to grab
 * each rendered frame (both eyes combined) as an RGBA texture,
 * then hands it to VideoStreamer for encoding.
 *
 * How it works:
 *   1. SteamVR uses a shared D3D11 texture for the compositor output.
 *   2. We create a staging texture (CPU-readable copy).
 *   3. After each Present, we CopyResource → Map → read RGBA bytes.
 *   4. Pass to VideoStreamer::SubmitFrame().
 *
 * Alternative: use IVRCompositor::GetMirrorTextureD3D11() which gives
 * us the mirror view texture directly — easier and no hooking needed.
 */
class FrameCapture {
public:
    FrameCapture()  = default;
    ~FrameCapture() { Shutdown(); }

    bool Init(VideoStreamer* streamer, uint32_t width, uint32_t height);
    void Shutdown();

    // Call after SteamVR compositor submits each eye.
    // In practice: called from IVRCompositorOverlay or a D3D hook.
    void CaptureFrame(ID3D11Device* device, ID3D11Texture2D* srcTexture);

    // Alternative: poll-based capture using the SteamVR mirror texture
    // Call this from RunFrame() in the driver provider.
    void PollMirrorTexture(vr::IVRCompositor* compositor);

private:
    VideoStreamer*       m_streamer      = nullptr;
    ID3D11Device*        m_device        = nullptr;
    ID3D11DeviceContext* m_context       = nullptr;
    ID3D11Texture2D*     m_stagingTex    = nullptr;
    uint32_t             m_width         = 0;
    uint32_t             m_height        = 0;

    // Read-back buffer (avoids alloc per frame)
    std::vector<uint8_t> m_readbackBuf;

    bool CreateStagingTexture(ID3D11Device* dev, uint32_t w, uint32_t h);
};
