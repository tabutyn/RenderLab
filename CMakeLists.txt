cmake_minimum_required(VERSION 3.26)
project(RenderLab)
set(CMAKE_CXX_STANDARD 20)

set(SOURCE_FILES source/main.cpp source/renderer.cpp)

add_executable(RenderLab WIN32 ${SOURCE_FILES})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT RenderLab)
target_include_directories(RenderLab PRIVATE "include")
target_link_libraries(RenderLab d3d12.lib)
target_link_libraries(RenderLab dxgi.lib)
