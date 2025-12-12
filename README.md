# Ray Tracing In One Weekend!

This is my repo for following along to [Ray Tracing In One Weekend](https://raytracing.github.io/) by Peter Shirley, Trevor David Black, and Steve Hollasch.

## Setup

Dependencies

- cmake
- gcc

1. cd into a project directory (ppm-generator, tracer, ...)
2. `cmake -B build`
3. `ninja -C build`
4. `"build/{PROJECT}.exe"`
5. That's it!

When directing output from .exe to a .ppm image, some terminals will have issues with encoding. Try using this:

`build/{PROJECT}.exe | set-content image.ppm -encoding String`

---

## C++ Notes

### clangd

clangd requires you to generate `compile_commands.json` to work properly. Make sure to have `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)` in your `CMakeLists.txt`.

If you add the `set` command after having run `cmake`. Delete your build files and run `cmake` again.

If you are using ninja and the `compile_commands.json` still hasn't been generated, delete your build files and use this `cmake` command:
`cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CONFIGURATION_TYPES=Debug -G "Ninja Multi-Config" -B build .` 

If mason lsp can't see the c++ standard libraries, [Change mason-lsp to use clangd from msys32](https://github.com/clangd/clangd/issues/2088)

## Progress Notes

12/11 9pm

- remove nanobench and benchmark config :skull:
- just using perf and release config from ipynb to generate benchmark data
- TODO: separate scene config, from render settings, from program args. maybe do config files for scene/render settings so ipynb can just swap them out

12/9 6:48pm

- added benchmark config to cmake (compiler define for code to conditionally compile)
- started benchmark.py to collect benchmark runs with different settings and analyze data
- TODO: benchmark needs to parse the output and run it with the different setting combinations

12/6 1:21pm

- added test scenes from text book
- cmd line argument to select scene to render 
- TODO: benchmarking script! (add nanobenchmark?)

12/2

- added timing code and openmp for parallelizing rendering

11/29 11:07am

- finished section 12
- positionable camera

11/28 9:18am

- finished section 11
- dialectric material (glass)

11/26 and 11/27

- finished section 7, 8, 9, and 10!
- spun out materials: lambert and metal!
- anti-aliasing
- spun out camera.h from main.cpp 
- I had accidentally barely changed the camera initialization and it took forever to find and fix T_T

11/25 4:11pm

- finish section 6
- abstracted sphere into hittable interface
- extended hittable into a list for the whole world!

11/23 8:15pm

- finished section 5
- (still haven't figured out clangd T_T)

11/21 2:26pm

- almost finished section 5
- become lazyvim pilled (and get sucked down the clangd rabbit hole)

11/20 5:20pm

- finished section 4
- setup camera for main loop (renders a skybox) and tracer/ray.h

11/20 9:46am

- finished section 3
- render is the same but tracer/vec3.h and color.h

11/19 9am

- sections 1, 2
- environment setup and basic image renderer in ppm-generator/main.cpp
