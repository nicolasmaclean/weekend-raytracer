# core library
add_library(tracer INTERFACE)
target_include_directories(tracer INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tracer INTERFACE cxx_std_17)

# include OneTBB (will automatically use hyrda/usd's version so it's safe to keep in tracer core)
target_link_libraries(tracer INTERFACE TBB::tbb)


# compile version of library with just tinyobjloader that can be used by cli and viewer
add_library(tracer_obj INTERFACE)
target_link_libraries(tracer_obj INTERFACE tracer tinyobjloader_double)
target_compile_definitions(tracer_obj 
  INTERFACE 
    TRACER_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
)

# compile cli (main.cpp create .ppm)
add_executable(tracer_cli main.cpp)
target_link_libraries(tracer_cli PRIVATE tracer tracer_obj)
target_compile_options(tracer_cli
   PRIVATE
       $<$<CONFIG:Debug>:-O2>
       $<$<CONFIG:Debug>:-g>
       $<$<CONFIG:Debug>:-Wall>
)

