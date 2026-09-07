// SPDX-FileCopyrightText: 2025-2026 Nick Maclean
// SPDX-License-Identifier: GPL-3.0-or-later

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

