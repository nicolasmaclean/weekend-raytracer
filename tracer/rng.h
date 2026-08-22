#pragma once

#include <cstdint>

// splitmix64 finalizer — used both to mix seeds and to advance the stream
inline uint64_t mix64(uint64_t z)
{
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

struct rng {
  uint64_t state;

  // seed must be well-mixed, not a raw index — see note below
  explicit rng(uint64_t seed) : state(seed) {}

  uint64_t next_u64() { return mix64(state += 0x9E3779B97F4A7C15ull); }

  // 53-bit uniform in [0,1); no distribution object, no locks
  double uniform() { return double(next_u64() >> 11) * 0x1.0p-53; }
  double uniform(double min, double max) { return (max - min) * uniform() + min; }
};

// stream identity: pixel + sample + a per-frame salt for later animation/dithering
inline uint64_t sample_seed(uint64_t pixel_index, uint64_t sample_index, uint64_t frame_seed)
{
  return mix64(mix64(frame_seed + 0x9E3779B97F4A7C15ull + pixel_index) + sample_index);
}
