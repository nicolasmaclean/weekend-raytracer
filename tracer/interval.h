#pragma once

#include "tracer.h"

class interval
{
public:
  double min, max;

  constexpr interval() : min(-infinity), max(infinity) {}
  constexpr interval(double min, double max) : min(min), max(max) {}

  [[nodiscard]] double size() const { return max - min; }

  [[nodiscard]] bool contains(double t) const { return min <= t && t <= max; }

  [[nodiscard]] bool surrounds(double t) const { return min < t && t < max; }

  [[nodiscard]] double clamp(double t) const
  {
    if (t > max) return max;
    if (t < min) return min;
    return t;
  }

  static const interval empty, universe;
};

// must be constexpr so they are defined once per program, not per translation unit
constexpr interval interval::empty = interval(infinity, -infinity);
constexpr interval interval::universe = interval(-infinity, infinity);
