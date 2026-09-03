#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hdsi/implicitSurfaceSceneIndex.h"

#include "implicitSurfaceSceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE


TF_DEFINE_PRIVATE_TOKENS(_tokens,
                         ((sceneIndexPluginName, "HdWeekend_ImplicitSurfaceSceneIndexPlugin")));

// MUST match `displayName` on HdWeekendRendererPlugin in plugInfo.json, and the
// `loadWithRenderer` of this plugin's own entry, exactly. A mismatch is silent:
// the scene index is never inserted, so geometry vanishes with no error and no
// warning.
static const char *const _pluginDisplayName = "Weekend";

TF_REGISTRY_FUNCTION(TfType)
{
  HdSceneIndexPluginRegistry::Define<HdWeekend_ImplicitSurfaceSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
  const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

  HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
      _pluginDisplayName, _tokens->sceneIndexPluginName,
      /* inputArgs = */ nullptr, insertionPhase, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdWeekend_ImplicitSurfaceSceneIndexPlugin::HdWeekend_ImplicitSurfaceSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdWeekend_ImplicitSurfaceSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle & /*inputArgs*/)
{
  // One mode token per prim type. Omitting a type would pass it through
  // untouched, which is exactly what we do not want here - the args are built
  // locally rather than in the TF_REGISTRY_FUNCTION above because the token
  // registry is not guaranteed initialised at registration time.
  HdDataSourceBaseHandle const toMeshSrc =
      HdRetainedTypedSampledDataSource<TfToken>::New(HdsiImplicitSurfaceSceneIndexTokens->toMesh);

  HdContainerDataSourceHandle const localInputArgs = HdRetainedContainerDataSource::New(
      HdPrimTypeTokens->sphere, toMeshSrc, HdPrimTypeTokens->cube, toMeshSrc,
      HdPrimTypeTokens->cone, toMeshSrc, HdPrimTypeTokens->cylinder, toMeshSrc,
      HdPrimTypeTokens->capsule, toMeshSrc, HdPrimTypeTokens->plane, toMeshSrc);

  return HdsiImplicitSurfaceSceneIndex::New(inputScene, localInputArgs);
}

PXR_NAMESPACE_CLOSE_SCOPE
