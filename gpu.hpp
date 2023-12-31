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
#include "simd.hpp"

static const u32 g_thread_count = 4;
static const u32 g_frame_count  = 2;
static       u32 g_frame_index  = 0;

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
static constexpr u32 UNIFORM_BUFFER_COUNT    = g_frame_count * g_thread_count;

// Host memory sizes - large enough to not get a warning about allocation size being too small
static constexpr u64 VERTEX_STAGE_SIZE       = 1048576;
static constexpr u64 INDEX_STAGE_SIZE        = 1048576;
static constexpr u64 TEXTURE_STAGE_SIZE      = 1048576 * 8;
static constexpr u64 UNIFORM_BUFFER_SIZE     = 1048576 / (g_frame_count * g_thread_count);

static constexpr u64 VERTEX_DEVICE_SIZE    = 1048576;
static constexpr u64 INDEX_DEVICE_SIZE     = 1048576;
static constexpr u64 TEXTURE_DEVICE_SIZE   = 1048576 * 8;

static constexpr u64 DESCRIPTOR_BUFFER_SIZE = 1048576;

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

    VkDeviceMemory uniform_memory;
    VkBuffer       uniform_buffers      [UNIFORM_BUFFER_COUNT];

    void *vertex_ptrs                   [VERTEX_STAGE_COUNT];
    void *index_ptrs                    [INDEX_STAGE_COUNT];
    void *texture_ptrs                  [TEXTURE_STAGE_COUNT];
    void *uniform_ptrs                  [UNIFORM_BUFFER_COUNT];

    VkBuffer       sampler_descriptor_buffer;
    VkBuffer       resource_descriptor_buffer;
    VkDeviceMemory descriptor_buffer_memory;

    u8 *sampler_descriptor_ptr;
    u8 *resource_descriptor_ptr;

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
    u64                   *layout_sizes;

    const char *entry_point = "main";

    // For set allocation of materials. We always just allocate per material from the descriptor allocators
    // a descriptor set layout size equivalent to that required for an array of 5 combined image samplers,
    // and a descriptor set layout size required for a ubo.
    u64 material_sampler_array_set_size;
    u64 material_ubo_set_size;
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

-----------------------------------------------------------------------------------------------------------------------

                                ** Notes On Multithreading Allocators ** - Sol 26 Nov 2023

    -- UPDATE: The below is whatever, better explanation is "Allocator exist on a thread, can call thread pool 
               to help create cmd info structs from its read only data". Easy. - Sol 13 Dec 2023

    I have just had a think about multithreading these things, and my thoughts are "do not introduce contention".
    It is not clear if this makes the programming easier or harder (as wrapping stuff in reference counts and mutexes
    and writing whereever is also easy) but it does give me a clearer design model.

    My plan is to have one thread controlling the primary sections of the allocator, such as the bit masks for which
    allocation ranges are made free and whatever (there is no benefit to threading this, I am sure), and then
    this thread can dispatch jobs to worker threads, such as creating the copy infos and the pipeline barriers:
    as there will be a known number of these, each thread can be given some section of the array, and then it can
    fill its section freely. This is at the end of the allocator pipeline.

    For earlier in the pipeline (i.e. adding allocations to queue), this would again be managed by the controlling
    thread, and work can be submitted to it in batches: for instance, threads can each own an array which they
    fill with their desired allocations. Then these arrays can each be submitted to the controlling thread. This
    thread can then chew through the arrays, and add the allocations to queues and stops when the queue is full.
    Then it can return where in the array it got to, so the threads know how much work was queued. Then the
    main thread can submit the queues, and return whether the submissions were successful. Then the threads know
    whether to submit their arrays from the beginning again at a later stage, or if they can begin from an offset.
*/

enum Gpu_Allocator_Result {
    GPU_ALLOCATOR_RESULT_SUCCESS                    = 0,
    GPU_ALLOCATOR_RESULT_QUEUE_IN_USE               = 1,
    GPU_ALLOCATOR_RESULT_QUEUE_FULL                 = 2,
    GPU_ALLOCATOR_RESULT_STAGE_FULL                 = 3,
    GPU_ALLOCATOR_RESULT_UPLOAD_FULL                = 4,
    GPU_ALLOCATOR_RESULT_ALLOCATOR_FULL             = 5,
    GPU_ALLOCATOR_RESULT_BIND_IMAGE_FAIL            = 6,
    GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY = 7,
    GPU_ALLOCATOR_RESULT_ALLOCATION_TOO_LARGE       = 8,
};

