#ifndef COLOR_H
#define COLOR_H


#include <iostream>

#include "vec3.h"


using color = vec3;


void write_color(std::ostream& out, const color& c)
{
    int r = int(255.999 * c[0]);
    int g = int(255.999 * c[1]);
    int b = int(255.999 * c[2]);

    out << r << ' ' << g << ' ' << b << '\n';
}


#endif
