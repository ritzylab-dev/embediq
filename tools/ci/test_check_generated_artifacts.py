#!/usr/bin/env python3
"""
tools/ci/test_check_generated_artifacts.py — self-test for check_generated_artifacts.py

Verifies that check_generated_artifacts.py compares the committed generated
files of an untouched checkout with a fresh generation in its own scratch
directory, and never writes into the checkout:
  - Exits 0 on a clean tree and prints the OK line with the output and source
    counts; the repository's own tree passes with 10 outputs from 6 sources.
  - Exits 1 and names the file for a stale message header, configuration
    header, configuration C file or Python stub, for outputs not regenerated
    after their schema changed, for a missing committed output and for a
    committed generated file no manifest entry lists.
  - Exits 2 for a new schema with no manifest entry, a missing or failing
    generator (its diagnostics shown), an unreadable schema, a generator that
    fails after partial output, one that omits a listed output or adds an
    unlisted one, every malformed manifest, and a source or output directory
    that resolves outside the root; drift is still reported next to an error.
  - Works from another working directory, under a path with a space, and in
    two concurrent runs (each with its own scratch directory, proven by a
    handshake, not timing); leaves no scratch directory behind.
  - Accepts only three generator argument lists — none, one explicit header
    name, or Python output — and rejects every other list (another output
    directory, `--opt=value`, abbreviations, repeats, a name with a path or
    not equal to the listed output) before any generator starts.
  - Treats a discovery directory it cannot list, and a matching entry it
    cannot inspect, that is a dangling link, resolves outside the root or is
    not a regular file, as an error — never as "no match"; listing and
    inspection failures are also injected in-process, so they do not depend
    on the user the test runs as.
  - The CI workflow verifies before anything else in each of its three jobs,
    and that first step, run on a stale fixture, fails without touching it;
    the local runner fails its generated-file step on a stale fixture, still
    runs its later steps, and touches nothing.
Every negative case hashes every file of its fixture before and after the run
and requires them to be identical.

Fixture roots are temporary directories with their own small generators that
follow the two real command lines (output content derived from the schema
bytes); a marker word in a schema makes its generator fail in a chosen way.

Run: python3 tools/ci/test_check_generated_artifacts.py
One PASS, FAIL or SKIP line per case. Exit 0 iff all pass.

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import contextlib
import hashlib
import importlib.util
import io
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
VERIFIER = os.path.join(HERE, "check_generated_artifacts.py")

if not os.path.isfile(VERIFIER):
    print("FAIL  check_generated_artifacts.py not found")
    sys.exit(1)

HANDSHAKE_ENV = "EMBEDIQ_TEST_GENERATOR_HANDSHAKE"
HANG_GUARD_S = 60          # bound on any wait; never a correctness oracle
REAL_OK_LINE = ("check_generated_artifacts.py: OK — 10 output(s) from 6 source(s) "
                "match generator output.")

_failed = []
_count = 0

# ---------------------------------------------------------------------------
# Fixture generators — same command lines as the real ones
# ---------------------------------------------------------------------------

_GEN_COMMON = r'''
import argparse, hashlib, os, sys, time

ran = os.environ.get("EMBEDIQ_TEST_GENERATOR_RAN")
if ran:                                   # proves whether a generator started at all
    with open(ran, "a") as fh:
        fh.write(" ".join(sys.argv[1:]) + "\n")

def handshake(out_dir):
    d = os.environ.get("EMBEDIQ_TEST_GENERATOR_HANDSHAKE")
    if not d:
        return
    with open(os.path.join(d, "marker-%d" % os.getpid()), "w") as fh:
        fh.write(out_dir)
    t0 = time.time()
    while not os.path.exists(os.path.join(d, "release")):
        if time.time() - t0 > 60:
            sys.exit(9)
        time.sleep(0.01)

def body(schema_bytes, name):
    return ("generated %s from %s\n" % (name, hashlib.sha256(schema_bytes).hexdigest())).encode()

def read_schema(path):
    try:
        with open(path, "rb") as fh:
            return fh.read()
    except OSError as e:
        sys.stderr.write("ERROR: cannot read %s: %s\n" % (path, e))
        sys.exit(1)

'''

# The same argument parser as the real message generator: options may be
# abbreviated, given as --opt=value, and repeated (the last one wins).
MSG_GEN = _GEN_COMMON + r'''
ap = argparse.ArgumentParser()
ap.add_argument("schema")
ap.add_argument("--out", required=True)
ap.add_argument("--output-name", default=None)
ap.add_argument("--lang", choices=["c", "python"], default="c")
a = ap.parse_args()
schema, out = a.schema, a.out
handshake(out)
data = read_schema(schema)
if b"FAIL3" in data:
    sys.stderr.write("fixture generator failure for %s\n" % schema)
    sys.exit(3)
stem = os.path.splitext(os.path.basename(schema))[0]
if a.lang == "python":
    name = a.output_name or stem + "_msgs.py"
else:
    name = a.output_name or stem + "_msg_catalog.h"
os.makedirs(out, exist_ok=True)
with open(os.path.join(out, name), "wb") as fh:
    fh.write(body(data, name))
'''

# The same argument parser as the real configuration generator.
CFG_GEN = _GEN_COMMON + r'''
ap = argparse.ArgumentParser()
ap.add_argument("schema")
ap.add_argument("--out", required=True)
a = ap.parse_args()
schema, out = a.schema, a.out
handshake(out)
data = read_schema(schema)
os.makedirs(out, exist_ok=True)
names = ["embediq_cfg_validate.h", "embediq_cfg_generated.h", "embediq_cfg_validate.c"]
for n, name in enumerate(names):
    if b"OMIT" in data and n == 2:
        break
    with open(os.path.join(out, name), "wb") as fh:
        fh.write(body(data, name))
    if b"PARTIAL" in data:
        sys.stderr.write("fixture generator stopped after %s\n" % name)
        sys.exit(1)
if b"EXTRA" in data:
    with open(os.path.join(out, "extra_unlisted.h"), "wb") as fh:
        fh.write(b"extra\n")
'''

PY_DIR = "py/msgs"

MANIFEST = {
    "version": 1,
    "artifacts": [
        {"generator": "tools/messages_iq/generate.py", "source": "messages/core.iq",
         "args": ["--output-name", "embediq_msg_catalog.h"], "out_dir": "generated",
         "outputs": ["embediq_msg_catalog.h"]},
        {"generator": "tools/messages_iq/generate.py", "source": "messages/telemetry.iq",
         "args": ["--output-name", "telemetry_msg_catalog.h"], "out_dir": "generated",
         "outputs": ["telemetry_msg_catalog.h"]},
        {"generator": "tools/config_iq/generate.py", "source": "config/config.iq",
         "args": [], "out_dir": "generated",
         "outputs": ["embediq_cfg_validate.h", "embediq_cfg_generated.h",
                     "embediq_cfg_validate.c"]},
        {"generator": "tools/messages_iq/generate.py", "source": "messages/core.iq",
         "args": ["--lang", "python"], "out_dir": PY_DIR, "outputs": ["core_msgs.py"]},
    ],
    "sources_must_be_mapped": [
        {"glob": "messages/*.iq", "out_dir": "generated"},
        {"glob": "config/*.iq", "out_dir": "generated"},
    ],
    "outputs_must_be_mapped": ["generated/*.h", "generated/*.c", PY_DIR + "/*_msgs.py"],
}
FIXTURE_OUTPUTS = 6
FIXTURE_SOURCES = 3


def _write(root, rel, content, mode="w"):
    path = os.path.join(root, *rel.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, mode) as fh:
        fh.write(content)
    return path


def make_fixture(parent, name="fixture", with_verifier=False, manifest=None):
    """A clean fixture root: generators, schemas, manifest, committed outputs
    produced by the fixture generators themselves."""
    root = os.path.join(parent, name)
    os.makedirs(root)
    _write(root, "tools/messages_iq/generate.py", MSG_GEN)
    _write(root, "tools/config_iq/generate.py", CFG_GEN)
    _write(root, "messages/core.iq", "version 1\nmessage CORE id=0x0001 {}\n")
    _write(root, "messages/telemetry.iq", "version 1\nmessage TEL id=0x0500 {}\n")
    _write(root, "config/config.iq", "version 1\nkey \"a\" type=u8\n")
    _write(root, "generated/.gitkeep", "")
    _write(root, PY_DIR + "/__init__.py", "# hand-written\n")
    _write(root, "tools/ci/generated_artifacts.json",
           json.dumps(manifest if manifest is not None else MANIFEST, indent=2))
    for art in MANIFEST["artifacts"]:
        out = os.path.join(root, *art["out_dir"].split("/"))
        subprocess.run([sys.executable, art["generator"], art["source"], "--out", out]
                       + art["args"], cwd=root, check=True, capture_output=True)
    if with_verifier:
        shutil.copy(VERIFIER, os.path.join(root, "tools", "ci", "check_generated_artifacts.py"))
    return root


def tree_hash(root):
    """SHA-256 of every regular file under root (relative path → digest);
    links are recorded by their target and other entries by their type, so
    a FIFO is never opened and a link is never followed."""
    out = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for f in sorted(filenames + [d for d in dirnames
                                     if os.path.islink(os.path.join(dirpath, d))]):
            p = os.path.join(dirpath, f)
            rel = os.path.relpath(p, root)
            st = os.lstat(p)
            if stat.S_ISLNK(st.st_mode):
                out[rel] = "link:" + os.readlink(p)
            elif not stat.S_ISREG(st.st_mode):
                out[rel] = "type:%o" % stat.S_IFMT(st.st_mode)
            else:
                try:
                    with open(p, "rb") as fh:
                        out[rel] = hashlib.sha256(fh.read()).hexdigest()
                except OSError:
                    out[rel] = "unreadable"
    return out


def run_verifier(root, extra=(), cwd=None, env=None, verifier=VERIFIER):
    result = subprocess.run([sys.executable, verifier, "--root", root, *extra],
                            cwd=cwd, env=env, capture_output=True, text=True,
                            timeout=HANG_GUARD_S)
    return result.returncode, result.stdout + result.stderr


def case(name, ok, detail=""):
    global _count
    _count += 1
    if ok:
        print(f"PASS  {name}")
    else:
        print(f"FAIL  {name}" + (f" — {detail}" if detail else ""))
        _failed.append(name)


def negative(name, parent, mutate, want_rc, want_texts=(), reject_texts=(), env=None):
    """Fresh fixture, mutate it, run the verifier, require want_rc, every text
    in want_texts, none of reject_texts, and an unchanged fixture."""
    root = make_fixture(parent, name.replace(" ", "_"))
    mutate(root)
    before = tree_hash(root)
    rc, out = run_verifier(root, env=env)
    after = tree_hash(root)
    problems = []
    if rc != want_rc:
        problems.append(f"exit {rc}, want {want_rc}")
    problems += [f"missing {t!r}" for t in want_texts if t not in out]
    problems += [f"unexpected {t!r}" for t in reject_texts if t in out]
    if before != after:
        problems.append("fixture changed")
    case(name, not problems, "; ".join(problems) + f"\n{out}")
    return rc, out


def append(root, rel, text):
    with open(os.path.join(root, *rel.split("/")), "a") as fh:
        fh.write(text)


# ---------------------------------------------------------------------------
# Cases
# ---------------------------------------------------------------------------

def scratch_left(tmpdir):
    return [d for d in os.listdir(tmpdir) if d.startswith("embediq-generated-")]


def test_comparisons(parent):
    own_tmp = os.path.join(parent, "own-tmp")
    os.makedirs(own_tmp)
    env_tmp = dict(os.environ, TMPDIR=own_tmp)
    leftovers = []

    root = make_fixture(parent, "clean")
    before = tree_hash(root)
    rc, out = run_verifier(root, env=env_tmp)
    leftovers += scratch_left(own_tmp)
    ok_line = (f"check_generated_artifacts.py: OK — {FIXTURE_OUTPUTS} output(s) from "
               f"{FIXTURE_SOURCES} source(s) match generator output.")
    case("clean tree passes with the OK line", rc == 0 and ok_line in out
         and tree_hash(root) == before, out)

    negative("stale message header is drift", parent,
             lambda r: append(r, "generated/embediq_msg_catalog.h", "/* edited */\n"), 1,
             ["DRIFT  generated/embediq_msg_catalog.h: differs from generator output"],
             env=env_tmp)
    leftovers += scratch_left(own_tmp)
    negative("stale configuration header is drift", parent,
             lambda r: append(r, "generated/embediq_cfg_generated.h", "x\n"), 1,
             ["DRIFT  generated/embediq_cfg_generated.h"])
    negative("stale configuration C file is drift", parent,
             lambda r: append(r, "generated/embediq_cfg_validate.c", "x\n"), 1,
             ["DRIFT  generated/embediq_cfg_validate.c"])
    negative("stale Python stub is drift", parent,
             lambda r: append(r, PY_DIR + "/core_msgs.py", "x\n"), 1,
             [f"DRIFT  {PY_DIR}/core_msgs.py"])
    negative("schema changed without regeneration is drift for each of its outputs", parent,
             lambda r: append(r, "messages/core.iq", "message NEW id=0x0002 {}\n"), 1,
             ["DRIFT  generated/embediq_msg_catalog.h", f"DRIFT  {PY_DIR}/core_msgs.py"],
             ["DRIFT  generated/telemetry_msg_catalog.h"])
    negative("missing committed output is drift", parent,
             lambda r: os.remove(os.path.join(r, "generated", "telemetry_msg_catalog.h")), 1,
             ["DRIFT  generated/telemetry_msg_catalog.h: committed file missing"])
    negative("new schema without a manifest entry is an error", parent,
             lambda r: _write(r, "messages/extra.iq", "version 1\n"), 2,
             ["ERROR  messages/extra.iq: source has no manifest entry"])
    negative("committed generated file without a manifest entry is drift", parent,
             lambda r: _write(r, "generated/orphan.h", "/* orphan */\n"), 1,
             ["DRIFT  generated/orphan.h: committed file has no manifest entry"])
    negative("missing generator is an error", parent,
             lambda r: os.remove(os.path.join(r, "tools", "config_iq", "generate.py")), 2,
             ["ERROR  tools/config_iq/generate.py: cannot read"])
    negative("failing generator is an error and its diagnostics are shown", parent,
             lambda r: append(r, "messages/telemetry.iq", "FAIL3\n"), 2,
             ["ERROR  messages/telemetry.iq: generator failed (exit 3)",
              "fixture generator failure for messages/telemetry.iq"],
             env=env_tmp)
    leftovers += scratch_left(own_tmp)

    if os.geteuid() == 0:
        print("SKIP  unreadable schema is an error  (running as root)")
    else:
        holder = {}

        def unreadable(r):
            p = os.path.join(r, "config", "config.iq")
            os.chmod(p, 0)
            holder["p"] = p
        try:
            negative("unreadable schema is an error", parent, unreadable, 2,
                     ["ERROR  config/config.iq: cannot read"])
        finally:
            if "p" in holder:
                os.chmod(holder["p"], stat.S_IRUSR | stat.S_IWUSR)

    negative("generator failing after partial output is an error, nothing it wrote passes",
             parent, lambda r: append(r, "config/config.iq", "PARTIAL\n"), 2,
             ["ERROR  config/config.iq: generator failed (exit 1)"],
             ["OK     generated/embediq_cfg_validate.h", "OK     generated/embediq_cfg_generated.h",
              "OK     generated/embediq_cfg_validate.c"])
    negative("generator omitting a listed output is an error", parent,
             lambda r: append(r, "config/config.iq", "OMIT\n"), 2,
             ["ERROR  generated/embediq_cfg_validate.c: generator did not produce it"])
    negative("generator producing an unlisted output is an error", parent,
             lambda r: append(r, "config/config.iq", "EXTRA\n"), 2,
             ["ERROR  config/config.iq: generator produced unlisted extra_unlisted.h"])

    def drift_and_failure(r):
        append(r, "generated/embediq_msg_catalog.h", "/* edited */\n")
        append(r, "messages/telemetry.iq", "FAIL3\n")
    negative("drift is still reported next to a generator error", parent, drift_and_failure, 2,
             ["DRIFT  generated/embediq_msg_catalog.h", "generator failed (exit 3)"])

    case("no scratch directory is left behind", not leftovers and not os.listdir(own_tmp),
         f"left: {os.listdir(own_tmp)}")


def test_manifest_errors(parent):
    def with_manifest(text):
        def mutate(r):
            _write(r, "tools/ci/generated_artifacts.json", text)
        return mutate

    def edited(fn):
        m = json.loads(json.dumps(MANIFEST))
        fn(m)
        return json.dumps(m)

    subs = [
        ("not JSON", "{ not json"),
        ("version 2", edited(lambda m: m.__setitem__("version", 2))),
        ("missing outputs", edited(lambda m: m["artifacts"][0].pop("outputs"))),
        ("absolute path", edited(lambda m: m["artifacts"][0].__setitem__(
            "source", "/etc/core.iq"))),
        ("parent-directory component", edited(lambda m: m["artifacts"][0].__setitem__(
            "out_dir", "generated/../generated"))),
        ("duplicate output", edited(lambda m: m["artifacts"][1].__setitem__(
            "args", ["--output-name", "embediq_msg_catalog.h"]) or m["artifacts"][1].__setitem__(
            "outputs", ["embediq_msg_catalog.h"]))),
    ]
    for label, text in subs:
        negative(f"malformed manifest is an error: {label}", parent, with_manifest(text), 2,
                 ["ERROR  manifest:"])


def test_path_escapes(parent):
    outside = os.path.join(parent, "outside")
    os.makedirs(outside)
    with open(os.path.join(outside, "outside.iq"), "w") as fh:
        fh.write("version 1\n")
    os.makedirs(os.path.join(outside, "pystubs"))
    with open(os.path.join(outside, "pystubs", "core_msgs.py"), "w") as fh:
        fh.write("x\n")

    def source_link(r):
        os.symlink(os.path.join(outside, "outside.iq"), os.path.join(r, "messages", "outside.iq"))
        m = json.loads(json.dumps(MANIFEST))
        m["artifacts"].append({"generator": "tools/messages_iq/generate.py",
                               "source": "messages/outside.iq",
                               "args": ["--output-name", "outside_msg_catalog.h"],
                               "out_dir": "generated", "outputs": ["outside_msg_catalog.h"]})
        _write(r, "tools/ci/generated_artifacts.json", json.dumps(m))
    negative("source linked outside the root is an error", parent, source_link, 2,
             ["ERROR  messages/outside.iq: resolves outside the root"])

    def out_dir_link(r):
        os.symlink(os.path.join(outside, "pystubs"), os.path.join(r, "pyout"))
        m = json.loads(json.dumps(MANIFEST))
        m["artifacts"][3]["out_dir"] = "pyout"
        m["outputs_must_be_mapped"][2] = "pyout/*_msgs.py"
        _write(r, "tools/ci/generated_artifacts.json", json.dumps(m))
    negative("output directory linked outside the root is an error", parent, out_dir_link, 2,
             ["ERROR  pyout: resolves outside the root"])


def test_invocation(parent):
    spaced = os.path.join(parent, "with space")
    os.makedirs(spaced)
    root = make_fixture(spaced)
    rc, out = run_verifier(root)
    case("root path containing a space passes", rc == 0, out)

    elsewhere = os.path.join(parent, "elsewhere")
    os.makedirs(elsewhere)
    root = make_fixture(parent, "from-elsewhere")
    rc, out = run_verifier(root, cwd=elsewhere)
    case("run from another working directory passes", rc == 0, out)

    rc, out = run_verifier(root, extra=("--bogus",))
    case("unknown option is a usage error", rc == 2 and "usage" in out.lower(), out)


def test_concurrent_runs(parent):
    root = make_fixture(parent, "concurrent")
    shake = os.path.join(parent, "handshake")
    os.makedirs(shake)
    env = dict(os.environ, **{HANDSHAKE_ENV: shake})
    procs = [subprocess.Popen([sys.executable, VERIFIER, "--root", root], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
             for _ in range(2)]
    markers = []
    t0 = time.time()
    while time.time() - t0 < HANG_GUARD_S:
        markers = [m for m in os.listdir(shake) if m.startswith("marker-")]
        if len(markers) >= 2:
            break
        time.sleep(0.01)
    scratch = set()
    for m in markers:
        with open(os.path.join(shake, m)) as fh:
            scratch.add(os.path.dirname(os.path.normpath(fh.read())))
    open(os.path.join(shake, "release"), "w").close()
    results = [p.communicate(timeout=HANG_GUARD_S) for p in procs]
    rcs = [p.returncode for p in procs]
    case("two concurrent runs use separate scratch directories and both pass",
         len(markers) >= 2 and len(scratch) == 2 and rcs == [0, 0],
         f"markers={len(markers)} scratch={scratch} rcs={rcs} {results}")


def test_real_tree():
    rc, out = run_verifier(REPO)
    case("the repository's tree passes: 10 outputs from 6 sources",
         rc == 0 and out.strip().splitlines()[-1:] == [REAL_OK_LINE], out)


# ---------------------------------------------------------------------------
# Entry-point order: the CI workflow and the local runner
# ---------------------------------------------------------------------------

def workflow_first_scripts(text):
    """Map job name → its steps' scripts (None for a step without one)."""
    lines = text.splitlines()
    try:
        start = lines.index("jobs:")
    except ValueError:
        return {}
    jobs, job, step, block = {}, None, None, None
    for line in lines[start + 1:]:
        m = re.match(r"^  ([a-z][a-z0-9-]*):$", line)
        if m:
            job = m.group(1)
            jobs[job] = []
            step, block = None, None
            continue
        if job is None:
            continue
        if re.match(r"^      - ", line):
            step = {"script": None}
            jobs[job].append(step)
            block = None
            continue
        if step is None:
            continue
        if block is not None:
            if line.startswith(" " * 10) or line.strip() == "":
                block.append(line[10:])
                step["script"] = "\n".join(block).strip("\n")
                continue
            block = None
        m = re.match(r"^        run: (.+)$", line)
        if m:
            if m.group(1) == "|":
                block = []
            else:
                step["script"] = m.group(1)
    return jobs


