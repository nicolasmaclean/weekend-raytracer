# transform support — step-by-step

**Roadmap item:** `0.2.0 - hydra prep` → `transform support` — see [[Roadmap]]
**Context:** [[hydra-spec]] §7.4 (`GetTransform`), §14 (instancing), §6 (scene edits) · [[roadmap-discussion-8-26]] §6, §5 (items 3 and 5) · [[interruptible-render-loop]] "Next up"
**Every number in this document was measured on this machine on 2026-08-27. See "Pre-verified facts".**
**Re-verified 2026-08-27 after implementation — see "Verification log" at the end.**

---

## What this task is

[[hydra-spec]] §7.4 gives the requirement one table row:

> | Transform | `sceneDelegate->GetTransform(id)` | `GfMatrix4d` |

and the USD header says what that matrix is:

> Returns the object space transform, including all parent transforms.
> (`hd/sceneDelegate.h:468`)

So: one 4×4 double per Rprim, already flattened through the prim's ancestors, object-to-world.
Nothing in the tracer can consume it today. `sphere` stores a `center` and a `radius`; there is no
place to put a matrix and no code that would read one.

[[roadmap-discussion-8-26]] §6 already picked the implementation:

> Transforms: per-prim `GfMatrix4d` from `sceneDelegate->GetTransform(id)`. Transforming the
> *ray* into object space rather than the geometry also sets up instancing for free.

At the end of this task there is one new 60-line header, `tracer/instance.h`, holding a hittable
that wraps a **prototype** hittable plus an object-to-world matrix, transforms the incoming ray
into the prototype's object space, and transforms the hit back out. `mat4.h` gains the two helpers
it needs (`transpose`, `is_finite`) and three author-side builders (`translate`, `scale`,
`rotate`), and `example_scenes.h` gains a scene that exercises them.

Nothing about the existing four scenes changes. Step 4 gates that as **byte-identical**, and it is
a sharper gate than it sounds: it holds even when every sphere in every scene is wrapped.

## Why it matters more than "add a matrix to the prim" sounds

1. **It is the last item that can pretend the scene is immutable.** `set_transform()` is the first
   mutator any part of the tracer has ever exposed. It is safe today only because the render is
   started and finished by the same code that builds the scene; the gateway that makes it safe in
   Hydra (`AcquireSceneForEdit()` calling `StopRender()` first, §6) belongs to
   `scene graph with mutation`. Getting the *shape* right now — one call site, one matrix, no
   geometry rebuild — is what keeps that item small.
2. **Instancing is this task plus a loop.** §14's Rprim "inserts the prototype into the scene once
   per transform". If the transform lives in a wrapper around a `shared_ptr` prototype, that
   sentence is literally `for (xf : transforms) world.add(make_shared<instance>(proto, xf))`, and
   nested instancers flatten by multiplying matrices. If the transform were baked into the
   geometry instead, every instance would be a full geometry copy.
3. **`DirtyTransform` arrives far more often than `DirtyPoints`.** A viewport drag of a parent
   Xform dirties the transform of every child. Re-placing a prim must cost one matrix inverse, not
   a re-upload of its points. That is what makes the wrapper the right shape rather than
   "transform the points at `Sync()` time".
4. **The normal transform is where reference implementations get it wrong.** hdEmbree transforms
   normals with `objectToWorldMatrix.TransformDir(normal)` (`hdEmbree/renderer.cpp:1079`). That is
   exact for rotations, translations and uniform scale, and up to **61.93°** wrong for a
   `scale(2, 0.5, 1.3)` — measured below. We do the inverse-transpose instead. The cost is one
   extra stored matrix; the alternative is ellipsoids that shade like spheres.

## What is explicitly NOT in this task

