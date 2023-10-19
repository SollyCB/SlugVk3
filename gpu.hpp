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

struct Gpu_Tex_Allocator;
struct Gpu_Buf_Allocator;

struct Gpu_Buffer_Copy_Info {
    VkSemaphoreSubmitInfo *wait_semaphore;
    VkSemaphore transfer_signal;
    VkCommandBuffer transfer_cmd;
    VkCommandBuffer graphics_cmd;

    int buffer_count;
    VkBuffer *buffers;
    VkCopyBufferInfo2 *copy_infos;
};
typedef VkSubmitInfo2 (*Pfn_Device_Buffer_Upload)(Gpu_Buffer_Copy_Info*);

struct Gpu_Bind_Index_Vertex_Buffers_Info {
    VkIndexType index_type;
    u32 vertex_buffer_count;
    u32 vertex_buffer_start_index;

    u64 index_buffer_offset;
    u64 *vertex_buffer_offsets;

    VkCommandBuffer cmd;
};
typedef void (*Pfn_Bind_Index_Vertex_Buffers)(Gpu_Bind_Index_Vertex_Buffers_Info*);

static constexpr u64 GPU_VERTEX_ALLOCATOR_SIZE = 1024 * 1024;
static constexpr u64 GPU_INDEX_ALLOCATOR_SIZE  = 1024 * 1024;
static constexpr u64 GPU_TEXTURE_ALLOCATOR_SIZE = 1024 * 1024;

// @Todo Storage equivalents
static constexpr u32 GPU_MAX_ALLOCATOR_COUNT_INDEX   = 1;
static constexpr u32 GPU_MAX_ALLOCATOR_COUNT_VERTEX  = 1;
static constexpr u32 GPU_MAX_ALLOCATOR_COUNT_UNIFORM = 1;
static constexpr u32 GPU_MAX_ALLOCATOR_COUNT_TEXTURE = 1;
static constexpr u32 GPU_MAX_ATTACHMENT_COUNT_DEPTH         = 1;
static constexpr u32 GPU_MAX_ATTACHMENT_COUNT_COLOR         = 1;

enum Gpu_Mem_Flag_Bits {
    GPU_MEM_UMA_BIT = 0x01,
};
typedef u32 Gpu_Mem_Flags;

struct Gpu_Memory_Resources {
    Gpu_Mem_Flags flags;

    VkDeviceMemory       index_vertex_mems_device       [GPU_MAX_ALLOCATOR_COUNT_INDEX  ];
    VkDeviceMemory       index_vertex_mems_host         [GPU_MAX_ALLOCATOR_COUNT_INDEX  ];
    VkDeviceMemory       uniform_mems                   [GPU_MAX_ALLOCATOR_COUNT_UNIFORM];
    VkDeviceMemory       color_mems                     [GPU_MAX_ATTACHMENT_COUNT_COLOR ];
    VkDeviceMemory       depth_mems                     [GPU_MAX_ATTACHMENT_COUNT_COLOR ];
    VkDeviceMemory       texture_mems_stage             [GPU_MAX_ALLOCATOR_COUNT_TEXTURE];
    VkDeviceMemory       texture_mems_device            [GPU_MAX_ALLOCATOR_COUNT_TEXTURE];

    VkBuffer       index_bufs_device                    [GPU_MAX_ALLOCATOR_COUNT_INDEX  ];
    VkBuffer       vertex_bufs_device                   [GPU_MAX_ALLOCATOR_COUNT_VERTEX ];
    VkBuffer       index_bufs_host                      [GPU_MAX_ALLOCATOR_COUNT_INDEX  ];
    VkBuffer       vertex_bufs_host                     [GPU_MAX_ALLOCATOR_COUNT_VERTEX ];
    VkBuffer       uniform_bufs                         [GPU_MAX_ALLOCATOR_COUNT_UNIFORM];
    VkImage        color_attachments                    [GPU_MAX_ATTACHMENT_COUNT_COLOR ];
    VkImage        depth_attachments                    [GPU_MAX_ATTACHMENT_COUNT_DEPTH ];
    VkBuffer       texture_stages                       [GPU_MAX_ALLOCATOR_COUNT_TEXTURE];
};
struct GpuInfo {
    VkPhysicalDeviceProperties properties;
};
struct Gpu {
    GpuInfo info;

