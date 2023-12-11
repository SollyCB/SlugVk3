#include "allocator.hpp"
#include "print.h"
#include "spirv.hpp"
#include "gpu.hpp"
#include "asset.hpp"
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

    init_assets();
    Assets *assets = get_assets_instance();

    zero_temp();

    VkFence     acquire_image_fence     = create_fence(false);
    VkSemaphore acquire_image_semaphore = create_semaphore();

    u32 present_image_index;
    VkResult present_result;
    VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present_info.swapchainCount   = 1;
    present_info.pSwapchains      = &window->swapchain;
    present_info.pImageIndices    = &present_image_index;
    present_info.pResults         = &present_result;

    while(!glfwWindowShouldClose(glfw->window)) {
        vkAcquireNextImageKHR(gpu->device, window->swapchain, 10e9, NULL, acquire_image_fence, &present_image_index);

        poll_and_get_input(glfw);

        wait_and_reset_fence(acquire_image_fence);

        vkQueuePresentKHR(gpu->graphics_queue, &present_info);

        zero_temp(); // Empty temp allocator at the end of the frame
        g_frame_index = (g_frame_index + 1) & 1;
    }

    destroy_fence(acquire_image_fence);
    destroy_semaphore(acquire_image_semaphore);

    kill_assets();
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
    test_asset();

    end_tests();
}
#endif
