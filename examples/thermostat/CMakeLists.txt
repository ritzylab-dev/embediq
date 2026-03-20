add_executable(thermostat main.c)

target_compile_options(thermostat PRIVATE
    -Wall
    -Wextra
    -Werror
)
