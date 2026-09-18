#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later

"""Read ci/targets.toml, the single source of truth for which Blender versions CI builds.

    ci/targets.py github      matrix=<json> and version=<addon version>, for $GITHUB_OUTPUT
    ci/targets.py env 5.2     export lines for running one row locally
    ci/targets.py list        the row ids, one per line

Stdlib only (tomllib, so Python >= 3.11). Every row is validated on load, so a typo fails here
naming its row rather than as a cmake error four jobs later. See docs/plans/blender-ci.md Step 0.2.
"""

import json
import os
import re
import shlex
import sys
import tomllib
from pathlib import Path

TARGETS = Path(__file__).with_name("targets.toml")

REQUIRED = (
    "id", "blender", "lib_branch", "lib_sha", "usd", "namespace",
    "python", "cxx", "version_min", "version_max", "platforms",
)


def fail(msg):
    print(f"targets: {msg}", file=sys.stderr)
    sys.exit(1)


def validate(row, index):
    name = f"row {row['id']!r}" if "id" in row else f"row #{index + 1}"
    missing = [k for k in REQUIRED if k not in row]
    if missing:
        fail(f"{name}: missing {', '.join(missing)}")

    # Relations that held for every row verified on 2026-09-15. Each is a typo waiting to happen.
    rid = row["id"]
    checks = [
        (re.fullmatch(r"\d+\.\d+", rid), f"id must be X.Y, got {rid!r}"),
        (row["blender"].startswith(f"{rid}."), f"blender {row['blender']!r} is not a {rid} release"),
        (row["lib_branch"] == f"blender-v{rid}-release",
         f"lib_branch {row['lib_branch']!r} != 'blender-v{rid}-release'"),
        (re.fullmatch(r"[0-9a-f]{40}", row["lib_sha"]), f"lib_sha {row['lib_sha']!r} is not a full SHA"),
        (row["namespace"] == "pxrBlender_v" + row["usd"].replace(".", "_"),
         f"namespace {row['namespace']!r} does not match usd {row['usd']!r}"),
        (row["version_min"] == f"{rid}.0", f"version_min {row['version_min']!r} != '{rid}.0'"),
        (row["cxx"] in (17, 20), f"cxx must be 17 or 20, got {row['cxx']!r}"),
    ]
    for ok, msg in checks:
        if not ok:
            fail(f"{name}: {msg}")


def load():
    with TARGETS.open("rb") as f:
        data = tomllib.load(f)
    if "addon_version" not in data:
        fail("addon_version missing")
    rows = data.get("target", [])
    if not rows:
        fail("no [[target]] rows")
    for i, row in enumerate(rows):
        validate(row, i)
        row.setdefault("smoke_digest", "")
    ids = [r["id"] for r in rows]
    dupes = sorted({i for i in ids if ids.count(i) > 1})
    if dupes:
        fail(f"duplicate row ids: {', '.join(dupes)}")
    return data["addon_version"], rows


def version(addon_version):
    """The add-on version this CI run packages. On a tag, the tag must agree with targets.toml."""
    if os.environ.get("GITHUB_REF_TYPE") == "tag":
        tag = os.environ.get("GITHUB_REF_NAME", "")
        if tag != f"v{addon_version}":
            fail(f"tag {tag!r} does not match addon_version {addon_version!r} (want 'v{addon_version}')")
        return addon_version
    sha = os.environ.get("GITHUB_SHA", "")
    if not sha:
        fail("GITHUB_SHA not set")
    return f"{addon_version}-dev+g{sha[:7]}"


def main(argv):
    addon_version, rows = load()
    cmd = argv[1] if len(argv) > 1 else ""

    if cmd == "list" and len(argv) == 2:
        for r in rows:
            print(r["id"])
    elif cmd == "github" and len(argv) == 2:
        v = version(addon_version)  # before printing, so a bad tag leaves $GITHUB_OUTPUT untouched
        print(f"matrix={json.dumps(rows, separators=(',', ':'))}")
        print(f"version={v}")
    elif cmd == "env" and len(argv) == 3:
        row = next((r for r in rows if r["id"] == argv[2]), None)
        if row is None:
            fail(f"no row {argv[2]!r}; have {', '.join(r['id'] for r in rows)}")
        env = {
            "TARGET_ID": row["id"],
            "BPY_VERSION": row["blender"],
            "LIB_BRANCH": row["lib_branch"],
            "LIB_SHA": row["lib_sha"],
            "USD_NAMESPACE": row["namespace"],
            "BLENDER_USD_NAMESPACE": row["namespace"],
            "PYTHON_MM": row["python"],
            "BLENDER_PYTHON": row["python"],
            "CXX_STANDARD": row["cxx"],
            "ADDON_VERSION": addon_version,
            "BLENDER_VERSION_MIN": row["version_min"],
            "BLENDER_VERSION_MAX": row["version_max"],
            "PLATFORMS": row["platforms"][0],
            "HDW_SMOKE_DIGEST": row["smoke_digest"],
        }
        for k, v in env.items():
            print(f"export {k}={shlex.quote(str(v))}")
    else:
        print(__doc__.strip().split("\n\n")[1], file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main(sys.argv)
