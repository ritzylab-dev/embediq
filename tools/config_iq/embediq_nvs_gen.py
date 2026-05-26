#!/usr/bin/env python3
"""
tools/config_iq/embediq_nvs_gen.py — EmbedIQ NVM blob generator

Reads a config values JSON file, validates every key against config.iq,
and produces a binary NVM blob compatible with fb_nvm.c.

Blob layout:
    [16-byte header]
    [64 × nvm_entry_t]

Header (16 bytes, little-endian):
    magic[4]         = 0x45 0x49 0x51 0x00  ('EIQ\0')
    version_major    uint8_t
    version_minor    uint8_t
    entry_count      uint16_t  (number of active entries written)
    crc32            uint32_t  (CRC32/ISO-HDLC of all bytes AFTER the 16-byte header)
    _reserved[4]     uint8_t[4] = 0x00 0x00 0x00 0x00

nvm_entry_t (134 bytes each, matches C struct layout in fb_nvm.c):
    key[64]          char array, NUL-terminated, NUL-padded
    val[64]          uint8_t array
    val_len          uint16_t (little-endian)
    schema_id        uint16_t (little-endian) = schema minor version
    active           uint8_t  (1 = active, 0 = inactive)
    _pad[1]          uint8_t  = 0x00  (compiler padding, 2-byte struct alignment)

Inactive entries: key=all zeros, val=all zeros, val_len=0, schema_id=0, active=0.
Total blob size: 16 + (64 × 134) = 8,592 bytes.

Usage:
    # From values file:
    python3 tools/config_iq/embediq_nvs_gen.py \
        --schema config/config.iq \
        --values config/values_dev.json \
        --out nvm_dev.bin

    # Factory defaults blob (all defaults from schema):
    python3 tools/config_iq/embediq_nvs_gen.py \
        --schema config/config.iq \
        --defaults \
        --out factory_defaults.bin

    # Per-device manufacturing blob (base values + factory-key overrides):
    python3 tools/config_iq/embediq_nvs_gen.py \
        --schema config/config.iq \
        --values config/values_prod.json \
        --device-overrides unit_00001.json \
        --out unit_00001.bin

    # Fleet push blob (only fleet-class keys accepted):
    python3 tools/config_iq/embediq_nvs_gen.py \
        --schema config/config.iq \
        --values config/values_fleet.json \
        --source fleet \
        --out fleet_update.bin

Exit codes:
    0 — success, blob written
    1 — validation error (bad key, wrong type, out of range, mutability violation)
    2 — file I/O error

@author  Ritesh Anand
@company embediq.com | ritzylab.com
"""

import argparse
import json
import re
import struct
import sys
import zlib
from pathlib import Path

# ---------------------------------------------------------------------------
# Binary format constants
# ---------------------------------------------------------------------------

MAGIC         = b'EIQ\x00'
HEADER_FORMAT = '<4sBBHI4s'   # magic, major, minor, entry_count, crc32, reserved
HEADER_SIZE   = struct.calcsize(HEADER_FORMAT)
ENTRY_FORMAT  = '<64s64sHHBx'  # key, val, val_len, schema_id, active, 1-byte pad
ENTRY_SIZE    = struct.calcsize(ENTRY_FORMAT)

# Mirrors EMBEDIQ_NVM_* in embediq_config.h / nvm_entry_t in fb_nvm.c.
NVM_MAX_KEYS = 64
NVM_KEY_SIZE = 64
NVM_VAL_SIZE = 64

PAYLOAD_SIZE = NVM_MAX_KEYS * ENTRY_SIZE          # 64 × 134 = 8576
BLOB_SIZE    = HEADER_SIZE + PAYLOAD_SIZE          # 16 + 8576 = 8592

assert HEADER_SIZE == 16, f"header size mismatch: {HEADER_SIZE}"
assert ENTRY_SIZE == 134, f"nvm_entry_t size mismatch: {ENTRY_SIZE}"

