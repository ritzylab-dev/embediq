# contrib_sim run — 2026-04-18 05:17 UTC

**Branch:** dev
**HEAD:** af2a0a2d098f3371791205937652d6cb57ae2b83
**Clone URL:** https://github.com/ritzylab-dev/embediq.git

---

Cloning into '/var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.ihmQHZQP3n/embediq'...
-- The C compiler identification is AppleClang 16.0.0.16000026
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Configuring done (0.7s)
-- Generating done (0.2s)
-- Build files have been written to: /var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.ihmQHZQP3n/embediq/build
[  1%] Building C object CMakeFiles/embediq_crc.dir/components/embediq_crc/embediq_crc.c.o
[  2%] Linking C static library libembediq_crc.a
[  2%] Built target embediq_crc
[  3%] Building C object CMakeFiles/test_embediq_crc.dir/components/embediq_crc/test_embediq_crc.c.o
[  4%] Linking C executable test_embediq_crc
[  4%] Built target test_embediq_crc
[  5%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/peripherals/hal_timer_posix.c.o
[  6%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/peripherals/hal_flash_posix.c.o
[  7%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/peripherals/hal_wdg_posix.c.o
[  8%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/peripherals/hal_obs_stream_posix.c.o
[ 10%] Building C object hal/posix/CMakeFiles/embediq_hal_posix.dir/ops/hal_tls_posix.c.o
[ 11%] Linking C static library libembediq_hal_posix.a
[ 11%] Built target embediq_hal_posix
[ 12%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/registry/fb_engine.c.o
[ 13%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/bus/message_bus.c.o
[ 14%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/fsm/fsm_engine.c.o
[ 15%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/observatory/obs.c.o
[ 16%] Building C object core/src/CMakeFiles/embediq_fb_engine.dir/embediq_platform.c.o
[ 17%] Linking C static library libembediq_fb_engine.a
[ 17%] Built target embediq_fb_engine
[ 18%] Building C object osal/posix/CMakeFiles/embediq_osal_posix.dir/embediq_osal_posix.c.o
[ 20%] Linking C static library libembediq_osal_posix.a
[ 20%] Built target embediq_osal_posix
[ 21%] Building C object CMakeFiles/test_platform_lib.dir/tests/test_platform_lib.c.o
[ 22%] Linking C executable test_platform_lib
ld: warning: ignoring duplicate libraries: 'core/src/libembediq_fb_engine.a', 'hal/posix/libembediq_hal_posix.a', 'osal/posix/libembediq_osal_posix.a'
[ 22%] Built target test_platform_lib
[ 23%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_timer.c.o
[ 24%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_nvm.c.o
[ 25%] Building C object fbs/drivers/CMakeFiles/embediq_driver_fbs.dir/fb_watchdog.c.o
[ 26%] Linking C static library libembediq_driver_fbs.a
[ 26%] Built target embediq_driver_fbs
[ 27%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/main.c.o
[ 28%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/fb_temp_sensor.c.o
[ 30%] Building C object examples/thermostat/CMakeFiles/thermostat.dir/fb_temp_controller.c.o
[ 31%] Linking C executable embediq_thermostat
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 31%] Built target thermostat
[ 32%] Building C object examples/gateway/CMakeFiles/gateway.dir/main.c.o
[ 33%] Building C object examples/gateway/CMakeFiles/gateway.dir/fb_field_receiver.c.o
[ 34%] Building C object examples/gateway/CMakeFiles/gateway.dir/fb_policy_engine.c.o
[ 35%] Building C object examples/gateway/CMakeFiles/gateway.dir/fb_north_bridge.c.o
[ 36%] Linking C executable embediq_gateway
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 36%] Built target gateway
[ 37%] Building C object core/src/CMakeFiles/embediq_cfg.dir/cfg/embediq_cfg.c.o
[ 38%] Linking C static library libembediq_cfg.a
[ 38%] Built target embediq_cfg
[ 40%] Building C object fbs/services/CMakeFiles/embediq_service_fbs.dir/fb_telemetry.c.o
[ 41%] Linking C static library libembediq_service_fbs.a
[ 41%] Built target embediq_service_fbs
[ 42%] Building C object tests/unit/CMakeFiles/test_osal_posix.dir/test_osal_posix.c.o
[ 43%] Linking C executable test_osal_posix
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 43%] Built target test_osal_posix
[ 44%] Building C object tests/unit/CMakeFiles/test_fb_engine.dir/test_fb_engine.c.o
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
[ 55%] Building C object tests/unit/CMakeFiles/test_fb_timer.dir/test_fb_timer.c.o
[ 56%] Linking C executable test_fb_timer
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 56%] Built target test_fb_timer
[ 57%] Building C object tests/unit/CMakeFiles/test_blocking_dispatch.dir/test_blocking_dispatch.c.o
[ 58%] Linking C executable test_blocking_dispatch
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 58%] Built target test_blocking_dispatch
[ 60%] Building C object tests/unit/CMakeFiles/test_dispatch_shutdown.dir/test_dispatch_shutdown.c.o
[ 61%] Linking C executable test_dispatch_shutdown
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 61%] Built target test_dispatch_shutdown
[ 62%] Building C object tests/unit/CMakeFiles/test_fb_nvm.dir/test_fb_nvm.c.o
[ 63%] Linking C executable test_fb_nvm
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 63%] Built target test_fb_nvm
[ 64%] Building C object tests/unit/CMakeFiles/test_hal_contracts.dir/test_hal_contracts.c.o
[ 65%] Linking C executable test_hal_contracts
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 65%] Built target test_hal_contracts
[ 66%] Building C object tests/unit/CMakeFiles/test_obs_capture.dir/test_obs_capture.c.o
[ 67%] Linking C executable test_obs_capture
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 67%] Built target test_obs_capture
[ 68%] Building C object tests/unit/CMakeFiles/test_osal_obs.dir/test_osal_obs.c.o
[ 70%] Linking C executable test_osal_obs
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 70%] Built target test_osal_obs
[ 71%] Building C object tests/unit/CMakeFiles/test_hal_obs.dir/test_hal_obs.c.o
[ 72%] Linking C executable test_hal_obs
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 72%] Built target test_hal_obs
[ 73%] Building C object tests/unit/CMakeFiles/test_embediq_cfg.dir/test_embediq_cfg.c.o
[ 74%] Linking C executable test_embediq_cfg
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 74%] Built target test_embediq_cfg
[ 75%] Building C object tests/unit/CMakeFiles/test_fb_telemetry.dir/test_fb_telemetry.c.o
[ 76%] Linking C executable test_fb_telemetry
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 76%] Built target test_fb_telemetry
[ 77%] Building C object tests/unit/CMakeFiles/embediq_test_support.dir/embediq_test_support.c.o
[ 78%] Linking C static library libembediq_test_support.a
[ 78%] Built target embediq_test_support
[ 80%] Building C object tests/unit/CMakeFiles/test_harness.dir/test_harness.c.o
[ 81%] Linking C executable test_harness
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 81%] Built target test_harness
[ 82%] Building C object tests/integration/CMakeFiles/test_thermostat_observable.dir/test_thermostat_observable.c.o
[ 83%] Linking C executable test_thermostat_observable
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 83%] Built target test_thermostat_observable
[ 84%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/test_thermostat_full.c.o
[ 85%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/__/__/examples/thermostat/fb_temp_sensor.c.o
[ 86%] Building C object tests/integration/CMakeFiles/test_thermostat_full.dir/__/__/examples/thermostat/fb_temp_controller.c.o
[ 87%] Linking C executable test_thermostat_full
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 87%] Built target test_thermostat_full
[ 88%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/test_thermostat_capture.c.o
[ 90%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/__/__/examples/thermostat/fb_temp_sensor.c.o
[ 91%] Building C object tests/integration/CMakeFiles/test_thermostat_capture.dir/__/__/examples/thermostat/fb_temp_controller.c.o
[ 92%] Linking C executable test_thermostat_capture
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 92%] Built target test_thermostat_capture
[ 93%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/test_gateway_full.c.o
[ 94%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/__/__/examples/gateway/fb_field_receiver.c.o
[ 95%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/__/__/examples/gateway/fb_policy_engine.c.o
[ 96%] Building C object tests/integration/CMakeFiles/test_gateway_full.dir/__/__/examples/gateway/fb_north_bridge.c.o
[ 97%] Linking C executable test_gateway_full
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[ 97%] Built target test_gateway_full
[ 98%] Building C object tests/lib/CMakeFiles/test_lib_tls_stub.dir/test_lib_tls_stub.c.o
[100%] Linking C executable test_lib_tls_stub
ld: warning: ignoring duplicate libraries: '../../core/src/libembediq_fb_engine.a', '../../hal/posix/libembediq_hal_posix.a', '../../osal/posix/libembediq_osal_posix.a'
[100%] Built target test_lib_tls_stub
Test project /var/folders/qw/frlylr_53mb4_zqvls9vvxvm0000gp/T/tmp.ihmQHZQP3n/embediq/build
      Start  1: unit_embediq_crc
 1/25 Test  #1: unit_embediq_crc ....................   Passed    0.23 sec
      Start  2: unit_platform_lib
 2/25 Test  #2: unit_platform_lib ...................   Passed    0.25 sec
      Start  3: cli_obs_tool
 3/25 Test  #3: cli_obs_tool ........................   Passed    0.13 sec
      Start  4: unit_osal_posix
 4/25 Test  #4: unit_osal_posix .....................   Passed    0.39 sec
      Start  5: unit_fb_engine
 5/25 Test  #5: unit_fb_engine ......................   Passed    0.26 sec
      Start  6: unit_message_bus
 6/25 Test  #6: unit_message_bus ....................   Passed    0.29 sec
      Start  7: unit_fsm_engine
 7/25 Test  #7: unit_fsm_engine .....................   Passed    0.24 sec
      Start  8: unit_observatory
 8/25 Test  #8: unit_observatory ....................   Passed    0.24 sec
      Start  9: unit_fb_watchdog
 9/25 Test  #9: unit_fb_watchdog ....................   Passed    0.28 sec
      Start 10: unit_fb_timer
10/25 Test #10: unit_fb_timer .......................   Passed    2.78 sec
      Start 11: unit_blocking_dispatch
11/25 Test #11: unit_blocking_dispatch ..............   Passed    0.27 sec
      Start 12: unit_dispatch_shutdown
12/25 Test #12: unit_dispatch_shutdown ..............   Passed    0.31 sec
      Start 13: unit_fb_nvm
13/25 Test #13: unit_fb_nvm .........................   Passed    0.30 sec
      Start 14: unit_hal_contracts
14/25 Test #14: unit_hal_contracts ..................   Passed    0.27 sec
      Start 15: unit_obs_capture
15/25 Test #15: unit_obs_capture ....................   Passed    0.27 sec
      Start 16: unit_osal_obs
16/25 Test #16: unit_osal_obs .......................   Passed    0.26 sec
      Start 17: unit_hal_obs
17/25 Test #17: unit_hal_obs ........................   Passed    0.28 sec
      Start 18: unit_embediq_cfg
18/25 Test #18: unit_embediq_cfg ....................   Passed    0.27 sec
      Start 19: unit_fb_telemetry
19/25 Test #19: unit_fb_telemetry ...................   Passed    0.25 sec
      Start 20: unit_test_harness
20/25 Test #20: unit_test_harness ...................   Passed    0.24 sec
      Start 21: integration_thermostat_observable
21/25 Test #21: integration_thermostat_observable ...   Passed    0.25 sec
      Start 22: integration_thermostat_full
22/25 Test #22: integration_thermostat_full .........   Passed   15.51 sec
      Start 23: integration_thermostat_capture
23/25 Test #23: integration_thermostat_capture ......   Passed    0.36 sec
      Start 24: integration_gateway_full
24/25 Test #24: integration_gateway_full ............   Passed   30.26 sec
      Start 25: lib_tls_stub
25/25 Test #25: lib_tls_stub ........................   Passed    0.24 sec

100% tests passed, 0 tests failed out of 25

Total Test time (real) =  54.47 sec

---

## Result: PASS

All steps completed successfully:
- git clone: ok
- cmake configure: ok
- cmake build: ok
- ctest: ok
