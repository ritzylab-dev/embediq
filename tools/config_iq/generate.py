#!/usr/bin/env python3
"""
tools/config_iq/generate.py — config.iq → C accessor + validation generator

Parses a config.iq schema file and emits two C11 artifacts:
    embediq_cfg_generated.h — typed static inline accessors (get/set wrappers)
    embediq_cfg_validate.c  — validation table (k_cfg_schema[])

Zero external dependencies — stdlib only (same constraint as
tools/messages_iq/generate.py).

config.iq schema format:
    version 1
    namespace "com.embediq.demo"

    # comment
    config <key_name> <type> <mutability> <default> [<range>] [sensitive]

Types:
    u32 i32 f32 bool         scalar types
    str[N]                   N = max bytes excluding NUL, 1..63

Mutability:
    factory  — no runtime setter generated
    fleet    — setter generated
    user     — setter generated
    firmware — no runtime setter generated

Usage:
    python3 tools/config_iq/generate.py config/config.iq --out generated/

@author  Ritesh Anand
@company embediq.com | ritzylab.com
"""

import argparse
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Type mapping — config.iq scalar type → (C type, CFG_TYPE enum)
# ---------------------------------------------------------------------------

_SCALAR_TYPES = {
    'u32':  ('uint32_t', 'CFG_TYPE_U32'),
    'i32':  ('int32_t',  'CFG_TYPE_I32'),
    'f32':  ('float',    'CFG_TYPE_F32'),
    'bool': ('bool',     'CFG_TYPE_BOOL'),
}

_MUTABILITY = {
    'factory':  'CFG_MUT_FACTORY',
    'fleet':    'CFG_MUT_FLEET',
    'user':     'CFG_MUT_USER',
    'firmware': 'CFG_MUT_FIRMWARE',
}

# Mutability classes that produce a runtime setter.
_SETTER_MUTABILITY = {'fleet', 'user'}

_STR_RE = re.compile(r'str\[(\d+)\]')


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def _strip_comment(line):
    """Remove everything from '#' to end of line."""
    idx = line.find('#')
    return line[:idx].strip() if idx != -1 else line.strip()


class SchemaError(Exception):
    """Raised on any parse or validation error. Message is user-facing."""


def _err(source_name, lineno, key, desc):
    key_part = f" key '{key}':" if key else ":"
    return SchemaError(f"{source_name}:{lineno}:{key_part} {desc}")


def parse_config_iq(source, source_name='<input>'):
    """
    Parse config.iq source text.

    Returns (version: int, namespace: str, keys: list[dict]).
    Each key dict carries: name, c_ident, kind ('scalar'|'str'), iq_type,
    c_type, type_enum, mutability, mut_enum, has_setter, default (raw token),
    sensitive (bool), and, for scalars, range_min / range_max (raw tokens or
    None when no explicit range was given), and for str, max_len (int).
    """
    version = None
    namespace = None
    keys = []
    seen = set()

    for lineno, raw in enumerate(source.splitlines(), 1):
        line = _strip_comment(raw)
        if not line:
            continue

        if line.startswith('version'):
            m = re.fullmatch(r'version\s+(\d+)', line)
            if not m:
                raise _err(source_name, lineno, None, "malformed 'version' directive")
            version = int(m.group(1))
            continue

        if line.startswith('namespace'):
            m = re.fullmatch(r'namespace\s+"([^"]+)"', line)
            if not m:
                raise _err(source_name, lineno, None, "malformed 'namespace' directive")
            namespace = m.group(1)
            continue

        if not line.startswith('config'):
            raise _err(source_name, lineno, None, f"unexpected token: {line!r}")

        key = _parse_config_line(line, source_name, lineno)
        if key['name'] in seen:
            raise _err(source_name, lineno, key['name'], "duplicate key")
        seen.add(key['name'])
        keys.append(key)

    if version is None:
        raise _err(source_name, 0, None, "missing 'version' directive")
    if namespace is None:
        raise _err(source_name, 0, None, "missing 'namespace' directive")

    return version, namespace, keys


