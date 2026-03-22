# tests/unit/CMakeLists.txt — OSAL POSIX unit test binary

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
