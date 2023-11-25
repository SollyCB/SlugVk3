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
#include "shader.hpp" // include g_shader_file_names global array

struct Settings {
    VkSampleCountFlagBits sample_count           = VK_SAMPLE_COUNT_1_BIT;
    u32                   mip_levels             = 1;
    float                 anisotropy             = 0;
    VkViewport            viewport               = {};
    VkRect2D              scissor                = {};
    u32                   pl_dynamic_state_count = 2;
    VkDynamicState        pl_dynamic_states[2]   = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
};
static Settings global_settings = {};
inline static Settings* get_global_settings() { return &global_settings; }

static constexpr u32 DEPTH_ATTACHMENT_COUNT  = 2;
static constexpr u32 SHADOW_ATTACHMENT_COUNT = 2;
static constexpr u32 COLOR_ATTACHMENT_COUNT  = 1; // @Unused Idk when I will do deferred stuff
static constexpr u32 VERTEX_STAGE_COUNT      = 1;
static constexpr u32 INDEX_STAGE_COUNT       = 1;
static constexpr u32 TEXTURE_STAGE_COUNT     = 1;
static constexpr u32 UNIFORM_BUFFER_COUNT    = 2;

// Host memory sizes - large enough to not get a warning about allocation size being too small
static constexpr u64 VERTEX_STAGE_SIZE       = 1048576;
static constexpr u64 INDEX_STAGE_SIZE        = 1048576;
static constexpr u64 TEXTURE_STAGE_SIZE      = 1048576 * 8;
static constexpr u64 UNIFORM_BUFFER_SIZE     = 1048576;

static constexpr float VERTEX_DEVICE_SIZE    = 1048576;
static constexpr float INDEX_DEVICE_SIZE     = 1048576;
static constexpr float TEXTURE_DEVICE_SIZE   = 1048576 * 8;

enum Memory_Flag_Bits {
    GPU_MEMORY_UMA_BIT               = 0x01,
    GPU_MEMORY_DISCRETE_TRANSFER_BIT = 0x02,
};
typedef u8 Memory_Flags;
struct Gpu_Memory {

    u32 attachment_mem_index; 
    u32 vertex_mem_index;
    u32 uniform_mem_index;

    VkDeviceMemory depth_mem            [DEPTH_ATTACHMENT_COUNT];
    VkDeviceMemory shadow_mem           [SHADOW_ATTACHMENT_COUNT];
    VkDeviceMemory color_mem            [COLOR_ATTACHMENT_COUNT];
    VkImage        depth_attachments    [DEPTH_ATTACHMENT_COUNT];
    VkImage        shadow_attachments   [SHADOW_ATTACHMENT_COUNT];
    VkImage        color_attachments    [COLOR_ATTACHMENT_COUNT];

    VkImageView    depth_views          [DEPTH_ATTACHMENT_COUNT];
    VkImageView    shadow_views         [SHADOW_ATTACHMENT_COUNT];

    VkDeviceMemory vertex_mem_stage     [VERTEX_STAGE_COUNT];
    VkDeviceMemory index_mem_stage      [INDEX_STAGE_COUNT];
    VkBuffer       vertex_bufs_stage    [VERTEX_STAGE_COUNT];
    VkBuffer       index_bufs_stage     [INDEX_STAGE_COUNT];

    VkDeviceMemory vertex_mem_device;
    VkDeviceMemory index_mem_device;
    VkBuffer       vertex_buf_device;
    VkBuffer       index_buf_device;

    VkDeviceMemory texture_mem_stage    [TEXTURE_STAGE_COUNT];
    VkBuffer       texture_bufs_stage   [TEXTURE_STAGE_COUNT];
    VkDeviceMemory texture_mem_device;

    VkDeviceMemory uniform_mem          [UNIFORM_BUFFER_COUNT];
    VkBuffer       uniform_bufs         [UNIFORM_BUFFER_COUNT];

    void *vertex_ptrs                   [VERTEX_STAGE_COUNT];
    void *index_ptrs                    [INDEX_STAGE_COUNT];
    void *texture_ptrs                  [TEXTURE_STAGE_COUNT];
    void *uniform_ptrs                  [UNIFORM_BUFFER_COUNT];

    Memory_Flags flags;
};
struct Shader {
    Shader_Id id;
    u32 layout_index;
    u32 layout_count;

