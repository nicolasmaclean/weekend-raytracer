#pragma once

#include <chrono>
#include <cmath>
#include <ostream>
#include <thread>

#include <omp.h>

#include "framebuffer.h"
#include "hittable.h"
#include "material.h"
#include "rng.h"
#include "tracer.h"
#include "vec3.h"


using namespace std::chrono;

class camera
{
public:
  int width_px = 400;
  int height_px;
  double aspect_ratio = 16.0 / 9.0;
  double v_fov = 90;
  int max_bounces = 20;
  point3 lookfrom = vec3(0, 0, 0);
  point3 lookat = vec3(0, 0, -1);
  vec3 vup = vec3(0, 1, 0);
  double focus_dist = 10;
  double defocus_angle = 0;
  bool use_openmp = true;

  double render(const hittable &world, framebuffer &buffer, int samples)
  {
    if (use_openmp)
    {
      return render_region_openmp(world, buffer, 0, width_px, 0, height_px, samples);
    }
    
    return render_region(world, buffer, 0, width_px, 0, height_px, samples);
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
        << "\n"
        << std::flush;
  }

  void init()
  {
    center = point3(0, 0, 0);
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
  point3 center;
  point3 viewport_origin;
  vec3 viewport_du;
  vec3 viewport_dv;
  vec3 u, v, w;
  vec3 defocus_u;
  vec3 defocus_v;

  double render_region(const hittable &world, framebuffer &buffer, int x0, int x1, int y0, int y1, int samples)
  {
    auto start = high_resolution_clock::now();

    // render scene to buffer
    for (int v = y0; v < y1; v++) {
      for (int u = x0; u < x1; u++) {
        int i = v * width_px + u;
        int sample_base = buffer.samples[i];
        for (int sample = 0; sample < samples; sample++) {
          rng generator = rng(sample_seed(v*width_px+u, sample_base+sample, 0));
          ray r = get_ray(generator, u, v);
          buffer.pixels[i] += ray_color(generator, r, max_bounces, world);
        }
        buffer.samples[i] += samples+1;
      }
    }

    using ms_d = duration<double, std::milli>;
    return ms_d(high_resolution_clock::now() - start).count();
  }

  double render_region_openmp(const hittable &world, framebuffer &buffer, int x0, int x1, int y0, int y1, int samples)
  {
    auto start = high_resolution_clock::now();
   
    int max_threads = std::thread::hardware_concurrency(); // typical default
    omp_set_num_threads(max_threads);

    // render scene to buffer
#pragma omp parallel for collapse(2) schedule(dynamic, 16)
    for (int v = y0; v < y1; v++) {
      for (int u = x0; u < x1; u++) {
        int i = v * width_px + u;
        for (int sample = 0; sample < samples; sample++) {
          rng generator = rng(sample_seed(v*width_px+u, buffer.samples[i]++, 0));
          ray r = get_ray(generator, u, v);
          buffer.pixels[i] += ray_color(generator, r, max_bounces, world);
        }
      }
    }
    
    using ms_d = duration<double, std::milli>;
    return ms_d(high_resolution_clock::now() - start).count();
  }
  
  color ray_color(rng &generator, const ray &r, int depth, const hittable &world)
  {
    hit_info hit;

    if (depth <= 0) {
      return color(0, 0, 0);
    }

    if (world.hit(r, interval(0.001, infinity), hit)) {
      color attenuation;
      ray bounced;
      if (hit.mat->scatter(generator, r, hit, attenuation, bounced)) {
        return attenuation * ray_color(generator, bounced, depth - 1, world);
      }
      return color(0, 0, 0);
    }

    vec3 unit_direction = unit_vector(r.direction());
    double a = 0.5 * (unit_direction.y() + 1);
    return (1.0 - a) * color(1, 1, 1) + a * color(0.5, 0.7, 1);
  }

  ray get_ray(rng &generator, int u, int v)
  {
    vec3 offset = sample_square(generator);
    vec3 pixel_sample =
        viewport_origin + ((u + offset.x()) * viewport_du) + ((v + offset.y()) * viewport_dv);
    vec3 origin = defocus_angle <= 0 ? center : defocus_disk_sample(generator);
    return ray(origin, pixel_sample - origin);
  }

  vec3 defocus_disk_sample(rng &generator)
  {
    vec3 sample = random_unit_disk(generator);
    return center + (sample.x() * defocus_u) + (sample.y() * defocus_v);
  }
  vec3 sample_square(rng &generator) { return vec3(generator.uniform(-0.5, 0.5), generator.uniform(-0.5, 0.5), 0); }
};
