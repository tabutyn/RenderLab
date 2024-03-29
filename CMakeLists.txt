cmake_minimum_required(VERSION 3.26)
project(RenderLab)
set(CMAKE_CXX_STANDARD 20)

set(SOURCE_FILES source/main.cpp source/renderer.cpp include/renderer.h)


add_executable(RenderLab ${SOURCE_FILES})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT RenderLab)
target_include_directories(RenderLab PRIVATE "include" "tinygltf")
target_link_libraries(RenderLab d3d12.lib)
target_link_libraries(RenderLab dxgi.lib)
target_link_libraries(RenderLab D3DCompiler.lib)

add_custom_command(
        TARGET RenderLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/output
)
add_custom_command(
        TARGET RenderLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/source/vertexShader.hlsl
                ${CMAKE_BINARY_DIR}/$<CONFIG>/vertexShader.hlsl)
add_custom_command(
        TARGET RenderLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
                ${CMAKE_SOURCE_DIR}/source/pixelShader.hlsl
                ${CMAKE_BINARY_DIR}/$<CONFIG>/pixelShader.hlsl)
                
add_custom_command(
        TARGET RenderLab POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory 
                ${CMAKE_SOURCE_DIR}/tinygltf/models/Cube
                ${CMAKE_BINARY_DIR}/$<CONFIG>/Cube)

add_custom_target(shaders
    SOURCES source/vertexShader.hlsl source/pixelShader.hlsl)