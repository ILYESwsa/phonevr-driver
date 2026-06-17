#include "encoder.h"
#include <cstring>
#include <cstdlib>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// x264 software encoder
// Links against x264.lib (included in CMake via vcpkg or manual download).
// This is the guaranteed fallback — works on any CPU, no GPU needed.
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {
#include <stdint.h>
    // x264 C API forward declarations (avoids requiring x264.h at parse time)
    typedef struct x264_t x264_t;
    typedef struct {
        int i_csp;
        int i_plane;
        int i_stride[4];
        uint8_t* plane[4];
    } x264_image_t;
    typedef struct {
        x264_image_t img;
        int64_t i_pts;
        int b_keyframe;
        int i_type;
    } x264_picture_t;
    typedef struct {
        uint8_t* p_payload;
        int      i_payload;
        int      i_type;
    } x264_nal_t;

    // We load x264 dynamically to avoid hard link dependency
    typedef x264_t*  (*PFN_x264_encoder_open) (void*);
    typedef void     (*PFN_x264_encoder_close)(x264_t*);
    typedef int      (*PFN_x264_encoder_encode)(x264_t*, x264_nal_t**, int*, x264_picture_t*, x264_picture_t*);
    typedef void     (*PFN_x264_picture_init) (x264_picture_t*);
    typedef int      (*PFN_x264_param_default_preset)(void*, const char*, const char*);
    typedef int      (*PFN_x264_param_apply_profile)  (void*, const char*);
}

#include <windows.h>

struct X264Api {
    HMODULE                     hLib                 = nullptr;
    PFN_x264_encoder_open       encoder_open         = nullptr;
    PFN_x264_encoder_close      encoder_close        = nullptr;
    PFN_x264_encoder_encode     encoder_encode       = nullptr;
    PFN_x264_picture_init       picture_init         = nullptr;
    PFN_x264_param_default_preset param_default_preset = nullptr;
    PFN_x264_param_apply_profile  param_apply_profile  = nullptr;

    bool Load() {
        // Try both DLL names
        hLib = LoadLibraryA("libx264.dll");
        if (!hLib) hLib = LoadLibraryA("x264.dll");
        if (!hLib) return false;

#define GET(fn) fn = (PFN_##fn)GetProcAddress(hLib, "x264_" #fn); if (!fn) return false;
        GET(encoder_open) GET(encoder_close) GET(encoder_encode)
        GET(picture_init) GET(param_default_preset) GET(param_apply_profile)
#undef GET
        return true;
    }
    void Unload() { if (hLib) { FreeLibrary(hLib); hLib = nullptr; } }
};

// x264 param struct (simplified — only the fields we set)
struct x264_param_t {
    uint32_t cpu;
    int      i_threads;
    int      i_width, i_height;
    struct { int i_num, i_den; } i_fps;
    struct { int i_keyint_max; } i_keyint;
    struct {
        int i_bitrate;
        int i_rc_method;  // 1 = ABR
        int i_vbv_max_bitrate;
        int i_vbv_buffer_size;
    } rc;
    int  i_log_level;   // -1 = none
    int  b_annexb;      // 1 = start codes
    int  b_repeat_headers;
    // We zero the rest
    uint8_t _pad[1024];
};

// ── X264Encoder ───────────────────────────────────────────────────────────────

static X264Api g_x264;

bool X264Encoder::Init(const EncoderConfig& cfg) {
    m_cfg = cfg;

    if (!g_x264.hLib && !g_x264.Load()) {
        // x264 DLL not found — still return false so factory tries next backend
        return false;
    }

    // Build param struct
    auto* param = new x264_param_t{};
    memset(param, 0, sizeof(*param));

    g_x264.param_default_preset(param,
        cfg.lowLatency ? "ultrafast" : "medium",
        cfg.lowLatency ? "zerolatency" : nullptr);

    param->i_width      = cfg.width;
    param->i_height     = cfg.height;
    param->i_fps.i_num  = cfg.fps;
    param->i_fps.i_den  = 1;
    param->i_keyint.i_keyint_max = cfg.gopSize;
    param->rc.i_rc_method       = 1; // ABR
    param->rc.i_bitrate         = cfg.bitrateMbps * 1000;
    param->rc.i_vbv_max_bitrate = cfg.bitrateMbps * 1000;
    param->rc.i_vbv_buffer_size = cfg.bitrateMbps * 1000;
    param->b_annexb             = 1;
    param->b_repeat_headers     = 1;
    param->i_log_level          = -1; // silence

    g_x264.param_apply_profile(param, "baseline"); // Quest-compatible

    m_encoder = g_x264.encoder_open(param);
    delete param;
    if (!m_encoder) return false;

    // Alloc picture
    auto* pic = new x264_picture_t{};
    g_x264.picture_init(pic);
    pic->i_type = 0; // AUTO
    // Alloc YUV planes
    uint32_t ySize  = cfg.width * cfg.height;
    uint32_t uvSize = ySize / 4;
    m_yuvBuf.resize(ySize + uvSize * 2);
    pic->img.i_csp     = 0x000F; // X264_CSP_I420
    pic->img.i_plane   = 3;
    pic->img.i_stride[0] = cfg.width;
    pic->img.i_stride[1] = cfg.width / 2;
    pic->img.i_stride[2] = cfg.width / 2;
    pic->img.plane[0]  = m_yuvBuf.data();
    pic->img.plane[1]  = m_yuvBuf.data() + ySize;
    pic->img.plane[2]  = m_yuvBuf.data() + ySize + uvSize;
    m_picture = pic;

    return true;
}