    VkInstance vk_instance;
    VkPhysicalDevice vk_physical_device;
    VkDevice vk_device;
    VkQueue *vk_queues; // 3 queues ordered graphics, presentation, transfer
    u32 *vk_queue_indices;

    Gpu_Memory_Resources memory_resources;

    VkImageView depth_views[GPU_MAX_ATTACHMENT_COUNT_DEPTH];

    Gpu_Buf_Allocator *index_device_allocator;
    Gpu_Buf_Allocator *vertex_device_allocator;
    Gpu_Buf_Allocator *index_host_allocator;
    Gpu_Buf_Allocator *vertex_host_allocator;
    Pfn_Device_Buffer_Upload device_buffer_upload_fn;

    Gpu_Buf_Allocator *uniform_allocator;

    Gpu_Tex_Allocator *texture_allocator;
};
Gpu* get_gpu_instance();

void init_gpu();
void kill_gpu(Gpu *gpu);

// Instance
struct Create_Vk_Instance_Info {
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
VkInstance create_vk_instance(Create_Vk_Instance_Info *info);

// Device and Queues
VkDevice create_vk_device(Gpu *gpu);
VkQueue create_vk_queue(Gpu *gpu);
void destroy_vk_device(Gpu *gpu);
void destroy_vk_queue(Gpu *gpu);

// Allocator Implementation
void gpu_init_memory_resources(Gpu *gpu);

// Buffer Allocator
struct Gpu_Buf_Allocator {
    u32 alloc_cnt;
    u32 alloc_cap;

    u64  alignment;
    u64  used;
    u64  cap;

    VkBuffer buf;
    void *ptr;
};
Gpu_Buf_Allocator gpu_get_buf_allocator(VkBuffer buffer, void *ptr, u64 size, u32 count);
void gpu_reset_buf_allocator(VkDevice device, Gpu_Buf_Allocator *allocator);
// Reserve space in allocator; Return pointer to beginning of new allocation
void* gpu_make_buf_allocation(Gpu_Buf_Allocator *allocator, u64 size, u64 *offset);
// Get copy information using allocators
VkCopyBufferInfo2 gpu_buf_allocator_setup_copy(
    Gpu_Buf_Allocator *to_allocator,
    Gpu_Buf_Allocator *from_allocator,
    u64 src_offset, u64 size);
// Call the correct vertex attribute upload function
inline static VkSubmitInfo2 gpu_cmd_begin_buf_transfer_graphics(Gpu_Buffer_Copy_Info *info) {
    return get_gpu_instance()->device_buffer_upload_fn(info);
}

struct Gpu_Tex_Allocator {
    u32 img_cap;
    u32 img_cnt;

    u64 alignment;
    u64 mem_used;
    u64 buf_used;
    u64 cap;

