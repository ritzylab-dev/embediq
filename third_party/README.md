# third_party/

Vendored third-party libraries used by EmbedIQ components.

Each subdirectory must contain:
- Source pinned to an exact version (commit SHA or release tag — no floating HEAD)
- A `NOTICE` file with the upstream license text
- A `VERSION` file stating the version string and upstream SHA

Third-party source is never modified. All integration goes through
the `embediq_lib_ops_t` ops table pattern in `components/`.

Currently empty. Will be populated in LIB-5 (mbedTLS).
