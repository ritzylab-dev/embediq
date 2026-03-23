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

# ---------------------------------------------------------------------------
# Thermostat full integration test (P1-T8)
#
# Boots all 5 FBs on real POSIX threads and runs for ~15 seconds.
# CTest timeout is set to 60 seconds.
# ---------------------------------------------------------------------------

add_executable(test_thermostat_full
    test_thermostat_full.c
    ${CMAKE_SOURCE_DIR}/examples/thermostat/fb_temp_sensor.c
    ${CMAKE_SOURCE_DIR}/examples/thermostat/fb_temp_controller.c
)

target_include_directories(test_thermostat_full PRIVATE
    ${CMAKE_SOURCE_DIR}/core/include
    ${CMAKE_SOURCE_DIR}/examples/thermostat
)

target_link_libraries(test_thermostat_full PRIVATE
    embediq_driver_fbs
    embediq_hal_posix
)

target_compile_options(test_thermostat_full PRIVATE
    -Wall
    -Wextra
    -Werror
)

add_test(NAME integration_thermostat_full COMMAND test_thermostat_full)
set_tests_properties(integration_thermostat_full PROPERTIES TIMEOUT 60)
