#ifndef COLOR_H
#define COLOR_H

#include "interval.h"
#include "vec3.h"
#include <cmath>
#include <iostream>

using color = vec3;

inline double linear_to_gamma(double v) { return v > 0 ? std::sqrt(v) : 0; }

inline void write_color(std::ostream &out, const color &c)
{
  double rd = linear_to_gamma(c.x());
  double gd = linear_to_gamma(c.y());
  double bd = linear_to_gamma(c.z());

  static const interval intensity(0, 0.999);
  int r = int(256 * intensity.clamp(rd));
  int g = int(256 * intensity.clamp(gd));
  int b = int(256 * intensity.clamp(bd));

  out << r << ' ' << g << ' ' << b << '\n';
}

#endif
