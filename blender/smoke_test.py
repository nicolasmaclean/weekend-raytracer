# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later

"""Headless render through hdWeekend, under bpy-as-module.

Reused verbatim by CI ([[ci]] §5). Reads HDW_PLUGIN_DIR, so one script covers the local
build tree and a CI-built .so.

Two bpy-as-module facts shape this script; both were measured, and both reproduce with
stock Cycles, so neither is anything to do with our delegate:

1. `bpy.data.images["Render Result"]` never exposes pixels to Python - it reports
   size (0, 0) and len(pixels) == 0 no matter which engine rendered it. The pixels have
   to come back off disk, so the render writes an EXR and we reload that.
2. `bpy` does not shut down cleanly as a module; the interpreter hangs in a futex at
   teardown, on both the success and the exception path. os._exit() past it.

And one hdWeekend fact: if no AOV is bound (a missing aovToken: key), the render does not
fail, it spins at 100% CPU forever. Nothing below runs, so the assertions cannot catch it -
hence the faulthandler watchdog around the render call.
"""

import faulthandler
import hashlib
import os
import sys
import tempfile
import traceback
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
import weekend_raytracer  # noqa: E402

RES = 64
EXPECTED_FLOATS = RES * RES * 4  # RGBA
# Wall-clock ceiling for the render. A 64x64x8spp frame takes ~0.2s; this only fires on a
# hang. Raise it for CI runners that are slower than they look.
RENDER_TIMEOUT = float(os.environ.get("HDW_SMOKE_TIMEOUT", "120"))


def main() -> None:
    weekend_raytracer.register()

    scene = bpy.context.scene
    scene.render.engine = "WEEKEND"
    scene.render.resolution_x = RES
    scene.render.resolution_y = RES
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "OPEN_EXR"
    # Full float: the digest does its own quantising below, so don't also half-quantise.
    scene.render.image_settings.color_depth = "32"
    # Pin the view transform so the digest does not depend on the host's default. EXR is
    # written scene-linear regardless, but this removes the question entirely.
    scene.view_settings.view_transform = "Raw"

    scene.hdweekend.samples = 8
    scene.hdweekend.seed = 12345  # deterministic
    scene.hdweekend.jitter_camera = False  # so two runs compare exactly

    out_dir = Path(tempfile.mkdtemp(prefix="hdweekend-smoke-"))
    scene.render.filepath = str(out_dir / "smoke")  # -> smoke.exr

    # exit=True makes this _exit(1) after dumping every thread's stack, which a pure-Python
    # timer could not do: the hang is inside the C++ render loop and never yields.
    faulthandler.dump_traceback_later(RENDER_TIMEOUT, exit=True)
    try:
        bpy.ops.render.render(write_still=True)
    finally:
        faulthandler.cancel_dump_traceback_later()

    exr = out_dir / "smoke.exr"
    assert exr.exists(), f"render wrote no file at {exr}"

    image = bpy.data.images.load(str(exr))
    # Read the buffer back untransformed, so the digest is of what the delegate produced.
    image.colorspace_settings.name = "Non-Color"
    px = list(image.pixels)

    assert len(px) == EXPECTED_FLOATS, f"unexpected pixel count {len(px)}"
    # Alpha is 1.0 across a fully-covered frame whether or not colour arrived, so an
    # any(px) test would pass on a black render. Check the colour channels only.
    rgb = [v for i, v in enumerate(px) if i % 4 != 3]
    assert any(v > 0.0 for v in rgb), "render produced an entirely black image"

    digest = hashlib.sha256(b"".join(f"{v:.4f}".encode() for v in px)).hexdigest()
    print("OK", len(px), "pixels, digest", digest[:16])


if __name__ == "__main__":
    status = 0
    try:
        main()
    except BaseException:
        traceback.print_exc()
        status = 1
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(status)  # see note 2 in the module docstring