    VkBuffer stage;
    u64 *offsets;
    VkImage *imgs;
    VkDeviceMemory mem;
    void *ptr;
};
// Setup allocator; Memory allocate .imgs pointer
Gpu_Tex_Allocator gpu_create_tex_allocator(VkDeviceMemory img_mem, VkBuffer stage, void *mapped_ptr, u64 byte_cap, u32 img_cap);
// Free .imgs pointer
void gpu_destroy_tex_allocator(Gpu_Tex_Allocator *alloc);
void* gpu_make_tex_allocation(Gpu_Tex_Allocator *alloc, u64 width, u64 height, VkImage *image);
void gpu_reset_tex_allocator(Gpu_Tex_Allocator *alloc);

// Surface and Swapchain
struct Window {
    VkSwapchainKHR vk_swapchain;
    VkSwapchainCreateInfoKHR info;
    u32 image_count;
    VkImage *vk_images;
    VkImageView *vk_image_views;
};
Window* get_window_instance();

void init_window(Gpu *gpu, Glfw *glfw);
void kill_window(Gpu *gpu, Window *window);

VkSurfaceKHR create_vk_surface(VkInstance vk_instance, Glfw *glfw);
void destroy_vk_surface(VkInstance vk_instance, VkSurfaceKHR vk_surface);

// @Note this function does a lot to initialize window members, because I consider
// these elems to be parts of the swapchain, not distinct things.
VkSwapchainKHR create_vk_swapchain(Gpu *gpu, VkSurfaceKHR vk_surface);
void destroy_vk_swapchain(VkDevice vk_device, Window *window);
VkSwapchainKHR recreate_vk_swapchain(Gpu *gpu, Window *window);

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

/* Command Buffers */
struct Gpu_Command_Allocator {
    VkCommandPool pool;
    int buffer_count; // Implicit cap of 64 (in_use_mask bit count)
    int cap;
    VkCommandBuffer *buffers;
};
Gpu_Command_Allocator
gpu_create_command_allocator(VkDevice vk_device, int queue_family_index, bool transient, int size);

// Free allocator.buffers memory; Call 'vk destroy pool'
void gpu_destroy_command_allocator(VkDevice vk_device, Gpu_Command_Allocator *allocator);

// Sets in use to zero; Calls 'vk reset pool'
void gpu_reset_command_allocator(VkDevice vk_device, Gpu_Command_Allocator *allocator);

// Allocate into allocator.buffers + allocators.in_use (offset pointer); Increment in_use by count;
// Return pointer to beginning of new allocation
VkCommandBuffer*
gpu_allocate_command_buffers(VkDevice vk_device, Gpu_Command_Allocator *allocator,
                             int count, bool secondary);

inline static void
gpu_begin_primary_command_buffer(VkCommandBuffer cmd, bool one_time)
{
    VkCommandBufferBeginInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    info.flags = (VkCommandBufferUsageFlags)one_time; // one time flag == 0x01
    vkBeginCommandBuffer(cmd, &info);
}
inline static void
gpu_end_command_buffer(VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);
}

/* Queue */
enum Gpu_Pipeline_Stage_Flags {
    GPU_PIPELINE_STAGE_TRANSFER         = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
    GPU_PIPELINE_STAGE_VERTEX_INPUT     = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
    GPU_PIPELINE_STAGE_COLOR_OUTPUT     = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    GPU_PIPELINE_STAGE_ALL_COMMANDS     = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    GPU_PIPELINE_STAGE_TOP_OF_PIPE      = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
    GPU_PIPELINE_STAGE_ALL_GRAPHICS     = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
};
VkSemaphoreSubmitInfo
gpu_define_semaphore_submission( VkSemaphore semaphore, Gpu_Pipeline_Stage_Flags stage);

struct Gpu_Queue_Submit_Info {
    int wait_count;
    int signal_count;
    int cmd_count;
    VkSemaphoreSubmitInfo *wait_infos;
    VkSemaphoreSubmitInfo *signal_infos;
    VkCommandBuffer *command_buffers;
};
VkSubmitInfo2 gpu_get_submit_info(Gpu_Queue_Submit_Info *info);

/* Sync */

// Pipeline Barriers
enum Gpu_Memory_Barrier_Setting {
    GPU_MEMORY_BARRIER_SETTING_TRANSFER_SRC,
    GPU_MEMORY_BARRIER_SETTING_VERTEX_INDEX_OWNERSHIP_TRANSFER,
    GPU_MEMORY_BARRIER_SETTING_VERTEX_BUFFER_TRANSFER_DST,
    GPU_MEMORY_BARRIER_SETTING_INDEX_BUFFER_TRANSFER_DST,
};
struct Gpu_Buffer_Barrier_Info {
    Gpu_Memory_Barrier_Setting setting;
    int src_queue;
    int dst_queue;
    VkBuffer buffer;
    u64 offset;
    u64 size;
};
VkBufferMemoryBarrier2 gpu_get_buffer_barrier(Gpu_Buffer_Barrier_Info *info);

struct Gpu_Pipeline_Barrier_Info {
    int memory_count;
    int buffer_count;
    int image_count;
    VkMemoryBarrier2       *memory_barriers;
    VkBufferMemoryBarrier2 *buffer_barriers;
    VkImageMemoryBarrier2  *image_barriers;
};
VkDependencyInfo gpu_get_pipeline_barrier(Gpu_Pipeline_Barrier_Info *info);

// Fences
struct Gpu_Fence_Pool {
    u32 len;
    u32 in_use;
    VkFence *vk_fences;
};
Gpu_Fence_Pool gpu_create_fence_pool(VkDevice vk_device, u32 size);
void gpu_destroy_fence_pool(VkDevice vk_device, Gpu_Fence_Pool *pool);

