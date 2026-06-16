#include "driver.h"
#include <openvr_driver.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <cmath>

#pragma comment(lib, "ws2_32.lib")

// ─── PhoneVRHMD ──────────────────────────────────────────────────────────────

PhoneVRHMD::PhoneVRHMD() {}

PhoneVRHMD::~PhoneVRHMD() {
    if (m_running) Deactivate();
}

vr::EVRInitError PhoneVRHMD::Activate(uint32_t unObjectId) {
    m_deviceId = unObjectId;

    auto props = vr::VRProperties();

    // Report ourselves as a generic HMD
    props->SetStringProperty(m_deviceId, vr::Prop_ModelNumber_String, "PhoneVR");
    props->SetStringProperty(m_deviceId, vr::Prop_SerialNumber_String, "PHONEVR001");
    props->SetStringProperty(m_deviceId, vr::Prop_RenderModelName_String, "generic_hmd");
    props->SetFloatProperty(m_deviceId, vr::Prop_UserIpdMeters_Float, 0.063f);
    props->SetFloatProperty(m_deviceId, vr::Prop_UserHeadToEyeDepthMeters_Float, 0.0f);
    props->SetFloatProperty(m_deviceId, vr::Prop_DisplayFrequency_Float, 60.0f);
    props->SetFloatProperty(m_deviceId, vr::Prop_SecondsFromVsyncToPhotons_Float, 0.011f);
    props->SetBoolProperty(m_deviceId, vr::Prop_IsOnDesktop_Bool, false);

    // Start UDP receiver thread
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TRACKING_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (sockaddr*)&addr, sizeof(addr));

    // Set receive timeout so thread can exit cleanly
    DWORD timeout = 100; // ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    m_socket = (intptr_t)sock;
    m_running = true;
    m_udpThread = std::thread(&PhoneVRHMD::UDPReceiveThread, this);

    return vr::VRInitError_None;
}

void PhoneVRHMD::Deactivate() {
    m_running = false;
    if (m_socket != -1) {
        closesocket((SOCKET)m_socket);
        m_socket = -1;
    }
    if (m_udpThread.joinable()) m_udpThread.join();
    WSACleanup();
    m_deviceId = vr::k_unTrackedDeviceIndexInvalid;
}

void* PhoneVRHMD::GetComponent(const char* pchComponentNameAndVersion) {
    if (!strcmp(pchComponentNameAndVersion, vr::IVRDisplayComponent_Version))
        return static_cast<IVRDisplayComponent*>(this);
    return nullptr;
}

void PhoneVRHMD::UDPReceiveThread() {
    TrackingPacket pkt{};
    while (m_running) {
        int n = recv((SOCKET)m_socket, (char*)&pkt, sizeof(pkt), 0);
        if (n == sizeof(TrackingPacket) && pkt.magic == 0x56 && pkt.type == 0x01) {
            m_quat.w = pkt.qw;
            m_quat.x = pkt.qx;
            m_quat.y = pkt.qy;
            m_quat.z = pkt.qz;
            m_poseReady = true;
        }
    }
}

vr::DriverPose_t PhoneVRHMD::GetPose() {
    vr::DriverPose_t pose{};
    pose.poseIsValid = m_poseReady;
    pose.result = m_poseReady
        ? vr::TrackingResult_Running_OK
        : vr::TrackingResult_Uninitialized;
    pose.deviceIsConnected = true;

    // Identity position (3DoF — no positional tracking)
    pose.vecPosition[0] = 0;
    pose.vecPosition[1] = 1.7; // eye height in meters
    pose.vecPosition[2] = 0;

    // Quaternion from phone
    pose.qRotation.w = m_quat.w;
    pose.qRotation.x = m_quat.x;
    pose.qRotation.y = m_quat.y;
    pose.qRotation.z = m_quat.z;

    // World from driver transform (identity)
    pose.qWorldFromDriverRotation.w = 1;
    pose.qDriverFromHeadRotation.w  = 1;

    return pose;
}

// ─── Display Component ────────────────────────────────────────────────────────

void PhoneVRHMD::GetWindowBounds(int32_t* x, int32_t* y, uint32_t* w, uint32_t* h) {
    *x = 0; *y = 0;
    *w = RENDER_WIDTH; *h = RENDER_HEIGHT;
}

void PhoneVRHMD::GetRecommendedRenderTargetSize(uint32_t* w, uint32_t* h) {
    *w = RENDER_WIDTH; *h = RENDER_HEIGHT;
}

void PhoneVRHMD::GetEyeOutputViewport(vr::EVREye eye,
    uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h)
{
    *y = 0; *w = EYE_WIDTH; *h = RENDER_HEIGHT;
    *x = (eye == vr::Eye_Left) ? 0 : EYE_WIDTH;
}

void PhoneVRHMD::GetProjectionRaw(vr::EVREye, float* left, float* right,
                                    float* top, float* bottom)
{
    // ~90° FoV (matching typical cardboard/Quest-like lenses)
    *left   = -1.0f;
    *right  =  1.0f;
    *top    = -1.0f;
    *bottom =  1.0f;
}

vr::DistortionCoordinates_t PhoneVRHMD::ComputeDistortion(vr::EVREye, float u, float v) {
    // No distortion on the PC side — the Android app does it via GLSL
    vr::DistortionCoordinates_t coords{};
    coords.rfRed[0]   = u; coords.rfRed[1]   = v;
    coords.rfGreen[0] = u; coords.rfGreen[1] = v;
    coords.rfBlue[0]  = u; coords.rfBlue[1]  = v;
    return coords;
}

// ─── Driver Provider ─────────────────────────────────────────────────────────

vr::EVRInitError PhoneVRDriverProvider::Init(vr::IVRDriverContext* ctx) {
    VR_INIT_SERVER_DRIVER_CONTEXT(ctx);
    m_hmd = new PhoneVRHMD();
    vr::VRServerDriverHost()->TrackedDeviceAdded("PHONEVR001",
        vr::TrackedDeviceClass_HMD, m_hmd);
    return vr::VRInitError_None;
}

void PhoneVRDriverProvider::Cleanup() {
    delete m_hmd;
    m_hmd = nullptr;
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

void PhoneVRDriverProvider::RunFrame() {
    if (m_hmd) {
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
            0, m_hmd->GetPose(), sizeof(vr::DriverPose_t));
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