def _parse_config_line(line, source_name, lineno):
    """Parse a single 'config ...' line into a key dict."""
    tokens = line.split()
    # tokens[0] == 'config'
    if len(tokens) < 5:
        raise _err(source_name, lineno, None,
                   "config line needs at least: config <key> <type> "
                   "<mutability> <default>")

    name, type_tok, mut_tok, default = tokens[1], tokens[2], tokens[3], tokens[4]
    extras = tokens[5:]

    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z_][A-Za-z0-9_]*)*', name):
        raise _err(source_name, lineno, name, "invalid key name")

    if mut_tok not in _MUTABILITY:
        raise _err(source_name, lineno, name,
                   f"unknown mutability '{mut_tok}' "
                   f"(expected: {', '.join(sorted(_MUTABILITY))})")

    # ---- type ----
    str_match = _STR_RE.fullmatch(type_tok)
    if str_match:
        kind = 'str'
        max_len = int(str_match.group(1))
        if not (1 <= max_len <= 63):
            raise _err(source_name, lineno, name,
                       f"str length {max_len} out of range (1..63)")
        c_type, type_enum = 'char *', 'CFG_TYPE_STR'
    elif type_tok in _SCALAR_TYPES:
        kind = 'scalar'
        max_len = None
        c_type, type_enum = _SCALAR_TYPES[type_tok]
    else:
        raise _err(source_name, lineno, name,
                   f"unknown type '{type_tok}' "
                   "(expected: u32, i32, f32, bool, str[N])")

    # ---- optional range + sensitive ----
    range_min = range_max = None
    sensitive = False
    for tok in extras:
        if tok == 'sensitive':
            if sensitive:
                raise _err(source_name, lineno, name, "duplicate 'sensitive' flag")
            sensitive = True
        elif '..' in tok:
            if range_min is not None:
                raise _err(source_name, lineno, name, "duplicate range")
            rm = re.fullmatch(r'(-?\d+)\.\.(-?\d+)', tok)
            if not rm:
                raise _err(source_name, lineno, name, f"malformed range '{tok}'")
            range_min, range_max = rm.group(1), rm.group(2)
        else:
            raise _err(source_name, lineno, name, f"unexpected token '{tok}'")

    if range_min is not None and kind != 'scalar':
        raise _err(source_name, lineno, name, "range is only valid for numeric types")
    if sensitive and kind != 'str':
        raise _err(source_name, lineno, name, "'sensitive' is only valid for str types")

    # ---- default sanity ----
    if kind == 'str':
        if not (default.startswith('"') and default.endswith('"') and len(default) >= 2):
            raise _err(source_name, lineno, name,
                       f"str default must be a quoted string, got {default!r}")

    return {
        'name':       name,
        'c_ident':    name.replace('.', '_').lower(),
        'kind':       kind,
        'iq_type':    type_tok,
        'c_type':     c_type,
        'type_enum':  type_enum,
        'mutability': mut_tok,
        'mut_enum':   _MUTABILITY[mut_tok],
        'has_setter': mut_tok in _SETTER_MUTABILITY,
        'default':    default,
        'sensitive':  sensitive,
        'range_min':  range_min,
        'range_max':  range_max,
        'max_len':    max_len,
    }


# ---------------------------------------------------------------------------
# Default-literal helpers
# ---------------------------------------------------------------------------

def _scalar_default_literal(key):
    """C literal for a scalar default, used in the getter call."""
    d = key['default']
    t = key['iq_type']
    if t == 'u32':
        return f'{d}u'
    if t == 'i32':
        return f'{d}'
    if t == 'f32':
        # Ensure a valid C float literal: "1" -> "1.0f", "1.5" -> "1.5f".
        return f'{d}f' if ('.' in d or 'e' in d or 'E' in d) else f'{d}.0f'
    if t == 'bool':
        return 'true' if d in ('true', '1') else 'false'
    raise SchemaError(f"internal: scalar default for {t}")


