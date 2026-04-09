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
