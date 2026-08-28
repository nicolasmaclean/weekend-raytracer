## Ideas

Wrapping my CPU ray tracer as a hydra delegate

---
## Draft



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