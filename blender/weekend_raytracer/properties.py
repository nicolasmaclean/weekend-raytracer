# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later

import bpy

# Must match WeekendHydraRenderEngine.bl_idname in engine.py.
_ENGINE_ID = "WEEKEND"


class WeekendRenderSettings(bpy.types.PropertyGroup):
    """Every default here is the matching HdWeekendDefault* constant from hydra/config.h.

    The delegate populates the same values itself, so a disagreement would be silent: the
    panel would read one number and a headless usdrecord another.
    """

    samples: bpy.props.IntProperty(
        name="Samples",
        default=100,
        min=1,
        soft_max=4096,
        description="Samples per pixel before the image is considered converged",
    )
    viewport_samples: bpy.props.IntProperty(
        name="Viewport Samples", default=32, min=1, soft_max=1024
    )
    max_bounces: bpy.props.IntProperty(
        name="Max Bounces",
        default=20,
        min=0,
        soft_max=64,
        description="Times a ray may scatter before it is terminated. Low values darken the image",
    )
    tile_size: bpy.props.IntProperty(
        name="Tile Size",
        default=8,
        min=1,
        soft_max=256,
        description="Edge length of one unit of parallel work, and the granularity at which a "
        "render can be cancelled. Large values make the viewport feel sluggish",
    )
    seed: bpy.props.IntProperty(
        name="Random Seed",
        default=-1,
        min=-1,
        description="Any value other than -1 gives a repeatable image for a given scene and camera",
    )
    thread_limit: bpy.props.IntProperty(
        name="Thread Limit", default=0, min=0, description="0 means all cores"
    )
    jitter_camera: bpy.props.BoolProperty(name="Jitter Camera", default=True)
    enable_scene_colors: bpy.props.BoolProperty(name="Scene Colors", default=True)


class WeekendPanel(bpy.types.Panel):
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "render"
    COMPAT_ENGINES = {_ENGINE_ID}

    @classmethod
    def poll(cls, context):
        return context.engine in cls.COMPAT_ENGINES


class WEEKEND_PT_sampling(WeekendPanel):
    bl_label = "Sampling"

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        s = context.scene.hdweekend
        layout.prop(s, "samples")
        layout.prop(s, "viewport_samples")
        layout.prop(s, "max_bounces")
        layout.prop(s, "seed")
        layout.prop(s, "jitter_camera")
        layout.prop(s, "enable_scene_colors")


class WEEKEND_PT_performance(WeekendPanel):
    bl_label = "Performance"

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        s = context.scene.hdweekend
        layout.prop(s, "thread_limit")
        layout.prop(s, "tile_size")


class WeekendViewLayerPanel(bpy.types.Panel):
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "view_layer"
    COMPAT_ENGINES = {_ENGINE_ID}

    @classmethod
    def poll(cls, context):
        return context.engine in cls.COMPAT_ENGINES


class WEEKEND_PT_passes(WeekendViewLayerPanel):
    bl_label = "Passes"

    def draw(self, context):
        pass


class WEEKEND_PT_passes_data(WeekendViewLayerPanel):
    bl_label = "Data"
    bl_parent_id = "WEEKEND_PT_passes"

    def draw(self, context):
        layout = self.layout
        layout.use_property_split = True
        layout.use_property_decorate = False

        col = layout.column(heading="Include", align=True)
        col.prop(context.view_layer, "use_pass_combined")
        col.prop(context.view_layer, "use_pass_z")
        col.prop(context.view_layer, "use_pass_normal")


_classes = (
    WeekendRenderSettings,
    WEEKEND_PT_sampling,
    WEEKEND_PT_performance,
    WEEKEND_PT_passes,
    WEEKEND_PT_passes_data,
)


def _builtin_panels():
    """Blender's own properties panels are engine-gated, and a custom engine must opt into
    each one. Without this, selecting Weekend leaves every built-in panel hidden - Output,
    Dimensions, Format, Color Management - and the View Layer tab shows nothing but Custom
    Properties, which is the one panel carrying no COMPAT_ENGINES at all.

    The Cycles idiom, as used by the Hydra engine Blender ships
    (4.5/scripts/addons_core/hydra_storm/ui.py:187-199). Excluded are the panels whose
    features we do not implement; lights and materials arrive with 0.4.0's materialX work,
    at which point this set shrinks.
    """
    exclude = {
        "RENDER_PT_stamp",
        "DATA_PT_light",
        "DATA_PT_spot",
        "NODE_DATA_PT_light",
        "DATA_PT_falloff_curve",
        "RENDER_PT_post_processing",
        "RENDER_PT_simplify",
        "SCENE_PT_audio",
        "RENDER_PT_freestyle",
    }
    for panel in bpy.types.Panel.__subclasses__():
        compat = getattr(panel, "COMPAT_ENGINES", None)
        if compat and "BLENDER_RENDER" in compat and panel.__name__ not in exclude:
            yield panel


def register():
    for c in _classes:
        bpy.utils.register_class(c)
    bpy.types.Scene.hdweekend = bpy.props.PointerProperty(type=WeekendRenderSettings)
    for panel in _builtin_panels():
        panel.COMPAT_ENGINES.add(_ENGINE_ID)


def unregister():
    # discard, not remove: a panel that was never opted in must not raise here and leave
    # the add-on half-unregistered.
    for panel in _builtin_panels():
        panel.COMPAT_ENGINES.discard(_ENGINE_ID)
    del bpy.types.Scene.hdweekend
    for c in reversed(_classes):
        bpy.utils.unregister_class(c)
