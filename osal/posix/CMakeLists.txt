# osal/posix/CMakeLists.txt — POSIX OSAL static library
#
# Builds embediq_osal_posix from a single translation unit.
# malloc/free are permitted only inside this library (OSAL exception to R-02).
# No headers from this directory are exposed to layers above osal/.

add_library(embediq_osal_posix STATIC embediq_osal_posix.c)

target_include_directories(embediq_osal_posix PUBLIC
    ${CMAKE_SOURCE_DIR}/core/include
)

target_compile_options(embediq_osal_posix PRIVATE
    -Wall
    -Wextra
    -Werror
)

target_link_libraries(embediq_osal_posix PUBLIC pthread)
