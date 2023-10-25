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

    // Load Shaders
    String basic_shader_files[] = {
        get_string("shaders/basic.vert.spv"),
        get_string("shaders/basic.frag.spv"),
    };
    gpu::Set_Allocate_Info set_allocate_info = insert_shader_set("basic", 2, basic_shader_files, &shader_map);
    gpu::Descriptor_Allocation basic_set_allocation = gpu::create_descriptor_sets(1, &set_allocate_info);

    // Load Models
    gpu::model::Model_Allocators model_allocs = gpu::model::init_allocators();

    String model_dir = get_string("models/cesium-man/");

    String model_files[] = {get_string("CesiumMan.gltf")};

    gpu::model::Static_Model model =
        load_static_model(&model_allocs, &model_files[0], &model_dir);

    while(!glfwWindowShouldClose(glfw->window)) {
        glfw::poll_and_get_input(glfw);
    }

    free_static_model(&model);

    gpu::model::shutdown_allocators(&model_allocs);
    gpu::destroy_descriptor_sets(&basic_set_allocation);
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
