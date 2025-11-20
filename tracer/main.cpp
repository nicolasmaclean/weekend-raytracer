#include <iostream>

#include "vec3.h"
#include "color.h"
#include "ray.h"


color ray_color(const ray& r)
{
    vec3 dir = unit_vector(r.direction());
    double a = 0.5*(dir.y()+1.0);
    return (1-a)*color(1, 1, 1) + a*color(0.5, 0.7, 1);
}

int main()
{
    // calculate screen size values
    double aspect_ratio = 16.0 / 9.0;
    int width  = 256;
    int height = int(width / aspect_ratio);
    height = (height < 1) ? 1 : height;
    double vheight = 2;
    double vwidth  = vheight * (double(width) / height);

    // camera settings
    double focal_length = 1.0;
    vec3 camera_position = point3(0, 0, 0);

    // setup maths for translating screen to viewport position
    vec3 viewport_u = vec3(vwidth, 0, 0);
    vec3 viewport_v = vec3(0, -vheight, 0);
    vec3 viewport_du = viewport_u / width;
    vec3 viewport_dv = viewport_v / height;

    // calculate top-left position in-world
    vec3 viewport_topleft = camera_position - vec3(0, 0, focal_length) - viewport_u/2 - viewport_v/2;
    vec3 viewport_topleft_px = viewport_topleft + 0.5*(viewport_du + viewport_dv);

    // render!
    std::cout << "P3\n" << width << " " << height << "\n255\n";
    for (int v = 0; v < height; v++)
    {
	std::clog << "\rScanlines remaining: " << (height - v) << "   " << std::flush;
	for (int u = 0; u < width; u++)
	{
	    vec3 pixel_center = viewport_topleft_px + u*viewport_du + v*viewport_dv;
	    ray cam_to_px = ray(camera_position, pixel_center - camera_position);
	    color pixel_color = ray_color(cam_to_px);
	    write_color(std::cout, pixel_color);
	}
    }

    std::clog << "\rDone!                             \n" << std::flush;
}