def stale_fixture(parent, name):
    root = make_fixture(parent, name, with_verifier=True)
    append(root, "generated/embediq_msg_catalog.h", "/* edited */\n")
    return root


def test_workflow_order(parent):
    with open(os.path.join(REPO, ".github", "workflows", "ci.yml")) as fh:
        text = fh.read()
    jobs = workflow_first_scripts(text)
    problems = []
    if sorted(jobs) != ["build", "release-symbols", "tsan"]:
        problems.append(f"jobs parsed: {sorted(jobs)}")
    if "generate.py" in text:
        problems.append("the workflow still runs a generator")
    for name in sorted(jobs):
        scripts = [s["script"] for s in jobs[name] if s["script"]]
        if not scripts:
            problems.append(f"{name}: no step with a script")
            continue
        first = scripts[0]
        if first != "python3 tools/ci/check_generated_artifacts.py":
            problems.append(f"{name}: first script is {first.splitlines()[0]!r}")
        root = stale_fixture(parent, f"workflow-{name}")
        before = tree_hash(root)
        r = subprocess.run(["bash", "-e", "-c", first], cwd=root, capture_output=True,
                           text=True, timeout=HANG_GUARD_S)
        if r.returncode != 1:
            problems.append(f"{name}: first step on a stale fixture exits {r.returncode}")
        if tree_hash(root) != before:
            problems.append(f"{name}: first step changed the fixture")
    case("each CI job verifies first and refuses a stale file without touching it",
         bool(jobs) and not problems, "; ".join(problems))


