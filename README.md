# Weekend Raytracer

This started by following [Ray Tracing In One Weekend](https://raytracing.github.io/) by Peter Shirley, Trevor David Black, and Steve Hollasch.

At this point I've broken away from that textbook series and have been researching and adding features on my own:
- triangles and triangle meshes
- bvh for O(log n) scene search, as opposed to O(n) linear searches
- support for USD Hydra
- a standalone SDL app for interactive rendering.

The same header-only tracer is intended to run one of two ways.

1. **tracer_cli**: a command-line program for rendering scenes built in C++ to `.ppm` files.
2. **Blender add-on**: the tracer as a Hydra render delegate (`hdWeekend`), `Weekend` render engine in Blender 4.5 LTS. 

Note: for now, only Linux is supported.

---

## tracer_cli

### Dependencies

- cmake ≥ 3.15
- gcc (C++17)
- ninja (scripts use `Ninja Multi-Config`)

SDL3, oneTBB, and tinyobjloader are fetched by CMake at build time.

### Build and run

```bash
cd tracer/
./release.sh [scene]  # release build, renders to tracer/image.ppm and opens it
./debug.sh   [scene]  # same, Debug config
```

See `tracer/example_scenes.h` (default `0`) for available scenes to render. You can also run the binary directly:

```bash
../build/tracer/Release/tracer_cli [scene] [single_thread] [width] [height] > image.ppm
# defaults: scene 0, multithreaded, 400x225. Set single_thread to 1 to disable multithreading.
```

There's also a live SDL window: `viewer/debug.sh [scene]`

---

## Blender add-on (Blender 4.5 LTS, Linux)

### Dependencies

- cmake, gcc, git-lfs
- Blender **4.5.13** official tarball (not snap or apt)
- Blender's precompiled SDK (`lib-linux_x64`, `blender-v4.5-release` branch)
- optional, for the headless smoke test: `uv` (installs Python 3.11 and `bpy==4.5.13`)

### One-time setup

```bash
sudo apt install git-lfs && git lfs install

# download the SDK: only usd, tbb, and python headers
mkdir -p ~/opt && cd ~/opt
git clone --filter=blob:none --no-checkout \
  https://projects.blender.org/blender/lib-linux_x64.git -b blender-v4.5-release
cd lib-linux_x64 && git sparse-checkout set usd tbb python/include && git checkout
cd ~/opt

# Download/install Blender tarball
curl -O https://download.blender.org/release/Blender4.5/blender-4.5.13-linux-x64.tar.xz
tar xf blender-4.5.13-linux-x64.tar.xz
```

The paths in `env.sh` assume `~/opt`. Edit `BLENDER_LIB_DIR` / `BLENDER_BIN` if you put these elsewhere.

### Build the delegate

From the repo root:

```bash
source env.sh
cmake -S hydra -B build-hydra-blender -DHDW_USD_FLAVOR=blender \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=build-hydra-blender/install
cmake --build build-hydra-blender -j && cmake --install build-hydra-blender
```

If the SDK's USD namespace isn't the one `env.sh` expects, CMake stops with an error.

> **Always launch Blender through `$BLENDER` (`blender/blender.sh`), never the binary directly**.
> Once you've sourced `env.sh`, non-blender USD builds will be in `LD_LIBRARY_PATH` that will 
> cause Blender to crash on start with `undefined symbol: _ZTIN17MaterialX_v1_39_27ElementE`.

### Option A: install the zip

```bash
./blender/build_zip.sh
$BLENDER --command extension install-file --repo user_default -e dist/weekend_raytracer-*.zip
$BLENDER
```

In Blender, turn on the add-on under preferences -> Add-ons (search "Weekend"). Then choose Render properties -> Render engine -> **Weekend**, and use F12 to render an image or use renderer viewport shading.

The zip is built against this machine's glibc, so treat it as a personal install, not a release build.


### Option B: dev install (symlink, Python hot reload)

```bash
sed -e "s|@BLENDER_VERSION_MIN@|4.5.0|" -e "s|@BLENDER_VERSION_MAX@|5.0.0|" -e "s|@PLATFORMS@|linux-x64|" \
    blender/weekend_raytracer/blender_manifest.toml.in > blender/weekend_raytracer/blender_manifest.toml

ln -sfn "$PWD/blender/weekend_raytracer" ~/.config/blender/4.5/extensions/user_default/weekend_raytracer
export HDW_PLUGIN_DIR="$PWD/build-hydra-blender/install/plugin/usd"
$BLENDER
```

Python hot reloads on add-on reload. A rebuilt `hdWeekend.so` only loads after a Blender restart.

Before testing the zip, delete this symlink and `unset HDW_PLUGIN_DIR`.

### Headless smoke test

```bash
uv venv --python 3.11 .venv-bpy45
uv pip install --python .venv-bpy45/bin/python bpy==4.5.13
HDW_PLUGIN_DIR=$PWD/build-hydra-blender/install/plugin/usd ./blender/bpy.sh blender/smoke_test.py
```

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
