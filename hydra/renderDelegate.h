// Copyright 2020 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.

#pragma once

#include <atomic>
#include <memory>

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/imaging/hd/resourceRegistry.h"

#include "renderer.h"
#include "renderParam.h"

PXR_NAMESPACE_OPEN_SCOPE


/// \class HdWeekendRenderDelegate
///
/// Render delegates provide renderer-specific functionality to the render
/// index, the main hydra state management structure. The render index uses
/// the render delegate to create and delete scene primitives, which include
/// geometry and also non-drawable objects. The render delegate is also
/// responsible for creating renderpasses, which know how to draw this
/// renderer's scene primitives.
///
class HdWeekendRenderDelegate final : public HdRenderDelegate
{
public:
  HdWeekendRenderDelegate();
  HdWeekendRenderDelegate(HdRenderSettingsMap const &settingsMap);
  virtual ~HdWeekendRenderDelegate();

  /// Supported types
  const TfTokenVector &GetSupportedRprimTypes() const override;
  const TfTokenVector &GetSupportedSprimTypes() const override;
  const TfTokenVector &GetSupportedBprimTypes() const override;

  // Basic value to return from the RD
  HdResourceRegistrySharedPtr GetResourceRegistry() const override;

  // Prims
  HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index, HdRprimCollection const &collection) override;

  HdInstancer *CreateInstancer(HdSceneDelegate *delegate, SdfPath const &id) override;
  void DestroyInstancer(HdInstancer *instancer) override;

  HdRprim *CreateRprim(TfToken const &typeId, SdfPath const &rprimId) override;
  void DestroyRprim(HdRprim *rPrim) override;

  HdSprim *CreateSprim(TfToken const &typeId, SdfPath const &sprimId) override;
  HdSprim *CreateFallbackSprim(TfToken const &typeId) override;
  void DestroySprim(HdSprim *sprim) override;

  HdBprim *CreateBprim(TfToken const &typeId, SdfPath const &bprimId) override;
  HdBprim *CreateFallbackBprim(TfToken const &typeId) override;
  void DestroyBprim(HdBprim *bprim) override;

  void CommitResources(HdChangeTracker *tracker) override;

  HdRenderParam *GetRenderParam() const override;

  HdAovDescriptor GetDefaultAovDescriptor(TfToken const &name) const override;

  VtDictionary GetRenderStats() const override;

  // The settings usdview's panel offers, and their starting values. Built once
  // in _Initialize() from HdWeekendConfig, so an env var set before launch is
  // what the panel opens on.
  HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;

  bool IsPauseSupported() const override { return true; }
  bool IsStopSupported() const override { return true; }

  bool Pause() override
  {
    _renderThread.PauseRender();
    return true;
  }

  bool Resume() override
  {
    _renderThread.ResumeRender();
    return true;
  }

  bool Stop(bool blocking) override
  {
    _renderThread.StopRender();
    return true;
  }

private:
  static const TfTokenVector SUPPORTED_RPRIM_TYPES;
  static const TfTokenVector SUPPORTED_SPRIM_TYPES;
  static const TfTokenVector SUPPORTED_BPRIM_TYPES;

  void _Initialize();

  HdResourceRegistrySharedPtr _resourceRegistry;

  HdRenderSettingDescriptorList _settingDescriptors;

  // The renderer is delegate-scoped: it holds the scene and the camera across
  // _Execute calls so the pass has something to diff against. Every pass this
  // delegate creates shares it.
  HdWeekendRenderer _renderer;

  // Unused until stage C, but the pass takes it in its constructor from now so
  // that signature doesn't change when the thread arrives.
  HdRenderThread _renderThread;

  // Bumped by HdWeekendRenderParam on every scene edit (stage B). Starts at 1
  // so the pass's _lastSceneVersion of 0 forces a first render.
  std::atomic<int> _sceneVersion{1};

  // The only route a prim has to the scene (§6). Constructed in _Initialize(),
  // after the three members it points at; handed out by GetRenderParam() and
  // owned for the delegate's whole lifetime, since prims hold the raw pointer.
  std::unique_ptr<HdWeekendRenderParam> _renderParam;

  // This class does not support copying.
  HdWeekendRenderDelegate(const HdWeekendRenderDelegate &) = delete;
  HdWeekendRenderDelegate &operator=(const HdWeekendRenderDelegate &) = delete;
};


PXR_NAMESPACE_CLOSE_SCOPE

