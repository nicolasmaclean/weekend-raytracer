# core library
add_library(tracer INTERFACE)
target_include_directories(tracer INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tracer INTERFACE cxx_std_17)

# include OpenMP
find_package(OpenMP REQUIRED)
target_link_libraries(tracer INTERFACE OpenMP::OpenMP_CXX)

# compile cli (main.cpp create .ppm)
add_executable(tracer_cli main.cpp)
target_link_libraries(tracer_cli PRIVATE tracer)
target_compile_options(tracer_cli
   PRIVATE
       $<$<CONFIG:Debug>:-O2>
       $<$<CONFIG:Debug>:-g>
       $<$<CONFIG:Debug>:-Wall>
)

