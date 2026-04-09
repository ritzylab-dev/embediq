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

mbedTLS source code is NOT committed here. This entry establishes the vendor
manifest and anomaly assessment. Source will be committed in Phase 2 when
hal_tls_freertos.c requires mbedtls/include/ at compile time.

To reproduce the exact source at the pinned version:
  git clone https://github.com/Mbed-TLS/mbedtls --branch v3.6.1 --depth 1
