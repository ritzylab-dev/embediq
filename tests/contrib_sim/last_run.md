# contrib_sim run — 2026-03-25 23:15 UTC

**Branch:** dev
**HEAD:** 3b3cbf50b3e1ec79da32b3c65eb563a3d4156a1a
**Clone URL:** https://github.com/ritzylab-dev/embediq.git

---

Cloning into '/var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.WTS9Pl9xYy/embediq'...
-- The C compiler identification is AppleClang 16.0.0.16000026
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Configuring done (0.6s)
-- Generating done (0.1s)
-- Build files have been written to: /var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.WTS9Pl9xYy/embediq/build
[  1%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/hal_timer_posix.c.o
[  3%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/hal_flash_posix.c.o
[  5%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/hal_wdg_posix.c.o
[  7%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/hal_obs_stream_posix.c.o
[  9%] Linking C static library libembediq_hal_posix.a
[  9%] Built target embediq_hal_posix
[ 11%] Building C object osal/posix/CMakeFiles/embediq_osal_posix.dir/embediq_osal_posix.c.o
[ 12%] Linking C static library libembediq_osal_posix.a
[ 12%] Built target embediq_osal_posix
[ 14%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/registry/fb_engine.c.o
[ 16%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/bus/message_bus.c.o
[ 18%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/fsm/fsm_engine.c.o
[ 20%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/observatory/obs.c.o
[ 22%] Linking C static library libembediq_fb_engine.a
[ 22%] Built target embediq_fb_engine
[ 24%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_timer.c.o
[ 25%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_nvm.c.o
[ 27%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_watchdog.c.o
[ 29%] Linking C static library libembediq_driver_fbs.a
[ 29%] Built target embediq_driver_fbs
[ 31%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/main.c.o
[ 33%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/fb_temp_sensor.c.o
[ 35%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/fb_temp_controller.c.o
[ 37%] Linking C executable embediq_thermostat
[ 37%] Built target thermostat
[ 38%] Building C object tests/unit/CMakeFiles/test_osal_posix.dir/test_osal_posix.c.o
[ 40%] Linking C executable test_osal_posix
[ 40%] Built target test_osal_posix
[ 42%] Building C object tests/unit/CMakeFiles/test_fb_engine.dir/test_fb_engine.c.o
[ 44%] Linking C executable test_fb_engine
[ 44%] Built target test_fb_engine
[ 46%] Building C object tests/unit/CMakeFiles/test_message_bus.dir/test_message_bus.c.o
[ 48%] Linking C executable test_message_bus
[ 48%] Built target test_message_bus
[ 50%] Building C object tests/unit/CMakeFiles/test_fsm_engine.dir/test_fsm_engine.c.o
[ 51%] Linking C executable test_fsm_engine
[ 51%] Built target test_fsm_engine
[ 53%] Building C object tests/unit/CMakeFiles/test_observatory.dir/test_observatory.c.o
[ 55%] Linking C executable test_observatory
[ 55%] Built target test_observatory
[ 57%] Building C object tests/unit/CMakeFiles/test_fb_watchdog.dir/test_fb_watchdog.c.o
[ 59%] Linking C executable test_fb_watchdog
[ 59%] Built target test_fb_watchdog
[ 61%] Building C object tests/unit/CMakeFiles/test_fb_timer.dir/test_fb_timer.c.o
[ 62%] Linking C executable test_fb_timer
[ 62%] Built target test_fb_timer
[ 64%] Building C object tests/unit/CMakeFiles/test_blocking_dispatch.dir/test_blocking_dispatch.c.o
[ 66%] Linking C executable test_blocking_dispatch
[ 66%] Built target test_blocking_dispatch
[ 68%] Building C object tests/unit/CMakeFiles/test_dispatch_shutdown.dir/test_dispatch_shutdown.c.o
[ 70%] Linking C executable test_dispatch_shutdown
[ 70%] Built target test_dispatch_shutdown
[ 72%] Building C object tests/unit/CMakeFiles/test_fb_nvm.dir/test_fb_nvm.c.o
[ 74%] Linking C executable test_fb_nvm
[ 74%] Built target test_fb_nvm
[ 75%] Building C object tests/unit/CMakeFiles/test_hal_contracts.dir/test_hal_contracts.c.o
[ 77%] Linking C executable test_hal_contracts
[ 77%] Built target test_hal_contracts
[ 79%] Building C object tests/unit/CMakeFiles/test_obs_capture.dir/test_obs_capture.c.o
[ 81%] Linking C executable test_obs_capture
[ 81%] Built target test_obs_capture
[ 83%] Building C object tests/integration/CMakeFiles/test_thermostat_observable.dir/test_thermostat_observable.c.o
[ 85%] Linking C executable test_thermostat_observable
[ 85%] Built target test_thermostat_observable
[ 87%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/test_thermostat_full.c.o
[ 88%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/__/__/examples/thermostat/fb_temp_sensor.c.o
[ 90%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/__/__/examples/thermostat/fb_temp_controller.c.o
[ 92%] Linking C executable test_thermostat_full
[ 92%] Built target test_thermostat_full
[ 94%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/test_thermostat_capture.c.o
[ 96%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/__/__/examples/thermostat/fb_temp_sensor.c.o
[ 98%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/__/__/examples/thermostat/fb_temp_controller.c.o
[100%] Linking C executable test_thermostat_capture
[100%] Built target test_thermostat_capture
Test project /var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.WTS9Pl9xYy/embediq/build
      Start  1: cli_obs_tool
 1/16 Test  #1: cli_obs_tool ........................   Passed    0.09 sec
      Start  2: unit_osal_posix
 2/16 Test  #2: unit_osal_posix .....................   Passed    0.46 sec
      Start  3: unit_fb_engine
 3/16 Test  #3: unit_fb_engine ......................   Passed    0.24 sec
      Start  4: unit_message_bus
 4/16 Test  #4: unit_message_bus ....................   Passed    0.26 sec
      Start  5: unit_fsm_engine
 5/16 Test  #5: unit_fsm_engine .....................   Passed    0.23 sec
      Start  6: unit_observatory
 6/16 Test  #6: unit_observatory ....................   Passed    0.23 sec
      Start  7: unit_fb_watchdog
 7/16 Test  #7: unit_fb_watchdog ....................   Passed    0.24 sec
      Start  8: unit_fb_timer
 8/16 Test  #8: unit_fb_timer .......................   Passed    2.73 sec
      Start  9: unit_blocking_dispatch
 9/16 Test  #9: unit_blocking_dispatch ..............   Passed    0.25 sec
      Start 10: unit_dispatch_shutdown
10/16 Test #10: unit_dispatch_shutdown ..............   Passed    0.25 sec
      Start 11: unit_fb_nvm
11/16 Test #11: unit_fb_nvm .........................   Passed    0.23 sec
      Start 12: unit_hal_contracts
12/16 Test #12: unit_hal_contracts ..................   Passed    0.22 sec
      Start 13: unit_obs_capture
13/16 Test #13: unit_obs_capture ....................   Passed    0.22 sec
      Start 14: integration_thermostat_observable
14/16 Test #14: integration_thermostat_observable ...   Passed    0.24 sec
      Start 15: integration_thermostat_full
15/16 Test #15: integration_thermostat_full .........   Passed   15.46 sec
      Start 16: integration_thermostat_capture
16/16 Test #16: integration_thermostat_capture ......   Passed    0.33 sec

100% tests passed, 0 tests failed out of 16

Total Test time (real) =  21.70 sec

---

## Result: PASS

All steps completed successfully:
- git clone: ok
- cmake configure: ok
- cmake build: ok
- ctest: ok