# Natural type bounds, used when a key declares no explicit range.
U32_MIN, U32_MAX = 0, 0xFFFFFFFF
I32_MIN, I32_MAX = -0x80000000, 0x7FFFFFFF


# ---------------------------------------------------------------------------
# Errors / exit helpers
# ---------------------------------------------------------------------------

class ValidationError(Exception):
    """Hard validation error — exit code 1."""


def _fail_validation(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)


def _fail_io(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(2)


def _warn(msg):
    print(f"WARNING: {msg}", file=sys.stderr)


# ---------------------------------------------------------------------------
# config.iq schema parser (re-parses the grammar directly — does not import
# tools/config_iq/generate.py, per the PR-C spec)
# ---------------------------------------------------------------------------

_STR_RE = re.compile(r'str\[(\d+)\]')
_SCALAR_TYPES = ('u32', 'i32', 'f32', 'bool')
_MUTABILITY = ('factory', 'fleet', 'user', 'firmware')


def _strip_comment(line):
    idx = line.find('#')
    return line[:idx].strip() if idx != -1 else line.strip()


def _parse_default(kind, iq_type, token, source_name, lineno, key):
    """Parse a config.iq default token into a typed Python value."""
    if kind == 'str':
        if not (token.startswith('"') and token.endswith('"') and len(token) >= 2):
            _fail_validation(f"{source_name}:{lineno}: key '{key}': "
                             f"str default must be quoted, got {token!r}")
        return token[1:-1]
    if iq_type in ('u32', 'i32'):
        try:
            return int(token)
        except ValueError:
            _fail_validation(f"{source_name}:{lineno}: key '{key}': "
                             f"invalid integer default {token!r}")
    if iq_type == 'f32':
        try:
            return float(token)
        except ValueError:
            _fail_validation(f"{source_name}:{lineno}: key '{key}': "
                             f"invalid float default {token!r}")
    if iq_type == 'bool':
        if token in ('true', '1'):
            return True
        if token in ('false', '0'):
            return False
        _fail_validation(f"{source_name}:{lineno}: key '{key}': "
                         f"invalid bool default {token!r}")
    _fail_validation(f"{source_name}:{lineno}: key '{key}': internal default error")


def parse_schema(schema_path):
    """
    Parse config.iq into (version_major, version_minor, schema dict).

    schema[name] = {
        'iq_type', 'kind' ('scalar'|'str'), 'max_len' (str only),
        'mutability', 'default' (typed Python value or None),
        'range_min', 'range_max' (ints or None), 'sensitive' (bool),
    }
    version_major = the N in 'version N'.
    version_minor = number of keys (auto schema minor — count of keys added).
    """
    try:
        source = Path(schema_path).read_text(encoding='utf-8')
    except OSError as exc:
        _fail_io(f"cannot read schema {schema_path}: {exc}")

    version_major = None
    schema = {}
    name = schema_path

    for lineno, raw in enumerate(source.splitlines(), 1):
        line = _strip_comment(raw)
        if not line:
            continue

        if line.startswith('version'):
            m = re.fullmatch(r'version\s+(\d+)', line)
            if not m:
                _fail_validation(f"{name}:{lineno}: malformed 'version' directive")
            version_major = int(m.group(1))
            continue
        if line.startswith('namespace'):
            if not re.fullmatch(r'namespace\s+"[^"]+"', line):
                _fail_validation(f"{name}:{lineno}: malformed 'namespace' directive")
            continue
        if not line.startswith('config'):
            _fail_validation(f"{name}:{lineno}: unexpected token: {line!r}")

        tokens = line.split()
        if len(tokens) < 5:
            _fail_validation(f"{name}:{lineno}: config line needs "
                             "config <key> <type> <mutability> <default>")
        key, type_tok, mut_tok, default_tok = tokens[1:5]
        extras = tokens[5:]

        if key in schema:
            _fail_validation(f"{name}:{lineno}: key '{key}': duplicate key in schema")
        if mut_tok not in _MUTABILITY:
            _fail_validation(f"{name}:{lineno}: key '{key}': unknown mutability '{mut_tok}'")

        sm = _STR_RE.fullmatch(type_tok)
        if sm:
            kind, max_len = 'str', int(sm.group(1))
            if not (1 <= max_len <= 63):
                _fail_validation(f"{name}:{lineno}: key '{key}': str length {max_len} "
                                 "out of range (1..63)")
        elif type_tok in _SCALAR_TYPES:
            kind, max_len = 'scalar', None
        else:
            _fail_validation(f"{name}:{lineno}: key '{key}': unknown type '{type_tok}'")

        range_min = range_max = None
        sensitive = False
        for tok in extras:
            if tok == 'sensitive':
                sensitive = True
            elif '..' in tok:
                rm = re.fullmatch(r'(-?\d+)\.\.(-?\d+)', tok)
                if not rm:
                    _fail_validation(f"{name}:{lineno}: key '{key}': malformed range '{tok}'")
                range_min, range_max = int(rm.group(1)), int(rm.group(2))
            else:
                _fail_validation(f"{name}:{lineno}: key '{key}': unexpected token '{tok}'")

        if range_min is not None and kind != 'scalar':
            _fail_validation(f"{name}:{lineno}: key '{key}': range only valid for numeric types")
        if sensitive and kind != 'str':
            _fail_validation(f"{name}:{lineno}: key '{key}': 'sensitive' only valid for str types")

        default = _parse_default(kind, type_tok, default_tok, name, lineno, key)

        schema[key] = {
            'iq_type':    type_tok,
            'kind':       kind,
            'max_len':    max_len,
            'mutability': mut_tok,
            'default':    default,
            'range_min':  range_min,
            'range_max':  range_max,
            'sensitive':  sensitive,
        }

    if version_major is None:
        _fail_validation(f"{name}: missing 'version' directive")
    if not schema:
        _fail_validation(f"{name}: no config keys found")

    version_minor = len(schema)
    return version_major, version_minor, schema


# ---------------------------------------------------------------------------
# JSON values loading (duplicate-key detection)
# ---------------------------------------------------------------------------

def _no_duplicate_pairs(path):
    """object_pairs_hook that raises on a duplicate key within one object."""
    def hook(pairs):
        seen = {}
        for k, v in pairs:
            if k in seen:
                _fail_validation(f"{path}: key '{k}': duplicate key in values file")
            seen[k] = v
        return seen
    return hook


def load_values(path):
    try:
        text = Path(path).read_text(encoding='utf-8')
    except OSError as exc:
        _fail_io(f"cannot read values file {path}: {exc}")
    try:
        data = json.loads(text, object_pairs_hook=_no_duplicate_pairs(path))
    except json.JSONDecodeError as exc:
        _fail_validation(f"{path}: malformed JSON: {exc}")
    if not isinstance(data, dict):
        _fail_validation(f"{path}: top-level JSON must be an object")
    return data


# ---------------------------------------------------------------------------
# Value validation + encoding
# ---------------------------------------------------------------------------

def _safe_value_repr(spec, value):
    """Mask sensitive values for any stdout/stderr output."""
    return '***' if spec.get('sensitive') else repr(value)


def _py_type_name(value):
    if isinstance(value, bool):
        return 'bool'
    if isinstance(value, int):
        return 'int'
    if isinstance(value, float):
        return 'float'
    if isinstance(value, str):
        return 'str'
    return type(value).__name__


def validate_and_encode(key, value, spec, src_file):
    """
    Validate a single (key, value) against its schema spec and return the
    encoded (val_bytes, val_len) for the nvm_entry_t. Hard-exits on error.
    """
    iq_type = spec['iq_type']
    kind = spec['kind']

    if kind == 'str':
        if not isinstance(value, str):
            _fail_validation(f"{src_file}: key '{key}': expected {iq_type}, "
                             f"got {_py_type_name(value)}")
        encoded = value.encode('utf-8')[:spec['max_len']]
        if len(encoded) > NVM_VAL_SIZE:
            _fail_validation(f"{src_file}: key '{key}': value exceeds {NVM_VAL_SIZE} bytes")
        # val field is zero-padded → implicit NUL terminator; val_len excludes NUL,
        # matching embediq_cfg_set_str (stores strlen, no terminator).
        return encoded, len(encoded)

    # ---- scalar types ----
    if iq_type == 'bool':
        if not isinstance(value, bool):
            _fail_validation(f"{src_file}: key '{key}': expected bool, "
                             f"got {_py_type_name(value)}")
        return struct.pack('B', 1 if value else 0), 1

    if iq_type in ('u32', 'i32'):
        if not isinstance(value, int) or isinstance(value, bool):
            _fail_validation(f"{src_file}: key '{key}': expected {iq_type}, "
                             f"got {_py_type_name(value)}")
        nat_min, nat_max = (U32_MIN, U32_MAX) if iq_type == 'u32' else (I32_MIN, I32_MAX)
        lo = spec['range_min'] if spec['range_min'] is not None else nat_min
        hi = spec['range_max'] if spec['range_max'] is not None else nat_max
        if not (lo <= value <= hi):
            _fail_validation(f"{src_file}: key '{key}': value "
                             f"{_safe_value_repr(spec, value)} out of range {lo}..{hi}")
        fmt = '<I' if iq_type == 'u32' else '<i'
        return struct.pack(fmt, value), 4

    if iq_type == 'f32':
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            _fail_validation(f"{src_file}: key '{key}': expected f32, "
                             f"got {_py_type_name(value)}")
        if spec['range_min'] is not None:
            lo, hi = float(spec['range_min']), float(spec['range_max'])
            if not (lo <= value <= hi):
                _fail_validation(f"{src_file}: key '{key}': value "
                                 f"{_safe_value_repr(spec, value)} out of range {lo}..{hi}")
        return struct.pack('<f', float(value)), 4

    _fail_validation(f"{src_file}: key '{key}': internal encode error for type {iq_type}")


# ---------------------------------------------------------------------------
# Blob assembly
# ---------------------------------------------------------------------------

def pack_entry(key, val_bytes, val_len, schema_minor):
    key_bytes = key.encode('utf-8')
    if len(key_bytes) >= NVM_KEY_SIZE:
        _fail_validation(f"key '{key}': key name exceeds {NVM_KEY_SIZE - 1} bytes")
    return struct.pack(
        ENTRY_FORMAT,
        key_bytes.ljust(NVM_KEY_SIZE, b'\x00')[:NVM_KEY_SIZE],
        val_bytes.ljust(NVM_VAL_SIZE, b'\x00')[:NVM_VAL_SIZE],
        val_len,
        schema_minor,
        1,
    )


def build_blob(entries, schema_major, schema_minor):
    """entries: list of already-packed 134-byte entry blobs (active first)."""
    if len(entries) > NVM_MAX_KEYS:
        _fail_validation(f"too many keys: {len(entries)} > {NVM_MAX_KEYS}")

    active_count = len(entries)
    inactive = b'\x00' * ENTRY_SIZE
    payload = b''.join(entries) + inactive * (NVM_MAX_KEYS - active_count)
    assert len(payload) == PAYLOAD_SIZE, f"payload size mismatch: {len(payload)}"

    crc32 = zlib.crc32(payload) & 0xFFFFFFFF
    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        schema_major,
        schema_minor,
        active_count,
        crc32,
        b'\x00\x00\x00\x00',
    )
    blob = header + payload
    assert len(blob) == BLOB_SIZE, f"blob size mismatch: {len(blob)}"
    return blob, crc32, active_count


