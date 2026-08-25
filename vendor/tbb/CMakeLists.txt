FetchContent_Declare(
  TBB
  GIT_REPOSITORY https://github.com/uxlfoundation/oneTBB.git
  GIT_TAG v2023.1.0
  GIT_SHALLOW TRUE
  FIND_PACKAGE_ARGS NAMES TBB          # uses a system oneTBB if one exists
)

set(TBB_TEST OFF CACHE BOOL "" FORCE)      # don't build TBB's test suite
set(TBB_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TBB_STRICT OFF CACHE BOOL "" FORCE)    # don't promote TBB's warnings to errors
FetchContent_MakeAvailable(TBB)
