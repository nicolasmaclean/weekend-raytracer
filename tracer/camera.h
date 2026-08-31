#pragma once

#include <cmath>
#include <ostream>

#include "mat4.h"
#include "tracer.h"


class camera
{
public:
  rect2i data_window;

  double focus_dist = 10;
  double defocus_angle = 0;

  bool jitter = true;

  void set_camera(const mat4 &view_matrix, const mat4 &proj_matrix)
  {
    set_camera(view_matrix, proj_matrix, inverse(view_matrix), inverse(proj_matrix));
  }

  void set_camera(const mat4 &view_matrix, const mat4 &proj_matrix, const mat4 &inv_view_matrix,
                  const mat4 &inv_proj_matrix)
  {
    view = view_matrix;
    proj = proj_matrix;
    inv_view = inv_view_matrix;
    inv_proj = inv_proj_matrix;

    orthographic = std::round(proj.m[3][3]) == 1.0;
  }

  [[nodiscard]] bool is_orthographic() const { return orthographic; }
  [[nodiscard]] const mat4 &view_matrix() const { return view; }
  [[nodiscard]] const mat4 &proj_matrix() const { return proj; }

  ray get_ray(rng &generator, int x, int y) const
  {
    double jy = jitter ? generator.uniform() : 0.5;
    double jx = jitter ? generator.uniform() : 0.5;

    // pixel -> NDC across the data window. The window is y-down and NDC is
    // y-up, hence the sign inversion on y. The renderer flips the y-down row
    // when it writes, so nothing here needs to know the buffer's line order.
    double ndc_x = 2 * ((x + jx - data_window.min_x) / double(data_window.width())) - 1;
    double ndc_y = 1 - 2 * ((y + jy - data_window.min_y) / double(data_window.height()));

    // Un-project through the near plane (NDC z == -1) into view space.
    vec3 near_plane_trace = inv_proj.transform(vec3(ndc_x, ndc_y, -1));

    point3 origin;
    vec3 direction;
    if (orthographic)
    {
      // parallel rays from the near plane trace
      origin = near_plane_trace;
      direction = vec3(0, 0, -1);
    }
    else
    {
      // perspective: from the eye through the near plane trace
      origin = point3(0, 0, 0);
      direction = near_plane_trace;
    }

    // Thin lens, in view space: scatter the origin over the aperture disk at
    // z == 0 and re-aim at wherever the pinhole ray crossed the focal plane
    // (z == -focus_dist), so that plane stays sharp.
    if (!orthographic && defocus_angle > 0)
    {
      double lens_radius = focus_dist * std::tan(degrees_to_radians(defocus_angle / 2));
      vec3 focal_point = direction * (focus_dist / -direction.z());
      vec3 lens_sample = random_unit_disk(generator);
      origin = point3(lens_sample.x() * lens_radius, lens_sample.y() * lens_radius, 0);
      direction = focal_point - origin;
    }

    // view space -> world space
    return ray(inv_view.transform(origin), inv_view.transform_dir(direction));
  }

  void print_settings(std::ostream &out) const
  {
    out << "\nCamera settings\n"
        << "===============\n"
        << "Projection: " << (orthographic ? "orthographic" : "perspective") << "\n"
        << "Data window: (" << data_window.min_x << ", " << data_window.min_y << ") - (" << data_window.max_x
        << ", " << data_window.max_y << ")  " << data_window.width() << "x" << data_window.height() << "\n"
        << "Camera position: (" << inv_view.transform(point3(0, 0, 0)) << ")\n"
        << "Focus distance: " << focus_dist << "  defocus angle: " << defocus_angle << "\n"
        << "Jitter: " << (jitter ? "on" : "off") << "\n"
        << "\n"
        << std::flush;
  }

private:
  mat4 view = mat4::identity();
  mat4 proj = mat4::identity();
  mat4 inv_view = mat4::identity();
  mat4 inv_proj = mat4::identity();

  bool orthographic = false;
};

