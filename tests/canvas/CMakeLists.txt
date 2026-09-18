add_library(regression_canvas STATIC
    ../../containers/test/test_container.cpp
    ../../containers/test/Bitmap.cpp
    ../../containers/test/Font.cpp
    ../../containers/test/lodepng.cpp)
target_include_directories(regression_canvas PUBLIC ../../containers/test)
target_link_libraries(regression_canvas PUBLIC litehtml)
set_target_properties(regression_canvas PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_executable(renderer_cleanup renderer_cleanup.cpp)
target_link_libraries(renderer_cleanup PRIVATE regression_canvas)
set_target_properties(renderer_cleanup PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
add_test(NAME renderer_cleanup COMMAND renderer_cleanup)

foreach(module style rendering)
    add_executable(motion_${module} motion_${module}.cpp)
    target_link_libraries(motion_${module} PRIVATE regression_canvas)
    set_target_properties(motion_${module} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME motion_${module} COMMAND motion_${module})
endforeach()