    VkShaderModule        module;
    VkShaderStageFlagBits stage;

    #if DEBUG
    u32 *layout_indices;
    #endif
};
struct Shader_Memory { // @Note This is a terrible name. @Todo Come up with something better.
    u32 shader_cap            = 128;
    u32 descriptor_set_cap    = 128; // @Note Idk how low this is.

    u32 shader_count;
    u32 layout_count;

    Shader                *shaders;
    VkDescriptorSetLayout *layouts;

    u64            descriptor_buffer_size = 1048576;
    VkBuffer       sampler_descriptor_buffer;
    VkBuffer       resource_descriptor_buffer;
    VkDeviceMemory descriptor_buffer_memory;
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_info;

    const char *entry_point = "main";

    // VkDescriptorPool      *descriptor_pools;
    // VkDescriptorSet       *descriptor_sets;
};
struct Gpu_Info {
    VkPhysicalDeviceProperties props;
};
struct Gpu {
    Gpu_Info info;

    VkInstance       instance;
    VkDevice         device;
    VkPhysicalDevice phys_device;

    VkQueue graphics_queue;
    VkQueue present_queue;
    VkQueue transfer_queue;

    VkPipelineCache pl_cache;

    u32 graphics_queue_index;
    u32 present_queue_index;
    u32 transfer_queue_index;

    Gpu_Memory    memory;
    Shader_Memory shader_memory;
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
VkDevice create_device (Gpu *gpu);
VkQueue  create_queue  (Gpu *gpu);
void     destroy_device(Gpu *gpu);
void     destroy_queue (Gpu *gpu);

// Surface and Swapchain
struct Window {
    VkSwapchainKHR            swapchain;
    VkSwapchainCreateInfoKHR  info;
    u32                       image_count = 2;
    VkImage                  *images;
    VkImageView              *views;
};
Window* get_window_instance();

void init_window(Gpu *gpu, Glfw *glfw);
void kill_window(Gpu *gpu, Window *window);

VkSurfaceKHR create_surface (VkInstance vk_instance, Glfw *glfw);
void         destroy_surface(VkInstance vk_instance, VkSurfaceKHR vk_surface);

// @Note this function does a lot to initialize window members, because I consider
// these elems to be parts of the swapchain, not distinct things.
VkSwapchainKHR create_swapchain  (Gpu *gpu, VkSurfaceKHR vk_surface);
void           destroy_swapchain (VkDevice vk_device, Window *window);
VkSwapchainKHR recreate_swapchain(Gpu *gpu, Window *window);

inline static void reset_viewport_and_scissor_to_window_extent() {
    VkViewport viewport = {};
    viewport.x          = 0;
    viewport.y          = 0;
    VkExtent2D *extent  = &get_window_instance()->info.imageExtent;
    viewport.width      = extent->width;
    viewport.height     = extent->height;
    viewport.minDepth   = 0.0;
    viewport.maxDepth   = 1.0;

    VkRect2D rect = {
        .offset = {0, 0},
        .extent = get_window_instance()->info.imageExtent,
    };

    Settings *settings = get_global_settings();
    settings->viewport = viewport;
    settings->scissor  = rect;
}

// Memory -- struct declarations above gpu
void allocate_memory(); // @Note I could remove these functions and structs from the header file. Probably should.
void free_memory();

// Shaders -- struct declarations above gpu
Shader_Memory init_shaders();
void shutdown_shaders(Shader_Memory *mem);

VkPipelineCache pl_load_cache();
void pl_store_cache();

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
    *                                         AND UPLOADS.                                                 *
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

    @Todo @Note The way I load the model data into allocators should be thought about: Currently I just
    load all the model data at once and stick them in an allocator. Really I could separate these meshes
    out, as they do not HAVE to be in memory at the same time as other opaque stuff: I can upload these
    meshes after the deferred renderer has run. This way I can stuff more models into video memory at once.
    (Mike Acton homogeneous data type shit.)
*/

enum Gpu_Allocator_Result {
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
enum Gpu_Allocation_State_Flag_Bits {
    ALLOCATION_STATE_TO_DRAW_BIT   = 0x01,
    ALLOCATION_STATE_STAGED_BIT    = 0x02,
    ALLOCATION_STATE_UPLOADED_BIT  = 0x04,
    ALLOCATION_STATE_TO_STAGE_BIT  = 0x08,
    ALLOCATION_STATE_TO_UPLOAD_BIT = 0x10,
};
typedef u8 Gpu_Allocation_State_Flags;

struct Gpu_Allocation {
    u64 size;
    u64 upload_offset;
    u64 stage_offset;
    u64 disk_offset;
};
struct Gpu_Allocator {
    u32  allocation_cap;
    u32  allocation_count;
    u32 *allocation_indices; // @Note These can probably be u16, allowing for faster simd.
    u8  *allocation_weights;

