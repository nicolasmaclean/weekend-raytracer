#pragma once

#include <utility>

#include "mesh.h"


inline shared_ptr<mesh> make_triangle(const vertex &a, const vertex &b, const vertex &c,
                                      shared_ptr<material> material)
{
  auto m = make_shared<mesh>();
  m->add_triangle(a, b, c);
  m->mat = std::move(material);
  return m;
}

