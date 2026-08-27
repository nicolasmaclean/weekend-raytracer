#pragma once

#include "hittable.h"
#include "interval.h"
#include "mat4.h"
#include "vec3.h"


class instance : public hittable
{
public:
  instance(shared_ptr<hittable> prototype, const mat4 &object_to_world) : proto(prototype)
  {
    set_transform(object_to_world);
  }

  void set_transform(const mat4 &object_to_world)
  {
    xform = object_to_world;
    inv = inverse(object_to_world);
    inv_t = transpose(inv);
    valid = is_finite(inv);
  }

  const mat4 &object_to_world() const { return xform; }

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    if (!valid) return false;

    const ray local(inv.transform(r.origin()), inv.transform_dir(r.direction()));
    if (!proto->hit(local, clipping_range, info))
    {
      return false;
    }

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
