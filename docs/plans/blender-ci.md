# Blender CI — step-by-step

**Roadmap item:** `0.3.0 - hydra delegate` → `ci pipeline to compile and distribute blender add-on` (see [[Roadmap]]) — the last open item in 0.3.0, so the `v0.3.0` tag is this task's first real release
*(Moved from 0.4.0 on 2026-09-15.)*
**Context:** [[ci]] · [[blender-addon]] · [[compile-against-blenders-usd-binary]]
**Every environment and API fact below was verified on 2026-09-15**, against: `lib-linux_x64.git`
(`git ls-remote` and the Gitea contents/raw/media endpoints), `blender.git` release branches (raw
`versions.cmake`, `options.cmake`, `source/blender/render/hydra/`), PyPI's `bpy` JSON, the local
OpenUSD checkout at `v25.02`/`v25.08`/`v26.03`/`v26.05`, the `.venv-bpy45` wheel, and the artifacts
already on this machine. Anything *not* verified is marked as such at the point it matters.

---

## Decisions taken

Settled before planning, so they are not re-opened mid-task. Each is a default chosen on evidence
below; change the row, not the stages, if you disagree.

| Decision | Choice | Consequence |
|---|---|---|
| CI host | **GitHub Actions** — `origin` is `github.com/nicolasmaclean/weekend-raytracer` | Workflows under `.github/workflows/` |
| Targets | **4.5 LTS, 5.0, 5.1, 5.2** — one row per Blender minor | 4 jobs. 5.0 and 5.1 share USD 25.08 but *not* Python, C++ standard or TBB — see facts |
| Platform | **`linux-x64` only** | Windows/macOS are a follow-up plan; the matrix is keyed so they add a dimension, not a rewrite |
| Cadence | **Every row on every push** | [[ci]] §3 recommends all rows × linux per commit; with one platform that is everything |
| Linux build environment | **`quay.io/pypa/manylinux_2_28_x86_64` via `docker run`**, not job-level `container:` | Only the compile runs in the old-glibc image; SDK provisioning, cache and the `bpy` test stay on the host |
| SDK pinning | **Row pins `lib_sha`**, not just the branch | Reproducible builds, a permanent cache key; the watch job proposes bumps |
| Where gates are tested | **Locally, in Docker, via `ci/local.sh <id>`** — added 2026-09-18 | Every script is green on this machine before a push; pushes test only what GitHub alone runs (the workflow file, cache, runner, release) |
| Packaging tool | **`blender_ext.py` from the row's `bpy` wheel** | No 378 MB app tarball in CI at all — [[ci]] §5 assumed one was needed |
| Release | **Tag `v*` → draft GitHub Release** with one zip per row | Publishing it, and extensions.blender.org, stay manual |
| Add-on version across rows | **One version number, rows told apart by filename** | Blender's "tracks" scheme ([[ci]] §7) is deferred to the publishing task — see Design notes |

---

## What this task is

Turning [[blender-addon]]'s hand-run gates into exit codes, four times over.

There is no new rendering or add-on code. The blender plugin item left CI exactly what [[blender-addon]] "Next up"
promised: `hydra/compat.h`, a `-D`-settable namespace assertion, a manifest template and
`blender/smoke_test.py`. What is missing is (1) the handful of places that still hardcode Blender 4.5,
(2) a build environment whose output actually loads on Blender's supported distros, and (3) a
workflow that runs Gate 0 → Gate G per row with nobody reading the logs.

## The one thing to understand before starting

**CI has no human reading output, and three of [[blender-addon]]'s failure modes were silent.** A
`ldd -r` that reports "not found" when the build is *correct*; a render that **hangs at 100% CPU**
instead of failing when AOVs are unbound; a black image that passes an `any(px > 0)` check because
alpha is 1.0. The 4.5 plan found each of these because a person was looking. Every gate below is
therefore written as an assertion that exits non-zero, and every assertion is proven to *fail* once
before it is trusted — the same "break it deliberately" discipline as GATE F.

The most important of these assertions is one the 4.5 plan could only describe. Measured today:

```
                                   GLIBC      GLIBCXX     CXXABI
lib-linux_x64 4.5 libusd_ms.so     2.27       3.4.22      1.3.11   <- what Blender itself needs
dist/weekend_raytracer-0.3.0.zip   2.32       3.4.32      1.3.11   <- what our shipped .so needs
  (plugin/usd/hdWeekend.so)
```

The locally built zip is worse than [[blender-addon]] Stage G said. It does not merely miss the 2.28 floor —
`GLIBCXX_3.4.32` is GCC 13's libstdc++, so it fails to load on **Ubuntu 22.04** (GCC 12,
`3.4.30`) and anything older, not just Rocky 8. [[ci]] §4 framed this as a glibc problem; it is
equally a libstdc++ problem, and `check_so.sh` (Step B4) asserts both.

---

## Targets — verified facts

| | 4.5 LTS | 5.0 | 5.1 | 5.2 |
|---|---|---|---|---|
| `bpy` pin (latest patch on PyPI) | **4.5.13** (4.5.14 exists — see Gate E) | 5.0.1 | 5.1.2 | 5.2.2 |
| `lib-linux_x64` branch | `blender-v4.5-release` | `blender-v5.0-release` | `blender-v5.1-release` | `blender-v5.2-release` |
| branch HEAD | `1df155d9de29` | `3abcca1aa43f` | `817d868e94a1` | `ecbd06cf6d2a` |
| `USD_VERSION` (`versions.cmake`) | 25.02 | 25.08 | 25.08 | 26.03 |
| `PXR_VERSION` (shipped `pxr.h`) | 2502 | 2508 | 2508 | 2603 |
| `PXR_INTERNAL_NS` | `pxrBlender_v25_02` | `pxrBlender_v25_08` | `pxrBlender_v25_08` | `pxrBlender_v26_03` |
| `PYTHON_VERSION` | 3.11.15 | 3.11.13 | **3.13.9** | 3.13.13 |
| `python/include/` | `python3.11` | `python3.11` | `python3.13` | `python3.13` |
| `bpy` wheel tag | cp311 | cp311 | cp313 | cp313 |
| C++ standard (`options.cmake`) | 17 | 17 | **20** | 20 |
| TBB | 2021.13 (`libtbb.so.12.13`) | 2021.13 (`.12.13`) | **2022.3** (`.12.17`) | 2022.3 (`.12.17`) |
| `libusd_ms.so` (LFS size) | 57,342,624 | 68,720,352 | 70,718,760 | 78,736,808 |
| `compat.h` branch taken | `bool IsSupported` | `bool IsSupported` | `bool IsSupported` | `HdRendererCreateArgs` |

Notes on the rows that are not self-evident:

- **5.0 vs 5.1 is the interesting pair.** Same USD, same namespace — but 5.1 moved Python 3.11 → 3.13,
  C++17 → 20 and TBB 2021 → 2022. [[ci]] §1's "5.0/5.1 (25.08)" reads as one target; it is two.
  The TBB soname stays `libtbb.so.12` in both, which is what `DT_NEEDED` records, so this is a
  header/ABI-flag difference, not a load-time one.
- **`lib-linux_x64` `main` == `blender-v5.2-release`** (same SHA), and `versions.cmake` on
  `blender.git` `main` still says USD 26.03 / Python 3.13.13. There is no 5.3 divergence to plan for
  yet. That is the watch job's baseline (Stage E).
- **The SDK is small.** A sparse `usd tbb python/include` checkout is 79 MB on disk plus 67 MB of
  `.git` (measured on the local 4.5 checkout). [[blender-addon]] budgeted ~1 GB; four rows cached is
  well inside GitHub's 10 GB per-repo cache.
- **LFS objects are also served over plain HTTPS** — `…/lib-linux_x64/media/branch/blender-v5.2-release/usd/lib/libusd_ms.so`
  returns `200`, `content-length: 78736808`. Not used by the plan (the runner has `git-lfs`), but it
  is the fallback if LFS ever misbehaves in CI.

### The host side did not move

Blender's Hydra host code, 4.5 vs 5.2, at every point [[blender-addon]]'s "one thing to understand"
depends on:

| Behaviour | 4.5 | 5.2 |
|---|---|---|
| `aovToken:` prefix builds the AOV map | `final_engine.cc:106` | `final_engine.cc:107` |
| colour+depth fallback gated on `RE_USE_GPU_CONTEXT` | `final_engine.cc:64` | `final_engine.cc:65` |
| float32/float16-only AOV check | `render_task_delegate.cc:120` | `render_task_delegate.cc:147` |
| `percentDone` read with `UncheckedGet<double>` | `engine.cc:135-139` | `engine.cc:155-159` |
| plain `CreateRenderDelegate(TfToken(name))` | `engine.cc:62` | `engine.cc:64` |
| `HydraRenderEngine.get_render_settings` | `bpy_types.py:1412` | `_bpy_types.py:1628` (module renamed; irrelevant to us) |
| `expose_bundled_modules` in `bpy.utils` | present | present |
| built-in panels opt in via `BLENDER_RENDER` (`properties.py:158`) | yes | yes — 15 in `properties_output.py`, and `hydra_storm/ui.py:258` still uses it |

So the add-on Python is expected to work unchanged on all four rows. The smoke test is what confirms
it; this table is why a failure there should be read as a *build* problem first.

### The USD side: one shim, already written

`IsSupported` checked at each intermediate tag, not inferred from the endpoints:

```
v25.08  rendererPlugin.h:95   virtual bool IsSupported(bool gpuEnabled = true) const = 0;
v26.03  rendererPlugin.h:54   virtual bool IsSupported(HdRendererCreateArgs const &, ...)  (pure)
        rendererPlugin.h:184  virtual bool IsSupported(bool gpuEnabled = true) const;       (deprecated)
```

`compat.h`'s `PXR_VERSION >= 2603` is therefore correct for all four rows as written.

Of the 27 `pxr/` headers `hydra/` includes, 6 changed `v25.02→v25.08`, 10 changed `v25.08→v26.03`
and 8 changed `v26.03→v26.05`. [[blender-addon]] audited `v25.02→v26.05` end to end and found only
`rendererPlugin.h`; an intermediate version can only add a break if something was removed and later
restored, which is unlikely but **not audited**. **Prediction:** 5.0/5.1/5.2 compile with zero new
`compat.h` entries. Gate B is where that becomes a fact — and if it is wrong, the compiler says
exactly where, which is the triage [[ci]] §6 wants.

