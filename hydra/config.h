#pragma once

#include "pxr/pxr.h"
#include "pxr/base/tf/singleton.h"
#include "pxr/base/tf/staticTokens.h"

PXR_NAMESPACE_OPEN_SCOPE


/// The two standard tokens we also honour live in HdRenderSettingsTokens and
/// are NOT repeated here:
///
///   HdRenderSettingsTokens->convergedSamplesPerPixel -> renderer::samples_to_converge
///   HdRenderSettingsTokens->threadLimit              -> WorkSetConcurrencyLimit
///
#define HDWEEKEND_RENDER_SETTINGS_TOKENS                                                                     \
  ((maxBounces, "hdWeekend:maxBounces"))((randomNumberSeed, "hdWeekend:randomNumberSeed"))(                  \
      (tileSize, "hdWeekend:tileSize"))((jitterCamera, "hdWeekend:jitterCamera"))

TF_DECLARE_PUBLIC_TOKENS(HdWeekendRenderSettingsTokens, HDWEEKEND_RENDER_SETTINGS_TOKENS);


// Defaults, shared by the env settings and by _PopulateDefaultSettings(). Types
// are restricted to bool/int/string, because that is all TF_DEFINE_ENV_SETTING
// accepts - note in particular that the seed is an int here and only widens to
// renderer::frame_seed's uint64_t at assignment.
constexpr int HdWeekendDefaultSamplesToConvergence = 100;
constexpr int HdWeekendDefaultMaxBounces = 20;
constexpr int HdWeekendDefaultRandomNumberSeed = -1;
constexpr int HdWeekendDefaultTileSize = 8;
constexpr bool HdWeekendDefaultJitterCamera = true;

// 0 means "all cores". There is deliberately no HDWEEKEND_THREAD_LIMIT: Work
// already reads PXR_WORK_THREAD_LIMIT, and a second variable for the same knob
// would just be a way to disagree with it.
constexpr int HdWeekendDefaultThreadLimit = 0;


/// \class HdWeekendConfig
///
/// Singleton holding hdWeekend's configuration. Every parameter has a default
/// and an environment variable that overrides it, read once at first access.
///
/// This is the headless half of §15's two mechanisms: render settings can be
/// driven interactively from usdview's settings panel, but in a `usdrecord` or
/// test run there is no panel, and editing the .usda to change sample count is
/// not an option. The values here become the fallbacks passed to
/// _PopulateDefaultSettings(), so an env var set before launch shows up as the
/// starting value of the corresponding setting.
///
/// Set HDWEEKEND_PRINT_CONFIGURATION to have the resolved values printed at
/// startup - the fastest way to tell "my env var was ignored" from "my env var
/// was applied and did nothing".
///
class HdWeekendConfig
{
public:
  static const HdWeekendConfig &GetInstance();

  /// How many samples per pixel before the image is considered converged?
  ///
  /// Override with *HDWEEKEND_SAMPLES_TO_CONVERGENCE*. Clamped to >= 1.
  int samplesToConvergence = HdWeekendDefaultSamplesToConvergence;

  /// How many times may a ray scatter before it is terminated? Low values
  /// darken the image; 0 means camera rays return the miss colour only.
  ///
  /// Override with *HDWEEKEND_MAX_BOUNCES*. Clamped to >= 0.
  int maxBounces = HdWeekendDefaultMaxBounces;

  /// Seed for the per-sample RNG (renderer::frame_seed). Anything other than
  /// -1 gives a repeatable image for a given scene and camera, because
  /// sample_seed(pixel, sample, frame_seed) does not depend on which thread
  /// picked up which tile. -1 lets the implementation vary it per invocation.
  ///
  /// Override with *HDWEEKEND_RANDOM_NUMBER_SEED*.
  int randomNumberSeed = HdWeekendDefaultRandomNumberSeed;

  /// Edge length, in pixels, of one unit of parallel work. Also the granularity
  /// at which a render can be cancelled, so a large value makes tumbling in
  /// usdview feel sluggish.
  ///
  /// Override with *HDWEEKEND_TILE_SIZE*. Clamped to >= 1.
  int tileSize = HdWeekendDefaultTileSize;

  /// Should camera rays be jittered within the pixel for antialiasing? Turning
  /// this off makes two renders of the same scene compare exactly, which is
  /// what the A/B image diffs rely on.
  ///
  /// Override with *HDWEEKEND_JITTER_CAMERA*.
  bool jitterCamera = HdWeekendDefaultJitterCamera;

private:
  // Reads the environment, clamps to valid ranges, and optionally prints.
  HdWeekendConfig();
  ~HdWeekendConfig() = default;

  HdWeekendConfig(const HdWeekendConfig &) = delete;
  HdWeekendConfig &operator=(const HdWeekendConfig &) = delete;

  friend class TfSingleton<HdWeekendConfig>;
};


PXR_NAMESPACE_CLOSE_SCOPE

