#include "driver.h"
#include "controller.h"
#include "streamer.h"
#include "capture.h"
#include <openvr_driver.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

// ── Config file (phonevr/resources/settings.cfg) ─────────────────────────────
// Written by the PC companion, read here at driver load time.
// Format: key=value lines
static std::string ReadSetting(const std::string& key, const std::string& def = "") {
    std::ifstream f("phonevr/resources/settings.cfg");
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (line.substr(0, eq) == key) return line.substr(eq + 1);
    }
    return def;
}

// ─── PhoneVRHMD ──────────────────────────────────────────────────────────────

PhoneVRHMD::PhoneVRHMD() {}
PhoneVRHMD::~PhoneVRHMD() { if (m_running) Deactivate(); }

vr::EVRInitError PhoneVRHMD::Activate(uint32_t unObjectId) {
    m_deviceId = unObjectId;
    auto props = vr::VRProperties();
    props->SetStringProperty(m_deviceId, vr::Prop_ModelNumber_String,     "PhoneVR");
    props->SetStringProperty(m_deviceId, vr::Prop_SerialNumber_String,    "PHONEVR001");
    props->SetStringProperty(m_deviceId, vr::Prop_RenderModelName_String, "generic_hmd");
    props->SetFloatProperty (m_deviceId, vr::Prop_UserIpdMeters_Float,    0.063f);
    props->SetFloatProperty (m_deviceId, vr::Prop_DisplayFrequency_Float, 60.0f);
    props->SetFloatProperty (m_deviceId, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.011f);
    props->SetBoolProperty  (m_deviceId, vr::Prop_IsOnDesktop_Bool,       false);

    WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{}; addr.sin_family = AF_INET;
    addr.sin_port = htons(TRACKING_PORT); addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (sockaddr*)&addr, sizeof(addr));
    DWORD timeout = 100;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    m_socket  = (intptr_t)sock;
    m_running = true;
    m_udpThread = std::thread(&PhoneVRHMD::UDPReceiveThread, this);
    return vr::VRInitError_None;
}

void PhoneVRHMD::Deactivate() {
    m_running = false;
    if (m_socket != -1) { closesocket((SOCKET)m_socket); m_socket = -1; }
    if (m_udpThread.joinable()) m_udpThread.join();
    WSACleanup();
    m_deviceId = vr::k_unTrackedDeviceIndexInvalid;
}

void* PhoneVRHMD::GetComponent(const char* name) {
    if (!strcmp(name, vr::IVRDisplayComponent_Version))
        return static_cast<IVRDisplayComponent*>(this);
    return nullptr;
}

void PhoneVRHMD::UDPReceiveThread() {
    TrackingPacket pkt{};
    while (m_running) {
        int n = recv((SOCKET)m_socket, (char*)&pkt, sizeof(pkt), 0);
        if (n == sizeof(TrackingPacket) && pkt.magic == 0x56 && pkt.type == 0x01) {
            m_quat.w = pkt.qw; m_quat.x = pkt.qx;
            m_quat.y = pkt.qy; m_quat.z = pkt.qz;
            m_poseReady = true;
            if (m_leftCtrl)  m_leftCtrl->UpdateHeadPose(pkt.qw, pkt.qx, pkt.qy, pkt.qz);
            if (m_rightCtrl) m_rightCtrl->UpdateHeadPose(pkt.qw, pkt.qx, pkt.qy, pkt.qz);
        }
    }
}

vr::DriverPose_t PhoneVRHMD::GetPose() {
    vr::DriverPose_t pose{};
    pose.poseIsValid       = m_poseReady;
    pose.deviceIsConnected = true;
    pose.result = m_poseReady ? vr::TrackingResult_Running_OK : vr::TrackingResult_Uninitialized;
    pose.vecPosition[0] = 0; pose.vecPosition[1] = 1.7; pose.vecPosition[2] = 0;
    pose.qRotation.w = m_quat.w; pose.qRotation.x = m_quat.x;
    pose.qRotation.y = m_quat.y; pose.qRotation.z = m_quat.z;
    pose.qWorldFromDriverRotation.w = 1;
    pose.qDriverFromHeadRotation.w  = 1;
    return pose;
}

void PhoneVRHMD::GetWindowBounds(int32_t* x, int32_t* y, uint32_t* w, uint32_t* h)
    { *x=0; *y=0; *w=RENDER_WIDTH; *h=RENDER_HEIGHT; }
void PhoneVRHMD::GetRecommendedRenderTargetSize(uint32_t* w, uint32_t* h)
    { *w=RENDER_WIDTH; *h=RENDER_HEIGHT; }
void PhoneVRHMD::GetEyeOutputViewport(vr::EVREye eye,
    uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h)
    { *y=0; *w=EYE_WIDTH; *h=RENDER_HEIGHT; *x=(eye==vr::Eye_Left)?0:EYE_WIDTH; }
