#pragma once

#include <memory>
#include <vector>

#include "hittable.h"
#include "vert.h"


class mesh : public hittable
{
public:
  std::vector<vec3> verts;
  std::vector<vec3> normals;
  std::vector<int32_t> tris;
  std::vector<int32_t> face;
  shared_ptr<material> mat;

  size_t triangle_count() const
  {
    return tris.size() / 3;
  }

  void add_triangle(const point3 &a, const point3 &b, const point3 &c)
  {
    const int32_t base = int32_t(verts.size());
    verts.push_back(a);
    verts.push_back(b);
    verts.push_back(c);
    tris.push_back(base);
    tris.push_back(base+1);
    tris.push_back(base+2);
  }

  void add_triangle(const vertex &a, const vertex &b, const vertex &c)
  {
    add_triangle(a.p, b.p, c.p);
    normals.push_back(a.n);
    normals.push_back(b.n);
    normals.push_back(c.n);
  }

  void commit() override
  {
    geom.clear();
    geom.reserve(triangle_count());

    for (size_t i = 0; i+2 < tris.size(); i += 3)
    {
      const vec3 &p0 = verts[tris[i]];
      const vec3 &p1 = verts[tris[i+1]];
      const vec3 &p2 = verts[tris[i+2]];
      geom.push_back({p0, p1-p0, p2-p0});
    }
  }

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    int32_t best = -1;
    double closest = clipping_range.max;
    double bu = 0, bv = 0;

    for (size_t f = 0; f < geom.size(); f++)
    {
      const tri_geom &g = geom[f];

      // Moller-Trumbore. The direction is NOT normalized, so `t` comes back in
      // the caller's units - the invariant `instance` and the 0.001 acne
      // epsilon both depend on.
      const vec3 pv = cross(r.direction(), g.e2);
      const double det = dot(g.e1, pv);

      // case 1: ray does not intersect with the plane containing this tri, does not hit...
      if (std::fabs(det) < 1e-12) continue;

      const double inv_det = 1.0 / det;
      const vec3 tv = r.origin() - g.p0;

      // case 2: check intersection is in the triangle
      // u coordinate is out of bounds, early exit
      const double u = dot(tv, pv) * inv_det;
      if (u < 0 || u > 1) continue;

      const vec3 qv = cross(tv, g.e1);
      const double v = dot(r.direction(), qv) * inv_det;
      if (v < 0 || u + v > 1) continue;

      const double t = dot(g.e2, qv) * inv_det;
      if (t <= clipping_range.min || t >= closest) continue;

      best = int32_t(f);
      closest = t;
      bu = u;
      bv = v;
    }

    // ray didn't hit mesh at all
    if (best < 0) return false;

    const tri_geom &g = geom[best];
    const vec3 ng = cross(g.e1, g.e2);   // geometric normal, not unit

    // calculate normal
    vec3 ns = ng;
    if (!normals.empty())
    {
      const int32_t i0 = tris[3 * best + 0];
      const int32_t i1 = tris[3 * best + 1];
      const int32_t i2 = tris[3 * best + 2];
      ns = (1 - bu - bv) * normals[i0] + bu * normals[i1] + bv * normals[i2];

      // Authored normals that cancel out, or that disagree with the winding.
      // Both are bad data; neither should produce a black pixel or a scatter
      // into the surface.
      if (ns.near_zero())       ns = ng;
      else if (dot(ns, ng) < 0) ns = -ns;
    }

    info.t = closest;
    info.p = r.at(closest);
    info.mat = mat.get();
    info.element_id = face.empty() ? best : face[best];
    info.set_face_normal(r, ng, unit_vector(ns));

    return true;
  }

private:
  struct tri_geom
  {
    vec3 p0, e1, e2;
  };

  std::vector<tri_geom> geom;
};

