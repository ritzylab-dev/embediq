# tests/integration/CMakeLists.txt — integration test binaries

# ---------------------------------------------------------------------------
# Thermostat observable integration test (P1-T5)
# ---------------------------------------------------------------------------

add_executable(test_thermostat_observable test_thermostat_observable.c)

target_include_directories(test_thermostat_observable PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(test_thermostat_observable PRIVATE embediq_fb_engine)

target_compile_options(test_thermostat_observable PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME integration_thermostat_observable COMMAND test_thermostat_observable)