VkFence* gpu_get_fences(Gpu_Fence_Pool *pool, u32 count);
void gpu_reset_fence_pool(VkDevice vk_device, Gpu_Fence_Pool *pool);
void gpu_cut_tail_fences(Gpu_Fence_Pool *pool, u32 size);

// Semaphores
struct Gpu_Binary_Semaphore_Pool {
    u32 len;
    u32 in_use;
    VkSemaphore *vk_semaphores;
};
Gpu_Binary_Semaphore_Pool gpu_create_binary_semaphore_pool(VkDevice vk_device, u32 size);
void gpu_destroy_semaphore_pool(VkDevice vk_device, Gpu_Binary_Semaphore_Pool *pool);

VkSemaphore* gpu_get_binary_semaphores(Gpu_Binary_Semaphore_Pool *pool, u32 count);
void gpu_reset_binary_semaphore_pool(Gpu_Binary_Semaphore_Pool *pool);
void gpu_cut_tail_binary_semaphores(Gpu_Binary_Semaphore_Pool *pool, u32 size);

// Descriptors - static, pool allocated
VkDescriptorPool create_vk_descriptor_pool(VkDevice vk_device, int max_set_count, int counts[11]);
VkResult reset_vk_descriptor_pool(VkDevice vk_device, VkDescriptorPool pool);
void destroy_vk_descriptor_pool(VkDevice vk_device, VkDescriptorPool pool);

struct Gpu_Allocate_Descriptor_Set_Info;
struct Gpu_Descriptor_Allocator {
    u16 sets_queued;
    u16 sets_allocated;
    u16 set_cap;

    VkDescriptorSetLayout *layouts;
    VkDescriptorSet       *sets;

    VkDescriptorPool pool;

    // @Todo @Note This is a trial style. These arrays make this struct verrrryy big, but also super
    // detailed so you can really see what is up with the pool. Probably better to enable this
    // functionality only in debug mode?? Or maybe this information can be useful even in release?
    // Tbf this struct isnt smtg which is an array that gets looped through all the time, for instance
    // it will only ever be being loaded once per function so the size honestly wont matter
    // (I dont think...)
    u16 cap[11];
    u16 counts[11]; // tracks individual descriptor allocations
};
// 'count' arg corresponds to the number of descriptors for each of the first 11 descriptor types
Gpu_Descriptor_Allocator
gpu_create_descriptor_allocator(VkDevice vk_device, int max_sets, int counts[11]);
void gpu_destroy_descriptor_allocator(VkDevice vk_device, Gpu_Descriptor_Allocator *allocator);
void gpu_reset_descriptor_allocator(VkDevice vk_device, Gpu_Descriptor_Allocator *allocator);

struct Gpu_Descriptor_List;
struct Gpu_Queue_Descriptor_Set_Allocation_Info {
    int layout_count;
    VkDescriptorSetLayout *layouts;
    int *descriptor_counts; // array of len 11
};
VkDescriptorSet* gpu_queue_descriptor_set_allocation(
    Gpu_Descriptor_Allocator *allocator, Gpu_Queue_Descriptor_Set_Allocation_Info *info, VkResult *result);

void gpu_allocate_descriptor_sets(VkDevice vk_device, Gpu_Descriptor_Allocator *allocator);

struct Create_Vk_Descriptor_Set_Layout_Info {
    int count;
    VkDescriptorSetLayoutBinding *bindings;
};
VkDescriptorSetLayout* create_vk_descriptor_set_layouts(VkDevice vk_device, int count, Create_Vk_Descriptor_Set_Layout_Info *infos);
void gpu_destroy_descriptor_set_layouts(VkDevice vk_device, int count, VkDescriptorSetLayout *layouts);
void gpu_destroy_descriptor_set_layouts(VkDevice vk_device, int count, Gpu_Allocate_Descriptor_Set_Info *layouts);

struct Gpu_Descriptor_List {
    int counts[11]; // count per descriptor type
};
Gpu_Descriptor_List gpu_make_descriptor_list(int count, Create_Vk_Descriptor_Set_Layout_Info *infos);


// Descriptors - buffer, dynamic

