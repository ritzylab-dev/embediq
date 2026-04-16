# tests/unit/CMakeLists.txt — unit test binaries

# ---------------------------------------------------------------------------
# OSAL POSIX unit tests (P1-T1)
# ---------------------------------------------------------------------------

add_executable(test_osal_posix test_osal_posix.c)

target_include_directories(test_osal_posix PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_osal_posix PRIVATE embediq_osal_posix)

target_compile_options(test_osal_posix PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_osal_posix COMMAND test_osal_posix)

# ---------------------------------------------------------------------------
# FB engine unit tests (P1-T2)
# ---------------------------------------------------------------------------

add_executable(test_fb_engine test_fb_engine.c)

target_include_directories(test_fb_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_fb_engine PRIVATE embediq_fb_engine)

target_compile_options(test_fb_engine PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_fb_engine COMMAND test_fb_engine)

# ---------------------------------------------------------------------------
# Message bus unit tests (P1-T3)
# ---------------------------------------------------------------------------

add_executable(test_message_bus test_message_bus.c)

target_include_directories(test_message_bus PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_message_bus PRIVATE embediq_fb_engine)

target_compile_options(test_message_bus PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_message_bus COMMAND test_message_bus)

# ---------------------------------------------------------------------------
# FSM engine unit tests (P1-T4)
# ---------------------------------------------------------------------------

add_executable(test_fsm_engine test_fsm_engine.c)

target_include_directories(test_fsm_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_fsm_engine PRIVATE embediq_fb_engine)

target_compile_options(test_fsm_engine PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_fsm_engine COMMAND test_fsm_engine)

# ---------------------------------------------------------------------------
# Observatory unit tests (P1-T5)
# ---------------------------------------------------------------------------

add_executable(test_observatory test_observatory.c)

target_include_directories(test_observatory PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_observatory PRIVATE embediq_fb_engine)

target_compile_options(test_observatory PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_observatory COMMAND test_observatory)

# ---------------------------------------------------------------------------
# Watchdog FB unit tests (P1-T6 / Arch-4c)
# ---------------------------------------------------------------------------

add_executable(test_fb_watchdog test_fb_watchdog.c)

target_include_directories(test_fb_watchdog PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/core/include/hal
    ${CMAKE_SOURCE_DIR}/fbs/drivers
)

target_link_libraries(test_fb_watchdog PRIVATE
    embediq_driver_fbs
)

target_compile_options(test_fb_watchdog PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_fb_watchdog COMMAND test_fb_watchdog)

# ---------------------------------------------------------------------------
# Timer FB unit tests (P1-T6)
# ---------------------------------------------------------------------------

add_executable(test_fb_timer test_fb_timer.c)

target_include_directories(test_fb_timer PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/core/include/hal
    ${CMAKE_SOURCE_DIR}/fbs/drivers
)

target_link_libraries(test_fb_timer PRIVATE
    embediq_driver_fbs
)

target_compile_options(test_fb_timer PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_fb_timer COMMAND test_fb_timer)

# ---------------------------------------------------------------------------
# Blocking dispatch unit tests (P2-T0)
# ---------------------------------------------------------------------------

add_executable(test_blocking_dispatch test_blocking_dispatch.c)

target_include_directories(test_blocking_dispatch PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_blocking_dispatch PRIVATE embediq_fb_engine)

target_compile_options(test_blocking_dispatch PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_blocking_dispatch COMMAND test_blocking_dispatch)

# ---------------------------------------------------------------------------
# Dispatch shutdown unit tests (P2-T0b)
# ---------------------------------------------------------------------------

add_executable(test_dispatch_shutdown test_dispatch_shutdown.c)

target_include_directories(test_dispatch_shutdown PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_dispatch_shutdown PRIVATE embediq_fb_engine)

target_compile_options(test_dispatch_shutdown PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_dispatch_shutdown COMMAND test_dispatch_shutdown)

# ---------------------------------------------------------------------------
# NVM FB unit tests (P1-T6 / Arch-4b)
# ---------------------------------------------------------------------------

add_executable(test_fb_nvm test_fb_nvm.c)

target_include_directories(test_fb_nvm PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/core/include/hal
    ${CMAKE_SOURCE_DIR}/fbs/drivers
)

target_link_libraries(test_fb_nvm PRIVATE
    embediq_driver_fbs
)

target_compile_options(test_fb_nvm PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_fb_nvm COMMAND test_fb_nvm)

# ---------------------------------------------------------------------------
# HAL contract headers + Endpoint Router compile-and-value tests (Arch-2)
# ---------------------------------------------------------------------------

add_executable(test_hal_contracts test_hal_contracts.c)

target_include_directories(test_hal_contracts PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_hal_contracts PRIVATE embediq_fb_engine)

target_compile_options(test_hal_contracts PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_hal_contracts COMMAND test_hal_contracts)

# ---------------------------------------------------------------------------
# Observatory binary capture tests (Obs-5)
# ---------------------------------------------------------------------------

add_executable(test_obs_capture test_obs_capture.c)

target_include_directories(test_obs_capture PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/core/include/hal
)

target_link_libraries(test_obs_capture PRIVATE
    embediq_fb_engine
)

target_compile_options(test_obs_capture PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_obs_capture COMMAND test_obs_capture)

# ---------------------------------------------------------------------------
# OSAL Observatory behavioral tests (XOBS-1 catch-up — Gate 2)
# ---------------------------------------------------------------------------

add_executable(test_osal_obs test_osal_obs.c)

target_include_directories(test_osal_obs PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_osal_obs PRIVATE
    embediq_osal_posix
)

target_compile_options(test_osal_obs PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_osal_obs COMMAND test_osal_obs)

# ---------------------------------------------------------------------------
# HAL Observatory behavioral tests (XOBS-2 + latch catch-up — Gate 2)
# ---------------------------------------------------------------------------

add_executable(test_hal_obs test_hal_obs.c)

target_include_directories(test_hal_obs PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/core/include/hal
)

target_link_libraries(test_hal_obs PRIVATE
    embediq_hal_posix
)

target_compile_options(test_hal_obs PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_hal_obs COMMAND test_hal_obs)

# ---------------------------------------------------------------------------
# Telemetry Service FB unit tests (Phase 2 S2)
# ---------------------------------------------------------------------------

add_executable(test_fb_telemetry test_fb_telemetry.c)

target_include_directories(test_fb_telemetry PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/generated
    ${CMAKE_SOURCE_DIR}/fbs/services
)

target_link_libraries(test_fb_telemetry PRIVATE
    embediq_service_fbs
    m
)

target_compile_options(test_fb_telemetry PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_fb_telemetry COMMAND test_fb_telemetry)

# ---------------------------------------------------------------------------
# Test harness support library (P2-T5)
# ---------------------------------------------------------------------------

add_library(embediq_test_support STATIC
    embediq_test_support.c
)

target_include_directories(embediq_test_support PUBLIC
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(embediq_test_support PUBLIC
    embediq_fb_engine
)

target_compile_options(embediq_test_support PRIVATE
    -Wall
    -Wextra
    -Werror
)

# ---------------------------------------------------------------------------
# Test harness self-tests (P2-T5)
# ---------------------------------------------------------------------------

add_executable(test_harness test_harness.c)

target_include_directories(test_harness PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_harness PRIVATE
    embediq_fb_engine
    embediq_test_support
)

target_compile_options(test_harness PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_test_harness COMMAND test_harness)
