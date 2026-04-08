# components/embediq_crc/CMakeLists.txt — CRC-32 library component (LIB-3)

add_library(embediq_crc STATIC embediq_crc.c)

target_include_directories(embediq_crc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(embediq_crc PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_compile_options(embediq_crc PRIVATE
    -Wall
    -Wextra
    -Werror
)
