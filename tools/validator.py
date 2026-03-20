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
"""

import argparse
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Magic numbers that correspond to embediq_config.h sizing constants.
# Keyed by literal value; value is the list of macro names that own it.
# ---------------------------------------------------------------------------
MAGIC_SIZES = {
    64:  ["EMBEDIQ_MAX_ENDPOINTS", "EMBEDIQ_MSG_MAX_PAYLOAD"],
    16:  ["EMBEDIQ_HIGH_QUEUE_DEPTH", "EMBEDIQ_LOW_QUEUE_DEPTH",
          "EMBEDIQ_MAX_SUBFNS_PER_FB"],
    32:  ["EMBEDIQ_NORMAL_QUEUE_DEPTH"],
    256: ["EMBEDIQ_OBS_RING_DEPTH"],
    8:   ["EMBEDIQ_MAX_BOOT_DEPS"],
}

# Regex: matches magic-number literals in array-sizing or allocation contexts.
#
#   Group "arr"   → [N]  style array sizing
#   Group "alloc" → malloc/calloc/realloc(N, ...) style allocation
#
_MAGIC_RE = re.compile(
    r"(?:"
    r"\[\s*(?P<arr>16|32|64|256|8)\s*\]"
    r"|"
    r"(?:malloc|calloc|realloc)\s*\(\s*(?P<alloc>16|32|64|256|8)\s*[,)]"
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
        return 1

    print("OK: no hardcoded sizing constants found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
