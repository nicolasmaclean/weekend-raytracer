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

---

## Progress Notes

11/20 5:20pm

- finished section 4
- setup camera for main loop (renders a skybox) and tracer/ray.h

11/20 9:46am

- finished section 3
- render is the same but tracer/vec3.h and color.h

11/19 9am

- sections 1, 2
- environment setup and basic image renderer in ppm-generator/main.cpp
