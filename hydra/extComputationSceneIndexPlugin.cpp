#include "pxr/imaging/hd/sceneIndexPluginRegistry.h"
#include "pxr/imaging/hdsi/extComputationPrimvarPruningSceneIndex.h"

#include "extComputationSceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE


TF_DEFINE_PRIVATE_TOKENS(_tokens,
                         ((sceneIndexPluginName, "HdWeekend_ExtComputationSceneIndexPlugin")));

// Same string, same silent-failure rule as the implicit-surface plugin.
static const char *const _pluginDisplayName = "Weekend";

TF_REGISTRY_FUNCTION(TfType)
{
  HdSceneIndexPluginRegistry::Define<HdWeekend_ExtComputationSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
  const HdSceneIndexPluginRegistry::InsertionPhase insertionPhase = 0;

  HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
      _pluginDisplayName, _tokens->sceneIndexPluginName,
      /* inputArgs = */ nullptr, insertionPhase, HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

HdWeekend_ExtComputationSceneIndexPlugin::HdWeekend_ExtComputationSceneIndexPlugin() = default;

HdSceneIndexBaseRefPtr HdWeekend_ExtComputationSceneIndexPlugin::_AppendSceneIndex(
    const HdSceneIndexBaseRefPtr &inputScene, const HdContainerDataSourceHandle & /*inputArgs*/)
{
  return HdSiExtComputationPrimvarPruningSceneIndex::New(inputScene);
}

PXR_NAMESPACE_CLOSE_SCOPE