| Not now | Comes with |
|---|---|
| `HdWeekendMesh::Sync()` reading `GetTransform(id)`, `IsTransformDirty` gating | `hydra wrapper` (0.3.0) — Appendix A is the sketch |
| `AcquireSceneForEdit()` / `StopRender()` gateway around `set_transform()`, `sceneVersion` | `scene graph with mutation` |
| Prims addressable by `SdfPath`, removal, `Finalize()` | `scene graph with mutation` |
| `hit_info.prim_id` / `instance_id` / `element_id` for the §8.3 int32 AOVs | `scene graph with mutation` ([[roadmap-discussion-8-26]] §5 item 3) — the wrapper is where `instance_id` will be stamped |
| `HdInstancer` subclass, `hydra:instanceTransforms` primvars, nested flattening | 0.4.0 instancing — Appendix B records the composition order so it isn't re-derived |
| World-space bounds of a transformed prim (transform the object-space AABB's 8 corners) | `bvh with rebuild-on-mutation` — `object_to_world()` exists for it |
| Per-instance material overrides | nobody — §13, hdEmbree has no material support at all |
| Motion blur / time-sampled transforms (`SampleTransform`) | not scheduled |
| Any change to `camera.h`, `render_buffer.h`, `renderer.h`, `render_control.h` | nothing — step 4 proves they are untouched |

The tracer stays **USD-free**: no `pxr/` include under `tracer/` or `viewer/`. Steps 4–7 lean on
scratchpad programs — per the standing rule, **no test code lands in the repo**.

---

## Pre-verified facts

Measured, not assumed. Every program named below exists and was run; the numbers are its output.

```
S=/tmp/claude-1000/-home-nick-git-weekend-raytracer-docs/d69ccfbc-cf54-4d5a-adb4-eb8051fc229e/scratchpad
```

| Claim | Measured |
|---|---|
| **Wrapping changes no pixel.** All four scenes, at 50 spp, with *every* sphere wrapped in an identity `instance` | `cmp` **byte-identical** to the goldens, 4/4 |
| **Rebuilding prims as unit spheres placed by `scale(r) * translate(c)`** — same four scenes | `cmp` **byte-identical** to the goldens, 4/4 |
| `t` and `p` survive an identity wrap **bit for bit** | 385 980/385 980 hits each |
| The normal does *not* survive bit for bit, by up to **105 ulp** (2.33e-14) | 26 263/385 980 bit-identical — the renormalization *corrects* the prototype's drift, see "Design notes" |
| `front_face` decided in object space is correct in world space | 154 725/154 725 hits under `scale * rotate * translate` — see the note in step 5 on how to check this without the test collapsing |
| Hit point agrees both ways: `r.at(t)` vs `M * p_object` | ≤ **1.6e-15** over 154 725 hits |
| The object-space hit really is on the prototype | `abs(length(p_object) - 1)` ≤ **2.64e-13** |
| Normals match the closed-form ellipsoid normal under `scale(2, 0.5, 1.3)` | max `abs(n - analytic)` = **8.71e-16** over 160 027 hits |
| A mirror transform (`scale(-1,1,1)`, negative determinant) keeps normal orientation | 76 866/76 866 |
| Nested instances == one composed matrix | 35 622 agree, **0** disagree; `dt` ≤ 2.12e-13, `dp` ≤ 2.19e-12, `dn` ≤ 4.52e-12 |
| Singular transforms (`scale(1,0,1)`, `scale(0,0,0)`, all-zero) never hit and never produce a NaN | **0** hits out of 20 000 rays each; `is_finite(inverse(M))` false for all three |
| **`translate` / `scale` / `transpose` / composition are element-for-element identical to `GfMatrix4d`** | exact, 0 differing elements |
| `inverse` agrees with `GfMatrix4d::GetInverse` but is **not** bit-exact | max **1.42e-14** over 20 000 random `S*R*T`; bit-exact only 1412/20 000. Our residual `abs(M*inv(M) - I)` = **8.88e-15** vs USD's **1.07e-14**, i.e. ours is slightly *more* accurate |
| `rotate` differs from `GfMatrix4d::SetRotate` only in the last bits | max **4.44e-16** over four axis/angle pairs (USD routes through a quaternion) |
| `GfMatrix4d` → `mat4` is a straight `memcpy` | both `sizeof` 128, **0** mismatched elements, `Transform` result exact |
| **hdEmbree's normal transform is wrong under non-uniform scale** | `scale(2,0.5,1.3)`: max **61.93°**, mean 43.75°. `scale(100,0.01,100)`: max **89.99°**. Rigid and uniform-scale: 0.00° |
| **Per-prim wrapping is expensive under a linear scan** | scene 0, 484 prims: plain **0.067–0.075** ms/px vs all-wrapped **0.182–0.193** ms/px — **2.6×** |
| **Wrapping only what needs it is free** | 4 of 484 wrapped: **0.072–0.077** ms/px (+5%) |
| **One instance around a 480-prim prototype is free** | **0.075–0.077** ms/px (+3%), and byte-identical |
| Storing 1, 2 or 3 matrices per instance is indistinguishable | 10 spp interleaved: 0.0350/0.0377/0.0392 vs 0.0368/0.0386/0.0394 vs 0.0355/0.0364/0.0370 ms/px |
| `sizeof` | `sphere` 56 B, `instance` 416 B, `mat4` 128 B |
| The showcase scene (206 prims, 200 of them instances of one prototype) is deterministic across threaded runs | two runs byte-identical, md5 `297533cce4fcb5d116e82b2322a6308d`, 0 non-finite pixels, 0 out-of-range depth values |

Three of those decide the shape of the task.

**The byte-identity gate is real and it is strict.** Wrapping a sphere perturbs its normal by up to
105 ulp, that normal seeds the diffuse scatter direction, and *still* nothing moves in the final
8-bit image at 50 spp — verified on all four scenes, through two different wrapping strategies,
including the orthographic scene 3. So **any pixel difference in step 4 is a bug you introduced**,
not float noise. Do not weaken this gate to "visually identical".

**The 2.6× is real, and it is not the transform math.** Storing one matrix instead of three
changes nothing measurable, so the cost is the per-prim-test work: an extra virtual call, a pointer
chase, and two matrix multiplies *per prim tested per ray*. `hittable_list::hit` tests all 484
prims on every ray, so wrapping all 484 pays it 484 times. Wrap 4 and it costs 5%. Put 480 prims
behind **one** instance and it costs 3% — because the ray is transformed once for the whole
prototype. That last number is the one that matters: after `triangle mesh`, one instance wraps one
mesh of thousands of triangles, and after `bvh` only a handful of prims are tested per ray at all.
The rule that follows is in "Design notes": **wrap once per prim, never once per primitive**.

**hdEmbree is not the reference for the normal transform.** It is the reference for almost
everything else in this project, and here it is simply wrong — 62° wrong on a plausible ellipsoid.
Copying `objectToWorldMatrix.TransformDir(normal)` would make every non-uniformly-scaled prim in
every USD asset shade incorrectly.

Build lines that work on this machine:

```bash
cd ~/git/weekend-raytracer
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release

# tracer-only scratchpad program (with the tbb scheduler)
g++ -std=c++17 -O3 -DNDEBUG -I$S -Itracer -Ibuild/_deps/tbb-src/include \
    -o $S/all_scenes $S/all_scenes.cpp -Lbuild/gnu_13.3_cxx11_64_release -ltbb

# no scheduler needed for the math gates
g++ -std=c++17 -O2 -I$S -Itracer -o $S/t_math $S/t_math.cpp

# USD-linked: -ltbb here is USD's TBB 2020, not the vendored oneTBB
source env.sh
g++ -std=c++17 -O2 -Wno-deprecated -I$S -Itracer \
    -I$USD_ROOT/include -I/usr/include/python3.12 \
    -o $S/t_usd $S/t_usd.cpp -L$USD_ROOT/lib -lusd_gf -lusd_tf -ltbb -lpython3.12
```

---

## The design in one page

```
tracer/mat4.h            EDIT     + transpose(), is_finite(), translate(), scale(), rotate().
                                  Author-side builders, like look_at/perspective already there.

tracer/instance.h        NEW      class instance : public hittable. Prototype + object-to-world
                                  matrix. Transforms the ray in, the hit out. ~60 lines.

tracer/example_scenes.h  EDIT     scene_5: ellipsoids, an extreme scale, a singular prim, and
                                  200 instances of one prototype. load_scene case 4.

viewer/main.cpp          EDIT     one line: take the scene index from argv.

tracer/camera.h          UNTOUCHED
tracer/renderer.h        UNTOUCHED
tracer/render_buffer.h   UNTOUCHED
tracer/sphere.h          UNTOUCHED   (it stays center+radius; see "Design notes")
tracer/hittable.h        UNTOUCHED   (hit_info gains ids with `scene graph with mutation`)
```

### The four invariants

Everything the wrapper does follows from wanting these to hold, so that no caller —
`hittable_list`, `renderer::raycast`, any `material` — needs to know a transform was involved.

| Invariant | How | Verified |
|---|---|---|
| **`t` means the same thing inside and outside.** `world(o' + t·d') == o + t·d` | do **not** normalize the object-space direction | `dt` ≤ 2.1e-13 nested-vs-flat; `t` bit-identical under identity |
| **`p` is in world space** | `info.p = r.at(info.t)` using the *world* ray — cheaper than `xform.transform(p_object)` and agrees with it to 1.6e-15 | ≤ 1.6e-15 |
| **`normal` is a unit world-space normal** | `unit_vector(inv_t.transform_dir(n))`, i.e. `n · M⁻ᵀ` | 8.71e-16 vs the analytic ellipsoid normal |
| **`front_face` is unchanged** | nothing to do: `dot(d·M, n·M⁻ᵀ) == dot(d, n)` for any invertible `M`, so the sign the prototype saw is the sign the world ray sees | 154 725/154 725, including a negative-determinant mirror |

Because `t` is preserved, the `interval` passes through untouched: the clipping range, the 0.001
shadow-acne epsilon in `renderer::raycast`, and `hittable_list`'s `closest` comparison all stay in
**world** units regardless of the prim's scale. An implementation that normalized the object-space
direction would have to convert `t` at every boundary, and its acne epsilon would silently become
`0.001 / scale` per prim.

### Who owns what, after this task

| Decision | Owner |
|---|---|
| A prim's object-to-world matrix | the caller — Hydra's `sceneDelegate->GetTransform(id)` |
| Building matrices from lookfrom/angles/vectors | `camera_desc.h` and `example_scenes.h` only. The delegate **never** constructs a matrix |
| Composing parent transforms | nobody — `GetTransform` already returns them flattened |
| Inverting, transposing, and detecting degeneracy | `instance`, once, at construction |
| Whether a prim is wrapped at all | the caller — see the wrapping rule below |
| When it is safe to call `set_transform()` | the caller; the enforcing gateway is `scene graph with mutation` |

### Why the ray, and not the geometry

The alternative is to transform the geometry at `Sync()` time — for a sphere, push `center` and
scale `radius`; for a mesh, transform every point into world space. It is cheaper per ray (zero
overhead) and it is what a renderer with only rigid transforms would do. It fails on three counts:

- **It cannot represent a non-uniform scale at all** for an implicit sphere. `scale(2, 0.5, 1.3)`
  applied to a sphere is an ellipsoid, and no `(center, radius)` describes one.
- **It makes instancing O(copies × geometry)** instead of O(copies), which is the opposite of what
  §14 asks for.
- **It makes `DirtyTransform` as expensive as `DirtyPoints`**, and transforms dirty far more often.

Embree makes the same choice (`RTC_GEOMETRY_TYPE_INSTANCE` + `rtcSetGeometryTransform`), which is
why hdEmbree's mesh keeps object-space points and a separate `_transform`.

---

# Step 0 — Capture goldens before you touch anything

The gate for the existing scenes is byte equality, so the goldens must come from the current
build.

```bash
cd ~/git/weekend-raytracer
cmake --build build --config Release
export LD_LIBRARY_PATH=$PWD/build/gnu_13.3_cxx11_64_release
S=/tmp/claude-1000/.../scratchpad          # your scratchpad

for i in 0 1 2 3; do ./build/tracer/Release/tracer_cli $i > $S/gold_$i.ppm; done
md5sum $S/gold_*.ppm | tee $S/gold.md5
```

Measured here — unchanged since [[interruptible-render-loop]] step 0, which is itself worth
knowing:

```
3292e039125ee04d7f4728ad9d89886f  gold_0.ppm
81978695472eb949e987e46fefe3e694  gold_1.ppm
418151b864772683d18aef594a1651b7  gold_2.ppm
57e57b71e5501b5f278b60a73793b64c  gold_3.ppm
```

Also take a warm timing baseline for step 7 — run scene 0 **four times** and keep the last three
(the first run on this machine is ~10% faster than the fifth; interleave old/new when comparing):

```bash
for i in 1 2 3 4; do ./build/tracer/Release/tracer_cli 0 >/dev/null; done
# here: 0.067 / 0.072 / 0.075 ms/px
```

---

# Step 1 — Add the matrix helpers to `tracer/mat4.h`

Two helpers `instance` needs, and three author-side builders the scenes need. Append them to the
"Matrix builders" section, after `orthographic()`.

```cpp
inline mat4 transpose(const mat4 &x)
{
  mat4 r{};
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      r.m[i][j] = x.m[j][i];
    }
  }
  return r;
}

// A singular matrix's adjugate inverse divides by a zero determinant, so the
// result is full of inf/nan rather than being merely inaccurate. That is the
// degeneracy test: cheaper than a second determinant pass, and it also catches
// a matrix that was already non-finite when it arrived.
inline bool is_finite(const mat4 &x)
{
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      if (!std::isfinite(x.m[i][j])) return false;
    }
  }
  return true;
}

// Object-to-world builders. ROW-VECTOR convention, so v * (S * R * T) applies
// the scale first, then the rotation, then the translation - read left to
// right. Identical to GfMatrix4d::SetTranslate / SetScale / SetRotate.
inline mat4 translate(const vec3 &t)
{
  mat4 r = mat4::identity();
  r.m[3][0] = t[0];
  r.m[3][1] = t[1];
  r.m[3][2] = t[2];
  return r;
}

inline mat4 scale(const vec3 &s)
{
  mat4 r = mat4::identity();
  r.m[0][0] = s[0];
  r.m[1][1] = s[1];
  r.m[2][2] = s[2];
  return r;
}

inline mat4 scale(double s) { return scale(vec3(s, s, s)); }

// Right-handed rotation of `degrees` about `axis`. Rodrigues, transposed for
// the row-vector convention. Agrees with GfMatrix4d::SetRotate(GfRotation(...))
// to 4.4e-16 - USD routes through a quaternion, so this is not bit-exact.
inline mat4 rotate(const vec3 &axis, double degrees)
{
  const vec3 a = unit_vector(axis);
  const double rad = degrees_to_radians(degrees);
  const double c = std::cos(rad), s = std::sin(rad), t = 1 - c;

  mat4 r = mat4::identity();
  r.m[0][0] = t * a.x() * a.x() + c;
  r.m[0][1] = t * a.x() * a.y() + s * a.z();
  r.m[0][2] = t * a.x() * a.z() - s * a.y();
  r.m[1][0] = t * a.x() * a.y() - s * a.z();
  r.m[1][1] = t * a.y() * a.y() + c;
  r.m[1][2] = t * a.y() * a.z() + s * a.x();
  r.m[2][0] = t * a.x() * a.z() + s * a.y();
  r.m[2][1] = t * a.y() * a.z() - s * a.x();
  r.m[2][2] = t * a.z() * a.z() + c;
  return r;
}
```

Also fix the section comment above `look_at`, which is about to become false:

```
-// Matrix builders. Only camera_desc.h and tests call these - the Hydra delegate
-// receives matrices already built and must never construct its own.
+// Matrix builders. Only camera_desc.h, example_scenes.h and tests call these -
+// the Hydra delegate receives matrices already built (GetWorldToViewMatrix,
+// GetProjectionMatrix, GetTransform) and must never construct its own.
```

`<cmath>` is already included, so `std::isfinite` needs nothing new.

**Gate — compare every one of them against USD.** `$S/t_usd.cpp` builds the same matrices with
`GfMatrix4d` and diffs element by element. Expected output, exactly as measured:

```
translate      exact=1  maxdiff=0
scale          exact=1  maxdiff=0
rotate a=(0.00,0.00,1.00) 30.0deg  exact=1  maxdiff=0
rotate a=(0.00,1.00,0.00) 90.0deg  exact=0  maxdiff=1.61e-16
rotate a=(1.00,0.00,0.00) -45.0deg  exact=0  maxdiff=1.11e-16
rotate a=(0.30,-0.70,0.64) 137.5deg  exact=0  maxdiff=4.44e-16
compose S*R*T  exact=1  maxdiff=0
Transform      diff=0
transpose      exact=1  maxdiff=0
inverse        exact=0  maxdiff=8.88e-16
normal matrix  exact=0  maxdiff=8.88e-16
zero-scale     usd_det=0 ours_finite_inverse=0
```

The two rows to actually read: **`compose S*R*T exact=1`** pins the multiplication order — if this
fails, every scene built with these helpers is mirrored or sheared in a way that looks almost
plausible. And **`zero-scale ours_finite_inverse=0`** is what step 6's degeneracy guard rests on.

Two rows that are *not* expected to be exact, and must not be "fixed":

- **`rotate`** goes through a quaternion in USD and Rodrigues here, so it lands within 4.44e-16.
- **`inverse`** is an adjugate here and Gauss-Jordan with pivoting in USD — two different
  algorithms, so bit-equality was never on the table. `mat4.h`'s own comment already records
  1.8e-15 against `GetInverse`. Measured over 20 000 random `S*R*T` matrices: bit-exact
  1412/20 000, max element diff 1.42e-14, and our residual `abs(M*inv(M) - I)` is **8.88e-15**
  against USD's **1.07e-14** — ours is the marginally better inverse. `normal matrix`
  (`transpose(inverse(M))`) inherits the same tolerance. Gate these on magnitude, not `exact=1`.

Both comparisons only mean anything when **both sides are handed the identical input matrix**.
`mat4` and `GfMatrix4d` are the same 128-byte row-major layout, so `memcpy` the composed `mat4`
into a `GfMatrix4d` and compare the *operations*. Building each side from its own `rotate` instead
re-tests `rotate` and reports a spurious `exact=0` for `compose`, `transpose` and `inverse` alike.

---

# Step 2 — Write `tracer/instance.h`

The whole file:

```cpp
#pragma once

#include <utility>

#include "hittable.h"
#include "mat4.h"
#include "ray.h"
#include "tracer.h"

// A prototype hittable placed into the world by a 4x4 object-to-world matrix -
// hydra's sceneDelegate->GetTransform(id), which already includes every parent
// transform. The RAY is transformed into object space rather than the geometry
// into world space, so one prototype can be placed many times (spec 14) and a
// DirtyTransform costs one matrix inverse instead of a geometry rebuild.
class instance : public hittable
{
public:
  instance(shared_ptr<hittable> prototype, const mat4 &object_to_world)
      : proto(std::move(prototype))
  {
    set_transform(object_to_world);
  }

  // Only safe while no render is in flight. The gateway that will guarantee
  // that - AcquireSceneForEdit() calling StopRender() first - belongs to
  // `scene graph with mutation`; until then the caller builds the scene and
  // starts the render itself.
  void set_transform(const mat4 &object_to_world)
  {
    xform = object_to_world;
    inv = inverse(object_to_world);
    inv_t = transpose(inv);
    valid = is_finite(inv);
  }

  // For the BVH's world-space bounds, and for composing nested instancers.
  // hit() never reads it.
  const mat4 &object_to_world() const { return xform; }

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    // A singular transform - any zero scale - collapses the prim to nothing.
    if (!valid) return false;

    // world -> object. The direction is deliberately NOT normalized: keeping
    // its length makes object-space t identical to world-space t, so the
    // clipping range, the shadow-acne epsilon and hittable_list's closest-hit
    // comparison all stay in world units and need no conversion.
    const ray local(inv.transform(r.origin()), inv.transform_dir(r.direction()));
    if (!proto->hit(local, clipping_range, info)) return false;

    // object -> world. t and front_face pass through untouched: dot(d, n) is
    // invariant under (M, M^-T), so the face the prototype picked is the face
    // the world-space ray sees.
    info.p = r.at(info.t);
    info.normal = unit_vector(inv_t.transform_dir(info.normal));
    return true;
  }

private:
  shared_ptr<hittable> proto;
  mat4 xform = mat4::identity();
  mat4 inv = mat4::identity();
  mat4 inv_t = mat4::identity();
  bool valid = true;
};
```

Five details that are easy to get wrong, all of them measured in step 5:

- **Do not normalize `local`'s direction.** It is the single most tempting change in this file and
  it silently rescales `t` by the prim's scale factor.
- **`info.p` comes from the *world* ray**, not from transforming the object-space point. Both are
  correct (they agree to 1.6e-15); `r.at(t)` is two multiplies instead of a matrix multiply.
- **`inv_t`, not `xform`, transforms the normal.** `xform.transform_dir` is hdEmbree's bug: exact
  for similarity transforms, up to 62° wrong otherwise.
- **Renormalize the normal.** A non-uniform scale does not preserve length, and materials assume a
  unit normal (`metal`'s `reflect`, `glass`'s `cos_theta`).
- **Do not touch `info.t`, `info.front_face` or `info.mat`.** The prototype set all three
  correctly; `mat` in particular means instances share a material, which is what §13-era Hydra
  wants anyway.

`hit()` reads only immutable state, so instances are safe under the tile threads exactly as
spheres are.

---

# Step 3 — A scene that exercises it

`tracer/example_scenes.h`: add the include, add `scene_5`, add the `load_scene` case.

```cpp
#include "instance.h"
```

```cpp
// Transform showcase: every prim here is one unit sphere prototype placed by a
// matrix. Ellipsoids come from non-uniform scale, the ground slab from an
// extreme scale, and the 200 pebbles are 200 instances of ONE prototype -
// the shape hydra-spec 14 instancing takes.
void scene_5(hittable_list &world, camera_desc &desc)
{
  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_a = make_shared<lambert>(color(0.8, 0.3, 0.2));
  auto m_b = make_shared<metal>(color(0.8, 0.8, 0.9), 0.02);
  auto m_c = make_shared<glass>(color(1, 1, 1), 1.5);

  auto unit = make_shared<sphere>(point3(0, 0, 0), 1.0, m_a);
  auto unit_b = make_shared<sphere>(point3(0, 0, 0), 1.0, m_b);
  auto unit_c = make_shared<sphere>(point3(0, 0, 0), 1.0, m_c);

  world.add(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));
  world.add(make_shared<instance>(unit,
      scale(vec3(1.2, 0.35, 0.6)) * rotate(vec3(0, 0, 1), 30) * translate(vec3(-1.3, 0, -1.2))));
  world.add(make_shared<instance>(unit_b,
      scale(vec3(0.5, 1.4, 0.5)) * rotate(vec3(1, 0, 0), -25) * translate(vec3(0.3, 0.2, -1.6))));
  world.add(make_shared<instance>(unit_c,
      scale(vec3(0.6, 0.6, 0.6)) * translate(vec3(1.4, 0.1, -1.1))));
  world.add(make_shared<instance>(unit,
      scale(vec3(100, 0.01, 100)) * translate(vec3(0, -0.5, -1))));   // extreme scale
  world.add(make_shared<instance>(unit,
      scale(vec3(1, 0, 1)) * translate(vec3(0, 0.8, -1))));            // singular: must vanish

  rng gen(7);
  for (int i = 0; i < 200; i++) {
    world.add(make_shared<instance>(unit_b,
        scale(0.06) * rotate(vec3(0, 1, 0), gen.uniform(0, 360))
        * translate(vec3(gen.uniform(-3, 3), gen.uniform(-0.4, 0.6), gen.uniform(-4, -0.6)))));
  }

  desc.lookfrom = vec3(0, 0.6, 1.2);
  desc.lookat = vec3(0, 0, -1.2);
  desc.v_fov = 60;
  desc.focus_dist = 2.2;
  desc.defocus_angle = 0;
}
```

```cpp
  case 4:
    scene_5(world, camera);
    break;
```

And one line in `viewer/main.cpp`, so the showcase can be looked at interactively instead of only
as a PPM:

```cpp
-int main()
+int main(int argc, char *argv[])
 {
   ...
-  load_scene(1, world, desc);
+  load_scene(argc > 1 ? atoi(argv[1]) : 1, world, desc);
```

Three things in that scene are there on purpose, not for looks: the **singular prim** (must render
as nothing, not as a NaN), the **extreme scale** (`0.01` in y — the pancake that makes hdEmbree's
normal transform 90° wrong), and the **200 shared-prototype instances** (the §14 shape, and the
measurement that per-instance cost is affordable when the prototype is shared).

---

# Step 4 — Gate: the existing scenes do not move, even when wrapped

Three renders per scene, all of which must produce the step 0 goldens byte for byte.

**4a — the plain scenes still match.** Nothing wrapped; this catches an accidental edit to
`mat4.h`'s existing builders.

```bash
cmake --build build --config Release
for i in 0 1 2 3; do ./build/tracer/Release/tracer_cli $i > $S/new_$i.ppm; done
for i in 0 1 2 3; do cmp $S/gold_$i.ppm $S/new_$i.ppm && echo "scene $i ok"; done
```

**4b — every sphere wrapped in an identity instance.** Do this in the scratchpad, not the repo:
generate a wrapped copy of `example_scenes.h` mechanically, so the scene definitions cannot drift.

```python
# writes $S/wrapped_scenes.h: example_scenes.h with every sphere routed through place()
import re
src = open("tracer/example_scenes.h").read()
src = re.sub(r'make_shared<sphere>\((.*?)\)\)', r'place(g_wrap, \1))', src)
open(S + "/wrapped_scenes.h", "w").write(PRELUDE + src.replace("#pragma once", ""))
```

where `PRELUDE` is the header that defines the three placement strategies:

```cpp
enum wrap_mode { plain = 0, identity_wrap = 1, xform_build = 2 };
inline wrap_mode g_wrap = plain;

inline shared_ptr<hittable> place(wrap_mode w, const point3 &c, double r, shared_ptr<material> m)
{
  switch (w) {
    case identity_wrap:
      return make_shared<instance>(make_shared<sphere>(c, r, m), mat4::identity());
    case xform_build:   // unit sphere at the origin, placed entirely by matrix
      return make_shared<instance>(make_shared<sphere>(point3(0,0,0), 1.0, m),
                                   scale(r) * translate(c));
    default:
      return make_shared<sphere>(c, r, m);
  }
}
```

`$S/all_scenes.cpp` is then `main.cpp` with `plan_scenes.h` swapped in, `g_wrap` set from `argv[2]`
and the PPM written with the same `write_color` format.

**4c — every sphere rebuilt as a unit sphere placed by a matrix** (`xform_build`). This is the
strong one: it exercises translation *and* scale on every prim in every scene, including the
orthographic camera in scene 3 and the depth AOV.

Measured — all twelve combinations:

```
scene 0 wrap 0: byte-identical to gold      scene 2 wrap 0: byte-identical to gold
scene 0 wrap 1: byte-identical to gold      scene 2 wrap 1: byte-identical to gold
scene 0 wrap 2: byte-identical to gold      scene 2 wrap 2: byte-identical to gold
scene 1 wrap 0: byte-identical to gold      scene 3 wrap 0: byte-identical to gold
scene 1 wrap 1: byte-identical to gold      scene 3 wrap 1: byte-identical to gold
scene 1 wrap 2: byte-identical to gold      scene 3 wrap 2: byte-identical to gold
```

If 4b passes and 4c fails, the bug is in the scale path — almost certainly a normalized direction
(which makes `t`, and therefore the hit point, wrong by the scale factor) or `xform` used where
`inv_t` belongs.

And prove the untouched files are untouched:

```bash
git diff --stat tracer/camera.h tracer/renderer.h tracer/render_buffer.h \
                tracer/render_control.h tracer/sphere.h tracer/hittable.h   # must be empty
grep -rn "#include.*pxr" tracer/ viewer/                                    # must be empty
```

---

# Step 5 — Gate: the four invariants, numerically

`$S/t_math.cpp` fires a few hundred thousand random rays at a wrapped unit sphere and checks each
invariant against an independent computation — the analytic ellipsoid normal, the composed matrix,
the world-space dot product. Expected output, as measured:

```
general xform: hits=154725  |p_obj|-1 <= 2.64e-13  p vs M*p_obj <= 1.6e-15
               |n|-1 <= 3.33e-16  n vs inverse-transpose ref <= 2.43e-14
               front_face == object-space decision: 154725/154725
               world normal opposes world ray:     154725/154725
ellipsoid (scale 2,0.5,1.3): hits=160027  max |n - analytic| = 8.71e-16
mirror (scale -1,1,1): hits=76866  surface err <= 3.04e-14  normal orientation ok 76866/76866
nested vs composed: agree=35622 disagree=0  dt<=2.12e-13 dp<=2.19e-12 dn<=4.52e-12
singular[0]: hits=0 (want 0)  inverse_finite=0
singular[1]: hits=0 (want 0)  inverse_finite=0
singular[2]: hits=0 (want 0)  inverse_finite=0
```

The five checks, and what a failure means:

| Check | Failure means |
|---|---|
| `abs(length(p_object) - 1)` — is the object-space hit on the prototype? | the ray transform is wrong (translation applied to a direction, or the inverse transposed) |
| `p` vs `M * p_object` | `info.p` was left in object space, or `t` was rescaled |
| `n` vs the **closed-form ellipsoid normal** `(x/a², y/b², z/c²)` | the normal matrix is wrong. This is the independent check — it never touches `inv_t` |
| `front_face` vs the **object-space** `dot(d_obj, p_obj) < 0`, recomputed independently | something re-derived the face after transforming, or the normal sign was lost |
| the world normal still opposes the world ray, `dot(d, n) < 0` | the `dot(d·M, n·M⁻ᵀ) == dot(d, n)` sign invariance broke — `xform` was used where `inv_t` belongs |
| nested instance vs one composed matrix | the composition order is reversed (see step 1's `compose S*R*T`) |

The ellipsoid check is worth writing even though it looks redundant: it is the only assertion in
the whole task that does not use the same matrix code it is testing.

**Do not write the `front_face` check as `info.front_face == (dot(r.direction(), info.normal) < 0)`.**
`set_face_normal` always flips `normal` to oppose the ray, so `dot(d, normal) < 0` is
*unconditionally* true and that comparison silently degenerates into "`front_face` is true" — it
then fails on every ray that starts inside the prim, and the failure looks like a transform bug.
It is two separate claims and they need two separate counters:

1. `front_face` equals the object-space decision, recomputed independently from the world ray:
   `dot(d_obj, p_obj) < 0`, where `p_obj = inv.transform(o) + t * inv.transform_dir(d)`.
2. The world normal still opposes the world ray. This is the sign-invariance claim, and it is the
   one that would actually catch `xform` used in place of `inv_t`.

Both must be 100%.

Then the identity-wrap field breakdown (`$S/t_identity.cpp`):

```
identity instance vs bare sphere over 385980 hits:
  t bit-identical      385980/385980
  p bit-identical      385980/385980
  front_face equal     385980/385980
  normal bit-identical 26263/385980   max |dn| = 2.33e-14 (104.9 ulp)
```

`t` and `p` bit-identical is not luck — multiplying by an identity matrix is `x*1 + y*0 + z*0 + 0`,
which is exact. The normal moves because `unit_vector` renormalizes, and it moves by 105 ulp
because the *prototype's* normal was that far off unit length to begin with (`(p - center)/radius`
after a grazing-ray root). The wrapper is more accurate than the thing it wraps.

---

# Step 6 — Gate: degenerate and extreme transforms in a real render

Render scene 4 and check the whole buffer, not just the picture:

```bash
./build/tracer/Release/tracer_cli 4 > $S/scene4.ppm
md5sum $S/scene4.ppm
# measured here: 297533cce4fcb5d116e82b2322a6308d
```

That md5 is only meaningful if `scene_5` was copied verbatim from step 3 — the `rng gen(7)` stream
has to be consumed in the same order. If it differs, diff the scene code before suspecting
`instance`; then fall back to the checks that do not depend on the exact scene:

- **no non-finite pixel and no out-of-range depth.** Bind color and depth AOVs, resolve, and sweep:
  `isfinite(rgba[c])` for all four channels and `0 <= d <= 1`. Measured: `nonfinite=0 depth_bad=0`.
  The singular prim and the `0.01`-thin pancake are what make this a real test — a missing `valid`
  guard shows up here as inf, and it will *not* show up as a crash.
- **the singular prim is invisible.** There is nothing at `(0, 0.8, -1)` in the image.
- **determinism.** Render twice; the two PPMs must be byte-identical. `instance::hit` is `const`
  and holds no scratch state, so a difference here means shared mutable state crept in.

Then look at it. The showcase renders as a rotated flattened ellipsoid on the left, a tall rotated
metal ellipsoid centre, a glass sphere right, the pancake as a floor slab, and 200 metal pebbles —
with highlights that follow each ellipsoid's actual curvature. Normals that were transformed by
`xform` instead of `inv_t` produce an image that is *lit wrong but not obviously broken*: the
ellipsoids read as spheres squashed by a bad shader. That is exactly why step 5's analytic check
exists, and why "it looks fine" is not the gate.

```bash
./build/viewer/Release/viewer 4
```

---

# Step 7 — Gate: performance, and the wrapping rule it implies

Interleaved warm runs of scene 0 at 400×225, 50 spp, `tbb_schedule`, 12 threads. Four
configurations, all producing byte-identical images:

| Configuration | Prims tested per ray | Measured (two rounds) |
|---|---|---|
| plain — no instances | 484 spheres | **0.0672 / 0.0754** ms/px |
| 4 hero prims wrapped, 480 plain | 484 | **0.0725 / 0.0771** ms/px (+5%) |
| 480 small prims inside **one** instance | 5 top-level, 480 behind one transform | **0.0746 / 0.0769** ms/px (+3%) |
| all 484 wrapped individually | 484, each with two matrix multiplies | **0.1861 / 0.1822** ms/px (**2.6×**) |

Run them interleaved and warm — the first run after a cold start is ~10% faster here (0.0664 vs
0.0823 ms/px in [[interruptible-render-loop]]), which will invent or hide a regression.

The rule that falls out, and it belongs in the delegate:

> **Wrap once per prim, never once per primitive.** An `instance` earns its cost when its
> prototype is a whole mesh, or when the same prototype is placed many times. Wrapping 484
> individual spheres pays the ray transform 484 times per ray for nothing.

Two corollaries for 0.3.0: the delegate should **skip the wrapper when `GetTransform` returns
identity** (a cheap element comparison at `Sync()` time), and it should never wrap below the Rprim
— one mesh, one instance, thousands of triangles behind it. The `bvh` item removes most of what is
left, since a ray then tests a handful of prims instead of all of them.

The layout question is settled by measurement too: storing one matrix (inverse only, normal via a
transposed multiply), two (inverse + its transpose), or three (plus `object_to_world`) are
indistinguishable at 10 spp — 0.0350/0.0377/0.0392 vs 0.0368/0.0386/0.0394 vs
0.0355/0.0364/0.0370 ms/px. Keep all three: `object_to_world()` is what the BVH needs for world
bounds and what nested instancers compose.

---

## Definition of done

- [ ] `cmp` says scenes 0–3 are **byte-identical** to the step 0 goldens
- [ ] All four scenes are byte-identical with **every sphere wrapped in an identity instance**
- [ ] All four scenes are byte-identical with **every sphere rebuilt as a unit sphere placed by
      `scale(r) * translate(c)`**
- [ ] `$S/t_usd` reports `exact=1` for translate, scale, transpose and `compose S*R*T`, with both
      sides fed the identical input matrix; `rotate` within 5e-16 and `inverse` within 5e-15
      (`inverse` is a different algorithm from USD's and is not bit-exact — see step 1)
- [ ] `$S/t_usd` reports `ours_finite_inverse=0` for a zero-scale matrix
- [ ] `$S/t_math` reproduces all five invariant blocks, including both `front_face` counters
      agreeing on every hit and `singular[*] hits=0`
- [ ] `$S/t_identity` shows `t` and `p` bit-identical on every hit
- [ ] Scene 4 renders with `nonfinite=0 depth_bad=0`, the singular prim invisible, and the same
      bytes twice in a row
- [ ] Scene 4's md5 is `297533cce4fcb5d116e82b2322a6308d` (or the scene code was intentionally
      changed and the new md5 is recorded here)
- [ ] Scene 0 with 4 of 484 prims wrapped is within ~5% of the plain baseline, warm and interleaved
- [ ] `git diff --stat` is empty for `camera.h`, `renderer.h`, `render_buffer.h`,
      `render_control.h`, `sphere.h`, `hittable.h`
- [ ] `grep -rn "#include.*pxr" tracer/ viewer/` is empty — the tracer is still USD-free.
      (Grep for the *include*, not for `pxr/`: `render_buffer.h:313` cites
      `pxr/imaging/hd/tokens.h` in a prose comment, so the looser grep never comes back empty)
- [ ] `build-hydra` still builds `hdWeekend` and `testHdWeekend` (still vacuous — `hydra/`
      includes no tracer header until 0.3.0)
- [ ] No test file and no test CMake target added to the repo

---

## Design notes — decisions made, recorded so they aren't re-litigated

**The ray moves, not the geometry.** Fixed by §14 and by non-uniform scale on implicit surfaces;
argued in full in "Why the ray, and not the geometry". Embree, hdEmbree and every production
instancing scheme make the same call.

**The object-space direction is not normalized.** This is the load-bearing decision in
`instance::hit`. Keeping `|d|` makes `t` a shared quantity between spaces, which in turn keeps the
`interval`, the 0.001 acne epsilon and `hittable_list`'s closest-hit comparison meaningful without
per-prim conversion. Normalizing would work only if `t` were rescaled on the way out, and the acne
epsilon would then be `0.001 / scale` per prim — visible as shadow acne on shrunken prims and
over-aggressive self-occlusion on enlarged ones.

**Normals use the inverse transpose, deliberately unlike hdEmbree.** `hdEmbree/renderer.cpp:1079`
uses `objectToWorldMatrix.TransformDir(normal)`, which is exact for similarity transforms and up
to 61.93° wrong for `scale(2, 0.5, 1.3)`, 89.99° for `scale(100, 0.01, 100)` (measured over
200 000 random normals each). hdEmbree gets away with it because its test assets are rigidly
placed; USD assets are not. Note that hdEmbree *does* use the correct construction for lights
(`light.cpp:133`, `ExtractRotationMatrix().GetTranspose()`), which suggests the mesh path is an
oversight rather than a considered trade.

**The normal is always renormalized.** A non-uniform scale does not preserve length, and materials
assume unit normals. The measured surprise is that under an identity transform the renormalization
*changes* the value by up to 105 ulp — because `sphere`'s own `(p - center)/radius` drifts that far
from unit length on grazing rays. So this is a correction, not just insurance, and the whole-image
gate holds anyway. A "skip normalization when the transform is rigid" fast path was considered and
dropped: it adds a flag and a branch to save one `sqrt` that does not appear in the measurements.

**`info.p = r.at(info.t)`, not `xform.transform(p_object)`.** Two multiplies instead of a matrix
multiply, and they agree to 1.6e-15 over 154 725 hits.

**`front_face` is not recomputed.** `dot(d·M, n·M⁻ᵀ) = dot(d, n)` identically, for any invertible
`M` including mirrors. Recomputing it would be wasted work that also invites a sign bug; the
mirror case (76 866/76 866) is the evidence that the identity holds where intuition wobbles.

**A singular transform makes the prim invisible.** A zero scale is legal in USD, and Hydra will
hand it over without comment. `is_finite(inverse(M))` is the test — the adjugate inverse divides by
the determinant, so a singular matrix yields inf/nan rather than a merely inaccurate result, and
the same check also rejects a matrix that arrived non-finite. It is computed once per
`set_transform`, never per ray. The alternative — a determinant epsilon — needs a magic threshold
and a second pass over the matrix.

**`sphere` keeps `center` and `radius`.** Forcing every prim to be a canonical unit prototype was
considered (it is what the `xform_build` gate simulates) and rejected: it makes every sphere pay a
matrix transform, and step 7 measures that at 2.6× on a 484-prim scene. `sphere(center, radius)`
stays the cheap path for prims that need no transform, and `sphere::hit` is already
direction-length-agnostic (`a = direction().length_sqr()`), which is what lets it serve as a
prototype unchanged. **The triangle intersector must keep that property** — Möller–Trumbore does
naturally; a variant that normalizes the direction would break the `t` invariant.

**Three matrices per instance, 416 bytes.** Measured indistinguishable from one or two.
`object_to_world()` is retained for the BVH's world-space bounds (transform the prototype's
object-space AABB corners) and for composing nested instancer transforms.