// @Todo This should be debug only.
inline static void CHECK_GPU_ALLOCATOR_RESULT(Gpu_Allocator_Result res) {
    switch(res) {
    case GPU_ALLOCATOR_RESULT_QUEUE_IN_USE:
        assert(false && "GPU_ALLOCATOR_RESULT_QUEUE_IN_USE");
    case GPU_ALLOCATOR_RESULT_QUEUE_FULL:
        assert(false && "GPU_ALLOCATOR_RESULT_QUEUE_FULL");
        break;
    case GPU_ALLOCATOR_RESULT_STAGE_FULL:
        assert(false && "GPU_ALLOCATOR_RESULT_STAGE_FULL");
        break;
    case GPU_ALLOCATOR_RESULT_UPLOAD_FULL:
        assert(false && "GPU_ALLOCATOR_RESULT_UPLOAD_FULL");
        break;
    case GPU_ALLOCATOR_RESULT_ALLOCATOR_FULL:
        assert(false && "GPU_ALLOCATOR_RESULT_ALLOCATOR_FULL");
        break;
    case GPU_ALLOCATOR_RESULT_BIND_IMAGE_FAIL:
        assert(false && "GPU_ALLOCATOR_RESULT_BIND_IMAGE_FAIL");
        break;
    case GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY:
        assert(false && "GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY");
        break;
    case GPU_ALLOCATOR_RESULT_ALLOCATION_TOO_LARGE:
        assert(false && "GPU_ALLOCATOR_RESULT_ALLOCATION_TOO_LARGE");
        break;
    default:
        break;
    }
}

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

    u64 upload_cap;
    u64 stage_cap;

    void    *stage_ptr;
    VkBuffer stage;
    VkBuffer upload;

    u64    disk_size;
    FILE  *disk;
    String disk_storage;

    // Secondary command buffers
    VkCommandPool   graphics_cmd_pools[2];
    VkCommandPool   transfer_cmd_pools[2];
    VkCommandBuffer graphics_cmds[2];
    VkCommandBuffer transfer_cmds[2];
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
Gpu_Allocator_Result create_allocator (Gpu_Allocator_Config *config, Gpu_Allocator *allocator);
void                 destroy_allocator(Gpu_Allocator *alloc);

Gpu_Allocator_Result begin_allocation    (Gpu_Allocator *alloc);
Gpu_Allocator_Result continue_allocation (Gpu_Allocator *alloc, u64 size, void *ptr);
Gpu_Allocator_Result submit_allocation   (Gpu_Allocator *alloc, u32 *key);

Gpu_Allocator_Result staging_queue_begin (Gpu_Allocator *alloc);
Gpu_Allocator_Result staging_queue_add   (Gpu_Allocator *alloc, u32 key, bool adjust_weights);
Gpu_Allocator_Result staging_queue_submit(Gpu_Allocator *alloc);

inline static void staging_queue_remove(Gpu_Allocator *alloc, u32 key) {
    u32 idx = alloc->allocation_indices[key];

    Gpu_Allocation_State_Flags state            = alloc->allocation_states[idx]; 
    u64                        bit_aligned_size = align(alloc->allocations[idx].size, alloc->stage_bit_granularity);

    alloc->staging_queue_byte_count -= bit_aligned_size & max64_if_true(state & ALLOCATION_STATE_TO_STAGE_BIT);
    alloc->to_stage_count           -= (state & ALLOCATION_STATE_TO_STAGE_BIT) != 0;

    // Allocator should not stage this allocation or wait on it to draw.
    alloc->allocation_states[idx] &= ~(ALLOCATION_STATE_TO_STAGE_BIT | ALLOCATION_STATE_TO_DRAW_BIT);

    return;
}

inline static void staging_queue_make_empty(Gpu_Allocator *alloc) {
    /*
       @Note This does potentially more than is necessary because the operation is so cheap I would rather avoid
       bugs by ensuring any possible weird program state is handled.
    */

    // Find every allocation which is 'TO_STAGE' but not 'TO_UPLOAD' and clear the 'TO_DRAW' flag
    simd_update_flags_u8(alloc->allocation_count, alloc->allocation_states, ALLOCATION_STATE_TO_STAGE_BIT,
                         ALLOCATION_STATE_TO_UPLOAD_BIT, 0x0, ALLOCATION_STATE_TO_DRAW_BIT);
    // Find every allocation which is 'TO_STAGE' and clear the 'TO_STAGE' flag
    simd_update_flags_u8(alloc->allocation_count, alloc->allocation_states, ALLOCATION_STATE_TO_STAGE_BIT,
                         0x0, 0x0, ALLOCATION_STATE_TO_STAGE_BIT);

    alloc->to_stage_count           = 0;
    alloc->staging_queue_byte_count = 0;
}

