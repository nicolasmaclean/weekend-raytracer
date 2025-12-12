#include "camera.h"
#include "hittable_list.h"
#include "material.h"
#include "sphere.h"
#include "tracer.h"
#include <cstdlib>
#include <iostream>

void scene_1(hittable_list &world, camera &camera)
{
  // Setup up the scene
  auto m_ground = make_shared<lambert>(color(0.6, 0.6, 0.6));
  auto m_glass = make_shared<glass>(color(1, 1, 1), 1.5);
  auto m_lam = make_shared<lambert>(color(0.4, 0.2, 0.1));
  auto m_metal = make_shared<metal>(color(0.7, 0.6, 0.5), 0);

  // hittable_list world;
  world.add(make_shared<sphere>(vec3(0, -1000, 0), 1000, m_ground));
  world.add(make_shared<sphere>(vec3(4, 1, 0), 1, m_metal));
  world.add(make_shared<sphere>(vec3(0, 1, 0), 1, m_glass));
  world.add(make_shared<sphere>(vec3(-4, 1, 0), 1, m_lam));

  for (int a = -11; a < 11; a++) {
    for (int b = -11; b < 11; b++) {
      point3 center = vec3(a + 0.9 * random_double(), .2, b + 0.9 * random_double());
      if ((center - vec3(4, .2, 0)).length() > .9) {
        shared_ptr<material> mat;
        double choose_mat = random_double();
        if (choose_mat < 0.8) {
          mat = make_shared<lambert>(color::random() * color::random());
        } else if (choose_mat < 0.95) {
          mat = make_shared<metal>(color::random(0.5, 1), random_double(0, 0.5));
        } else {
          mat = make_shared<glass>(color(1, 1, 1), 1.5);
        }
        world.add(make_shared<sphere>(center, 0.2, mat));
      }
    }
  }

  camera.lookfrom = vec3(13, 2, 3);
  camera.lookat = vec3(0, 0, 0);
  camera.defocus_angle = 0.6;
  camera.v_fov = 20;
  camera.focus_dist = 10;
}

void scene_2(hittable_list &world, camera &camera)
{

  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_left = make_shared<glass>(color(1, 1, 1), 1.5);
  auto m_bubble = make_shared<glass>(color(1, 1, 1), 1 / 1.5);
  auto m_center = make_shared<lambert>(color(0.1, 0.2, 0.5));
  auto m_right = make_shared<metal>(color(0.7, 0.7, 0.7), 0.05);

  world.add(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));
  world.add(make_shared<sphere>(vec3(-1, 0, -1), 0.5, m_left));
  world.add(make_shared<sphere>(vec3(-1, 0, -1), 0.4, m_bubble));
  world.add(make_shared<sphere>(vec3(0, 0, -1), 0.5, m_center));
  world.add(make_shared<sphere>(vec3(1, 0, -1), 0.5, m_right));

  camera.lookfrom = vec3(0, 0, 0);
  camera.lookat = vec3(0, 0, -1);
  camera.v_fov = 90;
  camera.focus_dist = 1;
  camera.defocus_angle = 0.6;
}

void scene_3(hittable_list &world, camera &camera)
{

  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_left = make_shared<glass>(color(1, 1, 1), 1.5);
  auto m_bubble = make_shared<glass>(color(1, 1, 1), 1 / 1.5);
  auto m_center = make_shared<lambert>(color(0.1, 0.2, 0.5));
  auto m_right = make_shared<metal>(color(0.7, 0.7, 0.7), 0.05);

  world.add(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));
  world.add(make_shared<sphere>(vec3(-1, 0, -1), 0.5, m_left));
  world.add(make_shared<sphere>(vec3(-1, 0, -1), 0.4, m_bubble));
  world.add(make_shared<sphere>(vec3(0, 0, -1), 0.5, m_center));
  world.add(make_shared<sphere>(vec3(1, 0, -1), 0.5, m_right));

  camera.lookfrom = vec3(-2, 2, 1);
  camera.lookat = vec3(0, 0, -1);
  camera.focus_dist = 4;
  camera.defocus_angle = 0.6;
  camera.v_fov = 20;
}

int main(int argc, char *argv[])
{
  // default cmd line args
  int i_scene = 0;
  bool write_render_to_cout = true;

  // select test scene
  if (argc > 1) {
    i_scene = atoi(argv[1]);
  }

  if (argc > 2) {
    write_render_to_cout = false;
  }

  camera camera;
  hittable_list world;
  camera.width_px = 400;
  camera.aa_samples_per_pixels = 50;
  camera.max_bounces = 10;
  // TODO: camera.open_mp

  switch (i_scene) {
  case 0:
    scene_1(world, camera);
    break;
  case 1:
    scene_2(world, camera);
    break;
  case 2:
    scene_3(world, camera);
    break;
  }

  std::clog << "Rendering scene " << i_scene << "..." << std::flush;
  auto renderDuration = camera.render(world, write_render_to_cout);

  auto per_pixel = renderDuration / (double(camera.height_px) * camera.width_px);
  std::clog << "\rRendered scene " << i_scene << " in " << renderDuration / double(1000) << "s ("
            << per_pixel << "ms/px)                       \n"
            << std::flush;
}
