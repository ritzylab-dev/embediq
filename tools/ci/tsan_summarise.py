#!/usr/bin/env python3
"""
tsan_summarise.py — de-duplicated ThreadSanitizer race inventory.

Reads every ThreadSanitizer report file (tsan.*) written via
TSAN_OPTIONS=log_path=<dir>/tsan and prints one Markdown table row per
distinct finding:

  | # | Kind | Variable or location | Writer frame | Reader/other frame | Count |

De-duplication key: (kind, variable location, top frame of the first stack,
top frame of the second stack). "Top frame" is the first frame inside the
repository (core/, osal/, hal/, fbs/, examples/, tests/, components/,
third_party/); sanitizer and libc interceptor frames are skipped.

Usage:
  python3 tools/ci/tsan_summarise.py <dir-containing-tsan-files>

Exits 0 always — the inventory is informational (the tsan CI job is advisory
until Set 2 / F7). Python 3 standard library only.

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import glob
import os
import re
import sys
from collections import OrderedDict

REPO_DIRS = ("core/", "osal/", "hal/", "fbs/", "examples/", "tests/",
             "components/", "third_party/")

# Sanitizer runtime / libc frames are never "the" frame of a finding.
RUNTIME_MODULE = re.compile(r"libclang_rt|libtsan|libc\.|libc-|libsystem|libdyld|libpthread")
# GCC/libtsan (Linux) appends " (BuildId: <hex>)" after the module.
BUILD_ID = re.compile(r"\s*\(BuildId: [0-9a-f]+\)\s*$")

SEPARATOR = re.compile(r"^={10,}\s*$")
WARNING = re.compile(r"WARNING: ThreadSanitizer: (.+?)(?: \(pid=\d+\))?\s*$")
FRAME = re.compile(r"^\s*#(\d+)\s+(\S+)\s+(.*?)\s*(?:\((\S+?)\+0x[0-9a-f]+\))?\s*$")
THREAD_REF = re.compile(r"by (main thread|thread (T\d+))")
THREAD_DEF = re.compile(r"^\s*Thread (T\d+)(?: '([^']*)')? \(tid=")
LOCATION = re.compile(r"^\s*Location is (.*)$")

KIND_NAMES = (
    ("data race", "data race"),
    ("lock-order-inversion", "lock-order-inversion"),
    ("thread leak", "thread leak"),
    ("signal-unsafe", "signal-unsafe"),
    ("signal handler spoils errno", "signal-unsafe"),
)


def normalise_kind(text):
    low = text.lower()
    for needle, name in KIND_NAMES:
        if needle in low:
            return name
    return text.strip()


def repo_path(path):
    """Strip an absolute path down to its repository-relative part."""
    for d in REPO_DIRS:
        idx = path.find("/" + d)
        if idx >= 0:
            return path[idx + 1:]
        if path.startswith(d):
            return path
    return None


def parse_frame(line):
    """Return (fn, location, module, in_repo) for a stack frame line, else None."""
    m = FRAME.match(BUILD_ID.sub("", line))
    if not m:
        return None
    fn, loc, module = m.group(2), m.group(3), m.group(4) or ""
    loc = loc.strip()
    module = module.split(":")[0]          # "test_x:x86_64" -> "test_x"
    # Drop a trailing column number: file.c:12:5 -> file.c:12
    loc = re.sub(r"^(.*:\d+):\d+$", r"\1", loc)
    rel = repo_path(loc) if loc else None
    # Linux prints absolute paths (matched by repo_path); macOS prints bare
    # file names, so any symbolised frame outside the sanitizer runtime counts.
    ours = rel is not None or (bool(loc) and not loc.startswith("<null>")
                               and not RUNTIME_MODULE.search(module))
    return fn, (rel or loc), module, ours


def split_stacks(lines):
    """Split a report body into (header_line, [frames]) stacks."""
    stacks = []
    current = None
    for line in lines:
        f = parse_frame(line)
        if f:
            if current is not None:
                current[1].append(f)
            continue
        stripped = line.strip()
        if not stripped or stripped.startswith("SUMMARY:"):
            current = None
            continue
        current = (stripped, [])
        stacks.append(current)
    return stacks


def top_frame(frames):
    for fn, loc, module, in_repo in frames:
        if in_repo:
            return fn, loc, module
    if frames:
        fn, loc, module, _ = frames[0]
        return fn, loc, module
    return None


def describe(stack, thread_names):
    """'fn, file:line, thread' for a stack."""
    if stack is None:
        return "—", None, ""
    header, frames = stack
    tf = top_frame(frames)
    thread = ""
    m = THREAD_REF.search(header)
    if m:
        if m.group(1) == "main thread":
            thread = "main"
        else:
            tid = m.group(2)
            thread = tid + (" '" + thread_names[tid] + "'" if thread_names.get(tid) else "")
    if tf is None:
        return "—", None, ""
    fn, loc, module = tf
    text = "{}, {}".format(fn, loc)
    if thread:
        text += ", " + thread
    return text, (fn, loc), module


def parse_report(lines):
    kind = None
    for line in lines:
        m = WARNING.search(line)
        if m:
            kind = normalise_kind(m.group(1))
            break
    if kind is None:
        return None

    thread_names = {}
    location = None
    for line in lines:
        m = THREAD_DEF.match(line)
        if m and m.group(2):
            thread_names[m.group(1)] = m.group(2)
        m = LOCATION.match(line)
        if m and location is None:
            location = m.group(1)

    stacks = split_stacks(lines)
    # Access stacks: every stack except the location, thread-creation and
    # mutex-creation stacks, in report order.
    access = [s for s in stacks if s[1]
              and not s[0].startswith("Location is")
              and not re.match(r"^Thread T\d+.* created by", s[0])
              and not re.match(r"^Mutex M\d+ .*created at", s[0])]

    if kind == "thread leak":
        # The only stack of a leak report is the creation stack.
        access = [s for s in stacks if s[1]
                  and re.match(r"^Thread T\d+.* created by", s[0])]

    first = access[0] if access else None
    second = access[1] if len(access) > 1 else None
    w_text, w_key, w_mod = describe(first, thread_names)
    r_text, r_key, r_mod = describe(second, thread_names)

    loc_text = "—"
    if location:
        g = re.match(r"global '([^']+)'", location)
        if g:
            loc_text = "global `{}`".format(g.group(1))
        elif location.startswith("heap block"):
            # Use the first in-repo frame of the allocation stack.
            alloc = [s for s in stacks if s[0].startswith("Location is heap")]
            tf = top_frame(alloc[0][1]) if alloc and alloc[0][1] else None
            loc_text = "heap block alloc'd at {}".format(tf[1]) if tf else "heap block"
        else:
            loc_text = location.split(" at 0x")[0]
    elif w_key:
        loc_text = w_key[1]

    module = w_mod or r_mod
    return {
        "kind": kind,
        "loc": loc_text,
        "writer": w_text,
        "reader": r_text,
        "key": (kind, loc_text, w_key, r_key),
        "module": module,
    }


def read_reports(path):
    with open(path, errors="replace") as fh:
        text = fh.read().splitlines()
    report, inside = [], False
    for line in text:
        if SEPARATOR.match(line):
            if inside and report:
                yield report
            report, inside = [], not inside
            continue
        if inside:
            report.append(line)
    if inside and report:
        yield report


def main(argv):
    if len(argv) != 2:
        print("usage: tsan_summarise.py <dir-containing-tsan-files>")
        return 0
    files = sorted(glob.glob(os.path.join(argv[1], "tsan.*")))
    rows = OrderedDict()
    total = 0
    for path in files:
        for lines in read_reports(path):
            r = parse_report(lines)
            if r is None:
                continue
            total += 1
            if r["key"] not in rows:
                rows[r["key"]] = dict(r, count=0, modules=set())
            rows[r["key"]]["count"] += 1
            if r["module"]:
                rows[r["key"]]["modules"].add(r["module"])

    print("TSan report files: {}   reports: {}   distinct findings: {}".format(
        len(files), total, len(rows)))
    print()
    print("| # | Kind | Variable or location (file:line) | Writer frame | "
          "Reader/other frame | Count |")
    print("| - | ---- | -------------------------------- | ------------ | "
          "------------------ | ----- |")
    ordered = sorted(rows.values(), key=lambda r: (r["kind"], -r["count"]))
    for n, r in enumerate(ordered, 1):
        print("| {} | {} | {} | {} | {} | {} |".format(
            n, r["kind"], r["loc"], r["writer"], r["reader"], r["count"]))
    print()
    print("Binaries per row:")
    for n, r in enumerate(ordered, 1):
        print("  {:>3}: {}".format(n, ", ".join(sorted(r["modules"])) or "—"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
