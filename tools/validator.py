#!/usr/bin/env python3
"""
tools/validator.py — EmbedIQ hardcoded-sizing detector (Invariant I-08)

Scans every .c and .h file in the repository and reports any location where
a numeric literal that matches a sizing constant defined in embediq_config.h
appears in an array-sizing or allocation context.

All sizing parameters must reference the named constants; hardcoded numbers
are a forbidden pattern (CODING_RULES.md §5, Invariant I-08).

Exit 0  → clean; no violations found.
Exit 1  → violations found; file:line details printed to stderr.

Usage:
    python3 tools/validator.py [--root <repo_root>]

@author  Ritesh Anand
@company embediq.com | ritzylab.com
"""

import argparse
import json
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Magic numbers that correspond to embediq_config.h sizing constants.
# Keyed by literal value; value is the list of macro names that own it.
# ---------------------------------------------------------------------------
MAGIC_SIZES = {
    64:  ["EMBEDIQ_MAX_ENDPOINTS", "EMBEDIQ_MSG_MAX_PAYLOAD",
          "EMBEDIQ_NVM_KEY_SIZE", "EMBEDIQ_NVM_VAL_SIZE", "EMBEDIQ_NVM_MAX_KEYS"],
    16:  ["EMBEDIQ_HIGH_QUEUE_DEPTH", "EMBEDIQ_LOW_QUEUE_DEPTH",
          "EMBEDIQ_MAX_SUBFNS_PER_FB"],
    32:  ["EMBEDIQ_NORMAL_QUEUE_DEPTH"],
    256: ["EMBEDIQ_OBS_RING_DEPTH"],
    8:   ["EMBEDIQ_MAX_BOOT_DEPS"],
    512: ["EMBEDIQ_MAX_SUBSCRIPTIONS"],
}

# Regex: matches magic-number literals in array-sizing or allocation contexts.
#
#   Group "arr"   → [N]  style array sizing
#   Group "alloc" → malloc/calloc/realloc(N, ...) style allocation
#
_MAGIC_RE = re.compile(
    r"(?:"
    r"\[\s*(?P<arr>16|32|64|256|512|8)\s*\]"
    r"|"
    r"(?:malloc|calloc|realloc)\s*\(\s*(?P<alloc>16|32|64|256|512|8)\s*[,)]"
    r")"
)

# Strip block comments (preserving newlines so line numbers stay valid).
_BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)
# Strip inline comments.
_LINE_COMMENT_RE  = re.compile(r"//.*$")
# Strip string literals (simplified; sufficient for a sizing validator).
_STRING_LIT_RE    = re.compile(r'"(?:[^"\\]|\\.)*"')

# Directories to skip entirely.
_SKIP_DIRS = {"build", ".git", "generated"}


def _strip_block_comments(source):
    """Replace block comments with whitespace, preserving newlines."""
    def _replace(m):
        return "\n" * m.group(0).count("\n")
    return _BLOCK_COMMENT_RE.sub(_replace, source)


def check_file(path):
    """
    Return a list of (line_number, description) tuples for every violation
    found in *path*.
    """
    try:
        source = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []

    source = _strip_block_comments(source)
    source = _STRING_LIT_RE.sub('""', source)

    violations = []
    for lineno, line in enumerate(source.splitlines(), 1):
        check_line = _LINE_COMMENT_RE.sub("", line)
        for m in _MAGIC_RE.finditer(check_line):
            raw = m.group("arr") or m.group("alloc")
            val = int(raw)
            names = MAGIC_SIZES[val]
            desc = (
                f"hardcoded size {val} — use "
                + " or ".join(names)
                + " from embediq_config.h"
            )
            violations.append((lineno, desc))

    return violations


def check_msg_id_ranges(root):
    """
    Verify that every MSG_ constant in embediq_platform_msgs.h falls within
    its FB's registered range in messages_registry.json, and that no two
    constants share the same ID value.

    Returns True if violations were found.
    """
    registry_path = root / "messages_registry.json"
    header_path = root / "core" / "include" / "embediq_platform_msgs.h"

    if not registry_path.exists() or not header_path.exists():
        return False

    # 1. Build FB name → (range_min, range_max) from official_allocations.
    with open(registry_path, encoding="utf-8") as f:
        registry = json.load(f)

    fb_ranges = {}
    for fb_name, info in registry.get("official_allocations", {}).items():
        r = info.get("range")
        if r and len(r) == 2:
            fb_ranges[fb_name] = (r[0], r[1])

    # 2. Parse embediq_platform_msgs.h: detect section headers and #define lines.
    header_text = header_path.read_text(encoding="utf-8")
    section_re = re.compile(r"/\*\s*-+\s*\n\s*\*\s+(fb_\w+)\s+", re.MULTILINE)
    define_re = re.compile(
        r"^\s*#define\s+(MSG_\w+)\s+0x([0-9A-Fa-f]+)u", re.MULTILINE
    )

    # Map each #define to the most recent section header preceding it.
    sections = [(m.start(), m.group(1)) for m in section_re.finditer(header_text)]
    defines = [
        (m.start(), m.group(1), int(m.group(2), 16))
        for m in define_re.finditer(header_text)
    ]

    violations = False
    seen_ids = {}  # id_value → constant_name

    for def_pos, const_name, id_val in defines:
        # Find which section this define belongs to.
        current_fb = None
        for sec_pos, fb_name in reversed(sections):
            if sec_pos < def_pos:
                current_fb = fb_name
                break

        # 3. Range check.
        if current_fb and current_fb in fb_ranges:
            lo, hi = fb_ranges[current_fb]
            if not (lo <= id_val <= hi):
                print(
                    f"embediq_platform_msgs.h: {const_name} = 0x{id_val:04X} "
                    f"is outside {current_fb} range "
                    f"[0x{lo:04X}, 0x{hi:04X}]",
                    file=sys.stderr,
                )
                violations = True

        # 4. Duplicate detection.
        if id_val in seen_ids:
            print(
                f"embediq_platform_msgs.h: duplicate ID 0x{id_val:04X} "
                f"used by {seen_ids[id_val]} and {const_name}",
                file=sys.stderr,
            )
            violations = True
        else:
            seen_ids[id_val] = const_name

    return violations