void PhoneVRHMD::GetProjectionRaw(vr::EVREye, float* l, float* r, float* t, float* b)
    { *l=-1.f; *r=1.f; *t=-1.f; *b=1.f; }
vr::DistortionCoordinates_t PhoneVRHMD::ComputeDistortion(vr::EVREye, float u, float v) {
    vr::DistortionCoordinates_t c{};
    c.rfRed[0]=u; c.rfRed[1]=v; c.rfGreen[0]=u; c.rfGreen[1]=v;
    c.rfBlue[0]=u; c.rfBlue[1]=v; return c;
}

// ─── Driver Provider ──────────────────────────────────────────────────────────

vr::EVRInitError PhoneVRDriverProvider::Init(vr::IVRDriverContext* ctx) {
    VR_INIT_SERVER_DRIVER_CONTEXT(ctx);

    // Read settings written by PC companion
    std::string phoneIp  = ReadSetting("phone_ip",  "127.0.0.1");
    bool        usbMode  = ReadSetting("usb_mode",  "1") == "1";
    int         bitrate  = std::stoi(ReadSetting("bitrate_mbps", "50"));
    int         width    = std::stoi(ReadSetting("width",  "2160"));
    int         height   = std::stoi(ReadSetting("height", "1200"));

    // Create and register HMD
    m_hmd = new PhoneVRHMD();
    vr::VRServerDriverHost()->TrackedDeviceAdded("PHONEVR001",
        vr::TrackedDeviceClass_HMD, m_hmd);

    // Create and register controllers
    m_leftCtrl  = new PhoneVRController(vr::TrackedControllerRole_LeftHand);
    m_rightCtrl = new PhoneVRController(vr::TrackedControllerRole_RightHand);
    vr::VRServerDriverHost()->TrackedDeviceAdded("PVRC001L",
        vr::TrackedDeviceClass_Controller, m_leftCtrl);
    vr::VRServerDriverHost()->TrackedDeviceAdded("PVRC001R",
        vr::TrackedDeviceClass_Controller, m_rightCtrl);

    m_hmd->SetControllers(m_leftCtrl, m_rightCtrl);
    m_ctrlReceiver.Start(m_leftCtrl, m_rightCtrl);

    // ── Start video streaming pipeline ───────────────────────────────────────
    VideoStreamer::Config vsCfg;
    vsCfg.phoneIp    = phoneIp;
    vsCfg.usbMode    = usbMode;
    vsCfg.encoder.width       = width;
    vsCfg.encoder.height      = height;
    vsCfg.encoder.fps         = 60;
    vsCfg.encoder.bitrateMbps = bitrate;
    vsCfg.encoder.lowLatency  = true;

    m_streamer = new VideoStreamer();
    if (!m_streamer->Start(vsCfg)) {
        delete m_streamer; m_streamer = nullptr;
        // Non-fatal: tracking + controllers still work, just no video
    }

    // Init frame capture (grabs SteamVR mirror texture each frame)
    if (m_streamer) {
        m_capture = new FrameCapture();
        if (!m_capture->Init(m_streamer, width, height)) {
            delete m_capture; m_capture = nullptr;
        }
    }

    return vr::VRInitError_None;
}

void PhoneVRDriverProvider::Cleanup() {
    m_ctrlReceiver.Stop();

    if (m_capture)   { m_capture->Shutdown(); delete m_capture; m_capture = nullptr; }
    if (m_streamer)  { m_streamer->Stop();    delete m_streamer; m_streamer = nullptr; }

    delete m_leftCtrl;  m_leftCtrl  = nullptr;
    delete m_rightCtrl; m_rightCtrl = nullptr;
    delete m_hmd;       m_hmd       = nullptr;

    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

void PhoneVRDriverProvider::RunFrame() {
    // Update HMD pose
    if (m_hmd) {
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
            0, m_hmd->GetPose(), sizeof(vr::DriverPose_t));
    }

    // Update controller poses + inputs
    if (m_leftCtrl)  m_leftCtrl->RunFrame();
    if (m_rightCtrl) m_rightCtrl->RunFrame();

    // Capture + encode + stream the current frame to the phone
    if (m_capture) {
        m_capture->PollMirrorTexture(vr::VRCompositor());
    }
}

// ─── DLL Entry Point ─────────────────────────────────────────────────────────

extern "C" __declspec(dllexport)
void* HmdDriverFactory(const char* pInterfaceName, int* pReturnCode) {
    static PhoneVRDriverProvider s_provider;
    if (!strcmp(pInterfaceName, vr::IServerTrackedDeviceProvider_Version))
        return &s_provider;
    if (pReturnCode) *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    return nullptr;
}
