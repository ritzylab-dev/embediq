# Vendoring Third-Party Libraries

This guide explains how to add an external library to EmbedIQ under
`third_party/`, what `VENDORING.md` must contain, and how CI
(`vendoring_check.py`, `vendoring_date_check.py`) enforces the rules.
Rule reference: **R-lib-3** in [CODING_RULES.md](../CODING_RULES.md).

---

## 1. When to vendor

Use `third_party/` for external libraries that are **compiled as part of
the EmbedIQ build** — a HAL ops implementation that needs mbedTLS, a
Service FB backend that needs Paho MQTT, and so on.

Do **not** vendor:
- Header-only libraries consumed at a system include path. Add them as
  system includes and document them in your HAL ops implementation.
- Build-time tooling (linters, formatters, generators). Pin those in
  `tools/` or as Python requirements.

---

## 2. Directory layout

```
third_party/<library-name>/
├── VENDORING.md        ← mandatory manifest (see §3)
└── <upstream source>   ← pinned to an exact version (tag or SHA)
```

Rules (from `third_party/README.md`):

- **Pin to an exact version** — a tag or commit SHA. No floating `HEAD`.
- **Never modify vendored source.** Any patch goes into the HAL ops
  implementation that calls the library.
- **FBs never include third-party headers directly.** All access goes
  through an ops table (R-lib-4).

---

## 3. `VENDORING.md` — 7 mandatory fields

`vendoring_check.py` parses `third_party/*/VENDORING.md` and requires
each of these seven fields to appear as a markdown table row in the form
`| field | value |`. All seven must be present or CI fails.

| Field                     | Purpose                                                                  |
|---------------------------|--------------------------------------------------------------------------|
| `name`                    | Short library name (matches the directory name)                          |
| `version`                 | Exact pinned version — tag (`v3.6.1`) or 40-char commit SHA              |
| `license`                 | SPDX identifier (e.g. `Apache-2.0`, `MIT`, `BSD-3-Clause`)                |
| `soup_class`              | Software-of-Unknown-Provenance class: `A` (trivial), `B` (established), `C` (critical / security-surface) |
| `source_url`              | Stable URL pointing at the pinned version                                |
| `anomaly_list_url`        | Upstream issue tracker or CVE list URL — where anomalies are reported    |
| `anomaly_assessment_date` | `YYYY-MM-DD` — date a maintainer last reviewed `anomaly_list_url`        |

### Example manifest (minimum complete)

```markdown
# VENDORING.md — libfoo

| Field                   | Value                                          |
|-------------------------|------------------------------------------------|
| name                    | libfoo                                         |
| version                 | 1.2.3                                          |
| license                 | MIT                                            |
| soup_class              | B                                              |
| source_url              | https://github.com/example/libfoo/tree/v1.2.3  |
| anomaly_list_url        | https://github.com/example/libfoo/issues       |
| anomaly_assessment_date | 2026-04-15                                     |

## Notes

soup_class B: established library, non-trivial attack surface. Last
anomaly review 2026-04-15 — no open critical CVEs against v1.2.3 in
the code paths EmbedIQ exercises.
```

Format points worth calling out — they match what the regex in
`vendoring_check.py` actually matches:

- The fields live in a markdown table; each row is `| field | value |`.
- Field names are lowercase with underscores, matching the list above.
- `anomaly_assessment_date` **must** be `YYYY-MM-DD`. Any other format
  is flagged as unparseable by `vendoring_date_check.py`.

---

## 4. Date staleness check

`vendoring_date_check.py` reads `anomaly_assessment_date` from every
`third_party/*/VENDORING.md` and compares it against today's date.

| Age of assessment  | Outcome (non-strict) | Outcome (`--strict`)          |
|--------------------|----------------------|-------------------------------|
| 0–365 days         | pass                 | pass                          |
| 366–730 days       | CI **warning**       | CI **warning**                |
| > 730 days         | CI warning           | CI **hard failure** (exit 1)  |
| Missing / unparseable | CI warning        | CI warning                    |

