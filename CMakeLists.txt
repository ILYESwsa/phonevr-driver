cmake_minimum_required(VERSION 3.20)
project(phonevr_driver VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# ── OpenVR SDK ────────────────────────────────────────────────────────────────
# Download from: https://github.com/ValveSoftware/openvr
# Set OPENVR_SDK to the folder containing "headers/" and "lib/"
set(OPENVR_SDK "${CMAKE_SOURCE_DIR}/openvr-sdk" CACHE PATH "Path to OpenVR SDK")

add_library(openvr_api STATIC IMPORTED)
set_target_properties(openvr_api PROPERTIES
    IMPORTED_LOCATION "${OPENVR_SDK}/lib/win64/openvr_api.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENVR_SDK}/headers"
)

# ── Driver DLL ────────────────────────────────────────────────────────────────
add_library(phonevr_driver SHARED
    src/driver.cpp
)

target_include_directories(phonevr_driver PRIVATE
    include/
    ${OPENVR_SDK}/headers
)

target_link_libraries(phonevr_driver PRIVATE
    openvr_api
    ws2_32      # Winsock
)

# Output name matches what SteamVR expects: driver_phonevr.dll
set_target_properties(phonevr_driver PROPERTIES
    OUTPUT_NAME "driver_phonevr"
    PREFIX ""
)

# ── Install layout (SteamVR driver folder structure) ─────────────────────────
install(TARGETS phonevr_driver
    RUNTIME DESTINATION "phonevr/bin/win64"
    LIBRARY DESTINATION "phonevr/bin/win64"
)
install(FILES driver_manifest.json DESTINATION "phonevr")