def test_local_runner_order(parent):
    root = stale_fixture(parent, "local-runner")
    shutil.copytree(os.path.join(REPO, "scripts"), os.path.join(root, "scripts"),
                    ignore=shutil.ignore_patterns("*.pyc", "__pycache__"))
    markers = os.path.join(parent, "runner-markers")
    stubs = os.path.join(parent, "runner-stubs")
    os.makedirs(markers)
    os.makedirs(stubs)
    for tool in ("cmake", "ctest"):
        p = os.path.join(stubs, tool)
        with open(p, "w") as fh:
            fh.write(f'#!/bin/sh\ntouch "$RUNNER_MARKERS/{tool}"\nexit 0\n')
        os.chmod(p, 0o755)
    _write(root, "tools/ci/static_checks.sh", '#!/bin/sh\ntouch "$RUNNER_MARKERS/static"\nexit 0\n')
    subprocess.run(["git", "init", "-q"], cwd=root, check=True)
    before = tree_hash(root)
    env = dict(os.environ, PATH=stubs + os.pathsep + os.environ.get("PATH", ""),
               RUNNER_MARKERS=markers)
    r = subprocess.run(["bash", "scripts/check.sh"], cwd=root, env=env, capture_output=True,
                       text=True, timeout=HANG_GUARD_S)
    out = r.stdout + r.stderr
    problems = []
    if r.returncode == 0:
        problems.append("exit 0")
    if "❌ Generated files match their sources" not in out:
        problems.append("the summary does not name the generated-file step as failed")
    if sorted(os.listdir(markers)) != ["cmake", "ctest", "static"]:
        problems.append(f"later steps ran: {sorted(os.listdir(markers))}")
    if tree_hash(root) != before:
        problems.append("the fixture changed (a generator wrote over it)")
    case("the local runner refuses a stale file, keeps going, and touches nothing",
         not problems, "; ".join(problems) + f"\n{out}")


