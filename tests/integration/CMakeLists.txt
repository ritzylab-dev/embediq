# tests/integration/CMakeLists.txt — integration test binaries

# ---------------------------------------------------------------------------
# Thermostat observable integration test (P1-T5)
# ---------------------------------------------------------------------------

add_executable(test_thermostat_observable test_thermostat_observable.c)

target_include_directories(test_thermostat_observable PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_thermostat_observable PRIVATE embediq_fb_engine)

target_compile_options(test_thermostat_observable PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME integration_thermostat_observable COMMAND test_thermostat_observable)

# ---------------------------------------------------------------------------
# Thermostat full integration test (P1-T8)
#
# Boots all 5 FBs on real POSIX threads and runs for ~15 seconds.
# CTest timeout is set to 60 seconds.
# ---------------------------------------------------------------------------

add_executable(test_thermostat_full
    test_thermostat_full.c
    ${CMAKE_SOURCE_DIR}/examples/thermostat/fb_temp_sensor.c
    ${CMAKE_SOURCE_DIR}/examples/thermostat/fb_temp_controller.c
)

target_include_directories(test_thermostat_full PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/examples/thermostat
)

target_link_libraries(test_thermostat_full PRIVATE
    embediq_driver_fbs
)

target_compile_options(test_thermostat_full PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME integration_thermostat_full COMMAND test_thermostat_full)
set_tests_properties(integration_thermostat_full PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# Thermostat .iqtrace capture integration test (Obs-5 E2E)
# ---------------------------------------------------------------------------

add_executable(test_thermostat_capture
    test_thermostat_capture.c
    ${CMAKE_SOURCE_DIR}/examples/thermostat/fb_temp_sensor.c
    ${CMAKE_SOURCE_DIR}/examples/thermostat/fb_temp_controller.c
)

target_include_directories(test_thermostat_capture PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/examples/thermostat
)

target_link_libraries(test_thermostat_capture PRIVATE
    embediq_driver_fbs
)

target_compile_definitions(test_thermostat_capture PRIVATE
    EMBEDIQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_compile_options(test_thermostat_capture PRIVATE
    -Wall -Wextra -Werror
)

add_test(NAME integration_thermostat_capture COMMAND test_thermostat_capture)
set_tests_properties(integration_thermostat_capture PROPERTIES TIMEOUT 30)

# ---------------------------------------------------------------------------
# Gateway full integration test
# ---------------------------------------------------------------------------

add_executable(test_gateway_full
    test_gateway_full.c
    ${CMAKE_SOURCE_DIR}/examples/gateway/fb_field_receiver.c
    ${CMAKE_SOURCE_DIR}/examples/gateway/fb_policy_engine.c
    ${CMAKE_SOURCE_DIR}/examples/gateway/fb_north_bridge.c
)

target_include_directories(test_gateway_full PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/examples/gateway
)

target_link_libraries(test_gateway_full PRIVATE
    embediq_driver_fbs
)

target_compile_options(test_gateway_full PRIVATE
    -Wall -Wextra -Werror
)

add_test(NAME integration_gateway_full COMMAND test_gateway_full)
set_tests_properties(integration_gateway_full PROPERTIES TIMEOUT 60)

# ---------------------------------------------------------------------------
# Bridge socket integration test (Item 5c)
#
# Boots fb_bridge with EMBEDIQ_BRIDGE_TRANSPORT_SOCKET, fork/exec
# bridge_client.py to connect via Unix socket, confirms MSG_BRIDGE_EXTFB_CONNECTED
# and a test message arrive on the internal bus. Timeout 30 seconds.
# ---------------------------------------------------------------------------

add_executable(test_bridge_socket
    test_bridge_socket.c
)

target_include_directories(test_bridge_socket PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_bridge_socket PRIVATE
    embediq_bridge_socket
    embediq_driver_fbs
)

target_compile_definitions(test_bridge_socket PRIVATE
    EMBEDIQ_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_compile_options(test_bridge_socket PRIVATE
    -Wall
    -Wextra
    -Werror
)

# integration_bridge_socket is Linux-only at runtime: the design (Topic 1)
# uses AF_UNIX + SOCK_SEQPACKET for message-boundary preservation, which
# macOS/Darwin does not implement for Unix domain sockets. The binary still
# compiles on macOS (development convenience); ctest skips registering it
# so `ctest` is green on macOS hosts. CI runs on Ubuntu and exercises it.
if(NOT APPLE)
    add_test(NAME integration_bridge_socket COMMAND test_bridge_socket)
    set_tests_properties(integration_bridge_socket PROPERTIES TIMEOUT 30)
endif()