Gpu_Allocator_Result upload_queue_begin     (Gpu_Allocator *alloc);
Gpu_Allocator_Result upload_queue_add       (Gpu_Allocator *alloc, u32 key, bool adjust_weights);
Gpu_Allocator_Result upload_queue_submit    (Gpu_Allocator *alloc);

inline static void upload_queue_remove(Gpu_Allocator *alloc, u32 key) {
    u32 idx = alloc->allocation_indices[key];

    Gpu_Allocation_State_Flags state            = alloc->allocation_states[idx]; 
    u64                        bit_aligned_size = align(alloc->allocations[idx].size, alloc->upload_bit_granularity);

    alloc->upload_queue_byte_count -= bit_aligned_size & max64_if_true(state & ALLOCATION_STATE_TO_UPLOAD_BIT);
    alloc->to_upload_count         -= (state & ALLOCATION_STATE_TO_UPLOAD_BIT) != 0;

    // Allocator should not upload this allocation or wait on it to draw.
    alloc->allocation_states[idx] &= ~(ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_TO_DRAW_BIT);

    return;
}

inline static void upload_queue_make_empty(Gpu_Allocator *alloc) {
    /*
       @Note This does potentially more than is necessary because the operation is so cheap I would rather avoid
       bugs by ensuring any possible weird program state is handled.
    */

    // Find every allocation which is 'TO_STAGE' but not 'TO_UPLOAD' and clear the 'TO_DRAW' flag
    simd_update_flags_u8(alloc->allocation_count, alloc->allocation_states, ALLOCATION_STATE_TO_UPLOAD_BIT,
                         ALLOCATION_STATE_TO_STAGE_BIT, 0x0, ALLOCATION_STATE_TO_DRAW_BIT);
    // Find every allocation which is 'TO_STAGE' and clear the 'TO_STAGE' flag
    simd_update_flags_u8(alloc->allocation_count, alloc->allocation_states, ALLOCATION_STATE_TO_UPLOAD_BIT,
                         0x0, 0x0, ALLOCATION_STATE_TO_UPLOAD_BIT);

    alloc->to_upload_count         = 0;
    alloc->upload_queue_byte_count = 0;
}

inline static Gpu_Allocation* gpu_get_allocation(Gpu_Allocator *allocator, u32 allocation_key) {
    u32 allocation_index = allocator->allocation_indices[allocation_key];
    return &allocator->allocations[allocation_index];
}

inline static void staging_queue_upload_queue_remove(Gpu_Allocator *alloc, u32 key) {
    u32 idx = alloc->allocation_indices[key];

    u64 bit_aligned_size_stage  = align(alloc->allocations[idx].size, alloc->stage_bit_granularity);
    u64 bit_aligned_size_upload = align(alloc->allocations[idx].size, alloc->upload_bit_granularity);

    Gpu_Allocation_State_Flags state = alloc->allocation_states[idx]; 

    alloc->upload_queue_byte_count  -= bit_aligned_size_upload & max64_if_true(state & ALLOCATION_STATE_TO_UPLOAD_BIT);
    alloc->staging_queue_byte_count -= bit_aligned_size_stage  & max64_if_true(state & ALLOCATION_STATE_TO_STAGE_BIT);

    alloc->to_upload_count -= (state & ALLOCATION_STATE_TO_UPLOAD_BIT) != 0;
    alloc->to_stage_count  -= (state & ALLOCATION_STATE_TO_STAGE_BIT)  != 0;

    // Allocator should not upload this allocation or wait on it to draw.
    alloc->allocation_states[idx] &= ~(ALLOCATION_STATE_TO_STAGE_BIT | ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_TO_DRAW_BIT);

    return;
}

