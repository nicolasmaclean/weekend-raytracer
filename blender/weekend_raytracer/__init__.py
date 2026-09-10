# SPDX-FileCopyrightText: 2025-2026 Nick Maclean
# SPDX-License-Identifier: GPL-3.0-or-later

from . import engine, properties


def register():
    properties.register()
    engine.register()


def unregister():
    engine.unregister()
    properties.unregister()
