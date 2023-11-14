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
        cstr_to_string("shaders/basic.vert.spv"),
        cstr_to_string("shaders/basic.frag.spv"),
    };
    String shader_set_names[] = {
        cstr_to_string("basic"),
    };
    gpu::Set_Allocate_Info set_allocate_info =
        insert_shader_set(&shader_set_names[0], 2, basic_shader_files, &shader_map);
    gpu::Descriptor_Allocation basic_set_allocation = gpu::create_descriptor_sets(1, &set_allocate_info);

    // Load Models
    u32 model_count = 2;
    String model_dirs[model_count] = {
        cstr_to_string("models/cube-static/"),
        cstr_to_string("models/cesium-man/"),
    };
    String model_files[model_count] = {
        cstr_to_string("Cube.gltf"),
        cstr_to_string("CesiumMan.gltf"),
    };
    String model_names[model_count] = {
        cstr_to_string("static_cube"),
        cstr_to_string("cesium_man"),
    };

    // @Todo setup the model allocators.
    gpu::Static_Model model;
    for(u32 i = 0; i < model_count; ++i) {
        //model = gpu::load_static_model(&model_allocs, &model_files[i], &model_dirs[i]);
    }

    while(!glfwWindowShouldClose(glfw->window)) {
        glfw::poll_and_get_input(glfw);
    }

    //gpu::shutdown_allocators(&model_allocs);
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