**C++20 is verified safe for our sources:** all 9 `hydra/*.cpp` pass `-std=c++20 -fsyntax-only`
against vanilla USD 26.05 with zero errors (compile commands from `build-hydra/`, standard flag
swapped). No `__cplusplus`-conditional declarations exist in the `tf`/`vt`/`gf`/`work`/`hd` headers
we include beyond vendored third-party code. Matching Blender's standard is still the right call,
since `libusd_ms.so` was built with it.

### Packaging needs no Blender app

`blender_ext.py` — the implementation behind `blender --command extension` — ships inside the wheel
at `bpy/4.5/scripts/addons_core/bl_pkg/cli/blender_ext.py` and imports only the standard library
(`tomllib`, `zipfile`, `urllib`, …). Run with the system Python 3.12, `build --help` works and lists
`--output-filepath` and `--split-platforms`; `validate` is a subcommand. Every row already installs
its `bpy` wheel for the smoke test, so every row already has its own matching packager.

**Not verified:** the path inside 5.x wheels. The package step `find`s it rather than hardcoding
`4.5/`.

### The build image

`pypa/manylinux`'s Dockerfile defaults `DEVTOOLSET_ROOTPATH=/opt/rh/gcc-toolset-14/root` → GCC 14,
which has full C++20. gcc-toolset links newer libstdc++ features statically from
`libstdc++_nonshared.a`, which is the mechanism that should keep `GLIBCXX` at the base system's
`3.4.25`. **Not verified:** that mechanism's result for our `.so`, and whether `cmake` is on `PATH`
in the image. Both are checked at Gate B, locally — the first by `check_so.sh`, which is why it
exists; the second by one `docker run … cmake --version` before the first build.

**Measured 2026-09-18, tag `2026.09.14-1`** (digest `sha256:531d7aa844bb…`): `gcc (GCC) 14.2.1`,
glibc 2.28, `cmake` 4.4.3 at `/usr/local/bin/cmake`, runs as root. CMake 4 rejects
`cmake_minimum_required` below 3.5; `hydra/` asks for 3.15, so that is fine. Step B3's 4.5 build
came out at GLIBC 2.14 / GLIBCXX 3.4.21 / CXXABI 1.3.11 — under every ceiling.

---

## What [[blender-addon]] left hardcoded to 4.5

Read from the files, not the plan:

| File | Hardcoded | Stage |
|---|---|---|
| `hydra/CMakeLists.txt` | `python/include/python3.11` — in the `Python.h` guard *and* the include dir | A1 |
| `hydra/CMakeLists.txt` | `set(CMAKE_CXX_STANDARD 17)`, unconditionally | A1 |
| `blender/build_zip.sh` | `4.5.0` / `5.0.0` / `linux-x64`; `build-hydra-blender/install`; `$BLENDER` app for `extension build`; no SPDX header | A3 |
| `blender/weekend_raytracer/blender_manifest.toml.in` | `version = "0.3.0"` | A2 |
| Step B3 of [[blender-addon]] | its own copy of the manifest `sed` | A2 |
| `blender/smoke_test.py` | nothing — already reads `HDW_PLUGIN_DIR` / `HDW_SMOKE_TIMEOUT`. Needs a digest assertion only | A4 |
| `blender/bpy.sh` | `.venv-bpy45` default, but `BPY_PYTHON_BIN` overrides it; CI does not need the wrapper at all (no `env.sh`) | — |

## What is explicitly NOT in this task

| Not now | Comes with |
|---|---|
| Windows (`lib-windows_x64`, MSVC runtime) and macOS (`MACOSX_DEPLOYMENT_TARGET`) | A follow-up plan; both unverified in [[ci]] §4 |
| Vanilla-USD (26.05) rows | Nightly, per [[ci]] §6/§8 — they are a source build |
| Publishing to extensions.blender.org, and tracks versioning | The publishing task |
| Automatic PRs from the watch job | Never — see Design notes |
| Verifying Depth/Normal pass *contents* (`OPEN_EXR_MULTILAYER`) | Deliberately left by GATE F; still left |
| A GUI check per row | Gate D does one row by hand; the rest are covered by `bpy` |

---

## New files

| File | Purpose |
|---|---|
| `ci/targets.toml` | The single source of truth — one row per Blender minor ([[ci]] §2) |
| `ci/targets.py` | Reads it: GitHub matrix JSON, per-row shell env for local runs, computed add-on version |
| `ci/provision_sdk.sh` | Sparse LFS checkout of `lib-linux_x64` at the row's pinned SHA |
| `ci/verify_sdk.sh` | GATE 0 of [[blender-addon]] as assertions — runs even on a cache hit |
| `ci/build.sh` | Configure/build/install inside the manylinux container |
| `ci/check_so.sh` | GATE A as assertions, plus the GLIBC/GLIBCXX/CXXABI ceilings |
| `ci/docker_build.sh` | The one `docker run` line: `ci/build.sh` in manylinux_2_28, for CI and local alike |
| `ci/local.sh` | Runs one row on this machine as the workflow does — the same scripts, in the same order |
| `ci/watch.py` | Weekly drift report: new branches, patch releases, USD bumps on `main` |
| `blender/render_manifest.sh` | The one place the manifest template is rendered (dev install and zip) |
| `.github/workflows/blender.yml` | Per-push build → check → smoke → package; draft release on tag |
| `.github/workflows/blender-watch.yml` | Weekly `ci/watch.py`, reported as one GitHub issue |

Modified: `hydra/CMakeLists.txt`, `blender/build_zip.sh`, `blender/smoke_test.py`,
`blender/weekend_raytracer/blender_manifest.toml.in`, `env.sh` (one export), and at the end
`docs/notes/ci.md` (corrections) and `docs/Roadmap.md`.

---

# Stage 0 — the targets file

## Step 0.1 — `ci/targets.toml`

```toml
# One row per Blender minor. CI's matrix is derived from this file and nothing else
# (docs/notes/ci.md §2). Every value was verified 2026-09-15 - see docs/plans/blender-ci.md.
#
# Adding a Blender version: add a row, run ci/local.sh <id>, read which step fails.

# The add-on version. A v* tag must equal it; untagged builds become <version>-dev+g<sha>.
addon_version = "0.3.0"

[[target]]
id          = "4.5"
blender     = "4.5.13"                 # bpy wheel pin
lib_branch  = "blender-v4.5-release"
lib_sha     = "1df155d9de293d4a9f09b94aae2cd94f77b37739"
usd         = "25.02"
namespace   = "pxrBlender_v25_02"      # asserted against pxr.h by cmake AND verify_sdk.sh
python      = "3.11"                   # uv interpreter + python/include/python3.11
cxx         = 17
version_min = "4.5.0"
version_max = "5.0.0"                  # EXCLUSIVE - blender_ext checks >= max
platforms   = ["linux-x64"]
# smoke_digest = ""                    # filled in from the first green CI run - Gate C

[[target]]
id          = "5.0"
blender     = "5.0.1"
lib_branch  = "blender-v5.0-release"
lib_sha     = "3abcca1aa43fe29f21cdf442d0cf412357cf4d25"
usd         = "25.08"
namespace   = "pxrBlender_v25_08"
python      = "3.11"
cxx         = 17
version_min = "5.0.0"
version_max = "5.1.0"
platforms   = ["linux-x64"]

[[target]]
id          = "5.1"
blender     = "5.1.2"
lib_branch  = "blender-v5.1-release"
lib_sha     = "817d868e94a1fa3d2320bca7b614f425a4a2d4f8"
usd         = "25.08"
namespace   = "pxrBlender_v25_08"
python      = "3.13"
cxx         = 20
version_min = "5.1.0"
version_max = "5.2.0"
platforms   = ["linux-x64"]

[[target]]
id          = "5.2"
blender     = "5.2.2"
lib_branch  = "blender-v5.2-release"
lib_sha     = "ecbd06cf6d2a4aa6b00a61ffb479fc81b17aba08"
usd         = "26.03"
namespace   = "pxrBlender_v26_03"
python      = "3.13"
cxx         = 20
version_min = "5.2.0"
version_max = "5.3.0"
platforms   = ["linux-x64"]
```

The 4.5 row pins **4.5.13, not 4.5.14**, on purpose. It is the version GATE F's digest
`d8e910b3f3b763d6` was measured on, which gives Gate C a cross-compiler comparison — and it gives
the watch job a real finding to report on its first run (Gate E).

`namespace` omits the `__pxrReserved__` suffix because the cmake check is a prefix regex
(`PXR_INTERNAL_NS[ \t]+${HDW_BLENDER_USD_NAMESPACE}`), and `env.sh` already uses that form.

## Step 0.2 — `ci/targets.py`

Stdlib only (`tomllib`, so Python ≥ 3.11 — the runner's system 3.12 is fine). Three subcommands:

```
ci/targets.py github          # -> matrix=<json>  version=<addon version>   for $GITHUB_OUTPUT
ci/targets.py env 5.2         # -> export LIB_BRANCH=... lines, for running a row locally
ci/targets.py list            # -> the ids, one per line
```

`github` computes the version from `GITHUB_REF_TYPE` / `GITHUB_REF_NAME` / `GITHUB_SHA`: on a tag
it **fails** unless `GITHUB_REF_NAME == "v" + addon_version`, so a mistyped tag cannot publish a zip
whose manifest disagrees with its release; otherwise `f"{addon_version}-dev+g{sha[:7]}"`. Both
forms pass `blender_ext`'s `RE_MANIFEST_SEMVER` (it accepts `-prerelease` and `+buildmetadata`,
verified at `blender_ext.py:95-102`).

Validate every row on load, so a bad row fails in the `targets` job with its id rather than as a
confusing cmake error four jobs later: required keys present; `id == blender`'s `X.Y`;
`lib_branch == f"blender-v{id}-release"`; `namespace == "pxrBlender_v" + usd.replace(".", "_")`;
`version_min == f"{id}.0"`; `cxx in (17, 20)`. Those relations held for every row verified above,
and each one is a typo someone will eventually make.