    Gpu_Allocation             *allocations;
    Gpu_Allocation_State_Flags *allocation_states;

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
struct Gpu_Allocator_Config {
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
Gpu_Allocator create_allocator (Gpu_Allocator_Config *info);
void          destroy_allocator(Gpu_Allocator *alloc);

Gpu_Allocator_Result begin_allocation    (Gpu_Allocator *alloc);
Gpu_Allocator_Result continue_allocation (Gpu_Allocator *alloc,  u64 size, void *ptr, u64 *ret_offset);
Gpu_Allocator_Result submit_allocation   (Gpu_Allocator *alloc,  u32 *key);

Gpu_Allocator_Result staging_queue_begin (Gpu_Allocator *alloc);
Gpu_Allocator_Result staging_queue_add   (Gpu_Allocator *alloc, u32 key);
Gpu_Allocator_Result staging_queue_submit(Gpu_Allocator *alloc);

Gpu_Allocator_Result upload_queue_begin  (Gpu_Allocator *alloc);
Gpu_Allocator_Result upload_queue_add    (Gpu_Allocator *alloc,  u32 key);
Gpu_Allocator_Result upload_queue_submit (Gpu_Allocator *alloc);

struct Gpu_Tex_Allocation { // @Note I would like struct to be smaller. Cannot see a good shrink rn...
    u64 stage_offset;
    u64 upload_offset;
    u64 size; // aligned to bit granularity (no reason to keep it as it is)
    u32 width;
    u32 height;
    VkImage image;
    String  file_name;
};
struct Gpu_Tex_Allocator {
    u32  allocation_cap;
    u32  allocation_count;
    u32 *allocation_indices;
    u8  *allocation_weights;

    Gpu_Tex_Allocation         *allocations;
    Gpu_Allocation_State_Flags *allocation_states;

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
struct Gpu_Tex_Allocator_Config {
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
Gpu_Tex_Allocator create_tex_allocator (Gpu_Tex_Allocator_Config *config);
void              destroy_tex_allocator(Gpu_Tex_Allocator *alloc);

Gpu_Allocator_Result tex_add_texture(Gpu_Allocator *alloc, String *file_name);

Gpu_Allocator_Result tex_staging_queue_begin (Gpu_Allocator *alloc);
Gpu_Allocator_Result tex_staging_queue_add   (Gpu_Allocator *alloc, u32 key);
Gpu_Allocator_Result tex_staging_queue_submit(Gpu_Allocator *alloc);

Gpu_Allocator_Result tex_upload_queue_begin  (Gpu_Allocator *alloc);
Gpu_Allocator_Result tex_upload_queue_add    (Gpu_Allocator *alloc, u32 key);
Gpu_Allocator_Result tex_upload_queue_submit (Gpu_Allocator *alloc);

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
    u8  *weights;
};
// Set cap to zero to let the allocator decide a size
Sampler_Allocator create_sampler_allocator (u32 sampler_cap, float anisotropy);
void              destroy_sampler_allocator(Sampler_Allocator *alloc);

u64               add_sampler(Sampler_Allocator *alloc, Sampler *sampler_info);
VkSampler         get_sampler(Sampler_Allocator *alloc, u64 hash);

struct Uniform_Allocator {
    u64 cap;
    u64 used;
    u8 *mem;
    VkBuffer buf;
};
inline static Uniform_Allocator create_uniform_allocator(u32 gpu_mem_index, u64 offset, u64 size) {
    assert(size < UNIFORM_BUFFER_SIZE - offset && "Size too big");
    assert(offset % 256 == 0 && "Offset must have minimum alignment");

    Gpu_Memory *gpu_mem = &get_gpu_instance()->memory;

    Uniform_Allocator ret;
    ret.cap  = size;
    ret.used = 0;
    ret.mem  = (u8*)gpu_mem->uniform_ptrs[gpu_mem_index] + offset;
    ret.buf  = gpu_mem->uniform_bufs[gpu_mem_index];

    return ret;
}
inline static u8* uniform_malloc(Uniform_Allocator *allocator, u64 size) {
    // pad to alignment
    allocator->used = align(allocator->used, 256);

    u8 *ret = allocator->mem + allocator->used;

    allocator->used += size;

    //
    // @Todo Handle failure here better. But tbh, this should not need worrying about for a really long time:
    // how much data is really going to be required for animation data? I do not think that it is THAT much...
    // There is really no point thinking about it in the same way as vertex data or texture data I am pretty sure.
    //
    assert(allocator->used <= allocator->cap && "Uniform Allocator Overflow");
    return ret;
}
inline static void uniform_allocator_reset(Uniform_Allocator *allocator) {
    allocator->used = 0;
}
inline static void uniform_allocator_reset_and_zero(Uniform_Allocator *allocator) {
    memset(allocator->mem, 0, allocator->used);
    allocator->used = 0;
}
    /* Model Memory Management End */

