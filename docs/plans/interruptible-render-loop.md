# interruptible tile-driven render loop — step-by-step

**Roadmap item:** `0.2.0 - hydra prep` → `interruptible tile-driven render loop` (`switch OMP to TBB` is already done) — see [[Roadmap]]
**Context:** [[hydra-spec]] §10.1, §10.2, §15, §17.6, §17.7, §20 (T3) · [[roadmap-discussion-8-26]] §2 "Interruptible rendering" / "The tracer must stop owning its parallel loop" · [[render-target]] "Next up"
**Every number in this document was measured on this machine on 2026-08-26. See "Pre-verified facts".**

---

## What this task is

[[hydra-spec]] §17.7 states the requirement in one sentence:

> A renderer whose `Render()` is an uninterruptible blocking call will make the viewport
> unusable.

and §17.6 states the other half:

> USD is built against oneTBB; a renderer with its own TBB arena or a private thread pool
> will oversubscribe inside a DCC. Route parallelism through `pxr/base/work` and respect
> `WorkGetConcurrencyLimitSetting()`.

Today `renderer::render` opens a `tbb::parallel_for` over a `blocked_range2d` and returns when
the last pixel is done. It cannot be stopped, it cannot be paused, and it decides for itself how
many threads to use and which pool they come from. Every one of those is a property the host owns
in Hydra.

At the end of this task the renderer:

- **owns the sample loop** (one pass == one sample per pixel over the whole data window) instead
  of receiving a sample count from the caller,
- **does not own the parallel dispatch** — it calls a caller-supplied `tile_scheduler`, which is
  `WorkParallelForN` in the delegate, `tbb::parallel_for` in the cli/viewer, and a plain serial
  call by default,
- **polls a caller-supplied `render_control`** for stop and pause: once per pass, and once per
  tile so pass 0 is interruptible too,
- **publishes `completed_samples()`** for `GetRenderStats()`, and sets `is_converged()` on every
  bound buffer when the loop ends.

`tracer/renderer.h` stops including any TBB header at all. That is what lets `hydra/` include the
renderer without dragging a second TBB runtime into a process that already has USD's.

Nothing about the output changes. Step 6 gates that as **byte-identical**, not "close".

## Why it matters more than "make render() cancellable" sounds

1. **Two TBB runtimes are already on this machine.** USD links `libtbb.so.2` (TBB 2020 —
   `tbb::internal::NFS_Allocate` is still an exported symbol); the tracer vendors oneTBB
   `libtbb.so.12`. Loading a plugin that links the second one into a process holding the first is
   the §17.6 hazard in its literal form. The only structural defence is that
   `tracer/renderer.h` contains no `#include <tbb/…>`, which step 6's gate compiles for.