## GATE 0 — the file parses and the matrix is right

```bash
python3 ci/targets.py list                         # 4.5 5.0 5.1 5.2
python3 ci/targets.py env 5.1 | grep -E "PYTHON|CXX" # 3.13 / 20
GITHUB_REF_TYPE=branch GITHUB_SHA=0123456789abcdef python3 ci/targets.py github
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=v0.3.0 python3 ci/targets.py github   # version=0.3.0
GITHUB_REF_TYPE=tag GITHUB_REF_NAME=v9.9.9 python3 ci/targets.py github   # MUST exit non-zero
```

And break a row once — change 5.0's `namespace` to `pxrBlender_v25_02` — to see the validator name
the row.

---

# Stage A — un-hardcode 4.5, locally, with nothing changing

Every step in this stage must leave the local 4.5 workflow producing exactly what it produces
today. That is Gate A, and it is why this stage comes before any CI exists.

## Step A1 — `hydra/CMakeLists.txt`

Two new cache variables, sourced from the environment the same way `HDW_BLENDER_USD_NAMESPACE`
already is:

```cmake
# C++ standard. 17 for vanilla USD 26.05 and Blender 4.5/5.0; Blender 5.1+ builds USD and every
# dependency with -DCMAKE_CXX_STANDARD=20 (build_environment options.cmake), so those rows pass 20.
set(HDW_CXX_STANDARD 17 CACHE STRING "C++ standard: match the USD being linked")
set(CMAKE_CXX_STANDARD ${HDW_CXX_STANDARD})
```

replacing the unconditional `set(CMAKE_CXX_STANDARD 17)` (keep the existing comment's two
citations, add the 5.1 one). And in the `blender` branch:

```cmake
    # Python major.minor of the Blender being targeted: 3.11 for 4.5/5.0, 3.13 for 5.1+.
    # Selects python/include/python<ver>, the headers libusd_ms.so was compiled against.
    set(HDW_BLENDER_PYTHON "$ENV{BLENDER_PYTHON}" CACHE STRING "e.g. 3.11")
    set(_py_include "${BLENDER_LIB_DIR}/python/include/python${HDW_BLENDER_PYTHON}")
    if(NOT EXISTS "${_py_include}/Python.h")
        message(FATAL_ERROR
            "no Python ${HDW_BLENDER_PYTHON} headers at ${_py_include} - check HDW_BLENDER_PYTHON, "
            "or widen the sparse checkout:\n"
            "    git -C ${BLENDER_LIB_DIR} sparse-checkout add python/include")
    endif()
```

and use `${_py_include}` in `target_include_directories`. No default value on purpose: an empty
`HDW_BLENDER_PYTHON` produces `python/include/python/Python.h`, which does not exist, so forgetting
it fails at configure time with the fix in the message — the same shape as the namespace guard.

Update the two "Blender 4.5" comments to say "the targeted Blender" and point at `ci/targets.toml`.

## Step A2 — one manifest renderer

`blender/render_manifest.sh <out-file>`, reading `ADDON_VERSION`, `BLENDER_VERSION_MIN`,
`BLENDER_VERSION_MAX`, `PLATFORMS` from the environment, **with today's 4.5 values as defaults** so
the dev loop needs no new env:

```bash
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Render blender_manifest.toml.in. Used by the dev install (docs/plans/blender-addon.md Step B3)
# and by build_zip.sh, so the two can never disagree. Defaults are the Blender 4.5 row;
# CI sets every variable from ci/targets.toml.
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
sed -e "s|@ADDON_VERSION@|${ADDON_VERSION:-0.3.0}|" \
    -e "s|@BLENDER_VERSION_MIN@|${BLENDER_VERSION_MIN:-4.5.0}|" \
    -e "s|@BLENDER_VERSION_MAX@|${BLENDER_VERSION_MAX:-5.0.0}|" \
    -e "s|@PLATFORMS@|${PLATFORMS:-linux-x64}|" \
    "$here/weekend_raytracer/blender_manifest.toml.in" > "$1"
if grep -q "@[A-Z_]*@" "$1"; then echo "unrendered placeholder in $1" >&2; exit 1; fi
```

and in the template, `version = "@ADDON_VERSION@"`.

The trailing `grep` is load-bearing, not tidiness. [[blender-addon]] GATE B's note established that
Blender skips a directory with a bad manifest **in silence**; a template that grows a placeholder
without this script learning about it would reproduce exactly that failure in the dev install. Then
replace Step B3's inline `sed` in [[blender-addon]] with a call to this script, with a dated note.

`PLATFORMS` stays one entry here. When a second platform exists, the template's
`platforms = ["@PLATFORMS@"]` becomes `platforms = [@PLATFORMS@]` with pre-quoted input — not needed
until then.

## Step A3 — `blender/build_zip.sh`

Parameterise; defaults reproduce today's behaviour exactly:

```bash
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

: "${HDW_INSTALL_DIR:=build-hydra-blender/install}"
: "${DIST_DIR:=$PWD/dist}"

STAGE=$(mktemp -d)/weekend_raytracer
mkdir -p "$STAGE/plugin/usd" "$DIST_DIR"
cp blender/weekend_raytracer/*.py "$STAGE/"
blender/render_manifest.sh "$STAGE/blender_manifest.toml"

# Mirror cmake's install layout exactly: the .so is a SIBLING of hdWeekend/, because
# plugInfo.json says LibraryPath "../hdWeekend.so" with Root "..".
cp -r "$HDW_INSTALL_DIR/plugin/usd/." "$STAGE/plugin/usd/"

# BLENDER_EXT: path to blender_ext.py - stdlib-only, ships in every bpy wheel, so CI needs no
# Blender app. Falls back to the app's own `--command extension` for the local dev loop.
if [[ -n "${BLENDER_EXT:-}" ]]; then
    ext=(python3 "$BLENDER_EXT")
else
    ext=("$BLENDER" --command extension)
fi

out=()
[[ -n "${ZIP_NAME:-}" ]] && out=(--output-filepath "$DIST_DIR/$ZIP_NAME") || out=(--output-dir "$DIST_DIR")
"${ext[@]}" build --source-dir "$STAGE" "${out[@]}"
```

CI sets `ZIP_NAME=weekend_raytracer-${ADDON_VERSION}-blender-${id}-linux-x64.zip`, because four zips
with the manifest's default `{id}-{version}.zip` name would overwrite each other in one release.

## Step A4 — `smoke_test.py`: assert the digest when told to

After the digest is computed:

```python
    expected = os.environ.get("HDW_SMOKE_DIGEST")
    if expected:
        # Per-row constant from ci/targets.toml. A change on an unchanged tracer means the host
        # integration (or the compiler) moved - see docs/plans/blender-ci.md Gate C.
        assert digest[:16] == expected, f"digest {digest[:16]} != expected {expected}"
```

Unset or empty means "report only", which is what a new row needs until its first green run.

## Step A5 — `env.sh`

One line beside `BLENDER_USD_NAMESPACE`:

```bash
export BLENDER_PYTHON=3.11                               # python/include/python3.11, Step A1
```

## GATE A — 4.5 is exactly as it was

From a sourced shell:

```bash
cmake -S hydra -B build-hydra-blender -DHDW_USD_FLAVOR=blender \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=build-hydra-blender/install
cmake --build build-hydra-blender -j && cmake --install build-hydra-blender

HDW_PLUGIN_DIR=$PWD/build-hydra-blender/install/plugin/usd \
HDW_SMOKE_DIGEST=d8e910b3f3b763d6 ./blender/bpy.sh blender/smoke_test.py      # OK ... d8e910b3f3b763d6

HDW_PLUGIN_DIR=$PWD/build-hydra-blender/install/plugin/usd \
HDW_SMOKE_DIGEST=0000000000000000 ./blender/bpy.sh blender/smoke_test.py      # MUST exit 1

BLENDER_EXT=$(find .venv-bpy45 -path '*/bl_pkg/cli/blender_ext.py') blender/build_zip.sh
python3 "$(find .venv-bpy45 -path '*/bl_pkg/cli/blender_ext.py')" validate dist/weekend_raytracer-0.3.0.zip

cmake --build build-hydra -j && cmake --install build-hydra      # vanilla still builds, still C++17
```

Plus two negatives for Step A1: configure the blender flavour with `BLENDER_PYTHON` unset, and with
`-DHDW_BLENDER_PYTHON=3.13` against the 4.5 SDK. Both must stop at configure with the new message.

The existing cache in `build-hydra-blender/` predates `HDW_CXX_STANDARD`/`HDW_BLENDER_PYTHON`, so
reconfigure rather than trusting an incremental build to have picked up the new variables.

**Not checked here, and worth one line when it runs:** whether `blender_ext.py validate` takes a
`.zip` directly. GATE G ran `$BLENDER --command extension validate dist/*.zip` successfully, and that
command dispatches to the same script, so it is expected to.

**Run on 2026-09-18: green.** After `cmake --fresh` (cache now `HDW_CXX_STANDARD=17`,
`HDW_BLENDER_PYTHON=3.11` from `env.sh`) and a full rebuild: `OK 16384 pixels, digest
d8e910b3f3b763d6`, exit 0; the `0000000000000000` run fails with `AssertionError: digest
d8e910b3f3b763d6 != expected 0000000000000000`, exit 1. The zip builds and validates, and the
vanilla build still builds at C++17. Both Step A1 negatives stop at configure (`CMakeLists.txt:56`),
with `BLENDER_PYTHON` unset and with `-DHDW_BLENDER_PYTHON=3.13`. Also found:

- `blender_ext.py validate` **does** take a `.zip` directly (`Success parsing TOML in
  "dist/weekend_raytracer-0.3.0.zip"`), which settles the question above.
- The rendered manifest keeps the template's trailing `# ...` comments. Harmless.

---

# Stage B — build every row in manylinux_2_28

**Changed 2026-09-18: Docker is installed on this machine (Docker CE from `download.docker.com`, Ubuntu 24.04), and every
gate from here on is run locally first.** The original plan iterated Stage B by pushing, because
there was no container runtime here. Now `ci/local.sh <id>` (Step B6) runs one row end to end by
calling the same `ci/` scripts the workflow calls, in the same order, and the `docker run` line
lives in one script (Step B5) that both call — so a local green is a green of the exact build CI
runs, not an approximation of it.

