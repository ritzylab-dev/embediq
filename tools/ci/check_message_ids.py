#!/usr/bin/env python3
"""
tools/ci/check_message_ids.py — rejects any message ID or message name defined more than once

Reads every messages/*.iq catalog through the catalog parser of
tools/messages_iq/generate.py (parse_iq) and every `#define MSG_<NAME> <id>`
in core/include/embediq_platform_msgs.h, and fails when one ID or one name
has more than one definition across all of them together.

Usage:
  python3 tools/ci/check_message_ids.py [--messages-dir DIR] [--header FILE]
  Defaults: the repository's messages/ and core/include/embediq_platform_msgs.h,
  located from this script's own path.

Exit codes: 0 = no duplicates; 1 = a duplicate ID or name (both definitions
are named as "<file>: <NAME> = 0xNNNN"); 2 = a source cannot be read or
parsed, no catalog exists, a catalog defines no messages, or the header
yields no definitions — an empty input is never a pass.

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import argparse
import glob
import importlib.util
import os
import re
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     "..", ".."))
GENERATOR = os.path.join(REPO, "tools", "messages_iq", "generate.py")
HEADER_DEFINE = re.compile(
    r"^\s*#\s*define\s+(MSG_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|[0-9]+)[uU]?\b")


def _display(path):
    path = os.path.abspath(path)
    if path.startswith(REPO + os.sep):
        return os.path.relpath(path, REPO)
    return path


def _load_parser():
    spec = importlib.util.spec_from_file_location("messages_iq_generate", GENERATOR)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.parse_iq


def _read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--messages-dir", default=os.path.join(REPO, "messages"))
    ap.add_argument("--header", default=os.path.join(
        REPO, "core", "include", "embediq_platform_msgs.h"))
    args = ap.parse_args(argv[1:])

    try:
        parse_iq = _load_parser()
    except (OSError, SyntaxError, AttributeError) as e:
        print(f"check_message_ids.py: cannot read {_display(GENERATOR)}: {e}")
        return 2

    catalogs = sorted(glob.glob(os.path.join(args.messages_dir, "*.iq")))
    if not catalogs:
        print(f"check_message_ids.py: no catalogs found in {args.messages_dir}")
        return 2

    defs = []   # (source file, name, id)
    n_messages = 0
    for path in catalogs:
        try:
            text = _read(path)
        except OSError as e:
            print(f"check_message_ids.py: cannot read {_display(path)}: {e}")
            return 2
        try:
            _version, _namespace, messages = parse_iq(text, _display(path))
        except Exception as e:  # parse_iq raises SyntaxError; anything else is a parse failure too
            print(f"check_message_ids.py: cannot parse {_display(path)}: {e}")
            return 2
        if not messages:
            print(f"check_message_ids.py: catalog defines no messages: {_display(path)}")
            return 2
        n_messages += len(messages)
        defs.extend((_display(path), m["name"], m["id"]) for m in messages)

    try:
        header_text = _read(args.header)
    except OSError as e:
        print(f"check_message_ids.py: cannot read {_display(args.header)}: {e}")
        return 2
    header_defs = [(_display(args.header), m.group(1), int(m.group(2), 0))
                   for m in (HEADER_DEFINE.match(l) for l in header_text.splitlines())
                   if m]
    if not header_defs:
        print(f"check_message_ids.py: no header definitions found in {_display(args.header)}")
        return 2
    defs.extend(header_defs)

    failures = []
    for label, key in (("ID", 2), ("name", 1)):
        seen = {}
        for d in defs:
            seen.setdefault(d[key], []).append(d)
        for value, group in seen.items():
            if len(group) > 1:
                shown = f"0x{value:04X}" if key == 2 else value
                failures.append(f"FAIL  message {label} {shown} is defined "
                                f"{len(group)} times:")
                failures.extend(f"  {f}: {n} = 0x{i:04X}" for f, n, i in group)

    if failures:
        print("\n".join(failures))
        return 1
    print(f"check_message_ids.py: OK — {n_messages} message(s) in {len(catalogs)} "
          f"catalog(s), {len(header_defs)} header definition(s), no duplicates.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
