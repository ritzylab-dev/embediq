#!/usr/bin/env python3
"""
tools/messages_iq/generate.py — messages.iq → C header generator

Parses a .iq schema file and emits a C11 header (embediq_msg_catalog.h)
containing message ID defines, payload structs, and compile-time layout
assertions.

Zero external dependencies — stdlib only.

.iq schema format:
    version 1
    namespace "com.embediq.core"

    // comment
    message MSG_NAME id=0xXXXX {
        field_name : type
    }

Supported field types:
    u8 u16 u32 u64  →  uint8_t  uint16_t  uint32_t  uint64_t
    i8 i16 i32 i64  →  int8_t   int16_t   int32_t   int64_t
    f32 f64         →  float    double
    bool            →  bool     (requires <stdbool.h>)

Usage:
    python3 tools/messages_iq/generate.py messages/core.iq --out generated/

Output:
    generated/embediq_msg_catalog.h

@author  Ritesh Anand
@company embediq.com | ritzylab.com
"""

import argparse
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Type mapping
# ---------------------------------------------------------------------------

_TYPE_MAP = {
    'u8':   'uint8_t',
    'u16':  'uint16_t',
    'u32':  'uint32_t',
    'u64':  'uint64_t',
    'i8':   'int8_t',
    'i16':  'int16_t',
    'i32':  'int32_t',
    'i64':  'int64_t',
    'f32':  'float',
    'f64':  'double',
    'bool': 'bool',
}

# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

def _strip_comment(line):
    """Remove everything from // to end of line."""
    idx = line.find('//')
    return line[:idx].strip() if idx != -1 else line.strip()


def parse_iq(source, source_name='<input>'):
    """
    Parse .iq source text.

    Returns (version: int, namespace: str, messages: list[dict]).
    Each message dict: {'name': str, 'id': int, 'fields': list[dict]}
    Each field  dict:  {'name': str, 'iq_type': str, 'c_type': str}

    Raises SyntaxError on malformed input.
    """
    version   = None
    namespace = None
    messages  = []
    cur_msg   = None
    brace_depth = 0

    lines = source.splitlines()

    for lineno, raw in enumerate(lines, 1):
        line = _strip_comment(raw)
        if not line:
            continue

        if brace_depth == 0:
            # ---- top-level directives ----

            if line.startswith('version'):
                m = re.fullmatch(r'version\s+(\d+)', line)
                if not m:
                    raise SyntaxError(
                        f"{source_name}:{lineno}: malformed 'version' directive")
                version = int(m.group(1))

            elif line.startswith('namespace'):
                m = re.fullmatch(r'namespace\s+"([^"]+)"', line)
                if not m:
                    raise SyntaxError(
                        f"{source_name}:{lineno}: malformed 'namespace' directive")
                namespace = m.group(1)

            elif line.startswith('message'):
                m = re.match(
                    r'message\s+([A-Z_][A-Z0-9_]*)\s+id=(0x[0-9A-Fa-f]+|\d+)',
                    line)
                if not m:
                    raise SyntaxError(
                        f"{source_name}:{lineno}: malformed 'message' declaration")
                cur_msg = {
                    'name':   m.group(1),
                    'id':     int(m.group(2), 0),
                    'fields': [],
                }
                if '{' in line:
                    brace_depth = 1
                    # Single-line empty body: message FOO id=0x0001 {}
                    if line.rstrip().endswith('{}') or (
                            line.count('{') == line.count('}')):
                        brace_depth = 0
                        messages.append(cur_msg)
                        cur_msg = None

            elif line == '{':
                # Opening brace on its own line
                if cur_msg is None:
                    raise SyntaxError(
                        f"{source_name}:{lineno}: unexpected '{{' outside message")
                brace_depth = 1

            else:
                raise SyntaxError(
                    f"{source_name}:{lineno}: unexpected token: {line!r}")

        else:
            # ---- inside message body ----

            if line == '}':
                brace_depth = 0
                messages.append(cur_msg)
                cur_msg = None

            else:
                # field_name : type
                m = re.fullmatch(r'(_?[a-z][a-z0-9_]*)\s*:\s*([a-z0-9]+)', line)
                if not m:
                    raise SyntaxError(
                        f"{source_name}:{lineno}: malformed field definition: {line!r}")
                fname    = m.group(1)
                iq_type  = m.group(2)
                if iq_type not in _TYPE_MAP:
                    raise SyntaxError(
                        f"{source_name}:{lineno}: unknown type '{iq_type}' "
                        f"(supported: {', '.join(sorted(_TYPE_MAP))})")
                cur_msg['fields'].append({
                    'name':    fname,
                    'iq_type': iq_type,
                    'c_type':  _TYPE_MAP[iq_type],
                })

    if cur_msg is not None:
        raise SyntaxError(
            f"{source_name}: unterminated message '{cur_msg['name']}' "
            f"(missing closing '}}')")

    return version, namespace, messages


