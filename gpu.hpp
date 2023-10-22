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
#include "math.hpp"

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


/* Model Loading */
namespace model {
enum class Allocation_State : u32 {
    NONE = 0,
    STAGED = 1,
    TO_UPLOAD = 2,
    UPLOADED = 3,
    DRAWN = 4,
};
struct Allocation {
    u64 stage_offset;
    u64 upload_offset;
    u64 prev_offset;
    u64 size;
    Allocation_State state;
};
struct Allocator {
    u32 alloc_cap;

    u32 staged; // end of staged field
    u32 to_upload; // index of first to upload
    u32 uploaded; // index of most recent upload

    // Byte counts
    u64 stage_cap;
    u64 stage_count;
    u64 upload_cap;
    u64 upload_count;

    VkBuffer stage;
    VkBuffer upload;

    Allocation *allocations;
};
struct Texture {
    String uri;

    u32 width;
    u32 height;

    VkImage image;
    Allocation_State state;

    VkSamplerAddressMode wrap_s;
    VkSamplerAddressMode wrap_t;
    VkFilter mag_filter;
    VkFilter min_filter;
};
struct Tex_Allocator {
    u32 tex_count;
    u32 tex_cap;

    u64 stage_byte_count;
    u64 stage_byte_cap;
    u64 upload_byte_count;
    u64 upload_byte_cap;

    Texture *textures;
    VkBuffer stage;
    VkDeviceMemory upload;
};
/*
    Buffer Allocation API:
    Assumes that buffer uploads will consist of multiple smaller allocations which can
    be grouped into a large allocation.
    'Allocator' does not evict to disk, vertex data always in memory.
    'Tex_Allocator' can evict textures back to disk if texture memory is low.

    TLDR, 'Allocator' greater control by the model, 'Tex_Allocator' controlled by allocator.
*/
void begin(Allocator *alloc);
// Push size to queue; write to pushed size
void* queue(Allocator *alloc, u64 size, u64 *offset);
// Signal queue complete; return size and offset of final total allocation
Allocation* stage(Allocator *alloc);
// Mark data for gpu transfer
VkBuffer upload(Allocator *alloc, Allocation *allocation);

Texture* tex_add(Tex_Allocator *alloc, String uri);
void tex_upload(Allocator *alloc);

struct Node;
struct Skin {
    u32 joint_count;
    Node *joints;
    Node *skeleton;
    VkBuffer matrices;
};
struct Trs {
    Vec3 trans;
    Vec4 rot;
    Vec3 scale;
};
struct Material {
    float base_factors[4];
    float metal_factor;
    float rough_factor;
    float norm_scale;
    float occlusion_strength;
    float emissive_factors[3];

    // @Note I do not know how to resolve texture coordinates defined on both the primitive
    // and the material...?? I will experiment I guess??

    // Texture indices
    Texture *tex_base;
    Texture *tex_pbr;
    Texture *tex_norm;
    Texture *tex_occlusion;
    Texture *tex_emissive;
    // @Todo Alpha mode
};
struct Primitive {
    u32 count; // draw count (num indices)
    VkIndexType index_type;

    VkBuffer indices;
    VkBuffer vertices;

    u64 offset_index;
    u64 offset_pos;
    u64 offset_norm;
    u64 offset_tang;
    u64 offset_tex;

    Material *material;
};
struct Skinned_Primitive {
    Primitive prim;
    u64 offset_joints;
    u64 offset_weights;
};
struct Pl_Prim_Info {
// Not sure how important these things are: I am already assuming bindings and location etc,
// so also assuming the format and the stride of the data is not a stretch...
    u32 strides[4];
    VkFormat formats[4];
};
struct Pl_Prim_Info_Skinned {
// Not sure how important these things are: I am already assuming bindings and location etc,
// so also assuming the format and the stride of the data is not a stretch...
    Pl_Prim_Info prim;
    u32 strides[2];
    VkFormat formats[2];
};
struct Mesh {
    u32 count;
    Primitive *primitives;
    Pl_Prim_Info *pl_infos;
};
struct Skinned_Mesh {
    u32 count;
    Skinned_Primitive *primitives;
    Pl_Prim_Info *pl_infos;
};
struct Node {
union {
    Trs trs;
    Mat4 mat;
};
    u32 child_count;
    Node *children;
    Mesh *mesh;
    Skin *skin;
};
struct Model {
    u32 node_count;
    u32 mesh_count;
    u32 skinned_mesh_count;
    u32 skin_count;
    u32 material_count;

    Node *nodes;
    Mesh *meshes;
    Skinned_Mesh *skinned_meshes;
    Skin *skins;
    Material *mats;

    Allocation *index_alloc;
    Allocation *vert_alloc;
    Allocation *tex_allocations;

    void *animation_data; // @Wip
};
struct Static_Model {
    u32 node_count;
    u32 mesh_count;
    u32 mat_count;

    // Node *nodes; <- Idk if this is necessary for a static model
    Mesh *meshes;
    Material *mats;

    Allocation *index_alloc;
    Allocation *vert_alloc;
    Allocation *tex_alloc;
};
struct Model_Allocators {
    Allocator *index;
    Allocator *vert;
    Tex_Allocator *tex;
};
Static_Model load_static_model(Model_Allocators *allocs, String *model_name, String *dir);
void free_static_model(Static_Model *model);

} // namespace model

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
