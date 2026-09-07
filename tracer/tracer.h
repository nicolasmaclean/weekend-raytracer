// SPDX-FileCopyrightText: 2025-2026 Nick Maclean
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>

using std::make_shared;
using std::shared_ptr;
using std::unique_ptr;

constexpr double infinity = std::numeric_limits<double>::infinity();
constexpr double pi = 3.1415926535897932385;

inline double degrees_to_radians(double degrees)
{
  return degrees * pi / 180.0;
}

#include "rng.h"
#include "color.h"
#include "interval.h"
#include "ray.h"
#include "vec3.h"