//Gpu_Allocate_Descriptor_Set_Info* create_vk_descriptor_set_layouts(VkDevice vk_device, int count, Create_Vk_Descriptor_Set_Layout_Info *binding_info);
//void destroy_vk_descriptor_set_layouts(VkDevice vk_device, int count, VkDescriptorSetLayout *layouts);

// Pipeline Setup
// `ShaderStages
struct Create_Vk_Pipeline_Shader_Stage_Info {
    u64 code_size;
    const u32 *shader_code;
    VkShaderStageFlagBits stage;
    VkSpecializationInfo *spec_info = NULL;
};
VkPipelineShaderStageCreateInfo* create_vk_pipeline_shader_stages(VkDevice vk_device, u32 count, Create_Vk_Pipeline_Shader_Stage_Info *infos);
void destroy_vk_pipeline_shader_stages(VkDevice vk_device, u32 count, VkPipelineShaderStageCreateInfo *stages);

// `VertexInputState
// @StructPacking pack struct
struct Create_Vk_Vertex_Input_Binding_Description_Info {
    u32 binding;
    u32 stride;
    // @Todo support instance input rate
};
VkVertexInputBindingDescription create_vk_vertex_binding_description(Create_Vk_Vertex_Input_Binding_Description_Info *info);
enum Vec_Type {
    VEC_TYPE_1 = VK_FORMAT_R32_SFLOAT,
    VEC_TYPE_2 = VK_FORMAT_R32G32_SFLOAT,
    VEC_TYPE_3 = VK_FORMAT_R32G32B32_SFLOAT,
    VEC_TYPE_4 = VK_FORMAT_R32G32B32A32_SFLOAT,
};
struct Create_Vk_Vertex_Input_Attribute_Description_Info {
    u32 location;
    u32 binding;
    VkFormat format;
    u32 offset;
};
VkVertexInputAttributeDescription create_vk_vertex_attribute_description(Create_Vk_Vertex_Input_Attribute_Description_Info *info);

struct Create_Vk_Pipeline_Vertex_Input_State_Info {
    u32 binding_count;
    u32 attribute_count;
    VkVertexInputBindingDescription *binding_descriptions;
    VkVertexInputAttributeDescription *attribute_descriptions;
};
VkPipelineVertexInputStateCreateInfo create_vk_pipeline_vertex_input_states(Create_Vk_Pipeline_Vertex_Input_State_Info *info);

// `InputAssemblyState
VkPipelineInputAssemblyStateCreateInfo create_vk_pipeline_input_assembly_state(VkPrimitiveTopology topology, VkBool32 primitive_restart);

// `TessellationState
// @Todo support Tessellation

// Viewport
VkPipelineViewportStateCreateInfo create_vk_pipeline_viewport_state(Window *window);

// `RasterizationState
VkPipelineRasterizationStateCreateInfo create_vk_pipeline_rasterization_state(VkPolygonMode polygon_mode, VkCullModeFlags cull_mode, VkFrontFace front_face);
void vkCmdSetDepthClampEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClampEnable);
void vkCmdSetPolygonModeEXT(VkCommandBuffer commandBuffer, VkPolygonMode polygonMode);
// `MultisampleState // @Todo support setting multisampling functions
//struct Create_Vk_Pipeline_Multisample_State_Info {};
VkPipelineMultisampleStateCreateInfo create_vk_pipeline_multisample_state(VkSampleCountFlagBits sample_count);

// `DepthStencilState
struct Create_Vk_Pipeline_Depth_Stencil_State_Info {
    VkBool32 depth_test_enable;
    VkBool32 depth_write_enable;
    VkBool32 depth_bounds_test_enable;
    VkCompareOp depth_compare_op;
    float min_depth_bounds;
    float max_depth_bounds;
};
VkPipelineDepthStencilStateCreateInfo create_vk_pipeline_depth_stencil_state(Create_Vk_Pipeline_Depth_Stencil_State_Info *info);

// `BlendState
void vkCmdSetLogicOpEnableEXT(VkCommandBuffer commandBuffer, VkBool32 logicOpEnable);
void vkCmdSetColorBlendEnableEXT(VkCommandBuffer commandBuffer, u32 firstAttachment,
        u32 attachmentCount, VkBool32 *pColorBlendEnables);
void vkCmdSetColorBlendEquationEXT(VkCommandBuffer commandBuffer, u32 firstAttachment,
        u32 attachmentCount, const VkColorBlendEquationEXT* pColorBlendEquations);
