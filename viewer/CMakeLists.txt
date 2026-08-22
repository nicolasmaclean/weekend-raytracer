add_executable(viewer main.cpp)
target_compile_options(viewer
   PRIVATE
       $<$<CONFIG:Debug>:-O2>
       $<$<CONFIG:Debug>:-g>
       $<$<CONFIG:Debug>:-Wall>
)

# include ray tracer library
target_include_directories(viewer PRIVATE ${CMAKE_SOURCE_DIR}/tracer)
target_link_libraries(viewer PRIVATE tracer)

# include sdl3
target_link_libraries(viewer PRIVATE SDL3::SDL3)
