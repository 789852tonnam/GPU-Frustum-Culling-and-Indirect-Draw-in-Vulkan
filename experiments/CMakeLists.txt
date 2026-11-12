if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

set(RENDERER_SOURCES
    ${PROJECT_SOURCE_DIR}/src/device.cpp
    ${PROJECT_SOURCE_DIR}/src/swapchain.cpp
    ${PROJECT_SOURCE_DIR}/src/frame.cpp
    ${PROJECT_SOURCE_DIR}/src/depth.cpp
    ${PROJECT_SOURCE_DIR}/src/culling.cpp
)

foreach(FRAMES IN ITEMS 180 36)
    if(FRAMES EQUAL 180)
        set(MAIN_FILE level-search/reproduce/main_benchmark.cpp)
    else()
        set(MAIN_FILE level-search/reproduce/main_screen36.cpp)
    endif()
    add_executable(experiment_benchmark_${FRAMES} EXCLUDE_FROM_ALL ${MAIN_FILE} ${RENDERER_SOURCES})
    set_target_properties(experiment_benchmark_${FRAMES} PROPERTIES OUTPUT_NAME benchmark-${FRAMES})
endforeach()
add_executable(experiment_capture EXCLUDE_FROM_ALL renderdoc/reproduce/capture_main.cpp ${RENDERER_SOURCES})
set_target_properties(experiment_capture PROPERTIES OUTPUT_NAME gpu_capture)
target_link_libraries(experiment_capture PRIVATE ${CMAKE_DL_LIBS})

foreach(TARGET IN ITEMS experiment_benchmark_180 experiment_benchmark_36 experiment_capture)
    target_include_directories(${TARGET} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(${TARGET} PRIVATE cxx_std_20)
    target_compile_options(${TARGET} PRIVATE -O3 -Wall -Wextra -Wpedantic)
    target_compile_definitions(${TARGET} PRIVATE NDEBUG
        GPU_CULLING_SHADER_DIR="${PROJECT_BINARY_DIR}/shaders")
    target_link_libraries(${TARGET} PRIVATE glfw Vulkan::Vulkan glm::glm)
    add_dependencies(${TARGET} gpu_culling_shaders)
endforeach()

add_executable(experiment_clock_gate_test EXCLUDE_FROM_ALL level-search/reproduce/clock_gate_test.cpp)
target_compile_features(experiment_clock_gate_test PRIVATE cxx_std_20)
add_executable(experiment_equivalent_levels EXCLUDE_FROM_ALL
    level-search/reproduce/verify_equivalent_levels.cpp ${PROJECT_SOURCE_DIR}/src/culling.cpp)
target_include_directories(experiment_equivalent_levels PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(experiment_equivalent_levels PRIVATE cxx_std_20)
target_link_libraries(experiment_equivalent_levels PRIVATE Vulkan::Vulkan glm::glm)
foreach(TARGET IN ITEMS experiment_clock_gate_test experiment_equivalent_levels)
    target_compile_options(${TARGET} PRIVATE -O3 -UNDEBUG)
endforeach()

add_custom_target(experiment_tools DEPENDS experiment_benchmark_180 experiment_benchmark_36
    experiment_capture experiment_clock_gate_test experiment_equivalent_levels)