void vkCmdSetColorWriteMaskEXT(VkCommandBuffer commandBuffer, u32 firstAttachment,
        u32 attachmentCount, const VkColorComponentFlags* pColorWriteMasks);

// `BlendState
struct Create_Vk_Pipeline_Color_Blend_State_Info {
    u32 attachment_count;
    VkPipelineColorBlendAttachmentState *attachment_states;
};
VkPipelineColorBlendStateCreateInfo create_vk_pipeline_color_blend_state(Create_Vk_Pipeline_Color_Blend_State_Info *info);

// `DynamicState
VkPipelineDynamicStateCreateInfo create_vk_pipeline_dyn_state();

// `PipelineLayout
struct Create_Vk_Pipeline_Layout_Info {
    u32 descriptor_set_layout_count;
    VkDescriptorSetLayout *descriptor_set_layouts;
    u32 push_constant_count;
    VkPushConstantRange *push_constant_ranges;
};
VkPipelineLayout create_vk_pipeline_layout(VkDevice vk_device, Create_Vk_Pipeline_Layout_Info *info);
void destroy_vk_pipeline_layout(VkDevice vk_device, VkPipelineLayout pl_layout);

// PipelineRenderingInfo
struct Create_Vk_Pipeline_Rendering_Info_Info {
    u32 view_mask;
    u32 color_attachment_count;
    VkFormat *color_attachment_formats;
    VkFormat  depth_attachment_format;
    VkFormat  stencil_attachment_format;
};
VkPipelineRenderingCreateInfo create_vk_pipeline_rendering_info(Create_Vk_Pipeline_Rendering_Info_Info *info);

/** `Pipeline Final -- static / descriptor pools **/

// Pl_Stage_1
struct Gpu_Vertex_Input_State {
    VkPrimitiveTopology topology;
    int input_binding_description_count;
    int input_attribute_description_count;

    // binding description info
    int *binding_description_bindings;
    int *binding_description_strides;

    // attribute description info
    int *attribute_description_locations;
    int *attribute_description_bindings;
    VkFormat *formats;
};

// Pl_Stage_2
enum Gpu_Polygon_Mode_Flag_Bits {
    GPU_POLYGON_MODE_FILL_BIT  = 0x01,
    GPU_POLYGON_MODE_LINE_BIT  = 0x02,
    GPU_POLYGON_MODE_POINT_BIT = 0x04,
};
typedef u8 Gpu_Polygon_Mode_Flags;

struct Gpu_Rasterization_State {
    int polygon_mode_count;
    VkPolygonMode polygon_modes[3];
    VkCullModeFlags cull_mode;
    VkFrontFace front_face;
};

// Pl_Stage_3
enum Gpu_Fragment_Shader_Flag_Bits {
    GPU_FRAGMENT_SHADER_DEPTH_TEST_ENABLE_BIT         = 0x01,
    GPU_FRAGMENT_SHADER_DEPTH_WRITE_ENABLE_BIT        = 0x02,
    GPU_FRAGMENT_SHADER_DEPTH_BOUNDS_TEST_ENABLE_BIT  = 0x04,
};
typedef u8 Gpu_Fragment_Shader_Flags;

struct Gpu_Fragment_Shader_State {
    u32 flags;
    VkCompareOp depth_compare_op;
    VkSampleCountFlagBits sample_count;
    float min_depth_bounds;
    float max_depth_bounds;
};

// Pl_Stage_4
enum Gpu_Blend_Setting {
    GPU_BLEND_SETTING_OPAQUE_FULL_COLOR = 0,
};
struct Gpu_Fragment_Output_State {
    VkPipelineColorBlendAttachmentState blend_state;
};

struct Create_Vk_Pipeline_Info {
    int subpass;

    int shader_stage_count;
    VkPipelineShaderStageCreateInfo *shader_stages;

    Gpu_Vertex_Input_State    *vertex_input_state;
    Gpu_Rasterization_State   *rasterization_state;
    Gpu_Fragment_Shader_State *fragment_shader_state;
    Gpu_Fragment_Output_State  *fragment_output_state;

