cmake_minimum_required(VERSION 3.15 FATAL_ERROR)

project(tracer)

find_package(OpenMP REQUIRED)

add_executable(tracer main.cpp)

target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)

target_compile_options(tracer
    PRIVATE
        $<$<CONFIG:Debug>:-O2>
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-Wall>
)