What a push is still needed for, and only that: the workflow file itself (YAML, the matrix from
`targets.py github`, `actions/cache`, `git-lfs` on the runner, artifacts) and the release. Each
gate below says which of its checks are local and which are the push. Docker facts in this stage
(image tag, `cmake` in the image) are **not verified** until GATE B's first local run.

## Step B1 — `ci/provision_sdk.sh <dir>`

[[blender-addon]] Step 0.2, at a pinned commit. Reads `LIB_BRANCH`, `LIB_SHA`:

```bash
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
dir=$1
git lfs install --skip-repo      # without this, checkout silently leaves 133-byte pointer files
git clone --filter=blob:none --no-checkout --branch "$LIB_BRANCH" \
    https://projects.blender.org/blender/lib-linux_x64.git "$dir"
git -C "$dir" sparse-checkout set usd tbb python/include
git -C "$dir" checkout --detach "$LIB_SHA"
```

`ubuntu-24.04` runner images ship `git-lfs`; `--skip-repo` installs the global filters without
needing a repository. If LFS ever becomes the problem, the `/media/` endpoint in the facts section
serves the same objects over plain HTTPS.

## Step B2 — `ci/verify_sdk.sh <dir>`

GATE 0 as assertions. Runs on **every** job, cache hit or not — a cache poisoned with pointer files is
otherwise permanent, because the key never changes.

```bash
#!/usr/bin/env bash
set -euo pipefail
d=$1
fail() { echo "verify_sdk: $*" >&2; exit 1; }

# 1. real ELF, not an LFS pointer. Magic rather than size, so the row doesn't carry a byte count.
[[ "$(head -c4 "$d/usd/lib/libusd_ms.so" | od -An -c | tr -d ' ')" == '177ELF' ]] \
    || fail "libusd_ms.so is not an ELF file - LFS pointer? ($(stat -c%s "$d/usd/lib/libusd_ms.so") bytes)"

# 2. namespace, independently of cmake - so a mismatch names the SDK, not the build
grep -qE "PXR_INTERNAL_NS[[:space:]]+${USD_NAMESPACE}" "$d/usd/include/pxr/pxr.h" \
    || fail "pxr.h namespace is $(grep -oE 'PXR_INTERNAL_NS[[:space:]]+[A-Za-z0-9_]+' "$d/usd/include/pxr/pxr.h"), row says ${USD_NAMESPACE}"

# 3. the Python headers this row compiles against
[[ -f "$d/python/include/python${PYTHON_MM}/Python.h" ]] \
    || fail "no python${PYTHON_MM} headers; have: $(ls "$d/python/include")"

# 4. sonames our DT_NEEDED will record - GATE A's no-RUNPATH design depends on both
# (captured, not piped into grep -q - pipefail + SIGPIPE, see ci/check_so.sh)
dyn=$(readelf -d "$d/usd/lib/libusd_ms.so")
grep -q 'soname: \[libusd_ms.so\]' <<<"$dyn" || fail "libusd_ms.so SONAME changed"
[[ -e "$d/tbb/lib/libtbb.so.12" ]] || fail "no libtbb.so.12"
echo "verify_sdk: OK"
```

## Step B3 — `ci/build.sh <sdk> <build-dir>`

Runs inside the container. Reads `USD_NAMESPACE`, `PYTHON_MM`, `CXX_STANDARD`:

```bash
#!/usr/bin/env bash
set -euo pipefail
sdk=$1 build=$2
gcc --version | head -1            # record the toolchain in the log - GCC 14 expected
cmake -S hydra -B "$build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$build/install" \
      -DHDW_USD_FLAVOR=blender -DBLENDER_LIB_DIR="$sdk" \
      -DHDW_BLENDER_USD_NAMESPACE="$USD_NAMESPACE" \
      -DHDW_BLENDER_PYTHON="$PYTHON_MM" \
      -DHDW_CXX_STANDARD="$CXX_STANDARD"
cmake --build "$build" -j"$(nproc)"
cmake --install "$build"
```

Every value arrives as `-D`, never through `$ENV{}`, so the configure line in the log is a complete
record of what was built.

**Run on 2026-09-18, 4.5 row: green**, through Step B5's `docker run` line (by hand; the script
didn't exist yet). Built in ~10s, `build/4.5/` owned by the caller. The `.so` has exactly GATE A's
six `NEEDED` entries and no `RUNPATH`, and the local smoke test (`blender/bpy.sh`, `bpy` 4.5.13)
prints `OK 16384 pixels, digest d8e910b3f3b763d6` — the **same** digest as the GCC 13 host build.
So for 4.5 the compiler does not perturb the image (GATE C item 2; confirm it on the other rows
there). The image has `cmake`, so the planned `pipx install cmake` fallback was dropped.

## Step B4 — `ci/check_so.sh <so>`

GATE A plus the ceilings. Reads `USD_NAMESPACE`:

```bash
#!/usr/bin/env bash
set -euo pipefail
so=$1
fail() { echo "check_so: $*" >&2; exit 1; }
maxver() { objdump -T "$so" | grep -oE "$1_[0-9.]+" | sed "s/^$1_//" | sort -uV | tail -1; }
le() { [[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -1)" == "$1" ]]; }

# Symbol-version ceilings. GLIBC 2.28 is Blender's stated floor (Rocky 8). GLIBCXX 3.4.25 and
# CXXABI 1.3.11 are GCC 8's libstdc++, i.e. what a Rocky 8 system provides; Blender's own
# libusd_ms.so needs only 3.4.22. A plain ubuntu-24.04 build gives 2.32 / 3.4.32 - see the plan.
g=$(maxver GLIBC);   le "$g" 2.28   || fail "needs GLIBC_$g > 2.28 - not built in manylinux_2_28?"
x=$(maxver GLIBCXX); le "$x" 3.4.25 || fail "needs GLIBCXX_$x > 3.4.25 - libstdc++ newer than Rocky 8's"
a=$(maxver CXXABI);  le "$a" 1.3.11 || fail "needs CXXABI_$a > 1.3.11"

# Captured, not piped into grep -q: grep exits at the first match, the writer dies of SIGPIPE, and
# pipefail turns a match into a failure.
syms=$(nm -DC "$so")
dyn=$(readelf -d "$so")

# Right namespace in, wrong one out
grep -q "$USD_NAMESPACE" <<<"$syms" || fail "no $USD_NAMESPACE symbols"
! grep -q 'pxrInternal_' <<<"$syms" || fail "vanilla pxrInternal_ symbols present"

# NEEDED allowlist - GATE A's list, verbatim. Anything else is a regression; libpython especially.
extra=$(sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' <<<"$dyn" \
    | grep -vxE 'libusd_ms\.so|libtbb\.so\.12|libstdc\+\+\.so\.6|libm\.so\.6|libgcc_s\.so\.1|libc\.so\.6' || true)
[[ -z "$extra" ]] || fail "unexpected NEEDED: $extra"
! grep -qE '\((RUNPATH|RPATH)\)' <<<"$dyn" || fail "RUNPATH/RPATH present - the design relies on SONAMEs, see blender-addon Step 0.2"
echo "check_so: OK (GLIBC_$g GLIBCXX_$x CXXABI_$a)"
```

**Changed while implementing (2026-09-18), two defects in the code as first planned:**

1. **`nm … | grep -q` under `set -o pipefail` fails on a match.** `grep -q` exits at the first hit,
   `nm` (637 matching lines here) dies of SIGPIPE, and pipefail reports the pipeline as failed — so
   the correct 4.5 `.so` was rejected with `no pxrBlender_v25_02 symbols`. The output is now captured
   once and grepped from a here-string. Step B2's `readelf | grep -q` had the same latent bug, masked
   only because its output is small; fixed the same way.
2. **`grep RUNPATH` missed old-style `DT_RPATH`**, which breaks the SONAME design equally. Now
   matches both.

**Run on 2026-09-18: green.** The 4.5 container `.so` passes: `check_so: OK (GLIBC_2.14
GLIBCXX_3.4.21 CXXABI_1.3.11)`. Every negative fails, each naming its cause: the host-built
[[blender-addon]] `.so` (`needs GLIBC_2.32 > 2.28`), the same with the GLIBC line disabled on a
scratch copy (`needs GLIBCXX_3.4.32 > 3.4.25`), the wrong namespace, and copies patched with the
image's `patchelf`: an added `libpython3.11.so.1.0` `NEEDED`, a `RUNPATH`, and an `RPATH`.

**Do not port GATE A's `ldd -r` check.** Its correct output depends on resolving against a Blender
install's `lib/`, which CI does not have, and its uncorrected form reports failure on a correct build
([[blender-addon]] GATE A, Correction 1). The smoke test's `dlopen` is the stronger version of the same
check.

## Step B5 — `ci/docker_build.sh <sdk> <build-dir>`

The one place the `docker run` line exists. The workflow's build step (Step B7) and `ci/local.sh`
both call it, so the local build and the CI build cannot drift apart. Reads `USD_NAMESPACE`,
`PYTHON_MM`, `CXX_STANDARD`, and optionally `MANYLINUX_IMAGE`:

```bash
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
sdk=$(realpath "$1") build=$2
[[ "$build" != /* ]] || { echo "docker_build: <build-dir> must be relative - only \$PWD is mounted" >&2; exit 1; }
image=${MANYLINUX_IMAGE:-quay.io/pypa/manylinux_2_28_x86_64:2026.09.14-1}   # bump deliberately
docker run --rm -v "$PWD:/src" -v "$sdk:/sdk:ro" -w /src \
    -e USD_NAMESPACE -e PYTHON_MM -e CXX_STANDARD -e HOST_IDS="$(id -u):$(id -g)" \
    "$image" bash -c 'ci/build.sh /sdk "$1"; s=$?; [[ -e "$1" ]] && chown -R "$HOST_IDS" "$1"; exit $s' _ "$build"
```