//
// @Note I would like struct to be smaller. Cannot see a good shrink rn...
//
// UPDATE: @Todo Switch the tex allocator to not shuffle allocations according to weight, instead just search for the
// smallest weight and use its index to look up the texture allocation from the unchanging array,
// because this struct is just way too big to justify the shuffle caching method I am using... - Sol 9 Dec 2023
//
// UPDATE: Nvm, I have decided to stick with the shuffle system: although it absolutely is expensive to do the
// allocation shuffle every time we add to the queue, this can be just be a job and another thread can take this
// relatively expensive work. I am happy with this, because it means that submission will be as fast as possible.
// This explanation makes sense because the work of queueing can just slow down one worker thread, at a time when
// this is not an issue: there will be lots of allocations from lots of models that will want to be queued, which
// will create lots of consistent, on going work. However submission is one big job of taking all these allocations
// and sticking them in at the end, so stretching this work out over the staging period is something I think I am
// ok with. It is untested, but it works with my mental model, so I will keep it for now.
//     - Sol 9 Dec 2023
//
//
struct Gpu_Tex_Allocation { // 64 bytes
    u64 stage_offset;
    u64 upload_offset;
    u64 size; // aligned to upload_alignment
    u64 upload_alignment;
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

    u64 stage_cap;
    u64 upload_cap;

    void          *stage_ptr;
    VkBuffer       stage;
    VkDeviceMemory upload;

    u64 *hashes;
    String_Buffer string_buffer;