def _scalar_table_value(key):
    """The .dflt/.min/.max designated-initializer body for the validation table."""
    t = key['iq_type']
    if t == 'u32':
        mn = key['range_min'] if key['range_min'] is not None else '0'
        mx = key['range_max'] if key['range_max'] is not None else 'UINT32_MAX'
        return f'{{ .u32 = {{ .dflt = {key["default"]}, .min = {mn}, .max = {mx} }} }}'
    if t == 'i32':
        mn = key['range_min'] if key['range_min'] is not None else 'INT32_MIN'
        mx = key['range_max'] if key['range_max'] is not None else 'INT32_MAX'
        return f'{{ .i32 = {{ .dflt = {key["default"]}, .min = {mn}, .max = {mx} }} }}'
    if t == 'f32':
        dflt = _scalar_default_literal(key)
        mn = (key['range_min'] + 'f') if key['range_min'] is not None else '0.0f'
        mx = (key['range_max'] + 'f') if key['range_max'] is not None else '0.0f'
        return f'{{ .f32 = {{ .dflt = {dflt}, .min = {mn}, .max = {mx} }} }}'
    if t == 'bool':
        dflt = 'true' if key['default'] in ('true', '1') else 'false'
        return f'{{ .b = {{ .dflt = {dflt} }} }}'
    raise SchemaError(f"internal: scalar table value for {t}")


# ---------------------------------------------------------------------------
# Header generator — embediq_cfg_generated.h
# ---------------------------------------------------------------------------

_H_FILE_HEADER = """\
/*
 * embediq_cfg_generated.h — Auto-generated typed configuration accessors
 *
 * Generated from config/config.iq. Do not edit manually.
 * To regenerate: python3 tools/config_iq/generate.py config/config.iq --out generated/
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_CFG_GENERATED_H
#define EMBEDIQ_CFG_GENERATED_H

#include "embediq_cfg.h"
#include "embediq_osal.h"   /* embediq_err_t, EMBEDIQ_OK, EMBEDIQ_ERR */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
"""

_H_FILE_FOOTER = """\
#ifdef __cplusplus
}
#endif

#endif /* EMBEDIQ_CFG_GENERATED_H */
"""

# Mutability phrase used in the doc comment.
_MUT_PHRASE = {
    'factory':  'factory-locked',
    'fleet':    'fleet-pushable',
    'user':     'user-settable',
    'firmware': 'firmware-internal',
}


def _key_doc_comment(key):
    """Build the /** ... */ doc comment lines for a key (list of strings)."""
    if key['kind'] == 'str':
        type_desc = f'str[{key["max_len"]}]'
    else:
        type_desc = key['iq_type']

    head = f'{key["name"]} — {type_desc}, {_MUT_PHRASE[key["mutability"]]}'

    # Detail (range/default) is shown only for fleet keys, matching the spec.
    if key['mutability'] == 'fleet':
        if key['kind'] == 'scalar' and key['range_min'] is not None:
            head += f', range {key["range_min"]}..{key["range_max"]}, default {key["default"]}'
        else:
            head += f', default {key["default"]}'

    if key['sensitive']:
        return [
            f'/** {head}',
            ' *  SENSITIVE: value is never included in Observatory event payloads or logs. */',
        ]
    return [f'/** {head} */']


def _key_accessors(key):
    """Return the static inline getter/setter (and no-setter note) for one key."""
    lines = []
    ident = key['c_ident']
    name = key['name']

    if key['kind'] == 'str':
        default_lit = key['default']  # already a quoted C string literal
        lines += [
            f'static inline embediq_err_t embediq_cfg_get_{ident}(char *out, size_t len) {{',
            '    if (!out || len == 0u) return EMBEDIQ_ERR;',
            f'    embediq_cfg_get_str("{name}", out, len, {default_lit});',
            '    return EMBEDIQ_OK;',
            '}',
        ]
        if key['has_setter']:
            lines += [
                f'static inline embediq_err_t embediq_cfg_set_{ident}(const char *value) {{',
                f'    return embediq_cfg_set_str("{name}", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;',
                '}',
            ]
    else:
        c_type = key['c_type']
        getter_fn, setter_fn = _scalar_cfg_fns(key['iq_type'])
        default_lit = _scalar_default_literal(key)
        lines += [
            f'static inline embediq_err_t embediq_cfg_get_{ident}({c_type} *out) {{',
            '    if (!out) return EMBEDIQ_ERR;',
            f'    *out = {getter_fn}("{name}", {default_lit});',
            '    return EMBEDIQ_OK;',
            '}',
        ]
        if key['has_setter']:
            lines += [
                f'static inline embediq_err_t embediq_cfg_set_{ident}({c_type} value) {{',
                f'    return {setter_fn}("{name}", value) ? EMBEDIQ_OK : EMBEDIQ_ERR;',
                '}',
            ]

    if not key['has_setter']:
        if key['mutability'] == 'factory':
            lines.append('/* No setter — factory-locked keys are immutable at runtime */')
        elif key['mutability'] == 'firmware':
            lines.append('/* No setter — firmware-internal keys are written only by firmware internals */')

    return lines


