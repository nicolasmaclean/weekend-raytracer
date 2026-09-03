#include <algorithm>
#include <iostream>

#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/instantiateSingleton.h"

#include "config.h"

PXR_NAMESPACE_OPEN_SCOPE


TF_INSTANTIATE_SINGLETON(HdWeekendConfig);

TF_DEFINE_PUBLIC_TOKENS(HdWeekendRenderSettingsTokens, HDWEEKEND_RENDER_SETTINGS_TOKENS);

TF_DEFINE_ENV_SETTING(HDWEEKEND_SAMPLES_TO_CONVERGENCE, HdWeekendDefaultSamplesToConvergence,
                      "Samples per pixel before the image is considered converged (must be >= 1)");

TF_DEFINE_ENV_SETTING(HDWEEKEND_MAX_BOUNCES, HdWeekendDefaultMaxBounces,
                      "Maximum scatter depth per camera ray (must be >= 0; 0 gives miss colour only)");

TF_DEFINE_ENV_SETTING(HDWEEKEND_RANDOM_NUMBER_SEED, HdWeekendDefaultRandomNumberSeed,
                      "Seed for the per-sample RNG. Any value other than -1 gives a repeatable image,"
                      " because sample_seed(pixel, sample, frame_seed) does not depend on which thread"
                      " picked up which tile - so unlike hdEmbree this does NOT also require"
                      " PXR_WORK_THREAD_LIMIT=1. A value of -1 (the default) varies per invocation.");

TF_DEFINE_ENV_SETTING(HDWEEKEND_TILE_SIZE, HdWeekendDefaultTileSize,
                      "Edge length in pixels of one unit of parallel work, and the granularity at which"
                      " a render can be cancelled (must be >= 1)");

TF_DEFINE_ENV_SETTING(HDWEEKEND_JITTER_CAMERA, HdWeekendDefaultJitterCamera,
                      "Should camera rays be jittered within the pixel for antialiasing? Turning this"
                      " off makes two renders of the same scene compare exactly.");

TF_DEFINE_ENV_SETTING(HDWEEKEND_ENABLE_SCENE_COLORS, HdWeekendDefaultEnableSceneColors,
                      "Should a mesh's authored displayColor primvar drive its albedo? Off renders"
                      " every mesh as the same grey.");

TF_DEFINE_ENV_SETTING(HDWEEKEND_PRINT_CONFIGURATION, false,
                      "Should hdWeekend print its resolved configuration on startup?");

HdWeekendConfig::HdWeekendConfig()
{
  // Clamp on the way in. These become the fallbacks handed to
  // _PopulateDefaultSettings(), so a garbage env var would otherwise show up as
  // a garbage starting value in usdview's settings panel.
  samplesToConvergence = std::max(1, TfGetEnvSetting(HDWEEKEND_SAMPLES_TO_CONVERGENCE));
  maxBounces = std::max(0, TfGetEnvSetting(HDWEEKEND_MAX_BOUNCES));
  randomNumberSeed = TfGetEnvSetting(HDWEEKEND_RANDOM_NUMBER_SEED);
  tileSize = std::max(1, TfGetEnvSetting(HDWEEKEND_TILE_SIZE));
  jitterCamera = TfGetEnvSetting(HDWEEKEND_JITTER_CAMERA);
  enableSceneColors = TfGetEnvSetting(HDWEEKEND_ENABLE_SCENE_COLORS);

  if (TfGetEnvSetting(HDWEEKEND_PRINT_CONFIGURATION))
  {
    std::cout << "hdWeekend configuration:\n"
              << "  samplesToConvergence = " << samplesToConvergence << "\n"
              << "  maxBounces           = " << maxBounces << "\n"
              << "  randomNumberSeed     = " << randomNumberSeed << "\n"
              << "  tileSize             = " << tileSize << "\n"
              << "  jitterCamera         = " << jitterCamera << "\n"
              << "  enableSceneColors    = " << enableSceneColors << "\n"
              << std::flush;
  }
}

/*static*/
const HdWeekendConfig &HdWeekendConfig::GetInstance()
{
  return TfSingleton<HdWeekendConfig>::GetInstance();
}


PXR_NAMESPACE_CLOSE_SCOPE
