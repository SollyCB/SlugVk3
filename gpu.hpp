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
#include "string.hpp"

struct Settings {
    VkSampleCountFlagBits sample_count = VK_SAMPLE_COUNT_1_BIT;
    u32 mip_levels                     = 1;
    float anisotropy                   = 0;
};
static Settings global_settings = {};
inline static Settings* get_global_settings() { return &global_settings; }

namespace gpu {

static constexpr u32 DEPTH_ATTACHMENT_COUNT = 1;
static constexpr u32 COLOR_ATTACHMENT_COUNT = 1;
static constexpr u32 VERTEX_STAGE_COUNT     = 1;
static constexpr u32 INDEX_STAGE_COUNT      = 1;
static constexpr u32 TEXTURE_STAGE_COUNT    = 1;
static constexpr u32 UNIFORM_BUFFER_COUNT   = 1;

// Host memory sizes - large enough to not get a warning about allocation size being too small
static constexpr u64 VERTEX_STAGE_SIZE      = 1048576;
static constexpr u64 INDEX_STAGE_SIZE       = 1048576;
static constexpr u64 TEXTURE_STAGE_SIZE     = 1048576 * 8;
static constexpr u64 UNIFORM_BUFFER_SIZE    = 1048576;

static constexpr float VERTEX_DEVICE_SIZE   = 1048576;
static constexpr float INDEX_DEVICE_SIZE    = 1048576;
static constexpr float TEXTURE_DEVICE_SIZE  = 1048576 * 8;

enum Memory_Flag_Bits {
    GPU_MEMORY_UMA_BIT = 0x01,
    GPU_MEMORY_DISCRETE_TRANSFER_BIT = 0x02,
};
typedef u8 Memory_Flags;
struct Gpu_Memory {
    VkDeviceMemory depth_mem        [DEPTH_ATTACHMENT_COUNT];
    VkDeviceMemory color_mem        [COLOR_ATTACHMENT_COUNT];
    VkImage        depth_attachments[DEPTH_ATTACHMENT_COUNT];
    VkImage        color_attachments[COLOR_ATTACHMENT_COUNT];

    VkDeviceMemory vertex_mem_stage [VERTEX_STAGE_COUNT];
    VkDeviceMemory index_mem_stage  [INDEX_STAGE_COUNT];
    VkBuffer       vertex_bufs_stage[VERTEX_STAGE_COUNT];
    VkBuffer       index_bufs_stage [INDEX_STAGE_COUNT];

    VkDeviceMemory vertex_mem_device; // will likely need multiple of these
    VkDeviceMemory index_mem_device;
    VkBuffer       vertex_buf_device;
    VkBuffer       index_buf_device;

    VkDeviceMemory texture_mem_stage [TEXTURE_STAGE_COUNT];
    VkBuffer       texture_bufs_stage[TEXTURE_STAGE_COUNT];
    VkDeviceMemory texture_mem_device;

    VkDeviceMemory uniform_mem [UNIFORM_BUFFER_COUNT];
    VkBuffer       uniform_bufs[UNIFORM_BUFFER_COUNT];

    void *vertex_ptrs   [VERTEX_STAGE_COUNT];
    void *index_ptrs  [INDEX_STAGE_COUNT];
    void *texture_ptrs    [TEXTURE_STAGE_COUNT];
    void *uniform_ptrs[UNIFORM_BUFFER_COUNT];

    Memory_Flags flags; // @Todo
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
    // @Todo Add state for hot reloading checks
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
Set_Allocate_Info insert_shader_set(String *set_name, u32 count, String *files, Shader_Map *map);
Shader_Set* get_shader_set(String *set_name, Shader_Map *map);


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


