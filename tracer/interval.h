#pragma once

#include "tracer.h"

class interval {
public:
  double min, max;

  interval() : min(-infinity), max(infinity) {}
  interval(double min, double max) : min(min), max(max) {}

  double size() { return max - min; }

  double contains(double t) { return min <= t && t <= max; }

  double surrounds(double t) { return min < t && t < max; }

  static const interval empty, universe;
};

const interval interval::empty = interval(infinity, -infinity);
const interval interval::universe = interval(-infinity, infinity);