def check_registry_vs_header(root):
    """
    Verify that every header-sourced entry in defined_messages matches
    the actual #define value in its source header, and that no two
    defined_messages entries share the same id_hex.

    Returns True if violations were found.
    """
    registry_path = root / "messages_registry.json"
    if not registry_path.exists():
        return False

    with open(registry_path, encoding="utf-8") as f:
        registry = json.load(f)

    defined = registry.get("defined_messages", {})
    if not defined:
        return False

    violations = False

    # Duplicate id_hex detection across all defined_messages.
    seen_hex = {}  # id_hex → msg_name
    for msg_name, info in defined.items():
        id_hex = info.get("id_hex", "")
        if id_hex in seen_hex:
            print(
                f"messages_registry.json: duplicate id_hex {id_hex} "
                f"used by {seen_hex[id_hex]} and {msg_name}",
                file=sys.stderr,
            )
            violations = True
        else:
            seen_hex[id_hex] = msg_name

    # For header-sourced entries, verify #define value matches registry.
    _define_re = re.compile(
        r"^\s*#define\s+(MSG_\w+)\s+0x([0-9A-Fa-f]+)u", re.MULTILINE
    )
    header_cache = {}  # source_path → {name: hex_value_int}

    for msg_name, info in defined.items():
        source = info.get("source", "")
        if not source.endswith(".h"):
            continue

        header_path = root / source
        if not header_path.exists():
            print(
                f"messages_registry.json: {msg_name} source file "
                f"{source} not found",
                file=sys.stderr,
            )
            violations = True
            continue

        if source not in header_cache:
            text = header_path.read_text(encoding="utf-8")
            header_cache[source] = {
                m.group(1): int(m.group(2), 16)
                for m in _define_re.finditer(text)
            }

        header_defines = header_cache[source]
        registry_hex = info.get("id_hex", "")

        if msg_name not in header_defines:
            print(
                f"messages_registry.json: {msg_name} not found in {source}",
                file=sys.stderr,
            )
            violations = True
            continue

        header_val = header_defines[msg_name]
        try:
            registry_val = int(registry_hex, 16)
        except ValueError:
            print(
                f"messages_registry.json: {msg_name} has invalid "
                f"id_hex {registry_hex!r}",
                file=sys.stderr,
            )
            violations = True
            continue

        if header_val != registry_val:
            print(
                f"messages_registry.json: {msg_name} registry id "
                f"{registry_hex} does not match header value "
                f"0x{header_val:04X}",
                file=sys.stderr,
            )
            violations = True

    return violations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root", default=".", help="Repository root (default: current directory)"
    )
    args = parser.parse_args()

    root = Path(args.root).resolve()
    found_violations = False

    for path in sorted(root.rglob("*.[ch]")):
        # Skip the config file itself — it defines the magic numbers.
        if path.name == "embediq_config.h":
            continue

        # Skip build directories and other non-source trees.
        if any(part in _SKIP_DIRS for part in path.parts):
            continue

        for lineno, description in check_file(path):
            rel = path.relative_to(root)
            print(f"{rel}:{lineno}: {description}", file=sys.stderr)
            found_violations = True

    if found_violations:
        print(
            "\nFAIL: hardcoded sizing constants detected. "
            "Replace with named constants from embediq_config.h.",
            file=sys.stderr,
        )

    # Check message ID ranges against registry.
    if check_msg_id_ranges(root):
        print(
            "\nFAIL: message ID range violations detected. "
            "Reconcile embediq_platform_msgs.h with messages_registry.json.",
            file=sys.stderr,
        )
        found_violations = True

    # Check registry entries match header values.
    if check_registry_vs_header(root):
        print(
            "\nFAIL: registry/header mismatch detected. "
            "Reconcile defined_messages in messages_registry.json "
            "with source headers.",
            file=sys.stderr,
        )
        found_violations = True

    if not found_violations:
        print("OK: no hardcoded sizing constants found. "
              "Message ID ranges verified. "
              "Registry entries verified against headers.")

    return 1 if found_violations else 0


if __name__ == "__main__":
    sys.exit(main())
