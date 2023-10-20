#ifndef SOL_GPU_HPP_INCLUDE_GUARD_
#define SOL_GPU_HPP_INCLUDE_GUARD_

#include <vulkan/vulkan_core.h>

#define VULKAN_ALLOCATOR_IS_NULL true

#if VULKAN_ALLOCATOR_IS_NULL
#define ALLOCATION_CALLBACKS_VULKAN NULL
#define ALLOCATION_CALLBACKS NULL
#endif

#include "basic.h"
#include "glfw.hpp"
#include "spirv.hpp"
#include "string.hpp"
#include "hash_map.hpp"

namespace gpu {

static constexpr u32 DEPTH_ATTACHMENT_COUNT = 1;
static constexpr u32 COLOR_ATTACHMENT_COUNT = 1;
static constexpr u32 VERTEX_STAGE_COUNT     = 1;
static constexpr u32 INDEX_STAGE_COUNT      = 1;
static constexpr u32 TEXTURE_STAGE_COUNT    = 1;
static constexpr u32 UNIFORM_BUFFER_COUNT   = 1;
struct Gpu_Memory {
    VkDeviceMemory depth_mem[DEPTH_ATTACHMENT_COUNT];
    VkDeviceMemory color_mem[COLOR_ATTACHMENT_COUNT];
    VkImage depth_attachments[DEPTH_ATTACHMENT_COUNT];
    VkImage color_attachments[COLOR_ATTACHMENT_COUNT];

    VkDeviceMemory vertex_mem_stage[VERTEX_STAGE_COUNT];
    VkDeviceMemory index_mem_stage [INDEX_STAGE_COUNT];
    VkDeviceMemory vertex_mem_device;
    VkDeviceMemory index_mem_device;

    VkDeviceMemory texture_mem_stage[TEXTURE_STAGE_COUNT];
    VkDeviceMemory texture_mem_device;

    VkDeviceMemory uniform_mem[UNIFORM_BUFFER_COUNT];
};
struct Gpu_Info {
    VkPhysicalDeviceProperties props;
};
struct Gpu {
    Gpu_Info info;

    VkInstance instance;
    VkPhysicalDevice phys_device;
    VkDevice device;

    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue transfer_queue;

    u32 graphics_queue_index;
    u32 present_queue_index;
    u32 transfer_queue_index;

    Gpu_Memory memory;
};
Gpu* get_gpu_instance();

void init_gpu();
void kill_gpu(Gpu *gpu);

// Instance
struct Create_Instance_Info {
    const char *application_name = "SlugApp";
    u32 application_version_major  = 0;
    u32 application_version_middle = 0;
    u32 application_version_minor  = 0;

    const char *engine_name = "SlugEngine";
    u32 engine_version_major  = 0;
    u32 engine_version_middle = 0;
    u32 engine_version_minor  = 0;

    u32 vulkan_api_version = VK_API_VERSION_1_3;
};
VkInstance create_instance(Create_Instance_Info *info);

// Device and Queues
VkDevice create_device(Gpu *gpu);
VkQueue create_queue(Gpu *gpu);
void destroy_device(Gpu *gpu);
void destroy_queue(Gpu *gpu);

// Surface and Swapchain
struct Window {
    VkSwapchainKHR swapchain;
    VkSwapchainCreateInfoKHR info;
    u32 image_count = 2;
    VkImage *images;
    VkImageView *views;
};
Window* get_window_instance();

void init_window(Gpu *gpu, glfw::Glfw *glfw);
void kill_window(Gpu *gpu, Window *window);

VkSurfaceKHR create_surface(VkInstance vk_instance, glfw::Glfw *glfw);
void destroy_surface(VkInstance vk_instance, VkSurfaceKHR vk_surface);

// @Note this function does a lot to initialize window members, because I consider
// these elems to be parts of the swapchain, not distinct things.
VkSwapchainKHR create_swapchain(Gpu *gpu, VkSurfaceKHR vk_surface);
void destroy_swapchain(VkDevice vk_device, Window *window);
VkSwapchainKHR recreate_swapchain(Gpu *gpu, Window *window);

inline static VkViewport gpu_get_complete_screen_viewport()
{
    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    VkExtent2D *extent = &get_window_instance()->info.imageExtent;
    viewport.width = extent->width;
    viewport.height = extent->height;
    viewport.minDepth = 0.0;
    viewport.maxDepth = 1.0;
    return viewport;
}
inline static VkRect2D gpu_get_complete_screen_area()
{
    VkRect2D rect = {
        .offset = {0, 0},
        .extent = get_window_instance()->info.imageExtent,
    };
    return rect;
}

// Memory
void allocate_memory();
void free_memory();

// Shaders
struct Shader {
    VkShaderStageFlagBits stage;
    VkShaderModule module;
};
struct Shader_Info {
    u32 count;
    Shader *shaders;
    Parsed_Spirv *spirv;
};
Shader_Info create_shaders(u32 count, String *file_names);
void destroy_shaders(u32 count, Shader *shaders);

struct Set_Layout_Info {
    u32 count;
    VkDescriptorSetLayoutBinding *bindings;
};
Set_Layout_Info* group_spirv(u32 count, Parsed_Spirv *parsed_spirv, u32 *returned_set_count);
void count_descriptors(u32 count, Set_Layout_Info *infos, u32 descriptor_counts[11]);

struct Shader_Set {
    u32 shader_count; // might not even need the counts as these will be retrieved by name
    u32 set_count;