# ---------------------------------------------------------------------------
# Generator arguments and discovery boundaries
# ---------------------------------------------------------------------------

_serial = [0]


def edit_manifest(root, fn):
    path = os.path.join(root, "tools", "ci", "generated_artifacts.json")
    with open(path) as fh:
        m = json.load(fh)
    fn(m)
    with open(path, "w") as fh:
        json.dump(m, fh)


def load_verifier():
    spec = importlib.util.spec_from_file_location("generated_artifacts_under_test", VERIFIER)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def run_in_process(root, patches):
    """Run the verifier's main() in this process with module names replaced."""
    mod = load_verifier()
    for name, value in patches.items():
        setattr(mod, name, value)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = mod.main([os.path.basename(VERIFIER), "--root", root])
    return rc, buf.getvalue()


def boundary(name, parent, mutate, want_rc, want_texts=(), reject_texts=(), stale=False,
             must_not_run=False, around=None, patches=None):
    """Fresh fixture (optionally with a stale committed header), mutate it,
    hash it, run the verifier — as a process, or in-process with patches —
    then require want_rc, the texts, an unchanged fixture and, when asked,
    that no generator started."""
    _serial[0] += 1
    tag = f"b{_serial[0]:02d}"
    root = make_fixture(parent, tag)
    if stale:
        append(root, "generated/embediq_msg_catalog.h", "/* edited */\n")
    mutate(root)
    before = tree_hash(root)
    ran = os.path.join(parent, tag + ".ran")
    env = dict(os.environ, EMBEDIQ_TEST_GENERATOR_RAN=ran)
    if around:
        around[0](root)
    try:
        if patches is not None:
            rc, out = run_in_process(root, patches(root))
        else:
            rc, out = run_verifier(root, env=env)
    finally:
        if around:
            around[1](root)
    after = tree_hash(root)
    problems = []
    if rc != want_rc:
        problems.append(f"exit {rc}, want {want_rc}")
    problems += [f"missing {t!r}" for t in want_texts if t not in out]
    problems += [f"unexpected {t!r}" for t in reject_texts if t in out]
    if before != after:
        problems.append("fixture changed")
    if must_not_run and os.path.exists(ran):
        problems.append("a generator ran")
    case(name, not problems, "; ".join(problems) + f"\n{out}")