**The `chown`.** The container runs as root, so without it `build/<id>/` comes out root-owned: on a
throwaway runner that doesn't matter, but here it leaves a tree only `sudo` can delete. Ownership is
handed back from inside the container, success or failure — verified by the Step B3 run. On a
runner it is a harmless no-op. `--user` was avoided because it broke Step B3's `pipx` fallback; that
fallback is gone, but `chown` stays: it was already verified, and it keeps the container running as
root identically here and on the runner.

## Step B6 — `ci/local.sh <id>`

One row, on this machine, the way the workflow runs it:

```bash
#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run one ci/targets.toml row here, as .github/workflows/blender.yml runs it: the same ci/ scripts,
# in the same order. Sequencing only - build logic belongs in the scripts, so CI runs it too.
# See docs/plans/blender-ci.md Step B6.
set -euo pipefail
cd "$(dirname "$0")/.."
# CI never sources env.sh. A sourced shell's vanilla USD on LD_LIBRARY_PATH breaks bpy (blender/bpy.sh).
unset LD_LIBRARY_PATH PXR_PLUGINPATH_NAME
eval "$(ci/targets.py env "$1")"
sdk=build/sdk/$TARGET_ID build=build/$TARGET_ID

[[ -d "$sdk" ]] || ci/provision_sdk.sh "$sdk"      # the local stand-in for actions/cache
ci/verify_sdk.sh "$sdk"
ci/docker_build.sh "$sdk" "$build"
ci/check_so.sh "$build/install/plugin/usd/hdWeekend.so"
# Step C1 appends the bpy install + smoke test, Step D1 the package step.
```

`build/` is already git-ignored. A half-finished provision leaves a directory that `verify_sdk.sh`
rejects, exactly like a poisoned CI cache — and the fix is the same: delete it. Running every row is
`for id in $(ci/targets.py list); do ci/local.sh "$id"; done`.

## Step B7 — `.github/workflows/blender.yml`, part 1

```yaml
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build, check, smoke-test and package hdWeekend for every row of ci/targets.toml.
# Design: docs/notes/ci.md. Plan: docs/plans/blender-ci.md.
name: blender

on:
  push:
  workflow_dispatch:

permissions:
  contents: read

jobs:
  targets:
    runs-on: ubuntu-24.04
    outputs:
      matrix: ${{ steps.t.outputs.matrix }}
      version: ${{ steps.t.outputs.version }}
    steps:
      - uses: actions/checkout@v7
      - id: t
        run: python3 ci/targets.py github >> "$GITHUB_OUTPUT"

  build:
    needs: targets
    name: blender ${{ matrix.t.id }} / linux-x64
    runs-on: ubuntu-24.04
    timeout-minutes: 30
    strategy:
      fail-fast: false            # one broken row must not hide the state of the others
      matrix:
        t: ${{ fromJSON(needs.targets.outputs.matrix) }}
    env:
      TARGET_ID: ${{ matrix.t.id }}
      BPY_VERSION: ${{ matrix.t.blender }}
      LIB_BRANCH: ${{ matrix.t.lib_branch }}
      LIB_SHA: ${{ matrix.t.lib_sha }}
      USD_NAMESPACE: ${{ matrix.t.namespace }}
      PYTHON_MM: ${{ matrix.t.python }}
      CXX_STANDARD: ${{ matrix.t.cxx }}
    steps:
      - uses: actions/checkout@v7

      - id: sdk
        uses: actions/cache@v6
        with:
          path: sdk
          key: lib-linux_x64-${{ matrix.t.lib_sha }}

      - name: Provision SDK
        if: steps.sdk.outputs.cache-hit != 'true'
        run: ci/provision_sdk.sh sdk

      - name: Verify SDK
        run: ci/verify_sdk.sh sdk

      - name: Build (manylinux_2_28)
        run: ci/docker_build.sh sdk "build/$TARGET_ID"

      - name: Check binary
        run: ci/check_so.sh "build/$TARGET_ID/install/plugin/usd/hdWeekend.so"
```

No image in the workflow's `env:` — the pinned tag lives in `ci/docker_build.sh` (Step B5), the one
place both CI and `ci/local.sh` read it from.

**Written 2026-09-18** with `checkout@v7` and `cache@v6`, the current majors that day (the plan had
said `@v4`). Checked locally before any push: `actionlint` (its Docker image) reports nothing, and
`shellcheck` is clean on every `ci/*.sh`.

## GATE B — every row builds, and the ceilings bite

**Local — all of this before any push.** First the negative test. The [[blender-addon]] artifact
already on disk is exactly the binary `check_so.sh` exists to reject:

```bash
USD_NAMESPACE=pxrBlender_v25_02 ci/check_so.sh build-hydra-blender/install/plugin/usd/hdWeekend.so
# want: exit 1, "needs GLIBC_2.32 > 2.28"   (measured 2026-09-15: GLIBC 2.32, GLIBCXX 3.4.32)
```

Then temporarily comment out the GLIBC line and confirm the GLIBCXX line fails too, so both
assertions are known to fire. Restore it.

Then pin the image and settle the two unverified facts from "The build image" (**done 2026-09-18**,
tag `2026.09.14-1`, `cmake` present — see there; re-run only when bumping the tag):

```bash
docker pull quay.io/pypa/manylinux_2_28_x86_64:<current tag from quay.io>
docker run --rm quay.io/pypa/manylinux_2_28_x86_64:<tag> bash -c 'gcc --version | head -1; cmake --version | head -1'
```

Put the tag in `ci/docker_build.sh`. If `cmake` is present, delete Step B3's `pipx` fallback line.

Then every row:

```bash
for id in $(ci/targets.py list); do ci/local.sh "$id" || echo "ROW $id FAILED"; done 2>&1 | tee build/gate-b.log
```

Want, for all four rows: `verify_sdk: OK`, `gcc (GCC) 14.x`, `check_so: OK` with `GLIBC ≤ 2.28` and
`GLIBCXX ≤ 3.4.25` printed, and `build/<id>/` owned by you, not root. Record the printed versions per
row in a dated note here — that is the evidence the zip is publishable, and nothing else in this
plan provides it.

Expected ways this gate fails, most likely first — all of them now seen locally, in seconds:

1. **`cmake` missing after an image bump** — present in `2026.09.14-1`. If a later tag drops it,
   `pipx install cmake` in `ci/build.sh`, or `/opt/python/cp312-cp312/bin/pip install cmake`
   (manylinux images carry interpreters under `/opt/python`).
2. **A 5.x row fails to compile** — the prediction in "The USD side" was wrong. Add a `compat.h`
   entry, commented with the USD version, exactly as [[blender-addon]] Step A2 did. This is the CI
   working, not failing.
3. **`GLIBCXX` over ceiling on a C++20 row** — some C++20 library feature pulled in a symbol that
   `libstdc++_nonshared.a` does not cover. Find it with
   `objdump -T hdWeekend.so | grep GLIBCXX_3.4.2[6-9]` and remove the use; do not raise the
   ceiling.

And one deliberate break, locally: the 5.0 row checked against 4.5's namespace.

```bash
(eval "$(ci/targets.py env 5.0)"; USD_NAMESPACE=pxrBlender_v25_02 ci/verify_sdk.sh build/sdk/5.0)
# want: exit 1, naming pxrBlender_v25_08 (the SDK) and pxrBlender_v25_02 (the row)
```

That it fails *before* the build spends any time is by construction: Verify precedes Build in both
the workflow and `ci/local.sh`.

**Local part run on 2026-09-18: green, first attempt, 77 s for all four rows** (three SDKs
provisioned fresh). Log: `build/gate-b.log`.

| Row | `check_so: OK` | C++ | Python | Namespace | `.so` sha256 |
|---|---|---|---|---|---|
| 4.5 | GLIBC_2.14 GLIBCXX_3.4.21 CXXABI_1.3.11 | 17 | 3.11 | `pxrBlender_v25_02` | `532b6a29c13f…` |
| 5.0 | GLIBC_2.14 GLIBCXX_3.4.21 CXXABI_1.3.11 | 17 | 3.11 | `pxrBlender_v25_08` | `35cb576a1e0e…` |
| 5.1 | GLIBC_2.14 GLIBCXX_3.4.21 CXXABI_1.3.11 | 20 | 3.13 | `pxrBlender_v25_08` | `94173edd1d18…` |
| 5.2 | GLIBC_2.14 GLIBCXX_3.4.21 CXXABI_1.3.11 | 20 | 3.13 | `pxrBlender_v26_03` | `5d648d056247…` |

All built with `gcc (GCC) 14.2.1`; every `build/<id>/` owned by the caller. None of the three
expected failures happened: the C++20 rows stay at GLIBCXX 3.4.21, and 5.2 compiles against USD
26.03 through `compat.h`'s existing `HdRendererCreateArgs` branch, as "The USD side" predicted. The
namespace break fails as wanted: `verify_sdk: pxr.h namespace is PXR_INTERNAL_NS
pxrBlender_v25_08__pxrReserved__, row says pxrBlender_v25_02`, exit 1.

**Push part run on 2026-09-18 (run 35369431587, `42a2157`): green, first push.** All four rows print
the identical `check_so: OK (GLIBC_2.14 GLIBCXX_3.4.21 CXXABI_1.3.11)`, with `gcc (GCC) 14.2.1`, and
the pulled image digest is `sha256:531d7aa844bb…`, the same as the local one, so the pin holds. Every
row provisioned fresh on the runner (`git-lfs` works there), then saved its SDK cache (~50–62 MB
compressed). The 5.1/5.2 (C++20) logs carry many `-Wdeprecated-declarations` warnings from USD's own
headers (`std::atomic_load(shared_ptr)`, `std::is_pod_v`); they come from the SDK, not our code, and
do not affect the result. The `workflow_dispatch` re-run (same commit) gave `Cache hit for:
lib-linux_x64-1df155d9…`, restored in under a second, with Provision skipped. It still printed
`verify_sdk: OK` and the same `check_so` line, then `not saving cache`. **GATE B closed 2026-09-18.**

**Then push**, once all four rows are green locally. The push tests only what the local run cannot:
the workflow parses, `targets.py github` produces the matrix, `git-lfs` works on the runner, and the
container runs there. Want the same four greens, printing **the same** `check_so: OK (…)` versions as
the local note — a difference means the image isn't really pinned. Re-run the workflow once
(`workflow_dispatch`) and want `cache-hit` on the SDK step, still followed by `verify_sdk: OK`.
Expect one push, two if the YAML has a mistake.

