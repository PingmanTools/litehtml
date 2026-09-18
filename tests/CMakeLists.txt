cmake_minimum_required(VERSION 3.11)
project(litehtml-regressions LANGUAGES C CXX)

set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(.. litehtml)
enable_testing()

file(GLOB regression_sources "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(source IN LISTS regression_sources)
    get_filename_component(name "${source}" NAME_WE)
    add_executable(${name} "${source}")
    target_link_libraries(${name} PRIVATE litehtml)
    set_target_properties(${name} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME ${name} COMMAND ${name})
endforeach()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/canvas/CMakeLists.txt")
    add_subdirectory(canvas)
endif()