# ---------------------------------------------------------------------------
# Code generator
# ---------------------------------------------------------------------------

def _c_str_width(fields):
    """Return the max c_type width for aligned column output."""
    return max((len(f['c_type']) for f in fields), default=0)


def generate_header(version, namespace, messages, source_path, output_name):
    """Return the complete content of the generated message catalog header as a string."""

    src   = Path(source_path).name
    stem  = Path(output_name).stem          # e.g. 'thermostat_msg_catalog'
    guard = stem.upper() + '_H'             # e.g. 'THERMOSTAT_MSG_CATALOG_H'
    regen = (f'python3 tools/messages_iq/generate.py {source_path}'
             f' --out generated/ --output-name {output_name}')
    lines = []

    # ---- file header ----
    lines += [
        '/*',
        f' * {output_name} — Generated message catalog',
        ' *',
        f' * Source:    {src}',
        f' * Namespace: {namespace}',
        f' * Schema:    version {version}',
        ' *',
        ' * DO NOT EDIT — regenerate with:',
        f' *   {regen}',
        ' *',
        ' * @author  Ritesh Anand',
        ' * @company embediq.com | ritzylab.com',
        ' *',
        ' * SPDX-License-Identifier: Apache-2.0',
        ' */',
        '',
        f'#ifndef {guard}',
        f'#define {guard}',
        '',
        '#include <stdint.h>',
        '#include <stddef.h>',
        '#include <stdbool.h>',
        '#include "embediq_config.h"',
        '',
        '#ifdef __cplusplus',
        'extern "C" {',
        '#endif',
        '',
    ]

    # ---- per-message output ----
    for msg in messages:
        name   = msg['name']
        msg_id = msg['id']
        fields = msg['fields']

        lines.append(f'/* --- {name} (0x{msg_id:04X}) --- */')
        lines.append(f'#define {name}  0x{msg_id:04X}u')
        lines.append('')

        # Payload struct
        lines.append(f'typedef struct {{')
        if fields:
            w = _c_str_width(fields)
            for f in fields:
                lines.append(f'    {f["c_type"]:<{w}}  {f["name"]};')
        else:
            lines.append('    uint8_t  _empty; /* no fields */')
        lines.append(f'}} {name}_Payload_t;')
        lines.append('')

        # Size assertion (Invariant I-04 — all payload sizing from config)
        lines.append(
            f'_Static_assert(sizeof({name}_Payload_t) <= EMBEDIQ_MSG_MAX_PAYLOAD,')
        lines.append(
            f'    "{name} payload exceeds EMBEDIQ_MSG_MAX_PAYLOAD");')

        # Layout stability assertion (first field at offset 0)
        if fields:
            first = fields[0]['name']
            lines.append(
                f'_Static_assert(offsetof({name}_Payload_t, {first}) == 0,')
            lines.append(
                f'    "{name} layout changed — increment schema_id");')

        lines.append('')

    lines += [
        '#ifdef __cplusplus',
        '}',
        '#endif',
        '',
        f'#endif /* {guard} */',
        '',  # trailing newline
    ]

    return '\n'.join(lines)


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Generate a C message catalog header from a .iq schema file.')
    parser.add_argument('schema',
                        help='Path to the .iq input schema file')
    parser.add_argument('--out', required=True, metavar='DIR',
                        help='Output directory (created if absent)')
    parser.add_argument('--output-name', metavar='FILENAME', default=None,
                        help='Output filename (default: <schema_stem>_msg_catalog.h)')
    args = parser.parse_args()

    schema_path = Path(args.schema)
    out_dir     = Path(args.out)

    if not schema_path.is_file():
        print(f'ERROR: schema not found: {schema_path}', file=sys.stderr)
        return 1

    # Default output filename: <stem>_msg_catalog.h (e.g. core → core_msg_catalog.h)
    # --output-name overrides for backward compatibility with embediq_msg_catalog.h
    output_name = args.output_name or f'{schema_path.stem}_msg_catalog.h'

    out_dir.mkdir(parents=True, exist_ok=True)

    source = schema_path.read_text(encoding='utf-8')

    try:
        version, namespace, messages = parse_iq(source, str(schema_path))
    except SyntaxError as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1

    if not messages:
        print(f'WARNING: no messages found in {schema_path}', file=sys.stderr)

    out_path = out_dir / output_name
    content  = generate_header(version, namespace, messages, schema_path, output_name)
    out_path.write_text(content, encoding='utf-8')

    print(f'OK  Generated {out_path}  ({len(messages)} message(s))')
    return 0


if __name__ == '__main__':
    sys.exit(main())
