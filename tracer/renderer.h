#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

#include "camera.h"
#include "render_buffer.h"
#include "hittable.h"
#include "material.h"
#include "render_control.h"
#include "rng.h"
#include "tracer.h"
#include "vec3.h"

using namespace std::chrono;


struct render_stats
{
  double ms = 0;
  int completed_samples = 0;
  bool stopped = false; // true when render was aborted
};

struct renderer
{
  int max_bounces = 20;
  int samples_to_converge = 100;

  uint64_t frame_seed = 0; // -1 means "nondeterministic".

  // multi-threading needs to be injected here
  tile_scheduler schedule = singlethread_schedule;
  int tile_size = 8;

  // Accumulates `samples` more samples per pixel into `buffer` over the
  // camera's data window. Returns elapsed milliseconds.
  render_stats render(const camera &cam, hittable &world, const aov_bindings &aovs,
                      const render_control *control = nullptr)
  {
    auto start = high_resolution_clock::now();
    render_stats stats;

    if (!validate(cam, aovs))
    {
      return stats;
    }

    _completed_samples.store(0);
    world.commit();

    for (const aov_binding &b : aovs)
    {
      b.buffer->map();
    }

    const tile_grid grid(cam.data_window, tile_size);
    const int passes = any_multisample(aovs) ? std::max(samples_to_converge, 1) : 1;

    for (int pass = 0; pass < passes; pass++)
    {
      // allow pause
      while (control != nullptr && control->is_pause_requested())
      {
        if (control->is_stop_requested())
        {
          break;
        }
        std::this_thread::sleep_for(milliseconds(10));
      }

      // allow render cancel
      if (control != nullptr && control->is_stop_requested())
      {
        stats.stopped = true;
        break;
      }

      schedule(grid.count(), [&](size_t begin, size_t end)
               { render_tiles(cam, world, aovs, grid, pass, begin, end, control); });

      // 1st pass marks single-pass aov to done
      if (pass == 0)
      {
        for (const aov_binding &b : aovs)
        {
          if (!b.buffer->is_multisampled())
          {
            b.buffer->set_converged(true);
          }
        }
      }

      // allow render cancel
      if (control != nullptr && control->is_stop_requested())
      {
        stats.stopped = true;
        break;
      }

      _completed_samples.store(pass + 1);
    }

    for (const aov_binding &b : aovs)
    {
      b.buffer->unmap();
      b.buffer->set_converged(true);
    }

    using ms_d = duration<double, std::milli>;
    stats.ms = ms_d(high_resolution_clock::now() - start).count();
    stats.completed_samples = _completed_samples.load();
    return stats;
  }

  void render_tiles(const camera &cam, const hittable &world, const aov_bindings &aovs, const tile_grid &grid,
                    int sample_pass, size_t begin, size_t end, const render_control *control = nullptr) const
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

    for (size_t t = begin; t < end; t++)
    {
      if (control != nullptr && control->is_stop_requested())
      {
        break;
      }

      const rect2i tile = grid.tile(t);
      for (int y = tile.min_y; y <= tile.max_y; y++)
      {
        for (int x = tile.min_x; x <= tile.max_x; x++)
        {
          const int by = height - 1 - y;
          const int i = y * width + x;
          int sample_base = sample_pass;
          if (counter != nullptr) // use the sample accumulator if available
          {
            sample_base = counter->samples_at(x, by);
          }
          const bool first_ever_sample = sample_base == 0;

          rng generator = rng(sample_seed(i, sample_base, frame_seed));
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
              const float rgba[4] = {float(pixel_color.x()), float(pixel_color.y()), float(pixel_color.z()),
                                     1};
              buffer.write(x, by, 4, rgba);

              break;
            }

            case aov::depth:
            {
              if (!did_hit) break;
              const vec3 clip = cam.proj_matrix().transform(cam.view_matrix().transform(hit_info.p));
              const auto d = float((clip.z() + 1) / 2);
              buffer.write(x, by, 1, &d);

              break;
            }

            case aov::camera_depth:
            {
              if (!did_hit) break;
              const auto d = float(hit_info.t);
              buffer.write(x, by, 1, &d);

              break;
            }

            case aov::normal:
            {
              if (!did_hit) break;
              const float n[3] = {float(hit_info.normal.x()), float(hit_info.normal.y()),
                                  float(hit_info.normal.z())};
              buffer.write(x, by, 3, n);

              break;
            }

            case aov::n_eye:
            {
              if (!did_hit) break;
              const vec3 ne = unit_vector(cam.view_matrix().transform_dir(hit_info.normal));
              const float n[3] = {float(ne.x()), float(ne.y()), float(ne.z())};
              buffer.write(x, by, 3, n);

              break;
            }

            case aov::prim_id:
            {
              if (!did_hit) break;
              const int32_t id = hit_info.prim_id;
              buffer.write(x, by, 1, &id);

              break;
            }

            case aov::instance_id:
            {
              if (!did_hit) break;
              const int32_t id = hit_info.instance_id;
              buffer.write(x, by, 1, &id);

              break;
            }

            case aov::element_id:
            {
              if (!did_hit) break;
              const int32_t id = hit_info.element_id;
              buffer.write(x, by, 1, &id);

              break;
            }
            }
          }
        }
      }
    }
  }

  [[nodiscard]] int completed_samples() const { return _completed_samples.load(); }

private:
  std::atomic<int> _completed_samples{0};

  static bool validate(const camera &cam, const aov_bindings &aovs)
  {
    if (aovs.empty())
    {
      return false;
    }

    render_buffer *buffer = aovs[0].buffer;
    if (buffer == nullptr)
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
    if (window.is_empty() || window.min_x < 0 || window.min_y < 0 || window.max_x >= w || window.max_y >= h)
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


  color raycast(rng &generator, const ray &r, int depth, const hittable &world, hit_info *primary,
                bool *did_hit) const
  {
    if (depth <= 0)
    {
      return {0, 0, 0};
    }

    hit_info hit;
    if (world.hit(r, interval(0.001, infinity), hit))
    {
      if (primary != nullptr)
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

      return {0, 0, 0};
    }

    vec3 unit_direction = unit_vector(r.direction());
    double a = 0.5 * (unit_direction.y() + 1);
    return (1.0 - a) * color(1, 1, 1) + a * color(0.5, 0.7, 1);
  }
};

inline void mark_unconverged(const aov_bindings &aovs)
{
  for (const aov_binding &b : aovs)
  {
    if (b.buffer != nullptr)
    {
      b.buffer->set_converged(false);
    }
  }
}

