add_executable(tracer main.cpp)
#target_compile_options(tracer
#    PRIVATE
#        $<$<CONFIG:Debug>:-O2>
#        $<$<CONFIG:Debug>:-g>
#        $<$<CONFIG:Debug>:-Wall>
#)

# include nanobench
target_link_libraries(${PROJECT_NAME} PRIVATE nanobench)

# include OpenMP
find_package(OpenMP REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC OpenMP::OpenMP_CXX)
