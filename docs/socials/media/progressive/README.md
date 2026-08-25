# progressive render — scene 0

What the SDL viewer shows while it accumulates, rendered headless and sped up
so it fits a social post. Scene 0 at 800x450, 1 → 1000 samples per pixel,
104 frames, ~535s of actual render time compressed to 8.3s.

| file | what |
| --- | --- |
| `progressive_scene0.mp4` | 8.3s, 800x450, 1.3 MB, H.264. Sample counter + progress bar. **Post this one.** |
| `progressive_scene0_clean.mp4` | Same timing, no overlay |
| `progressive_scene0.gif` | 500px wide, 2.4 MB — under the LinkedIn and X limits |
| `stills/spp_*.png` | 1, 4, 16, 65, 261, 1000 spp — for a carousel post |
| `png/` | the full 104-frame sequence, clean, no overlay (46 MB — delete if you don't want it) |

Frames are spaced by sample count on a dense-then-geometric schedule, and the
first 20 frames are held longer, so the early collapse in noise reads instead
of flashing past. The clip holds 2s on the finished image.

These are build artifacts, not source. Nothing in the tracer was changed to
produce them — the generating scripts were deliberately kept out of the repo.
