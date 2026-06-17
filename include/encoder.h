#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>

// ── Encoder backend selection ─────────────────────────────────────────────────
// Priority: NVENC (NVIDIA) → AMF (AMD) → x264 (CPU)
// The EncoderFactory::Create() picks automatically at runtime.

enum class EncoderBackend { NVENC, AMF, X264, NONE };

struct EncoderConfig {
    uint32_t width      = 2160;
    uint32_t height     = 1200;
    uint32_t fps        = 60;
    uint32_t bitrateMbps = 50;    // target bitrate
    uint32_t gopSize    = 60;     // keyframe every N frames
    bool     lowLatency = true;   // disable B-frames, minimize buffering
};

// Raw NAL unit output from encoder
struct NALPacket {
    std::vector<uint8_t> data;
    uint32_t             timestampMs;
    bool                 isKeyframe;
};

// Callback fired on each encoded NAL
using NALCallback = std::function<void(const NALPacket&)>;

// ── IEncoder interface ────────────────────────────────────────────────────────
class IEncoder {
public:
    virtual ~IEncoder() = default;
    virtual bool     Init(const EncoderConfig& cfg)  = 0;
    virtual bool     EncodeFrame(const uint8_t* rgba, uint32_t stride) = 0;
    virtual void     Shutdown()                       = 0;
    virtual EncoderBackend Backend() const            = 0;
    virtual std::string BackendName() const           = 0;
    void SetCallback(NALCallback cb) { m_cb = std::move(cb); }
protected:
    NALCallback m_cb;
};

// ── NVENC encoder (NVIDIA GPU) ────────────────────────────────────────────────
class NVENCEncoder : public IEncoder {
public:
    bool Init(const EncoderConfig& cfg) override;
    bool EncodeFrame(const uint8_t* rgba, uint32_t stride) override;
    void Shutdown() override;
    EncoderBackend Backend() const override { return EncoderBackend::NVENC; }
    std::string BackendName() const override { return "NVENC"; }
private:
    void* m_encoder  = nullptr; // NV_ENC_SESSION (opaque)
    void* m_d3dDev   = nullptr; // ID3D11Device
    void* m_inputBuf = nullptr; // NV_ENC_INPUT_PTR
    void* m_outputBuf= nullptr; // NV_ENC_OUTPUT_PTR
    EncoderConfig m_cfg;
};

// ── AMF encoder (AMD GPU) ─────────────────────────────────────────────────────
class AMFEncoder : public IEncoder {
public:
    bool Init(const EncoderConfig& cfg) override;
    bool EncodeFrame(const uint8_t* rgba, uint32_t stride) override;
    void Shutdown() override;
    EncoderBackend Backend() const override { return EncoderBackend::AMF; }
    std::string BackendName() const override { return "AMF"; }
private:
    void* m_amfContext  = nullptr;
    void* m_amfEncoder  = nullptr;
    EncoderConfig m_cfg;
};

// ── x264 software encoder (CPU fallback) ─────────────────────────────────────
// Always available — no GPU required.
class X264Encoder : public IEncoder {
public:
    bool Init(const EncoderConfig& cfg) override;
    bool EncodeFrame(const uint8_t* rgba, uint32_t stride) override;
    void Shutdown() override;
    EncoderBackend Backend() const override { return EncoderBackend::X264; }
    std::string BackendName() const override { return "x264 (CPU)"; }
private:
    void* m_encoder = nullptr; // x264_t*
    void* m_picture = nullptr; // x264_picture_t*
    EncoderConfig m_cfg;
    std::vector<uint8_t> m_yuvBuf;
    void RGBAtoYUV420(const uint8_t* rgba, uint32_t stride,
                      uint8_t* y, uint8_t* u, uint8_t* v);
};

// ── Factory ───────────────────────────────────────────────────────────────────
class EncoderFactory {
public:
    // Auto-detects best available backend and returns a ready encoder.
    // Returns nullptr if nothing is available (shouldn't happen — x264 is always present).
    static IEncoder* Create(const EncoderConfig& cfg, NALCallback cb);
    static EncoderBackend DetectBestBackend();
};
