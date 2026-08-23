#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H


#include <vector>
#include "color.h"

struct framebuffer {
  public:
    int width, height;
    int pixel_count;

    std::vector<color> pixels; // top-left origin
    std::vector<int> samples;

    color get_pixel(int i) const
    {
      int samples = this->samples[i];
      return samples == 0 ? this->pixels[i] : this->pixels[i] / samples; 
    }

    void allocate(int width, int height)
    {
      this->width = width;
      this->height = height;
      this->pixel_count = width * height;
      this->pixels.resize(this->pixel_count);
      this->samples.resize(this->pixel_count);
    }

    void reset()
    {
      std::fill(this->pixels.begin(), this->pixels.end(), color(0, 0, 0));
      std::fill(this->samples.begin(), this->samples.end(), 0);
    }
};

#endif