**`set_transform()` exists now, unguarded.** The shape is what matters: a transform update touches
one matrix and one inverse, never geometry. The stop-the-render gateway is `scene graph with
mutation`'s job, and `StopRender()` returning in 0.05–0.16 ms
([[interruptible-render-loop]] step 9) is what makes calling it per edit affordable.

**Row-vector composition, `S * R * T`.** Matches `GfMatrix4d` exactly (verified `exact=1`), so a
transform read from USD and a transform built by `example_scenes.h` compose the same way and can
be compared element-for-element in a debugger.

**No `instance_id` on `hit_info` yet.** §8.3's `primId`/`instanceId`/`elementId` AOVs need it, and
the wrapper is obviously where `instanceId` gets stamped, but `hit_info` is going to be edited
anyway for triangles and prim ids — doing it once, under `scene graph with mutation`, is cheaper
than doing it twice ([[roadmap-discussion-8-26]] §5 item 3).

---

## Appendix A — the delegate side, for 0.3.0

Not committed in this task. `hydra/mesh.cpp` already asks for `DirtyTransform` in
`GetInitialDirtyBitsMask()` and does nothing with it; this is what "does something" looks like.

```cpp
// hydra/mesh.cpp
static mat4 to_mat4(const GfMatrix4d &m)
{
  mat4 r{};
  std::memcpy(r.m, m.GetArray(), sizeof(r.m));   // verified: sizeof 128 both, 0 mismatches
  return r;
}

void HdWeekendMesh::Sync(HdSceneDelegate *sceneDelegate, HdRenderParam *renderParam,
                         HdDirtyBits *dirtyBits, TfToken const &reprToken)
{
  const SdfPath &id = GetId();

  if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
    const mat4 xf = to_mat4(sceneDelegate->GetTransform(id));
    // scene edits go through the gateway, which StopRender()s first
    auto scene = static_cast<HdWeekendRenderParam *>(renderParam)->AcquireSceneForEdit();
    _instance->set_transform(xf);
  }

  *dirtyBits &= ~HdChangeTracker::DirtyTransform;   // clear only what was consumed
}
```

