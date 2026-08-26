## Ideas

Wrapping my CPU ray tracer as a hydra delegate

---
## Draft

My ray tracer doesn't decide how many samples to draw per frame. The clock does.

I wrapped the tracer in an SDL window so I could watch a render converge instead of waiting on a file. The obvious version of that loop is: trace one sample per pixel, push it to the screen, repeat. It works, and it's wrong in both directions. On a cheap scene you spend most of your time uploading pixels instead of tracing them. On an expensive one, a single sample takes longer than a frame, and the window stops answering — you can't even close it.

So the loop measures itself. It keeps an exponential moving average of what one sample per pixel actually costs, divides the frame budget (40ms) by that, and renders as many samples as it can per frame. Cheap scene, big batches, no wasted uploads. Expensive scene, one sample at a time, window still responsive. Same code either way, and there's no number to tune per scene.

What makes that legal is the frame buffer. It holds summed color plus a per-pixel sample count and calculates the average on read, so "add eight more samples to the image you already have" is a valid operation at any moment.

800x450, 1 to 1000 samples per pixel. About nine minutes of render, sped up.

**Media:** `docs/socials/media/progressive/progressive_scene0.mp4` (8.3s, 800x450, sample counter + progress bar)

---

### Alternate hooks

1. "My ray tracer doesn't decide how many samples to draw per frame. The clock does." *(current)*
2. "The hard part of showing a render while it renders isn't the rendering. It's not freezing the window."
3. "I gave my ray tracer a 40 millisecond budget and let it figure out the rest."

### Alternate closers

A. Unexpected payoff — bugs are visible at 1 spp — plus a one-line camera-API next step.
B. Deflationary: "This isn't a renderer app, it's a test harness with a window on it. But it beats writing a .ppm and opening it in a picture viewer to find out I broke the camera again."
C. Circular: "Honestly, the 40ms budget isn't my favorite part. My favorite part is that I don't open an image viewer anymore."
D. Next step only: "Next up is the camera API, so I can move around the scene while it's still converging."
E. *(current)* No closer — end on the spec line.

---

## Posted

8/26/26

I made my C++ raytracer 5x faster by changing one type.

My hit record was holding its material as a `shared_ptr`. Every raycast would copy it, triggering an atomic refcount bump, millions per frame, all on the same few materials. When multiple threads want the same material, that bump makes them queue up and wait their turn to update the counter. Costs nothing single threaded, which is why it hid so long. With every core in the render loop, it was most of my frame time.

One line fixed it: `shared_ptr<material>` became `const material *`. The hit record doesn't need to own the material, the scene does. Now every thread can copy and read the pointer without synchronization.

![[render_and_table.png]]

> [!note]- Raw idea
> Switched from OpenMP to OneTBB (to better match embree/hyrda) and found shared_ptr in multi-threading context was destroying performance. Switched to const \* and got a 4x speed up. Noticed this issue when testing for best tile size in the multi-threading code. tile size had seemingly no impact on render time because shared_ptr cause coping/mutex locks that ate any performance speed ups.
>
> 400x225px, 50 samples, 12 thread, 5 runs
> | Method           | Median runtime |
| ---------------- | -------------- |
| shared_ptr       | 1377ms         |
| const material * | 245ms          |
>
> 30 samples, 12 threads, 7 runs
>
>| Tile size | shared_ptr | const material * | ratio |
| --------- | ---------- | ---------------- | ----- |
| 1         | 833ms      | 147ms            | 5.67x |
| 2         | 837ms      | 148ms            | 5.65x |
| 4         | 836ms      | 145ms            | 5.78x |
| 8         | 825ms      | 144ms            | 5.74x |
| 16        | 820ms      | 148ms            | 5.53x |
| 32        | 861ms      | 160ms            | 5.39x |
| 64        | 903ms      | 176ms            | 5.13x |
| 128       | 1123ms     | 288ms            | 3.89x |

---

I’ve been putting together some benchmarking tools for my raytracer (I’ll share those soon), but I couldn’t resist trying out OpenMP in the meantime.  
  
I tossed a simple `#pragma omp parallel` before the render loop and render time dropped by about 80%. Wild how much mileage you can get from a one-liner when the workload is super parallel-friendly.  
  
Next up, I’m planning to experiment with SIMD for ray generation, then compare OpenMP against a custom multi-threading model and OpenBLAS once I’ve got proper benchmarks.

(video of bechmark.ipyn and omp code, too lazy to find the video file)

---

📚 Progress update: I just finished working through "Ray Tracing in One Weekend" ([https://lnkd.in/gS-Rczyg](https://lnkd.in/gS-Rczyg)) building a simple ray tracer from scratch without any external graphics API!  
  
What I love about this book is how bite-sized steps build up fast: you go from drawing a simple skybox to building a path tracer that can render spheres with realistic materials, reflections, and refractions.  
  
Now that the basics are in place, I’m looking at adding multi-threading and using bounding-volume hierarchy (BVH) to significantly improve performance and scalability (especially as scene complexity grows).  
  
If you’ve got ideas for other features or optimizations, I’d love to hear your suggestions!

(timelapse video of features being developed, too lazy to find the video file)