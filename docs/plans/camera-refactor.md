# camera api refactor — step-by-step

**Roadmap item:** `0.2.0 - hydra prep` → `camera api refactor` (`matrix-driven rays`, `ortho/perspective`) — see [[Roadmap]]
**Context:** [[hydra-spec]] §9, §11, §17.7, §20 · [[roadmap-discussion-8-26]] §3 · [[hdtiny-stub-delegate]] "Next up"
**Every number in this document was measured on this machine on 2026-08-26. See "Pre-verified facts".**

---

## What this task is

[[hydra-spec]] §11 is three sentences long and they cost you the whole camera:

> The render pass gets what it needs from `HdRenderPassState`:
> `GetWorldToViewMatrix()`, `GetProjectionMatrix()`.
> Your renderer therefore has to generate rays **from matrices**, not from a
> fov/aperture description.

That is the entire brief. `camera` currently takes `lookfrom` / `lookat` / `vup` / `v_fov` /
`aspect_ratio` as *inputs* and computes a viewport basis in `init()`. Hydra will never give you
any of those. It gives you two 4×4 matrices and a rectangle, and it decides the resolution.

At the end of this task `tracer/camera.h` is a **matrix-driven ray generator**: you hand it a
world-to-view matrix, a view-to-NDC projection matrix, and a data window, and it hands back rays.
Perspective and orthographic come out of the same code path. The book's
`lookfrom`/`lookat`/`v_fov` vocabulary survives, but demoted to an author-side *description* that
the CLI and the viewer use to *build* matrices — code the delegate never touches.

Nothing about the output changes. This is a pure refactor, and step 7 gates it as one.

## Why it matters more than "camera refactor" sounds

Three ownership inversions get corrected, and they are the reason this is bigger than the name
([[roadmap-discussion-8-26]] §3):

1. **`camera::init()` computes `height_px` from `aspect_ratio`.** The renderer decides the
   resolution. That is backwards — framing belongs to the host. Hydra hands you a
   `dataWindow` and a render buffer and expects you to fill it.
2. **`camera` owns the render loop, the framebuffer write, and `ray_color`.** hdEmbree keeps
   `renderer.{h,cpp}` separate from the camera for a reason: the render loop is what gets
   tiled, cancelled and driven by `HdRenderThread`, and the camera is a pure function that
   loop calls. Splitting them now costs ~40 lines of moves and makes the next two roadmap
   items mechanical.
3. **There is no data window.** `render_region(x0,x1,y0,y1)` has the right *signature* but no
   notion of a sub-rect inside a larger buffer — the pixel index is `v * width_px + u`, i.e.
   the camera's own width, not the buffer's. §9 requires rendering into a sub-rect of a
   possibly larger target.

## What is explicitly NOT in this task

| Not now | Comes with |
|---|---|
| `HdRenderBuffer`, AOVs, float32 accumulation, `Resolve()` | `render target refactor` |
| `HdRenderThread`, `IsStopRequested()`, cancellation points | `interruptible tile-driven render loop` |
| `render_tile()` entry point, `WorkParallelForN`, `threadLimit` | `interruptible tile-driven render loop` |
| Making the framebuffer bottom-up | `render target refactor` — see step 4's flip note |
| Wiring any of this into `hydra/renderPass.cpp` | `hydra wrapper` (0.3.0) |
| Per-object transforms | `transform support` |
| Reading `focus_dist` / f-stop off a real `HdCamera` | `hydra wrapper` (0.3.0) |