UNSUPPORTED = "unsupported 'args'"
PLAIN_NAME = "'--output-name' must be a plain file name equal to the single listed output"
DO_NOT_FIT = "'args' do not fit the listed outputs"


def set_args(n, args, outputs=None):
    def mutate(root):
        def fn(m):
            m["artifacts"][n]["args"] = args
            if outputs is not None:
                m["artifacts"][n]["outputs"] = outputs
        edit_manifest(root, fn)
    return mutate


def test_generator_arguments(parent):
    boundary("an extra --out in a manifest entry is refused before any generator runs",
             parent, set_args(0, ["--output-name", "embediq_msg_catalog.h", "--out", "generated"]),
             2, [UNSUPPORTED], stale=True, must_not_run=True)
    for args in (["--out=generated"], ["--o", "generated"], ["--output-name=x.h"],
                 ["--lang=python"], ["--output-name", "a.h", "--output-name", "b.h"],
                 ["--lang", "python", "--lang", "c"], ["--unknown"], ["python", "--lang"]):
        boundary(f"argument list {args} is refused before any generator runs", parent,
                 set_args(0, args), 2, [UNSUPPORTED], stale=True, must_not_run=True)

    def absolute_name(root):
        set_args(0, ["--output-name",
                      os.path.join(root, "generated", "embediq_msg_catalog.h")])(root)
    boundary("an absolute --output-name is refused before any generator runs", parent,
             absolute_name, 2, [PLAIN_NAME], stale=True, must_not_run=True)
    for name in ("../x.h", "sub/x.h", "..", "-x.h", ""):
        boundary(f"--output-name {name!r} is refused", parent,
                 set_args(0, ["--output-name", name]), 2, [PLAIN_NAME],
                 stale=True, must_not_run=True)
    boundary("--output-name not equal to the listed output is refused", parent,
             set_args(0, ["--output-name", "other.h"]), 2, [PLAIN_NAME],
             must_not_run=True)
    boundary("an explicit name on a three-output entry is refused", parent,
             set_args(2, ["--output-name", "embediq_cfg_validate.h"]), 2, [DO_NOT_FIT],
             must_not_run=True)
    boundary("Python output on a three-output entry is refused", parent,
             set_args(2, ["--lang", "python"]), 2, [DO_NOT_FIT], must_not_run=True)
    boundary("Python output with a name other than <stem>_msgs.py is refused", parent,
             set_args(3, ["--lang", "python"], ["other_msgs.py"]), 2, [DO_NOT_FIT],
             must_not_run=True)

    def default_name(root):
        set_args(1, [], ["telemetry_msg_catalog.h"])(root)
    boundary("the three accepted forms pass: default header name, explicit header name, "
             "Python output, three-output configuration", parent, default_name, 0,
             ["OK     generated/telemetry_msg_catalog.h", "OK     generated/embediq_msg_catalog.h",
              f"OK     {PY_DIR}/core_msgs.py", "OK     generated/embediq_cfg_validate.c"])


