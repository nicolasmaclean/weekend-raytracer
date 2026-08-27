#include <SDL3/SDL.h>
#include <iostream>
#include <cstdlib>
#include <algorithm>

#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include "camera.h"
#include "hittable_list.h"
#include "render_buffer.h"
#include "render_control.h"
#include "renderer.h"
#include "example_scenes.h"
#include "schedulers.h"


struct viewer_control : render_control
{
  std::atomic<bool> stop { false };
  std::atomic<bool> pause { false };

  bool is_stop_requested() const override { return stop.load(); }
  bool is_pause_requested() const override { return pause.load(); }
};

bool blit_buffer_to_texture(SDL_Texture *texture, const render_buffer &buffer)
{
  void *raw = nullptr;
  int pitch = 0;

  if (!SDL_LockTexture(texture, nullptr, &raw, &pitch))
  {
    return false;
  }

  for (int y = 0; y < buffer.height(); y++)
  {
    // pitch is BYTES per row and may exceed width*4 (alignment padding),
    // so step in bytes and only then reinterpret
    uint32_t *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(raw) + y * pitch);
    const int by = buffer.height() - 1 - y;

    // convert color to pixel directly into the render texture!
    for (int x = 0; x < buffer.width(); x++)
    {
      float rgba[4];
      buffer.read(x, by, 4, rgba);
      int r, g, b;
      color_to_rgb8(color(rgba[0], rgba[1], rgba[2]), r, g, b);
      row[x] = (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
  }

  SDL_UnlockTexture(texture);
  return true;
}

int main(int argc, char *argv[])
{
  if (!SDL_Init(SDL_INIT_VIDEO)) {     
    std::clog << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }

  SDL_Window *window = nullptr;
  SDL_Renderer *sdl_renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("viewer", 640, 360, SDL_WINDOW_RESIZABLE, &window, &sdl_renderer)) {
    std::clog << "SDL_CreateWindowAndRenderer: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  // prepare scene 
  int i_scene = argc > 1 ? atoi(argv[1]) : 1;
  hittable_list world;
  camera_desc desc;
  load_scene(i_scene, world, desc);

  const int render_width = 400, render_height = 225;
  camera cam = desc.build(render_width, render_height);

  // prepare renderer and buffer 
  struct renderer r;
  r.max_bounces = 20;
  r.samples_to_converge = 10000;
  r.schedule = tbb_schedule;

  render_buffer buffer;
  aov_bindings aovs = {
    allocate_aov(buffer, aov::color, render_width, render_height)
  };

  // setup start, pause, and stop controls
  viewer_control control;
  render_stats stats;
  std::thread render_thread;

  auto stop_render = [&]()
  {
    control.stop.store(true);
    if (render_thread.joinable())
    {
      render_thread.join();
    }
  };

  auto start_render = [&]()
  {
    stop_render();
    buffer.clear(4, default_aov_descriptor(aov::color).clear_value);
    mark_unconverged(aovs);
    control.stop.store(false);
    control.pause.store(false);
    render_thread = std::thread([&]() { stats = r.render(cam, world, aovs, &control); });
  };

  // prepare render texture
  SDL_Texture *texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, render_width, render_height);
  SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
  SDL_SetRenderLogicalPresentation(sdl_renderer, render_width, render_height, SDL_LOGICAL_PRESENTATION_LETTERBOX);

  // clear screen to gray color
  std::clog << "Opening window..." << std::endl;
  SDL_SetRenderDrawColor(sdl_renderer, 20, 20, 20, 255);  // r, g, b, a
  SDL_RenderClear(sdl_renderer);
  SDL_RenderPresent(sdl_renderer);

  // have render start immediately
  start_render();
  std::clog << "Rendering scene " << i_scene << std::endl;

  int shown_samples = -1;
  bool running = true;
  while (running)
  {
    // check user input 
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_EVENT_QUIT)
      {
        running = false;
      }
      if (event.type == SDL_EVENT_KEY_DOWN)
      {
        switch (event.key.key) {
          case SDLK_ESCAPE:
          {
            running = false;
            break;
          }
          case SDLK_SPACE:
          {
            control.pause.store(!control.pause.load());
            break;
          }
          case SDLK_R:
          {
            start_render();
            break;
          }
        }
      }
    }

    // upload render buffer to screen
    buffer.resolve();
    if (!blit_buffer_to_texture(texture, buffer))
    {
      std::clog << "Failed to blit buffer to texture: could not lock texture." << std::endl;
    }

    // upload render texture
    SDL_RenderClear(sdl_renderer);
    SDL_RenderTexture(sdl_renderer, texture, nullptr, nullptr);  // src rect, dst rect
    SDL_RenderPresent(sdl_renderer);

    const int samples = r.completed_samples();
    if (samples != shown_samples)
    {
      shown_samples = samples;
      std::string title = "viewer - " + std::to_string(samples) + "/" +
                          std::to_string(r.samples_to_converge) + " samples";
      if (control.pause.load()) title += " (paused)";
      if (buffer.is_converged()) title += " (converged)";
      SDL_SetWindowTitle(window, title.c_str());
    }

    SDL_Delay(16);
  }

  stop_render();
  std::clog << "Stopped after " << stats.completed_samples << " samples in " << stats.ms / 1000.0
            << "s" << (stats.stopped ? " (interrupted)" : "") << std::endl;

  std::clog << "Closing window" << std::endl;
  SDL_DestroyTexture(texture); 
  SDL_DestroyRenderer(sdl_renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
