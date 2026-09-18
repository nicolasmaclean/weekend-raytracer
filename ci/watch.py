#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later

"""Report where Blender has moved away from ci/targets.toml.

    ci/watch.py        a markdown report on stdout; nothing at all when there is no drift

Exits 0 whether or not it finds drift: a finding is not a CI failure, a crash is. Stdlib only,
plus `git` for ls-remote. See docs/notes/ci.md §6 and docs/plans/blender-ci.md Step E1.
"""

import json
import re
import subprocess
import urllib.request

from targets import load

LIB_REPO = "https://projects.blender.org/blender/lib-linux_x64.git"
VERSIONS_CMAKE = ("https://projects.blender.org/blender/blender/raw/branch/{}"
                  "/build_files/build_environment/cmake/versions.cmake")
PYPI_BPY = "https://pypi.org/pypi/bpy/json"


def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read().decode()


def vkey(v):
    return tuple(int(x) for x in v.split("."))


def minor(v):
    return ".".join(v.split(".")[:2])


def lib_heads():
    """{branch: sha} for every head of the lib repo."""
    out = subprocess.run(["git", "ls-remote", "--heads", LIB_REPO],
                         check=True, capture_output=True, text=True).stdout
    heads = {}
    for line in out.splitlines():
        sha, ref = line.split("\t")
        heads[ref.removeprefix("refs/heads/")] = sha
    return heads


def deps(branch):
    """(USD, Python) versions that `branch` of blender builds its libraries with."""
    text = fetch(VERSIONS_CMAKE.format(branch))
    found = {}
    for name in ("USD_VERSION", "PYTHON_VERSION"):
        m = re.search(rf"^set\({name} ([0-9.]+)\)", text, re.M)
        if not m:
            raise SystemExit(f"watch: no {name} in versions.cmake @ {branch}")
        found[name] = m.group(1)
    return found["USD_VERSION"], found["PYTHON_VERSION"]


def main():
    _, rows = load()
    heads = lib_heads()
    releases = json.loads(fetch(PYPI_BPY))["releases"]
    bpy = sorted((v for v, files in releases.items()
                  if files and re.fullmatch(r"\d+\.\d+\.\d+", v)), key=vkey)
    newest_row = max((r["id"] for r in rows), key=vkey)

    loud, findings = [], []

    for r in rows:
        rid = r["id"]
        # 1. newer bpy patch in the row's minor
        latest = max((v for v in bpy if minor(v) == rid), key=vkey, default=r["blender"])
        # 2. the row's lib branch moved
        sha = heads.get(r["lib_branch"])
        if sha is None:
            loud.append(f"**{rid}**: lib branch `{r['lib_branch']}` no longer exists")
            continue
        if vkey(latest) > vkey(r["blender"]) or sha != r["lib_sha"]:
            what = []
            if vkey(latest) > vkey(r["blender"]):
                what.append(f"{latest} available (pinned {r['blender']})")
            if sha != r["lib_sha"]:
                what.append(f"lib branch at `{sha[:12]}`, pinned `{r['lib_sha'][:12]}`")
            findings.append(
                f"**{rid}**: {'; '.join(what)}. In `ci/targets.toml` (then clear `smoke_digest`,"
                f" run `ci/local.sh {rid}` and record the digest it prints):\n\n"
                f"  ```toml\n  blender     = \"{latest}\"\n  lib_sha     = \"{sha}\"\n  ```")
        # 3. USD or Python changed on a release branch - would break the ABI within a minor
        usd, py = deps(r["lib_branch"])
        if usd != r["usd"] or minor(py) != r["python"]:
            loud.append(f"**{rid}**: `{r['lib_branch']}` now builds USD {usd} / Python {py}, the"
                        f" row says USD {r['usd']} / Python {r['python']}. That changes the ABI"
                        f" inside a minor, so check before rebuilding")

    # 4. a release branch newer than every row
    for branch in sorted(heads):
        m = re.fullmatch(r"blender-v(\d+\.\d+)-release", branch)
        if m and vkey(m.group(1)) > vkey(newest_row):
            findings.append(f"`{branch}` exists with no row: add one")

    # 5. main's USD/Python pair isn't covered by any row
    usd, py = deps("main")
    if not any(r["usd"] == usd and r["python"] == minor(py) for r in rows):
        findings.append(f"`main` builds USD {usd} / Python {py}, which no row covers. Early"
                        f" warning: the next release will need a new SDK row")

    # 6. a bpy minor on PyPI newer than every row
    for m in sorted({minor(v) for v in bpy}, key=vkey):
        if vkey(m) > vkey(newest_row):
            first = min((v for v in bpy if minor(v) == m), key=vkey)
            findings.append(f"bpy {first} published with no {m} row: add one")

    if not loud and not findings:
        return
    print("Drift between `ci/targets.toml` and upstream Blender, found by `ci/watch.py`.\n")
    if loud:
        print("## Needs a look\n")
        for f in loud:
            print(f"- {f}")
        print()
    if findings:
        print("## Findings\n")
        for f in findings:
            print(f"- {f}\n")


if __name__ == "__main__":
    main()
