#ifndef SOL_ASSET_HPP_INCLUDE_GUARD_
#define SOL_ASSET_HPP_INCLUDE_GUARD_

#include "gpu.hpp"
#include "model.hpp"
#include "array.hpp"

#if TEST
void test_asset();
#endif

// @Todo I need to reimplement the allocators in a way that is more condusive to threading. Should be
// trivial as all the logic is the same, just need to create them from offsets into single buffers
// and device memory. I will do that soon. But it is fine for now. Would need to change how allocations
// are stored though. As I would want allocations to not be tied to one allocator. Maybe I should just
// largely reimplement them and just keep the bit mask memory idea, as that is really the only usable
// thing from those. And instead use a system of last used out, as opposed to a weighting, as I would
// like to avoid sorting this time...
struct Model_Allocators {
    Gpu_Allocator        index;
    Gpu_Allocator        vertex;
    Gpu_Tex_Allocator    tex;
    Sampler_Allocator    sampler;
    Image_View_Allocator image_view;
    Descriptor_Allocator descriptor_sampler [g_thread_count * g_frame_count];
    Descriptor_Allocator descriptor_resource[g_thread_count * g_frame_count];
    Uniform_Allocator    uniform            [g_thread_count * g_frame_count];
};

constexpr u32 g_assets_keys_array_tex_len        = 1024;
constexpr u32 g_assets_keys_array_index_len      = 1024;
constexpr u32 g_assets_keys_array_vertex_len     = 1024;
constexpr u32 g_assets_keys_array_sampler_len    = 1024;
constexpr u32 g_assets_keys_array_image_view_len = 1024;

constexpr u32 g_model_buffer_size                 = 1024 * 1024;
constexpr u32 g_model_buffer_allocation_alignment = 16;

struct Model;

struct Assets {
    Model_Allocators model_allocators;
    u8 *model_buffer;

    u32    model_count;
    Model *models;

    // Gpu transfer resources - @Note I have completely forgotten what these are here for...
    VkCommandPool   cmd_pools  [2];
    VkCommandBuffer cmd_buffers[2];
    VkSemaphore     semaphores [2];
    VkFence         fences     [2];
};

Assets* get_assets_instance();

void init_assets();
void kill_assets();

// Model

// @Todo Animations, Skins, Cameras.
// @Todo store more of the Gpu_Tex_Allocation data on the model.
// @Todo Find a place to store texture meta data, such as mip map levels (probably in the tex allocation,
// I have some space their before I hit the cache line I think. Maybe just keep a pointer in tex allocations
// to some extra data that we can look up if the image view is not cached. This sounds like a good plan.).

struct Texture {
    u32 texture_key;
    u32 sampler_key;
};

struct Pbr_Metallic_Roughness {
    Texture base_color_texture;
    Texture metallic_roughness_texture;
    u32     base_color_tex_coord;
    u32     metallic_roughness_tex_coord;
};

struct Normal_Texture {
    Texture texture;
    u32     tex_coord;
};

struct Occlusion_Texture {
    Texture texture;
    u32     tex_coord;
};

struct Emissive_Texture {
    Texture texture;
    u32     tex_coord;
};

enum Material_Flag_Bits {
    MATERIAL_BASE_BIT         = 0x0001,
    MATERIAL_PBR_BIT          = 0x0002, // This is a little misleading: I want it to mean just 'is there a metallic roughness texture?', but the pbr object in gltf includes the base.
    MATERIAL_NORMAL_BIT       = 0x0004,
    MATERIAL_OCCLUSION_BIT    = 0x0008,
    MATERIAL_EMISSIVE_BIT     = 0x0010,
    MATERIAL_OPAQUE_BIT       = 0x0020,
    MATERIAL_MASK_BIT         = 0x0040,
    MATERIAL_BLEND_BIT        = 0x0080,
    MATERIAL_DOUBLE_SIDED_BIT = 0x0100,
};
typedef u32 Material_Flags;

struct Material_Ubo {
    float base_color_factor[4]; // vec4
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    float emissive_factor[3]; // vec3
    float alpha_cutoff;
};

struct Material {
    Material_Flags         flags;

    Pbr_Metallic_Roughness pbr;
    Normal_Texture         normal;
    Occlusion_Texture      occlusion;
    Emissive_Texture       emissive;

    alignas(16) Material_Ubo ubo;
};

enum Accessor_Component_Type {
    ACCESSOR_COMPONENT_TYPE_SCHAR = 5120,
    ACCESSOR_COMPONENT_TYPE_UCHAR = 5121,
    ACCESSOR_COMPONENT_TYPE_S16   = 5122,
    ACCESSOR_COMPONENT_TYPE_U16   = 5123,
    ACCESSOR_COMPONENT_TYPE_U32   = 5125,
    ACCESSOR_COMPONENT_TYPE_FLOAT = 5126,
};

static constexpr u32 ACCESSOR_TYPE_COMPONENT_COUNT_SCALAR =  1;
static constexpr u32 ACCESSOR_TYPE_COMPONENT_COUNT_VEC2   =  2;
static constexpr u32 ACCESSOR_TYPE_COMPONENT_COUNT_VEC3   =  3;
static constexpr u32 ACCESSOR_TYPE_COMPONENT_COUNT_VEC4   =  4;
static constexpr u32 ACCESSOR_TYPE_COMPONENT_COUNT_MAT2   =  4;
static constexpr u32 ACCESSOR_TYPE_COMPONENT_COUNT_MAT3   =  9;
static constexpr u32 ACCESSOR_TYPE_COMPONENT_COUNT_MAT4   = 16;

