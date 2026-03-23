PHASE_COMPLETE: Phase 1
NEXT_SESSION: P2-T1 — FreeRTOS OSAL (osal/freertos/embediq_osal_freertos.c)

PHASE 1 SUMMARY:
  All core engine modules implemented and tested on macOS/Linux.
  Thermostat demo runs end-to-end with Observatory output.
  ctest passes 100%. CI green.

PHASE 1 WHAT WAS BUILT:
  ✓ OSAL POSIX          — osal/posix/embediq_osal_posix.c
  ✓ FB Engine           — core/src/registry/fb_engine.c
  ✓ Message Bus         — core/src/bus/message_bus.c
  ✓ FSM Engine          — core/src/fsm/fsm_engine.c
  ✓ Observatory         — core/src/observatory/obs.c
  ✓ Platform FBs        — platform/posix/fb_timer.c, fb_watchdog.c, fb_nvm.c
  ✓ Thermostat demo     — examples/thermostat/

PHASE 1 KNOWN LIMITATIONS (fix before Phase 2 ships to hardware):
  ✓ DONE (P2-T0):  fb_dispatch_loop replaced with blocking OSAL semaphore.
  ✓ DONE (P2-T0b): embediq_engine_dispatch_shutdown() added — clean teardown,
    pthread_join, idempotent, enables OTA restart and test teardown.
  ! embediq_bus.h header comment says "call message_bus_boot() after engine_boot"
    but it is now called inside engine_boot automatically — fix the comment.

PHASE 2 MUST DO (in order):
  1. P2-T0: Fix dispatch loop — replace 1ms polling with blocking OSAL semaphore (CRITICAL)
  2. P2-T1: FreeRTOS OSAL — osal/freertos/embediq_osal_freertos.c
  3. P2-T2: ESP32 platform target
  4. P2-T3: fb_cloud_mqtt — MQTT 3.1.1 pure C
  5. P2-T4: fb_ota — OTA FSM, dual-bank atomic write
  6. P2-T5: Industrial gateway example

DEFERRED FROM PHASE 1:
  fb_cloud_mqtt, fb_ota — not needed for thermostat demo, deferred to Phase 2
