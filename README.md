# Ray Tracing In One Weekend!

This is my repo for following along to [Ray Tracing In One Weekend](https://raytracing.github.io/) by Peter Shirley, Trevor David Black, and Steve Hollasch.

## Setup

Dependencies

- cmake
- gcc

1. cd into tracer/
2. use `debug.sh` or `release.sh` to use the cli

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

