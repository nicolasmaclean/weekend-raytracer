#pragma once

#include <chrono>
#include <cstdint>

#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>

#include "camera.h"
#include "framebuffer.h"
#include "hittable.h"
#include "material.h"
#include "rng.h"
#include "tracer.h"
#include "vec3.h"

using namespace std::chrono;

struct renderer
{
  int max_bounces = 20;
  bool multithread = true;

  uint64_t frame_seed = 0; // -1 means "nondeterministic".

  // Accumulates `samples` more samples per pixel into `buffer` over the
  // camera's data window. Returns elapsed milliseconds.
  double render(const camera &cam, const hittable &world, framebuffer &buffer, int samples)
  {
    auto start = high_resolution_clock::now();
    const rect2i &window = cam.data_window;

    if (multithread)
    {
      tbb::parallel_for(
          tbb::blocked_range2d<int>(window.min_y, window.max_y + 1, 16, window.min_x, window.max_x + 1, 16),
          [&](const tbb::blocked_range2d<int> &tile) {
            render_region(cam, world, buffer, samples, tile.cols().begin(), tile.cols().end(), tile.rows().begin(), tile.rows().end());
          });
    }
    else
    {
      render_region(cam, world, buffer, samples, window.min_x, window.max_x + 1, window.min_y, window.max_y + 1);
    }

    using ms_d = duration<double, std::milli>;
    return ms_d(high_resolution_clock::now() - start).count();
  }

private:
  void render_region(const camera &cam, const hittable &world, framebuffer &buffer, int samples, int x0, int x1, int y0, int y1)
  {
    for (int y = y0; y < y1; y++)
    {
      for (int x = x0; x < x1; x++)
      {
        int i = y * buffer.width + x;
        int sample_base = buffer.samples[i];
        
        for (int sample = 0; sample < samples; sample++)
        {
          rng generator = rng(sample_seed(i, sample_base + sample, frame_seed));
          ray r = cam.get_ray(generator, x, y);
          buffer.pixels[i] += ray_color(generator, r, max_bounces, world);
        }

        buffer.samples[i] += samples;
      }
    }
  }

  color ray_color(rng &generator, const ray &r, int depth, const hittable &world)
  {
    if (depth <= 0)
    {
      return color(0, 0, 0);
    }

    hit_info hit;
    
    if (world.hit(r, interval(0.001, infinity), hit))
    {
      color attenuation;
      ray bounced;
      if (hit.mat->scatter(generator, r, hit, attenuation, bounced))
      {
        return attenuation * ray_color(generator, bounced, depth - 1, world);
      }

      return color(0, 0, 0);
    }

    vec3 unit_direction = unit_vector(r.direction());
    double a = 0.5 * (unit_direction.y() + 1);
    return (1.0 - a) * color(1, 1, 1) + a * color(0.5, 0.7, 1);
  }
};
