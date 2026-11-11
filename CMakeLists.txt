cmake_minimum_required(VERSION 3.24)
project(gpu_culling_vulkan VERSION 0.1.0 LANGUAGES CXX)

# Dependencies: locate Vulkan and the libraries used for windows and camera mathematics.
# Prefer installed GLFW and GLM packages; fetch the specified upstream releases when unavailable.
# Vulkan supplies the headers, loader and shader compiler needed by the build below.
find_package(Vulkan 1.3 REQUIRED COMPONENTS glslc)
find_package(glfw3 3.4.0 EXACT CONFIG QUIET)
if(NOT TARGET glfw)
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glfw)
endif()

find_package(glm 1.0.1 CONFIG QUIET)
if(NOT TARGET glm::glm)
    include(FetchContent)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(glm)
endif()

# Application target: build only this project's renderer sources and list its shader inputs.
# C++20 and the linked library targets provide the language and platform requirements.
# Record the shader output directory so runtime loading does not depend on the launch directory.
add_executable(gpu_culling
    src/main.cpp
    src/device.cpp
    src/device.hpp
    src/swapchain.cpp
    src/swapchain.hpp
    src/frame.cpp
    src/frame.hpp
    src/depth.cpp
    src/depth.hpp
    src/culling.cpp
    src/culling.hpp
    src/vulkan_utils.hpp
    shaders/floor.vert
    shaders/floor.frag
    shaders/sphere.vert
    shaders/sphere.frag
    shaders/cull.comp
)
target_compile_features(gpu_culling PRIVATE cxx_std_20)
set_target_properties(gpu_culling PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(gpu_culling PRIVATE glfw Vulkan::Vulkan glm::glm)
target_compile_definitions(gpu_culling PRIVATE
    GPU_CULLING_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders"
)

# Shader build: compile every GLSL stage into Vulkan-compatible SPIR-V in the build tree.
# Track source dependencies so changing a shader automatically rebuilds its corresponding binary.
# Make the executable depend on these outputs before it can be launched.
set(SHADER_BINARIES)
foreach(SHADER floor.vert floor.frag sphere.vert sphere.frag cull.comp)
    set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER}")
    set(SHADER_BINARY "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}.spv")
    add_custom_command(
        OUTPUT "${SHADER_BINARY}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
        COMMAND Vulkan::glslc --target-env=vulkan1.3 "${SHADER_SOURCE}" -o "${SHADER_BINARY}"
        DEPENDS "${SHADER_SOURCE}"
        COMMENT "Compiling ${SHADER}"
        VERBATIM
    )
    list(APPEND SHADER_BINARIES "${SHADER_BINARY}")
endforeach()
add_custom_target(gpu_culling_shaders DEPENDS ${SHADER_BINARIES})
add_dependencies(gpu_culling gpu_culling_shaders)

# Compiler diagnostics: enable the normal warning level for the active compiler family.
# Keep these options local to the application target instead of imposing them on dependencies.
# Platform-specific choices here affect build diagnostics rather than rendering behavior.
if(MSVC)
    target_compile_options(gpu_culling PRIVATE /W4 /permissive-)
else()
    target_compile_options(gpu_culling PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Small CPU checks cover the clipping convention and boundary cases without a window or GPU.
enable_testing()
add_executable(culling_test tests/culling_test.cpp src/culling.cpp)
target_include_directories(culling_test PRIVATE src)
target_compile_features(culling_test PRIVATE cxx_std_20)
target_link_libraries(culling_test PRIVATE Vulkan::Vulkan glm::glm)
add_test(NAME frustum_culling COMMAND culling_test)
