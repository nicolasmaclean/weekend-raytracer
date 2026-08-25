
Progressive rendering? (and sdl)

Switched from OpenMP to OneTBB (to better match embree/hyrda) and found shared_ptr in multi-threading context was destroying performance. Switched to const \* and got a 4x speed up. Noticed this issue when testing for best tile size in the multi-threading code. tile size had seemingly no impact on render time because shared_ptr cause coping/mutex locks that ate any performance speed ups.

Building toward hydra delegate?


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