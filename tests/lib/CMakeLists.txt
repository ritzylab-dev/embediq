# tests/lib/CMakeLists.txt — library component unit tests

# ---------------------------------------------------------------------------
# CRC-32 library tests (LIB-3)
# ---------------------------------------------------------------------------

add_executable(test_embediq_crc test_embediq_crc.c)

target_include_directories(test_embediq_crc PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/components/embediq_crc
)

target_link_libraries(test_embediq_crc PRIVATE
    embediq_crc
    embediq_osal_posix
)

target_compile_options(test_embediq_crc PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME unit_embediq_crc COMMAND test_embediq_crc)
