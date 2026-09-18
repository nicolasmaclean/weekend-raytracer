# CD across Blender/USD versions

**Date:** 2026-09-07
**Question that framed this:** long term, how does CD rebuild the delegate for every supported
Blender/USD version when we push changes — and what happens when a new Blender/USD comes out that
we want to support?

**Companion note:** `docs/notes/compile-against-blenders-usd-binary.md` establishes *why* a separate
build per Blender version is unavoidable (Blender renames USD's internal namespace). This note is
the delivery pipeline built on top of that.

**Sources** (verified 2026-09-07):

- [Blender requirements](https://www.blender.org/download/requirements/) and the
  [Linux build handbook](https://developer.blender.org/docs/handbook/building_blender/linux/) — glibc floor
- [Python wheels / Platform Builds](https://docs.blender.org/manual/en/latest/advanced/extensions/python_wheels.html) — `platforms`, `--split-platforms`
- [Version Number Guidelines § Tracks](https://docs.blender.org/manual/en/latest/advanced/extensions/version_number_guidelines.html)
- [`bpy` on PyPI](https://pypi.org/pypi/bpy/json), [`usd-core` on PyPI](https://pypi.org/pypi/usd-core/json)
- `lib-linux_x64.git` LFS pointer for `usd/lib/libusd_ms.so` (branch `blender-v5.2-release`)
- `download.blender.org` release URL pattern (HTTP 200 confirmed for 5.2.1 and 4.5.13 linux-x64)

**9/15 corrections** (measured while planning `docs/plans/blender-ci.md`; the text below is left as
written):

1. §1 / §2 — 5.0 and 5.1 are two targets, not one: Python 3.11 vs 3.13, C++17 vs 20, TBB 2021.13
   vs 2022.3.
2. §4 — the constraint is `GLIBCXX` as much as `GLIBC`; the locally built zip needs
   `GLIBCXX_3.4.32`.
3. §5 — a full-app download is **not** needed for `extension build`/`validate`: `blender_ext.py` is
   stdlib-only and ships in every `bpy` wheel.
4. §5 — `bpy` now has 4.5.14 and 5.2.2; the version list there is a snapshot.
5. [[blender-addon]]'s ~1 GB SDK estimate is ~150 MB as a sparse checkout.

---

## 1. The shape of the problem

The only artifact that varies per target is one small `hdWeekend.so`. The add-on Python is
byte-identical across every Blender version and platform. So this is a compile-and-fan-out problem,
not an N-forks problem — which is what makes the whole thing tractable.

| Axis | Values | Cost per target |
| --- | --- | --- |
| Blender minor → USD version + namespace | 4.5 LTS (25.02), 5.0/5.1 (25.08), 5.2 (26.03), … | cheap — SDK is a 79 MB download |
| Platform | `linux-x64`, `windows-x64`, `macos-arm64`, `macos-x64`, `windows-arm64` | one runner each |
| Vanilla USD (usdview/husk users, our 26.05) | 26.05, … | **expensive** — source build |

### `usd-core` wheels are not a linkable SDK

Worth recording, because it kills the obvious shortcut for the vanilla-USD axis. Inspected
`usd_core-26.8-cp313-none-manylinux_2_27_x86_64.manylinux_2_28_x86_64.whl` (29.5 MB):

- 4 stray `.h` files total, **no `include/pxr/pxr.h`**
- the only shared objects are Python binding modules (`pxr/Ar/_ar.so`, `pxr/Gf/_gf.so`, …)

So every vanilla-USD target requires building USD from source. Blender targets are a download;
vanilla targets are an hour. They belong on different schedules (§6).

## 2. One source of truth: a targets file

`ci/targets.toml`, one row per target, and CI derives its job matrix from it:

```toml
[[target]]
id         = "blender-5.2"
blender    = "5.2.1"                # drives both the bpy wheel and the download URL
lib_branch = "blender-v5.2-release"
usd        = "26.03"
namespace  = "pxrBlender_v26_03"    # asserted against the checked-out pxr.h
python     = "3.13"                 # 3.11 for the 4.5 LTS row
platforms  = ["linux-x64", "windows-x64", "macos-arm64"]
```

Adding a Blender version becomes: **add a row, let CI tell you what breaks.** That is the entire
ergonomic argument for running the matrix on the write path rather than only at release time.

The `namespace` field earns its keep as an assertion — grep the checked-out
`usd/include/pxr/pxr.h` and fail immediately if it doesn't match the row. Otherwise a namespace
change surfaces as a link error late in the build, or as a runtime `TfType` miss, which is much
worse.

## 3. Per-commit pipeline

1. **Provision SDK** — sparse LFS checkout of `lib-<platform>` at `lib_branch` (see the companion
   note §4), cached on that branch's commit SHA. `libusd_ms.so` is 78,736,808 bytes; release
   branches barely move after release, so this is a near-permanent cache hit.
2. **Build in a glibc-2.28 container** — see §4, this is the important one.
3. **Smoke test against the `bpy` wheel** — §5.
4. **Package** one extension zip per (platform, track) — §7.

Suggested split: per-commit, run **every Blender row on `linux-x64` only** — that is what catches
USD API drift, and it is 3 fast jobs instead of 9. Save the full platform fan-out for release tags.

## 4. The glibc trap

Blender 5.x targets **glibc 2.28** (Ubuntu 18.10, Debian 10, RHEL/Rocky/Alma 8), and official
builds are produced on Rocky Linux 8 for VFX-reference-platform compatibility.

Build `hdWeekend.so` on `ubuntu-latest` and it links against glibc 2.39, then refuses to load on a
large share of Blender's own supported distros — while working perfectly on this dev box *and* in
CI. That is the classic silent CD failure for this kind of plugin.

Fix: build Linux targets inside `quay.io/pypa/manylinux_2_28_x86_64` (or a Rocky 8 image). Linking
a newer-GCC-compiled plugin against Rocky8-built `libusd_ms.so` is the safe direction; the reverse
is not.

**Unverified, needs checking when those platforms get added:** Blender 5.2's
`MACOSX_DEPLOYMENT_TARGET` and its MSVC runtime version. Both need the same treatment as the glibc
floor — match Blender's, don't inherit the runner's.

## 5. Verification: the `bpy` pip wheel is the test harness

PyPI ships `bpy` at exact patch releases — `4.2.0`…`4.2.23`, `4.5.0`…`4.5.13`, `5.0.x`, `5.1.x`,
`5.2.0`, `5.2.1`. Same Blender build, same `libusd_ms.so`, same `pxrBlender_*` namespace, as an
importable module: no app download, no display, no GPU.

```bash
pip install bpy==5.2.1     # python must match the row: 3.13 here, 3.11 for 4.5 LTS
python -c "
import bpy
# register the add-on, RegisterPlugins on the freshly built .so,
# set engine to WEEKEND, render 32x32, assert non-empty pixels + hash
"
```

This catches precisely the failure class that compiles clean and dies at runtime:

- namespace mismatch / unresolved symbols at `dlopen`
- wrong `LibraryPath` in `plugInfo.json`, or a `RegisterPlugins` path that doesn't resolve
- `TF_REGISTRY_FUNCTION` not firing
- `bl_delegate_id` not matching the registered `TfType`

Asserting on a pixel hash also gives a per-USD-version regression check on the tracer itself. Note
`bpy.utils.expose_bundled_modules()` in `register()` exists *for* the bpy-as-module path, so the
code under test is the shipped code.

The same `targets.toml` row drives both the build and the test, since the `bpy` wheel exists at
exactly the Blender versions in the matrix.

Full-app downloads are still wanted at release time — a real Blender binary is needed for
`blender --command extension build` validation. URL pattern (confirmed):

```
https://download.blender.org/release/Blender5.2/blender-5.2.1-linux-x64.tar.xz
https://download.blender.org/release/Blender4.5/blender-4.5.13-linux-x64.tar.xz
```

## 6. When a new Blender/USD version lands

Two halves; the first is the one that usually gets skipped.

### Watch, don't react

A weekly scheduled job that reads `USD_VERSION` from `build_files/build_environment/cmake/versions.cmake`
on Blender's `main` and on each release branch, plus `git ls-remote --heads` on the lib repo, and
diffs the result against `targets.toml`. When 5.3's alpha bumps USD, that opens a PR *months*
before release, instead of the information arriving via a user bug report. Both halves are a single
`curl` and a single `git ls-remote` — a ~20-line job.

### Then let the compiler triage

Add the row; CI fails on exactly the new target. Absorb drift in a `hydra/compat.h` of
`#if PXR_VERSION >= 2603` blocks — the `IsSupported(HdRendererCreateArgs const &, ...)` override is
already one such block waiting to be written if we ever want the 25.08 (Blender 5.0/5.1) row.

Comment each shim with the USD version that introduced it. That header then doubles as the
support-window ledger: dropping Blender 4.5 is "delete every `< 2502` branch", and the carrying
cost of each old version stays visible instead of accumulating invisibly.

Vanilla-USD rows follow the same flow but stay off the per-commit path: nightly or on-demand, with
the built USD tree cached on its release tag.

## 7. Distribution: tracks + platforms

Blender's official guidance for supporting multiple Blender versions is **tracks** — concurrent
version numbers with disjoint `blender_version_min` / `blender_version_max`. If we're on 1.2.1 and
Blender 5.3 needs a different binary, 1.3.0 targets 5.3 while 1.2.x keeps taking patch releases for
5.2 users. The guidelines explicitly warn against expressing this with patch bumps alone.

`platforms` in `blender_manifest.toml` handles the OS split, and
`blender --command extension build --split-platforms` emits one zip per platform (the split is
driven by the `platforms` field).

Template the manifest from the target row so `blender_version_min` / `max` / `platforms` are always
generated, never hand-edited.

**Don't use the `wheels` mechanism.** `hdWeekend.so` is not a Python module — it's a `Plug` plugin
resolved through `LibraryPath` — so it ships as a plain file in the zip and `register()` derives its
path from `__file__`.

**Alternative considered:** one fat zip carrying `lib/blender-5.2/hdWeekend.so`,
`lib/blender-4.5/…`, dispatching on `bpy.app.version` at registration. Simpler to distribute, but
per-binary Blender requirements become inexpressible and the zip grows with every supported
version. Only worth it if we self-host instead of publishing to extensions.blender.org.

## 8. Explicitly not doing

- **Building Blender in CI.** Hours of compute for something that is a 79 MB download.
- **Rebuilding Blender's patched USD.** See companion note §4 — namespace rename plus ~9 patches.
- **Matrixing on USD patch versions.** The namespace is per USD version *Blender ships*, so Blender
  minors are the real axis.
- **Vanilla-USD source builds on the per-commit path.** Nightly, cached on the USD tag.