# ---------------------------------------------------------------------------
# Key selection per mode
# ---------------------------------------------------------------------------

def collect_keys(args, schema):
    """
    Return an ordered list of (key, value, source_file) tuples to write.
    Applies --defaults / --values / --device-overrides / --source semantics.
    """
    if args.defaults:
        # Every schema key at its default. Each key must have a default.
        missing = [k for k, s in schema.items() if s['default'] is None]
        if missing:
            _fail_validation("--defaults: keys without a default: " + ", ".join(missing))
        return [(k, schema[k]['default'], args.schema) for k in schema]

    # ---- --values mode ----
    values = load_values(args.values)
    for k in values:
        if k not in schema:
            _fail_validation(f"{args.values}: key '{k}': not in schema")

    # device-overrides: factory keys only, win on collision
    if args.device_overrides:
        overrides = load_values(args.device_overrides)
        for k in overrides:
            if k not in schema:
                _fail_validation(f"{args.device_overrides}: key '{k}': not in schema")
            if schema[k]['mutability'] != 'factory':
                _fail_validation(f"{args.device_overrides}: key '{k}': "
                                 "device-overrides may contain only factory-mutability keys")
        merged = dict(values)
        merged.update(overrides)
        value_source = {k: (args.device_overrides if k in overrides else args.values)
                        for k in merged}
    else:
        merged = values
        value_source = {k: args.values for k in merged}

    if args.source:
        # Fleet/user/factory push: only matching-class keys from the values
        # file are written. Wrong-class keys are skipped with a warning (not
        # an error). Absent keys are NOT defaulted — a push contains only the
        # keys being pushed.
        selected = []
        for k in merged:
            if schema[k]['mutability'] == args.source:
                selected.append((k, merged[k], value_source[k]))
            else:
                _warn(f"{value_source[k]}: key '{k}': mutability "
                      f"'{schema[k]['mutability']}' != --source '{args.source}' — skipped")
        return selected

    # No source filter: write every schema key. Present keys use the merged
    # value; absent keys fall back to the schema default with a warning.
    result = []
    for k in schema:
        if k in merged:
            result.append((k, merged[k], value_source[k]))
        else:
            _warn(f"{args.values}: key '{k}': absent from values file — using schema default")
            result.append((k, schema[k]['default'], args.schema))
    return result


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description='EmbedIQ NVM blob generator')
    parser.add_argument('--schema',           required=True, help='Path to config.iq')
    parser.add_argument('--values',           help='Path to values JSON file')
    parser.add_argument('--defaults',         action='store_true', help='Use schema defaults')
    parser.add_argument('--device-overrides', help='Path to device-overrides JSON (factory keys only)')
    parser.add_argument('--source',           choices=['factory', 'fleet', 'user'],
                        help='Accept only keys of this mutability class')
    parser.add_argument('--out',              required=True, help='Output blob file path')
    args = parser.parse_args()

    if args.values and args.defaults:
        _fail_validation("--values and --defaults are mutually exclusive")
    if not args.values and not args.defaults:
        _fail_validation("one of --values or --defaults is required")
    if args.defaults and args.device_overrides:
        _fail_validation("--device-overrides requires --values")

    schema_major, schema_minor, schema = parse_schema(args.schema)

    selected = collect_keys(args, schema)

    entries = []
    for key, value, src in selected:
        spec = schema[key]
        val_bytes, val_len = validate_and_encode(key, value, spec, src)
        entries.append(pack_entry(key, val_bytes, val_len, schema_minor))

    blob, crc32, active_count = build_blob(entries, schema_major, schema_minor)

    try:
        Path(args.out).write_bytes(blob)
    except OSError as exc:
        _fail_io(f"cannot write output {args.out}: {exc}")

    print(f"OK  Wrote {args.out}  "
          f"({active_count} active entr{'y' if active_count == 1 else 'ies'}, "
          f"{len(blob)} bytes, schema v{schema_major}.{schema_minor}, "
          f"crc32={crc32:#010x})")
    return 0


if __name__ == '__main__':
    sys.exit(main())
