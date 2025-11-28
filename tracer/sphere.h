#ifndef SPHERE_H
#define SPHERE_H

#include "hittable.h"
#include "ray.h"

class sphere : public hittable
{
public:
  sphere(const point3 &center, double radius, shared_ptr<material> material)
      : center(center), radius(radius), mat(material)
  {
  }

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    vec3 oc = center - r.origin();
    auto a = r.direction().length_sqr();
    auto h = dot(r.direction(), oc);
    auto c = oc.length_sqr() - radius * radius;

    auto discriminant = h * h - a * c;
    if (discriminant < 0)
      return false;

    auto sqrtd = std::sqrt(discriminant);

    // Find the nearest root that lies in the acceptable range.
    auto root = (h - sqrtd) / a;
    if (!clipping_range.surrounds(root)) {
      root = (h + sqrtd) / a;
      if (!clipping_range.surrounds(root))
        return false;
    }

    info.t = root;
    info.p = r.at(info.t);
    info.normal = (info.p - center) / radius;
    info.mat = mat;
    vec3 outward_normal = (info.p - center) / radius;
    info.set_face_normal(r, outward_normal);

    return true;
  }

private:
  point3 center;
  double radius;
  shared_ptr<material> mat;
};

#endif
