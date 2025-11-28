#ifndef HIITABLE_H
#define HIITABLE_H

#include "tracer.h"
class material;

class hit_info
{
public:
  point3 p;
  vec3 normal;
  double t;
  bool front_face;
  shared_ptr<material> mat;

  void set_face_normal(const ray &r, const vec3 &outward_normal)
  {
    // outward should be normallzed
    front_face = dot(r.direction(), outward_normal) < 0;
    normal = front_face ? outward_normal : -outward_normal;
  }
};

class hittable
{
public:
  virtual ~hittable() = default;

  virtual bool hit(const ray &r, interval clipping_range, hit_info &info) const = 0;
};

#endif
