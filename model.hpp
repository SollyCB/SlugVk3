#ifndef SOL_MODEL_HPP_INCLUDE_GUARD_
#define SOL_MODEL_HPP_INCLUDE_GUARD_

#include <vulkan/vulkan_core.h>
#include "typedef.h"
#include "string.hpp"

static String g_model_file_names[] = {
    cstr_to_string("Cube.gltf"),
    cstr_to_string("CesiumMan.gltf"),
};
static String g_model_dir_names[] = {
    cstr_to_string("models/cube-static/"),
    cstr_to_string("models/cesium-man/"),
};

static const u32 g_model_count = sizeof(g_model_file_names) / sizeof(g_model_file_names[0]);

struct Texture_Info {
    u32 texture;
    u32 tex_coord;
};

struct Pbr_Metallic_Roughness { // 40 bytes
    float        base_color_factor[4] = {1,1,1,1};
    float        metallic_factor      = 1;
    float        roughness_factor     = 1;
    Texture_Info base_color_texture;
    Texture_Info metallic_roughness_texture;
};

struct Normal_Texture { // 12 bytes
    float        scale = 1;
    Texture_Info texture;
};

struct Occlusion_Texture { // 12 bytes
    float        strength = 1;
    Texture_Info texture;
};

enum Material_Flag_Bits { // @Todo I had duplicate flags here, indicating I was going to add smtg - check gltf mat ref
    MATERIAL_PBR_METALLIC_ROUGHNESS_BIT = 0x0001,
    MATERIAL_NORMAL_BIT                 = 0x0002,
    MATERIAL_OCCLUSION_BIT              = 0x0004,
    MATERIAL_EMISSIVE_BIT               = 0x0008,
    MATERIAL_MASK_BIT                   = 0x0010,
    MATERIAL_BLEND_BIT                  = 0x0020,
    MATERIAL_DOUBLE_SIDED_BIT           = 0x0040,
};
typedef u32 Material_Flags;

struct Material { // 92 bytes
    Material_Flags         flags;

    Pbr_Metallic_Roughness pbr_metallic_roughness;
    Normal_Texture         normal_texture;
    Occlusion_Texture      occlusion_texture;
    Texture_Info           emissive_texture;
    float                  emissive_factor[3] = {0,0,0};
    float                  alpha_cutoff       = 0.5;

    char pad[128 - 92];
};

enum Mesh_Primitive_Attribute_Type {
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORD_0,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORD_1,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORD_2,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORD_3,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR_0,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR_1,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR_2,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_COLOR_3,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS_0,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS_1,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS_2,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS_3,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS_0,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS_1,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS_2,
    MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS_3,
};

struct Mesh_Primitive_Attribute {
    u32 accessor;
    Mesh_Primitive_Attribute_Type type;
};

struct Morph_Target {
    u32 target_count;
    Mesh_Primitive_Attribute *attributes;
};

struct Mesh_Primitive {
    VkPrimitiveTopology topology;

    u32 indices;
    u32 material;
    u32 attribute_count;
    Mesh_Primitive_Attribute *attributes;
};

struct Mesh {
    u32  primitive_count;
    u64 *primitives;
    // @Todo Idk how to store and retrieve weights off the top of my head.
};

enum Buffer_View_Type {
    BUFFER_VIEW_TYPE_INDEX,
    BUFFER_VIEW_TYPE_VERTEX,
};
struct Buffer_View {
    Buffer_View_Type type;
    u32 allocation_key;
    u64 byte_stride;
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
    ACCESSOR_BYTE_OFFSET_BIT          = 0x0002,
    ACCESSOR_NORMALIZED_BIT           = 0x0004,
    ACCESSOR_MAX_MIN_BIT              = 0x0008,
    ACCESSOR_SPARSE_BIT               = 0x0010,
    ACCESSOR_COMPONENT_TYPE_SCHAR_BIT = 0x0020,
    ACCESSOR_COMPONENT_TYPE_UCHAR_BIT = 0x0040,
    ACCESSOR_COMPONENT_TYPE_S16_BIT   = 0x0080,
    ACCESSOR_COMPONENT_TYPE_U16_BIT   = 0x0100,
    ACCESSOR_COMPONENT_TYPE_U32_BIT   = 0x0200,
    ACCESSOR_COMPONENT_TYPE_FLOAT_BIT = 0x0400,
    ACCESSOR_TYPE_SCALAR              = 0x0080,
    ACCESSOR_TYPE_VEC2                = 0x0100,
    ACCESSOR_TYPE_VEC3                = 0x0200,
    ACCESSOR_TYPE_VEC4                = 0x0400,
    ACCESSOR_TYPE_MAT2                = 0x0800,
    ACCESSOR_TYPE_MAT3                = 0x1000,
    ACCESSOR_TYPE_MAT4                = 0x2000,

    ACCESSOR_TYPE_BITS = ACCESSOR_TYPE_SCALAR | ACCESSOR_TYPE_VEC2 | ACCESSOR_TYPE_VEC3 |
                         ACCESSOR_TYPE_VEC4   | ACCESSOR_TYPE_MAT2 | ACCESSOR_TYPE_MAT3 |
                         ACCESSOR_TYPE_MAT4,

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
    u32 count;
    u32 indices_buffer_view;
    u32 values_buffer_view;
    u64 indices_byte_offset;
    u64 values_byte_offset;
};

struct Accessor {
    Accessor_Flags flags;
    u32 buffer_view;
    u64 byte_offset;
    u64 count;

    Accessor_Max_Min *max_min;
    Accessor_Sparse  *sparse;
};

struct Model {
    String       model_data;
    u32          accessor_count;
    u32          buffer_view_count;
    u32          mesh_count;
    Accessor    *accessors;
    Buffer_View *buffer_views;
    Mesh        *meshes;
};

#endif // include guard
