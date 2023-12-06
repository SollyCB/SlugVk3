#ifndef SOL_ASSET_HPP_INCLUDE_GUARD_
#define SOL_ASSET_HPP_INCLUDE_GUARD_

#include "gpu.hpp"
#include "model.hpp"

struct Model_Allocators {
    Gpu_Allocator     index;
    Gpu_Allocator     vertex;
    Gpu_Tex_Allocator tex;
    Sampler_Allocator sampler;
};
struct Model_Allocators_Config {}; // @Unused I am just setting some arbitrary defaults set in gpu.hpp atm.

union Model;
struct Assets {
    Model_Allocators model_allocators;

    u32    model_count;
    Model *models;

    // fonts, etc.
};

Assets* get_assets_instance();

void init_assets();
void kill_assets();

struct Model_Cube {
    Model_Type type;

    u32 tex_key_base;
    u32 tex_key_pbr;
    u64 sampler_key_base;
    u64 sampler_key_pbr;

    u32 index_key;
    u32 vertex_key;

    u32 count; // draw count (num indices)
    VkIndexType index_type;

    // Allocation offsets
    u64 offset_index;
    u64 offset_position;
    u64 offset_normal;
    u64 offset_tangent;
    u64 offset_tex_coords;

    // Pipeline info
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
struct Model_Player { // @Unimplemented
    Model_Type type;
};

union Model {
    Model_Cube   cube;
    Model_Player player;
};
Model load_model(Model_Identifier model_id);
void  free_model(Model *model);

bool prepare_to_draw_models(u32 count, Model *models); // @Unimplemented

#endif // include guard

//
// Below is code that was created very general in the prototype phase when I would see everything that I would need.
// I keep it around as it is useful as an example of how I did stuff before when I come to do different things that
// I have already kind of implemented to whatever extent.
//

#if 0
#if 0 // General model data (mostly for example)
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
};

Static_Model load_static_model(Model_Allocators *allocs, String *model_name, String *dir);
void         free_static_model(Static_Model *model);

#endif // General model data (mostly for example)
#endif
