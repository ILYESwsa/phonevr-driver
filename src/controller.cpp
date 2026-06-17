#include "controller.h"
#include <cstring>
#include <cmath>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// ── PhoneVRController ─────────────────────────────────────────────────────────

PhoneVRController::PhoneVRController(vr::ETrackedControllerRole role)
    : m_role(role) {}

PhoneVRController::~PhoneVRController() {
    if (m_deviceId != vr::k_unTrackedDeviceIndexInvalid) Deactivate();
}

vr::EVRInitError PhoneVRController::Activate(uint32_t unObjectId) {
    m_deviceId = unObjectId;
    auto props = vr::VRProperties();
    bool isRight = (m_role == vr::TrackedControllerRole_RightHand);

    props->SetStringProperty(m_deviceId, vr::Prop_ModelNumber_String, isRight ? "PhoneVR-R" : "PhoneVR-L");
    props->SetStringProperty(m_deviceId, vr::Prop_SerialNumber_String, isRight ? "PVRC001R" : "PVRC001L");
    props->SetStringProperty(m_deviceId, vr::Prop_RenderModelName_String, isRight
        ? "{phonevr}/rendermodels/right_controller"
        : "{phonevr}/rendermodels/left_controller");
    props->SetInt32Property(m_deviceId, vr::Prop_ControllerHandSelectionPriority_Int32, 0);
    props->SetStringProperty(m_deviceId, vr::Prop_InputProfilePath_String,
        "{phonevr}/input/phonevr_controller_profile.json");

    // Register analog + button components
    auto driverInput = vr::VRDriverInput();

    driverInput->CreateScalarComponent(m_deviceId, "/input/trigger/value",
        &m_hTrigger, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    driverInput->CreateScalarComponent(m_deviceId, "/input/grip/value",
        &m_hGrip, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    driverInput->CreateScalarComponent(m_deviceId, "/input/joystick/x",
        &m_hThumbX, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    driverInput->CreateScalarComponent(m_deviceId, "/input/joystick/y",
        &m_hThumbY, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);

    driverInput->CreateBooleanComponent(m_deviceId,
        isRight ? "/input/a/click" : "/input/x/click", &m_hBtnAX);
    driverInput->CreateBooleanComponent(m_deviceId,
        isRight ? "/input/b/click" : "/input/y/click", &m_hBtnBY);
    driverInput->CreateBooleanComponent(m_deviceId, "/input/trigger/click", &m_hTriggerBtn);
    driverInput->CreateBooleanComponent(m_deviceId, "/input/grip/click",    &m_hGripBtn);

    return vr::VRInitError_None;
}

void PhoneVRController::Deactivate() {
    m_deviceId = vr::k_unTrackedDeviceIndexInvalid;
}

void PhoneVRController::UpdateState(const ControllerState& state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state = state;
}

void PhoneVRController::UpdateHeadPose(float qw, float qx, float qy, float qz) {
    m_hqw = qw; m_hqx = qx; m_hqy = qy; m_hqz = qz;
}

void PhoneVRController::RunFrame() {
    if (m_deviceId == vr::k_unTrackedDeviceIndexInvalid) return;

    ControllerState s;
    { std::lock_guard<std::mutex> lock(m_mutex); s = m_state; }

    auto inp = vr::VRDriverInput();

    // Analog axes
    inp->UpdateScalarComponent(m_hTrigger, s.trigger, 0.0);
    inp->UpdateScalarComponent(m_hGrip,    s.grip,    0.0);
    inp->UpdateScalarComponent(m_hThumbX,  s.thumbX,  0.0);
    inp->UpdateScalarComponent(m_hThumbY,  s.thumbY,  0.0);

    // Buttons
    inp->UpdateBooleanComponent(m_hBtnAX,      (s.buttons & CtrlBtn::A_OR_X)  != 0, 0.0);
    inp->UpdateBooleanComponent(m_hBtnBY,      (s.buttons & CtrlBtn::B_OR_Y)  != 0, 0.0);
    inp->UpdateBooleanComponent(m_hTriggerBtn, (s.buttons & CtrlBtn::TRIGGER)  != 0, 0.0);
    inp->UpdateBooleanComponent(m_hGripBtn,    (s.buttons & CtrlBtn::GRIP)     != 0, 0.0);

    // Update pose every frame
    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_deviceId, GetPose(), sizeof(vr::DriverPose_t));
}

vr::DriverPose_t PhoneVRController::GetPose() {
    vr::DriverPose_t pose{};
    pose.poseIsValid     = true;
    pose.deviceIsConnected = true;
    pose.result          = vr::TrackingResult_Running_OK;

    // ── Arm model ────────────────────────────────────────────────────────────
    // Rotate the fixed arm offset vector by the current head quaternion.
    // This makes the controller "follow" the head when you turn, like Gear VR.
    float ox, oy, oz;
    RotateVec(m_hqw, m_hqx, m_hqy, m_hqz,
              ArmOffsetX(), ArmOffsetY, ArmOffsetZ,
              ox, oy, oz);

    pose.vecPosition[0] = ox;
    pose.vecPosition[1] = 1.7f + oy; // eye height
    pose.vecPosition[2] = oz;

    // Controller points same direction as head (simple laser pointer model)
    pose.qRotation.w = m_hqw;
    pose.qRotation.x = m_hqx;
    pose.qRotation.y = m_hqy;
    pose.qRotation.z = m_hqz;

    pose.qWorldFromDriverRotation.w = 1.0;
    pose.qDriverFromHeadRotation.w  = 1.0;

    return pose;
}

// Rotate vector (vx,vy,vz) by quaternion (qw,qx,qy,qz)
void PhoneVRController::RotateVec(float qw, float qx, float qy, float qz,
                                   float vx, float vy, float vz,
                                   float& ox, float& oy, float& oz)
{
    // t = 2 * cross(q.xyz, v)
    float tx = 2.f * (qy * vz - qz * vy);
    float ty = 2.f * (qz * vx - qx * vz);
    float tz = 2.f * (qx * vy - qy * vx);
    // v + qw * t + cross(q.xyz, t)
    ox = vx + qw * tx + (qy * tz - qz * ty);
    oy = vy + qw * ty + (qz * tx - qx * tz);
    oz = vz + qw * tz + (qx * ty - qy * tx);
}

// ── ControllerReceiver ────────────────────────────────────────────────────────

void ControllerReceiver::Start(PhoneVRController* left, PhoneVRController* right) {
    m_left = left; m_right = right;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (sockaddr*)&addr, sizeof(addr));

    DWORD timeout = 100;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
    m_socket = (intptr_t)sock;

    m_running = true;
    m_thread  = std::thread(&ControllerReceiver::ReceiveThread, this);
}

void ControllerReceiver::Stop() {
    m_running = false;
    if (m_socket != -1) { closesocket((SOCKET)m_socket); m_socket = -1; }
    if (m_thread.joinable()) m_thread.join();
}

void ControllerReceiver::ReceiveThread() {
    ControllerPacket pkt{};
    while (m_running) {
        int n = recv((SOCKET)m_socket, (char*)&pkt, sizeof(pkt), 0);
        if (n != sizeof(ControllerPacket)) continue;
        if (pkt.magic != 0x56 || pkt.type != 0x03) continue;

        ControllerState state;
        state.buttons = pkt.buttons;
        state.trigger = pkt.trigger;
        state.grip    = pkt.grip;
        state.thumbX  = pkt.thumbX;
        state.thumbY  = pkt.thumbY;

        if (pkt.hand == 0 && m_left)  m_left->UpdateState(state);
        if (pkt.hand == 1 && m_right) m_right->UpdateState(state);
    }
}
