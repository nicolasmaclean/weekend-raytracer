#pragma once

#include "hittable.h"
#include "ray.h"
#include "tracer.h"
#include "vec3.h"
#include <cmath>

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

class glass : public material
{
public:
  glass(color albedo, double r) : albedo(albedo), r(r) {}

  bool scatter(const ray &r_in, const hit_info &info, color &attenuation, ray &r_out) const override
  {
    attenuation = albedo;
    double ri = info.front_face ? (1 / r) : r;

    double cos_theta = std::fmin(dot(-unit_vector(r_in.direction()), info.normal), 1);
    double sin_theta = std::sqrt(1 - cos_theta * cos_theta);
    bool cannot_refract = ri * sin_theta > 1;
    if (cannot_refract || reflectance(cos_theta, ri) > random_double()) {
      // reflect
      r_out = ray(info.p, reflect(r_in.direction(), info.normal));
    } else {
      // refract
      r_out = ray(info.p, refract(unit_vector(r_in.direction()), info.normal, ri));
    }

    return true;
  }

private:
  color albedo;
  double r;

  static double reflectance(double cos, double ri)
  {
    auto r0 = (1 - ri) / (1 + ri);
    r0 = r0 * r0;
    return r0 + (1 - r0) * std::pow(1 - cos, 5);
  }
};
