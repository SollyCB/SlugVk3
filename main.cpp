#include "allocator.hpp"
#include "print.hpp"
#include "gpu.hpp"
#include "spirv.hpp"
#include "gltf.hpp"
#include "glfw.hpp"

#if TEST
   #include "test.hpp"
   void run_tests(); 
#endif

int main() {
    init_allocators();

#if TEST
    run_tests();
#endif

    glfw::init_glfw();
    glfw::Glfw *glfw = glfw::get_glfw_instance();

    gpu::init_gpu();
    gpu::Gpu *gpu = gpu::get_gpu_instance();

    gpu::init_window(gpu, glfw);
    gpu::Window *window = gpu::get_window_instance();

    zero_temp();

    while(!glfwWindowShouldClose(glfw->window)) {
        glfw::poll_and_get_input(glfw);
    }
          

    gpu::kill_window(gpu, window);
    gpu::kill_gpu(gpu);
    glfw::kill_glfw(glfw);

    kill_allocators();
    return 0;
}

#if TEST
void run_tests() {
    load_tests();

    test_spirv();
    test_gltf();

    end_tests();
}
#endif