    /* Model Data */
struct Node;
struct Skin {
    u32       joint_count;
    Node     *joints;
    Node     *skeleton;
    VkBuffer  matrices;
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
    u64 offset_position;
    u64 offset_normal;
    u64 offset_tangent;
    u64 offset_tex_coords;

    Material *material;
};
struct Skinned_Primitive {
    Primitive primitive;
    u64 offset_joints;
    u64 offset_weights;
};
struct Pl_Primitive_Info {
    VkPrimitiveTopology topology;
    u32 stride_position;
    u32 stride_normal;
    u32 stride_tangent;
    u32 stride_tex_coords;
    VkFormat fmt_position;
    VkFormat fmt_normal;
    VkFormat fmt_tangent;
    VkFormat fmt_tex_coords;
};
struct Pl_Prim_Info_Skinned {
    Pl_Primitive_Info prim;
    u32 stride_joints;
    u32 stride_weights;
    VkFormat fmt_joints;
    VkFormat fmt_weights;
};
struct Mesh {
    u32 count;
    Primitive         *primitives;
    Pl_Primitive_Info *pl_infos;
};
struct Skinned_Mesh {
    u32 count;
    Skinned_Primitive *primitives;
    Pl_Primitive_Info *pl_infos;
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
    // Texture *textures;
};
struct Model_Allocators {
    Gpu_Allocator     index;
    Gpu_Allocator     vertex;
    Gpu_Tex_Allocator tex;
    Sampler_Allocator sampler;
};
struct Model_Allocators_Config {}; // @Unused I am just setting some arbitrary defaults atm.

Model_Allocators init_model_allocators(Model_Allocators_Config *config);
void             shutdown_allocators  (Model_Allocators *allocs);

Static_Model load_static_model(Model_Allocators *allocs, String *model_name, String *dir);
void         free_static_model(Static_Model *model);