Three facts that make this shorter than it could be:

- **`GetTransform` is already flattened.** `hd/sceneDelegate.h:468`: "the object space transform,
  **including all parent transforms**". There is no ancestor walk to write, and no parent-transform
  cache to invalidate.
- **`GfMatrix4d` → `mat4` is a `memcpy`.** Both are 128 bytes, row-major, translation in
  `[3][0..2]`. Verified element-for-element, and `Transform()` of a point agrees exactly
  afterwards. `mat4.h`'s claim to be layout-identical to `GfMatrix4d` now has a test behind it.
- **The delegate never builds a matrix.** It copies one. `translate`/`scale`/`rotate` exist for
  `example_scenes.h` and the cli only — same rule `camera_desc` already follows for
  `look_at`/`perspective`.

Skip the wrapper entirely when the transform is identity (step 7's rule): `instance` costs 2.6× on
a scene of many small individually-wrapped prims, and an untransformed prim should not pay it.

## Appendix B — what §14 instancing adds

Recorded now because the composition order is the part that gets re-derived wrongly. From
`hdEmbree`:

- **Per-instance transform** (`instancer.cpp:86-93`):
  `instancerTransform * instanceTranslations(i) * instanceRotations(i) * instanceScales(i) * instanceTransforms(i)`
- **Combined with the Rprim's own transform** (`mesh.cpp:904`):
  `matf = _transform * GfMatrix4f(transforms[i])`
- **Nested instancers flatten by cartesian product** (`instancer.cpp:186`):
  `final[i * n + j] = transforms[j] * parentTransforms[i]`

All three are row-vector multiplies in the same convention as our `mat4`, so they transcribe
directly. In the tracer that is:

```cpp
for (const mat4 &xf : instance_transforms) {
  world.add(make_shared<instance>(prototype, own_transform * xf));
}
```

with **one** `prototype` `shared_ptr` shared by every entry — which is what makes scene 4's 200
pebbles cost 206 prims' worth of memory instead of 200 copies of a sphere, and why the measured
cost of instancing a shared prototype (+3% when the prototype holds 480 prims) is the number that
matters rather than the +2.6× of wrapping everything individually.

§14's other requirements are unchanged by this task: `HdInstancer::Sync()`, primvar caching as
`HdVtBufferSource*`, re-syncing on `DirtyInstancer | DirtyTransform`, and calling parent
instancers' `Sync()` first.

---

## Verification log — 2026-08-27, after implementation

Every gate in steps 1 and 4–7 was re-run against the committed implementation, from a fresh
scratchpad with an independently written set of programs. Everything passed. Absolute hit counts
in steps 5 differ from the numbers above because the re-run uses a different ray generator; the
error bounds, the ratios and the pass/fail counts all reproduce.

**Two defects were found in the implementation and fixed. Both are places where the code drifted
from the snippets above, so the snippets stand as written:**

1. **`is_finite()` in `mat4.h` was transcribed with the sense of the test inverted** — the shipped
   code read `if (std::isfinite(x.m[i][j])) return false;`, dropping the `!` that step 1 has. It
   returns on the first *finite* element, so it answered false for every well-formed matrix and
   true only for one already full of inf/nan. `instance::set_transform` feeds it straight into
   `valid`, so **every instance was invalid and `hit()` returned early**: scene 4 rendered as the
   single bare ground sphere and nothing else. The same inversion also flipped the degeneracy
   guard the other way, so the singular `scale(1,0,1)` prim was the *only* thing that would have
   been traced — through an inf-filled inverse. An empty image was the lucky failure mode.
   Step 6's `nonfinite=0` sweep is what would have caught the unlucky one.
2. **`load_scene` case 4 had no `break`.** Harmless as the last case; a trap the moment a case 5
   is added.

Results that differ from what is recorded above:

| Gate | Recorded | Re-run |
|---|---|---|
| `t_usd` `inverse` / `normal matrix` | `exact=1` / `diff=0` | `exact=0`, maxdiff 8.88e-16 — see step 1; the plan's claim was unachievable |
| step 7, 4 hero prims wrapped | +5% | +3% (0.0694 vs 0.0674 ms/px) |
| step 7, 480 behind one instance | +3% | +1% (0.0684 vs 0.0674 ms/px) |
| step 7, all 484 wrapped | 2.6× | 2.6× (0.1756 vs 0.0674 ms/px) — reproduced exactly |
| step 0 warm baseline | 0.067 / 0.072 / 0.075 ms/px | 0.063–0.065 ms/px, so compare interleaved, never against these absolutes |

Everything else reproduced: all 12 step-4 combinations byte-identical to the goldens; scene 4 at
md5 `297533cce4fcb5d116e82b2322a6308d`; `nonfinite=0 depth_bad=0`; `sizeof` 56 / 416 / 128.

Two gates were strengthened over what is written above:

- **The singular prim is checked by removal, not by eye.** Rendering scene 4 with that prim
  deleted is byte-identical to rendering it with the prim present, which proves it contributes
  exactly zero rather than merely being hard to see.
- **Step 7's four configurations are built by re-parenting one loaded scene** rather than by four
  scene definitions, so the geometry is identical by construction and all four are byte-identical.

## Next up

`scene graph with mutation` — prims addressable by `SdfPath`, insert/update/**remove**, a monotonic
`sceneVersion`, and the `AcquireSceneForEdit()` gateway that calls `StopRender()` before handing
back a mutable scene pointer ([[hydra-spec]] §6, [[roadmap-discussion-8-26]] §6). This task hands
it two things: the first mutator in the codebase (`instance::set_transform`), which is precisely
what the gateway has to protect, and the knowledge that a transform edit is one matrix inverse —
so the expensive part of a viewport drag will be the `StopRender()`/restart, not the edit.
`hit_info`'s `prim_id` / `instance_id` / `element_id` fields belong to that item too, and the
wrapper written here is where `instance_id` gets stamped.
