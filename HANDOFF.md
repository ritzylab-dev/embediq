PHASE_COMPLETE: Phase 1
NEXT_SESSION: P2-T1 — Replace 1 ms polling dispatch loop with blocking OSAL semaphore
STATUS: All Phase 1 checks green. Thermostat demo runs end-to-end. All 9 tests pass.

## PHASE 2 MUST-DO BEFORE FREERTOS

- Replace 1 ms polling in `fb_dispatch_loop` (core/src/registry/fb_engine.c) with a
  blocking OSAL semaphore: `embediq_osal_sem_wait()` posted by `embediq_publish()` on
  each queue write.  The current `embediq_osal_delay_ms(1u)` spin loop burns CPU and
  is incompatible with FreeRTOS tickless idle.

- Implement `embediq_engine_dispatch_shutdown()`: signal `g_dispatch_running = false`,
  post a dummy semaphore to unblock each dispatch thread, then join all pthreads before
  returning.  Required for clean teardown in tests and before FreeRTOS port.

- `fb_cloud_mqtt` (MQTT 3.1.1 client FB) and `fb_ota` (OTA FSM FB) were deferred from
  Phase 1.  Both are Phase 2 deliverables and must be implemented before the FreeRTOS
  port begins.
