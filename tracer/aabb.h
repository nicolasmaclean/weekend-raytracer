#pragma once

#include "tracer.h"
#include "vec3.h"


struct aabb
{
  point3 lo { infinity, infinity, infinity };
  point3 hi { -infinity, -infinity, -infinity };

  static aabb empty()
  {
    return aabb{};
  }

  static aabb infinite()
  {
    return aabb{
      point3(-infinity, -infinity, -infinity),
      point3(infinity, infinity, infinity)
    };
  }

  bool is_empty() const
  {
    return hi[0] < lo[0] || hi[1] < lo[1] || hi[2] < lo[2];
  }

  void expand(const point3 &p)
  {
    for (int a = 0; a < 3; a++)
    {
      lo[a] = std::min(lo[a], p[a]);
      hi[a] = std::max(hi[a], p[a]);
    }
  }

  void expand(const aabb &b)
  {
    for (int a = 0; a < 3; a++)
    {
      lo[a] = std::min(lo[a], b.lo[a]);
      hi[a] = std::max(hi[a], b.hi[a]);
    }
  }

  vec3 extent() const
  {
    if (is_empty()) return vec3();
    return hi-lo;
  }

  double surface_area() const
  {
    if (is_empty()) return 0;
    vec3 extent = hi-lo;
    return 2 * (extent[0]*extent[1] + extent[0]*extent[2] + extent[1]*extent[2]);
  }

  point3 centroid() const
  {
    if (is_empty())
    {
      return point3();
    }

    return 0.5 * (lo + hi);
  }
};

inline aabb merge(const aabb &a, const aabb &b)
{
  aabb merged = a;
  merged.expand(b);
  return merged;
}

struct slab_ray
{
  point3 o;
  vec3 inv;
  int neg[3];

  explicit slab_ray(const ray &r) : o(r.origin())
  {
    const vec3 &d = r.direction();
    for (int a = 0; a < 3; a++)
    {
      inv[a] = 1/d[a];
      neg[a] = inv[a] < 0;
    }
  }
};

inline bool slab_hit(const aabb &b, const slab_ray &s, double tmin, double tmax)
{
  for (int a = 0; a < 3; a++)
  {
    double t0 = (b.lo[a] - s.o[a]) * s.inv[a];
    double t1 = (b.hi[a] - s.o[a]) * s.inv[a];
    if (s.neg[a]) std::swap(t0, t1);

    if (t0 > tmin) tmin = t0;
    if (t1 < tmax) tmax = t1;

    if (tmax < tmin) return false;
  }

  return true;
}

