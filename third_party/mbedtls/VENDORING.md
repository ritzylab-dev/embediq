# VENDORING.md — mbedTLS

| Field                  | Value                                                                   |
|------------------------|-------------------------------------------------------------------------|
| name                   | mbedtls                                                                 |
| version                | 3.6.1                                                                   |
| license                | Apache-2.0                                                              |
| soup_class             | B                                                                       |
| source_url             | https://github.com/Mbed-TLS/mbedtls/tree/v3.6.1                        |
| anomaly_list_url       | https://github.com/Mbed-TLS/mbedtls/issues                             |
| anomaly_assessment_date| 2026-04-08                                                              |

## Notes

soup_class B: established library with non-trivial security surface. Anomaly list
reviewed 2026-04-08 — no open critical CVEs applicable to the TLS handshake path
used by EmbedIQ (client mode, certificate validation, MQTT over TLS).

## Source status

mbedTLS 3.6.1 source committed to third_party/mbedtls/ in Item 5.5 PR-B.
Only library/ and include/mbedtls/ are committed — programs, tests, docs,
and scripts are excluded. The LICENSE file is included.

EmbedIQ config file: include/mbedtls_embediq_posix_config.h
  Enabled: TLS 1.2 client, X.509, ECDHE+RSA, AES-GCM, SHA-256, /dev/urandom entropy.
  Disabled: TLS server, debug output, PSK-only modes.

Phase 3 (FreeRTOS/ESP32): requires a separate config with hardware entropy
and constrained memory settings. Will be added to hal/freertos/ in Phase 3.

To reproduce the exact source at the pinned version:
  git clone https://github.com/Mbed-TLS/mbedtls --branch v3.6.1 --depth 1