                                    /* Model Memory Management Begin */

/*
    Allocator Front End: (Front end functions are effectively equivalent unless stated otherwise)

    add_texture(u8 weight, u32 *key) track new image memory: (Tex_Allocator Only)
        - Returns ALLOCATOR_FULL if the allocator cannot track anymore allocations.
        - Writes in 'key' the allocation's identifier.
        - Weight is used to set a priority for the allocation. This effects how likely the allocation
          is to be in memory at any given time.

    begin_allocation() begin a new allocation: (Allocator Only)
        - Return ALLOCATOR_FULL if the allocator is at capacity.
        - Return QUEUE_IN_USE if submit allocation has not been called since calling begin.
    continue_allocation(u64 size, void *ptr) add 'size' to allocation started by queue_begin:
        - Copies data from ptr into its internal storage.
        - Returns STAGE_FULL if the allocation is larger than the internal stage cap.
        - Returns UPLOAD_FULL if the allocation is larger than the internal upload cap.
    submit_allocation(u8 weight, u32 *key) complete an allocation:
        - Writes to 'key' the identifier for the allocation.
        - Weight is used to set a priority for the allocation. This effects how likely the allocation
          is to be in memory at any given time.

********************************************************************************************************
* THE ABOVE FUNCTIONS MUST NOT BE CALLED AFTER ANY OF THE BELOW FUNCTIONS. ADDING ALLOCATIONS/TEXTURES *
* MUST BE DONE DURING SOME DEFINED STAGE OF THE PROGRAM, A STAGE WHICH OCCURS BEFORE QUEUEING STAGING  *
* AND UPLOADS.                                                                                         *
********************************************************************************************************

    queue_begin() prepares the allocator internal queue:
        - Returns QUEUE_IN_USE if submit has not been called since calling begin.
    queue_add(u32 key) queue the allocation corresponding to the key
        - Returns QUEUE_FULL if the allocation would overflow the queue cap.
        - Flags the allocation as TO_DRAW, do not queue an allocation unless this flag will be unset
          later.
    queue_submit() submit the for upload/staging
        - Returns QUEUE_FULL if there are too many allocations waiting to draw.
        - Writes the internal secondary command buffers with the appropriate transfer commands if
          this is a device upload. Binds images to memory if this is a tex queue submission.
*/

enum Allocator_Result {
    ALLOCATOR_RESULT_SUCCESS                    = 0,
    ALLOCATOR_RESULT_QUEUE_IN_USE               = 1,
    ALLOCATOR_RESULT_QUEUE_FULL                 = 2,
    ALLOCATOR_RESULT_STAGE_FULL                 = 3,
    ALLOCATOR_RESULT_UPLOAD_FULL                = 4,
    ALLOCATOR_RESULT_ALLOCATOR_FULL             = 5,
    ALLOCATOR_RESULT_BIND_IMAGE_FAIL            = 6,
    ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY = 7,
    ALLOCATOR_RESULT_ALLOCATION_TOO_LARGE       = 8,
};
enum Allocation_State_Flag_Bits {
    ALLOCATION_STATE_TO_DRAW_BIT   = 0x01,
    ALLOCATION_STATE_STAGED_BIT    = 0x02,
    ALLOCATION_STATE_UPLOADED_BIT  = 0x04,
    ALLOCATION_STATE_TO_STAGE_BIT  = 0x08,
    ALLOCATION_STATE_TO_UPLOAD_BIT = 0x10,
};
typedef u8 Allocation_State_Flags;

struct Allocation {
    u64 size;
    u64 upload_offset;
    u64 stage_offset;
    u64 disk_offset;
};
struct Allocator {
    u32  allocation_cap;
    u32  allocation_count;
    u32 *allocation_indices; // @Note These can probably be u16, allowing for faster simd.
    u8  *allocation_weights;

    Allocation             *allocations;
    Allocation_State_Flags *allocation_states;

    u32  stage_bit_granularity;
    u32  upload_bit_granularity;
    u32  stage_mask_count;
    u32  upload_mask_count;
    u64 *stage_masks;
    u64 *upload_masks;

    u32 to_stage_count;
    u32 to_stage_cap;
    u64 staging_queue_byte_cap; // these are awful names lol
    u64 staging_queue_byte_count;

    u32 to_upload_count;
    u32 to_upload_cap;
    u64 upload_queue_byte_cap;
    u64 upload_queue_byte_count;

    // Only used during the allocation adding phase.
    u64 stage_cap;
    u64 bytes_staged;

    void    *stage_ptr;
    VkBuffer stage;
    VkBuffer upload;

    u64    disk_size;
    FILE  *disk;
    String disk_storage;

    // Secondary command buffers
    VkCommandPool   graphics_cmd_pool;
    VkCommandPool   transfer_cmd_pool;
    VkCommandBuffer graphics_cmd;
    VkCommandBuffer transfer_cmd;
};
struct Allocator_Config {
    u32 allocation_cap;
    u32 to_stage_cap;
    u32 to_upload_cap;
    u32 stage_bit_granularity;
    u32 upload_bit_granularity;
    u64 staging_queue_byte_cap;
    u64 upload_queue_byte_cap;
    u64 stage_cap;
    u64 upload_cap;

    void    *stage_ptr;
    VkBuffer stage;
    VkBuffer upload;
    String   disk_storage;
};
Allocator create_allocator (Allocator_Config *info);
void      destroy_allocator(Allocator *alloc);

Allocator_Result begin_allocation    (Allocator *alloc);
Allocator_Result continue_allocation (Allocator *alloc,  u64 size, void *ptr, u64 *ret_offset);
Allocator_Result submit_allocation   (Allocator *alloc,  u32 *key);

Allocator_Result staging_queue_begin (Allocator *alloc);
Allocator_Result staging_queue_add   (Allocator *alloc, u32 key);
Allocator_Result staging_queue_submit(Allocator *alloc);

Allocator_Result upload_queue_begin  (Allocator *alloc);
Allocator_Result upload_queue_add    (Allocator *alloc,  u32 key);
Allocator_Result upload_queue_submit (Allocator *alloc);

struct Tex_Allocation { // @Note I would like struct to be smaller. Cannot see a good shrink rn...
    u64 stage_offset;
    u64 upload_offset;
    u64 size; // aligned to bit granularity (no reason to keep it as it is)
    u32 width;
    u32 height;
    VkImage image;
    String file_name;
};
struct Tex_Allocator {
    u32  allocation_cap;
    u32  allocation_count;
    u32 *allocation_indices;
    u8  *allocation_weights;

