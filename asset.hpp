#ifndef SOL_ASSET_HPP_INCLUDE_GUARD_
#define SOL_ASSET_HPP_INCLUDE_GUARD_

#include "gpu.hpp"
#include "model.hpp"

struct Model_Allocators {
    Gpu_Allocator        index;
    Gpu_Allocator        vertex;
    Gpu_Tex_Allocator    tex;
    Sampler_Allocator    sampler;
    Image_View_Allocator image_view;
    Descriptor_Allocator descriptor_sampler;
    Descriptor_Allocator descriptor_resource;
};
struct Model_Allocators_Config {}; // @Unused I am just setting some arbitrary size defaults set in gpu.hpp atm.

constexpr u32 g_assets_keys_array_tex_len        = 256;
constexpr u32 g_assets_keys_array_index_len      = 256;
constexpr u32 g_assets_keys_array_vertex_len     = 256;
constexpr u32 g_assets_keys_array_sampler_len    = 256;
constexpr u32 g_assets_keys_array_image_view_len = 256;

struct Model;

struct Assets {
    Model_Allocators model_allocators;

    u32    model_count;
    Model *models;

    u32 pos_keys_tex;
    u32 pos_keys_index;
    u32 pos_keys_vertex;
    u32 pos_keys_sampler;
    u32 pos_keys_image_view;

    // Read by allocators
    Array<u32> keys_index;
    Array<u32> keys_vertex;
    Array<u32> keys_tex;

    // @Note Will need to add another descriptor array here for uniform buffers? I dont think so.
    Array<u64> keys_sampler;
    Array<u64> keys_image_view;

    // Written by allocators
    Array<bool> results_tex;
    Array<bool> results_index;
    Array<bool> results_vertex;
    Array<bool> results_sampler;
    Array<bool> results_image_view;

    u32 pos_pipelines;
    Array<VkPipeline> pipelines;

    // fonts, etc.

    // Gpu transfer resources
    VkCommandPool   cmd_pools  [2];
    VkCommandBuffer cmd_buffers[2];
    VkSemaphore     semaphores [2];
    VkFence         fences     [2];
};

Assets* get_assets_instance();

void init_assets();
void kill_assets();

// Model

// @Todo Animations, Skins, Cameras
// @Todo store more of the Gpu_Tex_Allocation data on the model

struct Pbr_Metallic_Roughness { // 40 bytes
    float base_color_factor[4] = {1,1,1,1};
    float metallic_factor      = 1;
    float roughness_factor     = 1;
    u32   base_color_texture;
    u32   base_color_tex_coord;
    u32   metallic_roughness_texture;
    u32   metallic_roughness_tex_coord;
};

struct Normal_Texture { // 12 bytes
    float scale = 1;
    u32   texture;
    u32   tex_coord;
};

struct Occlusion_Texture { // 12 bytes
    float strength = 1;
    u32   texture;
    u32   tex_coord;
};

struct Emissive_Texture { // 20 bytes
    float factor[3] = {0,0,0};
    u32   texture;
    u32   tex_coord;
};

enum Material_Flag_Bits {
    MATERIAL_BASE_BIT                             = 0x0001,
    MATERIAL_PBR_METALLIC_ROUGHNESS_BIT           = 0x0002,
    MATERIAL_NORMAL_BIT                           = 0x0004,
    MATERIAL_OCCLUSION_BIT                        = 0x0008,
    MATERIAL_EMISSIVE_BIT                         = 0x0010,
    MATERIAL_BASE_TEX_COORD_BIT                   = 0x0020,
    MATERIAL_PBR_METALLIC_ROUGHNESS_TEX_COORD_BIT = 0x0040,
    MATERIAL_NORMAL_TEX_COORD_BIT                 = 0x0080,
    MATERIAL_OCCLUSION_TEX_COORD_BIT              = 0x0100,
    MATERIAL_EMISSIVE_TEX_COORD_BIT               = 0x0200,
    MATERIAL_OPAQUE_BIT                           = 0x0400,
    MATERIAL_MASK_BIT                             = 0x0800,
    MATERIAL_BLEND_BIT                            = 0x1000,
    MATERIAL_DOUBLE_SIDED_BIT                     = 0x2000,
};
typedef u32 Material_Flags;

struct Material { // 92 bytes
    Material_Flags         flags;                    // 4
    float                  alpha_cutoff       = 0.5; // 4

    Pbr_Metallic_Roughness pbr_metallic_roughness;   // 40
    Normal_Texture         normal;                   // 12
    Occlusion_Texture      occlusion;                // 12
    Emissive_Texture       emissive;                 // 20

    char pad[128 - 92]; // 4 + 4 + 40 + 12 + 12 + 20
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
    u32 accessor;
    Mesh_Primitive_Attribute_Type type;
};

struct Morph_Target {
    u32 attribute_count;
    Mesh_Primitive_Attribute *attributes;
};

struct Mesh_Primitive {
    VkPrimitiveTopology topology;

    u32 indices;
    u32 material;
    u32 attribute_count;
    u32 target_count;
    Mesh_Primitive_Attribute *attributes;
    Morph_Target             *targets;
};

struct Mesh {
    u32 primitive_count;
    u32 weight_count;
    Mesh_Primitive *primitives;
    float          *weights;
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
    ACCESSOR_BUFFER_VIEW_BIT          = 0x0001,
    ACCESSOR_BYTE_OFFSET_BIT          = 0x0002, // Irrelevant, but cba changing all the flags
    ACCESSOR_NORMALIZED_BIT           = 0x0004,
    ACCESSOR_MAX_MIN_BIT              = 0x0008,
    ACCESSOR_SPARSE_BIT               = 0x0010,
    ACCESSOR_COMPONENT_TYPE_SCHAR_BIT = 0x0020,
    ACCESSOR_COMPONENT_TYPE_UCHAR_BIT = 0x0040,
    ACCESSOR_COMPONENT_TYPE_S16_BIT   = 0x0080,
    ACCESSOR_COMPONENT_TYPE_U16_BIT   = 0x0100,
    ACCESSOR_COMPONENT_TYPE_U32_BIT   = 0x0200,
    ACCESSOR_COMPONENT_TYPE_FLOAT_BIT = 0x0400,
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
    u32 indices_buffer_view;
    u32 values_buffer_view;
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

struct Texture { // Allocation keys
    u64 sampler;
    u32 texture;
};

struct Model {
    u64       size; // size required in model buffer
    u32       accessor_count;
    u32       material_count;
    u32       texture_count;
    u32       mesh_count;
    Accessor *accessors;
    Material *materials;
    Texture  *textures;
    Mesh     *meshes;
};

Model model_from_gltf(String *gltf_file);

struct Model_Storage_Info {
    u64 offset;
    u64 size;
};
Model_Storage_Info store_model(Model *model); // @Unimplemented
Model              load_model (String *gltf_file);

#endif // include guard
