#pragma once

#include "hittable.h"
#include "tracer.h"
#include "vec3.h"

class camera
{
public:
  int width_px = 400;
  double aspect_ratio = 16.0 / 9.0;

  void render(const hittable &world)
  {
    init();

    std::cout << "P3\n" << width_px << " " << height_px << "\n255\n";
    for (int v = 0; v < height_px; v++) {
      std::clog << "\rScanlines remaining: " << height_px - v << "       " << std::flush;
      for (int u = 0; u < width_px; u++) {
        vec3 pixel_center = viewport_origin + (u * viewport_du) + (v * viewport_dv);
        ray cam_to_px = ray(center, pixel_center - center);
        color pixel_color = ray_color(cam_to_px, world);
        write_color(std::cout, pixel_color);
        // break;
      }
    }

    std::clog << "\rDone!                        \n" << std::flush;
  }

private:
  int height_px;
  point3 center;
  point3 viewport_origin;
  vec3 viewport_du;
  vec3 viewport_dv;

  void init()
  {
    center = point3(0, 0, 0);
    height_px = int(width_px / aspect_ratio);
    height_px = (height_px < 1) ? 1 : height_px;
    double vheight = 2;
    double vwidth = vheight * (double(width_px) / height_px);

    double focal_length = 1.0;
    vec3 viewport_u = vec3(vwidth, 0, 0);
    vec3 viewport_v = vec3(0, -vheight, 0);
    viewport_du = viewport_u / width_px;
    viewport_dv = viewport_v / height_px;

    vec3 viewport_topleft = center - vec3(0, 0, focal_length) - viewport_u / 2 - viewport_v / 2;
    viewport_origin = viewport_topleft + 0.5 * (viewport_du + viewport_dv);

    std::clog << "\nCamera settings\n"
              << "===============\n"
              << "Output size: (" << width_px << ", " << height_px << ")\n"
              << "Viewport size: (" << vwidth << ", " << vheight << ")\n"
              << "Viewport dx: (" << viewport_du.x() << ", " << viewport_dv.y() << ")\n"
              << "Viewport worldspace origin: (" << viewport_origin << ")\n"
              << "Camera position: (" << center << ")\n"
              << "\n"
              << std::flush;
  }

  color ray_color(const ray &r, const hittable &world)
  {
    hit_info hit;

    if (world.hit(r, interval(0, infinity), hit)) {
      return 0.5 * (hit.normal + color(1, 1, 1));
    }

    vec3 unit_direction = unit_vector(r.direction());
    double a = 0.5 * (unit_direction.y() + 1);
    return (1.0 - a) * color(1, 1, 1) + a * color(0.5, 0.7, 1);
  }
};
