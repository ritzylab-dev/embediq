# Using embediq_cfg — Device Configuration

`embediq_cfg` is a typed get/set wrapper over `fb_nvm` (the persistent NVM
Driver FB). It is not a Function Block — it has no dispatch thread, no
message subscriptions, and no registration step. You call it directly from
your FB's init sub-function or handler, like a C library.

Storage is owned by `fb_nvm`. `embediq_cfg` is the typed interface to it.

---

## 1. Key convention

Keys follow the pattern `"namespace.key_name"`. Use the FB name as the
namespace — that avoids collisions when two FBs happen to want a key
called `"host"` or `"timeout_ms"`.

| Key                 | Owner FB          | Type     | Example value |
|---------------------|-------------------|----------|---------------|
| `motor.pid_kp`      | `fb_motor_ctrl`   | float    | `1.5f`        |
| `motor.pid_ki`      | `fb_motor_ctrl`   | float    | `0.25f`       |
| `mqtt.host`         | `fb_cloud_mqtt`   | string   | `"broker.example.com"` |
| `mqtt.port`         | `fb_cloud_mqtt`   | uint32   | `1883u`       |
| `logger.level`      | `fb_logger`       | uint32   | `2u`          |

Keep keys under `EMBEDIQ_NVM_KEY_MAX` bytes **including the NUL terminator**
— `fb_nvm` rejects longer keys at set time. Define your keys as `#define`
constants at the top of your FB's source file so every call site references
the same string; never build keys at runtime.

---

## 2. Reading configuration at FB init

Read typed values during your FB's `init_fn`. Each getter takes a default
that is returned when the key is absent, when the stored length doesn't
match the expected width, or when `fb_nvm` returns an error — so the
normal code path has no error handling.

```c
#include "embediq_fb.h"
#include "embediq_cfg.h"

#include <string.h>

#define CFG_PID_KP         "motor.pid_kp"
#define CFG_SAMPLE_RATE    "motor.sample_hz"
#define CFG_INVERT_DIR     "motor.invert_dir"
#define CFG_NAME           "motor.name"

typedef struct {
    float    pid_kp;
    uint32_t sample_hz;
    bool     invert_dir;
    char     name[24];
} MotorData_t;

static MotorData_t s_data;

static void motor_init(EmbedIQ_FB_Handle_t fb, void *fb_data)
{
    (void)fb_data;
    MotorData_t *d = &s_data;

    /* Defaults chosen to boot safely if NVM is empty (first-run / factory reset). */
    d->pid_kp     = embediq_cfg_get_float(CFG_PID_KP,      1.0f);
    d->sample_hz  = embediq_cfg_get_u32  (CFG_SAMPLE_RATE, 100u);
    d->invert_dir = embediq_cfg_get_bool (CFG_INVERT_DIR,  false);
    (void)embediq_cfg_get_str(CFG_NAME, d->name, sizeof(d->name), "motor-0");

    /* Register sub-functions against &s_data ... */
}
```

Absent key → default, no branch needed. That's the point of the API: the
FB's boot path is the same whether NVM is empty or already provisioned.

---

## 3. Writing configuration from a handler

Any sub-function can persist a new value. `embediq_cfg` is not opinionated
about how the update arrives — a message from a commissioning tool, a cloud
command, a button press, a serial console command. The application defines
the trigger; `embediq_cfg` provides the storage operation.

```c
static void on_pid_kp_update(EmbedIQ_FB_Handle_t fb, const void *msg,
                             void *fb_data, void *subfn_data)
{
    (void)fb; (void)subfn_data;
    MotorData_t *d = (MotorData_t *)fb_data;

    /* Application-defined payload: extract the new kp value however your
     * MSG_MOTOR_CFG_UPDATE schema defines it. */
    float new_kp = extract_new_kp(msg);

    if (!embediq_cfg_set_float(CFG_PID_KP, new_kp)) {
        /* set failed — key too long, store full, or NVM I/O error.
         * Keep the in-RAM value unchanged so the FB keeps running. */
        return;
    }

    d->pid_kp = new_kp;
}
```

`embediq_cfg_set_*` returns `true` on success, `false` on any NVM error.
Check the return value; on failure, leave your in-RAM state unchanged.

---

## 4. Flushing before OTA

`fb_nvm` buffers writes and persists at its own cadence — fast for
throughput, but it means a value written with `embediq_cfg_set_*` is in the
cache and may not yet be durable. An unexpected power loss before the next
flush can lose it.

Before an OTA reboot, call `embediq_nvm_flush()` from your
`MSG_SYS_OTA_REQUEST` handler and publish `MSG_SYS_OTA_READY` only after
it returns `EMBEDIQ_OK`. This is the documented OTA shutdown contract
(see `core/include/embediq_nvm.h` and `docs/architecture/lifecycle.md`).

```c
#include "embediq_nvm.h"

static void on_ota_request(EmbedIQ_FB_Handle_t fb, const void *msg,
                           void *fb_data, void *subfn_data)
{
    (void)msg; (void)fb_data; (void)subfn_data;

    /* Persist every buffered cfg write before we let the updater continue. */
    (void)embediq_nvm_flush();

    /* Signal to fb_ota that this FB's persistent state is safe on disk. */
    EmbedIQ_Msg_t m;
    memset(&m, 0, sizeof(m));
    m.msg_id   = MSG_SYS_OTA_READY;
    m.priority = EMBEDIQ_MSG_PRIORITY_HIGH;
    embediq_publish(fb, &m);
}
```

For safety-critical values (calibration, safety thresholds), also call
`embediq_nvm_flush()` immediately after the `embediq_cfg_set_*` that
writes them — don't rely on the next periodic flush catching them.

---

## 5. Common mistakes

### `fb_nvm` not registered — every getter returns its default

`embediq_cfg` is a call-through to `embediq_nvm_get / _set / _delete`. If
`fb_nvm_register()` does not run before `embediq_engine_boot()`, every
`embediq_cfg_get_*` returns its supplied default and every `embediq_cfg_set_*`
returns `false`. The symptom is "my device always boots with defaults, never
remembers anything." Check `main.c` for `fb_nvm_register()` before
`embediq_engine_boot()`.

### Key too long — silently rejected

Keys exceeding `EMBEDIQ_NVM_KEY_MAX` bytes (including the NUL) are rejected
by `fb_nvm`. The getter returns the default; the setter returns `false`.
Keep keys short, define them as `#define` constants, and don't build them
from `sprintf` / `strcat` at runtime — that's a ready-made way to cross the
limit in a field condition you don't test.

### Assuming persistence without flush

A value written with `embediq_cfg_set_float()` lives in the NVM cache. It
survives a **clean** shutdown (the framework flushes during ordered exit).
It may **not** survive a power cut before the next flush. For
safety-critical config, call `embediq_nvm_flush()` immediately after the
write. For OTA, see §4.
