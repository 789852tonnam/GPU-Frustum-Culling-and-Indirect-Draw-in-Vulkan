#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdlib>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    const bool smokeTest = argc == 2 && std::string_view(argv[1]) == "--smoke-test";
    if (argc > 1 && !smokeTest) {
        std::cerr << "Usage: gpu_culling [--smoke-test]\n";
        return EXIT_FAILURE;
    }

    glfwSetErrorCallback([](int code, const char* description) {
        std::cerr << "GLFW error " << code << ": " << description << '\n';
    });

    if (!glfwInit()) {
        return EXIT_FAILURE;
    }

    // Vulkan will provide the rendering surface in S2; no OpenGL context is needed.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(960, 540, "GPU Culling - S1 Window", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return EXIT_FAILURE;
    }

    std::cout << "GLFW window created (960x540). Press Escape or close the window to exit.\n";
    const double startTime = glfwGetTime();

    while (!glfwWindowShouldClose(window)) {
        glfwWaitEventsTimeout(0.05);
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS ||
            (smokeTest && glfwGetTime() - startTime >= 0.5)) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Window closed; GLFW shut down.\n";
    return EXIT_SUCCESS;
}
