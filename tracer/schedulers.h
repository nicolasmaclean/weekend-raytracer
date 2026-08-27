#pragma once

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>


inline void tbb_schedule(size_t n, const std::function<void(size_t, size_t)> &work)
{
  tbb::parallel_for(
      tbb::blocked_range<size_t>(0, n), [&](const tbb::blocked_range<size_t> &r) { 
        work(r.begin(), r.end()); 
      }
  );
}
