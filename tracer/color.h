#ifndef COLOR_H
#define COLOR_H

#include "vec3.h"
#include <iostream>

using color = vec3;

inline void write_color(std::ostream &out, const color &c)
{
  int r = int(255.999 * c[0]);
  int g = int(255.999 * c[1]);
  int b = int(255.999 * c[2]);

  out << r << ' ' << g << ' ' << b << '\n';
}

#endif
