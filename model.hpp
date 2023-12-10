/*
------------------------------------------------------------------------------------------------------------------------

   ** The below explanation is a little incoherent at points, but the main point I think is plenty clear - I just **
   ** quickly wrote it out. @Todo improve the explanation **

------------------------------------------------------------------------------------------------------------------------

   This file is really intended to simulate model subsets as a system for model loading. In a more developed
   app, I would want to load models as they correspond to particular subsets. For instance, I imagine here
   CesiumMan as a player model, as it is an animated figure with some arbitrary skin an material attributes.

   If one were to use only one model loading function which branched on all potential vertex and material
   attributes that a model might have, this would be an expensive and annoying to maintain function. It would
   be preferable to instead branch once outside the function in order to call the appropriate function to load
   the given model type. This requires writing more functions, but each function is faaar easier to reason
   about, as it is clear about what exactly it will be doing.

   Some examples: it is likely the case that we have many building models, but in terms of loading them all,
   we only need one function which can manage them all without having to dynamically branch, as a building has
   a predefined set of attributes (maybe a metallic roughness texture, normal texture, but no emissive
   texture). Now we want to load cars, but we cannot use the same function, as now we need an emissive texture
   for the headlights, plus the glass, interior and the body require different meshes, primitives etc., but
   again one function is appropriate for all cars.

   As I do have a number of models which all fit given subsets, I am simulating it in this little way. I think
   it is a decent implementation...???

                                       ******** @Todo @Note *********
   I understand that for a larger app, defining the models in this way where the programmer has to order
   the models and define what type they are a part of is unsustainable, so later I will come to writing some
   custom file format or something which makes it easier for a human to enumerate models, and then this file
   can be generated  automatically.
*/
#ifndef SOL_MODEL_HPP_INCLUDE_GUARD_
#define SOL_MODEL_HPP_INCLUDE_GUARD_

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

// @Note model ids must appear in the enum in the same order that they appear in 'model_file_names'
enum Model_Id {
    MODEL_ID_CUBE       = 0,
    MODEL_ID_CESIUM_MAN = 1,
};
enum Model_Type { // These are mostly just example types for now - Sol 6 Dec 2023
    MODEL_TYPE_INVALID  = 0,
    MODEL_TYPE_CUBE     = 1,
    MODEL_TYPE_PLAYER   = 2,
    MODEL_TYPE_BUILDING = 3,
    MODEL_TYPE_CAR      = 4,
};

struct Model_Identifier {
    Model_Id   id;
    Model_Type type;
};

// @Note This identifier array must be ordered by model id
static Model_Identifier g_model_identifiers[] = {
    {MODEL_ID_CUBE,       MODEL_TYPE_CUBE},
    {MODEL_ID_CESIUM_MAN, MODEL_TYPE_PLAYER},
};

static const u32 g_model_count = sizeof(g_model_file_names) / sizeof(g_model_file_names[0]);

struct Animation_Sampler {};

struct Model_Cube {
    Model_Type type;

    //
    // On other, more complicated models, this info would be arrayed by primitives,
    // but a cube model type only hase one primitive.
    //

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
constexpr u32 g_model_type_primitive_count_cube          = 1;
constexpr u32 g_model_type_descriptor_count_cube         = 2; // 2 textures: base + pbr
constexpr u32 g_model_type_descriptor_binding_count_cube = 1; // 1 array[2] of combined image samplers.
constexpr u32 g_model_type_descriptor_set_count_cube     = 1; // 1 set: texture set

// @Unimplemented I am just using player to imagine how a more complex model would work, to see if my
// system scales.
struct Model_Player_Bone {};
struct Model_Player_Skeleton {
    Model_Player_Bone bones[16];
};
struct Model_Player_Run {};
struct Model_Player_Walk {};
struct Model_Player { // @Unimplemented
    Model_Type type;

    Model_Player_Skeleton skeleton;
    Model_Player_Run      run;
    Model_Player_Walk     walk;
};
const u32 g_model_type_descriptor_count_player  = 1; // @Todo This value is not correct, just random one for now.
const u32 g_model_type_primitive_count_player   = 1;

// @Unimplemented I am just using car to imagine how a more complex model would work, to see if my
// system scales.
struct Model_Mesh_Car_Glass {
    u32 allocation_key_index;
    u32 allocation_key_position;
    u32 allocation_key_normal;
    u32 allocation_key_tex_coords;

    VkPrimitiveTopology topology;

    u32 tex_key_base;
    u32 tex_key_slightly_broken;
    u32 tex_key_medium_broken;
    u32 tex_key_very_broken;
    u64 sampler_key_base;
    u64 sampler_key_pbr;
};
struct Model_Mesh_Car_Body {
    u32 allocation_key_index;
    u32 allocation_key_position;
    u32 allocation_key_normal;
    u32 allocation_key_tex_coords;

    VkPrimitiveTopology topology;

    u32 tex_key_base;
    u32 tex_key_pbr;
    u64 sampler_key_base;
    u64 sampler_key_pbr;
};
struct Model_Mesh_Car_Wheel {
    u32 allocation_key_index;
    u32 allocation_key_position;
    u32 allocation_key_normal;
    u32 allocation_key_tex_coords;

    VkPrimitiveTopology topology;

    u32 tex_key_base;
    u32 tex_key_pbr;
    u64 sampler_key_base;
    u64 sampler_key_pbr;
};
union Model_Car_Mesh {
    Model_Mesh_Car_Glass  mesh_glass;
    Model_Mesh_Car_Body   mesh_body;
    Model_Mesh_Car_Wheel  mesh_wheel;
};
struct Model_Car_Spin_Wheel {
    u32 wheel_index;
    Animation_Sampler animation_sampler;
};
struct Model_Car_Turn_Wheel {
    u32 wheel_index;
    Animation_Sampler animation_sampler;
};
struct Model_Car_Tilt_Body {
    Animation_Sampler animation_sampler;
};
struct Model_Car {
    Model_Type type;

    Model_Mesh_Car_Glass  mesh_glass;
    Model_Mesh_Car_Body   mesh_body;
    Model_Mesh_Car_Wheel  mesh_wheel[4];

    Model_Car_Spin_Wheel spin_wheel;
    Model_Car_Turn_Wheel turn_wheel;
    Model_Car_Tilt_Body  tilt_body;
};
const u32 g_model_type_descriptor_count_car  = 1; // @Unimplemented This is just a random value
const u32 g_model_type_primitive_count_car = 1;

union Model {
    Model_Cube   cube;
    Model_Player player;
};

#endif // include guard
