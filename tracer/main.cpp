#include <cstdlib>
#include <iostream>

#include "camera.h"
#include "scene.h"
#include "example_scenes.h"
#include "renderer.h"
#include "schedulers.h"


int main(int argc, char *argv[])
{
  int i_scene = argc > 1 ? atoi(argv[1]) : 0;
  bool multithread = argc <= 2 || atoi(argv[2]) <= 0;
  int width = argc > 3 ? atoi(argv[3]) : 400;
  int height = argc > 4 ? atoi(argv[4]) : 225;

  scene world;
  camera_desc desc;
  load_scene(i_scene, world, desc);

  camera cam = desc.build(width, height);

  renderer r;
  r.max_bounces = 10;
  r.samples_to_converge = 50;
  if (multithread)
  {
    r.schedule = tbb_schedule;
  }

  render_buffer color_buf, depth_buf;
  aov_bindings aovs = {allocate_aov(color_buf, aov::color, width, height),
                       allocate_aov(depth_buf, aov::depth, width, height)};

  std::clog << "Rendering scene " << " " << i_scene << "..." << std::flush;
  render_stats stats = r.render(cam, world, aovs);
  color_buf.resolve();

  // save buffer to image file
  std::cout << "P3\n" << width << " " << height << "\n255\n";
  for (int y = height - 1; y >= 0; y--)
  {
    for (int x = 0; x < width; x++)
    {
      float rgba[4];
      color_buf.read(x, y, 4, rgba);
      write_color(std::cout, color(rgba[0], rgba[1], rgba[2]));
    }
  }

  // output finish message
  auto per_pixel = stats.ms / (double(height) * width);
  std::clog << "\rRendered scene " << i_scene << " in " << stats.ms / double(1000) << "s (" << per_pixel
            << "ms/px)                       \n"
            << std::flush;
}