def _scalar_cfg_fns(iq_type):
    """Return (getter, setter) embediq_cfg function names for a scalar type."""
    return {
        'u32':  ('embediq_cfg_get_u32',   'embediq_cfg_set_u32'),
        'i32':  ('embediq_cfg_get_i32',   'embediq_cfg_set_i32'),
        'f32':  ('embediq_cfg_get_float', 'embediq_cfg_set_float'),
        'bool': ('embediq_cfg_get_bool',  'embediq_cfg_set_bool'),
    }[iq_type]


def generate_header(keys):
    """Return the full text of embediq_cfg_generated.h."""
    out = [_H_FILE_HEADER, '']
    for key in keys:
        out.extend(_key_doc_comment(key))
        out.extend(_key_accessors(key))
        out.append('')
    out.append(_H_FILE_FOOTER)
    return '\n'.join(out)


# ---------------------------------------------------------------------------
# Validation-table generator — embediq_cfg_validate.c
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Validation-table header generator — embediq_cfg_validate.h
# ---------------------------------------------------------------------------

_C_VALIDATE_H = """\
/*
 * embediq_cfg_validate.h — Auto-generated configuration schema types and lookup
 *
 * Generated from config/config.iq. Do not edit manually.
 * To regenerate: python3 tools/config_iq/generate.py config/config.iq --out generated/
 *
 * Declares cfg_mut_t, cfg_type_t, cfg_schema_entry_t, and cfg_schema_lookup().
 * Included by: embediq_cfg_validate.c and fbs/drivers/fb_nvm.c (Item 4 PR-D).
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EMBEDIQ_CFG_VALIDATE_H
#define EMBEDIQ_CFG_VALIDATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Mutability classes — must match config.iq grammar */
typedef enum {
    CFG_MUT_FACTORY  = 0,
    CFG_MUT_FLEET    = 1,
    CFG_MUT_USER     = 2,
    CFG_MUT_FIRMWARE = 3,
} cfg_mut_t;

/* Value types — must match config.iq grammar */
typedef enum {
    CFG_TYPE_U32  = 0,
    CFG_TYPE_I32  = 1,
    CFG_TYPE_F32  = 2,
    CFG_TYPE_BOOL = 3,
    CFG_TYPE_STR  = 4,
} cfg_type_t;

typedef struct {
    const char *key;         /* NUL-terminated key name — dot notation */
    cfg_type_t  type;
    cfg_mut_t   mutability;
    bool        sensitive;   /* true = exclude value from all log/obs output */
    union {
        struct { uint32_t dflt; uint32_t min; uint32_t max; } u32;
        struct { int32_t  dflt; int32_t  min; int32_t  max; } i32;
        struct { float    dflt; float    min; float    max; } f32;
        struct { bool     dflt;                             } b;
        struct { const char *dflt; uint8_t max_len;         } str;
    } val;
} cfg_schema_entry_t;

/**
 * Look up a key in the generated schema table.
 *
 * @param key  NUL-terminated key name (dot notation).
 * @return     Pointer to matching cfg_schema_entry_t, or NULL if key is unknown.
 */
const cfg_schema_entry_t *cfg_schema_lookup(const char *key);

#endif /* EMBEDIQ_CFG_VALIDATE_H */
"""


def generate_validate_header():
    """Return the full text of embediq_cfg_validate.h (static — no key input needed)."""
    return _C_VALIDATE_H