When CI warns:

1. Re-read `anomaly_list_url` — scan for new CVEs / disclosures against
   the pinned version.
2. Update `anomaly_assessment_date` to today in `VENDORING.md`.
3. Add a short **Notes** entry describing what you reviewed and what you
   concluded (no open CVEs, or specific CVEs with mitigation).

The goal is not to rubber-stamp the date — it is to force an annual
re-read of the upstream anomaly feed.

---

## 5. License requirements (R-04)

The vendored library's license must be compatible with Apache 2.0.

**Compatible (acceptable without review):**
`Apache-2.0`, `MIT`, `BSD-2-Clause`, `BSD-3-Clause`, `ISC`, `Zlib`.

**Incompatible (will be rejected at review):**
`GPL-*`, `LGPL-*`, `AGPL-*`, any "copyleft" license that propagates
obligations to EmbedIQ itself.

**Requires legal sign-off:**
`MPL-2.0`, `EPL-2.0`, and dual-licensed libraries where the permissive
option is narrow.

**Important caveat:** `licence_check.py` enforces SPDX headers on
**EmbedIQ source files only**. It does **not** scan vendored library
files. The license compatibility check is a **human review gate** — the
reviewer must verify the `license` field in `VENDORING.md` matches the
library's actual LICENSE file and is compatible with Apache 2.0.

---

## 6. Worked example — Eclipse Paho MQTT C client

Adding the Paho MQTT C client for a `fb_cloud_mqtt` Service FB:

```
third_party/paho.mqtt.c/
├── VENDORING.md
└── (source pinned at v1.3.13)
```

`third_party/paho.mqtt.c/VENDORING.md`:

```markdown
# VENDORING.md — Eclipse Paho MQTT C Client

| Field                   | Value                                                       |
|-------------------------|-------------------------------------------------------------|
| name                    | paho.mqtt.c                                                 |
| version                 | 1.3.13                                                      |
| license                 | EPL-2.0 OR BSD-3-Clause                                     |
| soup_class              | C                                                           |
| source_url              | https://github.com/eclipse/paho.mqtt.c/tree/v1.3.13         |
| anomaly_list_url        | https://github.com/eclipse/paho.mqtt.c/security/advisories  |
| anomaly_assessment_date | 2026-04-15                                                  |

## Notes

soup_class C: security-critical — handles network-facing protocol parsing
and TLS. Library is dual-licensed; EmbedIQ consumes it under the
BSD-3-Clause option (Apache-2.0 compatible).

Assessment 2026-04-15: reviewed upstream security advisories; no open
advisories affecting v1.3.13. CVE-2023-5408 (fixed in v1.3.13 itself)
verified closed by version pin.

Integration boundary: library is called only from
hal/posix/peripherals/hal_mqtt_posix.c, which implements
embediq_mqtt_ops_t. fb_cloud_mqtt never sees paho symbols directly.
```

That single file satisfies both `vendoring_check.py` (all 7 fields
present) and `vendoring_date_check.py` (date < 365 days old).

---

## 7. Updating a vendored library

```bash
# 1. Replace the pinned source in place
cd third_party/<library>
# ... update source to new tag ...

# 2. Update VENDORING.md — both fields
#    - version: new tag or SHA
#    - anomaly_assessment_date: today
#    - Notes: what you reviewed in the upstream anomaly feed

# 3. Verify locally
bash scripts/check.sh

# 4. Commit
git add third_party/<library>
git commit -m "deps: bump <library> to <new version>"
```

If CI warns or fails after the bump, the most common causes are a
stale `anomaly_assessment_date` you forgot to touch, or a new ABI
version needed in your HAL ops implementation.

---

## References

- [CODING_RULES.md](../CODING_RULES.md) — R-lib-3, R-04
- [third_party/README.md](../third_party/README.md) — field-by-field reference
- `tools/ci/vendoring_check.py` — CI enforcement source
- `tools/ci/vendoring_date_check.py` — date policy source
