# tests/compat/CMakeLists.txt — public-contract compatibility tests
#
# fb_v1_compat.c is compiled by the CI "Compat" step, not here.
#
# compat_wdg_public_client      a client that includes only the public
#                               watchdog header, linked against the production
#                               driver library, run.
# compat_wdg_signature_conflict a definition with the same name and another
#                               signature must fail to compile against the
#                               header; the client above must compile.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

add_executable(wdg_public_client wdg_public_client.c)

target_include_directories(wdg_public_client PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(wdg_public_client PRIVATE
    embediq_driver_fbs
)

target_compile_options(wdg_public_client PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME compat_wdg_public_client COMMAND wdg_public_client)

add_test(NAME compat_wdg_signature_conflict
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/check_wdg_signature_conflict.sh
        ${CMAKE_C_COMPILER}
        ${CMAKE_SOURCE_DIR}/core/include
)