    // Secondary command buffers
    VkCommandPool   graphics_cmd_pools[2];
    VkCommandPool   transfer_cmd_pools[2];
    VkCommandBuffer graphics_cmds[2];
    VkCommandBuffer transfer_cmds[2];
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
Gpu_Allocator_Result create_tex_allocator (Gpu_Tex_Allocator_Config *config, Gpu_Tex_Allocator *allocator);
void                 destroy_tex_allocator(Gpu_Tex_Allocator *alloc);

Gpu_Allocator_Result tex_add_texture(Gpu_Tex_Allocator *alloc, String *file_name, u32 *key);

Gpu_Allocator_Result tex_staging_queue_begin (Gpu_Tex_Allocator *alloc);
Gpu_Allocator_Result tex_staging_queue_add   (Gpu_Tex_Allocator *alloc, u32 key, bool adjust_weights);
Gpu_Allocator_Result tex_staging_queue_submit(Gpu_Tex_Allocator *alloc);

inline static void tex_staging_queue_remove(Gpu_Tex_Allocator *alloc, u32 key) {
    u32 idx = alloc->allocation_indices[key];

    Gpu_Allocation_State_Flags state              = alloc->allocation_states[idx]; 
    u64                        img_size           = alloc->allocations[idx].width * alloc->allocations[idx].height * 4;
    u64                        bit_aligned_size   = align(img_size, alloc->stage_bit_granularity);

    alloc->staging_queue_byte_count -= bit_aligned_size & max64_if_true(state & ALLOCATION_STATE_TO_STAGE_BIT);
    alloc->to_stage_count           -= (state & ALLOCATION_STATE_TO_STAGE_BIT) != 0;

    // Allocator should not stage this allocation or wait on it to draw.
    alloc->allocation_states[idx] &= ~(ALLOCATION_STATE_TO_STAGE_BIT | ALLOCATION_STATE_TO_DRAW_BIT);

    return;
}

inline static void tex_staging_queue_make_empty(Gpu_Tex_Allocator *alloc) {
    /*
       @Note This does potentially more than is necessary because the operation is so cheap I would rather avoid
       bugs by ensuring any possible weird program state is handled.
    */

    // Find every allocation which is 'TO_STAGE' but not 'TO_UPLOAD' and clear the 'TO_DRAW' flag
    simd_update_flags_u8(alloc->allocation_count, alloc->allocation_states, ALLOCATION_STATE_TO_STAGE_BIT,
                         ALLOCATION_STATE_TO_UPLOAD_BIT, 0x0, ALLOCATION_STATE_TO_DRAW_BIT);
    // Find every allocation which is 'TO_STAGE' and clear the 'TO_STAGE' flag
    simd_update_flags_u8(alloc->allocation_count, alloc->allocation_states, ALLOCATION_STATE_TO_STAGE_BIT,
                         0x0, 0x0, ALLOCATION_STATE_TO_STAGE_BIT);

    alloc->to_stage_count           = 0;
    alloc->staging_queue_byte_count = 0;
}

Gpu_Allocator_Result tex_upload_queue_begin      (Gpu_Tex_Allocator *alloc);
Gpu_Allocator_Result tex_upload_queue_add        (Gpu_Tex_Allocator *alloc, u32 key, bool adjust_weights);
Gpu_Allocator_Result tex_upload_queue_submit     (Gpu_Tex_Allocator *alloc);

inline static void tex_upload_queue_remove(Gpu_Tex_Allocator *alloc, u32 key) {
    u32 idx = alloc->allocation_indices[key];

    Gpu_Allocation_State_Flags state            = alloc->allocation_states[idx]; 
    u64                        bit_aligned_size = alloc->allocations[idx].size; // Tex allocations already aligned

    alloc->upload_queue_byte_count -= bit_aligned_size & max64_if_true(state & ALLOCATION_STATE_TO_UPLOAD_BIT);
    alloc->to_upload_count         -= (state & ALLOCATION_STATE_TO_UPLOAD_BIT) != 0;

    // Allocator should not upload this allocation or wait on it to draw.
    alloc->allocation_states[idx] &= ~(ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_TO_DRAW_BIT);

    return;
}

inline static void tex_upload_queue_make_empty(Gpu_Tex_Allocator *alloc) {
    /*
       @Note This does potentially more than is necessary because the operation is so cheap I would rather avoid
       bugs by ensuring any possible weird program state is handled.
    */

    // Find every allocation which is 'TO_STAGE' but not 'TO_UPLOAD' and clear the 'TO_DRAW' flag
    simd_update_flags_u8(alloc->allocation_count, alloc->allocation_states, ALLOCATION_STATE_TO_UPLOAD_BIT,
                         ALLOCATION_STATE_TO_STAGE_BIT, 0x0, ALLOCATION_STATE_TO_DRAW_BIT);
    // Find every allocation which is 'TO_STAGE' and clear the 'TO_STAGE' flag
    simd_update_flags_u8(alloc->allocation_count, alloc->allocation_states, ALLOCATION_STATE_TO_UPLOAD_BIT,
                         0x0, 0x0, ALLOCATION_STATE_TO_UPLOAD_BIT);

    alloc->to_upload_count         = 0;
    alloc->upload_queue_byte_count = 0;
}

inline static void tex_staging_queue_upload_queue_remove(Gpu_Tex_Allocator *alloc, u32 key) {
    u32 idx = alloc->allocation_indices[key];

    u64 img_size                = alloc->allocations[idx].width * alloc->allocations[idx].height * 4;
    u64 bit_aligned_size_stage  = align(img_size, alloc->stage_bit_granularity);
    u64 bit_aligned_size_upload = alloc->allocations[idx].size; // image upload sizes are already aligned

    Gpu_Allocation_State_Flags state = alloc->allocation_states[idx]; 

    alloc->upload_queue_byte_count  -= bit_aligned_size_upload & max64_if_true(state & ALLOCATION_STATE_TO_UPLOAD_BIT);
    alloc->staging_queue_byte_count -= bit_aligned_size_stage  & max64_if_true(state & ALLOCATION_STATE_TO_STAGE_BIT);

    alloc->to_upload_count -= (state & ALLOCATION_STATE_TO_UPLOAD_BIT) != 0;
    alloc->to_stage_count  -= (state & ALLOCATION_STATE_TO_STAGE_BIT)  != 0;

    // Allocator should not upload this allocation or wait on it to draw.
    alloc->allocation_states[idx] &= ~(ALLOCATION_STATE_TO_STAGE_BIT | ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_TO_DRAW_BIT);

    return;
}

inline static Gpu_Tex_Allocation* gpu_get_tex_allocation(Gpu_Tex_Allocator *allocator, u32 allocation_key) {
    u32 allocation_index = allocator->allocation_indices[allocation_key];
    return &allocator->allocations[allocation_index];
}

typedef Gpu_Allocator_Result (*Gpu_Allocator_Queue_Begin_Func)     (Gpu_Allocator*);
typedef Gpu_Allocator_Result (*Gpu_Tex_Allocator_Queue_Begin_Func) (Gpu_Tex_Allocator*);
typedef Gpu_Allocator_Result (*Gpu_Allocator_Queue_Add_Func)       (Gpu_Allocator*, u32, bool);
typedef Gpu_Allocator_Result (*Gpu_Tex_Allocator_Queue_Add_Func)   (Gpu_Tex_Allocator*, u32, bool);
typedef void                 (*Gpu_Allocator_Queue_Remove_Func)    (Gpu_Allocator*, u32);
typedef void                 (*Gpu_Tex_Allocator_Queue_Remove_Func)(Gpu_Tex_Allocator*, u32);
typedef Gpu_Allocator_Result (*Gpu_Allocator_Queue_Submit_Func)    (Gpu_Allocator*);
typedef Gpu_Allocator_Result (*Gpu_Tex_Allocator_Queue_Submit_Func)(Gpu_Tex_Allocator*);

struct Get_Sampler_Info {
    VkSamplerAddressMode wrap_s;
    VkSamplerAddressMode wrap_t;
    VkSamplerMipmapMode  mipmap_mode;

