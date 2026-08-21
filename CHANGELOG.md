# Change Log

8/21/26

- made roadmap: putting optimization to the side for now in favor of front-end work
- the ultimate goal for now is to make work as a hydra delegate so it can be used in any DCC!
- render to framebuffer (cli will still output to ppm)

12/11/26 9pm

- remove nanobench and benchmark config :skull:
- just using perf and release config from ipynb to generate benchmark data
- TODO: separate scene config, from render settings, from program args. maybe do config files for scene/render settings so ipynb can just swap them out

12/9/26 6:48pm

- added benchmark config to cmake (compiler define for code to conditionally compile)
- started benchmark.py to collect benchmark runs with different settings and analyze data
- TODO: benchmark needs to parse the output and run it with the different setting combinations

12/6/26 1:21pm

- added test scenes from text book
- cmd line argument to select scene to render 
- TODO: benchmarking script! (add nanobenchmark?)

12/2/26

- added timing code and openmp for parallelizing rendering

11/29/26 11:07am

- finished section 12
- positionable camera

11/28/26 9:18am

- finished section 11
- dialectric material (glass)

11/26/26 and 11/27/26

- finished section 7, 8, 9, and 10!
- spun out materials: lambert and metal!
- anti-aliasing
- spun out camera.h from main.cpp 
- I had accidentally barely changed the camera initialization and it took forever to find and fix T_T

11/25/26 4:11pm

- finish section 6
- abstracted sphere into hittable interface
- extended hittable into a list for the whole world!

11/23/26 8:15pm

- finished section 5
- (still haven't figured out clangd T_T)

11/21/26 2:26pm

- almost finished section 5
- become lazyvim pilled (and get sucked down the clangd rabbit hole)

11/20/26 5:20pm

- finished section 4
- setup camera for main loop (renders a skybox) and tracer/ray.h

11/20/26 9:46am

- finished section 3
- render is the same but tracer/vec3.h and color.h

11/19/26 9am

- sections 1, 2
- environment setup and basic image renderer in ppm-generator/main.cpp
