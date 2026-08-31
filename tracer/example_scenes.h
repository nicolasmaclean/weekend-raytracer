#pragma once

#include <cstdlib>

#include "camera_desc.h"
#include "camera.h"
#include "instance.h"
#include "hittable_list.h"
#include "material.h"
#include "mesh.h"
#include "obj_loader.h"
#include "scene.h"
#include "sphere.h"
#include "triangle.h"
#include "tracer.h"

void scene_1(scene_edit &world, camera_desc &camera)
{
  rng gen(0xC0FFEE);

  // Setup up the scene
  auto m_ground = make_shared<lambert>(color(0.6, 0.6, 0.6));
  auto m_glass = make_shared<glass>(color(1, 1, 1), 1.5);
  auto m_lam = make_shared<lambert>(color(0.4, 0.2, 0.1));
  auto m_metal = make_shared<metal>(color(0.7, 0.6, 0.5), 0);

  // hittable_list world;
  world.insert(make_shared<sphere>(vec3(0, -1000, 0), 1000, m_ground));
  world.insert(make_shared<sphere>(vec3(4, 1, 0), 1, m_metal));
  world.insert(make_shared<sphere>(vec3(0, 1, 0), 1, m_glass));
  world.insert(make_shared<sphere>(vec3(-4, 1, 0), 1, m_lam));

  for (int a = -11; a < 11; a++)
  {
    for (int b = -11; b < 11; b++)
    {
      point3 center = vec3(a + 0.9 * gen.uniform(), .2, b + 0.9 * gen.uniform());
      if ((center - vec3(4, .2, 0)).length() > .9)
      {
        shared_ptr<material> mat;
        double choose_mat = gen.uniform();
        if (choose_mat < 0.8)
        {
          mat = make_shared<lambert>(color::random(gen) * color::random(gen));
        }
        else if (choose_mat < 0.95)
        {
          color albedo = color::random(gen, 0.5, 1);
          double fuzz = gen.uniform(0, 0.5);
          mat = make_shared<metal>(albedo, fuzz);
        }
        else
        {
          mat = make_shared<glass>(color(1, 1, 1), 1.5);
        }
        world.insert(make_shared<sphere>(center, 0.2, mat));
      }
    }
  }

  camera.lookfrom = vec3(13, 2, 3);
  camera.lookat = vec3(0, 0, 0);
  camera.defocus_angle = 0.6;
  camera.v_fov = 20;
  camera.focus_dist = 10;
}

void scene_2(scene_edit &world, camera_desc &camera)
{
  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_left = make_shared<glass>(color(1, 1, 1), 1.5);
  auto m_bubble = make_shared<glass>(color(1, 1, 1), 1 / 1.5);
  auto m_center = make_shared<lambert>(color(0.1, 0.2, 0.5));
  auto m_right = make_shared<metal>(color(0.7, 0.7, 0.7), 0.05);

  world.insert(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));
  world.insert(make_shared<sphere>(vec3(-1, 0, -1), 0.5, m_left));
  world.insert(make_shared<sphere>(vec3(-1, 0, -1), 0.4, m_bubble));
  world.insert(make_shared<sphere>(vec3(0, 0, -1), 0.5, m_center));
  world.insert(make_shared<sphere>(vec3(1, 0, -1), 0.5, m_right));

  camera.lookfrom = vec3(0, 0, 0);
  camera.lookat = vec3(0, 0, -1);
  camera.v_fov = 90;
  camera.focus_dist = 1;
  camera.defocus_angle = 0.6;
}

void scene_3(scene_edit &world, camera_desc &camera)
{
  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_left = make_shared<glass>(color(1, 1, 1), 1.5);
  auto m_bubble = make_shared<glass>(color(1, 1, 1), 1 / 1.5);
  auto m_center = make_shared<lambert>(color(0.1, 0.2, 0.5));
  auto m_right = make_shared<metal>(color(0.7, 0.7, 0.7), 0.05);

  world.insert(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));
  world.insert(make_shared<sphere>(vec3(-1, 0, -1), 0.5, m_left));
  world.insert(make_shared<sphere>(vec3(-1, 0, -1), 0.4, m_bubble));
  world.insert(make_shared<sphere>(vec3(0, 0, -1), 0.5, m_center));
  world.insert(make_shared<sphere>(vec3(1, 0, -1), 0.5, m_right));

  camera.lookfrom = vec3(-2, 2, 1);
  camera.lookat = vec3(0, 0, -1);
  camera.focus_dist = 4;
  camera.defocus_angle = 0.6;
  camera.v_fov = 20;
}

void scene_4(scene_edit &world, camera_desc &desc)
{
  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_left = make_shared<glass>(color(1, 1, 1), 1.5);
  auto m_bubble = make_shared<glass>(color(1, 1, 1), 1 / 1.5);
  auto m_center = make_shared<lambert>(color(0.1, 0.2, 0.5));
  auto m_right = make_shared<metal>(color(0.7, 0.7, 0.7), 0.05);

  world.insert(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));
  world.insert(make_shared<sphere>(vec3(-1, 0, -1), 0.5, m_left));
  world.insert(make_shared<sphere>(vec3(-1, 0, -1), 0.4, m_bubble));
  world.insert(make_shared<sphere>(vec3(0, 0, -1), 0.5, m_center));
  world.insert(make_shared<sphere>(vec3(1, 0, -1), 0.5, m_right));

  desc.lookfrom = vec3(0, 0, 3);
  desc.lookat = vec3(0, 0, -1);
  desc.proj_type = projection::orthographic;
  desc.ortho_half_height = 1.2;
  desc.defocus_angle = 0; // no DoF under ortho
}

