#include "streamer.h"
#include <cstring>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

bool VideoStreamer::Start(const Config& cfg) {
    // Init Winsock
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    // Create UDP socket
    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == INVALID_SOCKET) return false;

    // Set send buffer large enough for video bursts
    int sndbuf = 4 * 1024 * 1024; // 4MB
    setsockopt(m_socket, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, sizeof(sndbuf));

    // Configure destination
    memset(&m_dest, 0, sizeof(m_dest));
    m_dest.sin_family = AF_INET;
    m_dest.sin_port   = htons(VIDEO_PORT);
    inet_pton(AF_INET,
        cfg.usbMode ? "127.0.0.1" : cfg.phoneIp.c_str(),
        &m_dest.sin_addr);

    // Create encoder (auto-detects NVENC/AMF/x264)
    m_encoder.reset(EncoderFactory::Create(cfg.encoder,
        [this](const NALPacket& pkt) { SendNAL(pkt); }));

    if (!m_encoder) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    m_running = true;
    return true;
}

void VideoStreamer::Stop() {
    m_running = false;
    if (m_encoder) { m_encoder->Shutdown(); m_encoder.reset(); }
    if (m_socket != INVALID_SOCKET) {
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
    WSACleanup();
}

void VideoStreamer::SubmitFrame(const uint8_t* rgba,
                                 uint32_t width, uint32_t height,
                                 uint32_t stride)
{
    if (!m_running || !m_encoder) return;
    // Encoder fires callback synchronously → SendNAL → FragmentAndSend
    m_encoder->EncodeFrame(rgba, stride);
}

void VideoStreamer::SendNAL(const NALPacket& pkt) {
    if (m_socket == INVALID_SOCKET) return;
    FragmentAndSend(pkt.data.data(), pkt.data.size(),
                    pkt.timestampMs, pkt.isKeyframe);
}

void VideoStreamer::FragmentAndSend(const uint8_t* data, size_t size,
                                     uint32_t timestamp, bool /*keyframe*/)
{
    if (size == 0) return;

    // Calculate fragment count
    uint16_t totalFrags = static_cast<uint16_t>(
        (size + MAX_FRAG_SIZE - 1) / MAX_FRAG_SIZE);

    // Build and send each fragment
    uint8_t buf[1400];
    for (uint16_t i = 0; i < totalFrags; i++) {
        size_t offset    = i * MAX_FRAG_SIZE;
        size_t fragSize  = std::min(MAX_FRAG_SIZE, size - offset);

        // Header (12 bytes)
        buf[0]  = 0x56;                      // magic
        buf[1]  = 0x02;                      // type: video
        buf[2]  = (m_seqNum >> 8) & 0xFF;
        buf[3]  =  m_seqNum       & 0xFF;
        buf[4]  = (totalFrags >> 8) & 0xFF;
        buf[5]  =  totalFrags      & 0xFF;
        buf[6]  = (i >> 8) & 0xFF;
        buf[7]  =  i       & 0xFF;
        buf[8]  = (timestamp >> 24) & 0xFF;
        buf[9]  = (timestamp >> 16) & 0xFF;
        buf[10] = (timestamp >>  8) & 0xFF;
        buf[11] =  timestamp        & 0xFF;

        // Payload
        memcpy(buf + 12, data + offset, fragSize);

        sendto(m_socket,
               (char*)buf, (int)(12 + fragSize),
               0,
               (sockaddr*)&m_dest, sizeof(m_dest));
    }
    m_seqNum++;
}