    VkFilter mag_filter;
    VkFilter min_filter;
};
struct Sampler_Info {
    VkSamplerAddressMode wrap_s;
    VkSamplerAddressMode wrap_t;
    VkSamplerMipmapMode  mipmap_mode;

    VkFilter mag_filter;
    VkFilter min_filter;

    VkSampler sampler;
    u32       user_count;
};
enum Sampler_Allocator_Result {
    SAMPLER_ALLOCATOR_RESULT_SUCCESS        = 0,
    SAMPLER_ALLOCATOR_RESULT_ALL_IN_USE     = 1,
    SAMPLER_ALLOCATOR_RESULT_INVALID_KEY    = 2,
    SAMPLER_ALLOCATOR_RESULT_ALLOCATOR_FULL = 3,
};

#define CHECK_SAMPLER_ALLOCATOR_RESULT(res)              \
    if (res != SAMPLER_ALLOCATOR_RESULT_SUCCESS) {       \
        assert(res == SAMPLER_ALLOCATOR_RESULT_SUCCESS); \
        return {};                                       \
    }

enum Sampler_Allocation_Flag_Bits {
    SAMPLER_ACTIVE_BIT = 0x01,
    SAMPLER_IN_USE_BIT = 0x02,
};
typedef u8 Sampler_Allocation_Flags;

struct Sampler_Allocator {
    u32 device_cap;

    u32 cap;
    u32 count;
    u32 active;
    u32 in_use;
    Sampler_Info *samplers;

    VkDevice device;
    u64 *hashes;
    u8  *weights;
    Sampler_Allocation_Flags  *flags;
};

// Set cap to zero to let the allocator decide a size
Sampler_Allocator create_sampler_allocator (u32 cap);
void              destroy_sampler_allocator(Sampler_Allocator *alloc);

Sampler_Allocator_Result add_sampler(Sampler_Allocator *alloc, Get_Sampler_Info *sampler_info, u32 *key);
Sampler_Allocator_Result get_sampler(Sampler_Allocator *alloc, u32 key, VkSampler *ret_sampler, bool adjust_weights);

void done_with_sampler(Sampler_Allocator *alloc, u32 key);

// Image View Allocator
enum Image_View_Allocator_Result {
    IMAGE_VIEW_ALLOCATOR_RESULT_SUCCESS     = 0,
    IMAGE_VIEW_ALLOCATOR_RESULT_ALL_IN_USE  = 1,
    IMAGE_VIEW_ALLOCATOR_RESULT_INVALID_KEY = 2,
};
enum Image_View_Flag_Bits {
    IMAGE_VIEW_IN_USE_BIT = 0x01,
};
typedef u8 Image_View_Flags;

struct Image_View {
    VkImageView view;
    u32         user_count;
};
struct Image_View_Allocator {
    u32 cap;
    u32 count;
    u32 in_use;
    HashMap<u64, Image_View> map;

