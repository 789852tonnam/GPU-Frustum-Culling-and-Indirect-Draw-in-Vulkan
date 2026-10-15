cmake_minimum_required(VERSION 3.24)
project(gpu_culling_vulkan VERSION 0.1.0 LANGUAGES CXX)

find_package(Vulkan 1.3 REQUIRED)
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

add_executable(gpu_culling src/main.cpp)
target_compile_features(gpu_culling PRIVATE cxx_std_20)
set_target_properties(gpu_culling PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(gpu_culling PRIVATE glfw Vulkan::Vulkan)

if(MSVC)
    target_compile_options(gpu_culling PRIVATE /W4 /permissive-)
else()
    target_compile_options(gpu_culling PRIVATE -Wall -Wextra -Wpedantic)
endif()