    Shader *shaders;
    VkDescriptorSet *sets;

    VkPipelineLayout pl_layout;
};
struct Shader_Map {
    HashMap<u64, Shader_Set> map;
};
struct Set_Allocate_Info {
    u32 count;
    Set_Layout_Info *infos; // for counting descriptor pool requirements
    VkDescriptorSetLayout *layouts;
    VkDescriptorSet *sets;
};
Shader_Map create_shader_map(u32 size);
void destroy_shader_map(Shader_Map *map);
Set_Allocate_Info insert_shader_set(const char *set_name, u32 count, String *files, Shader_Map *map);
Shader_Set* get_shader_set(const char *set_name, Shader_Map *map);


// Descriptors

VkDescriptorSetLayout* create_set_layouts(u32 count, Set_Layout_Info *info);
void destroy_set_layouts(u32 count, VkDescriptorSetLayout *layouts);

struct Descriptor_Allocation {
    VkDescriptorPool pool;
};
Descriptor_Allocation create_descriptor_sets(u32 count, Set_Allocate_Info *infos);
void destroy_descriptor_sets(Descriptor_Allocation *allocation);

//
// @Todo Add function to count pool size required for some set of models
//

// `PipelineLayout
struct Pl_Layout_Info {
    u32 layout_count;
    VkDescriptorSetLayout *layouts;
    u32 push_constant_count;
    VkPushConstantRange *push_constants;
};
VkPipelineLayout create_pl_layout(VkDevice device, Pl_Layout_Info *info);
void destroy_pl_layout(VkDevice device, VkPipelineLayout pl_layout);

// Pipeline
VkPipelineShaderStageCreateInfo* create_pl_shaders(u32 count, Shader *shaders);

#if DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        println("\nValidation Layer: %c", pCallbackData->pMessage);

    return VK_FALSE;
}

struct Create_Debug_Messenger_Info {
    VkInstance instance;

    VkDebugUtilsMessageSeverityFlagsEXT severity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT    |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    VkDebugUtilsMessageTypeFlagsEXT type =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT     |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT  |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT;

    PFN_vkDebugUtilsMessengerCallbackEXT callback = debug_messenger_callback;
};

VkDebugUtilsMessengerEXT create_debug_messenger(Create_Debug_Messenger_Info *info);

VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pDebugMessenger);
void DestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT messenger,
        const VkAllocationCallbacks *pAllocator);

inline VkDebugUtilsMessengerCreateInfoEXT fill_vk_debug_messenger_info(Create_Debug_Messenger_Info *info) {
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info =
    {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};

    debug_messenger_create_info.messageSeverity = info->severity;
    debug_messenger_create_info.messageType     = info->type;
    debug_messenger_create_info.pfnUserCallback = info->callback;

    return debug_messenger_create_info;
}

#endif // DEBUG (debug messenger setup)

} // namespace gpu
#endif // include guard
