/*
 * embediq_meta.h — Functional Block metadata contract
 *
 * Every FB can optionally register a metadata record with the framework.
 * Metadata enables Registry v0 introspection: tooling (EmbedIQ Studio,
 * bridge daemon) can discover what FBs are running, what messages they
 * publish and subscribe to, and their boot phase.
 *
 * Metadata is OPTIONAL. An FB that does not call embediq_meta_register()
 * is visible in the registry by endpoint index only (no name/version/desc).
 *
 * All metadata is static (compile-time constants). Dynamic updates at
 * runtime are not supported in v1 (AGENTS.md §9 non-goals: static registry).
 *
 * I-01: Compiles standalone with zero OSAL or BSP dependencies.
 * R-03: C11. Fixed-width types from <stdint.h> only.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_META_H
#define EMBEDIQ_META_H

#include "embediq_osal.h"   /* embediq_err_t */
#include "embediq_config.h" /* EMBEDIQ_FB_SAFETY_CLASS_LEN */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Sizing limits
 * ------------------------------------------------------------------------- */

/** Maximum FB name length including NUL terminator (bytes). */
#define EMBEDIQ_META_NAME_MAX   32u

/** Maximum version string length including NUL terminator (bytes). */
#define EMBEDIQ_META_VER_MAX    16u

/** Maximum description string length including NUL terminator (bytes). */
#define EMBEDIQ_META_DESC_MAX   128u

/** Maximum number of message IDs listed in the published/subscribed arrays. */
#define EMBEDIQ_META_MAX_MSGS   16u

/* ---------------------------------------------------------------------------
 * FB metadata record
 * ------------------------------------------------------------------------- */

typedef struct {
    /** Human-readable FB name. Should match EmbedIQ_FB_Config_t.name. */
    char     name[EMBEDIQ_META_NAME_MAX];

    /** Semantic version string, e.g. "1.0.3". */
    char     version[EMBEDIQ_META_VER_MAX];

    /** One-sentence description of the FB's purpose. */
    char     description[EMBEDIQ_META_DESC_MAX];

    /** Message IDs this FB publishes (0-terminated or filled to EMBEDIQ_META_MAX_MSGS). */
    uint16_t msgs_published[EMBEDIQ_META_MAX_MSGS];

    /** Message IDs this FB subscribes to (0-terminated or filled to EMBEDIQ_META_MAX_MSGS). */
    uint16_t msgs_subscribed[EMBEDIQ_META_MAX_MSGS];

    /** Boot phase declared by this FB (EmbedIQ_BootPhase_t cast to uint8_t). */
    uint8_t  boot_phase;

    /**
     * Safety standard and level for this FB — Decision D.
     *
     * Canonical encoding: "STD:LEVEL" format (max 15 chars + NUL).
     *
     * Single-standard examples:
     *   "ISO26262:ASIL-B"   — ISO 26262 Automotive Safety Integrity Level B
     *   "IEC61508:SIL-2"    — IEC 61508 Safety Integrity Level 2
     *   "IEC62443:SL-2"     — IEC 62443 Security Level 2
     *   "IEC62304:CLASS-B"  — IEC 62304 Software Safety Class B
     *   "DO178C:DAL-C"      — DO-178C Design Assurance Level C
     *   "EN50128:SIL-1"     — EN 50128 Safety Integrity Level 1
     *   "NONE"              — Not safety-classified (default for non-safety FBs)
     *
     * Multi-standard FBs: put the primary standard here; list all applicable
     * standards in the compliance manifest's safety_standards[] array.
     *
     * This field is read by offline tools and manifest generators — it is NOT
     * queried at runtime by the embedded system. String parsing cost is zero
     * on-device. CON-01 resolution: single string, not split fields.
     */
    char     safety_class[EMBEDIQ_FB_SAFETY_CLASS_LEN];
} EmbedIQ_FB_Meta_t;

/* ---------------------------------------------------------------------------
 * Registration and lookup
 * ------------------------------------------------------------------------- */

/**
 * Register metadata for an FB.
 * Typically called at the end of an FB's init_fn.
 *
 * @param meta  Pointer to a statically allocated metadata record. Must remain
 *              valid for the process lifetime (do not pass stack-allocated data).
 * @return EMBEDIQ_OK on success, EMBEDIQ_ERR if meta is NULL or name is empty.
 */
embediq_err_t embediq_meta_register(const EmbedIQ_FB_Meta_t *meta);

/**
 * Look up a registered FB's metadata by name.
 *
 * @param fb_name  NUL-terminated FB name (must match EmbedIQ_FB_Meta_t.name).
 * @return Pointer to the registered metadata, or NULL if not found.
 */
const EmbedIQ_FB_Meta_t *embediq_meta_get(const char *fb_name);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_META_H */
