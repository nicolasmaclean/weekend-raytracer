#pragma once

#include <utility>

#include "hittable.h"
#include "interval.h"
#include "mat4.h"
#include "vec3.h"


class instance : public hittable
{
public:
  instance(shared_ptr<hittable> prototype, const mat4 &object_to_world) : proto(std::move(prototype))
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

  [[nodiscard]] const mat4 &object_to_world() const { return xform; }

  int32_t instance_id = -1;

  void commit(uint64_t epoch) override { proto->commit(epoch); }

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
    info.instance_id = instance_id;
    return true;
  }

  [[nodiscard]] aabb bounds() const override
  {
    if (!valid) return aabb::empty();

    const aabb local = proto->bounds();
    if (local.is_empty()) return aabb::empty();

    aabb world = aabb::empty();
    for (int i = 0; i < 8; i++)
    {
      const point3 corner(i & 1 ? local.hi[0] : local.lo[0], i & 2 ? local.hi[1] : local.lo[1],
                          i & 4 ? local.hi[2] : local.lo[2]);
      world.expand(xform.transform(corner));
    }

    return world;
  }

private:
  shared_ptr<hittable> proto;
  mat4 xform = mat4::identity();
  mat4 inv = mat4::identity();
  mat4 inv_t = mat4::identity();
  bool valid = true;
};