---

# Stage C — smoke test every row

## Step C1 — workflow, part 2

Appended to the `build` job's steps:

```yaml
      - uses: astral-sh/setup-uv@v10.1.0     # no floating major tags upstream - pin the full version
        with:
          enable-cache: false                # see the GATE C note

      - name: Install bpy ${{ matrix.t.blender }}
        run: |
          uv venv --python "$PYTHON_MM" .venv-bpy
          uv pip install --python .venv-bpy/bin/python "bpy==$BPY_VERSION"

      - name: bpy loads
        # Separate from the smoke test so a missing system library reads as that, not as a
        # plugin failure. find, not `python -c "import bpy"`: bpy hangs at interpreter teardown.
        run: |
          so=$(find .venv-bpy -path '*/site-packages/bpy/__init__*.so' | head -1)
          ! LD_LIBRARY_PATH="$(dirname "$so")/lib" ldd "$so" | grep 'not found'

      - name: Smoke test
        env:
          HDW_PLUGIN_DIR: ${{ github.workspace }}/build/${{ matrix.t.id }}/install/plugin/usd
          HDW_SMOKE_TIMEOUT: "60"
          HDW_SMOKE_DIGEST: ${{ matrix.t.smoke_digest }}
        run: .venv-bpy/bin/python blender/smoke_test.py
```

No `blender/bpy.sh` — CI never sources `env.sh`, so there is no vanilla USD on `LD_LIBRARY_PATH` to
scrub. No `xvfb` — GATE F verified the CPU path renders with `DISPLAY`/`WAYLAND_DISPLAY`/
`XDG_RUNTIME_DIR` all unset (for 4.5; 5.x is covered by this gate).

`HDW_SMOKE_TIMEOUT` drops from 120 to 60: a 64×64×8spp frame takes ~0.2s, and the only thing the
watchdog catches is a hang, so waiting longer buys nothing but a slower red.

**Not verified:** the `bpy` 5.x wheels' system-library needs on `ubuntu-24.04`, and the exact
filename of the wheel's extension module (the `find` pattern). The "bpy loads" step turns a gap into
a named `.so`; add an `apt-get install` for whatever it names.

Appended to `ci/local.sh`, the same steps on this machine (`uv` is already installed here):

```bash
venv=build/venv-$TARGET_ID
[[ -x "$venv/bin/python" ]] || uv venv --python "$PYTHON_MM" "$venv"
# Every run, not just on creation: a reused venv must follow a bumped pin. A no-op when it matches.
uv pip install --python "$venv/bin/python" "bpy==$BPY_VERSION"
HDW_PLUGIN_DIR=$PWD/$build/install/plugin/usd HDW_SMOKE_TIMEOUT=60 \
    "$venv/bin/python" blender/smoke_test.py      # HDW_SMOKE_DIGEST comes from targets.py env
```

The local run verifies the `find` pattern and that the 5.x wheels load, on Ubuntu 24.04 — the
runner's OS. It does not prove the runner has every system library: a desktop install has more than
a runner image does, which is what the workflow's "bpy loads" step is still there for.

## GATE C — four greens, and a digest per row

**Local first:** `for id in $(ci/targets.py list); do ci/local.sh "$id"; done`. Want
`OK 16384 pixels, digest …` in every row. Then:

1. **Record each row's digest** in `ci/targets.toml` as `smoke_digest`, re-run the loop locally (it
   now asserts them), then push. CI asserting the same digests is the proof that a local container
   build and a CI container build render identically — so from then on a digest may be filled in
   from `ci/local.sh`, not only from CI.
2. **Compare the 4.5 digest with `d8e910b3f3b763d6`** (the host build: GCC 13, glibc 2.39). Same →
   the compiler does not perturb the image. Different → it does, and a digest must only ever come
   from a container build (`ci/local.sh` or CI), never from `build-hydra-blender/`. Record which in a
   note here.
3. **Expect 4.5/5.0/5.1/5.2 digests to possibly differ from each other** — different EXR writer and
   colour-management code per Blender. A cross-row difference is not a defect; a *within-row* change
   on an unchanged tracer is.
4. **Break it once, locally:** in every row, point `HDW_PLUGIN_DIR` at a nonexistent directory:

   ```bash
   for id in $(ci/targets.py list); do
       HDW_PLUGIN_DIR=/nonexistent build/venv-$id/bin/python blender/smoke_test.py; echo "$id exit=$?"
   done
   ```

   Want the `RuntimeError: hdWeekend plugInfo.json not found` from `engine.py`, exit 1, in every row,
   well under the timeout.

**Local part run on 2026-09-18: green, first attempt.** Log: `build/gate-c.log`.

| Row | bpy | Python | Digest |
|---|---|---|---|
| 4.5 | 4.5.13 | 3.11 | `d8e910b3f3b763d6` |
| 5.0 | 5.0.1 | 3.11 | `d8e910b3f3b763d6` |
| 5.1 | 5.1.2 | 3.13 | `d8e910b3f3b763d6` |
| 5.2 | 5.2.2 | 3.13 | `d8e910b3f3b763d6` |

- **All four rows give the same digest,** and it matches the host GCC 13 build, so both of this
  gate's open questions come out "no difference". Item 2: the compiler (GCC 14.2.1 in the container
  vs GCC 13 on the host, glibc 2.28 vs 2.39) does not change the image. Item 3: the rows' different
  EXR writers and colour code don't either, at least for a full-float EXR with the `Raw` view.
