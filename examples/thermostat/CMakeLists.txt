add_executable(thermostat main.c)

set_target_properties(thermostat PROPERTIES OUTPUT_NAME embediq_thermostat)

target_compile_options(thermostat PRIVATE
    -Wall
    -Wextra
    -Werror
)
