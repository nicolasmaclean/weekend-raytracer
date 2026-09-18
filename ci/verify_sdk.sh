#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Assert <dir> is a usable SDK for this row. Runs on every job, cache hit or not: a cache poisoned
# with LFS pointer files is otherwise permanent. Reads USD_NAMESPACE, PYTHON_MM.
# See docs/plans/blender-ci.md Step B2.
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
