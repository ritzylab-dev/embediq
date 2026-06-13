# D-LIB-3 EXCEPTION — 2026-06-12 (updated 2026-06-13)
# This CMakeLists.txt vendors Paho MQTT C v1.3.13 (Eclipse Distribution License v1.0).
# D-LIB-3 (CODING_RULES.md) states: "third_party/ source is never modified."
#
# TWO patches have been applied to the vendored Paho source as approved D-LIB-3 exceptions.
# See PAHO_PATCHES.md in this directory for full details.
#   Patch 1 (PR #160): src/MQTTProtocolOut.c — TCPSOCKET_INTERRUPTED → WAIT_FOR_CONNACK
#            on macOS loopback synchronous connect (EAGAIN on first writev).
#   Patch 2 (PR #161): src/Socket.c — EPROTOTYPE → TCPSOCKET_INTERRUPTED under __APPLE__
#            (macOS kernel quirk: EPROTOTYPE=41 on first writev after loopback connect).
#
# macOS SEMAPHORE BUILD FIX (PR #162) — not a D-LIB-3 source patch:
# Paho Thread.c/.h uses '#if defined(OSX)' to select macOS GCD dispatch semaphores
# (dispatch_semaphore_create). Without -DOSX, macOS compiles the Linux sem_init() path.
# On macOS, sem_init() returns ENOSYS — all four Paho semaphores (connect_sem,
# connack_sem, suback_sem, unsuback_sem) silently report rc=-1 at creation. The
# uninitialised sem_t causes sem_trywait() to return EINVAL, which the trywait loop
# treats as immediate success (rc=0). All semaphore waits become no-ops. Result:
# MQTTClient_connect() exits immediately with a NULL CONNACK pack → SOCKET_ERROR.
# Fix: $<$<PLATFORM_ID:Darwin>:OSX> added to target_compile_definitions (below).
# libdispatch is in libSystem.dylib — no explicit framework link is required.

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
    $<$<PLATFORM_ID:Darwin>:OSX>
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