def _chmod(rel, mode):
    return (lambda r: os.chmod(os.path.join(r, *rel.split("/")), mode),
            lambda r: os.chmod(os.path.join(r, *rel.split("/")), 0o755))


def _failing(fn, suffix, exc=PermissionError):
    def wrapped(path, *a, **k):
        if os.path.normpath(str(path)).endswith(os.path.normpath(suffix)):
            raise exc(13, "injected", str(path))
        return fn(path, *a, **k)
    return wrapped


def test_discovery_boundaries(parent):
    if os.geteuid() == 0:
        print("SKIP  unlistable schema directory is an error  (running as root)")
        print("SKIP  unlistable output directory is an error  (running as root)")
    else:
        boundary("unlistable schema directory is an error; known comparisons still run", parent,
                 lambda r: _write(r, "messages/extra.iq", "version 1\n"), 2,
                 ["ERROR  messages: cannot list", "OK     generated/embediq_msg_catalog.h"],
                 around=_chmod("messages", 0o111))
        boundary("unlistable output directory is an error; listed outputs still compare", parent,
                 lambda r: None, 2,
                 ["ERROR  generated: cannot list", "OK     generated/embediq_msg_catalog.h"],
                 around=_chmod("generated", 0o111))

    boundary("dangling link matching the schema pattern is an error", parent,
             lambda r: os.symlink("missing-target", os.path.join(r, "messages", "extra.iq")), 2,
             ["ERROR  messages/extra.iq: dangling link"])
    boundary("dangling link matching an output pattern is an error", parent,
             lambda r: os.symlink("missing-target", os.path.join(r, "generated", "orphan.h")), 2,
             ["ERROR  generated/orphan.h: dangling link"])

    for rel in ("messages", "generated"):
        boundary(f"injected listing failure for {rel}/ is an error", parent, lambda r: None, 2,
                 [f"ERROR  {rel}: cannot list"],
                 patches=lambda r, rel=rel: {"_listdir": _failing(os.listdir, os.path.join(r, rel))})
    boundary("injected lstat failure on a matching entry is an error", parent, lambda r: None, 2,
             ["ERROR  messages/telemetry.iq: cannot inspect"],
             patches=lambda r: {"_lstat": _failing(os.lstat, "messages/telemetry.iq")})
    boundary("injected stat failure on a matching entry is an error", parent, lambda r: None, 2,
             ["ERROR  generated/embediq_cfg_validate.c: cannot inspect"],
             patches=lambda r: {"_stat": _failing(os.stat, "generated/embediq_cfg_validate.c")})

    boundary("directory matching the schema pattern is an error", parent,
             lambda r: os.mkdir(os.path.join(r, "messages", "dir.iq")), 2,
             ["ERROR  messages/dir.iq: not a regular file"])
    if hasattr(os, "mkfifo"):
        boundary("FIFO matching an output pattern is an error", parent,
                 lambda r: os.mkfifo(os.path.join(r, "generated", "fifo.h")), 2,
                 ["ERROR  generated/fifo.h: not a regular file"])
    else:
        print("SKIP  FIFO matching an output pattern is an error  (no mkfifo)")

    boundary("link to a regular file inside the root, listed by no entry, is drift", parent,
             lambda r: os.symlink("../config/config.iq", os.path.join(r, "generated", "linked.h")),
             1, ["DRIFT  generated/linked.h: committed file has no manifest entry"])
    outside = os.path.join(parent, "outside-target.h")
    with open(outside, "w") as fh:
        fh.write("outside\n")
    boundary("link matching an output pattern that resolves outside the root is an error",
             parent, lambda r: os.symlink(outside, os.path.join(r, "generated", "escape.h")), 2,
             ["ERROR  generated/escape.h: resolves outside the root"])

    def schema_link(r):
        os.makedirs(os.path.join(r, "schemas"))
        os.rename(os.path.join(r, "messages", "telemetry.iq"),
                  os.path.join(r, "schemas", "telemetry.iq"))
        os.symlink("../schemas/telemetry.iq", os.path.join(r, "messages", "telemetry.iq"))
    boundary("listed schema that links to a file inside the root is accepted", parent,
             schema_link, 0, ["OK     generated/telemetry_msg_catalog.h"])

    boundary("listing failure next to real drift: error, and the drift is still reported",
             parent, lambda r: append(r, "generated/telemetry_msg_catalog.h", "x\n"), 2,
             ["ERROR  messages: cannot list", "DRIFT  generated/telemetry_msg_catalog.h"],
             patches=lambda r: {"_listdir": _failing(os.listdir, os.path.join(r, "messages"))})

    for label, pattern in (("wildcard in the directory part", "mess*/x.iq"),
                           ("wildcard directory level", "messages/*/x.iq"),
                           ("no directory part", "x.iq")):
        def mutate(r, pattern=pattern):
            edit_manifest(r, lambda m: m["sources_must_be_mapped"].append(
                {"glob": pattern, "out_dir": "generated"}))
        boundary(f"discovery pattern of the wrong shape is a manifest error: {label}", parent,
                 mutate, 2, ["ERROR  manifest:"], must_not_run=True)


def main():
    parent = tempfile.mkdtemp(prefix="embediq-generated-selftest-")
    try:
        test_comparisons(parent)
        test_manifest_errors(parent)
        test_path_escapes(parent)
        test_invocation(parent)
        test_concurrent_runs(parent)
        test_real_tree()
        test_workflow_order(parent)
        test_local_runner_order(parent)
        test_generator_arguments(parent)
        test_discovery_boundaries(parent)
    finally:
        shutil.rmtree(parent, ignore_errors=True)

    print()
    if _failed:
        print(f"FAILED: {len(_failed)} of {_count} tests: {', '.join(_failed)}")
        return 1
    print(f"All {_count} tests passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