bool X264Encoder::EncodeFrame(const uint8_t* rgba, uint32_t stride) {
    if (!m_encoder || !m_picture || !m_cb) return false;

    auto* pic = static_cast<x264_picture_t*>(m_picture);
    RGBAtoYUV420(rgba, stride,
        pic->img.plane[0], pic->img.plane[1], pic->img.plane[2]);

    static int64_t pts = 0;
    pic->i_pts = pts++;

    x264_picture_t picOut{};
    x264_nal_t* nals = nullptr;
    int nalCount = 0;

    int frameSize = g_x264.encoder_encode(
        static_cast<x264_t*>(m_encoder),
        &nals, &nalCount, pic, &picOut);

    if (frameSize <= 0 || nalCount == 0) return true; // buffering

    // Concatenate all NALs into one packet
    NALPacket pkt;
    pkt.timestampMs = static_cast<uint32_t>(
        GetTickCount64() % UINT32_MAX);
    pkt.isKeyframe  = (picOut.b_keyframe != 0);
    for (int i = 0; i < nalCount; i++) {
        auto* src = nals[i].p_payload;
        pkt.data.insert(pkt.data.end(), src, src + nals[i].i_payload);
    }
    m_cb(pkt);
    return true;
}

void X264Encoder::Shutdown() {
    if (m_encoder) {
        g_x264.encoder_close(static_cast<x264_t*>(m_encoder));
        m_encoder = nullptr;
    }
    delete static_cast<x264_picture_t*>(m_picture);
    m_picture = nullptr;
    m_yuvBuf.clear();
}

// RGBA → YUV420 planar (fast integer version)
void X264Encoder::RGBAtoYUV420(const uint8_t* rgba, uint32_t stride,
                                 uint8_t* Y, uint8_t* U, uint8_t* V)
{
    uint32_t W = m_cfg.width, H = m_cfg.height;
    for (uint32_t row = 0; row < H; row++) {
        const uint8_t* src = rgba + row * stride;
        uint8_t*       y   = Y + row * W;
        for (uint32_t col = 0; col < W; col++, src += 4) {
            int r = src[0], g = src[1], b = src[2];
            y[col] = (uint8_t)(( 66*r + 129*g +  25*b + 128) >> 8) + 16;
        }
    }
    for (uint32_t row = 0; row < H; row += 2) {
        const uint8_t* src = rgba + row * stride;
        uint8_t* u = U + (row/2) * (W/2);
        uint8_t* v = V + (row/2) * (W/2);
        for (uint32_t col = 0; col < W; col += 2, src += 8) {
            int r = src[0], g = src[1], b = src[2];
            u[col/2] = (uint8_t)((-38*r -  74*g + 112*b + 128) >> 8) + 128;
            v[col/2] = (uint8_t)((112*r -  94*g -  18*b + 128) >> 8) + 128;
        }
    }
}

// ── NVENC stub (real impl needs CUDA SDK — skeleton shown) ────────────────────
bool NVENCEncoder::Init(const EncoderConfig& cfg) {
    m_cfg = cfg;
    // Load nvEncodeAPI64.dll dynamically
    HMODULE hNvEnc = LoadLibraryA("nvEncodeAPI64.dll");
    if (!hNvEnc) return false; // NVIDIA not present, fall through
    FreeLibrary(hNvEnc);
    // Full NVENC impl: create D3D11 device, open NvEncodeAPI session,
    // call NvEncCreateInputBuffer / NvEncCreateBitstreamBuffer,
    // configure NV_ENC_INITIALIZE_PARAMS with H264 + low-latency preset.
    // Returning false here so factory falls back to x264 until NVENC is wired.
    return false;
}
bool NVENCEncoder::EncodeFrame(const uint8_t*, uint32_t) { return false; }
void NVENCEncoder::Shutdown() {}

// ── AMF stub (real impl needs AMD AMF SDK) ────────────────────────────────────
bool AMFEncoder::Init(const EncoderConfig& cfg) {
    m_cfg = cfg;
    HMODULE hAmf = LoadLibraryA("amfrt64.dll");
    if (!hAmf) return false;
    FreeLibrary(hAmf);
    // Full AMF impl: AMFCreateContext, AMFCreateComponent(AMFVideoEncoderVCE_AVC),
    // set AMF_VIDEO_ENCODER_USAGE = AMF_VIDEO_ENCODER_USAGE_ULTRA_LOW_LATENCY,
    // set resolution, frameRate, targetBitrate, then AMFComponent::Init().
    return false;
}
bool AMFEncoder::EncodeFrame(const uint8_t*, uint32_t) { return false; }
void AMFEncoder::Shutdown() {}

// ── Factory ───────────────────────────────────────────────────────────────────
EncoderBackend EncoderFactory::DetectBestBackend() {
    if (LoadLibraryA("nvEncodeAPI64.dll")) return EncoderBackend::NVENC;
    if (LoadLibraryA("amfrt64.dll"))       return EncoderBackend::AMF;
    return EncoderBackend::X264;
}

IEncoder* EncoderFactory::Create(const EncoderConfig& cfg, NALCallback cb) {
    // Try in priority order
    for (auto backend : { EncoderBackend::NVENC, EncoderBackend::AMF, EncoderBackend::X264 }) {
        IEncoder* enc = nullptr;
        switch (backend) {
            case EncoderBackend::NVENC: enc = new NVENCEncoder(); break;
            case EncoderBackend::AMF:   enc = new AMFEncoder();   break;
            case EncoderBackend::X264:  enc = new X264Encoder();  break;
            default: continue;
        }
        enc->SetCallback(cb);
        if (enc->Init(cfg)) return enc; // success
        delete enc; // failed, try next
    }
    return nullptr; // nothing worked
}
