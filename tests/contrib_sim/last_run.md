# contrib_sim run — 2026-04-01 04:54 UTC

**Branch:** dev
**HEAD:** 17b533578aa1ff39e1a2f9ea6fe6159a724414b2
**Clone URL:** https://github.com/ritzylab-dev/embediq.git

---

Cloning into '/var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.3Auyy7R1tP/embediq'...
-- The C compiler identification is AppleClang 16.0.0.16000026
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Configuring done (0.8s)
-- Generating done (0.1s)
-- Build files have been written to: /var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.3Auyy7R1tP/embediq/build
[  1%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/hal_timer_posix.c.o
[  3%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/hal_flash_posix.c.o
[  4%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/hal_wdg_posix.c.o
[  6%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/hal_obs_stream_posix.c.o
[  7%] Linking C static library libembediq_hal_posix.a
[  7%] Built target embediq_hal_posix
[  9%] Building C object osal/posix/CMakeFiles/embediq_osal_posix.dir/embediq_osal_posix.c.o
[ 10%] Linking C static library libembediq_osal_posix.a
[ 10%] Built target embediq_osal_posix
[ 12%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/registry/fb_engine.c.o
[ 14%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/bus/message_bus.c.o
[ 15%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/fsm/fsm_engine.c.o
[ 17%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/observatory/obs.c.o
[ 18%] Linking C static library libembediq_fb_engine.a
[ 18%] Built target embediq_fb_engine
[ 20%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_timer.c.o
[ 21%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_nvm.c.o
[ 23%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_watchdog.c.o
[ 25%] Linking C static library libembediq_driver_fbs.a
[ 25%] Built target embediq_driver_fbs
[ 26%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/main.c.o
[ 28%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/fb_temp_sensor.c.o
[ 29%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/fb_temp_controller.c.o
[ 31%] Linking C executable embediq_thermostat
[ 31%] Built target thermostat
[ 32%] Building C object examples/gateway/CMakeFiles/gateway.dir/main.c.o
[ 34%] Building C object examples/gateway/CMakeFiles/gateway.dir/fb_field_receiver.c.o
[ 35%] Building C object examples/gateway/CMakeFiles/gateway.dir/fb_policy_engine.c.o
[ 37%] Building C object examples/gateway/CMakeFiles/gateway.dir/fb_north_bridge.c.o
[ 39%] Linking C executable embediq_gateway
[ 39%] Built target gateway
[ 40%] Building C object tests/unit/CMakeFiles/test_osal_posix.dir/test_osal_posix.c.o
[ 42%] Linking C executable test_osal_posix
[ 42%] Built target test_osal_posix
[ 43%] Building C object tests/unit/CMakeFiles/test_fb_engine.dir/test_fb_engine.c.o
[ 45%] Linking C executable test_fb_engine
[ 45%] Built target test_fb_engine
[ 46%] Building C object tests/unit/CMakeFiles/test_message_bus.dir/test_message_bus.c.o
[ 48%] Linking C executable test_message_bus
[ 48%] Built target test_message_bus
[ 50%] Building C object tests/unit/CMakeFiles/test_fsm_engine.dir/test_fsm_engine.c.o
[ 51%] Linking C executable test_fsm_engine
[ 51%] Built target test_fsm_engine
[ 53%] Building C object tests/unit/CMakeFiles/test_observatory.dir/test_observatory.c.o
[ 54%] Linking C executable test_observatory
[ 54%] Built target test_observatory
[ 56%] Building C object tests/unit/CMakeFiles/test_fb_watchdog.dir/test_fb_watchdog.c.o
[ 57%] Linking C executable test_fb_watchdog
[ 57%] Built target test_fb_watchdog
[ 59%] Building C object tests/unit/CMakeFiles/test_fb_timer.dir/test_fb_timer.c.o
[ 60%] Linking C executable test_fb_timer
[ 60%] Built target test_fb_timer
[ 62%] Building C object tests/unit/CMakeFiles/test_blocking_dispatch.dir/test_blocking_dispatch.c.o
[ 64%] Linking C executable test_blocking_dispatch
[ 64%] Built target test_blocking_dispatch
[ 65%] Building C object tests/unit/CMakeFiles/test_dispatch_shutdown.dir/test_dispatch_shutdown.c.o
[ 67%] Linking C executable test_dispatch_shutdown
[ 67%] Built target test_dispatch_shutdown
[ 68%] Building C object tests/unit/CMakeFiles/test_fb_nvm.dir/test_fb_nvm.c.o
[ 70%] Linking C executable test_fb_nvm
[ 70%] Built target test_fb_nvm
[ 71%] Building C object tests/unit/CMakeFiles/test_hal_contracts.dir/test_hal_contracts.c.o
[ 73%] Linking C executable test_hal_contracts
[ 73%] Built target test_hal_contracts
[ 75%] Building C object tests/unit/CMakeFiles/test_obs_capture.dir/test_obs_capture.c.o
[ 76%] Linking C executable test_obs_capture
[ 76%] Built target test_obs_capture
[ 78%] Building C object tests/integration/CMakeFiles/test_thermostat_observable.dir/test_thermostat_observable.c.o
[ 79%] Linking C executable test_thermostat_observable
[ 79%] Built target test_thermostat_observable
[ 81%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/test_thermostat_full.c.o
[ 82%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/__/__/examples/thermostat/fb_temp_sensor.c.o
[ 84%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/__/__/examples/thermostat/fb_temp_controller.c.o
[ 85%] Linking C executable test_thermostat_full
[ 85%] Built target test_thermostat_full
[ 87%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/test_thermostat_capture.c.o
[ 89%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/__/__/examples/thermostat/fb_temp_sensor.c.o
[ 90%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/__/__/examples/thermostat/fb_temp_controller.c.o
[ 92%] Linking C executable test_thermostat_capture
[ 92%] Built target test_thermostat_capture
[ 93%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/test_gateway_full.c.o
[ 95%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/__/__/examples/gateway/fb_field_receiver.c.o
[ 96%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/__/__/examples/gateway/fb_policy_engine.c.o
[ 98%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/__/__/examples/gateway/fb_north_bridge.c.o
[100%] Linking C executable test_gateway_full
[100%] Built target test_gateway_full
Test project /var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.3Auyy7R1tP/embediq/build
      Start  1: cli_obs_tool
 1/17 Test  #1: cli_obs_tool ........................   Passed    0.06 sec
      Start  2: unit_osal_posix
 2/17 Test  #2: unit_osal_posix .....................   Passed    0.42 sec
      Start  3: unit_fb_engine
 3/17 Test  #3: unit_fb_engine ......................   Passed    0.27 sec
      Start  4: unit_message_bus
 4/17 Test  #4: unit_message_bus ....................   Passed    0.31 sec
      Start  5: unit_fsm_engine
 5/17 Test  #5: unit_fsm_engine .....................   Passed    0.27 sec
      Start  6: unit_observatory
 6/17 Test  #6: unit_observatory ....................   Passed    0.27 sec
      Start  7: unit_fb_watchdog
 7/17 Test  #7: unit_fb_watchdog ....................   Passed    0.30 sec
      Start  8: unit_fb_timer
 8/17 Test  #8: unit_fb_timer .......................   Passed    2.78 sec
      Start  9: unit_blocking_dispatch
 9/17 Test  #9: unit_blocking_dispatch ..............   Passed    0.27 sec
      Start 10: unit_dispatch_shutdown
10/17 Test #10: unit_dispatch_shutdown ..............   Passed    0.28 sec
      Start 11: unit_fb_nvm
11/17 Test #11: unit_fb_nvm .........................   Passed    0.28 sec
      Start 12: unit_hal_contracts
12/17 Test #12: unit_hal_contracts ..................   Passed    0.27 sec
      Start 13: unit_obs_capture
13/17 Test #13: unit_obs_capture ....................   Passed    0.28 sec
      Start 14: integration_thermostat_observable
14/17 Test #14: integration_thermostat_observable ...   Passed    0.28 sec
      Start 15: integration_thermostat_full
15/17 Test #15: integration_thermostat_full .........   Passed   15.51 sec
      Start 16: integration_thermostat_capture
16/17 Test #16: integration_thermostat_capture ......   Passed    0.35 sec
      Start 17: integration_gateway_full
17/17 Test #17: integration_gateway_full ............   Passed   30.27 sec

100% tests passed, 0 tests failed out of 17

Total Test time (real) =  52.48 sec

---

## Result: PASS

All steps completed successfully:
- git clone: ok
- cmake configure: ok
- cmake build: ok
- ctest: ok
