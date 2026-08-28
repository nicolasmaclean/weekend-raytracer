#pragma once

#include <cstdint>

#include "tracer.h"


class material;

class hit_info
{
public:
  point3 p;
  vec3 normal;
  double t;
  bool front_face;
  const material *mat = nullptr;

  int32_t prim_id = -1;
  int32_t instance_id = -1;
  int32_t element_id = -1;

  void set_face_normal(const ray &r, const vec3 &outward_normal)
  {
    // outward should be normallzed
    front_face = dot(r.direction(), outward_normal) < 0;
    normal = front_face ? outward_normal : -outward_normal;
  }

  void set_face_normal(const ray &r, const vec3 &geometric, const vec3 &shading)
  {
    front_face = dot(r.direction(), geometric) < 0;
    normal = front_face ? shading : -shading;
  }
};

class hittable
{
public:
  virtual ~hittable() = default;

  virtual bool hit(const ray &r, interval clipping_range, hit_info &info) const = 0;

  virtual void commit() {}
};

