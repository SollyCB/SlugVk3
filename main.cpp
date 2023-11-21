#include "allocator.hpp"
#include "print.h"
#include "spirv.hpp"
#include "gpu.hpp"
#include "gltf.hpp"
#include "glfw.hpp"
#include "hash_map.hpp"
#include "assert.h"

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

    gpu->shader_memory = gpu::init_shaders();

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
        cstr_to_string("static_cube"), // These should be turned into enum values.
        cstr_to_string("cesium_man"),
    };

    gpu::Model_Allocators_Config model_allocators_config = {}; // @Unused
    gpu::Model_Allocators model_allocators = gpu::init_model_allocators(&model_allocators_config);
    gpu::Static_Model models[model_count];
    for(u32 i = 0; i < model_count; ++i) {
        models[i] = gpu::load_static_model(&model_allocators, &model_files[i], &model_dirs[i]);
    }

    gpu::Allocator_Result res;
    res = gpu::staging_queue_begin(&model_allocators.vertex);
    assert(res == gpu::ALLOCATOR_RESULT_SUCCESS);

    u32 vertex_key = model_allocators.vertex.allocation_indices[models[1].vertex_allocation_key];
    gpu::Allocator *alloc = &model_allocators.vertex;

    res = gpu::staging_queue_add(&model_allocators.vertex, vertex_key);
    assert(res == gpu::ALLOCATOR_RESULT_SUCCESS);

    res = gpu::staging_queue_submit(&model_allocators.vertex);
    assert(res == gpu::ALLOCATOR_RESULT_SUCCESS);

    while(!glfwWindowShouldClose(glfw->window)) {
        glfw::poll_and_get_input(glfw);
    }

    for(u32 i = 0; i < model_count; ++i) {
        free_static_model(&models[i]);
    }

    gpu::shutdown_allocators(&model_allocators);
    gpu::shutdown_shaders(&gpu->shader_memory);
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
