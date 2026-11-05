# examples/service_fb_template/CMakeLists.txt — Service FB template (fb_logger)
#
# Built as a static library so the template compiles with every build and its
# behaviour is tested (tests/unit/test_fb_logger_template.c). A copy of the
# template for a real service keeps this file and renames the target.
#
# @author  Ritesh Anand
# @company embediq.com | ritzylab.com
#
# SPDX-License-Identifier: Apache-2.0

add_library(embediq_fb_logger_template STATIC
    fb_logger.c
)

target_include_directories(embediq_fb_logger_template PUBLIC
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/examples/service_fb_template
)

target_link_libraries(embediq_fb_logger_template PUBLIC
    embediq_fb_engine
)

target_compile_options(embediq_fb_logger_template PRIVATE
    -Wall
    -Wextra
    -Werror
)
