# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later

import os
from pathlib import Path

import bpy


def _plugin_dir() -> Path:
    """Directory holding hdWeekend/resources/plugInfo.json.

    Packaged, that is <addon>/plugin/usd - the same layout cmake installs, because
    plugInfo.json's LibraryPath ("../hdWeekend.so") resolves against it. HDW_PLUGIN_DIR
    overrides it, which is the whole dev loop: point at build-hydra-blender/install.
    """
    if override := os.environ.get("HDW_PLUGIN_DIR"):
        return Path(override)
    return Path(__file__).parent / "plugin" / "usd"


class WeekendHydraRenderEngine(bpy.types.HydraRenderEngine):
    bl_idname = "WEEKEND"
    bl_label = "Weekend"
    bl_info = "CPU path tracer, via a Hydra render delegate"

    # The TfType name registered by HdRendererPluginRegistry::Define<HdWeekendRendererPlugin>()
    # in hydra/rendererPlugin.cpp - NOT the "hdWeekend" plugin name and NOT the "Weekend"
    # display name. Blender passes this straight to registry.CreateRenderDelegate (engine.cc:63).
    bl_delegate_id = "HdWeekendRendererPlugin"

    # We are a CPU tracer. This is load-bearing in three places: no Hgi driver is created,
    # the CPU RenderTaskDelegate is used instead of GPURenderTaskDelegate, and the
    # "add color+depth anyway" fallback in final_engine.cc does not apply to us - which is
    # why get_render_settings MUST return the aovToken: keys. See the plan, "the one thing".
    bl_use_gpu_context = False

    # No materials yet, so preview thumbnails would be flat grey and would cost real time.
    # Flip to True with 0.4.0's material work.
    bl_use_preview = False
    bl_use_materialx = False

    @classmethod
    def register(cls):
        # Needed for bpy-as-module (Stage F); a harmless no-op inside the real app, where pxr
        # is already in site-packages. One code path for both.
        bpy.utils.expose_bundled_modules()

        import pxr.Plug

        plug_info = _plugin_dir() / "hdWeekend" / "resources" / "plugInfo.json"
        if not plug_info.exists():
            raise RuntimeError(f"hdWeekend plugInfo.json not found at {plug_info}")
        pxr.Plug.Registry().RegisterPlugins([str(plug_info)])

    def get_render_settings(self, engine_type: str):
        s = bpy.context.scene.hdweekend

        # Keys are forwarded from Blender HdRenderDelegate::SetRenderSetting
        settings = {
            "convergedSamplesPerPixel": (
                s.viewport_samples if engine_type == "VIEWPORT" else s.samples
            ),
            "threadLimit": s.thread_limit,
            "hdWeekend:maxBounces": s.max_bounces,
            "hdWeekend:randomNumberSeed": s.seed,
            "hdWeekend:tileSize": s.tile_size,
            "hdWeekend:jitterCamera": s.jitter_camera,
            "hdWeekend:enableSceneColors": s.enable_scene_colors,
        }

        if engine_type != "VIEWPORT":
            settings |= {
                "aovToken:Combined": "color",
                "aovToken:Depth": "cameraDepth",
                "aovToken:Normal": "normal",
            }

        return settings

    def update_render_passes(self, scene, render_layer):
        if render_layer.use_pass_combined:
            self.register_pass(scene, render_layer, "Combined", 4, "RGBA", "COLOR")
        if render_layer.use_pass_z:
            self.register_pass(scene, render_layer, "Depth", 1, "Z", "VALUE")
        if render_layer.use_pass_normal:
            self.register_pass(scene, render_layer, "Normal", 3, "XYZ", "VECTOR")


def register():
    bpy.utils.register_class(WeekendHydraRenderEngine)


def unregister():
    bpy.utils.unregister_class(WeekendHydraRenderEngine)
