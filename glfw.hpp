#ifndef SOL_GLFW_HPP_INCLUDE_GUARD_
#define SOL_GLFW_HPP_INCLUDE_GUARD_

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "basic.h"

struct Glfw {
    GLFWwindow *window;
    u32 width;
    u32 height;

    // @Todo cursor pos
};

Glfw* get_glfw_instance();

static void error_callback_glfw(int error, const char *description) {
    println("GLFW Error: ", description);
}

void init_glfw(); 
void kill_glfw();

void glfw_poll_and_get_input();

// inlines
inline void poll_glfw() {
    glfwPollEvents();
}

#endif // include guard