    VkPipelineLayout layout;
    VkRenderPass renderpass;
};
void create_vk_graphics_pipelines(VkDevice vk_device, VkPipelineCache, int count, Create_Vk_Pipeline_Info *infos, VkPipeline *pipelines);
void gpu_destroy_pipeline(VkDevice vk_device, VkPipeline pipeline);

// `Pipeline Final -- dynamic
VkPipeline* create_vk_graphics_pipelines_heap(VkDevice vk_device, VkPipelineCache cache,
        u32 count, VkGraphicsPipelineCreateInfo *create_infos);
void destroy_vk_pipelines_heap(VkDevice vk_device, u32 count, VkPipeline *pipelines); // Also frees memory associated with the 'pipelines' pointer

// `Static Rendering (framebuffers, renderpass, subpasses)

/* Begin Better Automate Rendering */

enum Gpu_Image_Layout {
    GPU_IMAGE_LAYOUT_COLOR = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    GPU_IMAGE_LAYOUT_DEPTH_STENCIL = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
};
enum Gpu_Attachment_Bits {
    GPU_ATTACHMENT_DEFAULT           = 0x0,
    GPU_ATTACHMENT_COLOR_BIT         = 0x01,
    GPU_ATTACHMENT_DEPTH_STENCIL_BIT = 0x02,
};
typedef u8 Gpu_Attachment_Flags;
struct Gpu_Renderpass_Info {
    Gpu_Attachment_Flags resolve_flags; // default: assume NULL resolve attachments
    Gpu_Attachment_Flags input_flags;   // default && input_attachment count > 1: assume color input

    int sample_count = 1;
    int input_attachment_count;
    int color_attachment_count;
    bool32 no_depth_attachment;

    VkClearValue color_clear_value = {.color = {0, 0, 0, 0}};
    VkClearValue depth_clear_value = {.depthStencil = {1, 0}};

    Gpu_Image_Layout *resolve_layouts; // Ignored unless flags is both depth and color
    Gpu_Image_Layout *input_layouts;   // Ignored unless flags is both depth and color

    VkImageView *depth_view;
    VkImageView *color_views;
    VkImageView *resolve_views;
    VkImageView *input_views;
};

struct Gpu_Renderpass_Framebuffer {
    VkRenderPass renderpass;
    VkFramebuffer framebuffer;
    int clear_count;
    VkClearValue *clear_values;
};
Gpu_Renderpass_Framebuffer gpu_create_renderpass_framebuffer_graphics_single(
    VkDevice vk_device, Gpu_Renderpass_Info *info);

struct Gpu_Renderpass_Begin_Info {
    Gpu_Renderpass_Framebuffer *renderpass_framebuffer;
    VkRect2D *render_area; // if NULL, render to full viewport
};
void gpu_cmd_primary_begin_renderpass(VkCommandBuffer command_buffer, Gpu_Renderpass_Begin_Info *info);

// @Unimplemented
VkRenderPass gpu_create_first_renderpass_graphics();
VkRenderPass gpu_create_subsequent_renderpass_graphics();
VkRenderPass gpu_create_final_renderpass_graphics();
VkRenderPass gpu_create_renderpass_deferred();

void gpu_destroy_renderpass_framebuffer(
    VkDevice vk_device, Gpu_Renderpass_Framebuffer *renderpass_framebuffer);

/* End Better Automate Rendering */

enum Gpu_Attachment_Description_Setting {
    GPU_ATTACHMENT_DESCRIPTION_SETTING_COLOR_LOAD_UNDEFINED_STORE,
    GPU_ATTACHMENT_DESCRIPTION_SETTING_COLOR_LOAD_OPTIMAL_STORE,
    GPU_ATTACHMENT_DESCRIPTION_SETTING_DEPTH_LOAD_UNDEFINED_STORE,
    GPU_ATTACHMENT_DESCRIPTION_SETTING_DEPTH_LOAD_OPTIMAL_STORE,
};
struct Gpu_Attachment_Description_Info {
    VkFormat format;
    Gpu_Attachment_Description_Setting setting;
};
VkAttachmentDescription gpu_get_attachment_description(Gpu_Attachment_Description_Info *info);

struct Create_Vk_Subpass_Description_Info {
    u32 input_attachment_count;
    VkAttachmentReference *input_attachments;
    u32 color_attachment_count;
    VkAttachmentReference *color_attachments;
    VkAttachmentReference *resolve_attachments;
    VkAttachmentReference *depth_stencil_attachment;

