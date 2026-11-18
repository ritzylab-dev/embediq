#!/usr/bin/env python3
"""
tools/ci/check_generated_artifacts.py — every committed generated file equals what its generator produces

Reads the manifest (tools/ci/generated_artifacts.json): one entry per
generator run, naming the generator, its source schema, its extra arguments,
the directory its outputs are committed in and the exact output file names.
Every entry is run into its own sub-directory of one scratch directory this
run creates and removes; the committed files of the checkout are compared
byte for byte with the fresh ones and are never written. Each generator runs
with the root as its working directory and the source path exactly as the
manifest writes it, as a developer regenerating by hand does.

The manifest also declares discovery rules: every schema matching a
`sources_must_be_mapped` glob must be the source of an entry with that output
directory (a new catalog cannot be skipped silently), and every file matching
an `outputs_must_be_mapped` glob must be a listed output.

Usage:
  python3 tools/ci/check_generated_artifacts.py [--root DIR] [--manifest FILE]
  Defaults: the repository containing this script, and
  <root>/tools/ci/generated_artifacts.json. Works from any directory and in a
  source export; Git is not used.

Output: one line per comparison — "OK     <file>", "DRIFT  <file>: <reason>"
(with at most 20 lines of unified diff, committed → fresh) or
"ERROR  <path>: <reason>" (with a failing generator's output) — then one
summary line. Every entry and output is processed even after a failure.

Exit codes: 0 = every listed output matches; 1 = drift — a committed output
is stale or missing, or a committed generated file has no manifest entry;
2 = the comparison could not be completed — an unusable manifest, a source
with no manifest entry, a path that resolves outside the root, an unreadable
generator, schema or committed file, a generator that fails, omits a listed
output or writes an unlisted one. 2 takes precedence over 1; a partial run is
never a pass.

@author  Ritesh Anand
@company embediq.com | ritzylab.com

SPDX-License-Identifier: Apache-2.0
"""

import argparse
import difflib
import glob
import json
import os
import posixpath
import shutil
import subprocess
import sys
import tempfile

SCRIPT = "check_generated_artifacts.py"
DIFF_LINES_MAX = 20   # enough to show where a file went stale without flooding the log


class ManifestError(Exception):
    pass


def _bad_path(p):
    """Why the manifest path p is not acceptable, or None."""
    if not isinstance(p, str) or p == "":
        return "empty or non-string path"
    if p.startswith("/") or os.path.isabs(p):
        return f"absolute path {p!r}"
    if ".." in p.replace("\\", "/").split("/"):
        return f"'..' component in {p!r}"
    return None


def _require(obj, key, kind, where):
    if not isinstance(obj, dict) or key not in obj:
        raise ManifestError(f"{where}: missing {key!r}")
    value = obj[key]
    if kind is int:
        ok = type(value) is int
    else:
        ok = isinstance(value, kind)
    if not ok:
        raise ManifestError(f"{where}: {key!r} has the wrong type")
    return value


