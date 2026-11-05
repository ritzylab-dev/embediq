# fb_logger — Service FB template

A minimal, complete **Service FB** you can copy as a starting point for
your own Layer 2 service (cloud publisher, telemetry forwarder, syslog
sink, cache, serializer, etc.).

## What this demonstrates

1. **Ops-table pattern (R-lib-4).** `embediq_logger_ops_t` has
   `uint32_t version` as its first field and an `EMBEDIQ_LOGGER_OPS_VERSION`
   macro. The FB rejects an ops table whose version is below the compiled
   constant. This is the ABI-mismatch protection every Service FB needs.
2. **Platform-agnostic layer rules.** `fb_logger.c` includes **zero** HAL
   headers and makes **zero** OSAL calls. The backend is the only interface
   to the outside world — swap it freely without editing the FB.
3. **Sub-function registration from `init_fn`** (R-sub-08), matching
   `examples/thermostat/fb_temp_sensor.c`.
4. **The sub-function payload contract (R-sub-34).** A sub-function's `msg`
   is the message's payload bytes, never the `EmbedIQ_Msg_t` envelope: no
   header, no `payload_len`, no source endpoint. `MSG_LOG_ENTRY`'s payload is up
   to `EMBEDIQ_MSG_MAX_PAYLOAD` bytes of text, NUL-terminated when shorter. The
   handler checks for NULL, copies at most that many bytes up to the first NUL
   into a stack buffer one byte larger, and terminates it.
   `tests/unit/test_fb_logger_template.c` builds the template and tests this.

## Copying the template for your own Service FB

```
cp -r examples/service_fb_template examples/fb_<your_service>
cd examples/fb_<your_service>
# rename: fb_logger → fb_<your_service> throughout
# redefine: embediq_logger_ops_t → embediq_<your_service>_ops_t
# redefine: MSG_LOG_ENTRY → your reserved message ID (range 0x1400–0xFFFF;
#           reserve in messages_registry.json first)
```

## Swapping the ops table for a real backend

Because the FB is wired only to the ops table, switching backends means
writing a new ops table and registering a different one at boot:

```c
/* file backend */
static const embediq_logger_ops_t file_ops = {
    .version = EMBEDIQ_LOGGER_OPS_VERSION,
    .open = file_open, .write = file_write,
    .flush = file_flush, .close = file_close,
};

/* UART backend */
static const embediq_logger_ops_t uart_ops = {
    .version = EMBEDIQ_LOGGER_OPS_VERSION,
    .open = NULL, .write = uart_write,
    .flush = NULL, .close = NULL,
};

fb_logger_register(&file_ops);   /* or &uart_ops, or &mqtt_ops */
```

Unused function pointers may be `NULL` — the FB always NULL-checks before
calling.

## Adding external libraries

If your backend needs a third-party library (e.g. Paho MQTT, mbedTLS),
vendor it under `third_party/<library>/` with a `VENDORING.md` manifest.
See **[docs/VENDORING.md](../../docs/VENDORING.md)** for the 7 mandatory
fields and the CI staleness policy.

## What CI enforces on files in this directory

| Check                    | Rule                | What it looks for                                  |
|--------------------------|---------------------|----------------------------------------------------|
| `boundary_checker.py`    | Layer boundaries    | Out-of-layer `#include`s                           |
| `check_fb_calls.py`      | I-07, R-02, R-sub-03, R-sub-03a, R-stdio, R-sub-34 | `malloc`, `usleep`, direct OSAL / POSIX threading, `printf` and other stdio output, a handler's `msg` cast to or aliased as `EmbedIQ_Msg_t` — scans `examples/` too; only the host programs `examples/*/main.c` may print and sleep |
| `licence_check.py`       | SPDX                | Missing `SPDX-License-Identifier` header           |
| `ops_version_check.py`   | R-lib-4, D-LIB-5    | Ops headers in `core/include/ops/` only (template is outside that scope, but follows the same convention) |

Before pushing, run `bash scripts/check.sh` — it mirrors CI exactly.
