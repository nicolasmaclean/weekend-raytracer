#include "camera.h"
#include "hittable_list.h"
#include "material.h"
#include "sphere.h"
#include "tracer.h"
#include <memory>

int main()
{
  // Setup up the scene
  auto m_ground = make_shared<lambert>(color(0.6, 0.6, 0.6));
  auto m_glass = make_shared<glass>(color(1, 1, 1), 1.5);
  auto m_lam = make_shared<lambert>(color(0.4, 0.2, 0.1));
  auto m_metal = make_shared<metal>(color(0.7, 0.6, 0.5), 0);

  // hittable_list world;
  hittable_list world;
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

  camera camera;
  camera.width_px = 1200;
  camera.lookfrom = vec3(13, 2, 3);
  camera.lookat = vec3(0, 0, 0);
  camera.defocus_angle = 0.6;
  camera.focus_dist = 10;
  camera.v_fov = 20;
  camera.aa_samples_per_pixels = 100;
  camera.max_bounces = 20;

  // render!
  camera.render(world);
}