enum Accessor_Flag_Bits {
    ACCESSOR_NORMALIZED_BIT           = 0x0001,
    ACCESSOR_COMPONENT_TYPE_SCHAR_BIT = 0x0002,
    ACCESSOR_COMPONENT_TYPE_UCHAR_BIT = 0x0004,
    ACCESSOR_COMPONENT_TYPE_S16_BIT   = 0x0008,
    ACCESSOR_COMPONENT_TYPE_U16_BIT   = 0x0010,
    ACCESSOR_COMPONENT_TYPE_U32_BIT   = 0x0020,
    ACCESSOR_COMPONENT_TYPE_FLOAT_BIT = 0x0040,
    ACCESSOR_TYPE_SCALAR_BIT          = 0x0080,
    ACCESSOR_TYPE_VEC2_BIT            = 0x0100,
    ACCESSOR_TYPE_VEC3_BIT            = 0x0200,
    ACCESSOR_TYPE_VEC4_BIT            = 0x0400,
    ACCESSOR_TYPE_MAT2_BIT            = 0x0800,
    ACCESSOR_TYPE_MAT3_BIT            = 0x1000,
    ACCESSOR_TYPE_MAT4_BIT            = 0x2000,

    ACCESSOR_TYPE_BITS = ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_TYPE_VEC3_BIT |
                         ACCESSOR_TYPE_VEC4_BIT   | ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_TYPE_MAT3_BIT |
                         ACCESSOR_TYPE_MAT4_BIT,

    ACCESSOR_COMPONENT_TYPE_BITS = ACCESSOR_COMPONENT_TYPE_SCHAR_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT |
                                   ACCESSOR_COMPONENT_TYPE_S16_BIT   | ACCESSOR_COMPONENT_TYPE_U16_BIT   |
                                   ACCESSOR_COMPONENT_TYPE_U32_BIT   |
                                   ACCESSOR_COMPONENT_TYPE_FLOAT_BIT,
};
typedef u32 Accessor_Flags;

struct Accessor_Max_Min {
    float max[16];
    float min[16];
};

struct Accessor_Sparse {
    Accessor_Flag_Bits indices_component_type;
    u32 count;
    u32 indices_allocation_key;
    u32 values_allocation_key;
    u64 indices_byte_offset;
    u64 values_byte_offset;
};

struct Accessor { // 44 bytes
    Accessor_Flags flags;

    u32 allocation_key;
    u32 byte_stride;
    u64 byte_offset;
    u64 count;

    Accessor_Max_Min *max_min;
    Accessor_Sparse  *sparse;
};

enum Mesh_Primitive_Attribute_Type {
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION   = 1,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL     = 2,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT    = 3,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS = 4,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR      = 5,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS     = 6,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS    = 7,
};

struct Mesh_Primitive_Attribute {
    u32 n;
    Accessor accessor;
    Mesh_Primitive_Attribute_Type type;
};

struct Morph_Target {
    u32 attribute_count;
    Mesh_Primitive_Attribute *attributes;
};

struct Allocation_Key_Counts {
    u32 index;
    u32 vertex;
    u32 tex;
    u32 sampler;
};

struct Mesh_Primitive {
    Allocation_Key_Counts key_counts;

    VkPrimitiveTopology topology;

    u32 attribute_count;
    u32 target_count;
    Accessor indices;
    Material material;
    Mesh_Primitive_Attribute *attributes;
    Morph_Target             *targets;
};

struct Mesh {
    u32 primitive_count;
    u32 weight_count;
    Mesh_Primitive *primitives;
    float          *weights;
};

struct Model {
    u64   size; // size required in model buffer
    u32   mesh_count;
    Mesh *meshes;
};

Model model_from_gltf(
    Model_Allocators *model_allocators,
    const String     *model_dir,
    const String     *gltf_file_name,
    u64               size_available,
    u8               *model_buffer,
    u64              *ret_req_size);

struct Model_Storage_Info {
    u64 offset;
    u64 size;
};
Model_Storage_Info store_model(Model *model); // @Unimplemented
Model              load_model (String *gltf_file); // @Unimplemented

struct Allocation_Key_Arrays { // 48 bytes
    u32 *index;
    u32 *vertex;
    u32 *tex;
    u32 *sampler;

    alignas(16) Allocation_Key_Counts lens; // Align 16 SIMD store.
};

struct Primitive_Draw_Info { // 16 bytes
    u32          count; // draw_count
    VkIndexType  index_type;
};

enum Primitive_Load_Result {
    PRIMITIVE_LOAD_RESULT_SUCCESS                       = 0,
    PRIMITIVE_LOAD_RESULT_PARTIAL                       = 1,
    PRIMITIVE_LOAD_RESULT_FAILED_TO_ACQUIRE_DESCRIPTORS = 2,
};

struct Primitive_Draw_Prep_Result { // 112 bytes @OutdatedComment
    Primitive_Load_Result  result;
    u64                   *success_masks;
    Primitive_Draw_Info   *primitive_draw_infos;
    VkPipeline            *pipelines;
    Allocation_Key_Arrays  failed_keys;
    Allocation_Key_Arrays  success_keys;
};

// If the return value is not 0, success masks is an array of bit masks with each set bit corresponding
// to a primitive in the array that has all its allocation keys successfully staged.
Primitive_Draw_Prep_Result prepare_to_draw_primitives(
    u32                          count,
    const Mesh_Primitive        *primitives,
    const Allocation_Key_Counts *key_counts,
    const Pl_Config             *pipeline_configs,
    bool                         adjust_weights);

#endif // include guard