    VkDevice device;
    u64 *hashes;
    u8  *weights;
    Image_View_Flags *flags;
};
Image_View_Allocator create_image_view_allocator (u32 cap);
void                 destroy_image_view_allocator(Image_View_Allocator *alloc);

Image_View_Allocator_Result get_image_view(
    Image_View_Allocator  *alloc,
    VkImageViewCreateInfo *view_info,
    VkImageView           *ret_image_view,
    u64                   *ret_view_hash,
    bool                   adjust_weights);

void done_with_image_view(Image_View_Allocator *alloc, u64 hash);

struct Uniform_Allocator {
    u64              cap;
    u64              used;
    u8              *mem;
    VkBuffer         buf;
    VkDeviceAddress  address;
};
inline static Uniform_Allocator get_uniform_allocator(u64 size, void *ptr, VkBuffer buf) {
    Gpu *gpu  = get_gpu_instance();

    Uniform_Allocator ret;
    ret.cap  = size;
    ret.used = 0;
    ret.mem  = (u8*)ptr;
    ret.buf  = buf;

    VkBufferDeviceAddressInfo address_info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    address_info.buffer = buf;

    ret.address = vkGetBufferDeviceAddress(gpu->device, &address_info);

    return ret;
}

struct Uniform_Allocation {
    u8              *ptr;
    VkDeviceAddress  address;
};

inline static Uniform_Allocation uniform_malloc(Uniform_Allocator *allocator, u64 size) {
    // pad to alignment
    allocator->used = align(allocator->used, 16);

    Uniform_Allocation ret = {};
    ret.ptr     = allocator->mem     + allocator->used;
    ret.address = allocator->address + allocator->used;

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

inline static void vkGetDescriptor(
    VkDevice                      device,
    const VkDescriptorGetInfoEXT *pDescriptorGetInfo,
    u64                           dataSize,
    void                         *pDescriptor)
{
    auto func = (PFN_vkGetDescriptorEXT)vkGetDeviceProcAddr(device, "vkGetDescriptorEXT");
    return func(device, pDescriptorGetInfo, dataSize, pDescriptor);
}

inline static void vkGetDescriptorSetLayoutBindingOffset(
    VkDevice               device,
    VkDescriptorSetLayout  layout,
    u32                    binding,
    u64                   *offset)
{
    auto func = (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)vkGetDeviceProcAddr(device, "vkGetDescriptorSetLayoutBindingOffsetEXT");
    return func(device, layout, binding, offset);
}

inline static void vkGetDescriptorSetLayoutSize(
    VkDevice               device,
    VkDescriptorSetLayout  layout,
    u64                   *pLayoutSizeInBytes)
{
    auto func = (PFN_vkGetDescriptorSetLayoutSizeEXT)vkGetDeviceProcAddr(device, "vkGetDescriptorSetLayoutSizeEXT");
    return func(device, layout, pLayoutSizeInBytes);
}

struct Descriptor_Allocator {
    u64 cap;
    u64 used;
    u64 buffer_address;
    u8 *mem;
    VkBuffer buf;

    VkPhysicalDeviceDescriptorBufferPropertiesEXT info;
};

// Does not need to be freed
Descriptor_Allocator get_descriptor_allocator(u64 size, void *mem, VkBuffer buf);

struct Descriptor_Allocation {
    void         *ptr;
    VkDeviceSize  offset;
};

inline static u64 descriptor_get_layout_size(Descriptor_Allocator *alloc, u64 size) {
    return align(size, alloc->info.descriptorBufferOffsetAlignment);
}

inline static Descriptor_Allocation descriptor_allocate_layout(Descriptor_Allocator *alloc, u64 size) {
    Descriptor_Allocation ret;
    ret.ptr    = (u8*)alloc->mem + alloc->used;
    ret.offset = alloc->used;

    size         = align(size, alloc->info.descriptorBufferOffsetAlignment);
    alloc->used += size;

    assert(alloc->used <= alloc->cap && "Descriptor Allocator Overflow");
    return ret;
}

void descriptor_write_uniform_buffer(
    VkDevice              device,
    Descriptor_Allocator *alloc,
    VkDescriptorDataEXT   datas,
    u8                   *mem);

void descriptor_write_combined_image_sampler(
    VkDevice              device,
    Descriptor_Allocator *alloc,
    VkDescriptorDataEXT   datas,
    u8                   *mem);

void descriptor_write_input_attachment(
    VkDevice              device,
    Descriptor_Allocator *alloc,
    VkDescriptorDataEXT   datas,
    u8                   *mem);

inline static void descriptor_allocator_reset(Descriptor_Allocator *allocator) {
    allocator->used = 0;
}
inline static u64 descriptor_get_binding_offset(VkDescriptorSetLayout layout, u32 binding) {
    u64 ret;
    vkGetDescriptorSetLayoutBindingOffsetEXT(get_gpu_instance()->device, layout, binding, &ret);
    return ret;
}

    /* Allocation End */

    /* Renderpass Framebuffer Pipeline */
// @Todo Add more renderpass types
void rp_forward_shadow(VkImageView present_attachment, VkImageView depth_attachment, VkImageView shadow_attachment,
                       VkRenderPass *renderpass, VkFramebuffer *framebuffer);

enum Pl_Config_Flag_Bits {
    PL_CONFIG_CULL_FRONT_BIT            = 0x0001,
    PL_CONFIG_CULL_BACK_BIT             = 0x0002,
    PL_CONFIG_WIRE_FRAME_BIT            = 0x0004,
    PL_CONFIG_CLOCKWISE_FRONT_FACE_BIT  = 0x0008,
    PL_CONFIG_DEPTH_TEST_ENABLE_BIT     = 0x0010,
    PL_CONFIG_DEPTH_WRITE_ENABLE_BIT    = 0x0020,
    PL_CONFIG_DEPTH_COMPARE_GREATER_BIT = 0x0040,
    PL_CONFIG_DEPTH_COMPARE_LESS_BIT    = 0x0080,
    PL_CONFIG_DEPTH_COMPARE_EQUAL_BIT   = 0x0100,
    PL_CONFIG_STENCIL_TEST_ENABLE_BIT   = 0x0200,

    PL_CONFIG_RASTERIZATION_BITS = PL_CONFIG_WIRE_FRAME_BIT | PL_CONFIG_CULL_FRONT_BIT |
                                   PL_CONFIG_CULL_BACK_BIT  | PL_CONFIG_CLOCKWISE_FRONT_FACE_BIT,

    PL_CONFIG_DEPTH_STENCIL_BITS = PL_CONFIG_DEPTH_TEST_ENABLE_BIT     | PL_CONFIG_DEPTH_WRITE_ENABLE_BIT |
                                   PL_CONFIG_DEPTH_COMPARE_GREATER_BIT | PL_CONFIG_DEPTH_COMPARE_LESS_BIT |
                                   PL_CONFIG_DEPTH_COMPARE_EQUAL_BIT   | PL_CONFIG_DEPTH_WRITE_ENABLE_BIT |
                                   PL_CONFIG_STENCIL_TEST_ENABLE_BIT,
};
typedef u32 Pl_Config_Flags;

struct Pl_Primitive_Info { // 4 + 4 + 8 + 8 = 24 bytes
    u32                 *strides;
    VkFormat            *formats;
    VkPrimitiveTopology  topology;
    u32                  count;
};

struct Pl_Shader_Info {
    VkShaderModule        module;
    VkShaderStageFlagBits stage;
};

struct Pl_Layout {
    u32                    set_count;
    u32                    stage_count;
    u64                   *set_layout_sizes; // Save from api calls
    VkDescriptorSetLayout *set_layouts;
    Pl_Shader_Info        *stages;
    VkPipelineLayout       pl_layout;
};
void pl_get_stages_and_layout(
    u32                  count,
    u32                 *shader_indices,
    u32                  push_constant_count,
    VkPushConstantRange *push_constants,
    Pl_Layout           *layout);

struct Pl_Stencil_Ops {
    VkStencilOpState front;
    VkStencilOpState back;
};

constexpr VkPipelineColorBlendAttachmentState PL_BLEND_ATTACHMENT_STATE_NO_BLEND = {
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
};

constexpr VkPipelineColorBlendAttachmentState PL_BLEND_ATTACHMENT_STATE_ALPHA_BLEND = {
    .blendEnable         = VK_TRUE,
    .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
    .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    .colorBlendOp        = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
    .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
    .alphaBlendOp        = VK_BLEND_OP_ADD,
    .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
};
typedef VkPipelineColorBlendAttachmentState Pl_Blend_Info;

struct Pl_Config {
    Pl_Config_Flags   flags;
    u32               subpass;
    u32               shader_count;
    u32               blend_count;
    VkRenderPass      renderpass;
    Pl_Shader_Info   *shader_infos;
    Pl_Blend_Info    *blend_infos;
    Pl_Stencil_Ops   *stencil_ops;
    VkPipelineLayout  layout;
};

void pl_create_pipelines(
    u32                      count,
    const Pl_Primitive_Info *primitive_infos,
    const Pl_Config         *configs,
    VkPipeline              *pipelines);

inline static void pl_destroy_pipelines(u32 count, VkPipeline *pipelines) {
    VkDevice device = get_gpu_instance()->device;
    for(u32 i = 0; i < count; ++i)
        vkDestroyPipeline(device, pipelines[i], ALLOCATION_CALLBACKS);
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
