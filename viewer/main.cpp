#include <SDL3/SDL.h>
#include <iostream>
#include <cstdlib>

#include "SDL3/SDL_render.h"
#include "SDL3/SDL_video.h"
#include "camera.h"
#include "framebuffer.h"
#include "hittable_list.h"
#include "example_scenes.h"


void render_to_buffer(int i_scene, framebuffer &buffer)
{
  // configure scene/camera
  camera camera;
  hittable_list world;
  load_scene(i_scene, world, camera);

  // prepare frame buffer
  buffer.allocate(camera.width_px, camera.height_px);

  // render!
  std::clog << "Rendering scene " << " " << i_scene << "..." << std::flush;
  double renderDuration;
  renderDuration = camera.render_openmp(world, buffer);
  
  // output finish message
  auto per_pixel = renderDuration / (double(camera.height_px) * camera.width_px);
  std::clog << "\rRendered scene " << i_scene << " in " << renderDuration / double(1000) << "s ("
            << per_pixel << "ms/px)                       \n"
            << std::flush;
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
    const color *src = &buffer.pixels[y * buffer.width];

    // convert color to pixel directly into the render texture!
    for (int x = 0; x < buffer.width; x++) {
      int r, g, b;
      color_to_rgb8(src[x], r, g, b);
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
  bool rendered = false;

  // clear screen to gray color
  std::clog << "Opening window..." << std::endl;
  SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);  // r, g, b, a
  SDL_RenderClear(renderer);
  SDL_RenderPresent(renderer);

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
    if (!rendered)
    {
      render_to_buffer(0, buffer);
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
    
      rendered = true;
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
