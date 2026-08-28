FetchContent_Declare(
  tinyobjloader
  GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
  GIT_TAG v2.0.0rc13
  GIT_SHALLOW TRUE
  # No FIND_PACKAGE_ARGS, a system tinyobjloader would be the float build.
  # We want to make sure to pull the source and build with doubles enabled.
)

# NOTE: this also renames the target to `tinyobjloader_double`.
set(TINYOBJLOADER_USE_DOUBLE ON CACHE BOOL "" FORCE)

set(TINYOBJLOADER_BUILD_TEST_LOADER OFF CACHE BOOL "" FORCE)
set(TINYOBJLOADER_BUILD_OBJ_STICHER OFF CACHE BOOL "" FORCE)
set(TINYOBJLOADER_WITH_PYTHON OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(tinyobjloader)

