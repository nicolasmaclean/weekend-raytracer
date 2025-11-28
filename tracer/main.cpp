#include "camera.h"
#include "hittable_list.h"
#include "material.h"
#include "sphere.h"
#include "tracer.h"

int main()
{
  // Setup up the scene
  auto m_ground = make_shared<lambert>(color(0.5, 0.5, 0.5));
  auto m_left = make_shared<metal>(color(0.8, 0.8, 0.8), .3);
  auto m_center = make_shared<lambert>(color(0.1, 0.2, 0.5));
  auto m_right = make_shared<metal>(color(0.8, 0.6, 0.2), .8);

  hittable_list world;
  world.add(make_shared<sphere>(vec3(0, 0, -1.2), 0.5, m_center));
  world.add(make_shared<sphere>(vec3(0, -100.5, -1), 100, m_ground));
  world.add(make_shared<sphere>(vec3(-1, 0, -1), 0.5, m_left));
  world.add(make_shared<sphere>(vec3(1, 0, -1), 0.5, m_right));

  // render!
  camera camera;
  camera.render(world);
}
