# examples/env_monitor/CMakeLists.txt — Simulated Environmental Monitor
#
# Connects to EmbedIQ Cloud OSS and publishes simulated sensor telemetry.
# Demonstrates full device→cloud integration using fb_cloud_mqtt.
#
# Prerequisites: Mosquitto running on localhost:1883
#                Cloud OSS running on localhost:8080
#
# Run:
#   EMBEDIQ_NVM_PATH=/tmp/env_monitor.bin ./build/examples/env_monitor/env_monitor

add_executable(env_monitor
    main.c
    fb_env_sim.c
)

set_target_properties(env_monitor PROPERTIES
    OUTPUT_NAME env_monitor
)

target_include_directories(env_monitor PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/generated
    ${CMAKE_SOURCE_DIR}/examples/env_monitor
    ${CMAKE_SOURCE_DIR}/fbs/services
    ${CMAKE_SOURCE_DIR}/hal/posix/ops
)

target_link_libraries(env_monitor PRIVATE
    embediq_driver_fbs
    embediq_service_fbs
    embediq_hal_posix
    embediq_cfg
    m
)

target_compile_options(env_monitor PRIVATE
    -Wall
    -Wextra
    -Werror
)
