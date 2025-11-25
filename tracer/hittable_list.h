#pragma once

#include <vector>

#include "hittable.h"
#include "tracer.h"

class hittable_list : public hittable {
public:
  std::vector<shared_ptr<hittable>> objects;

  hittable_list() {}
  hittable_list(shared_ptr<hittable> obj) { add(obj); }

  void add(shared_ptr<hittable> obj) { objects.push_back(obj); }

  void clear() { objects.clear(); }

  bool hit(const ray &r, interval clipping_range,
           hit_info &info) const override {
    hit_info temp_info;
    bool did_hit = false;
    double closest = clipping_range.max;

    for (const auto &obj : objects) {
      if (obj->hit(r, interval(clipping_range.min, closest), temp_info)) {
        did_hit = true;
        closest = temp_info.t;
        info = temp_info;
      }
    }

    return did_hit;
  }
};