def load_manifest(path):
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
    except (OSError, UnicodeDecodeError) as e:
        raise ManifestError(f"cannot read {path}: {e}")
    except json.JSONDecodeError as e:
        raise ManifestError(f"not JSON: {e}")
    if not isinstance(data, dict):
        raise ManifestError("not a JSON object")
    if _require(data, "version", int, "manifest") != 1:
        raise ManifestError(f"version {data['version']!r} is not 1")

    artifacts = _require(data, "artifacts", list, "manifest")
    if not artifacts:
        raise ManifestError("'artifacts' is empty")
    seen = set()
    for n, art in enumerate(artifacts):
        where = f"artifact {n}"
        if not isinstance(art, dict):
            raise ManifestError(f"{where}: not an object")
        for key in ("generator", "source", "out_dir"):
            why = _bad_path(_require(art, key, str, where))
            if why:
                raise ManifestError(f"{where}: {key}: {why}")
        args = _require(art, "args", list, where)
        if not all(isinstance(a, str) for a in args):
            raise ManifestError(f"{where}: 'args' must be strings")
        outputs = _require(art, "outputs", list, where)
        if not outputs:
            raise ManifestError(f"{where}: 'outputs' is empty")
        for name in outputs:
            why = _bad_path(name)
            if why:
                raise ManifestError(f"{where}: output: {why}")
            if "/" in name or os.sep in name:
                raise ManifestError(f"{where}: output {name!r} contains a path separator")
            key = (posixpath.normpath(art["out_dir"]), name)
            if key in seen:
                raise ManifestError(f"{where}: output {key[0]}/{name} is listed twice")
            seen.add(key)

    for n, rule in enumerate(_require(data, "sources_must_be_mapped", list, "manifest")):
        where = f"sources_must_be_mapped {n}"
        for key in ("glob", "out_dir"):
            why = _bad_path(_require(rule, key, str, where))
            if why:
                raise ManifestError(f"{where}: {key}: {why}")
    for n, pattern in enumerate(_require(data, "outputs_must_be_mapped", list, "manifest")):
        why = _bad_path(pattern)
        if why:
            raise ManifestError(f"outputs_must_be_mapped {n}: {why}")
    return data


class Report:
    def __init__(self):
        self.errors = 0
        self.drift = 0

    def ok(self, path):
        print(f"OK     {path}")

    def driftline(self, path, reason, detail=()):
        self.drift += 1
        print(f"DRIFT  {path}: {reason}")
        for line in detail:
            print(f"         {line}")

    def error(self, path, reason, detail=()):
        self.errors += 1
        print(f"ERROR  {path}: {reason}")
        for line in detail:
            print(f"         {line}")


def _inside(real_root, path):
    real = os.path.realpath(path)
    return real == real_root or real.startswith(real_root + os.sep)


def _matches(root, pattern):
    """Files matching a root-relative glob (non-recursive), as sorted posix paths."""
    hits = glob.glob(os.path.join(glob.escape(root), *pattern.split("/")))
    rels = [os.path.relpath(h, root).replace(os.sep, "/") for h in hits if os.path.isfile(h)]
    return sorted(rels)


def _readable(path):
    try:
        with open(path, "rb"):
            return True
    except OSError:
        return False


def _diff(committed, fresh):
    a = committed.decode("utf-8", errors="replace").splitlines()
    b = fresh.decode("utf-8", errors="replace").splitlines()
    lines = list(difflib.unified_diff(a, b, "committed", "fresh", lineterm=""))
    return lines[:DIFF_LINES_MAX]


