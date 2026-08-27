#pragma once

#include <functional>

#include "mat4.h"


// USD-free stand-in for HdRenderThread's cancellation functionality
struct render_control
{
  virtual ~render_control() = default;
  virtual bool is_stop_requested() const { return false; }
  virtual bool is_pause_requested() const { return false; }
};


// render loop takes a pointer to scheduler so caller can inject multithreadin
// logic or use the singlethreaded default
using tile_scheduler = std::function<
  void(
    size_t n, 
    const std::function<void(size_t, size_t)> &work
  )
>;

inline void singlethread_schedule(size_t n, const std::function<void(size_t, size_t)> &work)
{
  work(0,n);
}


struct tile_grid
{
  rect2i window;
  int tile_size = 8;
  int tiles_x = 0, tiles_y = 0;

  tile_grid() = default;
  tile_grid(const rect2i &w, int size) : window(w), tile_size(size < 1 ? 1 : size)
  {
    tiles_x = (w.width() + tile_size - 1) / tile_size;
    tiles_y = (w.height() + tile_size - 1) / tile_size;
  }

  size_t count() const
  {
    return size_t(tiles_x < 0 ? 0 : tiles_x) * size_t(tiles_y < 0 ? 0 : tiles_y);
  }

  rect2i tile(size_t i) const
  {
    const int ty = int(i) / tiles_x;
    const int tx = int(i) - ty * tiles_x;
    const int x0 = window.min_x + tx * tile_size;
    const int y0 = window.min_y + ty * tile_size;
    return {x0, y0, std::min(x0 + tile_size - 1, window.max_x), std::min(y0 + tile_size - 1, window.max_y)}; 
  }
};

