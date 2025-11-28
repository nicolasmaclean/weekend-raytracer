#pragma once

#include "hittable.h"
#include "ray.h"
#include "vec3.h"

class material
{
public:
  virtual ~material() = default;

  virtual bool scatter(const ray &r_in, const hit_info &info, color &attenuation, ray &r_out) const
  {
    return false;
  }
};

class lambert : public material
{
public:
  lambert(const color &albedo) : albedo(albedo) {}

  bool scatter(const ray &r_in, const hit_info &info, color &attenuation, ray &r_out) const override
  {
    attenuation = albedo;
    vec3 scatter_direction = info.p + random_unit_vec3();
    if (scatter_direction.near_zero())
      scatter_direction = info.normal;
    r_out = ray(info.p, scatter_direction);

    return true;
  }

private:
  color albedo;
};

class metal : public material
{
public:
  metal(const color &albedo, double fuzz) : albedo(albedo), fuzz(fuzz < 1 ? fuzz : 1) {}

  bool scatter(const ray &r_in, const hit_info &info, color &attenuation, ray &r_out) const override
  {
    attenuation = albedo;
    vec3 reflected = reflect(r_in.direction(), info.normal);
    reflected = unit_vector(reflected) + fuzz * random_unit_vec3();
    r_out = ray(info.p, reflected);

    return dot(reflected, info.normal) > 0;
  }

private:
  color albedo;
  double fuzz;
};
