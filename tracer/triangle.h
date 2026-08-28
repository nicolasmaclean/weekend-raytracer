#pragma once

#include "hittable.h"
#include "ray.h"
#include "vec3.h"
#include "vert.h"
#include <cmath>


class triangle: public hittable
{
public:
  triangle(const vertex &p0, const vertex &p1, const vertex &p2, shared_ptr<material> material)
      : p0(p0), p1(p1), p2(p2), mat(material)
  {
    e1 = p1.p - p0.p;
    e2 = p2.p - p0.p;

    vec3 n = cross(e1, e2);
    normal = unit_vector(n);
    d = dot(normal, p0.p);
    w = n / dot(n, n);
  }

  bool hit(const ray &r, interval clipping_range, hit_info &info) const override
  {
    double denominator = dot(normal, r.direction());

    // step 1: check ray hits the plane containing this triangle
    // ray is parallel to plane holding triange, does not hit
    if (std::fabs(denominator) < 1e-8)
    {
      return false;
    }

    float t = (d - dot(normal, r.origin())) / denominator;

    // triange is too far!
    if (!clipping_range.contains(t))
    {
      return false;
    }

    // step 2: check the hit is inside the triangle
    vec3 intersection = r.at(t);
    vec3 p = intersection - p0.p;
    double a = dot(w, cross(p, e2));
    double b = dot(w, cross(e1, p));

    if (!is_interior(a, b, info))
    {
      return false;
    }

    info.t = t;
    info.p = intersection;
    info.mat = mat.get();
    info.set_face_normal(r, normal);

    return true;  
  }

  virtual bool is_interior(double a, double b, hit_info &hit) const
  {
    if (a < 0 || b < 0 || a+b > 1)
    {
      return false;
    }

    return true;
  }

private:
  vertex p0, p1, p2;
  
  vec3 e1, e2;
  vec3 normal; // face normal
  double d;
  vec3 w;

  shared_ptr<material> mat;
};