void scene_5(scene_edit &world, camera_desc &desc)
{
  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_a = make_shared<lambert>(color(0.8, 0.3, 0.2));
  auto m_b = make_shared<metal>(color(0.8, 0.8, 0.9), 0.02);
  auto m_c = make_shared<glass>(color(1, 1, 1), 1.5);

  auto unit = make_shared<sphere>(point3(0, 0, 0), 1.0, m_a);
  auto unit_b = make_shared<sphere>(point3(0, 0, 0), 1.0, m_b);
  auto unit_c = make_shared<sphere>(point3(0, 0, 0), 1.0, m_c);

  world.insert(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));
  world.insert(make_shared<instance>(unit, scale(vec3(1.2, 0.35, 0.6)) * rotate(vec3(0, 0, 1), 30) *
                                               translate(vec3(-1.3, 0, -1.2))));
  world.insert(make_shared<instance>(unit_b, scale(vec3(0.5, 1.4, 0.5)) * rotate(vec3(1, 0, 0), -25) *
                                                 translate(vec3(0.3, 0.2, -1.6))));
  world.insert(make_shared<instance>(unit_c, scale(vec3(0.6, 0.6, 0.6)) * translate(vec3(1.4, 0.1, -1.1))));
  world.insert(make_shared<instance>(unit, scale(vec3(100, 0.01, 100)) *
                                               translate(vec3(0, -0.5, -1)))); // extreme scale
  world.insert(make_shared<instance>(unit, scale(vec3(1, 0, 1)) *
                                               translate(vec3(0, 0.8, -1)))); // singular: must vanish

  rng gen(7);
  for (int i = 0; i < 200; i++)
  {
    world.insert(make_shared<instance>(
        unit_b, scale(0.06) * rotate(vec3(0, 1, 0), gen.uniform(0, 360)) *
                    translate(vec3(gen.uniform(-3, 3), gen.uniform(-0.4, 0.6), gen.uniform(-4, -0.6)))));
  }

  desc.lookfrom = vec3(0, 0.6, 1.2);
  desc.lookat = vec3(0, 0, -1.2);
  desc.v_fov = 60;
  desc.focus_dist = 2.2;
  desc.defocus_angle = 0;
}

void scene_6(scene_edit &world, camera_desc &desc)
{
  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_tri = make_shared<lambert>(color(0.1, 0.2, 0.5));
  auto m_behind = make_shared<metal>(color(0.8, 0.5, 0.2), 0.15);

  world.insert(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));

  // Upright isoceles triangle sitting just above the ground in the z = -1
  // plane. Wound counter-clockwise as seen from +z, so cross(p1-p0, p2-p0)
  // points at the camera.
  const vec3 n_face(0, 0, 1);
  world.insert(make_triangle(vertex{vec3(-0.8, -0.4, -1), n_face}, vertex{vec3(0.8, -0.4, -1), n_face},
                             vertex{vec3(0.0, 0.9, -1), n_face}, m_tri));

  // Off to the side and behind the triangle's plane: should stay visible past
  // the triangle's edge, and gives the flat face something to be measured
  // against for depth.
  world.insert(make_shared<sphere>(vec3(-1.5, -0.1, -2.2), 0.4, m_behind));

  desc.lookfrom = vec3(0.9, 0.55, 2.4);
  desc.lookat = vec3(0, 0.1, -1);
  desc.v_fov = 38;
  desc.focus_dist = 3.55;
  desc.defocus_angle = 0;
}

// The icosphere asset, in scene_2's camera minus the defocus blur: the mesh
// lands exactly where that scene's centre sphere is, same material, so a
// smooth-shaded mesh can be measured against the sphere it approximates.
void scene_7(scene_edit &world, camera_desc &desc)
{
  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_body = make_shared<lambert>(color(0.1, 0.2, 0.5));

  world.insert(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));

  obj_load_result loaded = load_obj(std::string(TRACER_ASSET_DIR) + "/icosphere.obj", m_body);
  if (!loaded.error.empty())
  {
    std::clog << "obj: " << loaded.error << "\n";
  }

  // One instance per mesh, never one per face: the ray is transformed once for
  // the whole mesh. Row-vector convention, so the scale happens first.
  for (const shared_ptr<mesh> &m : loaded.meshes)
  {
    world.insert(make_shared<instance>(m, scale(0.5) * translate(vec3(0, 0, -1))));
  }

  desc.lookfrom = vec3(0, 0, 0);
  desc.lookat = vec3(0, 0, -1);
  desc.v_fov = 90;
  desc.focus_dist = 1;
  desc.defocus_angle = 0;
}

inline void load_scene(int i, scene &world, camera_desc &camera)
{

  scene_edit edit = world.edit();
  switch (i)
  {
  case 0: scene_1(edit, camera); break;
  case 1: scene_2(edit, camera); break;
  case 2: scene_3(edit, camera); break;
  case 3: scene_4(edit, camera); break;
  case 4: scene_5(edit, camera); break;
  case 5: scene_6(edit, camera); break;
  case 6: scene_7(edit, camera); break;
  }
}