    /* Renderpass Framebuffer Pipeline */

struct Pl_Layout {
    u32                              stage_count;
    u32                              set_count;
    VkPipelineShaderStageCreateInfo *stages;
    VkPipelineLayout                 pl_layout;
};
void pl_get_stages_and_layout(u32 count, u32 *shader_indices, u32 push_constant_count, VkPushConstantRange *push_constants, Pl_Layout *layout);

void pl_get_vertex_input_and_assembly_static(Pl_Primitive_Info *primitive,
                                             VkPipelineVertexInputStateCreateInfo *ret_input_info,
                                             VkPipelineInputAssemblyStateCreateInfo *ret_assembly_info);
void pl_get_viewport_and_scissor(VkPipelineViewportStateCreateInfo *ret_info);

//
// Ik the below two are not in keeping with every where else in passing func args, but create graphics pipelines
// is a pretty unique setup so I am ok with it being different. This allows easily zeroing stuff, not creating
// new local vars etc...
//
struct Pl_Rasterization_Args {
    bool wire_frame;
    bool cull_front;
    bool cull_back;
    bool clockwise_front_face;
};
void pl_get_rasterization(Pl_Rasterization_Args args, VkPipelineRasterizationStateCreateInfo *ret_info);
void pl_get_multisample(VkPipelineMultisampleStateCreateInfo *ret_info);

struct Pl_Depth_Stencil_Args { // @Todo @BoolsInStructs These should be done as flags
    bool             depth_test_enable;
    bool             depth_write_enable;
    bool             stencil_test_enable;
    VkCompareOp      depth_compare_op;
    VkStencilOpState stencil_op_front;
    VkStencilOpState stencil_op_back;
};
void pl_get_depth_stencil(Pl_Depth_Stencil_Args args, VkPipelineDepthStencilStateCreateInfo *ret_info);

// Blend functions -- I plan to add more for quickly filling out common blend options
void pl_attachment_get_no_blend   (VkPipelineColorBlendAttachmentState *ret_blend_function);
void pl_attachment_get_alpha_blend(VkPipelineColorBlendAttachmentState *ret_blend_function);
void pl_get_color_blend           (u32 attachment_count,
                                   VkPipelineColorBlendAttachmentState *attachment_blend_states,
                                   VkPipelineColorBlendStateCreateInfo *ret_info);

void pl_get_dynamic(VkPipelineDynamicStateCreateInfo *ret_info);

// Begin renderpass
struct Rp_Config { // Bad name, should be something like "_Attachments"
    VkImageView present;
    VkImageView depth;
    VkImageView shadow;
};
void rp_forward_shadow_basic(Rp_Config *config, VkRenderPass *renderpass, VkFramebuffer *framebuffer);

// Final pipeline creation
struct Pl_Final {
    u32         count;
    VkPipeline *pipelines;
    Pl_Layout   layout;
};
Pl_Final pl_create_basic (VkRenderPass renderpass, u32 count, Static_Model *models);
Pl_Final pl_create_shadow(VkRenderPass renderpass, u32 count, Static_Model *models);

inline static void pl_destroy_final(Pl_Final *pl) {
    VkDevice device = get_gpu_instance()->device;
    for(u32 i = 0; i < pl->count; ++i)
        vkDestroyPipeline(device, pl->pipelines[i], ALLOCATION_CALLBACKS);

    vkDestroyPipelineLayout(device, pl->layout.pl_layout, ALLOCATION_CALLBACKS);
}

struct Draw_Final_Basic {
    Pl_Final      pl_basic;
    Pl_Final      pl_shadow;
    VkRenderPass  renderpass;
    VkFramebuffer framebuffer;
};
struct Draw_Final_Basic_Config {
    u32           count;
    Static_Model *models;
    Rp_Config     rp_config;
};
Draw_Final_Basic draw_create_basic(Draw_Final_Basic_Config *config);

inline static void draw_destroy_basic(Draw_Final_Basic *draw) {
    pl_destroy_final(&draw->pl_basic);
    pl_destroy_final(&draw->pl_shadow);

    VkDevice device = get_gpu_instance()->device;
    vkDestroyRenderPass(device, draw->renderpass, ALLOCATION_CALLBACKS);
    vkDestroyFramebuffer(device, draw->framebuffer, ALLOCATION_CALLBACKS);
}

// Sync
inline static VkFence create_fence(bool signalled) {
    VkFenceCreateInfo info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    info.flags = (VkFenceCreateFlags)signalled;

    VkFence fence;
    auto check = vkCreateFence(get_gpu_instance()->device, &info, ALLOCATION_CALLBACKS, &fence);
    return fence;
}
inline static void destroy_fence(VkFence fence) {
    vkDestroyFence(get_gpu_instance()->device, fence, ALLOCATION_CALLBACKS);
}
inline static void reset_fence(VkFence fence) {
    vkResetFences(get_gpu_instance()->device, 1, &fence);
}
inline static void wait_fence(VkFence fence) {
    vkWaitForFences(get_gpu_instance()->device, 1, &fence, 1, 10e9);
}
inline static void wait_and_reset_fence(VkFence fence) {
    wait_fence(fence);
    reset_fence(fence);
}
inline static VkSemaphore create_semaphore() {
    VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkSemaphore ret;
    auto check = vkCreateSemaphore(get_gpu_instance()->device, &info, ALLOCATION_CALLBACKS, &ret);
    return ret;
}
inline static void destroy_semaphore(VkSemaphore semaphore) {
    vkDestroySemaphore(get_gpu_instance()->device, semaphore, ALLOCATION_CALLBACKS);
}

#if DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_messenger_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        println("\nValidation Layer: %s", pCallbackData->pMessage);

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

#endif // include guard