The tracer stays **USD-free**. `tracer/` must not gain a single `pxr/` include; `hydra/` already
includes `tracer/` as headers and never links the `tracer` CMake target (that would drag oneTBB
into USD's process — see [[hdtiny-stub-delegate]] "Two TBBs is a live issue"). Step 9 is the only
place USD appears, and it is a test.

---

## Pre-verified facts

These were measured, not assumed. The programs that produced them are steps 2 and 9 — you will
re-run them yourself, but you are not discovering these from scratch.

| Claim | Measured |
|---|---|
| The adjugate 4×4 inverse below is correct for `m[row][col]` indexing | worst `\|M·M⁻¹ − I\|` = **1.2e-10** over 2000 random matrices; `\|M⁻¹·M − I\|` = **4.6e-13** |
| `look_at()` below == `GfMatrix4d::SetLookAt()` | max element diff = **0** (exactly) |
| `perspective()` below == `GfFrustum::ComputeProjectionMatrix()`, Perspective | max element diff = **0** (exactly), over 60 fov/aspect/near-far combinations |
| `orthographic()` below == `GfFrustum::ComputeProjectionMatrix()`, Orthographic | max element diff = **0** (exactly) |
| `inverse()` == `GfMatrix4d::GetInverse()` | max element diff = **1.8e-15** |
| `transform()` / `transform_dir()` == `GfMatrix4d::Transform()` / `TransformDir()` | max diff **6.9e-18** / **0** |
| hdEmbree's ortho test `round(proj[3][3]) == 1.0` | perspective → `proj[3][3]` = 0; orthographic → 1. Discriminates correctly |
| **The new matrix camera reproduces the current camera's rays** | worst origin diff **1.8e-15**, worst unit-direction diff **2.2e-16**, across all three example scenes plus two synthetic ones, at 8 pixel positions × 4 jitter/lens-sample combinations each — **including defocus blur** |

That last row is the load-bearing one. The refactor is provably a no-op at the ray level, so any
image difference in step 7 is a bug you introduced, not an expected consequence.

USD libraries on this machine are prefixed: link `-lusd_gf -lusd_tf`, not `-lgf -ltf`.

---

## The design in one page

Four files, one new concept boundary.

```
tracer/mat4.h          NEW   mat4 (row-vector, GfMatrix4d-compatible) + rect2i
                             + look_at / perspective / orthographic / inverse

tracer/camera.h        REWRITE  matrix-driven ray generator. Delegate-facing.
                                Inputs: view, proj, data_window, focus_dist, defocus_angle.
                                One method that matters: get_ray(rng, x, y).
                                NO render loop. NO framebuffer. NO ray_color. NO init().

tracer/camera_desc.h   NEW   author-side lookfrom/lookat/v_fov description, and the ONLY
                             place that turns framing choices into matrices. The delegate
                             never includes this file.

tracer/renderer.h      NEW   ray_color + the render loop, moved out of camera.h verbatim.
                             Iterates the camera's data window over the buffer's stride.
```

The boundary to hold in your head: **`camera` is what Hydra drives, `camera_desc` is what a human
writes.** If you find yourself wanting `camera::v_fov` back, the answer is that `HdRenderPassState`
does not have it either.

### Why `mat4` copies `GfMatrix4d`'s conventions exactly

`GfMatrix4d` is **row-major storage, row-vector convention** — `v' = v · M`, so translation lives
in row 3 (`m[3][0..2]`), which is the transpose of the column-major layout most GL/GLM code uses
(`matrix4d.h:56`, "by convention, vectors are treated primarily as row vectors").

Adopting the same convention means `hydra/` can copy a `GfMatrix4d` into a `mat4` with a plain
double loop and no transpose, and — more usefully — it means step 9 can assert *element equality*
against USD rather than "equivalent up to layout". A transposed matrix that is only ever used via
`transform()` produces plausible, wrong images; making the layouts identical turns that whole
class of bug into a compile-and-run check.

---

# Step 0 — Capture golden images before you touch anything

**Why:** step 7's gate compares against these. Capture them from a clean tree; once `camera.h` is
gone you cannot regenerate them.

```bash
cd /home/nick/git/weekend-raytracer
git status --porcelain          # must be empty

./tracer/release.sh 0 >/dev/null 2>&1 || cmake --build build --config Release
mkdir -p /tmp/camrefactor
for s in 0 1 2; do
  ./build/tracer/Release/tracer_cli $s > /tmp/camrefactor/gold_$s.ppm
done
md5sum /tmp/camrefactor/gold_*.ppm | tee /tmp/camrefactor/gold.md5
```

Also record the current per-pixel timing from the `tracer_cli` stderr line for scene 0 — step 8
checks you did not regress it.

> **Note the pre-existing bug you are about to fix.** `camera::render_region` (the serial path)
> does `buffer.samples[i] += samples + 1` while `render_region_parallel` does `+= samples`.
> Single-threaded renders have been dividing by one sample too many for every batch. Step 5
> unifies both onto `+= samples`, so **single-threaded output will change** and only the default
> multi-threaded goldens above are valid comparanda. Do not spend an hour on this in step 7.

---

# Step 1 — Write `tracer/mat4.h`

**Why first:** everything else depends on it and it is the only part with a real chance of a silent
sign or transpose error. Step 2 proves it before anything is built on top.

`vec3.h` cannot be included standalone — it includes `tracer.h`, which includes `color.h`, which
needs `vec3`. Include `tracer.h` and let it pull `vec3.h` in. (Pre-existing; not this task's
problem, but it will bite you in the test file otherwise.)

```cpp
#pragma once

#include <cmath>

#include "tracer.h"

// 4x4 double matrix. Row-major storage, ROW-VECTOR convention: v' = v * M, so
// translation lives in m[3][0..2].
//
// This is deliberately identical to pxr GfMatrix4d (matrix4d.h:56) so hydra/ can
// copy element-for-element with no transpose, and so tests can assert exact
// element equality against USD rather than "equivalent up to layout".
struct mat4 {
  double m[4][4];

  static mat4 identity()
  {
    mat4 r{};
    for (int i = 0; i < 4; i++) {
      r.m[i][i] = 1;
    }
    return r;
  }

  // treats v as (x,y,z,1) and divides through by the resulting w.
  // == GfMatrix4d::Transform
  vec3 transform(const vec3 &v) const
  {
    double x = v[0] * m[0][0] + v[1] * m[1][0] + v[2] * m[2][0] + m[3][0];
    double y = v[0] * m[0][1] + v[1] * m[1][1] + v[2] * m[2][1] + m[3][1];
    double z = v[0] * m[0][2] + v[1] * m[1][2] + v[2] * m[2][2] + m[3][2];
    double w = v[0] * m[0][3] + v[1] * m[1][3] + v[2] * m[2][3] + m[3][3];
    return w == 1 ? vec3(x, y, z) : vec3(x / w, y / w, z / w);
  }

  // treats v as (x,y,z,0): no translation, no divide.  == GfMatrix4d::TransformDir
  vec3 transform_dir(const vec3 &v) const
  {
    return vec3(v[0] * m[0][0] + v[1] * m[1][0] + v[2] * m[2][0],
                v[0] * m[0][1] + v[1] * m[1][1] + v[2] * m[2][1],
                v[0] * m[0][2] + v[1] * m[1][2] + v[2] * m[2][2]);
  }
};

inline mat4 operator*(const mat4 &a, const mat4 &b)
{
  mat4 r{};
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      double s = 0;
      for (int k = 0; k < 4; k++) {
        s += a.m[i][k] * b.m[k][j];
      }
      r.m[i][j] = s;
    }
  }
  return r;
}

// Adjugate inverse via 2x2 sub-determinants. Layout-agnostic: this is a genuine
// inverse of the matrix indexed as m[row][col]. Verified to 1e-10 over 2000
// random matrices and to 1.8e-15 against GfMatrix4d::GetInverse (step 2, step 9).
inline mat4 inverse(const mat4 &x)
{
#define M(i, j) x.m[i][j]
  double s0 = M(0, 0) * M(1, 1) - M(1, 0) * M(0, 1);
  double s1 = M(0, 0) * M(1, 2) - M(1, 0) * M(0, 2);
  double s2 = M(0, 0) * M(1, 3) - M(1, 0) * M(0, 3);
  double s3 = M(0, 1) * M(1, 2) - M(1, 1) * M(0, 2);
  double s4 = M(0, 1) * M(1, 3) - M(1, 1) * M(0, 3);
  double s5 = M(0, 2) * M(1, 3) - M(1, 2) * M(0, 3);

  double c5 = M(2, 2) * M(3, 3) - M(3, 2) * M(2, 3);
  double c4 = M(2, 1) * M(3, 3) - M(3, 1) * M(2, 3);
  double c3 = M(2, 1) * M(3, 2) - M(3, 1) * M(2, 2);
  double c2 = M(2, 0) * M(3, 3) - M(3, 0) * M(2, 3);
  double c1 = M(2, 0) * M(3, 2) - M(3, 0) * M(2, 2);
  double c0 = M(2, 0) * M(3, 1) - M(3, 0) * M(2, 1);

  double det = s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
  double d = 1.0 / det;

  mat4 r{};
  r.m[0][0] = ( M(1, 1) * c5 - M(1, 2) * c4 + M(1, 3) * c3) * d;
  r.m[0][1] = (-M(0, 1) * c5 + M(0, 2) * c4 - M(0, 3) * c3) * d;
  r.m[0][2] = ( M(3, 1) * s5 - M(3, 2) * s4 + M(3, 3) * s3) * d;
  r.m[0][3] = (-M(2, 1) * s5 + M(2, 2) * s4 - M(2, 3) * s3) * d;

  r.m[1][0] = (-M(1, 0) * c5 + M(1, 2) * c2 - M(1, 3) * c1) * d;
  r.m[1][1] = ( M(0, 0) * c5 - M(0, 2) * c2 + M(0, 3) * c1) * d;
  r.m[1][2] = (-M(3, 0) * s5 + M(3, 2) * s2 - M(3, 3) * s1) * d;
  r.m[1][3] = ( M(2, 0) * s5 - M(2, 2) * s2 + M(2, 3) * s1) * d;

  r.m[2][0] = ( M(1, 0) * c4 - M(1, 1) * c2 + M(1, 3) * c0) * d;
  r.m[2][1] = (-M(0, 0) * c4 + M(0, 1) * c2 - M(0, 3) * c0) * d;
  r.m[2][2] = ( M(3, 0) * s4 - M(3, 1) * s2 + M(3, 3) * s0) * d;
  r.m[2][3] = (-M(2, 0) * s4 + M(2, 1) * s2 - M(2, 3) * s0) * d;

  r.m[3][0] = (-M(1, 0) * c3 + M(1, 1) * c1 - M(1, 2) * c0) * d;
  r.m[3][1] = ( M(0, 0) * c3 - M(0, 1) * c1 + M(0, 2) * c0) * d;
  r.m[3][2] = (-M(3, 0) * s3 + M(3, 1) * s1 - M(3, 2) * s0) * d;
  r.m[3][3] = ( M(2, 0) * s3 - M(2, 1) * s1 + M(2, 2) * s0) * d;
#undef M
  return r;
}

// ---------------------------------------------------------------------------
// Matrix builders. Only camera_desc.h and tests call these - the Hydra delegate
// receives matrices already built and must never construct its own.
// ---------------------------------------------------------------------------

// World-to-view. Element-for-element identical to GfMatrix4d::SetLookAt.
inline mat4 look_at(const point3 &eye, const point3 &center, const vec3 &up)
{
  vec3 forward = unit_vector(center - eye);
  vec3 right = unit_vector(cross(forward, up));
  vec3 real_up = cross(right, forward);

  mat4 r = mat4::identity();
  for (int i = 0; i < 3; i++) {
    r.m[i][0] = right[i];
    r.m[i][1] = real_up[i];
    r.m[i][2] = -forward[i];
  }
  r.m[3][0] = -dot(right, eye);
  r.m[3][1] = -dot(real_up, eye);
  r.m[3][2] = dot(forward, eye);
  return r;
}

// View-to-NDC, symmetric window. Identical to GfFrustum::ComputeProjectionMatrix
// for GfFrustum::Perspective (frustum.cpp:560-574). Leaves m[3][3] == 0, which is
// what camera's orthographic test keys off.
inline mat4 perspective(double v_fov_degrees, double aspect, double near_clip, double far_clip)
{
  double t = std::tan(degrees_to_radians(v_fov_degrees) / 2);

  mat4 r{};
  r.m[0][0] = 1.0 / (aspect * t);
  r.m[1][1] = 1.0 / t;
  r.m[2][2] = -(far_clip + near_clip) / (far_clip - near_clip);
  r.m[2][3] = -1.0;
  r.m[3][2] = -2.0 * far_clip * near_clip / (far_clip - near_clip);
  r.m[3][3] = 0.0;
  return r;
}

// View-to-NDC, symmetric window. Identical to GfFrustum::ComputeProjectionMatrix
// for GfFrustum::Orthographic (frustum.cpp:552-558). Leaves m[3][3] == 1.
inline mat4 orthographic(double half_width, double half_height, double near_clip, double far_clip)
{
  mat4 r = mat4::identity();
  r.m[0][0] = 1.0 / half_width;
  r.m[1][1] = 1.0 / half_height;
  r.m[2][2] = -2.0 / (far_clip - near_clip);
  r.m[3][2] = -(far_clip + near_clip) / (far_clip - near_clip);
  return r;
}

// ---------------------------------------------------------------------------

// Integer pixel rect. y-DOWN, max INCLUSIVE - the same semantics as pxr GfRect2i
// and therefore as HdRenderPassState::GetFraming().dataWindow (hydra-spec §9).
struct rect2i {
  int min_x = 0, min_y = 0, max_x = -1, max_y = -1;

  static rect2i from_size(int width, int height) { return {0, 0, width - 1, height - 1}; }

  int width() const { return max_x - min_x + 1; }
  int height() const { return max_y - min_y + 1; }
  bool is_empty() const { return width() <= 0 || height() <= 0; }
};

inline bool operator==(const rect2i &a, const rect2i &b)
{
  return a.min_x == b.min_x && a.min_y == b.min_y && a.max_x == b.max_x && a.max_y == b.max_y;
}

inline bool operator!=(const rect2i &a, const rect2i &b) { return !(a == b); }
```

**Watch for:** `max_x = -1` in the default `rect2i` is intentional — a default-constructed rect is
*empty*, matching `GfRect2i`. If you default it to 0 you get a 1×1 window that renders a single
pixel and looks like a hang.

---

# Step 2 — GATE 1: the matrix math is right, and the new camera reproduces the old one

**Why this is the most important step in the plan:** it converts "the refactor should be
equivalent" into a number. Write the test *before* deleting the old camera, with the old math
copied in verbatim as the oracle. That copy is not throwaway — it pins the book's camera as a
permanent regression reference, so nothing can silently drift `get_ray` later.

> **A working copy exists.** The program described below was written and run to produce the
> numbers in "Pre-verified facts". Copy it rather than retyping:
> `/tmp/claude-1000/-home-nick-git-weekend-raytracer/55df08a3-ce0c-40a5-83a2-2b9f9d0aafe1/scratchpad/test_camera_reference.cpp`
> (it includes `mat4.h`, of which `/tmp/claude-1000/-home-nick-git-weekend-raytracer/55df08a3-ce0c-40a5-83a2-2b9f9d0aafe1/scratchpad/mat4.h` is the same file step 1 asks you to write).
> Build check: `g++ -O2 -I<scratch> -Itracer -o /tmp/eq <that file> && /tmp/eq`

Create `tracer/tests/test_camera.cpp`. No framework; plain comparisons and a nonzero exit,
matching the project's existing minimalism.

The file has three parts:

**(a) `inverse()` identity check** — 2000 random matrices, assert `max |M·M⁻¹ − I| < 1e-9` and
the same for `M⁻¹·M`. Expected worst case ~1.2e-10 and ~4.6e-13.

> If this fails you have a transpose error. The fix is one word, but you must know that is what
> it is: check `M(i,j)` is reading `x.m[i][j]` and not `x.m[j][i]`.

**(b) `reference_camera`** — a verbatim copy of the *current* `camera::init()` and
`camera::get_ray()` math, with the rng draws lifted out into explicit arguments so both cameras
can be fed identical jitter and lens samples:

```cpp
// Verbatim math from tracer/camera.h at b73911c, before the matrix refactor.
// This is the oracle: `Ray Tracing in One Weekend`'s camera, frozen. Do not
// "improve" it - its only job is to disagree with camera.h when camera.h breaks.
struct reference_camera {
  int width_px, height_px;
  double aspect_ratio, v_fov, focus_dist, defocus_angle;
  point3 lookfrom, lookat;
  vec3 vup;
  // derived
  point3 center, viewport_origin;
  vec3 viewport_du, viewport_dv, u, v, w, defocus_u, defocus_v;

  void init() { /* copy lines 59-85 of the old camera.h verbatim */ }

  // jx, jy are what sample_square() would have returned: [-0.5, 0.5)
  // dx, dy are what random_unit_disk() would have returned
  void get_ray(int px, int py, double jx, double jy, double dx, double dy,
               point3 &origin, vec3 &direction) const
  {
    vec3 pixel_sample = viewport_origin + ((px + jx) * viewport_du) + ((py + jy) * viewport_dv);
    origin = defocus_angle <= 0 ? center : center + dx * defocus_u + dy * defocus_v;
    direction = pixel_sample - origin;
  }
};
```

**(c) the equivalence sweep.** For each of the three example-scene camera setups plus two
synthetic ones (a no-defocus square frame, and a portrait frame with a wide lens), build both
cameras and compare rays at 8 pixel positions — all four corners, the centre, and three
off-centre asymmetric points — × 4 jitter/lens combinations.

Two details make this comparison meaningful rather than accidentally trivial:

- **Feed the same jitter through both, offset by 0.5.** The old camera draws
  `uniform(-0.5, 0.5)` and adds it to an integer pixel index whose *centre* was pre-baked into
  `viewport_origin` as `+0.5 * (du + dv)`. The new camera draws `uniform()` in `[0,1)` and adds it
  to the raw index. Since `uniform(-0.5, 0.5) == uniform() - 0.5`, for the same underlying draw the
  two land on the *identical* sub-pixel position. So the test passes `j - 0.5` to the reference
  and `j` to the new camera, and the rng stream is unchanged by the refactor.
- **Compare directions as unit vectors.** The old `direction` is unnormalised with length ≈
  `focus_dist`; the new one has length ≈ `near_clip`. Only the direction is meaningful.

Assert `max |Δorigin| < 1e-12` and `max |Δ unit direction| < 1e-12`.

Expected output (these are the measured values):

```
scene1    400x225   worst|origin diff| = 1.78e-15   worst|unit dir diff| = 2.22e-16
scene2    400x225   worst|origin diff| = 0          worst|unit dir diff| = 2.22e-16
scene3    400x225   worst|origin diff| = 2.22e-16   worst|unit dir diff| = 2.22e-16
nodof     640x640   worst|origin diff| = 2.22e-16   worst|unit dir diff| = 2.22e-16
tallish   300x400   worst|origin diff| = 8.88e-16   worst|unit dir diff| = 2.22e-16
```

**(d) an orthographic smoke test**, since there is no oracle for it — the old camera cannot do
ortho at all. Build `orthographic(4, 2, 0.1, 100)` with `look_at((0,0,5), origin, +Y)` over a
100×50 window and assert:

- every ray direction is exactly `(0, 0, -1)` in world space (the camera looks down −Z);
- ray origins span the window: top-left ≈ `(-3.96, +1.96, 4.9)`, bottom-right ≈ `(+3.96, -1.96, 4.9)`.

Those signs are the whole point of the test — **top-left must have positive y**. Getting it
backwards flips the image vertically, and a symmetric scene will not tell you.

Wire it up in `tracer/CMakeLists.txt`:

```cmake
# unit tests - plain comparisons, no framework
add_executable(tracer_tests tests/test_camera.cpp)
target_link_libraries(tracer_tests PRIVATE tracer)
```

```bash
cmake --build build --config Release && ./build/tracer/Release/tracer_tests
```

**Do not proceed until this passes.** Every later step assumes it.

---

# Step 3 — Write `tracer/camera_desc.h`

**Why a separate file:** it is the firewall. `camera.h` is what the delegate includes;
`camera_desc.h` is what humans include. Keeping them apart means a stray `desc.v_fov` inside
`hydra/` fails to compile instead of quietly reintroducing the ownership inversion.

```cpp
#pragma once

#include "camera.h"
#include "mat4.h"
#include "tracer.h"

enum class projection { perspective, orthographic };

// Author-side camera description: `Ray Tracing in One Weekend`'s
// lookfrom/lookat/v_fov vocabulary, and the ONLY place in the tracer that turns
// framing choices into matrices.
//
// The Hydra delegate never includes this file. It receives view and projection
// matrices from HdRenderPassState and a dataWindow from CameraUtilFraming
// (hydra-spec §9, §11); nothing here has an equivalent on that path.
struct camera_desc {
  point3 lookfrom = point3(0, 0, 0);
  point3 lookat = point3(0, 0, -1);
  vec3 vup = vec3(0, 1, 0);

  projection proj_type = projection::perspective;
  double v_fov = 90;             // perspective only, degrees, vertical
  double ortho_half_height = 1;  // orthographic only, world units

  double near_clip = 0.1;
  double far_clip = 1000;

  // Lens. Deliberately NOT encoded in the projection matrix, which carries
  // neither focus distance nor aperture (hydra-spec §11) - on the Hydra path
  // these arrive as HdCamera attributes instead.
  double focus_dist = 10;
  double defocus_angle = 0;

  // Resolution is the HOST's decision now, so it is an argument, not a member.
  // This is the line that used to be `camera::init()` computing height_px from
  // aspect_ratio, i.e. the ownership inversion this task exists to remove.
  camera build(int width, int height) const
  {
    double aspect = double(width) / double(height);

    camera cam;
    cam.set_camera(look_at(lookfrom, lookat, vup),
                   proj_type == projection::orthographic
                       ? orthographic(ortho_half_height * aspect, ortho_half_height,
                                      near_clip, far_clip)
                       : perspective(v_fov, aspect, near_clip, far_clip));
    cam.data_window = rect2i::from_size(width, height);
    cam.focus_dist = focus_dist;
    cam.defocus_angle = defocus_angle;
    return cam;
  }
};
```

**Note what is gone:** `aspect_ratio`, `width_px`, `height_px`, `multithread`, `max_bounces`.
The first three are now `build()`'s arguments; the last two move to `renderer` in step 5.

---

# Step 4 — Rewrite `tracer/camera.h`

**Why:** this is the deliverable. Both roadmap sub-items — `matrix-driven rays` and
`ortho/perspective` — are the body of `get_ray()`.

```cpp
#pragma once

#include <cmath>
#include <ostream>

#include "mat4.h"
#include "tracer.h"

// Matrix-driven ray generator: the piece a Hydra render pass drives directly.
//
// hydra-spec §11: the render pass gets only GetWorldToViewMatrix() and
// GetProjectionMatrix() - no fov, no aperture, no lookat. So this class takes
// matrices and a data window, and nothing else. Ray generation follows
// hdEmbree's recipe (renderer.cpp:691-719) so perspective and orthographic
// share one code path.
class camera
{
public:
  // Which sub-rect of the render target to fill. y-DOWN, max inclusive -
  // GfRect2i semantics, same coordinate system as
  // HdRenderPassState::GetFraming().dataWindow (hydra-spec §9). May be smaller
  // than the framebuffer; renderer.h uses the buffer's stride, not this width.
  rect2i data_window;

  // Thin-lens depth of field. Not derivable from the projection matrix, which
  // carries neither (hydra-spec §11); on the Hydra path these come from
  // HdCamera attributes. defocus_angle <= 0 disables it.
  double focus_dist = 10;
  double defocus_angle = 0;

  // Sub-pixel jitter, hdEmbree's `jitterCamera` config knob. Off => rays through
  // exact pixel centres, which is what makes step 2's comparison deterministic.
  bool jitter = true;

  void set_camera(const mat4 &view_matrix, const mat4 &proj_matrix)
  {
    set_camera(view_matrix, proj_matrix, inverse(view_matrix), inverse(proj_matrix));
  }

  // Overload for callers that already hold the inverses - the Hydra delegate
  // gets them free from GfMatrix4d::GetInverse().
  void set_camera(const mat4 &view_matrix, const mat4 &proj_matrix,
                  const mat4 &inv_view_matrix, const mat4 &inv_proj_matrix)
  {
    view = view_matrix;
    proj = proj_matrix;
    inv_view = inv_view_matrix;
    inv_proj = inv_proj_matrix;

    // hdEmbree's discriminator (renderer.cpp:703). GfFrustum leaves proj[3][3]
    // at 0 for Perspective and 1 for Orthographic; verified both ways.
    orthographic = std::round(proj.m[3][3]) == 1.0;
  }

  bool is_orthographic() const { return orthographic; }
  const mat4 &view_matrix() const { return view; }
  const mat4 &proj_matrix() const { return proj; }

  ray get_ray(rng &generator, int x, int y) const
  {
    double jx = jitter ? generator.uniform() : 0.5;
    double jy = jitter ? generator.uniform() : 0.5;

    // pixel -> NDC across the data window.
    double ndc_x = 2 * ((x + jx - data_window.min_x) / double(data_window.width())) - 1;

    // The data window is y-down and framebuffer.h is top-left origin, so row
    // indices agree directly and no loop flip is needed. NDC is y-up, hence the
    // sign inversion here. hdEmbree instead swaps its loop bounds
    // (renderer.cpp:626-635) because its image lines run bottom-to-top.
    //
    // >>> THIS IS THE LINE TO REVISIT if `render target refactor` makes the
    // >>> framebuffer bottom-up to match HdRenderBuffer. (hydra-spec §9)
    double ndc_y = 1 - 2 * ((y + jy - data_window.min_y) / double(data_window.height()));

    // Un-project through the near plane (NDC z == -1) into view space.
    vec3 near_plane_trace = inv_proj.transform(vec3(ndc_x, ndc_y, -1));

    point3 origin;
    vec3 direction;
    if (orthographic) {
      // parallel rays from the near plane trace
      origin = near_plane_trace;
      direction = vec3(0, 0, -1);
    } else {
      // perspective: from the eye through the near plane trace
      origin = point3(0, 0, 0);
      direction = near_plane_trace;
    }

    // Thin lens, in view space: scatter the origin over the aperture disk at
    // z == 0 and re-aim at wherever the pinhole ray crossed the focal plane
    // (z == -focus_dist), so that plane stays sharp.
    if (!orthographic && defocus_angle > 0) {
      double lens_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
      vec3 focal_point = direction * (focus_dist / -direction.z());
      vec3 lens_sample = random_unit_disk(generator);
      origin = point3(lens_sample.x() * lens_radius, lens_sample.y() * lens_radius, 0);
      direction = focal_point - origin;
    }

    // view space -> world space
    return ray(inv_view.transform(origin), inv_view.transform_dir(direction));
  }

  void print_settings(std::ostream &out) const
  {
    out << "\nCamera settings\n"
        << "===============\n"
        << "Projection: " << (orthographic ? "orthographic" : "perspective") << "\n"
        << "Data window: (" << data_window.min_x << ", " << data_window.min_y << ") - ("
        << data_window.max_x << ", " << data_window.max_y << ")  "
        << data_window.width() << "x" << data_window.height() << "\n"
        << "Camera position: (" << inv_view.transform(point3(0, 0, 0)) << ")\n"
        << "Focus distance: " << focus_dist << "  defocus angle: " << defocus_angle << "\n"
        << "Jitter: " << (jitter ? "on" : "off") << "\n"
        << "\n"
        << std::flush;
  }

private:
  mat4 view = mat4::identity();
  mat4 proj = mat4::identity();
  mat4 inv_view = mat4::identity();
  mat4 inv_proj = mat4::identity();
  bool orthographic = false;
};
```

### Why the defocus rewrite is equivalent, not merely similar

Worth understanding, because it looks like a behaviour change and is not.

The old camera scaled its viewport by `focus_dist` (`vheight = 2 * h * focus_dist`) **and** pushed
the viewport plane out to `center - focus_dist * w`. Those two cancel: the angular extent is
`tan(v_fov / 2)` regardless of `focus_dist`, so `focus_dist` never affected framing — only which
plane was sharp. It happened to be the plane the viewport sat on, which is why aiming rays at
`pixel_sample` produced correct depth of field.

In the matrix formulation the near plane is wherever `near_clip` puts it, so the focal plane has to
be found explicitly: scale the view-space direction until its z reaches `-focus_dist`. Same disk
radius, same focal plane, same result — step 2 measures the difference at 1.8e-15.

Two things that follow and are worth stating: `focus_dist` no longer influences framing *at all*
(correct, and matches how `HdCamera` treats it), and `near_clip` / `far_clip` do not affect ray
*directions* either — only the parameterisation. Do not chase an image difference into `near_clip`.

### Ortho and defocus

Skipped deliberately. hdEmbree has no depth of field at all, orthographic depth of field needs a
lens model this renderer does not have, and Hydra gives you no signal that would disambiguate.
Leaving the branch off is the honest behaviour.

---

# Step 5 — Write `tracer/renderer.h`, moving the loop out of the camera

**Why:** three responsibilities were fused in `camera`. `renderer` takes the two that get tiled
and cancelled next; `camera` keeps the pure function. This mirrors hdEmbree's
`renderer.{h,cpp}` / `HdCamera` split, and it is what makes `interruptible tile-driven render
loop` a small task instead of another rewrite.

```cpp
#pragma once

#include <chrono>
#include <cstdint>

#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>

#include "camera.h"
#include "framebuffer.h"
#include "hittable.h"
#include "material.h"
#include "rng.h"
#include "tracer.h"

using namespace std::chrono;

struct renderer {
  int max_bounces = 20;
  bool multithread = true;

  // rng.h's third sample_seed slot. hydra-spec §10.2's `randomNumberSeed`
  // render setting drops straight in here; -1 there means "nondeterministic".
  uint64_t frame_seed = 0;

  // Accumulates `samples` more samples per pixel into `buffer` over the
  // camera's data window. Returns elapsed milliseconds.
  double render(const camera &cam, const hittable &world, framebuffer &buffer, int samples)
  {
    auto start = high_resolution_clock::now();
    const rect2i &window = cam.data_window;

    if (multithread) {
      tbb::parallel_for(
          tbb::blocked_range2d<int>(window.min_y, window.max_y + 1, 16,
                                    window.min_x, window.max_x + 1, 16),
          [&](const tbb::blocked_range2d<int> &tile) {
            render_region(cam, world, buffer, samples, tile.cols().begin(), tile.cols().end(),
                          tile.rows().begin(), tile.rows().end());
          });
    } else {
      render_region(cam, world, buffer, samples, window.min_x, window.max_x + 1, window.min_y,
                    window.max_y + 1);
    }

    using ms_d = duration<double, std::milli>;
    return ms_d(high_resolution_clock::now() - start).count();
  }

private:
  void render_region(const camera &cam, const hittable &world, framebuffer &buffer, int samples,
                     int x0, int x1, int y0, int y1)
  {
    for (int y = y0; y < y1; y++) {
      for (int x = x0; x < x1; x++) {
        // stride is the BUFFER's width, not the data window's - the window may
        // be a sub-rect of a larger target (hydra-spec §9).
        int i = y * buffer.width + x;
        int sample_base = buffer.samples[i];
        for (int sample = 0; sample < samples; sample++) {
          rng generator = rng(sample_seed(i, sample_base + sample, frame_seed));
          ray r = cam.get_ray(generator, x, y);
          buffer.pixels[i] += ray_color(generator, r, max_bounces, world);
        }
        buffer.samples[i] += samples;
      }
    }
  }

  color ray_color(rng &generator, const ray &r, int depth, const hittable &world)
  {
    // moved verbatim from camera.h
  }
};
```

Changes to be deliberate about:

- **`i = y * buffer.width + x`**, not `y * width_px + x`. This is the data window becoming real.
  With a full-frame window the two are equal, which is why step 7's gate still holds.
- **Both paths now do `buffer.samples[i] += samples`.** Fixes the serial `+ 1` noted in step 0.
- **`render_region` takes `(x0, x1, y0, y1)` and both call sites pass exclusive upper bounds.**
  `rect2i` is max-*inclusive*, so every conversion is `max + 1`. This off-by-one is the single most
  likely bug in this step; a missing `+1` drops the last row and column, which reads as a thin dark
  border and is easy to dismiss as a rendering artefact.
- `ray_color` moves unchanged. Resist tidying it — keeping it byte-identical is what lets step 7
  attribute any image change to the camera.

---

# Step 6 — Update the three call sites

### `tracer/example_scenes.h`

Change every `camera &camera` parameter to `camera_desc &desc`, drop `camera.init()`, and drop the
framing lines from `load_scene`:

```cpp
inline void load_scene(int i, hittable_list &world, camera_desc &desc)
{
  switch (i) { /* unchanged */ }
  // no init(), no width_px, no max_bounces - the host owns all three now
}
```

Inside each scene, `camera.lookfrom = ...` becomes `desc.lookfrom = ...`; `camera.v_fov` becomes
`desc.v_fov`; and so on. `focus_dist` and `defocus_angle` carry over unchanged — §11 is explicit
that these survive precisely *because* the projection matrix cannot express them.

`max_bounces = 10` was set here and is now a `renderer` field; set it in each host. Keep the value
at 10 or step 7's gate will fail for an uninteresting reason.

### `tracer/main.cpp`

```cpp
int i_scene = argc > 1 ? atoi(argv[1]) : 0;
bool single_thread = argc > 2 && atoi(argv[2]) > 0;
int width  = argc > 3 ? atoi(argv[3]) : 400;
int height = argc > 4 ? atoi(argv[4]) : 225;   // was width / (16/9)

hittable_list world;
camera_desc desc;
load_scene(i_scene, world, desc);

camera cam = desc.build(width, height);

renderer r;
r.max_bounces = 10;
r.multithread = !single_thread;

framebuffer buffer;
buffer.allocate(width, height);

double render_duration = r.render(cam, world, buffer, 50);
```

...and replace `camera.width_px` / `camera.height_px` in the PPM header and the timing line with
`buffer.width` / `buffer.height`. The new width/height arguments are optional, but adding them is
the cheapest possible demonstration that framing is now the host's call — and it lets you render
non-16:9 frames, which is how you would catch an aspect bug.

**Default height:** `400 / (16.0/9.0)` truncated is **225**. Use 225 exactly or the goldens will not
match.

### `viewer/main.cpp`

`render_to_buffer()` currently rebuilds the scene, the camera *and* reallocates the framebuffer on
**every** sample batch — several times a second. That was hidden behind `camera.init()`; with
`build()` taking explicit dimensions it becomes obvious, and it has to be hoisted:

```cpp
// once, before the loop
hittable_list world;
camera_desc desc;
load_scene(1, world, desc);

const int render_width = 400, render_height = 225;
camera cam = desc.build(render_width, render_height);

renderer r;
r.max_bounces = 10;

framebuffer buffer;
buffer.allocate(render_width, render_height);

// inside the loop, replacing render_to_buffer(1, buffer, samples_to_do)
double ms = r.render(cam, world, buffer, samples_to_do);
```

Keep the logging, the EMA pacing and the texture (re)creation exactly as they are — the texture
still needs to react to `buffer.width` / `buffer.height`, and leaving that path alone means the
window-resize work in a later item has one obvious place to go.

Build both, with the tests:

```bash
cmake --build build --config Release && ./build/tracer/Release/tracer_tests
```

---

# Step 7 — GATE 2: the images did not change

```bash
for s in 0 1 2; do
  ./build/tracer/Release/tracer_cli $s > /tmp/camrefactor/new_$s.ppm
done
md5sum -c /tmp/camrefactor/gold.md5
```

**Expect the md5 check to FAIL, and expect that to be fine.** Step 2 measured ray differences at
~1e-15: the matrix path reorders the arithmetic, so the last bits move, and once a ray differs in
its last bit a rejection-sampling loop or a hit/miss branch can take a different path and diverge
visibly in a handful of pixels. Byte equality is not the claim.

The claim is *perceptual* equality, so compare properly:

```python
# /tmp/camrefactor/cmp.py
import sys
def read(p):
    t = open(p).read().split()
    assert t[0] == 'P3'
    w, h = int(t[1]), int(t[2])
    return w, h, [int(v) for v in t[4:]]

w1, h1, a = read(sys.argv[1])
w2, h2, b = read(sys.argv[2])
assert (w1, h1) == (w2, h2), f"size changed: {w1}x{h1} vs {w2}x{h2}"
d = [abs(x - y) for x, y in zip(a, b)]
n = sum(1 for v in d if v)
rms = (sum(v * v for v in d) / len(d)) ** 0.5
print(f"{w1}x{h1}  max|d|={max(d)}  rms={rms:.4f}  differing channels={n}/{len(d)} ({100*n/len(d):.3f}%)")
```

```bash
for s in 0 1 2; do
  python3 /tmp/camrefactor/cmp.py /tmp/camrefactor/gold_$s.ppm /tmp/camrefactor/new_$s.ppm
done
```

**Pass:** `max|d| <= 2`, `rms < 0.1`, differing channels well under 1%. That is last-bit noise in a
stochastic renderer.

**Fail, and what it means:**

| Symptom | Cause |
|---|---|
| `size changed` | default height is not 225 |
| Image is vertically mirrored | the `ndc_y` sign in step 4 |
| Image is horizontally mirrored | `look_at`'s `right` / `real_up` cross-product order |
| Whole image subtly wrong scale, correct composition | aspect fed to `perspective()` — should be `width/height`, not `desc.aspect_ratio` |
| Thin dark border on the right and bottom edges | missing `max + 1` in step 5's loop bounds |
| Depth of field gone or wildly strong | `focus_dist / -direction.z()` sign, or `defocus_angle / 2` |
| `max\|d\|` in the tens, scattered | `max_bounces` is not 10 |
| Correct at the centre, wrong at the edges | `data_window.min_x/min_y` not subtracted in `get_ray` |

Then eyeball them, because a metric can pass on a stupid image:

```bash
xdg-open /tmp/camrefactor/new_0.ppm
```

---

# Step 8 — GATE 3: orthographic renders, and nothing got slower

The refactor is only half-delivered if ortho is untested at image scale — step 2 tested rays, not
pixels. Add a fourth scene to `example_scenes.h` reusing scene 2's geometry:

```cpp
void scene_4_ortho(hittable_list &world, camera_desc &desc)
{
  // same five spheres as scene_2
  desc.lookfrom = vec3(0, 0, 3);
  desc.lookat = vec3(0, 0, -1);
  desc.proj_type = projection::orthographic;
  desc.ortho_half_height = 1.2;
  desc.defocus_angle = 0;      // no DoF under ortho
}
```

```bash
./build/tracer/Release/tracer_cli 3 > /tmp/camrefactor/ortho.ppm && xdg-open /tmp/camrefactor/ortho.ppm
```

**Expect:** the spheres read as flat circles with no perspective convergence, the ground plane
shows as a hard horizontal edge rather than a receding plane, and the left/right spheres are the
same size as the centre one. If it looks even slightly perspective, `round(proj[3][3]) == 1.0` is
not firing — print `is_orthographic()` and check.

Render it non-square too (`tracer_cli 3 0 600 200`) and confirm the circles stay circular. That is
the `ortho_half_height * aspect` line in `camera_desc::build`.

Then the performance check:

```bash
./build/tracer/Release/tracer_cli 0 > /dev/null      # read the ms/px on stderr
```

Compare against the number recorded in step 0. `get_ray` gained two 4×4 transforms per ray; that
is noise next to `ray_color`'s recursion, so **expect within a few percent**. A large regression
means `set_camera` — and therefore two `inverse()` calls — is being invoked per ray or per tile
instead of once per frame. It should be called exactly once, from `camera_desc::build`.

Also confirm the viewer still converges smoothly and no longer stutters (you removed a per-batch
scene rebuild):

```bash
./build/viewer/Release/viewer
```

---

# Step 9 — GATE 4: assert against USD itself

**Why:** steps 2–8 prove the camera is self-consistent and matches the *old* camera. They do not
prove it matches the matrices **Hydra will actually hand you**. This is the step that de-risks
`hydra wrapper`, and it is ~80 lines.

This is the only place USD appears, so it lives in `hydra/`, which is a separate CMake project
already configured against `$USD_ROOT`.

> **A working copy exists:** `/tmp/claude-1000/-home-nick-git-weekend-raytracer/55df08a3-ce0c-40a5-83a2-2b9f9d0aafe1/scratchpad/testMat4Gf_reference.cpp`. It produced the six rows in the
> table below. Built with
> `g++ -std=c++17 -I<scratch> -Itracer -I$USD_ROOT/include -L$USD_ROOT/lib -lusd_gf -lusd_tf`.

Create `hydra/tests/testMat4Gf.cpp`, including both `pxr/base/gf/{matrix4d,frustum}.h` and the
tracer's `mat4.h`, and assert element-for-element equality for:

| tracer | pxr | Expected max element diff |
|---|---|---|
| `look_at(eye, center, up)` | `GfMatrix4d::SetLookAt` | **0** |
| `perspective(fov, aspect, n, f)` | `GfFrustum::SetPerspective(fov, /*isFovVertical=*/true, aspect, n, f)` then `ComputeProjectionMatrix()` | **0** |
| `orthographic(hw, hh, n, f)` | `GfFrustum::SetOrthographic(-hw, hw, -hh, hh, n, f)` then `ComputeProjectionMatrix()` | **0** |
| `inverse(m)` | `GfMatrix4d::GetInverse()` | **1.8e-15** |
| `mat4::transform(v)` | `GfMatrix4d::Transform(GfVec3d)` | **6.9e-18** |
| `mat4::transform_dir(v)` | `GfMatrix4d::TransformDir(GfVec3d)` | **0** |

Sweep the projections over several fovs, aspects and near/far pairs — 60-odd combinations is a
couple of nested loops and it is the difference between "matches" and "matches for the one case I
tried". Also assert the discriminator both ways: `round(perspectiveProj[3][3]) == 0` and
`round(orthoProj[3][3]) == 1`.

Add to `hydra/CMakeLists.txt`:

```cmake
add_executable(testMat4Gf tests/testMat4Gf.cpp)
target_link_libraries(testMat4Gf PRIVATE gf tf)
target_include_directories(testMat4Gf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../tracer)
```

```bash
source env.sh
cmake --build build-hydra --target testMat4Gf -j && ./build-hydra/testMat4Gf
```

The three exact zeros are not luck — `look_at` and `perspective` above were transcribed from
`matrix4d.cpp:807` and `frustum.cpp:534` operation-for-operation, which is why they are worth
keeping that way even where a shorter algebraic form exists. If a zero becomes nonzero, you
"simplified" one of them.

> Linking note: USD libraries here are prefixed `usd_` on disk (`libusd_gf.so`), but the CMake
> imported targets are the unprefixed `gf` / `tf`. Use the targets. A hand-rolled
> `-lgf` fails with `cannot find -lgf`.

---

# Step 10 — Commit

```bash
git add tracer/mat4.h tracer/camera.h tracer/camera_desc.h tracer/renderer.h \
        tracer/tests/ tracer/example_scenes.h tracer/main.cpp tracer/CMakeLists.txt \
        viewer/main.cpp hydra/tests/testMat4Gf.cpp hydra/CMakeLists.txt \
        docs/plans/camera-refactor.md
git commit -m "camera: matrix-driven rays, ortho/perspective, host-owned framing

camera now takes a world-to-view matrix, a view-to-NDC projection matrix and a
y-down data window, and generates rays via hdEmbree's recipe - one code path for
perspective and orthographic, discriminated by round(proj[3][3]) == 1.

Framing moves to the host: camera::init() no longer derives height from
aspect_ratio. lookfrom/lookat/v_fov survive as camera_desc, which is the only
place matrices get built and which the Hydra delegate never includes.

ray_color and the render loop move to renderer.h, ahead of the tile-driven
interruptible loop. Fixes the serial path counting one sample too many.

Rays verified equivalent to the pre-refactor camera to 1.8e-15, and the matrix
builders verified element-exact against GfMatrix4d/GfFrustum."
```

Then tick `camera api refactor` and both its sub-items in [[Roadmap]].

---

## Definition of done

- [ ] `tracer_tests` passes: `inverse()` identity, ray equivalence < 1e-12 across five camera
      setups, orthographic origins/directions correct **including the sign of y at top-left**
- [ ] `testMat4Gf` passes: three exact zeros against `GfMatrix4d` / `GfFrustum`, inverse and
      transforms within 2e-15
- [ ] Scenes 0–2 render within `max|d| <= 2`, `rms < 0.1` of the step 0 goldens
- [ ] Scene 3 renders as visibly orthographic, and stays circular at 600×200
- [ ] `tracer_cli 0` ms/px within a few percent of the step 0 baseline
- [ ] `viewer` converges smoothly, no per-batch scene rebuild
- [ ] `grep -rn "pxr/" tracer/ viewer/` is empty — the tracer is still USD-free
- [ ] `grep -rn "camera_desc\|v_fov\|lookfrom" hydra/` is empty — the firewall holds
- [ ] `grep -rn "width_px\|aspect_ratio\|\.init()" tracer/ viewer/` is empty — inversion gone
- [ ] `build-hydra` still builds `hdWeekend` and `testHdWeekend` (you changed headers it includes)

---

## Design notes — decisions made, recorded so they aren't re-litigated

**Row-vector convention, not column-vector.** Copying `GfMatrix4d` costs nothing at the tracer's
scale and buys element-exact assertions against USD (step 9) plus a transpose-free copy in the
delegate. The cost is that `mat4` disagrees with GLM/GL habits; the comment at the top of the
struct is load-bearing for whoever reads it next.

**`camera` does not own its inverses' computation.** `set_camera` has a four-matrix overload
precisely so the delegate can pass `GfMatrix4d::GetInverse()` results straight through. Hydra
already has them, `inverse()` is not free, and `_Execute` may be called per frame.

**No `render_tile()` yet.** §17.6 requires the tracer to stop owning its parallel loop so it can
run inside USD's TBB arena via `WorkParallelForN`. `render_region` is already the right shape — a
rectangle plus a sample index — but exposing a tile entry point means deciding tile indexing,
`renderThread` cancellation points and `WorkGetConcurrencyLimitSetting()` all at once. That is
`interruptible tile-driven render loop`, and it is a smaller task now that the loop is in
`renderer.h` and takes the window from the camera.

**The framebuffer stays top-left origin.** It happens to agree with the y-down data window, so the
flip hdEmbree needs (`renderer.cpp:626-635`) is currently a no-op and lives as a single sign in
`ndc_y`. If `render target refactor` makes the buffer bottom-up to match `HdRenderBuffer`'s
line order, that sign flips and the loop bounds gain the swap. The comment in step 4 marks the
exact line; debug it on a scene asymmetric in **both** axes.

**`near_clip` / `far_clip` are in `camera_desc`, not `camera`.** The camera never needs them —
they only exist to build a projection matrix, and on the Hydra path the matrix arrives with them
already baked in. Storing them on `camera` would invite code that reads them back, which is
exactly the ownership inversion this task removes.

**`frame_seed` is on `renderer`, not `camera`.** `rng.h`'s third `sample_seed` slot is already
§10.2's `randomNumberSeed` render setting, unused. Surfacing it now costs one field and means the
setting has somewhere to land; `-1` meaning nondeterministic is the render-settings layer's
problem, not the tracer's.

**Ortho gets no depth of field.** hdEmbree has none at all; ortho DoF needs a lens model that does
not exist here; Hydra provides no signal to disambiguate. Silently ignoring `defocus_angle` under
ortho beats inventing behaviour.

---

## Next up

`render target refactor` — spec **T1** together with this one. It is the other half of "first
pixels": `HdRenderBuffer`'s 12 pure virtuals, the three-allocation resolved/accumulation/count
split, `Resolve()`, the atomic `Map`/`Unmap` count, and `color` as premultiplied
`HdFormatFloat32Vec4` (§8.1–8.3). `framebuffer.h` is already ~70% the right shape — wrong in type
(3 doubles vs float32×4) and missing the host-facing contract.

Decide the buffer's line order there, not later: it is the one decision that reaches back into
this task, and step 4's marked comment is where it lands.
