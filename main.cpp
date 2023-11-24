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

    init_glfw();
    Glfw *glfw = get_glfw_instance();

    init_gpu();
    Gpu *gpu = get_gpu_instance();

    init_window(gpu, glfw);
    Window *window = get_window_instance();

    zero_temp();

    gpu->shader_memory = init_shaders();

    // Load Models
    u32 model_count = 2;
    String model_dirs[] = {
        cstr_to_string("models/cube-static/"),
        cstr_to_string("models/cesium-man/"),
    };
    String model_files[] = {
        cstr_to_string("Cube.gltf"),
        cstr_to_string("CesiumMan.gltf"),
    };
    String model_names[] = {
        cstr_to_string("static_cube"), // These should be turned into enum values.
        cstr_to_string("cesium_man"),
    };

    Model_Allocators_Config model_allocators_config = {}; // @Unused
    Model_Allocators model_allocators = init_model_allocators(&model_allocators_config);

    Static_Model *models = (Static_Model*)malloc_h(sizeof(Static_Model) * model_count, 8);
    for(u32 i = 0; i < model_count; ++i) {
        models[i] = load_static_model(&model_allocators, &model_files[i], &model_dirs[i]);
    }

    Gpu_Allocator_Result res;
    res = staging_queue_begin(&model_allocators.vertex);
    assert(res == ALLOCATOR_RESULT_SUCCESS);

    u32 vertex_key = model_allocators.vertex.allocation_indices[models[1].vertex_allocation_key];
    Gpu_Allocator *alloc = &model_allocators.vertex;

    res = staging_queue_add(&model_allocators.vertex, vertex_key);
    assert(res == ALLOCATOR_RESULT_SUCCESS);

    res = staging_queue_submit(&model_allocators.vertex);
    assert(res == ALLOCATOR_RESULT_SUCCESS);

    VkFence acquire_image_fence = create_fence(false);
    u32 present_image_index;

    while(!glfwWindowShouldClose(glfw->window)) {
        vkAcquireNextImageKHR(gpu->device, window->swapchain, 10e9, NULL, acquire_image_fence, &present_image_index); 

        poll_and_get_input(glfw);

        wait_and_reset_fence(acquire_image_fence);

        Draw_Final_Basic_Config draw_config;
        draw_config.count  = 1;
        draw_config.models = &models[0];

        draw_config.rp_config.present = window->views[present_image_index];
        draw_config.rp_config.depth   = gpu->memory.depth_views[0];
        draw_config.rp_config.shadow  = gpu->memory.depth_views[1];

        Draw_Final_Basic draw_basic = draw_create_basic(&draw_config);
        draw_destroy_basic(&draw_basic);
    }

    for(u32 i = 0; i < model_count; ++i) {
        free_static_model(&models[i]);
    }
    free_h(models);

    destroy_fence(acquire_image_fence);

    shutdown_allocators(&model_allocators);
    shutdown_shaders(&gpu->shader_memory);
    kill_window(gpu, window);
    kill_gpu(gpu);
    kill_glfw(glfw);

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
