# third_party/

Vendored third-party libraries used by EmbedIQ HAL ops table implementations.

## Rules

- Pin to an exact version — a commit SHA or a release tag. No floating HEAD.
- Include a `VENDORING.md` with all 7 mandatory fields (see below).
- Never modify vendored source. Patches go in the HAL ops implementation.
- FBs never include third-party headers directly (Component rule C5).

## VENDORING.md — 7 mandatory fields

| Field | Example |
|-------|---------|
| name | mbedtls |
| version | 3.6.1 |
| license | Apache-2.0 |
| soup_class | B |
| source_url | https://github.com/Mbed-TLS/mbedtls/tree/v3.6.1 |
| anomaly_list_url | https://github.com/Mbed-TLS/mbedtls/issues |
| anomaly_assessment_date | 2026-04-08 |

`soup_class` B or C requires `anomaly_list_url` and `anomaly_assessment_date`.
`anomaly_assessment_date` older than 1 year → CI warning. Older than 2 years → CI fail.

Currently empty. Populated in LIB-5 (mbedTLS).
