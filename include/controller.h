#pragma once
#include <openvr_driver.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <winsock2.h>

/**
 * PhoneVR Virtual Controller
 *
 * Emulates a Meta Quest-style 6DoF controller in SteamVR.
 * Receives input from the Android touchscreen over UDP port 6002.
 *
 * Since we have no physical 6DoF tracking for the controllers,
 * we use a "laser pointer" model: the controller is positioned
 * in front of the HMD and rotates with the head, offset by a
 * fixed arm model offset (like a Gear VR controller).
 */

// ── Packet definition (must match ControllerInputSender.kt) ─────────────────
#pragma pack(push, 1)
struct ControllerPacket {
    uint8_t  magic;       // 0x56
    uint8_t  type;        // 0x03
    uint8_t  hand;        // 0=left, 1=right
    uint8_t  buttons;     // bitmask
    float    trigger;
    float    grip;
    float    thumbX;
    float    thumbY;
    float    posX;        // placeholder (0)
    float    posY;        // placeholder (0)
    uint32_t timestamp;
};
#pragma pack(pop)

// Button bitmask flags (match Kotlin side)
namespace CtrlBtn {
    constexpr uint8_t TRIGGER    = 0x01;
    constexpr uint8_t GRIP       = 0x02;
    constexpr uint8_t THUMBSTICK = 0x04;
    constexpr uint8_t A_OR_X     = 0x08;
    constexpr uint8_t B_OR_Y     = 0x10;
    constexpr uint8_t MENU       = 0x20;
}

// ── Controller state ─────────────────────────────────────────────────────────
struct ControllerState {
    uint8_t buttons = 0;
    float   trigger = 0.f;
    float   grip    = 0.f;
    float   thumbX  = 0.f;
    float   thumbY  = 0.f;
};

// ── PhoneVRController ─────────────────────────────────────────────────────────
class PhoneVRController : public vr::ITrackedDeviceServerDriver {
public:
    explicit PhoneVRController(vr::ETrackedControllerRole role);
    ~PhoneVRController();

    // ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void Deactivate() override;
    void EnterStandby() override {}
    void* GetComponent(const char*) override { return nullptr; }
    void DebugRequest(const char*, char*, uint32_t) override {}
    vr::DriverPose_t GetPose() override;

    void UpdateState(const ControllerState& state);
    void UpdateHeadPose(float qw, float qx, float qy, float qz);
    void RunFrame();

private:
    vr::ETrackedControllerRole m_role;
    uint32_t m_deviceId = vr::k_unTrackedDeviceIndexInvalid;

    std::mutex  m_mutex;
    ControllerState m_state;

    // Head quaternion (for arm model)
    float m_hqw = 1.f, m_hqx = 0.f, m_hqy = 0.f, m_hqz = 0.f;

    // SteamVR input handles
    vr::VRInputComponentHandle_t m_hTrigger    = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_hGrip       = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_hThumbX     = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_hThumbY     = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_hBtnAX      = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_hBtnBY      = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_hTriggerBtn = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_hGripBtn    = vr::k_ulInvalidInputComponentHandle;

    // Arm model: controller offset relative to HMD
    // Right: +0.2m right, -0.15m down, -0.35m forward
    // Left:  -0.2m right, -0.15m down, -0.35m forward
    float ArmOffsetX() const { return (m_role == vr::TrackedControllerRole_RightHand) ? 0.20f : -0.20f; }
    static constexpr float ArmOffsetY = -0.15f;
    static constexpr float ArmOffsetZ = -0.35f;

    // Rotate a vector by a quaternion
    static void RotateVec(float qw, float qx, float qy, float qz,
                          float vx, float vy, float vz,
                          float& ox, float& oy, float& oz);
};

// ── UDP receiver for both controllers ────────────────────────────────────────
class ControllerReceiver {
public:
    void Start(PhoneVRController* left, PhoneVRController* right);
    void Stop();

private:
    void ReceiveThread();
    std::thread    m_thread;
    std::atomic<bool> m_running{ false };
    intptr_t m_socket = -1;
    PhoneVRController* m_left  = nullptr;
    PhoneVRController* m_right = nullptr;
    static constexpr uint16_t PORT = 6002;
};
