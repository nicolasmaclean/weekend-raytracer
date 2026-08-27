#pragma once

#include <chrono>
#include <cstdint>

#include <tbb/blocked_range2d.h>
#include <tbb/parallel_for.h>

#include "camera.h"
#include "render_buffer.h"
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
  double render(const camera &cam, const hittable &world, const aov_bindings &aovs, int samples)
  {
    auto start = high_resolution_clock::now();
    const rect2i &window = cam.data_window;

    if (!validate(cam, aovs))
    {
      return 0;
    }

    if (!any_multisample(aovs))
    {
      samples = 1;
    }

    if (multithread)
    {
      tbb::parallel_for(
          tbb::blocked_range2d<int>(window.min_y, window.max_y + 1, 16, window.min_x, window.max_x + 1, 16),
          [&](const tbb::blocked_range2d<int> &tile) {
            render_region(cam, world, aovs, samples, tile.cols().begin(), tile.cols().end(), tile.rows().begin(), tile.rows().end());
          });
    }
    else
    {
      render_region(cam, world, aovs, samples, window.min_x, window.max_x + 1, window.min_y, window.max_y + 1);
    }

    using ms_d = duration<double, std::milli>;
    return ms_d(high_resolution_clock::now() - start).count();
  }

private:
  bool validate(const camera &cam, const aov_bindings &aovs)
  {
    // validate expected buffers are setup
    render_buffer *buffer = aovs[0].buffer;
    if (aovs.empty() || buffer == nullptr)
    {
      return false;
    }

    const int w = buffer->width(), h = buffer->height();
    for (const aov_binding &b : aovs)
    {
      if (b.buffer == nullptr || b.buffer->width() != w || b.buffer->height() != h)
      {
        return false;
      }
    }

    // validate window size
    const rect2i &window = cam.data_window;
    if (window.min_x < 0 || window.min_y < 0 || window.max_x >= w || window.max_y >= h)
    {
      return false;
    }

    return true;
  }

  static bool any_multisample(const aov_bindings &aovs)
  {
    for (const aov_binding &b : aovs)
    {
        if (b.buffer->is_multisampled())
        {
          return true;
        }
    }

    return false;
  }

  void render_region(const camera &cam, const hittable &world, const aov_bindings &aovs, int samples, int x0, int x1, int y0, int y1)
  {
    const int height = aovs[0].buffer->height();
    const int width = aovs[0].buffer->width();

    // get the sample count buffer
    // we can just use first multisample aov, which will from the color pass
    const render_buffer *counter = nullptr;
    for (const aov_binding &b : aovs)
    {
      if (b.buffer->is_multisampled())
      {
        counter = b.buffer;
        break;
      }
    }

    for (int y = y0; y < y1; y++)
    {
      for (int x = x0; x < x1; x++)
      {
        const int by = height - 1 - y;
        const int i = y * width + x;
        int sample_base = 0;
        if (counter != nullptr) // use the sample accumulator if available
        {
          sample_base = counter->samples_at(x, by);
        }
        
        for (int sample = 0; sample < samples; sample++)
        {
          const bool first_ever_sample = sample_base == 0 && sample == 0;

          rng generator = rng(sample_seed(i, sample_base + sample, frame_seed));
          ray r = cam.get_ray(generator, x, y);
          hit_info hit_info;
          bool did_hit = false;
          color pixel_color = raycast(generator, r, max_bounces, world, &hit_info, &did_hit);

          for (const aov_binding &aov : aovs)
          {
            render_buffer &buffer = *aov.buffer;

            // ignore this buffer if doesn't need any more samples
            if (buffer.is_converged())
            {
              continue;
            }
            if (!first_ever_sample && !buffer.is_multisampled())
            {
              continue;
            }

            switch (aov.name)
            {
              case aov::color:
              {
                const float rgba[4] = {
                  float(pixel_color.x()),
                  float(pixel_color.y()),
                  float(pixel_color.z()),
                  1
                };
                buffer.write(x, by, 4, rgba);

                break;
              }

              case aov::depth:
              {
                if (!did_hit) break;
                const vec3 clip = cam.proj_matrix().transform(cam.view_matrix().transform(hit_info.p));
                const float d = float((clip.z() + 1) / 2);
                buffer.write(x, by, 1, &d);

                break;
              }

              case aov::camera_depth:
              {
                if (!did_hit) break;
                const float d = float(hit_info.t);
                buffer.write(x, by, 1, &d);

                break;
              }

              case aov::normal:
              {
                if (!did_hit) break;
                const float n[3] = {
                  float(hit_info.normal.x()),
                  float(hit_info.normal.y()),
                  float(hit_info.normal.z())
                };
                buffer.write(x, by, 3, n);

                break;
              }

              case aov::n_eye:
              {
                if (!did_hit) break;
                const vec3 ne = unit_vector(cam.view_matrix().transform_dir(hit_info.normal));
                const float n[3] = {
                  float(ne.x()),
                  float(ne.y()),
                  float(ne.z())
                };
                buffer.write(x, by, 3, n);

                break;
              }
            }
          }
        }
      }
    }
  }

  color raycast(rng &generator, const ray &r, int depth, const hittable &world, hit_info *primary, bool *did_hit)
  {
    if (depth <= 0)
    {
      return color(0, 0, 0);
    }

    hit_info hit;
    if (world.hit(r, interval(0.001, infinity), hit))
    {
      if (primary)
      {
        *primary = hit;
        *did_hit = true;
      }

      color attenuation;
      ray bounced;
      if (hit.mat->scatter(generator, r, hit, attenuation, bounced))
      {
        return attenuation * raycast(generator, bounced, depth - 1, world, nullptr, nullptr);
      }

      return color(0, 0, 0);
    }

    vec3 unit_direction = unit_vector(r.direction());
    double a = 0.5 * (unit_direction.y() + 1);
    return (1.0 - a) * color(1, 1, 1) + a * color(0.5, 0.7, 1);
  }
};
