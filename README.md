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

---

## License

Copyright © 2025-2026 Nick Maclean.

The raytracer, the Hydra render delegate and the Blender add-on are licensed
**GPL-3.0-or-later** — see [LICENSE](LICENSE). Source files carry
`SPDX-License-Identifier` headers.

GPL-3 rather than GPL-2 is a requirement, not a preference: OpenUSD (Tomorrow Open
Source Technology License 1.0) and oneTBB (Apache-2.0) both carry trademark and
indemnification clauses that GPL-2 forbids as "further restrictions", and that
GPL-3 §7(e) and §7(f) explicitly permit.

### Exceptions

Not everything in this repository is mine to license:

| Path | License | Notes |
|---|---|---|
| `hydra/{mesh,renderDelegate,renderPass,rendererPlugin}.{cpp,h}` | Tomorrow Open Source Technology License 1.0 | Derived from OpenUSD's `hdTiny`. Upstream notices are intact; my modifications are GPL-3.0-or-later |
| `assets/StandardShaderBall/` | CC BY 4.0 | Academy Software Foundation |
| `assets/OpenChessSet/` | CC BY 4.0 | Academy Software Foundation |
| `assets/ElephantWithMonochord/` | CC BY-SA 4.0 | A.MUSE – Interactive Design Studio |
| `assets/Teapot/` | CC0 | PolyHaven |
| `assets/Kitchen_set/` | **Pixar EULA — not redistributable** | Not committed. Fetch it yourself: see [assets/README.md](assets/README.md) |
| `docs/.obsidian/plugins/`, `docs/.obsidian/themes/` | own licenses | Third-party Obsidian plugins and themes, vendored by the editor |

Dependencies are fetched at build time and are not redistributed here: SDL3 (zlib),
oneTBB (Apache-2.0), tinyobjloader (MIT), nanobench (MIT), OpenUSD (TOST 1.0).
