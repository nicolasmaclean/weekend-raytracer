// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
#pragma once

#include "pxr/pxr.h"
#include "pxr/imaging/hd/rendererPlugin.h"

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
  bool IsSupported(HdRendererCreateArgs const &rendererCreateArgs,
                   std::string *reasonWhyNot = nullptr) const override;

private:
  // This class does not support copying.
  HdWeekendRendererPlugin(const HdWeekendRendererPlugin &) = delete;
  HdWeekendRendererPlugin &operator=(const HdWeekendRendererPlugin &) = delete;
};

PXR_NAMESPACE_CLOSE_SCOPE

