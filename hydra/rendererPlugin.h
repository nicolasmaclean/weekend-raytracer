// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
// SPDX-FileCopyrightText: 2025-2026 Nick Maclean
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Modifications to this file are licensed GPL-3.0-or-later. The original Pixar
// material remains under the Tomorrow Open Source Technology License 1.0.
#pragma once

#include "pxr/pxr.h"
#include "pxr/imaging/hd/rendererPlugin.h"

#include "hydra/compat.h"

PXR_NAMESPACE_OPEN_SCOPE


/// \class HdWeekendRendererPlugin
///
/// A registered child of HdRendererPlugin, this is the class that gets
/// loaded when a Hydra application asks to draw with a certain renderer.
/// It supports rendering via creation/destruction of renderer-specific
/// classes. The render delegate is the Hydra-facing entrypoint into the
/// renderer; it's responsible for creating specialized implementations of Hydra
/// prims (which translate scene data into drawable representations) and Hydra
/// renderpasses (which draw the scene to the framebuffer).
///
class HdWeekendRendererPlugin final : public HdRendererPlugin
{
public:
  HdWeekendRendererPlugin() = default;
  virtual ~HdWeekendRendererPlugin() = default;

  /// Construct a new render delegate of type HdWeekendRenderDelegate.
  HdRenderDelegate *CreateRenderDelegate() override;

  /// Construct a new render delegate of type HdWeekendRenderDelegate.
  HdRenderDelegate *CreateRenderDelegate(HdRenderSettingsMap const &settingsMap) override;

  /// Destroy a render delegate created by this class's CreateRenderDelegate.
  ///   \param renderDelegate The render delegate to delete.
  void DeleteRenderDelegate(HdRenderDelegate *renderDelegate) override;

  /// Checks to see if the plugin is supported on the running system.
#if HDW_HAS_RENDERER_CREATE_ARGS
  bool IsSupported(HdRendererCreateArgs const &rendererCreateArgs,
                   std::string *reasonWhyNot = nullptr) const override;
#else
  bool IsSupported(bool gpuEnabled = true) const override;
#endif

private:
  // This class does not support copying.
  HdWeekendRendererPlugin(const HdWeekendRendererPlugin &) = delete;
  HdWeekendRendererPlugin &operator=(const HdWeekendRendererPlugin &) = delete;
};

PXR_NAMESPACE_CLOSE_SCOPE

