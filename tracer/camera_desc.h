#pragma once

#include "camera.h"
#include "mat4.h"
#include "tracer.h"

enum class projection { perspective, orthographic };

// This class is for cli/sdl versions of the tracer. Hydra delegates are passed
// the matrix information we are constructing from the values here.
struct camera_desc
{
  point3 lookfrom = point3(0, 0, 0);
  point3 lookat = point3(0, 0, -1);
  vec3 vup = vec3(0, 1, 0);

  projection proj_type = projection::perspective;
  double v_fov = 90;             // perspective only, degrees, vertical
  double ortho_half_height = 1;  // orthographic only, world units

  double near_clip = 0.1;
  double far_clip = 1000;

  double focus_dist = 10;
  double defocus_angle = 0;

  camera build(int width, int height) const
  {
    double aspect = double(width) / double(height);

    camera cam;
    cam.set_camera(
        look_at(lookfrom, lookat, vup),
        proj_type == projection::orthographic
          ? orthographic(ortho_half_height * aspect, ortho_half_height, near_clip, far_clip)
          : perspective(v_fov, aspect, near_clip, far_clip)
    );
    cam.data_window = rect2i::from_size(width, height);
    cam.focus_dist = focus_dist;
    cam.defocus_angle = defocus_angle;
    
    return cam;
  }
};
