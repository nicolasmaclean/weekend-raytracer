#include "camera.h"
#include "hittable_list.h"
#include <cstdlib>
#include <iostream>
#include "example_scenes.h"


int main(int argc, char *argv[])
{
  // default cmd line args
  int i_scene = 0;

  // select test scene
  if (argc > 1) {
    i_scene = atoi(argv[1]);
  }

  // prepare camera/scene
  camera camera;
  hittable_list world;
  load_scene(i_scene, world, camera);

  // prepare frame buffer
  framebuffer buffer;
  buffer.allocate(camera.width_px, camera.height_px);

  // render!
  std::clog << "Rendering scene " << " " << i_scene << "..." << std::flush;
  double renderDuration;
  if (argc > 2 && atoi(argv[2]) > 0) {
    renderDuration = camera.render(world, buffer);
  } else {
    renderDuration = camera.render_openmp(world, buffer);
  }

  // save buffer to image file
  std::cout << "P3\n" << camera.width_px << " " << camera.height_px << "\n255\n";
  for (int i = 0; i < camera.width_px * camera.height_px; i++) {
    write_color(std::cout, buffer.pixels[i]);
  }

  // output finish message
  auto per_pixel = renderDuration / (double(camera.height_px) * camera.width_px);
  std::clog << "\rRendered scene " << i_scene << " in " << renderDuration / double(1000) << "s ("
            << per_pixel << "ms/px)                       \n"
            << std::flush;
}
