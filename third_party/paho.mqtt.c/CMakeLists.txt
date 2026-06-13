# D-LIB-3 EXCEPTION — 2026-06-12
# This CMakeLists.txt vendors Paho MQTT C v1.3.13 (Eclipse Distribution License v1.0).
# D-LIB-3 (CODING_RULES.md) states: "third_party/ source is never modified."
#
# ONE patch has been applied to the vendored source as an approved D-LIB-3 exception:
#   File:   src/MQTTProtocolOut.c (lines ~344-351)
#   Reason: macOS-specific bug — connect() returns 0 synchronously for non-blocking
#           loopback socket, but first writev() returns EAGAIN. Paho treats
#           TCPSOCKET_INTERRUPTED (-22) as a fatal send error and discards the pending
#           write, preventing MQTT CONNECT from reaching the broker.
#   Fix:    TCPSOCKET_INTERRUPTED in the sync-connect path sets WAIT_FOR_CONNACK
#           (not NOT_IN_PROGRESS). Dispatch thread flushes pending write normally.
#   Doc:    See PAHO_PATCHES.md in this directory for full change description.
#   Upstream: Not yet reported. Fix is applicable to Paho 1.3.13.

# third_party/paho.mqtt.c/CMakeLists.txt — EmbedIQ vendored Paho MQTT C v1.3.13
#
# Builds the synchronous MQTTClient API as a static library: embediq_paho_mqtt.
# The async API (MQTTAsync) is NOT built — hal_mqtt_posix.c uses MQTTClient only.
# FBs never link embediq_paho_mqtt directly (D-LIB-4).
#
# D-LIB-3: source in third_party/, never modified.
# License: EPL-2.0 / EDL-1.0 (BSD-like — permissive for use, Apache-2.0 compatible).

cmake_minimum_required(VERSION 3.16)

# Build all Paho src/*.c — explicit enumeration silently misses deps
# (MQTTPacket, Base64, Persistence, etc.) that only surface when the
# full hal_mqtt_posix link chain is exercised. Glob is more robust
# against version bumps; unused .o files stay in the .a, zero cost.
file(GLOB PAHO_SYNC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_library(embediq_paho_mqtt STATIC ${PAHO_SYNC_SOURCES})

# Generate VersionInfo.h — Paho's sources include it; upstream generates it
# via configure_file. Reproduce that here with known version values.
string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
set(CLIENT_VERSION "1.3.13")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VersionInfo.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/VersionInfo.h
    @ONLY
)

target_include_directories(embediq_paho_mqtt PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_include_directories(embediq_paho_mqtt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Do NOT define OPENSSL or MBEDTLS — Paho's headers use #if defined(X),
# so defining them (even to 0) enables the TLS code paths.
# EmbedIQ routes TLS through embediq_tls_ops_t, not through Paho.
target_compile_definitions(embediq_paho_mqtt PUBLIC
    NO_TLS_GATEWAY=1
)

# PTHREAD_MUTEX_ERRORCHECK requires _GNU_SOURCE on Linux GCC.
# Paho's upstream CMakeLists sets this — reproduce it here.
target_compile_definitions(embediq_paho_mqtt PRIVATE
    $<$<PLATFORM_ID:Linux>:_GNU_SOURCE>
)

# Suppress vendored source warnings (D-LIB-3: we do not own or modify this source)
target_compile_options(embediq_paho_mqtt PRIVATE
    -Wno-unused-parameter
    -Wno-sign-conversion
    -Wno-conversion
    -Wno-pedantic
    -Wno-implicit-function-declaration
    -Wno-deprecated-declarations
)