- **Each row does load its own plugin.** Identical digests could hide a row picking up another row's
  build, so I tested that: 5.2's bpy pointed at the 4.5 plugin fails with `undefined symbol:
  _ZTIN32pxrBlender_v25_02__pxrReserved__16HdRendererPluginE`, then `Cannot create render delegate`.
  The process then segfaults (exit 139) instead of exiting 1. It is still a red, so it was left alone.
- **Digests recorded in `ci/targets.toml`, then the loop re-ran:** green in every row. A wrong
  `HDW_SMOKE_DIGEST` gives `AssertionError: digest d8e910b3f3b763d6 != expected 0000…`, exit 1.
- **Item 4:** `HDW_PLUGIN_DIR=/nonexistent` gives the `RuntimeError: hdWeekend plugInfo.json not
  found at /nonexistent/hdWeekend/resources/plugInfo.json`, exit 1, in 0–1 s in every row.
- **Step C1's "not verified" items:** the extension module is `site-packages/bpy/__init__.so` in both
  the 3.11 and 3.13 venvs, so the `find` pattern matches. `ldd` reports nothing `not found` for any
  row on this Ubuntu 24.04 desktop.

**Push part run on 2026-09-18.** The first push failed before any step ran: `Unable to resolve
action astral-sh/setup-uv@v10`. setup-uv no longer publishes floating major tags, so it is pinned to
`@v10.1.0`. **Run 35374920110 (`6cecc66`) was green on all four rows. Each asserted `d8e910b3f3b763d6`
and printed it back,** which shows that CI's container build renders the same image as the local one.
So from here on a digest may be filled in from `ci/local.sh`. The runner needed no extra `apt-get`,
and the workflow's "bpy loads" step passed in every row. That run left two warnings per job, both
from setup-uv's cache, which is on by default on hosted runners. Its key is not per-row, so the four
rows raced to save one 366 MB entry that held only 5.0's wheel. And with no lock file in the repo,
that entry could never be invalidated. PyPI serves the wheel in 3–5 s, so the cache was turned off
with `enable-cache: false`. **Run 35376824345 (`fda07ec`): green, zero warnings, the same digest in
all four rows, about 1m05s per row. GATE C closed 2026-09-18.**

---

# Stage D — package and release

## Step D1 — workflow, part 3

Appended to `build`:

```yaml
      - name: Package
        env:
          HDW_INSTALL_DIR: build/${{ matrix.t.id }}/install
          ADDON_VERSION: ${{ needs.targets.outputs.version }}
          BLENDER_VERSION_MIN: ${{ matrix.t.version_min }}
          BLENDER_VERSION_MAX: ${{ matrix.t.version_max }}
          PLATFORMS: linux-x64
          ZIP_NAME: weekend_raytracer-${{ needs.targets.outputs.version }}-blender-${{ matrix.t.id }}-linux-x64.zip
        run: |
          export BLENDER_EXT=$(find .venv-bpy -path '*/bl_pkg/cli/blender_ext.py' | head -1)
          [[ -n "$BLENDER_EXT" ]] || { echo "blender_ext.py not found in bpy wheel" >&2; exit 1; }
          blender/build_zip.sh
          python3 "$BLENDER_EXT" validate "dist/$ZIP_NAME"

      - uses: actions/upload-artifact@v7
        with:
          name: blender-${{ matrix.t.id }}-linux-x64
          path: dist/*.zip
          if-no-files-found: error
```

Each row packages with **its own** `blender_ext.py`, so a 5.2 zip is validated by 5.2's rules.

`PLATFORMS` is literal rather than from the row: the row's `platforms` is a list for the day there
are several, and `--split-platforms` is the tool for that day.

The plan first used `upload-artifact@v4` / `download-artifact@v4`. Run 35377495890 (`788ec83`)
was green, but `@v4` runs on Node 20 and raised a deprecation warning on every row. On 2026-09-18
the latest versions were `upload-artifact@v7` and `download-artifact@v8`, both on Node 24 with
floating major tags. Nothing that changed since v4 affects the inputs used here.

Appended to `ci/local.sh`, the same step (`ADDON_VERSION` from `targets.py env` is the plain
version; the `-dev+g…` suffix is computed only in `github` mode):

```bash
export HDW_INSTALL_DIR=$build/install PLATFORMS=linux-x64 \
    ZIP_NAME=weekend_raytracer-$ADDON_VERSION-blender-$TARGET_ID-linux-x64.zip
export BLENDER_EXT=$(find "$venv" -path '*/bl_pkg/cli/blender_ext.py' | head -1)
[[ -n "$BLENDER_EXT" ]] || { echo "blender_ext.py not found in bpy wheel" >&2; exit 1; }
blender/build_zip.sh
"$venv/bin/python" "$BLENDER_EXT" validate "dist/$ZIP_NAME"
```

## Step D2 — the release job

```yaml
  release:
    if: github.ref_type == 'tag'
    needs: build
    runs-on: ubuntu-24.04
    permissions:
      contents: write
    steps:
      - uses: actions/download-artifact@v8
        with:
          path: dist
          merge-multiple: true
      - name: Draft release
        env:
          GH_TOKEN: ${{ github.token }}
        run: gh release create "$GITHUB_REF_NAME" dist/*.zip --draft --title "$GITHUB_REF_NAME" --repo "$GITHUB_REPOSITORY"
```

**Draft**, always. Publishing is an outward-facing act; the workflow prepares it and a person
presses the button. `needs: build` means one red row blocks the release — intended, since a release
missing a Blender version is a support regression.

## GATE D — a CI zip installs from clean and renders

The one GUI check, on the row this machine can run. Use the 4.5 zip from the rc dry run below, since
its pre-release version is the less certain one to install:

```bash
gh run download <run-id> -n blender-4.5-linux-x64 -D dist-ci    # same bytes as the release asset
$BLENDER --command extension validate dist-ci/weekend_raytracer-*-blender-4.5-linux-x64.zip

unset HDW_PLUGIN_DIR
$BLENDER --command extension remove user_default.weekend_raytracer    # dev symlink or earlier install
ls -A ~/.config/blender/4.5/extensions/user_default/                  # want: only .blender_ext
$BLENDER --command extension install-file --repo user_default -e dist-ci/weekend_raytracer-*-blender-4.5-linux-x64.zip
$BLENDER -b -E WEEKEND -o /tmp/gated_ -F PNG -f 1    # headless F12 on the default cube
$BLENDER      # then the GUI: Weekend engine, F12, Rendered viewport
```

Same reasoning as [[blender-addon]] GATE G: with the symlink or `HDW_PLUGIN_DIR` left in place, a
broken zip layout passes anyway.

Then check what only a real install can show:

- `unzip -l` lists exactly the six entries the local [[blender-addon]] zip has (`blender_manifest.toml`, three `.py`,
  `plugin/usd/hdWeekend.so`, `plugin/usd/hdWeekend/resources/plugInfo.json`) and nothing else.
- The manifest inside reads the run's version and `blender_version_max = "5.0.0"`.
- Blender 4.5 accepts a non-plain version (`-dev+g…`, `-rc.1`) for install. If it refused, non-tag
  builds would use plain `addon_version` and the git SHA would move to the zip filename.

**Optional, and the only direct proof of the whole glibc story:** Docker is installed (Stage B), so run the 4.5
smoke test inside `rockylinux:8` against the CI-built `.so` (`uv` installs Python 3.11 there; the
`bpy` wheel is `manylinux_2_28`). A green run there is what `check_so.sh`'s ceilings stand in for.

### The release dry run — an rc branch, not a throwaway tag

`ci/targets.py` fails the `targets` job on any tag other than `v{addon_version}`, so a throwaway tag
on `main` never reaches `release`, and `v0.3.0` must not be spent on a test. Instead, bump the
version on a local branch and tag that. Tags, branches and the draft are yours to create and delete;
nothing in this plan pushes them.

```bash
git tag -l 'v*'; gh release list                  # both names must be free
git switch -c ci/rc-dry-run main
sed -i 's/^addon_version = "0.3.0"$/addon_version = "0.3.0-rc.1"/' ci/targets.toml
git commit -m "ci: rc dry run (throwaway)" ci/targets.toml
git tag v0.3.0-rc.1 && git push origin v0.3.0-rc.1   # want: draft release, four zips
git tag v0.3.0-rc.2 && git push origin v0.3.0-rc.2   # want: targets fails on the mismatch

# after the install check, which downloads from the rc.1 run
gh release delete v0.3.0-rc.1 --yes --cleanup-tag
git push origin --delete v0.3.0-rc.2
git tag -d v0.3.0-rc.1 v0.3.0-rc.2
git switch main && git branch -D ci/rc-dry-run && rm -rf dist-ci
```

The tag push carries the commit, so the branch itself is never pushed. A read-only PAT cannot see
draft releases, through GraphQL or REST, so check the draft's assets in the browser.

**Run on 2026-09-18.**
- **D1:** `ci/local.sh 4.5` was green and built
  `weekend_raytracer-0.3.0-blender-4.5-linux-x64.zip`: six entries, and `validate` passed. A zip
  built with `ADDON_VERSION=0.3.0-dev+gfda07ec` also passed `validate`. **Run 35377495890
  (`788ec83`) was green on all four rows,** each uploading `blender-<id>-linux-x64`, but
  `upload-artifact@v4` raised a Node 20 warning per row. After the bump to `@v7`, **run 35377999974
  (`8a0a660`) was green with zero annotations.**
- **D2:** **run 35378482499 (`a9e3f79`, `main`)** was green, with `release` skipped.
- **Release dry run:** **run 35378896669 (`v0.3.0-rc.1`, `43aaccf`)** was green on all four rows.
  `download-artifact@v8` found four artifacts and each download's SHA256 matched its upload.
  `release` made a draft with the four `weekend_raytracer-0.3.0-rc.1-blender-<id>-linux-x64.zip`
  files, confirmed in the browser. **Run 35378977959 (`v0.3.0-rc.2`)** failed in `targets` with
  `tag 'v0.3.0-rc.2' does not match addon_version '0.3.0-rc.1' (want 'v0.3.0-rc.1')`, and `build`
  and `release` were skipped.
- **Install:** the rc.1 4.5 zip has the six entries and `version = "0.3.0-rc.1"`, and Blender
  4.5.13's `validate` passed. The earlier 0.3.0 install was removed first, then `install-file`
  reported `Installed "weekend_raytracer"`, so **Blender 4.5 accepts a pre-release version.** With
  `HDW_PLUGIN_DIR` unset, a headless `-b -E WEEKEND -f 1` rendered the default cube in 5.6 s. The
  GUI check (F12 and a Rendered viewport) passed too. The optional `rockylinux:8` run was not done.
- Cleanup: both remote tags and the local branch are gone, and `main` still has
  `addon_version = "0.3.0"`.

**GATE D closed 2026-09-18.**

---

# Stage E — watch for new Blender versions

[[ci]] §6, "watch, don't react".

## Step E1 — `ci/watch.py`

Stdlib only. Prints a markdown report and exits 0 whether or not it found drift (a finding is not a
CI failure; a crash is). Checks, all verified runnable today:

| Check | Source | Finding |
|---|---|---|
| Newer `bpy` patch in a row's minor | `https://pypi.org/pypi/bpy/json` | "4.5: 4.5.14 available (pinned 4.5.13)" |
| Row's `lib_branch` moved | `git ls-remote --heads …/lib-linux_x64.git` | "5.2: lib branch at `abc…`, pinned `ecbd…`" |
| USD or Python changed *on a release branch* | raw `versions.cmake` @ `lib_branch` | loud — this would break the ABI within a minor |
| New `blender-v*-release` branch with no row | `ls-remote` | "blender-v5.3-release exists — add a row" |
| `main`'s `USD_VERSION` / `PYTHON_VERSION` not in any row | raw `versions.cmake` @ `main` | early warning, months ahead of a release |
| `bpy` minor on PyPI with no row | PyPI JSON | "bpy 5.3.0 published — add a row" |

URLs, as used during this plan's verification:

```
https://projects.blender.org/blender/lib-linux_x64.git                                             (ls-remote)
https://projects.blender.org/blender/blender/raw/branch/<branch>/build_files/build_environment/cmake/versions.cmake
https://projects.blender.org/blender/lib-linux_x64/raw/branch/<branch>/usd/include/pxr/pxr.h
https://pypi.org/pypi/bpy/json
```

A "patch available" finding should include the exact `targets.toml` edit — new `blender`, and the
branch's current SHA for `lib_sha` — so acting on it is a paste.

## Step E2 — `.github/workflows/blender-watch.yml`

```yaml
name: blender-watch
on:
  schedule:
    - cron: "17 6 * * 1"        # Mondays; off the hour to dodge the scheduler's rush
  workflow_dispatch:
permissions:
  contents: read
  issues: write
jobs:
  watch:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v7
      - run: python3 ci/watch.py > report.md
      - name: Report in the run summary
        run: |
          if [[ -s report.md ]]; then cat report.md; else echo "No drift."; fi >> "$GITHUB_STEP_SUMMARY"
      - name: Report as one issue
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          n=$(gh issue list --label ci-watch --state open --json number -q '.[0].number // empty')
          if [[ ! -s report.md ]]; then
            [[ -z "$n" ]] || gh issue close "$n" --comment "No drift as of run $GITHUB_RUN_ID."
          elif [[ -n "$n" ]]; then
            gh issue edit "$n" --body-file report.md
          else
            gh issue create --title "Blender targets drift" --label ci-watch --body-file report.md
          fi
```

One issue, edited in place — not a new issue per week — and closed by the first clean run. Create
the `ci-watch` label once by hand, and make sure Issues are enabled on the repo (they were off:
the first dispatch, run 35381875985, failed with "repository has disabled issues"). The run
summary carries the report too, so every run shows it, clean or not.

## GATE E — it reports exactly what is true today

`workflow_dispatch` it. As of 2026-09-15 the correct report is **exactly one finding**: `4.5: 4.5.14
available (pinned 4.5.13)`. No branch moved (every `lib_sha` was pinned today), no new release
branch, `main` is USD 26.03 / Python 3.13.13, which the 5.2 row covers.

A clean run with *zero* findings would be as wrong as a noisy one, which is why the 4.5 row was
pinned one patch behind. If it has been a while since this was written, re-derive today's expected
report by hand from the four URLs first, then compare.

Then act on it the way the job intends: bump the row to 4.5.14 and its current branch SHA, clear its
`smoke_digest`, run `ci/local.sh 4.5`, record the digest it prints, and push — CI then asserts it. That is the full "new version" loop
from [[ci]] §6, exercised once while it is small.

**Run on 2026-09-18.** Re-derived by hand from the four URLs that day: nothing had changed since
2026-09-15, so the expected report was still the single 4.5.14 finding.

- Run 35381875985 failed: `watch.py` passed, but the repo had Issues disabled ("repository has
  disabled issues"). Issues were enabled, and the run-summary step was added (Step E2 above).
- Run 35382211057 was green and opened issue #1 with exactly one finding, `4.5: 4.5.14 available
  (pinned 4.5.13)`, and the paste block (lib_sha unchanged, because the branch had not moved).
- Before any of this ran in CI, a test against a changed copy of `targets.toml` made all six checks fire.
- Acting on it caught a bug in `ci/local.sh`: it reused an existing venv without installing the new
  pin, so the first "4.5.14" run actually tested 4.5.13. It now runs `uv pip install` every time
  (Step B6 updated). The real 4.5.14 run printed `d8e910b3f3b763d6`, unchanged.
- dc22a75 pushed the bump. `blender` run 35382747062 was green on all four rows (bpy 4.5.14 / 5.0.1 /
  5.1.2 / 5.2.2, each asserting `d8e910b3f3b763d6`), and the release job was skipped.
- `blender-watch` run 35382972232 was clean and closed issue #1 with "No drift as of run
  35382972232."

**GATE E closed 2026-09-18.**

---

# Stage F — close out

## Step F1 — correct [[ci]]

Add a dated "9/15 corrections" note rather than rewriting, listing what planning measured:

1. §1 / §2 — 5.0 and 5.1 are two targets: Python 3.11 vs 3.13, C++17 vs 20, TBB 2021.13 vs 2022.3.
2. §4 — the constraint is `GLIBCXX` as much as `GLIBC`; the locally built zip needs `GLIBCXX_3.4.32`.
3. §5 — a full-app download is **not** needed for `extension build`/`validate`: `blender_ext.py`
   is stdlib-only and ships in every `bpy` wheel.
4. §5 — `bpy` now has 4.5.14 and 5.2.2; the list there is a snapshot.
5. [[blender-addon]]'s ~1 GB SDK estimate is ~150 MB sparse.

## Step F2 — `docs/Roadmap.md`

Check `ci pipeline to compile and distribute blender add-on` under `0.3.0 - hydra delegate`. That
completes 0.3.0; tagging `v0.3.0` (which `ci/targets.toml`'s `addon_version` already matches) is
yours to do, and produces the milestone's draft release.

---

## Definition of done

- [x] `ci/targets.toml` has rows 4.5 / 5.0 / 5.1 / 5.2, and `ci/targets.py` rejects a malformed row by id
- [x] `hydra/CMakeLists.txt` takes `HDW_CXX_STANDARD` and `HDW_BLENDER_PYTHON`; local 4.5 and vanilla
      builds unchanged; smoke digest still `d8e910b3f3b763d6` locally
- [x] The manifest is rendered in one place, which fails on an unrendered placeholder
- [x] `check_so.sh` rejects the local [[blender-addon]] `.so` (GLIBC 2.32 / GLIBCXX 3.4.32), shown by running it
- [x] `ci/local.sh` runs every row green on this machine, through the same `ci/docker_build.sh` CI uses
- [x] All four rows build in manylinux_2_28 and pass `check_so.sh`, with printed versions recorded,
      and CI prints the same versions
- [x] All four rows pass the smoke test with an asserted per-row digest
- [x] A broken namespace row and a broken plugin path each fail with a message naming the cause
      (shown locally; CI runs the same scripts)
- [x] Each row uploads a zip validated by its own `blender_ext.py`
- [x] A CI-built 4.5 zip installs from clean in Blender 4.5 and renders F12
- [x] A `v*` tag produces a draft release with four zips; a mismatched tag fails early
- [x] `blender-watch` reports today's single real finding, and acting on it round-trips to green
- [x] [[ci]] carries the corrections note; Roadmap item checked, closing 0.3.0

---

## Design notes — decisions recorded so they aren't re-litigated

**Why `docker run` in a step, not `container:` on the job.** Only one command needs an old glibc: the
compile. Job-level `container:` would also move SDK provisioning (needs `git-lfs`), `actions/cache`,
`uv` and the `bpy` wheel install into Rocky 8 — all of which work out of the box on the host. The
smoke test running on glibc 2.39 against a 2.28-built `.so` is the safe direction, and Gate D's
optional Rocky 8 run covers the other one.

**Why test locally in Docker, not by pushing.** Added 2026-09-18, once Docker was installed. A
push-and-wait loop costs minutes per attempt and leaves each red attempt in the repo's CI history;
a local container run costs seconds and nothing. It is only a faithful stand-in because nothing is
duplicated: `ci/local.sh` is sequencing, the `docker run` line exists once (`ci/docker_build.sh`),
and the image tag is pinned in that one file. If the workflow ever gains a step with real logic in
YAML, move the logic into a `ci/` script and call it from both.

**Why pin `lib_sha`.** A branch name makes the build depend on the day it ran and makes the cache key
a lie the first time the branch moves. A SHA makes every build reproducible and every cache entry
permanent, and turns "Blender updated its SDK" from an invisible event into a watch-job line and a
one-line diff. The SHA-to-patch mapping is approximate — the lib branch has no per-patch tags — and
that is acceptable because the ABI surface that matters (namespace, sonames) is fixed per minor and
asserted, and the smoke test runs against the exact patch wheel.

**Why every row on every push.** [[ci]] §3's point is that USD API drift is caught by the Blender
axis, not the platform axis. Four jobs, SDK cached, a ~0.2s render: the whole matrix costs less than
one full-app download would have. Revisit only when platforms multiply it.

**Why an issue, not an automatic PR.** Two reasons. A bump is not mechanical: its `smoke_digest` is
unknown until CI runs it, so an auto-PR is incomplete by construction. And PRs opened with
`GITHUB_TOKEN` do not trigger workflow runs, so the PR would arrive without the one thing it needs —
CI — unless a PAT is stored, which this repo does not otherwise need.

**Why the ceilings are symbol versions, not "built in the right image".** Being in the right image is
the cause; `GLIBC_2.28` / `GLIBCXX_3.4.25` in `objdump -T` is the effect users actually depend on.
Checking the effect catches the cause going wrong (an image bump, a `cmake` fallback that pulls a
different toolchain, a C++20 feature outside `libstdc++_nonshared.a`) and also catches problems no
image choice would.

**Why one add-on version across rows, for now.** Blender's version guidelines want disjoint version
numbers per Blender window when publishing ([[ci]] §7). This task publishes to GitHub Releases, where
filenames already tell the rows apart. Choosing a track scheme now would mean guessing at
extensions.blender.org's behaviour: its manifest validator accepts `+buildmetadata` (verified), but
whether the *platform* treats `0.4.0+b45` and `0.4.0+b52` as distinct uploads is not verified.
`targets.py` owns version computation, so a per-row scheme later is a change in one function.

**Why not reuse local `bpy.sh`.** It exists to scrub a vanilla-USD `LD_LIBRARY_PATH` that only a
sourced `env.sh` creates. CI never has that environment; using the wrapper would add an indirection
that guards against nothing.

## Failure modes, and what each looks like

| Symptom | Cause | Where to look |
|---|---|---|
| `targets` job fails naming a row | malformed `targets.toml` row | Step 0.2 validator message |
| `verify_sdk: libusd_ms.so is not an ELF file` | LFS filters not installed, or a poisoned cache | `git lfs install --skip-repo`; delete the cache entry |
| `verify_sdk: pxr.h namespace is …` | row typo, or `lib_sha` on the wrong branch | the row vs the facts table |
| Configure: `no Python X headers` | `python` wrong for the row, or sparse checkout missing `python/include` | facts table, `python/include/` row |
| Compile error on one 5.x row only | USD API drift | add a `compat.h` entry with its USD version ([[ci]] §6) |
| `check_so: needs GLIBC_… > 2.28` | built outside the container | Step B5, `ci/docker_build.sh` |
| `check_so: needs GLIBCXX_… > 3.4.25` | a libstdc++ feature not in `libstdc++_nonshared.a` | `objdump -T … \| grep GLIBCXX` |
| Local: `permission denied` deleting `build/<id>/` | a `docker_build.sh` run killed before its `chown` | `docker run --rm -v "$PWD:/src" <image> chown -R "$(id -u):$(id -g)" /src/build` |
| Local: `permission denied … docker.sock` | shell predates joining the `docker` group | log out and in, or `sg docker -c '…'` |
| `check_so: unexpected NEEDED: libpython…` | Python linked, not just included | Step A1; [[blender-addon]] GATE A Correction 2 |
| "bpy loads" names a missing `.so` | runner lacks a system library the wheel needs | `apt-get install` it in the workflow |
| Smoke: `plugInfo.json not found` | wrong `HDW_PLUGIN_DIR` / install layout | Step C1 env |
| Smoke: `Timeout (0:01:00)!` with a stack in `render` | no AOV bound — `aovToken:` wiring | [[blender-addon]] GATE F item 2 |
| Smoke: digest mismatch, tracer unchanged | host or compiler changed the image | Gate C item 2's recorded answer |
| Package: `blender_ext.py not found` | wheel layout changed | the `find` pattern in Step D1 |
| Zip installs but engine absent | manifest placeholder unrendered, or version rejected | `render_manifest.sh` guard; Gate D version note |
| Watch report empty when a patch exists | PyPI/ls-remote parsing | Gate E's hand re-derivation |

## Next up

- **Windows and macOS.** Each is a new `platforms` entry, a new `lib-<platform>` provision script, and
  one unverified floor to verify first: MSVC runtime for Windows, `MACOSX_DEPLOYMENT_TARGET` for macOS
  ([[ci]] §4). `hydra/CMakeLists.txt`'s `libusd_ms.so` / `libtbb.so` literals become per-platform.
  `bpy` wheels exist for `win_amd64`, `win_arm64` and `macosx_11_0_arm64` at every pinned version
  (verified), so the smoke test carries over.
- **Publishing.** Pick the tracks versioning scheme, then upload per-row zips to extensions.blender.org.
  The draft GitHub Release is the input.
- **Vanilla-USD rows**, nightly, cached on the USD tag ([[ci]] §6).
- **Depth/Normal pass verification** via `OPEN_EXR_MULTILAYER`, still owed from GATE F.
