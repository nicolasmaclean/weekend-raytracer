#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "hittable.h"
#include "tracer.h"

class hittable_list : public hittable
{
public:
  std::vector<shared_ptr<hittable>> objects;

  hittable_list() = default;
  hittable_list(shared_ptr<hittable> obj) { add(std::move(obj)); }

  void add(shared_ptr<hittable> obj) { objects.push_back(std::move(obj)); }

  void clear() { objects.clear(); }

  void commit(uint64_t epoch) override
  {
    for (const auto &obj : objects)
    {
      obj->commit(epoch);
    }
  }

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    hit_info temp_info;
    bool did_hit = false;
    double closest = clipping_range.max;

    for (const auto &obj : objects)
    {
      if (obj->hit(r, interval(clipping_range.min, closest), temp_info))
      {
        did_hit = true;
        closest = temp_info.t;
        info = temp_info;
        temp_info.instance_id = -1;
        temp_info.element_id = -1;
      }
    }

    return did_hit;
  }

  [[nodiscard]] aabb bounds() const override
  {
    aabb b = aabb::empty();
    for (const auto &obj : objects)
    {
      b.expand(obj->bounds());
    }
    return b;
  }
};

