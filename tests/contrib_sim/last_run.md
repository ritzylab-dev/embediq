# contrib_sim run — 2026-04-15 18:27 UTC

**Branch:** dev
**HEAD:** 5c5ac6cb0718e473bd588db94df9a95a5caed363
**Clone URL:** https://github.com/ritzylab-dev/embediq.git

---

Cloning into '/var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.8VCIhtVz0N/embediq'...
-- The C compiler identification is AppleClang 16.0.0.16000026
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Configuring done (0.9s)
-- Generating done (0.2s)
-- Build files have been written to: /var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.8VCIhtVz0N/embediq/build
[  1%] Building C object CMakeFiles/embediq_crc.dir/components/embediq_crc/embediq_crc.c.o
[  2%] Linking C static library libembediq_crc.a
[  2%] Built target embediq_crc
[  3%] Building C object CMakeFiles/test_embediq_crc.dir/components/embediq_crc/test_embediq_crc.c.o
[  4%] Linking C executable test_embediq_crc
[  4%] Built target test_embediq_crc
[  6%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/peripherals/hal_timer_posix.c.o
[  7%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/peripherals/hal_flash_posix.c.o
[  8%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/peripherals/hal_wdg_posix.c.o
[  9%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/peripherals/hal_obs_stream_posix.c.o
[ 10%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/ops/hal_tls_posix.c.o
[ 12%] Linking C static library libembediq_hal_posix.a
[ 12%] Built target embediq_hal_posix
[ 13%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/registry/fb_engine.c.o
[ 14%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/bus/message_bus.c.o
[ 15%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/fsm/fsm_engine.c.o
[ 17%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/observatory/obs.c.o
[ 18%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/embediq_platform.c.o
[ 19%] Linking C static library libembediq_fb_engine.a
[ 19%] Built target embediq_fb_engine
[ 20%] Building C object osal/posix/CMakeFiles/embediq_osal_posix.dir/embediq_osal_posix.c.o
[ 21%] Linking C static library libembediq_osal_posix.a
[ 21%] Built target embediq_osal_posix
[ 23%] Building C object CMakeFiles/test_platform_lib.dir/tests/test_platform_lib.c.o
[ 24%] Linking C executable test_platform_lib
ld: warning: ignoring duplicate libraries: 'core/src/libembediq_fb_engine.a', 'hal/posix/libembediq_hal_posix.a', 'osal/posix/libembediq_osal_posix.a'
[ 24%] Built target test_platform_lib
[ 25%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_timer.c.o
[ 26%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_nvm.c.o
[ 28%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_watchdog.c.o
[ 29%] Linking C static library libembediq_driver_fbs.a
[ 29%] Built target embediq_driver_fbs
[ 30%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/main.c.o
[ 31%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/fb_temp_sensor.c.o
[ 32%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/fb_temp_controller.c.o
[ 34%] Linking C executable embediq_thermostat
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 34%] Built target thermostat
[ 35%] Building C object examples/gateway/CMakeFiles/gateway.dir/main.c.o
[ 36%] Building C object examples/gateway/CMakeFiles/gateway.dir/fb_field_receiver.c.o
[ 37%] Building C object examples/gateway/CMakeFiles/gateway.dir/fb_policy_engine.c.o
[ 39%] Building C object examples/gateway/CMakeFiles/gateway.dir/fb_north_bridge.c.o
[ 40%] Linking C executable embediq_gateway
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 40%] Built target gateway
[ 41%] Building C object tests/unit/CMakeFiles/test_osal_posix.dir/test_osal_posix.c.o
[ 42%] Linking C executable test_osal_posix
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 42%] Built target test_osal_posix
[ 43%] Building C object tests/unit/CMakeFiles/test_fb_engine.dir/test_fb_engine.c.o
[ 45%] Linking C executable test_fb_engine
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 45%] Built target test_fb_engine
[ 46%] Building C object tests/unit/CMakeFiles/test_message_bus.dir/test_message_bus.c.o
[ 47%] Linking C executable test_message_bus
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 47%] Built target test_message_bus
[ 48%] Building C object tests/unit/CMakeFiles/test_fsm_engine.dir/test_fsm_engine.c.o
[ 50%] Linking C executable test_fsm_engine
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 50%] Built target test_fsm_engine
[ 51%] Building C object tests/unit/CMakeFiles/test_observatory.dir/test_observatory.c.o
[ 52%] Linking C executable test_observatory
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 52%] Built target test_observatory
[ 53%] Building C object tests/unit/CMakeFiles/test_fb_watchdog.dir/test_fb_watchdog.c.o
[ 54%] Linking C executable test_fb_watchdog
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 54%] Built target test_fb_watchdog
[ 56%] Building C object tests/unit/CMakeFiles/test_fb_timer.dir/test_fb_timer.c.o
[ 57%] Linking C executable test_fb_timer
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 57%] Built target test_fb_timer
[ 58%] Building C object tests/unit/CMakeFiles/test_blocking_dispatch.dir/test_blocking_dispatch.c.o
[ 59%] Linking C executable test_blocking_dispatch
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 59%] Built target test_blocking_dispatch
[ 60%] Building C object tests/unit/CMakeFiles/test_dispatch_shutdown.dir/test_dispatch_shutdown.c.o
[ 62%] Linking C executable test_dispatch_shutdown
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 62%] Built target test_dispatch_shutdown
[ 63%] Building C object tests/unit/CMakeFiles/test_fb_nvm.dir/test_fb_nvm.c.o
[ 64%] Linking C executable test_fb_nvm
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 64%] Built target test_fb_nvm
[ 65%] Building C object tests/unit/CMakeFiles/test_hal_contracts.dir/test_hal_contracts.c.o
[ 67%] Linking C executable test_hal_contracts
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 67%] Built target test_hal_contracts
[ 68%] Building C object tests/unit/CMakeFiles/test_obs_capture.dir/test_obs_capture.c.o
[ 69%] Linking C executable test_obs_capture
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 69%] Built target test_obs_capture
[ 70%] Building C object tests/unit/CMakeFiles/test_osal_obs.dir/test_osal_obs.c.o
[ 71%] Linking C executable test_osal_obs
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 71%] Built target test_osal_obs
[ 73%] Building C object tests/unit/CMakeFiles/test_hal_obs.dir/test_hal_obs.c.o
[ 74%] Linking C executable test_hal_obs
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 74%] Built target test_hal_obs
[ 75%] Building C object tests/unit/CMakeFiles/embediq_test_support.dir/embediq_test_support.c.o
[ 76%] Linking C static library libembediq_test_support.a
[ 76%] Built target embediq_test_support
[ 78%] Building C object tests/unit/CMakeFiles/test_harness.dir/test_harness.c.o
[ 79%] Linking C executable test_harness
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 79%] Built target test_harness
[ 80%] Building C object tests/integration/CMakeFiles/test_thermostat_observable.dir/test_thermostat_observable.c.o
[ 81%] Linking C executable test_thermostat_observable
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 81%] Built target test_thermostat_observable
[ 82%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/test_thermostat_full.c.o
[ 84%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/__/__/examples/thermostat/fb_temp_sensor.c.o
[ 85%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/__/__/examples/thermostat/fb_temp_controller.c.o
[ 86%] Linking C executable test_thermostat_full
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 86%] Built target test_thermostat_full
[ 87%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/test_thermostat_capture.c.o
[ 89%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/__/__/examples/thermostat/fb_temp_sensor.c.o
[ 90%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/__/__/examples/thermostat/fb_temp_controller.c.o
[ 91%] Linking C executable test_thermostat_capture
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 91%] Built target test_thermostat_capture
[ 92%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/test_gateway_full.c.o
[ 93%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/__/__/examples/gateway/fb_field_receiver.c.o
[ 95%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/__/__/examples/gateway/fb_policy_engine.c.o
[ 96%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/__/__/examples/gateway/fb_north_bridge.c.o
[ 97%] Linking C executable test_gateway_full
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 97%] Built target test_gateway_full
[ 98%] Building C object tests/lib/CMakeFiles/test_lib_tls_stub.dir/test_lib_tls_stub.c.o
[100%] Linking C executable test_lib_tls_stub
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[100%] Built target test_lib_tls_stub
Test project /var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.8VCIhtVz0N/embediq/build
      Start  1: unit_embediq_crc
 1/23 Test  #1: unit_embediq_crc ....................   Passed    0.27 sec
      Start  2: unit_platform_lib
 2/23 Test  #2: unit_platform_lib ...................   Passed    0.26 sec
      Start  3: cli_obs_tool
 3/23 Test  #3: cli_obs_tool ........................   Passed    0.13 sec
      Start  4: unit_osal_posix
 4/23 Test  #4: unit_osal_posix .....................   Passed    0.39 sec
      Start  5: unit_fb_engine
 5/23 Test  #5: unit_fb_engine ......................   Passed    0.24 sec
      Start  6: unit_message_bus
 6/23 Test  #6: unit_message_bus ....................   Passed    0.27 sec
      Start  7: unit_fsm_engine
 7/23 Test  #7: unit_fsm_engine .....................   Passed    0.24 sec
      Start  8: unit_observatory
 8/23 Test  #8: unit_observatory ....................   Passed    0.27 sec
      Start  9: unit_fb_watchdog
 9/23 Test  #9: unit_fb_watchdog ....................   Passed    0.26 sec
      Start 10: unit_fb_timer
10/23 Test #10: unit_fb_timer .......................   Passed    2.76 sec
      Start 11: unit_blocking_dispatch
11/23 Test #11: unit_blocking_dispatch ..............   Passed    0.25 sec
      Start 12: unit_dispatch_shutdown
12/23 Test #12: unit_dispatch_shutdown ..............   Passed    0.28 sec
      Start 13: unit_fb_nvm
13/23 Test #13: unit_fb_nvm .........................   Passed    0.29 sec
      Start 14: unit_hal_contracts
14/23 Test #14: unit_hal_contracts ..................   Passed    0.28 sec
      Start 15: unit_obs_capture
15/23 Test #15: unit_obs_capture ....................   Passed    0.28 sec
      Start 16: unit_osal_obs
16/23 Test #16: unit_osal_obs .......................   Passed    0.28 sec
      Start 17: unit_hal_obs
17/23 Test #17: unit_hal_obs ........................   Passed    0.26 sec
      Start 18: unit_test_harness
18/23 Test #18: unit_test_harness ...................   Passed    0.27 sec
      Start 19: integration_thermostat_observable
19/23 Test #19: integration_thermostat_observable ...   Passed    0.28 sec
      Start 20: integration_thermostat_full
20/23 Test #20: integration_thermostat_full .........   Passed   15.51 sec
      Start 21: integration_thermostat_capture
21/23 Test #21: integration_thermostat_capture ......   Passed    0.41 sec
      Start 22: integration_gateway_full
22/23 Test #22: integration_gateway_full ............   Passed   30.25 sec
      Start 23: lib_tls_stub
23/23 Test #23: lib_tls_stub ........................   Passed    0.27 sec

100% tests passed, 0 tests failed out of 23

Total Test time (real) =  54.04 sec

---

## Result: PASS

All steps completed successfully:
- git clone: ok
- cmake configure: ok
- cmake build: ok
- ctest: ok
