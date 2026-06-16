# pc-driver

C++ OpenVR driver that makes SteamVR see your phone as a VR headset.

## What It Does
- Registers as a fake HMD (`TrackedDeviceClass_HMD`) in SteamVR
- Listens on UDP port 6000 for head tracking quaternions from the phone
- Reports a 2160×1200 render target (split to 1080×1200 per eye)
- 90° FoV projection matrix (Quest-like)
- No distortion on PC side — Android handles it via GLSL shader

## Build
```bash
# Requires CMake + Visual Studio 2022 + OpenVR SDK
cmake -B build -S . -A x64
cmake --build build --config Release
```
Or use GitHub Actions — the DLL is built automatically on push.

## Install Driver
Copy the `phonevr/` folder to:
```
C:\Program Files (x86)\Steam\steamapps\common\SteamVR\drivers\
```
Or let the PC companion do it automatically.

## Packet Format (UDP port 6000)
```
Byte  0     : magic = 0x56
Byte  1     : type  = 0x01 (head tracking)
Bytes 2-3   : uint16 sequence number
Bytes 4-7   : float  qw
Bytes 8-11  : float  qx
Bytes 12-15 : float  qy
Bytes 16-19 : float  qz
```
Total: 20 bytes per packet at ~250Hz.

## Extending
- Add controller emulation: implement `ITrackedDeviceServerDriver` for left/right controllers
- Add 6DoF position prediction: apply velocity integration to `vecPosition[]` in `GetPose()`
- Add H.264 encoding: use NVENC/AMF in a separate thread, send to port 6001
