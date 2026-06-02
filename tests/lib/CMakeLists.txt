# tests/lib/CMakeLists.txt — library integration tests

add_executable(test_lib_tls_stub test_lib_tls_stub.c)

target_include_directories(test_lib_tls_stub PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_lib_tls_stub PRIVATE embediq_hal_posix)

target_compile_options(test_lib_tls_stub PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME lib_tls_stub COMMAND test_lib_tls_stub)

# ---------------------------------------------------------------------------
# Real TLS connection tests (Item 5.5 PR-C)
# Requires: openssl (cert generation) + python3 (echo server)
# ---------------------------------------------------------------------------

add_executable(test_tls_posix test_tls_posix.c)

target_include_directories(test_tls_posix PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_tls_posix PRIVATE embediq_hal_posix pthread)

target_compile_definitions(test_tls_posix PRIVATE
    EMBEDIQ_REPO_ROOT="${CMAKE_SOURCE_DIR}")

target_compile_options(test_tls_posix PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME lib_tls_posix COMMAND test_tls_posix)
set_tests_properties(lib_tls_posix PROPERTIES TIMEOUT 60)
