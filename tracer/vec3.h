#ifndef VEC3_H
#define VEC3_H

#include "tracer.h"
#include <cmath>
#include <iostream>

struct vec3 {
public:
  double e[3];

  vec3() : e{0, 0, 0} {}
  vec3(double x, double y, double z) : e{x, y, z} {}

  double x() const { return e[0]; }
  double y() const { return e[1]; }
  double z() const { return e[2]; }

  vec3 operator-() const { return {-e[0], -e[1], -e[2]}; }
  double operator[](int i) const { return e[i]; }
  double &operator[](int i) { return e[i]; }

  vec3 &operator+=(const vec3 other)
  {
    e[0] += other[0];
    e[1] += other[1];
    e[2] += other[2];

    return *this;
  }

  vec3 &operator*=(double t)
  {
    e[0] *= t;
    e[1] *= t;
    e[2] *= t;

    return *this;
  }

  vec3 &operator/=(double t) { return *this *= 1 / t; }

  double length() const { return std::sqrt(length_sqr()); }

  double length_sqr() const { return e[0] * e[0] + e[1] * e[1] + e[2] * e[2]; }

  bool near_zero() const
  {
    double epsilon = 1e-8;
    return std::fabs(x()) < epsilon && std::fabs(y()) < epsilon && std::fabs(z()) < epsilon;
  }

  static vec3 random() { return vec3(random_double(), random_double(), random_double()); }

  static vec3 random(double min, double max)
  {
    return vec3(random_double(min, max), random_double(min, max), random_double(min, max));
  }
};

using point3 = vec3;

inline std::ostream &operator<<(std::ostream &out, const vec3 &v)
{
  return out << v.e[0] << ", " << v.e[1] << ", " << v.e[2];
}

inline vec3 operator+(const vec3 &a, const vec3 &b)
{
  return vec3(a.e[0] + b.e[0], a.e[1] + b.e[1], a.e[2] + b.e[2]);
}

inline vec3 operator-(const vec3 &a, const vec3 &b)
{
  return vec3(a.e[0] - b.e[0], a.e[1] - b.e[1], a.e[2] - b.e[2]);
}

inline vec3 operator*(const vec3 &a, const vec3 &b)
{
  return vec3(a.e[0] * b.e[0], a.e[1] * b.e[1], a.e[2] * b.e[2]);
}

inline vec3 operator*(double t, const vec3 &v) { return vec3(t * v.e[0], t * v.e[1], t * v.e[2]); }

inline vec3 operator*(const vec3 &v, double t) { return t * v; }

inline vec3 operator/(const vec3 &v, double t) { return (1 / t) * v; }

inline double dot(const vec3 &a, const vec3 &b)
{
  return a.e[0] * b.e[0] + a.e[1] * b.e[1] + a.e[2] * b.e[2];
}

inline vec3 cross(const vec3 &a, const vec3 &b)
{
  return vec3(a.e[1] * b.e[2] - a.e[2] * b.e[1], a.e[2] * b.e[0] - a.e[0] * b.e[2],
              a.e[0] * b.e[1] - a.e[1] * b.e[0]);
}

inline vec3 unit_vector(const vec3 &v) { return v / v.length(); }

inline vec3 random_unit_vec3()
{
  while (true) {
    vec3 p = vec3::random(-1, 1);
    double len_sqr = p.length_sqr();
    if (1e-160 < len_sqr && len_sqr <= 1) {
      return p / sqrt(len_sqr);
    }
  }
}

inline vec3 random_in_unit_sphere(const vec3 &normal)
{
  vec3 p = random_unit_vec3();
  if (dot(p, normal) > 0) {
    return p;
  } else {
    return -p;
  }
}

inline vec3 reflect(const vec3 &v, const vec3 &n) { return v - 2 * dot(v, n) * n; }

// refractive_index_in / refractive_index_out
inline vec3 refract(const vec3 &r, const vec3 &n, double relative_refractive_index)
{
  double cost_theta = std::fmin(dot(-r, n), 1);
  auto r_out_perpendicular = relative_refractive_index * (r + cost_theta * n);
  auto r_out_parallel = -n * std::sqrt(1 - r_out_perpendicular.length_sqr());
  return r_out_perpendicular + r_out_parallel;
}

#endif