    // @Todo preserve attachments
};
VkSubpassDescription create_vk_graphics_subpass_description(Create_Vk_Subpass_Description_Info *info);

enum Gpu_Subpass_Dependency_Setting {
    GPU_SUBPASS_DEPENDENCY_SETTING_ACQUIRE_TO_RENDER_TARGET_BASIC,
    GPU_SUBPASS_DEPENDENCY_SETTING_COLOR_DEPTH_BASIC_DRAW,
    GPU_SUBPASS_DEPENDENCY_SETTING_WRITE_READ_COLOR_FRAGMENT,
    GPU_SUBPASS_DEPENDENCY_SETTING_WRITE_READ_DEPTH_FRAGMENT,
};
struct Create_Vk_Subpass_Dependency_Info {
    Gpu_Subpass_Dependency_Setting access_rules;
    u32 src_subpass;
    u32 dst_subpass;
};
VkSubpassDependency create_vk_subpass_dependency(Create_Vk_Subpass_Dependency_Info *info);

struct Create_Vk_Renderpass_Info {
    u32 attachment_count;
    u32 subpass_count;
    u32 dependency_count;
    VkAttachmentDescription *attachments;
    VkSubpassDescription    *subpasses;
    VkSubpassDependency     *dependencies;
};
VkRenderPass create_vk_renderpass(VkDevice vk_device, Create_Vk_Renderpass_Info *info);
void destroy_vk_renderpass(VkDevice vk_device, VkRenderPass renderpass);

struct Gpu_Framebuffer_Info {
    VkRenderPass renderpass;
    int attachment_count;
    VkImageView *attachments;
    int width;
    int height;
};
VkFramebuffer gpu_create_framebuffer(VkDevice vk_device, Gpu_Framebuffer_Info *info);
void gpu_destroy_framebuffer(VkDevice vk_device, VkFramebuffer framebuffer);

// Attachment Views
VkImageView gpu_create_depth_attachment_view(VkDevice vk_device, VkImage vk_image);
void gpu_destroy_image_view(VkDevice vk_device, VkImageView view);

// Textures
struct Gpu_Texture_Allocation_2D {
    u64 offset;
    u64 width;
    u64 height;
};
struct Gpu_Texture_Allocator_2D {
};

// Samplers
struct Gpu_Sampler_Storage {
    int cap;
    int stored;
    VkSampler *samplers;
    VkDevice device;
};
Gpu_Sampler_Storage gpu_create_sampler_storage(int size);
void gpu_free_sampler_storage(Gpu_Sampler_Storage *storage);

enum Gpu_Sampler_Setting {
    GPU_SAMPLER_SETTING_LINEAR_REPEAT,
    GPU_SAMPLER_SETTING_MIP_LINEAR_ZERO,
};
struct Gpu_Sampler_Info {
    Gpu_Sampler_Setting filter_address_setting;
    Gpu_Sampler_Setting mip_map_setting;
    float anisotropy; // -1 == max anisotropy; 0 == off
};
VkSampler* gpu_create_samplers(Gpu_Sampler_Info *info);

#if DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_messenger_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        println("\nValidation Layer: %c", pCallbackData->pMessage);
        //std::cout << "Validation Layer: " << pCallbackData->pMessage << "\n";

    return VK_FALSE;
}

struct Create_Vk_Debug_Messenger_Info {
    VkInstance vk_instance;

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

    PFN_vkDebugUtilsMessengerCallbackEXT callback = vk_debug_messenger_callback;
};

VkDebugUtilsMessengerEXT create_debug_messenger(Create_Vk_Debug_Messenger_Info *info);

VkResult vkCreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pDebugMessenger);
void vkDestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT messenger,
        const VkAllocationCallbacks *pAllocator);

inline VkDebugUtilsMessengerCreateInfoEXT fill_vk_debug_messenger_info(Create_Vk_Debug_Messenger_Info *info) {
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info =
    {VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};

    debug_messenger_create_info.messageSeverity = info->severity;
    debug_messenger_create_info.messageType     = info->type;
    debug_messenger_create_info.pfnUserCallback = info->callback;

    return debug_messenger_create_info;
}

#endif // DEBUG (debug messenger setup)

#endif // include guard