    Tex_Allocation         *allocations;
    Allocation_State_Flags *allocation_states;

    u32  stage_bit_granularity;
    u32  upload_bit_granularity;
    u32  stage_mask_count;
    u32  upload_mask_count;
    u64 *stage_masks;
    u64 *upload_masks;

    u32 to_stage_count;
    u32 to_stage_cap;
    u64 staging_queue_byte_cap; // these are awful names lol
    u64 staging_queue_byte_count;

    u32 to_upload_count;
    u32 to_upload_cap;
    u64 upload_queue_byte_cap;
    u64 upload_queue_byte_count;

    // Only used during the allocation adding phase.
    u64 stage_cap;
    u64 bytes_staged;

    void          *stage_ptr;
    VkBuffer       stage;
    VkDeviceMemory upload;

    u64 *hashes;
    String_Buffer string_buffer;

    // Secondary command buffers
    VkCommandPool   graphics_cmd_pool;
    VkCommandPool   transfer_cmd_pool;
    VkCommandBuffer graphics_cmd;
    VkCommandBuffer transfer_cmd;
};
struct Tex_Allocator_Config {
    u32 allocation_cap;
    u32 to_stage_cap;
    u32 to_upload_cap;
    u32 stage_bit_granularity;
    u32 upload_bit_granularity;
    u32 string_buffer_size;

    u64 staging_queue_byte_cap;
    u64 upload_queue_byte_cap;
    u64 stage_cap;
    u64 upload_cap;

    void          *stage_ptr;
    VkBuffer       stage;
    VkDeviceMemory upload;
};
Tex_Allocator create_tex_allocator (Tex_Allocator_Config *config);
void          destroy_tex_allocator(Tex_Allocator *alloc);

Allocator_Result tex_add_texture         (Allocator *alloc, String *file_name);

Allocator_Result tex_staging_queue_begin (Allocator *alloc);
Allocator_Result tex_staging_queue_add   (Allocator *alloc, u32 key);
Allocator_Result tex_staging_queue_submit(Allocator *alloc);

Allocator_Result tex_upload_queue_begin  (Allocator *alloc);
Allocator_Result tex_upload_queue_add    (Allocator *alloc, u32 key);
Allocator_Result tex_upload_queue_submit (Allocator *alloc);

struct Sampler { // This is potentially a bad name
    VkSamplerAddressMode wrap_s;
    VkSamplerAddressMode wrap_t;
    VkSamplerMipmapMode  mipmap_mode;

    VkFilter mag_filter;
    VkFilter min_filter;

    VkSampler sampler;
};
struct Sampler_Allocator {
    u32 device_cap;

    u32 cap;
    u32 count;
    u32 active;
    HashMap<u64, Sampler> map;

    u64 *hashes;
    u8 *weights;
};
// Set to cap to zero to let the allocator decide a size
Sampler_Allocator create_sampler_allocator(u32 sampler_cap, float anisotropy);
void destroy_sampler_allocator(Sampler_Allocator *alloc);
u64 add_sampler(Sampler_Allocator *alloc, Sampler *sampler_info);
VkSampler get_sampler(Sampler_Allocator *alloc, u64 hash);

    /* Model Memory Management End */

    /* Model Data */
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
struct Texture {
    u32 allocation_key;
    u64 sampler_key; // @Todo Look at if sampler allocator works with the vert/tex index system.
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
    Texture tex_base;
    Texture tex_pbr;
    Texture tex_norm;
    Texture tex_occlusion;
    Texture tex_emissive;
    // @Todo Alpha mode
};
struct Primitive {
    u32 count; // draw count (num indices)
    VkIndexType index_type;

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

    Node         *nodes;
    Mesh         *meshes;
    Material     *mats;
    Skin         *skins;
    Skinned_Mesh *skinned_meshes;

    u32      index_allocation_key;
    u32      vertex_allocation_key;
    Texture *textures;

    void *animation_data; // @Wip
};
struct Static_Model {
    u32 node_count;
    u32 mesh_count;
    u32 mat_count;

    // Node *nodes; <- Idk if this is necessary for a static model
    Mesh     *meshes;
    Material *mats;

    u32      index_allocation_key;
    u32      vertex_allocation_key;
    Texture *textures;
};
struct Model_Allocators {
    Allocator index;
    Allocator vertex;
    Tex_Allocator tex;
    Sampler_Allocator sampler;
};
struct Model_Allocators_Config {}; // @Unused
Model_Allocators init_model_allocators(Model_Allocators_Config *config);
void shutdown_allocators(Model_Allocators *allocs);

Static_Model load_static_model(Model_Allocators *allocs, String *model_name, String *dir);
void free_static_model(Static_Model *model);

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
