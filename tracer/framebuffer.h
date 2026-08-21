#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H


#include <vector>
#include "color.h"

struct framebuffer {
  public:
    int width, height;
    std::vector<color> pixels; // top-left origin

    void allocate(int width, int height)
    {
      this->width = width;
      this->height = height;

      int buff_size = width * height;
      this->pixels.resize(buff_size);
    }
};

#endif