_C_FILE_HEADER = """\
/*
 * embediq_cfg_validate.c — Auto-generated configuration validation table
 *
 * Generated from config/config.iq. Do not edit manually.
 * To regenerate: python3 tools/config_iq/generate.py config/config.iq --out generated/
 *
 * Used by: fb_nvm.c blob loader (Item 4 PR-D) and embediq_nvs_gen.py (Item 4 PR-C).
 * At boot: unknown keys are skipped. Mutability is enforced against this table.
 *
 * @author  Ritesh Anand
 * @company embediq.com | ritzylab.com
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "embediq_cfg_validate.h"
#include <string.h>   /* strcmp — used by cfg_schema_lookup() */
"""


def generate_validate(keys):
    """Return the full text of embediq_cfg_validate.c."""
    out = [_C_FILE_HEADER, '']

    # Build the four leading columns, aligned to the widest entry.
    key_fields = [f'"{k["name"]}",' for k in keys]
    type_fields = [f'{k["type_enum"]},' for k in keys]
    mut_fields = [f'{k["mut_enum"]},' for k in keys]
    sens_fields = ['true,' if k['sensitive'] else 'false,' for k in keys]

    w_key = max(len(s) for s in key_fields) + 2
    w_type = max(len(s) for s in type_fields) + 2
    w_mut = max(len(s) for s in mut_fields)        # longest sits flush, per spec
    w_sens = max(len(s) for s in sens_fields)

    out.append('static const cfg_schema_entry_t k_cfg_schema[] = {')
    out.append('    /* key                   type           mutability       sens  val */')
    for i, k in enumerate(keys):
        if k['kind'] == 'str':
            val = f'{{ .str = {{ .dflt = {k["default"]}, .max_len = {k["max_len"]} }} }}'
        else:
            val = _scalar_table_value(k)
        row = (
            '    { '
            + key_fields[i].ljust(w_key)
            + type_fields[i].ljust(w_type)
            + mut_fields[i].ljust(w_mut)
            + sens_fields[i].ljust(w_sens)
            + ' '
            + val
            + ' },'
        )
        out.append(row)
    out.append('};')
    out.append('')
    out.append('#define EMBEDIQ_CFG_SCHEMA_COUNT \\')
    out.append('    ((uint16_t)(sizeof(k_cfg_schema) / sizeof(k_cfg_schema[0])))')
    out.append('')
    out.append('const cfg_schema_entry_t *cfg_schema_lookup(const char *key)')
    out.append('{')
    out.append('    uint16_t i;')
    out.append('    for (i = 0u; i < EMBEDIQ_CFG_SCHEMA_COUNT; i++) {')
    out.append('        if (strcmp(key, k_cfg_schema[i].key) == 0) {')
    out.append('            return &k_cfg_schema[i];')
    out.append('        }')
    out.append('    }')
    out.append('    return NULL;')
    out.append('}')
    out.append('')
    return '\n'.join(out)


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Generate typed C config accessors + validation table from config.iq.')
    parser.add_argument('schema', help='Path to the config.iq input schema file')
    parser.add_argument('--out', required=True, metavar='DIR',
                        help='Output directory (created if absent)')
    args = parser.parse_args()

    schema_path = Path(args.schema)
    out_dir = Path(args.out)

    if not schema_path.is_file():
        print(f'ERROR: schema not found: {schema_path}', file=sys.stderr)
        return 1

    source = schema_path.read_text(encoding='utf-8')

    try:
        version, namespace, keys = parse_config_iq(source, str(schema_path))
    except SchemaError as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1

    if not keys:
        print(f'ERROR: {schema_path}: no config keys found', file=sys.stderr)
        return 1

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / 'embediq_cfg_validate.h').write_text(generate_validate_header(), encoding='utf-8')
    (out_dir / 'embediq_cfg_generated.h').write_text(generate_header(keys), encoding='utf-8')
    (out_dir / 'embediq_cfg_validate.c').write_text(generate_validate(keys), encoding='utf-8')

    print(f'OK  Generated embediq_cfg_validate.h + embediq_cfg_generated.h + '
          f'embediq_cfg_validate.c ({len(keys)} key(s), schema v{version}, '
          f'namespace {namespace})')
    return 0


if __name__ == '__main__':
    sys.exit(main())
