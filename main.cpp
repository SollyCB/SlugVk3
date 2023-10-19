#include "allocator.hpp"
#include "print.hpp"
#include "spirv.hpp"
#include "gpu.hpp"
#include "gltf.hpp"
#include "glfw.hpp"
#include "hash_map.hpp"

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

    gpu::Shader_Map shader_map = gpu::create_shader_map(128);

    String basic_shader_files[] = {
        get_string("shaders/basic.vert.spv"),
        get_string("shaders/basic.frag.spv"),
    };
    gpu::Set_Allocate_Info shader_allocate_info = insert_shader_set("basic", 2, basic_shader_files, &shader_map);
    // count_descriptors(info);

    // Create DescriptorSets

    while(!glfwWindowShouldClose(glfw->window)) {
        glfw::poll_and_get_input(glfw);
    }
          
    gpu::destroy_shader_map(&shader_map);
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