def run(root, manifest_path):
    rep = Report()
    try:
        manifest = load_manifest(manifest_path)
    except ManifestError as e:
        print(f"ERROR  manifest: {e}")
        return 2, 0, 0, 0

    real_root = os.path.realpath(root)
    artifacts = manifest["artifacts"]
    runnable = [True] * len(artifacts)
    reported = set()

    def error_once(path, reason):
        if (path, reason) not in reported:
            reported.add((path, reason))
            rep.error(path, reason)

    # 2. Containment of every generator, source and output directory.
    for n, art in enumerate(artifacts):
        for key in ("generator", "source", "out_dir"):
            if not _inside(real_root, os.path.join(root, art[key])):
                error_once(art[key], "resolves outside the root")
                runnable[n] = False

    # 3. Discovery: every schema and every committed generated file is mapped.
    for rule in manifest["sources_must_be_mapped"]:
        out_dir = posixpath.normpath(rule["out_dir"])
        for rel in _matches(root, rule["glob"]):
            if not any(posixpath.normpath(a["source"]) == rel
                       and posixpath.normpath(a["out_dir"]) == out_dir for a in artifacts):
                rep.error(rel, "source has no manifest entry")
    listed = {(posixpath.normpath(a["out_dir"]), name)
              for a in artifacts for name in a["outputs"]}
    for pattern in manifest["outputs_must_be_mapped"]:
        for rel in _matches(root, pattern):
            if (posixpath.dirname(rel), posixpath.basename(rel)) not in listed:
                rep.driftline(rel, "committed file has no manifest entry")

    # 4. Readable generator and source.
    for n, art in enumerate(artifacts):
        if not runnable[n]:
            continue
        for key in ("generator", "source"):
            path = os.path.join(root, art[key])
            if not os.path.isfile(path) or not _readable(path):
                error_once(art[key], "cannot read")
                runnable[n] = False

    # 5–7. Generate into owned scratch directories and compare.
    compared_ok = 0
    scratch = tempfile.mkdtemp(prefix="embediq-generated-")
    try:
        for n, art in enumerate(artifacts):
            if not runnable[n]:
                continue
            sub = os.path.join(scratch, f"{n:02d}")
            os.mkdir(sub)
            cmd = [sys.executable, os.path.join(root, art["generator"]), art["source"],
                   "--out", sub, *art["args"]]
            try:
                res = subprocess.run(cmd, cwd=root, capture_output=True)
            except OSError as e:
                rep.error(art["source"], f"generator could not be started: {e}")
                continue
            if res.returncode != 0:
                text = (res.stderr + res.stdout).decode("utf-8", errors="replace")
                rep.error(art["source"], f"generator failed (exit {res.returncode})",
                          text.splitlines())
                continue

            for extra in sorted(set(os.listdir(sub)) - set(art["outputs"])):
                rep.error(art["source"], f"generator produced unlisted {extra}")

            out_dir = posixpath.normpath(art["out_dir"])
            for name in art["outputs"]:
                shown = f"{out_dir}/{name}"
                fresh_path = os.path.join(sub, name)
                committed_path = os.path.join(root, *out_dir.split("/"), name)
                try:
                    with open(fresh_path, "rb") as fh:
                        fresh = fh.read()
                except OSError:
                    rep.error(shown, "generator did not produce it")
                    continue
                if not os.path.lexists(committed_path):
                    rep.driftline(shown, "committed file missing")
                    continue
                if not _inside(real_root, committed_path):
                    rep.error(shown, "resolves outside the root")
                    continue
                try:
                    with open(committed_path, "rb") as fh:
                        committed = fh.read()
                except OSError:
                    rep.error(shown, "cannot read")
                    continue
                if committed != fresh:
                    rep.driftline(shown, "differs from generator output",
                                  _diff(committed, fresh))
                else:
                    rep.ok(shown)
                    compared_ok += 1
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    sources = len({posixpath.normpath(a["source"]) for a in artifacts})
    if rep.errors:
        return 2, compared_ok, sources, rep.drift
    if rep.drift:
        return 1, compared_ok, sources, rep.drift
    return 0, compared_ok, sources, 0


def main(argv):
    default_root = os.path.normpath(os.path.join(
        os.path.dirname(os.path.realpath(__file__)), "..", ".."))
    ap = argparse.ArgumentParser(prog=SCRIPT, allow_abbrev=False,
                                 description=__doc__.splitlines()[1])
    ap.add_argument("--root", default=default_root)
    ap.add_argument("--manifest", default=None)
    args = ap.parse_args(argv[1:])   # unknown option or positional: usage, exit 2

    root = os.path.abspath(args.root)
    manifest = args.manifest or os.path.join(root, "tools", "ci", "generated_artifacts.json")
    rc, n_ok, n_sources, n_drift = run(root, manifest)
    if rc == 0:
        print(f"{SCRIPT}: OK — {n_ok} output(s) from {n_sources} source(s) "
              f"match generator output.")
    elif rc == 1:
        print(f"{SCRIPT}: FAIL — {n_drift} generated file(s) do not match their source; "
              f"regenerate and commit them.")
    else:
        print(f"{SCRIPT}: ERROR — the comparison could not be completed.")
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv))
