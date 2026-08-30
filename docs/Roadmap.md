---

kanban-plugin: board

---

## 0.1.0 - "ray tracer in one weekend"

**Complete**
- [x] ppm image rendering
- [x] sphere primitive
- [x] lambert, metal, and glass materials
- [x] OMP multi-threading


## 0.2.0 - hydra prep

- [x] output to pixel buffer
- [x] sdl viewer and make tracer into a library
- [x] thread-safe rng (rng generator is created/seeded for each pixel and sample)
- [x] progressive rendering
- [x] hdTiny stub delegate
- [x] camera api refactor
- [x] render target refactor (prepare framebuffer to work with hydra)
- [x] interruptible tile-driven render loop
	- [x] switch OMP to TBB
- [x] transform support
- [x] scene graph with mutation
- [x] triangle prim
- [x] triangle mesh (and tinyobjloader)
- [x] bvh with rebuild-on-mutation


## 0.3.0 - hydra delegate

- [ ] hydra delegate + usdview
- [ ] blender plugin


## 0.4.0 - more features!

- [ ] texture mapping


## Wishlist

- [ ] profiling tools
- [ ] float/double compile option




%% kanban:settings
```
{"kanban-plugin":"board","list-collapse":[false,false,false,false,false]}
```
%%