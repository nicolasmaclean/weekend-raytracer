#ifndef EXAMPLE_SCENES_H
#define EXAMPLE_SCENES_H

#include "camera.h"
#include "framebuffer.h"
#include "hittable_list.h"
#include "material.h"
#include "sphere.h"
#include "tracer.h"
#include <cstdlib>
#include <iostream>

void scene_1(hittable_list &world, camera &camera)
{
  rng gen(0xC0FFEE);

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
      point3 center = vec3(a + 0.9 * gen.uniform(), .2, b + 0.9 * gen.uniform());
      if ((center - vec3(4, .2, 0)).length() > .9) {
        shared_ptr<material> mat;
        double choose_mat = gen.uniform();
        if (choose_mat < 0.8) {
          mat = make_shared<lambert>(color::random(gen) * color::random(gen));
        } else if (choose_mat < 0.95) {
          color albedo = color::random(gen, 0.5, 1);
          double fuzz = gen.uniform(0, 0.5);
          mat = make_shared<metal>(albedo, fuzz);
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

inline void load_scene(int i, hittable_list &world, camera &camera)
{
  // prepare camera/scene
  camera.width_px = 400;
  camera.max_bounces = 10;

  switch (i) {
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
  camera.init();
}

#endif
