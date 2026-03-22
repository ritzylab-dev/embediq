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
