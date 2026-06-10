# third_party/paho.mqtt.c/CMakeLists.txt — EmbedIQ vendored Paho MQTT C v1.3.13
#
# Builds the synchronous MQTTClient API as a static library: embediq_paho_mqtt.
# The async API (MQTTAsync) is NOT built — hal_mqtt_posix.c uses MQTTClient only.
# FBs never link embediq_paho_mqtt directly (D-LIB-4).
#
# D-LIB-3: source in third_party/, never modified.
# License: EPL-2.0 / EDL-1.0 (BSD-like — permissive for use, Apache-2.0 compatible).

cmake_minimum_required(VERSION 3.16)

# Synchronous MQTTClient source files only (no Async, no samples)
set(PAHO_SYNC_SOURCES
    src/MQTTClient.c
    src/MQTTTime.c
    src/MQTTProtocolClient.c
    src/MQTTProtocolOut.c
    src/Clients.c
    src/Heap.c
    src/LinkedList.c
    src/Log.c
    src/Messages.c
    src/MutexType.c
    src/Net.c
    src/OsWrapper.c
    src/SHA1.c
    src/Socket.c
    src/SocketBuffer.c
    src/Thread.c
    src/utf-8.c
)

# Filter to only existing files (some versions may not have all files)
set(PAHO_EXISTING_SOURCES "")
foreach(SRC ${PAHO_SYNC_SOURCES})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}")
        list(APPEND PAHO_EXISTING_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}")
    endif()
endforeach()

add_library(embediq_paho_mqtt STATIC ${PAHO_EXISTING_SOURCES})

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
