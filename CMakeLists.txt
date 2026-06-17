cmake_minimum_required(VERSION 3.20)
project(phonevr_driver VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# ── OpenVR SDK ────────────────────────────────────────────────────────────────
set(OPENVR_SDK "${CMAKE_SOURCE_DIR}/openvr-sdk" CACHE PATH "Path to OpenVR SDK")
add_library(openvr_api STATIC IMPORTED)
set_target_properties(openvr_api PROPERTIES
    IMPORTED_LOCATION "${OPENVR_SDK}/lib/win64/openvr_api.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENVR_SDK}/headers"
)

# ── x264 (via vcpkg: vcpkg install x264:x64-windows) ─────────────────────────
find_library(X264_LIB NAMES libx264 x264 HINTS "${CMAKE_SOURCE_DIR}/libs/x264")
if(NOT X264_LIB)
    message(STATUS "x264 not found as static lib — will load dynamically at runtime")
endif()

# ── Driver DLL ────────────────────────────────────────────────────────────────
add_library(phonevr_driver SHARED
    src/driver_full.cpp
    src/controller.cpp
    src/encoder.cpp
    src/streamer.cpp
    src/capture.cpp
)

target_include_directories(phonevr_driver PRIVATE
    include/
    ${OPENVR_SDK}/headers
)

target_link_libraries(phonevr_driver PRIVATE
    openvr_api
    ws2_32
    d3d11
    dxgi
)

if(X264_LIB)
    target_link_libraries(phonevr_driver PRIVATE ${X264_LIB})
endif()

set_target_properties(phonevr_driver PROPERTIES
    OUTPUT_NAME "driver_phonevr"
    PREFIX ""
)

# ── Install layout ────────────────────────────────────────────────────────────
install(TARGETS phonevr_driver RUNTIME DESTINATION "phonevr/bin/win64")
install(FILES driver_manifest.json DESTINATION "phonevr")
install(DIRECTORY resources/ DESTINATION "phonevr/resources")
