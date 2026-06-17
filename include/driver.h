#pragma once
#include <openvr_driver.h>
#include <thread>
#include <atomic>
#include <string>
#include "controller.h"
#include "streamer.h"
#include "capture.h"

#pragma pack(push, 1)
struct TrackingPacket {
    uint8_t  magic;
    uint8_t  type;
    uint16_t seq;
    float    qw, qx, qy, qz;
};
#pragma pack(pop)

class PhoneVRHMD : public vr::ITrackedDeviceServerDriver,
                   public vr::IVRDisplayComponent
{
public:
    PhoneVRHMD();
    virtual ~PhoneVRHMD();

    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void Deactivate() override;
    void EnterStandby() override {}
    void* GetComponent(const char* name) override;
    void DebugRequest(const char*, char*, uint32_t) override {}
    vr::DriverPose_t GetPose() override;

    void GetWindowBounds(int32_t* x, int32_t* y, uint32_t* w, uint32_t* h) override;
    bool IsDisplayOnDesktop()   override { return false; }
    bool IsDisplayRealDisplay() override { return false; }
    void GetRecommendedRenderTargetSize(uint32_t* w, uint32_t* h) override;
    void GetEyeOutputViewport(vr::EVREye eye, uint32_t* x, uint32_t* y,
                              uint32_t* w, uint32_t* h) override;
    void GetProjectionRaw(vr::EVREye, float* l, float* r,
                          float* t, float* b) override;
    vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye, float u, float v) override;

    void SetControllers(PhoneVRController* left, PhoneVRController* right) {
        m_leftCtrl = left; m_rightCtrl = right;
    }

private:
    void UDPReceiveThread();

    uint32_t           m_deviceId   = vr::k_unTrackedDeviceIndexInvalid;
    std::atomic<bool>  m_running    { false };
    std::thread        m_udpThread;
    struct { float w=1,x=0,y=0,z=0; } m_quat;
    std::atomic<bool>  m_poseReady  { false };
    PhoneVRController* m_leftCtrl   = nullptr;
    PhoneVRController* m_rightCtrl  = nullptr;
    intptr_t           m_socket     = -1;

    static constexpr uint32_t RENDER_WIDTH  = 2160;
    static constexpr uint32_t RENDER_HEIGHT = 1200;
    static constexpr uint32_t EYE_WIDTH     = RENDER_WIDTH / 2;
    static constexpr uint16_t TRACKING_PORT = 6000;
};

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
    PhoneVRHMD*        m_hmd         = nullptr;
    PhoneVRController* m_leftCtrl    = nullptr;
    PhoneVRController* m_rightCtrl   = nullptr;
    ControllerReceiver m_ctrlReceiver;
    VideoStreamer*     m_streamer     = nullptr;
    FrameCapture*      m_capture      = nullptr;
};
