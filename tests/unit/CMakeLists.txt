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
# Watchdog FB unit tests (P1-T6)
# ---------------------------------------------------------------------------

add_executable(test_fb_watchdog test_fb_watchdog.c)

target_include_directories(test_fb_watchdog PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/platform/posix
)

target_link_libraries(test_fb_watchdog PRIVATE embediq_platform_posix)

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
    ${CMAKE_SOURCE_DIR}/platform/posix
)

target_link_libraries(test_fb_timer PRIVATE embediq_platform_posix)

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
# NVM FB unit tests (P1-T6)
# ---------------------------------------------------------------------------

add_executable(test_fb_nvm test_fb_nvm.c)

target_include_directories(test_fb_nvm PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/platform/posix
)

target_link_libraries(test_fb_nvm PRIVATE embediq_platform_posix)

target_compile_options(test_fb_nvm PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_fb_nvm COMMAND test_fb_nvm)
