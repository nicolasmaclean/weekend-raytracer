// SPDX-FileCopyrightText: 2025-2026 Nick Maclean
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <pxr/pxr.h>


// USD version compatibility for hdWeekend.
//
// PXR_VERSION is major*10000 + minor*100 + patch: 2502 for Blender 4.5's USD 25.02,
// 2605 for our own build. Every macro below records the version that introduced the
// change, so this header doubles as the support-window ledger.

// --- HdRendererPlugin::IsSupported ------------------------------------ USD 26.03
// <= 25.08: `virtual bool IsSupported(bool gpuEnabled = true) const = 0;` is the pure
//           virtual, and HdRendererCreateArgs does not exist.
//  > 26.03: IsSupported(HdRendererCreateArgs const &, std::string *) is the pure
//           virtual; the bool form survives as a deprecated, non-pure overload.
#if PXR_VERSION >= 2603
#define HDW_HAS_RENDERER_CREATE_ARGS 1
#else
#define HDW_HAS_RENDERER_CREATE_ARGS 0
#endif

