#ifndef COLOR_H
#define COLOR_H

#include "interval.h"
#include "vec3.h"
#include <iostream>

using color = vec3;

inline void write_color(std::ostream &out, const color &c)
{
  static const interval intensity(0, 0.999);
  int r = int(256 * intensity.clamp(c.x()));
  int g = int(256 * intensity.clamp(c.y()));
  int b = int(256 * intensity.clamp(c.z()));

  out << r << ' ' << g << ' ' << b << '\n';
}

#endif
