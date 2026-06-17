#pragma once
#include "encoder.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>

/**
 * VideoStreamer
 *
 * Captures the SteamVR compositor output (left+right eye combined),
 * encodes it with the best available encoder (NVENC/AMF/x264),
 * and sends H.264 NAL fragments to the Android phone over UDP.
 *
 * Transport modes:
 *   WiFi  — sends to phone IP directly on port 6001
 *   USB   — sends to 127.0.0.1:6001 (ADB tunnel handles forwarding)
 *
 * Packet format matches VideoStreamReceiver.kt:
 *   [0]    byte   magic  0x56
 *   [1]    byte   type   0x02 (video)
 *   [2-3]  uint16 sequence number
 *   [4-5]  uint16 total fragments in this frame
 *   [6-7]  uint16 fragment index
 *   [8-11] uint32 frame timestamp ms
 *   [12..] bytes  H.264 NAL data fragment
 *
 * Max UDP payload: 1400 bytes (below MTU to avoid IP fragmentation)
 */
class VideoStreamer {
public:
    static constexpr uint16_t VIDEO_PORT    = 6001;
    static constexpr size_t   MAX_FRAG_SIZE = 1388; // 1400 - 12 byte header

    struct Config {
        std::string phoneIp    = "127.0.0.1"; // 127.0.0.1 for USB, phone IP for WiFi
        bool        usbMode    = true;
        EncoderConfig encoder;
    };

    VideoStreamer() = default;
    ~VideoStreamer() { Stop(); }

    bool Start(const Config& cfg);
    void Stop();

    // Call each frame from the SteamVR Present hook with the compositor RGBA buffer
    void SubmitFrame(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t stride);

    std::string GetBackendName() const {
        return m_encoder ? m_encoder->BackendName() : "none";
    }

    bool IsRunning() const { return m_running; }

private:
    void SendNAL(const NALPacket& pkt);
    void FragmentAndSend(const uint8_t* data, size_t size,
                         uint32_t timestamp, bool keyframe);

    std::unique_ptr<IEncoder> m_encoder;
    SOCKET   m_socket    = INVALID_SOCKET;
    sockaddr_in m_dest{};
    std::atomic<bool> m_running{ false };
    uint16_t m_seqNum    = 0;
};