2. **`StopRender()` is called from many Hydra threads and must return promptly.** It is what
   every scene edit goes through (§6's `AcquireSceneForEdit`), so its latency is the interactive
   latency of the whole delegate. Measured through a real `HdRenderThread`: **0.05–0.16 ms**.
   That number only exists because the cancellation point is inside the tile loop; with 64-pixel
   tiles at 800×450 the same test measures **47–66 ms**.
3. **Pass 0 is the frame the user sees.** A cancellation point that only fires between passes
   makes the *first* pass — the one that runs while the user is still dragging the camera —
   uninterruptible. hdEmbree passes the render thread into `_RenderTiles` for exactly this reason
   and says so in a comment (`renderer.cpp:576-578`).
4. **`samples` as a call argument is the wrong ownership.** Hydra decides convergence
   (`convergedSamplesPerPixel`, §15) and asks the renderer to keep going until it is stopped. A
   caller that hands over a batch size is a caller that owns the progressive policy — and today
   that policy is duplicated in the viewer's exponential-moving-average sample budget, which
   exists only because `render()` cannot be left running.

## What is explicitly NOT in this task

| Not now | Comes with |
|---|---|
| `HdRenderThread` member, `SetRenderCallback`, `StartThread`/`StopThread` | `hydra wrapper` (0.3.0) — Appendix A is written and compiled, just not committed to `hydra/` |
| `HdRenderParam` + `AcquireSceneForEdit()` gateway, `sceneVersion` | `scene graph with mutation` |
| `HdRenderPass::_Execute` 5-way change detection, `MarkAovBuffersUnconverged` from the pass | `hydra wrapper` (0.3.0) — sketched in Appendix B |
| `GetRenderStats()`, `IsPauseSupported()`, `Pause`/`Resume`/`Stop` overrides | `hydra wrapper` (0.3.0) — `completed_samples()` is the value they return |
| `HdRenderSettingDescriptorList`, reading `convergedSamplesPerPixel` / `threadLimit` / `randomNumberSeed` from the host | `hydra wrapper` (0.3.0) — the fields they write already exist after this task |
| `WorkSetConcurrencyLimit` / `tbb::global_control` policy | nobody — `WorkParallelForN` honours `PXR_WORK_THREAD_LIMIT` on its own (verified), and the tracer must not set global policy |
| `LockFramebuffer()` / tear-free presentation | not scheduled — hdEmbree deliberately does not lock either (§10.1); see "Design notes" |
| Adaptive convergence (`convergedVariance`) | not scheduled |
| Any change to `camera.h`, `render_buffer.h`, `mat4.h` | nothing — step 6 proves they are untouched |

The tracer stays **USD-free**: no `pxr/` include appears under `tracer/` or `viewer/`. Step 9 is
the only place USD appears and it is a scratchpad program — per the standing rule, **no test code
lands in the repo**.

---

## Pre-verified facts

These were measured, not assumed. Every program named below exists and was run; the numbers are
its output. Scratchpad:

```
S=/tmp/claude-1000/-home-nick-git-weekend-raytracer-docs/879db9f8-9443-472d-87da-e070fbab7170/scratchpad
```

| Claim | Measured |
|---|---|
| **The refactor is bit-exact.** Scenes 0–3 at 50 spp | `cmp` **byte-identical** to the pre-refactor `tracer_cli`, all four (`e2e.cpp` vs `gold_*.ppm`) |
| **Tile size cannot change the image** | scene 2 byte-identical at `tile_size` = 1, 8, 16, 32, 64, 400 |
| **Scheduler cannot change the image** | serial == threaded == gold, byte for byte; and unchanged under `PXR_WORK_THREAD_LIMIT=2` |
| **Stop latency, 8-pixel tiles** | 400×225: **1.4 / 1.7 / 1.9 ms**; 800×450: **1.1 / 1.9 / 2.2 ms** — measured from the flag store to `render()` returning, during pass 0 |
| **Stop latency, 64-pixel tiles** | 800×450: **47 / 60 / 66 ms** — the whole argument for keeping hdEmbree's `tileSize = 8` |
| **Stop latency through a real `HdRenderThread`** | `StopRender()` → callback returned in **0.05–0.16 ms** (`test_control_hd.cpp`, gate 4) |
| **A pass is ~133 ms** at 400×225 scene 0 (6.65 s / 50) | so *pause* latency is up to one pass — coarse by design, see "Two granularities" |
| **Pause freezes and resumes** | through `HdRenderThread`: samples `136 → 136` while paused, `→ 366` after `ResumeRender()` |
| **A cancelled render leaves a usable partial image** | 800×450 stopped mid-pass-0: 183 040/360 000 pixels sampled, mean channel 0.211, all buffers unmapped |
| **`renderer.h` compiles with no TBB on the include path at all** | `g++ -I$S -Itracer` (no `-Ibuild/_deps/tbb-src/include`) compiles and runs (`no_tbb.cpp`) |
| **USD and the tracer carry different TBB runtimes** | `$USD_ROOT/lib/libtbb.so.2` vs `build/gnu_13.3_cxx11_64_release/libtbb.so.12` |
| **The tiled sub-window is exact** | data window `(37,23)-(262,171)`, not tile-aligned: **33 674/33 674 inside sampled, 0 outside**, top row 0.863 vs bottom 0.308 — at tile 8, 16 and 64 |
| **`WorkParallelForN` honours the host limit for free** | `PXR_WORK_THREAD_LIMIT=2` → `WorkGetConcurrencyLimit()` 12 → 2, image unchanged |
| **`HdRenderThread::IsStopRequested()` latches** | peeking it *outside* the render callback makes the **next** render return 0 samples. See "The latch" |
| No performance regression | interleaved warm runs, scene 0: old **0.0812 / 0.0825 / 0.0823** vs new **0.0812 / 0.0820 / 0.0821** ms/px |

Two of those deserve emphasis before you start.

**Byte-identical is the gate, and it is achievable.** Moving the sample loop outside the pixel
loop does not perturb a single pixel: `sample_seed(pixel_index, sample_index, frame_seed)` keys
the rng on the pixel and its own sample count, so pass *k* re-derives exactly the seed the old
inner loop's iteration *k* used, and each pixel's accumulator still receives its samples in
increasing order — the same float additions in the same sequence. **Any difference at all in step
6 is a bug you introduced, not an expected consequence of the refactor.** That is a sharper gate
than [[render-target]] could offer, and it is worth the strictness: this task moves every loop in
the renderer.

**The first run of anything on this machine is ~10% faster than the fifth.** The very first
`tracer_cli 0` after a cold start measured 0.0664 ms/px and the same binary measured 0.0823 ms/px
four runs later. Comparing a cold "old" against a warm "new" will invent a 10–15% regression that
is not there. Interleave old/new/old/new and compare the *warm* rows, which is how the table's
last line was produced.

Build lines that work on this machine:

```bash
# tracer-only scratchpad program (with the tbb scheduler)
g++ -std=c++17 -O3 -DNDEBUG -I$S -Itracer -Ibuild/_deps/tbb-src/include \
    -o $S/e2e $S/e2e.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release

# the no-TBB gate: note the absence of any tbb include path
g++ -std=c++17 -O2 -I$S -Itracer -o $S/no_tbb $S/no_tbb.cpp

# USD-linked scratchpad program. -ltbb here is USD's TBB 2020, NOT the vendored oneTBB;
# link it from $USD_ROOT/lib or hd's symbols will not resolve.
source env.sh
g++ -std=c++17 -O2 -Wno-deprecated -I$S -Itracer \
    -I$USD_ROOT/include -I/usr/include/python3.12 \
    -o $S/test_control $S/test_control_hd.cpp \
    -L$USD_ROOT/lib -lusd_hd -lusd_work -lusd_tf -lusd_gf -lusd_vt -lusd_sdf -ltbb -lpython3.12
```

`std::thread` needs no `-lpthread` here (glibc 2.39 folded it into libc), so `viewer` needs no
CMake change for it.

---

## The design in one page

```
tracer/render_control.h  NEW      render_control (stop/pause polling, USD-free), tile_scheduler
                                  (the injected parallel dispatch), serial_schedule, tile_grid.

tracer/schedulers.h      NEW      tbb_schedule. The ONLY tracer header that includes <tbb/…>.
                                  The cli and viewer include it; hydra/ never will.

tracer/renderer.h        REWRITE  render(cam, world, aovs, control) owns the progressive sample
                                  loop and returns render_stats. render_tiles(...) is the public
                                  per-tile entry point a host scheduler drives. No TBB include.

tracer/main.cpp          EDIT     samples_to_convergence = 50; installs tbb_schedule unless the
                                  serial flag is set; reads stats.ms.
viewer/main.cpp          REWRITE  render on a std::thread, resolve+blit+present on the main
                                  thread, ESC/space/R drive stop/pause/restart.

tracer/camera.h          UNTOUCHED
tracer/render_buffer.h   UNTOUCHED
```

### Who owns what, after this task

| Decision | Owner |
|---|---|
| How many samples before convergence | caller (`samples_to_convergence`) — Hydra's `convergedSamplesPerPixel` |
| When to stop / pause | caller, through `render_control` — Hydra's `HdRenderThread` |
| Which threads run tiles, and how many | caller, through `tile_scheduler` — Hydra's `WorkParallelForN` |
| Tile size | renderer (`tile_size`, default 8) — overridable, and it is a latency knob, not a correctness one |
| Sample loop, tile decomposition, cancellation points, convergence flags | **renderer** |
| Clearing buffers and marking them unconverged before a restart | caller — Hydra's render pass |

That last row is the one correction this task makes to [[render-target]]'s closing note, which
predicted the render *pass* would own `set_converged(true)`. It does not: hdEmbree's renderer sets
it, at the end of `Render()`, on every bound buffer (`hdEmbree/renderer.cpp:608-615`), and the
pass owns only the *un*-setting (`renderPass.cpp:222` calls `MarkAovBuffersUnconverged()` right
before `StartRender()`). Convergence is a property of the render that just finished, so it belongs
to whoever finished it. `mark_unconverged(aovs)` is the free function the caller uses for the
other direction.

### Two granularities: stop is per tile, pause is per pass

Stop must be fast because a scene edit blocks on it; it is polled **before each pass and before
each tile**, which measures 1–2 ms at `tile_size = 8`. Pause is a user toggling a button; it is
polled **once per pass** in a 10 ms sleep loop, so it takes effect within one pass (~133 ms at
400×225). hdEmbree makes exactly this split, and the reason to copy it is that a pause point
inside the tile loop would mean parking N worker threads in sleep loops inside a
`WorkParallelForN` — holding the host's task arena hostage while "paused".

The pause loop breaks on stop, so a stop while paused is still fast.

### Why the scheduler is injected rather than abstracted away

The alternative — keep `tbb::parallel_for` in the renderer and let the delegate live with it —
fails §17.6 outright. The other alternative — expose only `render_tiles()` and let every caller
write its own sample loop — duplicates the cancellation points, the pass-0 single-sample rule and
the convergence bookkeeping into both the viewer and the delegate, which is precisely the logic
that is easy to get subtly wrong and impossible to test twice.

A `std::function` per pass costs one indirect call per ~1450 tiles (400×225 at tile 8). It does
not appear in the measurements.

### The write index, restated

Unchanged from [[render-target]], and worth re-reading before touching the loops: tiles are cut
out of `camera::data_window`, which is **y-down**; `render_buffer` row 0 is the **bottom** image
line. The renderer iterates y-down window rows and flips only the write index
(`by = height - 1 - y`). hdEmbree instead flips the loop bounds and derives NDC from the flipped
`minY`; the two are algebraically identical, and ours leaves `camera.h` alone. Step 8 is the gate
that the flip survived tiling.

---

# Step 0 — Capture golden images before you touch anything

The gate for this task is byte equality, so the goldens must come from the current build.

```bash
cd ~/git/weekend-raytracer
cmake --build build --config Release
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release
S=/tmp/claude-1000/.../scratchpad          # your scratchpad

for i in 0 1 2 3; do ./build/tracer/Release/tracer_cli $i > $S/gold_$i.ppm; done
./build/tracer/Release/tracer_cli 2 1 > $S/gold_serial_2.ppm     # serial path
md5sum $S/gold_*.ppm | tee $S/gold.md5
```

Measured here, and they must match yours before you start (same machine, same build):

```
3292e039125ee04d7f4728ad9d89886f  gold_0.ppm
81978695472eb949e987e46fefe3e694  gold_1.ppm
418151b864772683d18aef594a1651b7  gold_2.ppm
57e57b71e5501b5f278b60a73793b64c  gold_3.ppm
```

Also take a warm timing baseline — run it **four times** and keep the last three:

```bash
for i in 1 2 3 4; do ./build/tracer/Release/tracer_cli 0 >/dev/null; done
# here: 0.0664 (cold), then 0.0812 / 0.0825 / 0.0823 ms/px
```

---

# Step 1 — Write `tracer/render_control.h`

Three things the renderer needs from its caller and one thing it computes for itself. No TBB, no
USD, no renderer state — this header is the whole contract between the tracer and whoever drives
it.

```cpp
#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>

#include "mat4.h"

// USD-free stand-in for HdRenderThread's cancellation surface. The delegate
// subclasses this over an HdRenderThread*; the viewer over two atomics; the cli
// passes nullptr. Polled once per sample pass and once per tile - never per
// pixel - so the virtual call is free.
struct render_control
{
  virtual ~render_control() = default;
  virtual bool is_stop_requested() const { return false; }
  virtual bool is_pause_requested() const { return false; }
};

// A caller-supplied parallel dispatch. `work(begin, end)` is called for
// sub-ranges that together cover [0, n). WorkParallelForN in the delegate,
// tbb::parallel_for in the cli/viewer, this one in a unit test.
using tile_scheduler = std::function<void(size_t n, const std::function<void(size_t, size_t)> &work)>;

inline void serial_schedule(size_t n, const std::function<void(size_t, size_t)> &work) { work(0, n); }

// The data window cut into square tiles. Tile coordinates are y-down window
// coordinates, exactly like camera::data_window; the renderer flips only when
// it writes.
struct tile_grid
{
  rect2i window;
  int tile_size = 8;
  int tiles_x = 0, tiles_y = 0;

  tile_grid() = default;
  tile_grid(const rect2i &w, int size) : window(w), tile_size(size < 1 ? 1 : size)
  {
    tiles_x = (window.width() + tile_size - 1) / tile_size;
    tiles_y = (window.height() + tile_size - 1) / tile_size;
  }

  size_t count() const { return size_t(tiles_x < 0 ? 0 : tiles_x) * size_t(tiles_y < 0 ? 0 : tiles_y); }

  // Tile `i` as an INCLUSIVE rect, same convention as rect2i everywhere else.
  // The last row/column of tiles is clamped to the window.
  rect2i tile(size_t i) const
  {
    const int ty = int(i) / tiles_x;
    const int tx = int(i) - ty * tiles_x;
    const int x0 = window.min_x + tx * tile_size;
    const int y0 = window.min_y + ty * tile_size;
    return {x0, y0, std::min(x0 + tile_size - 1, window.max_x), std::min(y0 + tile_size - 1, window.max_y)};
  }
};
```

Three details that are easy to get wrong:

- **`tile()` returns an inclusive rect.** `rect2i` is inclusive everywhere else in the tracer
  (`from_size(w,h)` gives `max_x = w-1`), and a half-open tile rect in an inclusive-rect codebase
  is a one-off waiting to happen. The loops in step 3 are therefore `y <= tile.max_y`.
- **Tiles are in window coordinates, not buffer coordinates.** `x0 = window.min_x + tx*tile_size`,
  not `tx*tile_size`. That is what makes step 8's inset data window land in the right place.
- **`render_control`'s defaults return false**, so a subclass that only cares about stop does not
  have to implement pause.

---

# Step 2 — Write `tracer/schedulers.h`

```cpp
#pragma once

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include "render_control.h"

// The cli/viewer scheduler. The hydra delegate does NOT use this - it binds
// WorkParallelForN so USD's arena and thread limit are honoured (spec 17.6).
inline void tbb_schedule(size_t n, const std::function<void(size_t, size_t)> &work)
{
  tbb::parallel_for(tbb::blocked_range<size_t>(0, n),
                    [&](const tbb::blocked_range<size_t> &r) { work(r.begin(), r.end()); });
}
```

That is the entire file, and it is the **only** header under `tracer/` allowed to include
`<tbb/…>` from here on. Step 6's second gate compiles the renderer with no TBB include path at
all to keep it that way.

Grain size is deliberately left at TBB's default: tiles are already the work unit, and at
`tile_size = 8` a 400×225 window is 1450 of them across 12 hardware threads.

---

# Step 3 — Rewrite `tracer/renderer.h`

The whole file, with the changes called out afterwards:

```cpp
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "camera.h"
#include "hittable.h"
#include "material.h"
#include "render_buffer.h"
#include "render_control.h"
#include "rng.h"
#include "tracer.h"
#include "vec3.h"

using namespace std::chrono;

struct render_stats
{
  double ms = 0;              // wall clock spent in render()
  int completed_samples = 0;  // full passes finished
  bool stopped = false;       // true if a stop request ended the render early
};

struct renderer
{
  int max_bounces = 20;
  int samples_to_convergence = 100;  // HdRenderSettingsTokens->convergedSamplesPerPixel
  int tile_size = 8;                 // hdEmbree's default
  uint64_t frame_seed = 0;           // randomNumberSeed

  // How tiles reach threads. Serial by default so the tracer core needs no
  // scheduler at all; the cli/viewer install tbb_schedule, the delegate
  // WorkParallelForN.
  tile_scheduler schedule = serial_schedule;

  // Progressive, interruptible render of the camera's data window into `aovs`.
  // One pass == one sample per pixel over every tile.
  render_stats render(const camera &cam, const hittable &world, const aov_bindings &aovs,
                      const render_control *control = nullptr)
  {
    const auto start = high_resolution_clock::now();
    render_stats stats;

    if (!validate(cam, aovs))
    {
      return stats;
    }

    _completed_samples.store(0);

    for (const aov_binding &b : aovs)
    {
      b.buffer->map();
    }

    const tile_grid grid(cam.data_window, tile_size);
    const int passes = any_multisample(aovs) ? std::max(samples_to_convergence, 1) : 1;

    for (int pass = 0; pass < passes; pass++)
    {
      // pause point
      while (control != nullptr && control->is_pause_requested())
      {
        if (control->is_stop_requested()) break;
        std::this_thread::sleep_for(milliseconds(10));
      }

      // cancellation point, before the pass
      if (control != nullptr && control->is_stop_requested())
      {
        stats.stopped = true;
        break;
      }

      schedule(grid.count(), [&](size_t begin, size_t end) {
        render_tiles(cam, world, aovs, grid, pass, begin, end, control);
      });

      // after pass 0 every single-sampled aov holds its one and only value
      if (pass == 0)
      {
        for (const aov_binding &b : aovs)
        {
          if (!b.buffer->is_multisampled()) b.buffer->set_converged(true);
        }
      }

      // cancellation point, after the pass. Checked BEFORE publishing the
      // sample count: a pass that was cancelled mid-flight did not give every
      // pixel a sample, so it must not be reported as completed.
      if (control != nullptr && control->is_stop_requested())
      {
        stats.stopped = true;
        break;
      }

      _completed_samples.store(pass + 1);
    }

    for (const aov_binding &b : aovs)
    {
      b.buffer->unmap();
      b.buffer->set_converged(true);
    }

    using ms_d = duration<double, std::milli>;
    stats.ms = ms_d(high_resolution_clock::now() - start).count();
    stats.completed_samples = _completed_samples.load();
    return stats;
  }

  // One sample per pixel over tiles [begin, end) of `grid`. This is the entry
  // point a host scheduler drives; `control` may be null.
  void render_tiles(const camera &cam, const hittable &world, const aov_bindings &aovs,
                    const tile_grid &grid, int sample_pass, size_t begin, size_t end,
                    const render_control *control = nullptr) const
  {
    const int height = aovs[0].buffer->height();
    const int width = aovs[0].buffer->width();

    // the sample counter: the first multisampled aov, which is the color pass
    const render_buffer *counter = nullptr;
    for (const aov_binding &b : aovs)
    {
      if (b.buffer->is_multisampled())
      {
        counter = b.buffer;
        break;
      }
    }

    for (size_t t = begin; t < end; t++)
    {
      // cancellation point, so pass 0 is interruptible too
      if (control != nullptr && control->is_stop_requested())
      {
        break;
      }

      const rect2i tile = grid.tile(t);
      for (int y = tile.min_y; y <= tile.max_y; y++)
      {
        for (int x = tile.min_x; x <= tile.max_x; x++)
        {
          const int by = height - 1 - y;
          const int i = y * width + x;
          const int sample_base = counter != nullptr ? int(counter->samples_at(x, by)) : sample_pass;
          const bool first_ever_sample = sample_base == 0;

          rng generator = rng(sample_seed(i, sample_base, frame_seed));
          ray r = cam.get_ray(generator, x, y);
          hit_info hit_info;
          bool did_hit = false;
          color pixel_color = raycast(generator, r, max_bounces, world, &hit_info, &did_hit);

          for (const aov_binding &aov : aovs)
          {
            render_buffer &buffer = *aov.buffer;

            if (buffer.is_converged()) continue;
            if (!first_ever_sample && !buffer.is_multisampled()) continue;

            switch (aov.name)
            {
              // ... unchanged from the current file: color / depth / camera_depth
              //     / normal / n_eye, all writing at (x, by)
            }
          }
        }
      }
    }
  }

  int completed_samples() const { return _completed_samples.load(); }

private:
  std::atomic<int> _completed_samples{0};

  static bool validate(const camera &cam, const aov_bindings &aovs)
  {
    if (aovs.empty() || aovs[0].buffer == nullptr)
    {
      return false;
    }

    const render_buffer *buffer = aovs[0].buffer;
    const int w = buffer->width(), h = buffer->height();
    for (const aov_binding &b : aovs)
    {
      if (b.buffer == nullptr || b.buffer->width() != w || b.buffer->height() != h)
      {
        return false;
      }
    }

    const rect2i &window = cam.data_window;
    if (window.is_empty() || window.min_x < 0 || window.min_y < 0 || window.max_x >= w || window.max_y >= h)
    {
      return false;
    }

    return true;
  }

  static bool any_multisample(const aov_bindings &aovs) { /* unchanged */ }

  color raycast(rng &generator, const ray &r, int depth, const hittable &world,
                hit_info *primary, bool *did_hit) const  { /* unchanged, now const */ }
};

// Mark every bound buffer unconverged; the caller does this before restarting a
// render (hdEmbree: HdEmbreeRenderer::MarkAovBuffersUnconverged).
inline void mark_unconverged(const aov_bindings &aovs)
{
  for (const aov_binding &b : aovs)
  {
    if (b.buffer != nullptr) b.buffer->set_converged(false);
  }
}
```

What actually changed, and why each one matters:

1. **`#include <tbb/...>` is gone**, and so is `render_region`. The parallel dispatch is
   `schedule(grid.count(), ...)`.
2. **The `samples` parameter is gone**; `samples_to_convergence` is a member. The inner
   `for (sample …)` loop over a pixel is gone — a pass is one sample per pixel, which is what
   makes the render interruptible at a useful granularity and what makes `completed_samples()`
   mean something.
3. **`sample_base` is read per pass instead of per batch**, and `first_ever_sample` becomes
   `sample_base == 0`. This is the line that keeps the output bit-exact: pass *k* reads a sample
   count of *k* and therefore builds `sample_seed(i, k, frame_seed)` — the same seed the old inner
   loop's iteration *k* built.
4. **Two cancellation points per pass and one per tile.** The per-tile one is why the measured
   stop latency is 1–2 ms instead of a whole pass.
5. **The pause loop is per pass**, sleeping 10 ms, and breaks on stop.
6. **Buffers are mapped for the duration of the render and unmapped at the end**, matching
   hdEmbree's `_PreRenderSetup` / end-of-`Render` (§10.2: "at the end, unmap every buffer and
   `SetConverged(true)`"). It costs two atomic increments and it means `is_mapped()` is a true
   statement about who is writing.
7. **`validate()` no longer dereferences `aovs[0]` before checking `aovs.empty()`** — that
   ordering was undefined behaviour on the empty-bindings input §9 says is legal. It also rejects
   an empty data window now.
8. **`_completed_samples` is published only after a pass that was not cancelled.** This is a
   deliberate deviation from hdEmbree, which stores `i+1` before its post-pass cancellation check
   (`renderer.cpp:594-605`). Measured consequence of the hdEmbree ordering, at 800×450 stopped
   6 ms into pass 0: `completed_samples = 1` while only 183 040 of 360 000 pixels had been
   sampled. Reporting one complete sample there is a lie the host would put in the viewport HUD.
9. **`render_tiles` and `raycast` are `const`**; `render` is not, because it publishes the sample
   count. Adding the `std::atomic<int>` member makes `renderer` non-copyable — fine, everything
   holds it by value or reference, and hdEmbree's renderer is non-copyable too.

---

# Step 4 — Update `tracer/main.cpp`

Four lines. The `multithread` argument now selects the scheduler instead of a branch inside the
renderer:

```cpp
  renderer r;
  r.max_bounces = 10;
  r.samples_to_convergence = 50;
  if (multithread) r.schedule = tbb_schedule;   // default is serial_schedule
```

```cpp
  std::clog << "Rendering scene " << " " << i_scene << "..." << std::flush;
  render_stats stats = r.render(cam, world, aovs);
  color_buf.resolve();
```

```cpp
  auto per_pixel = stats.ms / (double(height) * width);
  std::clog << "\rRendered scene " << i_scene << " in " << stats.ms / double(1000) << "s ("
            << per_pixel << "ms/px)                       \n" << std::flush;
```

and add `#include "schedulers.h"`. The cli passes no `render_control`: a batch render has nothing
to cancel, and `nullptr` skips the polls entirely.

---

# Step 5 — Rewrite `viewer/main.cpp` as a real render thread

This is the step that makes the feature visible, and it is also the only proof that the design
works from the *outside*. The viewer becomes a miniature of the delegate: a control object, a
render thread, and a main loop that resolves and presents whatever exists so far.

The adaptive `budget_ms_per_update` / `ms_per_sample` machinery goes away entirely — it existed
only to keep `render()` calls short enough that the window stayed responsive, which is exactly the
problem this task solves.

```cpp
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include "camera.h"
#include "example_scenes.h"
#include "hittable_list.h"
#include "render_buffer.h"
#include "render_control.h"
#include "renderer.h"
#include "schedulers.h"

// The viewer's half of HdRenderThread: two flags the ui thread sets and the
// render thread polls. The hydra delegate swaps this for HdRenderThread.
struct viewer_control : render_control
{
  std::atomic<bool> stop{false};
  std::atomic<bool> pause{false};

  bool is_stop_requested() const override { return stop.load(); }
  bool is_pause_requested() const override { return pause.load(); }
};

// blit_buffer_to_texture() is UNCHANGED - it already flips on read.

int main()
{
  // ... SDL_Init / SDL_CreateWindowAndRenderer unchanged ...

  hittable_list world;
  camera_desc desc;
  load_scene(1, world, desc);

  const int render_width = 400, render_height = 225;
  camera cam = desc.build(render_width, render_height);

  renderer r;
  r.max_bounces = 10;
  r.samples_to_convergence = 1000;
  r.schedule = tbb_schedule;

  render_buffer buffer;
  aov_bindings aovs = {allocate_aov(buffer, aov::color, render_width, render_height)};

  // the render resolution is fixed, so the texture is created once
  SDL_Texture *texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_XRGB8888,
                                           SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
  SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
  SDL_SetRenderLogicalPresentation(sdl_renderer, render_width, render_height,
                                   SDL_LOGICAL_PRESENTATION_LETTERBOX);

  // the render thread. start_render() is the viewer's HdRenderPass::_Execute:
  // stop what is running, clear, un-converge, start again.
  viewer_control control;
  render_stats stats;
  std::thread render_thread;

  auto stop_render = [&]() {
    control.stop.store(true);
    if (render_thread.joinable())
    {
      render_thread.join();
    }
  };

  auto start_render = [&]() {
    stop_render();
    buffer.clear(4, default_aov_descriptor(aov::color).clear_value);
    mark_unconverged(aovs);
    control.stop.store(false);
    control.pause.store(false);
    render_thread = std::thread([&]() { stats = r.render(cam, world, aovs, &control); });
  };

  std::clog << "Opening window...  [space] pause  [r] restart  [esc] quit\n";
  start_render();

  int shown_samples = -1;
  bool running = true;
  while (running)
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_EVENT_QUIT)
      {
        running = false;
      }
      if (event.type == SDL_EVENT_KEY_DOWN)
      {
        switch (event.key.key)
        {
          case SDLK_ESCAPE: running = false; break;
          case SDLK_SPACE: control.pause.store(!control.pause.load()); break;
          case SDLK_R: start_render(); break;
        }
      }
    }

    // Resolve and present whatever the render thread has produced so far. This
    // races with the tile threads by design - see "Design notes".
    buffer.resolve();
    if (!blit_buffer_to_texture(texture, buffer))
    {
      std::clog << "Failed to blit buffer to texture: could not lock texture." << std::endl;
    }

    SDL_RenderClear(sdl_renderer);
    SDL_RenderTexture(sdl_renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(sdl_renderer);

    const int samples = r.completed_samples();
    if (samples != shown_samples)
    {
      shown_samples = samples;
      std::string title = "viewer - " + std::to_string(samples) + "/" +
                          std::to_string(r.samples_to_convergence) + " samples";
      if (control.pause.load()) title += " (paused)";
      if (buffer.is_converged()) title += " (converged)";
      SDL_SetWindowTitle(window, title.c_str());
    }

    SDL_Delay(16);
  }

  std::clog << "Closing window" << std::endl;
  stop_render();
  std::clog << "Stopped after " << stats.completed_samples << " samples in " << stats.ms / 1000.0
            << "s" << (stats.stopped ? " (interrupted)" : "") << std::endl;

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
```

Notes on the ones that bite:

- **Rename the local `SDL_Renderer *renderer` to `sdl_renderer`.** The current file has
  `struct renderer r;` precisely because the name is already taken; now that `renderer` also
  appears in a lambda capture and a thread body, keeping the collision is asking for trouble.
- **`stop_render()` before every `start_render()`, and once more after the loop.** The render
  thread writes into `buffer`; destroying it (or the `aovs` vector, or `world`) while a tile is
  mid-write is a use-after-free that will not reproduce on demand.
- **`start_render()` clears *and* un-converges.** `render()` sets every buffer converged on the
  way out, and a converged buffer is skipped by the pixel loop — a restart that forgets
  `mark_unconverged` renders a black frame forever, and it is the exact mistake the render pass
  will be able to make in 0.3.0.
- **The window title is the progress readout.** [[render-target]]'s retro noted the viewer prints
  nothing per frame and that `ms_per_sample` was computed and never shown; `completed_samples()`
  is now the number worth showing, and it costs one atomic load per frame.
- **No CMake change is needed.** `std::thread` links without `-lpthread` under glibc 2.39, and
  `viewer` already links `tracer`, which carries `TBB::tbb`.

---

# Step 6 — GATE 1: the images did not change, and the renderer has no TBB in it

Two separate assertions, both cheap, both fatal if they fail.

### 1a — byte equality

```bash
cmake --build build --config Release
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release
for i in 0 1 2 3; do
  ./build/tracer/Release/tracer_cli $i > $S/new_$i.ppm
  cmp $S/gold_$i.ppm $S/new_$i.ppm && echo "scene $i IDENTICAL"
done
./build/tracer/Release/tracer_cli 2 1 > $S/new_serial_2.ppm
cmp $S/gold_serial_2.ppm $S/new_serial_2.ppm && echo "serial IDENTICAL"
```

Expected — and measured — output is four `IDENTICAL` lines plus the serial one. Not "within
1/255": **identical**. If a pixel moves, work through this list before anything else:

| Symptom | Cause |
|---|---|
| Every pixel differs slightly | `sample_base` is being taken from `sample_pass` instead of `counter->samples_at(x, by)`, or `first_ever_sample` is wrong |
| A regular grid of blocks differs | tile rect treated as half-open (`y < tile.max_y`), or tiles not offset by `window.min_x/min_y` |
| Two runs of the *new* binary differ from each other | `samples_at(x, y)` instead of `samples_at(x, by)` — the y-flip on the counter read. This is the defect that cost [[render-target]] a debugging session |
| The image is upside down | the flip moved into `camera::get_ray`; it belongs on the write index only |
| Only the last row/column differs | tile clamp uses `window.width()` instead of `window.max_x` |

To be thorough about the claim that tiling cannot change the image, temporarily expose
`tile_size` as a cli argument (or hard-code it) and re-run scene 2:

```
tile_size=1   identical to gold
tile_size=8   identical to gold
tile_size=16  identical to gold
tile_size=32  identical to gold
tile_size=64  identical to gold
tile_size=400 identical to gold
```

All six were measured. If any of them differs, the rng is picking up tile identity from somewhere
— which is what hdEmbree does (`TfHash::Combine(seed, tileStart, sampleNum)`) and what our
per-pixel seeding deliberately does not.

### 1b — the renderer compiles with no TBB anywhere

```cpp
// $S/no_tbb.cpp — GATE: no tbb on the include path at all
#include "renderer.h"
#include "example_scenes.h"
#include "hittable_list.h"

int main()
{
  hittable_list world; camera_desc desc;
  load_scene(2, world, desc);
  camera cam = desc.build(64, 36);

  renderer r;                       // schedule defaults to serial_schedule
  r.samples_to_convergence = 4;
  render_buffer buf;
  aov_bindings aovs = {allocate_aov(buf, aov::color, 64, 36)};
  render_stats s = r.render(cam, world, aovs, nullptr);
  buf.resolve();
  float rgba[4]; buf.read(32, 18, 4, rgba);
  std::cout << "serial render ok: samples=" << s.completed_samples
            << " stopped=" << s.stopped
            << " center=(" << rgba[0] << "," << rgba[1] << "," << rgba[2] << ")\n";
  return 0;
}
```

```bash
g++ -std=c++17 -O2 -I$S -Itracer -o $S/no_tbb $S/no_tbb.cpp && $S/no_tbb
# serial render ok: samples=4 stopped=0 center=(0.063387,0.156064,0.5)
```

Note the missing `-Ibuild/_deps/tbb-src/include`. If this fails to compile, something in the
renderer's include graph still reaches TBB and the delegate will end up with two runtimes in one
process. Back it up with:

```bash
grep -rn "tbb" tracer/*.h | grep -v "^tracer/schedulers.h"    # must be empty
grep -rn "pxr/" tracer/ viewer/                               # must be empty
```

---

# Step 7 — GATE 2: it actually stops, it actually pauses, and what is left is usable

```cpp
// $S/cancel.cpp — stop latency, pause/resume, and the partial image
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

#include "camera.h"
#include "example_scenes.h"
#include "hittable_list.h"
#include "renderer.h"
#include "schedulers.h"

using namespace std::chrono;

struct atomic_control : render_control
{
  std::atomic<bool> stop{false}, pause{false};
  bool is_stop_requested() const override { return stop.load(); }
  bool is_pause_requested() const override { return pause.load(); }
};

int main(int argc, char *argv[])
{
  const int width = argc > 1 ? atoi(argv[1]) : 400;
  const int height = argc > 2 ? atoi(argv[2]) : 225;
  const int tile = argc > 3 ? atoi(argv[3]) : 8;

  hittable_list world; camera_desc desc;
  load_scene(0, world, desc);
  camera cam = desc.build(width, height);

  renderer r; r.max_bounces = 10; r.samples_to_convergence = 1000; r.tile_size = tile;
  r.schedule = tbb_schedule;

  render_buffer color_buf, depth_buf;
  aov_bindings aovs = {allocate_aov(color_buf, aov::color, width, height),
                       allocate_aov(depth_buf, aov::depth, width, height)};

  for (int delay_ms : {5, 20, 60}) {
    color_buf.clear(4, default_aov_descriptor(aov::color).clear_value);
    mark_unconverged(aovs);

    atomic_control ctl;
    render_stats stats;
    std::thread th([&] { stats = r.render(cam, world, aovs, &ctl); });
    std::this_thread::sleep_for(milliseconds(delay_ms));
    auto req = steady_clock::now();
    ctl.stop.store(true);
    th.join();
    std::cout << "  stop after " << delay_ms << "ms -> returned in "
              << duration<double, std::milli>(steady_clock::now() - req).count()
              << "ms, stopped=" << stats.stopped
              << ", completed_samples=" << stats.completed_samples << "\n";
  }
  // ... pause/resume block and partial-image block: see the measured output below
}
```

```bash
g++ -std=c++17 -O3 -DNDEBUG -I$S -Itracer -Ibuild/_deps/tbb-src/include \
    -o $S/cancel $S/cancel.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb
$S/cancel 400 225 8 ; $S/cancel 800 450 8 ; $S/cancel 800 450 64
```

Measured:

```
400x225 tile=8
  stop after 5.6ms  -> returned in 1.39ms, stopped=1, completed_samples=0
  stop after 20.1ms -> returned in 1.70ms, stopped=1, completed_samples=0
  stop after 60.7ms -> returned in 1.87ms, stopped=1, completed_samples=0
800x450 tile=8
  stop after 6.1ms  -> returned in 1.87ms, stopped=1, completed_samples=0
  stop after 21.0ms -> returned in 2.20ms, stopped=1, completed_samples=0
  stop after 60.1ms -> returned in 1.13ms, stopped=1, completed_samples=0
  paused: samples 0 -> 1 (frozen), after resume 2 (advancing)
  partial render: 183040/360000 pixels sampled, mean channel 0.211, samples=0, mapped=0
800x450 tile=64
  stop after 6.1ms  -> returned in 60.0ms, stopped=1, completed_samples=0
  stop after 21.0ms -> returned in 65.5ms, stopped=1, completed_samples=0
  stop after 60.4ms -> returned in 46.9ms, stopped=1, completed_samples=0
```

What to read out of it:

- **Pass criterion: under 5 ms at `tile_size = 8`.** All six measurements are 1.1–2.2 ms. If you
  see a number near the pass time (~133 ms at 400×225, ~530 ms at 800×450), the per-tile
  cancellation point is missing and only the per-pass one is firing.
- **The `tile=64` rows are the control**, not a failure: they show the same code with a coarser
  cancellation granularity, which is what justifies keeping hdEmbree's default of 8.
- **`mapped=0` after the render returns** — the unmap at the end of `render()` ran even on the
  stopped path.
- **`completed_samples=0` when pass 0 was cut short**, which is the deviation from hdEmbree in
  step 3 note 8. With hdEmbree's ordering this reads `1` while 49% of the pixels are still black.
- **Pause is coarse on purpose.** `samples 0 -> 1 (frozen)` means the in-flight pass finished
  before the pause took hold. Expect up to one pass of lag; anything more means the pause loop is
  in the wrong place.

---

# Step 8 — GATE 3: the tiled sub-window is still exact, and still right side up

Tiling is the one thing in this task that can silently break the data window, because tile origins
are now a second place where `window.min_x/min_y` has to be added.

```cpp
// $S/subwindow.cpp — an inset, deliberately NOT tile-aligned data window
  camera cam = desc.build(400, 225);
  cam.data_window = {37, 23, 262, 171};   // y-down, inclusive

  renderer r; r.max_bounces = 10; r.samples_to_convergence = 4; r.tile_size = tile;
  r.schedule = tbb_schedule;
  ...
  // count pixels whose samples_at() > 0, inside vs outside the window
  // (remember: buffer row y corresponds to window row H-1-y)
  // and compare the mean of the window's top row against its bottom row
```

Measured, at three tile sizes:

```
tile=8  grid=29x19 (551 tiles) samples=4
  inside sampled 33674/33674, outside sampled 0
  window top row mean 0.863, bottom row mean 0.308  (top must be brighter: sky)
tile=16 grid=15x10 (150 tiles)   -> same counts, same means
tile=64 grid=4x3 (12 tiles)      -> same counts, same means
```

`33674 = 226 × 149`, the exact area of the window. `outside sampled 0` is the assertion that
matters: a tile grid anchored at 0 instead of `window.min_x` writes outside the window and would
show up here as a non-zero count. The 0.863 / 0.308 pair is the same orientation check
[[render-target]] used, re-run under tiling.

---

# Step 9 — GATE 4: assert against USD itself

This is the gate that pays for the whole design, and it found something not documented anywhere.

```cpp
// $S/test_control_hd.cpp — the delegate-side shims, against real USD types
#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/base/work/loops.h"
#include "pxr/base/work/threadLimits.h"

#include "render_control.h"
#include "renderer.h"
#include "example_scenes.h"
#include "hittable_list.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdWeekendRenderControl final : public render_control
{
public:
  explicit HdWeekendRenderControl(HdRenderThread *thread) : _thread(thread) {}
  bool is_stop_requested() const override { return _thread->IsStopRequested(); }
  bool is_pause_requested() const override { return _thread->IsPauseRequested(); }
private:
  HdRenderThread *_thread;  // IsStopRequested/IsPauseRequested are non-const
};

inline void hdWeekendWorkSchedule(size_t n, const std::function<void(size_t, size_t)> &work)
{
  WorkParallelForN(n, [&work](size_t begin, size_t end) { work(begin, end); });
}

// A: StartThread/StartRender, wait for the callback to finish, StopThread
// B: StartRender, sleep 200ms, time StopRender() until the callback returns
// C: PauseRender -> sample count frozen -> ResumeRender -> advancing
// D: call control.is_stop_requested() BEFORE StartRender, then render
```

```bash
source env.sh
g++ -std=c++17 -O2 -Wno-deprecated -I$S -Itracer \
    -I$USD_ROOT/include -I/usr/include/python3.12 \
    -o $S/test_control $S/test_control_hd.cpp \
    -L$USD_ROOT/lib -lusd_hd -lusd_work -lusd_tf -lusd_gf -lusd_vt -lusd_sdf -ltbb -lpython3.12
$S/test_control
PXR_WORK_THREAD_LIMIT=2 $S/test_control
```

Measured:

```
WorkGetConcurrencyLimit()        = 12
WorkGetConcurrencyLimitSetting() = 0
A: through HdRenderThread samples=8 stopped=0 converged=1 center=(0.0480032, 0.122604, 0.40625)
B: StopRender() -> callback returned in 0.158756ms, stopped=1 samples=181
C: paused 136 -> 136 (frozen), resumed -> 366
D: peeked stop before StartRender = 1 -> render produced samples=0 stopped=1

# with PXR_WORK_THREAD_LIMIT=2
WorkGetConcurrencyLimit()        = 2
WorkGetConcurrencyLimitSetting() = 2
A: ... center=(0.0480032, 0.122604, 0.40625)      <- identical pixel, two threads instead of twelve
```

Four things this establishes:

1. **The two shims are the entire delegate-side adapter**, and they compile against USD 26.05 as
   written. `IsStopRequested()` / `IsPauseRequested()` are non-`const` members of
   `HdRenderThread`, which is fine to call through a non-const pointer held by a `const` method.
2. **`StopRender()` reaches the renderer in ~0.1 ms.** `StopRender()` blocks until the render
   thread is idle, so this number *is* the delegate's scene-edit latency.
3. **`WorkParallelForN` honours `PXR_WORK_THREAD_LIMIT` with no code from us**, and the image is
   bit-identical at 2 threads and at 12. §10.2's "honour the host's concurrency limit" is
   therefore satisfied by *not* writing a thread pool, which is the point of injecting the
   scheduler.
4. **The latch.** See below — it is the reason case D exists.

### The latch: never poll the control outside the render callback

`HdRenderThread::IsStopRequested()` is not a query, it is a state transition
(`renderThread.cpp:120-127`):

```cpp
bool HdRenderThread::IsStopRequested() {
    if (!_enableRender.test_and_set()) {
        _stopRequested = true;
    }
    return _stopRequested;
}
```

`_enableRender` is an `atomic_flag` that `StartRender()` sets and `StopRender()` clears;
`_stopRequested` is a plain bool cleared **only** by `_RenderLoop` after a render callback
returns (`renderThread.cpp:156`). So a single call to `IsStopRequested()` while the thread is idle
consumes the flag, latches `_stopRequested = true`, and the *next* `StartRender()` runs a callback
that sees a stop on its very first poll. Measured: `samples=0 stopped=1`, a black frame, with no
error and nothing in the log.

The rule that follows, and it belongs in `hydra/` when the delegate is written: **the
`render_control` is polled by `renderer::render` and by nothing else.** Not by the render pass to
decide whether to restart, not by `GetRenderStats()`, not by a debug print. The renderer already
obeys this — its only calls are inside `render()` and `render_tiles()`.

(This was found the boring way: a diagnostic `std::cout << control.is_stop_requested()` before the
first `StartRender()` turned a working test into a black image.)

---

# Step 10 — GATE 5: the viewer, by hand

The only gate that needs eyes. `viewer/debug.sh`, then:

| Do this | Expect |
|---|---|
| Just watch | image refines continuously; title counts `1/1000 … 1000/1000`, then `(converged)` |
| Press **space** mid-render | title gains `(paused)`, the sample count stops within one pass; the image stays on screen |
| Press **space** again | count resumes from where it stopped — *not* from zero |
| Press **r** | image goes black and restarts from sample 1 |
| Press **r** repeatedly, fast | no crash, no leak, no frozen window — each restart joins the previous thread first |
| Press **esc** mid-render | window closes immediately; console prints `Stopped after N samples … (interrupted)` with N < 1000 |
| Press **esc** after convergence | prints the full sample count with no `(interrupted)` |

Measured on the headless X display with `timeout` standing in for the keypress: quit at 3 s printed
`Stopped after 488 samples in 2.92534s (interrupted)` and the process was gone **21 ms** later
(16 ms of that is the loop's own `SDL_Delay`). A full run converged at 1000 samples in 5.85 s and
shut down in 50 ms.

If pressing **r** ever produces a permanently black window, `mark_unconverged(aovs)` is missing
from `start_render()` — the buffers are still flagged converged from the previous run and every
pixel write is being skipped.

---

# Step 11 — Commit

```bash
git add tracer/render_control.h tracer/schedulers.h tracer/renderer.h tracer/main.cpp \
        viewer/main.cpp docs/plans/interruptible-render-loop.md docs/Roadmap.md CHANGELOG.md
git commit -m "interruptible tile-driven render loop

The renderer now owns the progressive sample loop (one pass == one sample per
pixel over the data window, cut into tile_size squares) and does not own the
parallel dispatch: callers inject a tile_scheduler, which is tbb_schedule in
the cli/viewer and will be WorkParallelForN in the delegate. tracer/renderer.h
no longer includes any TBB header, so hydra/ can include it without pulling a
second TBB runtime into a process that already has USD's.

Cancellation follows hdEmbree: a render_control is polled once per pass and
once per tile, so the first pass is interruptible too, plus a 10ms pause loop
per pass. Measured stop-to-return: 1.1-2.2ms at 8-pixel tiles (47-66ms at 64),
and 0.16ms end to end through a real HdRenderThread. completed_samples() is
published only for passes that were not cancelled, so a half-finished pass is
never reported as a whole sample.

The viewer renders on its own thread and resolves/presents on the main thread;
space pauses, r restarts, esc stops. render() maps the bound buffers for the
duration and unmaps and marks them converged on the way out; the caller
un-converges before restarting (mark_unconverged).

Verified: scenes 0-3 are byte-identical to the pre-refactor images, at tile
sizes 1/8/16/32/64/400, serial and threaded, and under PXR_WORK_THREAD_LIMIT=2;
an inset non-tile-aligned data window fills exactly 33674/33674 pixels and
none outside; renderer.h compiles with no tbb include path at all."
```

Then tick `interruptible tile-driven render loop` in [[Roadmap]] and add a `CHANGELOG.md` entry.

---

## Definition of done

- [ ] `cmp` says scenes 0–3 and the serial scene 2 are **byte-identical** to the step 0 goldens
- [ ] Scene 2 is byte-identical at `tile_size` 1, 8, 16, 32, 64 and 400
- [ ] `$S/no_tbb` compiles **without** `-Ibuild/_deps/tbb-src/include` and renders
- [ ] `grep -rn "tbb" tracer/*.h` names only `tracer/schedulers.h`
- [ ] `grep -rn "pxr/" tracer/ viewer/` is empty — the tracer is still USD-free
- [ ] `$S/cancel 800 450 8` returns from a stop in **under 5 ms**, three times out of three
- [ ] A stopped render leaves buffers unmapped and a partial-but-correct image
- [ ] Pause freezes the sample count and resume continues it, without restarting
- [ ] `$S/subwindow` reports `33674/33674` inside and `0` outside at tile 8, 16 and 64
- [ ] `$S/test_control` prints A/B/C/D as measured, and the same centre pixel under
      `PXR_WORK_THREAD_LIMIT=2`
- [ ] The viewer's seven manual checks in step 10 all behave
- [ ] `git diff --stat tracer/camera.h tracer/render_buffer.h tracer/mat4.h` is empty
- [ ] `build-hydra` still builds `hdWeekend` and `testHdWeekend` (still vacuous — `hydra/`
      includes no tracer header until 0.3.0)
- [ ] No test file and no test CMake target added to the repo

---

## Design notes — decisions made, recorded so they aren't re-litigated

**The scheduler is injected, not abstracted.** A `tile_scheduler` typedef over `std::function` is
the smallest thing that lets `WorkParallelForN` and `tbb::parallel_for` both drive the same loop.
The alternatives were a compile-time policy template (which would put TBB back in the renderer's
include graph for anyone who instantiates it, and makes `renderer` a template for one axis of
variation) or exposing only `render_tiles()` (which duplicates the sample loop, the cancellation
points and the convergence rules into every caller). Cost of the chosen option: one indirect call
per pass, invisible in the measurements.

**The renderer owns the sample loop; the caller owns convergence policy.** `samples_to_convergence`
is a field precisely so `HdRenderSettingsTokens->convergedSamplesPerPixel` can write it. The
viewer's old adaptive batch-size heuristic is deleted rather than ported: it was compensating for
an uninterruptible `render()`.

**`set_converged(true)` is the renderer's, `set_converged(false)` is the caller's.** This corrects
[[render-target]]'s closing prediction. hdEmbree sets converged at the end of `Render()`
(`renderer.cpp:608-615`) and un-sets it from the render pass just before `StartRender()`
(`renderPass.cpp:222`). Convergence describes the render that just ended, so the renderer knows
it; un-converging is part of deciding to start a new one, so the pass knows that.

**Buffers are marked converged even on the stopped path.** hdEmbree does this unconditionally, and
it is right: a stopped render is not going to produce more samples, so leaving `is_converged()`
false would tell the host to keep re-executing a render that nobody restarted. The restart path
un-converges explicitly.

**`completed_samples` is not published for a cancelled pass.** The one place this document
knowingly diverges from hdEmbree. Justified by measurement in step 7: hdEmbree's ordering reports
one complete sample when 49% of the pixels have none.

**Stop is per tile, pause is per pass.** A pause point inside the tile loop would park worker
threads in 10 ms sleeps *inside* the host's task arena. Stop needs to be fine-grained because
every scene edit blocks on it; pause is a human pressing a key.

**No `LockFramebuffer()`.** The viewer's `resolve()` runs while tile threads are writing
`_samples` and `_sample_count`, which is a data race in the strict sense: a pixel can resolve with
an accumulation and a count that disagree by one sample. The visible consequence is one frame of
a slightly wrong value on a handful of pixels, corrected by the next resolve 16 ms later.
hdEmbree makes the same trade and §10.1 says so explicitly ("a CPU renderer writing into an
accumulation buffer that the host reads via `Resolve()` can often skip this"). If it ever needs
fixing, the hook is a `LockFramebuffer()`-shaped method on `render_control`, not a mutex inside
`render_buffer`.

**`tile_size = 8`, matching `HdEmbreeDefaultTileSize`.** It is a latency knob, not a throughput
knob — step 7 measures 1–2 ms of stop latency at 8 and 47–66 ms at 64, with no image difference
and no measurable throughput difference at 400×225. There is no reason to invent our own default.

**Per-pixel rng seeding is kept, and is better than hdEmbree's.** hdEmbree seeds one engine per
tile from `TfHash::Combine(seed, tileStart, sampleNum)`, which makes its output depend on the tile
decomposition. Ours seeds per pixel per sample, which is why step 6 can assert byte equality
across six tile sizes, two schedulers and two thread counts. The `frame_seed` field is already
`randomNumberSeed`'s home (§10.2).

**The renderer polls the control; nobody else does.** Forced by `HdRenderThread::IsStopRequested()`
being a destructive read — see "The latch" in step 9.

---

## Appendix A — the delegate shims, for 0.3.0

Compiled and exercised by step 9's gate. Not committed in this task; they belong to
`hydra wrapper` alongside `HdWeekendRenderParam` and the render pass.

```cpp
// hydra/renderControl.h
class HdWeekendRenderControl final : public render_control
{
public:
  explicit HdWeekendRenderControl(HdRenderThread *thread) : _thread(thread) {}

  // NB: only ever called from inside the render callback. IsStopRequested() is
  // a destructive read of HdRenderThread::_enableRender - calling it from the
  // render pass or a diagnostic will silently kill the NEXT render.
  bool is_stop_requested() const override { return _thread->IsStopRequested(); }
  bool is_pause_requested() const override { return _thread->IsPauseRequested(); }

private:
  HdRenderThread *_thread;
};

inline void HdWeekendWorkSchedule(size_t n, const std::function<void(size_t, size_t)> &work)
{
  WorkParallelForN(n, [&work](size_t begin, size_t end) { work(begin, end); });
}
```

Delegate constructor:

```cpp
_renderer.schedule = HdWeekendWorkSchedule;
_renderThread.SetRenderCallback([this]() {
    HdWeekendRenderControl control(&_renderThread);
    _stats = _renderer.render(_camera, _scene, _aovBindings, &control);
});
_renderThread.StartThread();
// destructor: _renderThread.StopThread();
```

Settings, when `GetRenderSettingsVersion()` bumps (§15):

| Hydra | tracer |
|---|---|
| `convergedSamplesPerPixel` | `renderer::samples_to_convergence` |
| `threadLimit` | nothing — `WorkParallelForN` reads it; scope a `WorkWithScopedParallelism` if the host sets it per-pass |
| `randomNumberSeed` (`-1` == nondeterministic) | `renderer::frame_seed` |
| `HDWEEKEND_TILE_SIZE` env, hdEmbree-style | `renderer::tile_size` |

`GetRenderStats()`:

```cpp
stats[HdPerfTokens->numCompletedSamples.GetString()] = _renderer.completed_samples();
```

and `IsPauseSupported()` / `IsStopSupported()` return `true` — the machinery behind them now
exists.

## Appendix B — where the render pass will call this (0.3.0, sketch only)

§9's five-way change detection, with the calls this task defines marked:

```
_Execute(renderPassState, renderTags):
  needStartRender = false
  if sceneVersion    changed:  needStartRender = true
  if settingsVersion changed:  _renderThread->StopRender(); re-read settings ->
                                 samples_to_convergence / frame_seed / tile_size
                                 needStartRender = true
  if view/proj       changed:  _renderThread->StopRender(); cam.set_camera(...)
                                 needStartRender = true
  if dataWindow      changed:  _renderThread->StopRender(); cam.data_window = ...
                                 (re)allocate fallback buffers
  if aovBindings     changed:  _renderThread->StopRender(); rebuild aov_bindings; clear
                                 needStartRender = true
  if needStartRender:
      mark_unconverged(_aovBindings)        // <- this task
      _renderThread->StartRender()          // callback runs renderer::render(..., &control)
```

`IsConverged()` is the AND of `is_converged()` over the bound buffers, which `render()` now sets.
The destructor must `StopRender()` — the render thread writes into pass-owned buffers.

---

## Next up

`transform support` — per-prim `GfMatrix4d` from `sceneDelegate->GetTransform(id)`, applied by
transforming the *ray* into object space rather than the geometry, which sets up instancing for
free ([[roadmap-discussion-8-26]] §6). Nothing in this task constrains it, with one exception: the
scene is now read concurrently by tile threads *and* about to be mutated by `Sync()`, so
`transform support` is the last item that can pretend the scene is immutable. The gateway that
fixes that (`AcquireSceneForEdit()` calling `StopRender()` before handing back a mutable pointer)
belongs to `scene graph with mutation`, and `StopRender()` returning in 0.16 ms is what makes it
affordable.
