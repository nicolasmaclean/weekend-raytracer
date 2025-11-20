#include <iostream>

#include "vec3.h"
#include "color.h"


int main()
{
    int width = 256;
    int height = 256;

    std::cout << "P3\n" << width << " " << height << "\n255\n";

    for (int y = 0; y < height; y++)
    {
	std::clog << "\rScanlines remaining: " << (height - y) << std::flush;
	for (int x = 0; x < width; x++)
	{
	    auto pixel = color(double(x) / (width = 1), double(y) / (height - 1), 0);
	    write_color(std::cout, pixel);
	}
    }

    std::clog << "\rDone!                             \n" << std::flush;
}
