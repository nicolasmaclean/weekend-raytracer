#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Assert a built hdWeekend.so is publishable: symbol-version ceilings, the row's USD namespace, and
# GATE A's NEEDED allowlist. Reads USD_NAMESPACE. See docs/plans/blender-ci.md Step B4.
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
