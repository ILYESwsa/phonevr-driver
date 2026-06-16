#pragma once
#include <openvr_driver.h>
#include <thread>
#include <atomic>
#include <string>

// UDP packet structure matching android-client/HeadTrackingSender.kt
#pragma pack(push, 1)
struct TrackingPacket {
    uint8_t  magic;     // 0x56
    uint8_t  type;      // 0x01 = head tracking
    uint16_t seq;
    float    qw;
    float    qx;
    float    qy;
    float    qz;
};
#pragma pack(pop)

/**
 * PhoneVR HMD Device Driver
 *
 * Registers as a fake HMD in SteamVR via the OpenVR driver API.
 * Receives head tracking quaternions from the Android app over UDP port 6000.
 * Sends H.264 encoded frames to Android over UDP port 6001.
 *
 * SteamVR sees this as a real HMD — games render to it normally.
 */
class PhoneVRHMD : public vr::ITrackedDeviceServerDriver,
                   public vr::IVRDisplayComponent
{
public:
    PhoneVRHMD();
    virtual ~PhoneVRHMD();

    // ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void Deactivate() override;
    void EnterStandby() override {}
    void* GetComponent(const char* pchComponentNameAndVersion) override;
    void DebugRequest(const char*, char*, uint32_t) override {}
    vr::DriverPose_t GetPose() override;

    // IVRDisplayComponent
    void GetWindowBounds(int32_t* pnX, int32_t* pnY, uint32_t* pnWidth, uint32_t* pnHeight) override;
    bool IsDisplayOnDesktop() override { return false; }
    bool IsDisplayRealDisplay() override { return false; }
    void GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight) override;
    void GetEyeOutputViewport(vr::EVREye eEye, uint32_t* pnX, uint32_t* pnY,
                              uint32_t* pnWidth, uint32_t* pnHeight) override;
    void GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight,
                          float* pfTop, float* pfBottom) override;
    vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye eEye, float fU, float fV) override;

    // Called by driver provider on each frame
    void RunFrame();

private:
    void UDPReceiveThread();

    uint32_t m_deviceId = vr::k_unTrackedDeviceIndexInvalid;
    std::atomic<bool> m_running{ false };
    std::thread m_udpThread;

    // Current head pose (updated by UDP thread)
    struct { float w=1, x=0, y=0, z=0; } m_quat;
    std::atomic<bool> m_poseReady{ false };

    // Display config
    static constexpr uint32_t RENDER_WIDTH  = 2160;
    static constexpr uint32_t RENDER_HEIGHT = 1200;
    static constexpr uint32_t EYE_WIDTH     = RENDER_WIDTH / 2;

    // UDP socket (Windows)
    intptr_t m_socket = -1; // SOCKET is uintptr_t on Windows
    static constexpr uint16_t TRACKING_PORT = 6000;
};

/**
 * Driver Provider — entry point SteamVR calls to get the driver.
 */
class PhoneVRDriverProvider : public vr::IServerTrackedDeviceProvider
{
public:
    vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
    void Cleanup() override;
    const char* const* GetInterfaceVersions() override { return vr::k_InterfaceVersions; }
    void RunFrame() override;
    bool ShouldBlockStandbyMode() override { return false; }
    void EnterStandby() override {}
    void LeaveStandby() override {}

private:
    PhoneVRHMD* m_hmd = nullptr;
};
