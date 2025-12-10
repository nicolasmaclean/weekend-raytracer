#pragma once

#include "hittable.h"
#include "material.h"
#include "tracer.h"
#include "vec3.h"
#include <chrono>
#include <cmath>
#include <ostream>

#define MULTI_THREAD
#ifdef MULTI_THREAD
#include <omp.h>
#endif

using namespace std::chrono;

class camera
{
public:
  int width_px = 400;
  double aspect_ratio = 16.0 / 9.0;
  double v_fov = 90;
  int aa_samples_per_pixels = 50;
  int max_bounces = 20;
  point3 lookfrom = vec3(0, 0, 0);
  point3 lookat = vec3(0, 0, -1);
  vec3 vup = vec3(0, 1, 0);
  double focus_dist = 10;
  double defocus_angle = 0;

  void render(const hittable &world)
  {
    unique_ptr<vec3[]> pixel_colors(new vec3[width_px * height_px]);

    auto start = high_resolution_clock::now();

#ifdef MULTI_THREAD
    // int max_threads = std::thread::hardware_concurrency(); // typical default
    int max_threads = 12;
    omp_set_num_threads(max_threads);

#pragma omp parallel for collapse(2) schedule(dynamic)
#endif
    // render scene to buffer
    for (int v = 0; v < height_px; v++) {
      for (int u = 0; u < width_px; u++) {
        color pixel_color(0, 0, 0);
        for (int sample = 0; sample < aa_samples_per_pixels; sample++) {
          ray r = get_ray(u, v);
          pixel_color += ray_color(r, max_bounces, world);
        }

        int i = v * width_px + u;
        pixel_colors[i] = aa_sample_scale * pixel_color;
      }
      // auto elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - start);
      // auto average_per_row = elapsed.count() / (v + 1);
      // std::clog << "\rScanlines remaining: " << height_px - v
      //           << " (est time: " << average_per_row * (height_px - v + 1) / 1000 << "s) "
      //           << std::flush;
    }

    auto elapsed = duration_cast<milliseconds>(high_resolution_clock::now() - start).count();
    auto per_pixel = elapsed / (double(height_px) * width_px);
    std::clog << "\rRendered in " << elapsed / double(1000) << "s (" << per_pixel
              << "ms/px)                       \n"
              << std::flush;

    // save pixels colors to file
    std::cout << "P3\n" << width_px << " " << height_px << "\n255\n";
    for (int i = 0; i < width_px * height_px; i++) {
      write_color(std::cout, pixel_colors[i]);
    }
  }

  void print_settings(std::ostream &out)
  {
    out << "\nCamera settings\n"
        << "===============\n"
        << "Output size: (" << width_px << ", " << height_px << ")\n"
        << "Viewport dx: (" << viewport_du.x() << ", " << viewport_dv.y() << ")\n"
        << "Viewport worldspace origin: (" << viewport_origin << ")\n"
        << "Camera position: (" << center << ")\n"
        << "Max ray bounces: " << max_bounces << "\n"
        << "Anti-aliasing samples/pixel: " << aa_samples_per_pixels << "\n"
        << "\n"
        << std::flush;
  }

  void init()
  {
    center = point3(0, 0, 0);
    aa_sample_scale = 1.0 / aa_samples_per_pixels;
    center = lookfrom;

    w = unit_vector(lookfrom - lookat);
    u = unit_vector(cross(vup, w));
    v = cross(w, u);

    auto h = std::tan(degrees_to_radians(v_fov) / 2);
    height_px = int(width_px / aspect_ratio);
    height_px = (height_px < 1) ? 1 : height_px;
    double vheight = 2 * h * focus_dist;
    double vwidth = vheight * (double(width_px) / height_px);

    vec3 viewport_u = vwidth * u;
    vec3 viewport_v = -vheight * v;
    viewport_du = viewport_u / width_px;
    viewport_dv = viewport_v / height_px;

    vec3 viewport_topleft = center - focus_dist * w - viewport_u / 2 - viewport_v / 2;
    viewport_origin = viewport_topleft + 0.5 * (viewport_du + viewport_dv);

    double defocus_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
    defocus_u = u * defocus_radius;
    defocus_v = v * defocus_radius;
  }

private:
  int height_px;
  point3 center;
  point3 viewport_origin;
  vec3 viewport_du;
  vec3 viewport_dv;
  double aa_sample_scale;
  vec3 u, v, w;
  vec3 defocus_u;
  vec3 defocus_v;

  color ray_color(const ray &r, int depth, const hittable &world)
  {
    hit_info hit;

    if (depth <= 0) {
      return color(0, 0, 0);
    }

    if (world.hit(r, interval(0.001, infinity), hit)) {
      color attenuation;
      ray bounced;
      if (hit.mat->scatter(r, hit, attenuation, bounced)) {
        return attenuation * ray_color(bounced, depth - 1, world);
      }
      return color(0, 0, 0);
    }

    vec3 unit_direction = unit_vector(r.direction());
    double a = 0.5 * (unit_direction.y() + 1);
    return (1.0 - a) * color(1, 1, 1) + a * color(0.5, 0.7, 1);
  }

  ray get_ray(int u, int v)
  {
    vec3 offset = sample_square();
    vec3 pixel_sample =
        viewport_origin + ((u + offset.x()) * viewport_du) + ((v + offset.y()) * viewport_dv);
    vec3 origin = defocus_angle <= 0 ? center : defocus_disk_sample();
    return ray(origin, pixel_sample - origin);
  }

  vec3 defocus_disk_sample()
  {
    vec3 sample = random_unit_disk();
    return center + (sample.x() * defocus_u) + (sample.y() * defocus_v);
  }
  vec3 sample_square() { return vec3(random_double(-0.5, 0.5), random_double(-0.5, 0.5), 0); }
};
