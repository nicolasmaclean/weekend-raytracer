#include <SDL3/SDL.h>
#include <iostream>
#include <cstdlib>
#include <algorithm>

#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include "camera.h"
#include "framebuffer.h"
#include "hittable_list.h"
#include "example_scenes.h"


double render_to_buffer(int i_scene, framebuffer &buffer, int samples)
{
  // configure scene/camera
  camera camera;
  hittable_list world;
  load_scene(i_scene, world, camera);

  // prepare frame buffer
  buffer.allocate(camera.width_px, camera.height_px);

  // render!
  std::clog << "Rendering scene " << " " << i_scene << "..." << std::flush;
  double renderDuration = camera.render(world, buffer, samples);
  
  // output finish message
  auto per_pixel = renderDuration / (double(camera.height_px) * camera.width_px);
  std::clog << "\rRendered scene " << i_scene << " (" << samples << " samples) in " << renderDuration / double(1000) << "s ("
            << per_pixel << "ms/px)                       \n"
            << std::flush;

  return renderDuration;
}

bool blit_buffer_to_texture(SDL_Texture *texture, const framebuffer &buffer)
{
  void *raw = nullptr;
  int pitch = 0;

  if (!SDL_LockTexture(texture, nullptr, &raw, &pitch)) {
    return false;
  }

  for (int y = 0; y < buffer.height; y++) {
    // pitch is BYTES per row and may exceed width*4 (alignment padding),
    // so step in bytes and only then reinterpret
    uint32_t *row = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(raw) + y * pitch);
    const int base = y * buffer.width;

    // convert color to pixel directly into the render texture!
    for (int x = 0; x < buffer.width; x++) {
      int r, g, b;
      color_to_rgb8(buffer.get_pixel(base + x), r, g, b);
      row[x] = (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
  }

  SDL_UnlockTexture(texture);
  return true;
}

int main()
{
  if (!SDL_Init(SDL_INIT_VIDEO)) {     
    std::clog << "SDL_Init: " << SDL_GetError() << "\n";
    return 1;
  }

  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("viewer", 640, 360, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
    std::clog << "SDL_CreateWindowAndRenderer: " << SDL_GetError() << "\n";
    SDL_Quit();
    return 1;
  }

  // reuse frame buffer and render texture 
  framebuffer buffer;
  SDL_Texture *texture = nullptr; 

  // clear screen to gray color
  std::clog << "Opening window..." << std::endl;
  SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);  // r, g, b, a
  SDL_RenderClear(renderer);
  SDL_RenderPresent(renderer);

  const double budget_ms_per_update = 40;
  double ms_per_sample = 0;
  int max_samples = 1000;
  int samples_done = 0;

  bool running = true;
  while (running)
  {
    // check if exit requested
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      if (event.type == SDL_EVENT_QUIT)
        running = false;
      if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
        running = false;
    }

    // render and copy to render texture
    if (samples_done < max_samples)
    {
      int samples_to_do = 1;
      if (ms_per_sample > 0.0)
      {
        samples_to_do = std::clamp(int(budget_ms_per_update / ms_per_sample), 1, 64); 
      }
      samples_to_do = std::min(samples_to_do, max_samples - samples_done); 
      
      double ms = render_to_buffer(1, buffer, samples_to_do);
      samples_done += samples_to_do;

      if (ms > 0)
      {
        double measured = ms / samples_to_do;
        ms_per_sample = (ms_per_sample == 0)
          ? measured
          : 0.7*ms_per_sample + 0.3*measured; // maintain value as an exponential moving average
      }

      if (texture == nullptr || texture->h != buffer.height || texture->w != buffer.width)
      {
        if (texture != nullptr)
        {
          SDL_DestroyTexture(texture);
        }

        texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_XRGB8888,      // format
            SDL_TEXTUREACCESS_STREAMING,   // access
            buffer.width, buffer.height  // render resolution, NOT window size
            );
        SDL_SetRenderLogicalPresentation(renderer, buffer.width, buffer.height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
      }

      if (!blit_buffer_to_texture(texture, buffer))
      {
        std::clog << "Failed to blit buffer to texture: could not lock texture." << std::endl;
      }

      // config render texture
      SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

      if (samples_done >= max_samples)
      {
        std::clog << "Finished rendering " << samples_done << " samples." << std::endl;
      }
    }

    // upload render texture
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, nullptr);  // src rect, dst rect
    SDL_RenderPresent(renderer);
  }

  std::clog << "Closing window" << std::endl;
  SDL_DestroyTexture(texture); 
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
