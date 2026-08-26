#include <cstdlib>
#include <iostream>

#include "camera.h"
#include "hittable_list.h"
#include "example_scenes.h"
#include "renderer.h"


int main(int argc, char *argv[])
{
  int i_scene = argc > 1 ? atoi(argv[1]) : 0;
  bool multithread = !(argc > 2 && atoi(argv[2]) > 0);
  int width  = argc > 3 ? atoi(argv[3]) : 400;
  int height = argc > 4 ? atoi(argv[4]) : 225;

  hittable_list world;
  camera_desc desc;
  load_scene(i_scene, world, desc);

  camera cam = desc.build(width, height);

  renderer r;
  r.max_bounces = 10;
  r.multithread = multithread;

  framebuffer buffer;
  buffer.allocate(width, height);

  std::clog << "Rendering scene " << " " << i_scene << "..." << std::flush;
  double render_duration = r.render(cam, world, buffer, 50);

  // save buffer to image file
  std::cout << "P3\n" << width << " " << height << "\n255\n";
  for (int i = 0; i < buffer.pixel_count; i++) {
    write_color(std::cout, buffer.get_pixel(i));
  }

  // output finish message
  auto per_pixel = render_duration / (double(height) * width);
  std::clog << "\rRendered scene " << i_scene << " in " << render_duration / double(1000) << "s ("
            << per_pixel << "ms/px)                       \n"
            << std::flush;
}
