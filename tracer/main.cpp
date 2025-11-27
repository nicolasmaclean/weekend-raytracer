#include "camera.h"
#include "hittable_list.h"
#include "sphere.h"
#include "tracer.h"

int main()
{
  // Setup up the scene
  hittable_list world;
  world.add(make_shared<sphere>(vec3(0, 0, -1), 0.5));
  world.add(make_shared<sphere>(vec3(0, -100.5, -1), 100));

  // render!
  camera camera;
  camera.render(world);
}
