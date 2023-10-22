#ifndef SOL_GLTF_HPP_INCLUDE_GUARD_
#define SOL_GLTF_HPP_INCLUDE_GUARD_

#include "basic.h"
#include "string.hpp"
#include "math.hpp"

enum Gltf_Accessor_Type {
    GLTF_ACCESSOR_TYPE_NONE           = 0,
    GLTF_ACCESSOR_TYPE_SCALAR         = 1,
    GLTF_ACCESSOR_TYPE_VEC2           = 2,
    GLTF_ACCESSOR_TYPE_VEC3           = 3,
    GLTF_ACCESSOR_TYPE_VEC4           = 4,
    GLTF_ACCESSOR_TYPE_MAT2           = 5,
    GLTF_ACCESSOR_TYPE_MAT3           = 6,
    GLTF_ACCESSOR_TYPE_MAT4           = 7,
    GLTF_ACCESSOR_TYPE_BYTE           = 5120,
    GLTF_ACCESSOR_TYPE_UNSIGNED_BYTE  = 5121,
    GLTF_ACCESSOR_TYPE_SHORT          = 5122,
    GLTF_ACCESSOR_TYPE_UNSIGNED_SHORT = 5123,
    GLTF_ACCESSOR_TYPE_UNSIGNED_INT   = 5125,
    GLTF_ACCESSOR_TYPE_FLOAT          = 5126,
};
// Lots of these are basically useless I think, because gltf does not define
// anywhere near as many formats as vulkan, but whatever, I cba to go through and cull them,
// only to find out I do in fact need one...
enum Gltf_Accessor_Format {
    GLTF_ACCESSOR_FORMAT_UNKNOWN = 0,

    GLTF_ACCESSOR_FORMAT_SCALAR_U8      =  13,
    GLTF_ACCESSOR_FORMAT_SCALAR_S8      =  14,

    GLTF_ACCESSOR_FORMAT_VEC2_U8        =  20,
    GLTF_ACCESSOR_FORMAT_VEC2_S8        =  21,

    GLTF_ACCESSOR_FORMAT_VEC3_U8        =  27,
    GLTF_ACCESSOR_FORMAT_VEC3_S8        =  28,

    GLTF_ACCESSOR_FORMAT_VEC4_U8        =  41,
    GLTF_ACCESSOR_FORMAT_VEC4_S8        =  42,

    // 16 bit
    GLTF_ACCESSOR_FORMAT_SCALAR_U16     =  74,
    GLTF_ACCESSOR_FORMAT_SCALAR_S16     =  75,
    GLTF_ACCESSOR_FORMAT_SCALAR_FLOAT16 =  76,

    GLTF_ACCESSOR_FORMAT_VEC2_U16       =  81,
    GLTF_ACCESSOR_FORMAT_VEC2_S16       =  82,
    GLTF_ACCESSOR_FORMAT_VEC2_FLOAT16   =  83,

    GLTF_ACCESSOR_FORMAT_VEC3_U16       =  88,
    GLTF_ACCESSOR_FORMAT_VEC3_S16       =  89,
    GLTF_ACCESSOR_FORMAT_VEC3_FLOAT16   =  90,

    GLTF_ACCESSOR_FORMAT_VEC4_U16       =  95,
    GLTF_ACCESSOR_FORMAT_VEC4_S16       =  96,
    GLTF_ACCESSOR_FORMAT_VEC4_FLOAT16   =  97,

    // 32 bit
    GLTF_ACCESSOR_FORMAT_SCALAR_U32     =  98,
    GLTF_ACCESSOR_FORMAT_SCALAR_S32     =  99,
    GLTF_ACCESSOR_FORMAT_SCALAR_FLOAT32 = 100,

    GLTF_ACCESSOR_FORMAT_VEC2_U32       = 101,
    GLTF_ACCESSOR_FORMAT_VEC2_S32       = 102,
    GLTF_ACCESSOR_FORMAT_VEC2_FLOAT32   = 103,

    GLTF_ACCESSOR_FORMAT_VEC3_U32       = 104,
    GLTF_ACCESSOR_FORMAT_VEC3_S32       = 105,
    GLTF_ACCESSOR_FORMAT_VEC3_FLOAT32   = 106,

    GLTF_ACCESSOR_FORMAT_VEC4_U32       = 107,
    GLTF_ACCESSOR_FORMAT_VEC4_S32       = 108,
    GLTF_ACCESSOR_FORMAT_VEC4_FLOAT32   = 109,

    // Matrix equivalents
    GLTF_ACCESSOR_FORMAT_MAT2_U8        = 10107,
    GLTF_ACCESSOR_FORMAT_MAT2_S8        = 10108,

    GLTF_ACCESSOR_FORMAT_MAT3_U8        = 11107,
    GLTF_ACCESSOR_FORMAT_MAT3_S8        = 11108,

    GLTF_ACCESSOR_FORMAT_MAT4_U8        = 12107,
    GLTF_ACCESSOR_FORMAT_MAT4_S8        = 12108,

    GLTF_ACCESSOR_FORMAT_MAT2_U16       = 13107,
    GLTF_ACCESSOR_FORMAT_MAT2_S16       = 13108,
    GLTF_ACCESSOR_FORMAT_MAT2_FLOAT16   = 13109,

    GLTF_ACCESSOR_FORMAT_MAT3_U16       = 14107,
    GLTF_ACCESSOR_FORMAT_MAT3_S16       = 14108,
    GLTF_ACCESSOR_FORMAT_MAT3_FLOAT16   = 14109,

    GLTF_ACCESSOR_FORMAT_MAT4_U16       = 15107,
    GLTF_ACCESSOR_FORMAT_MAT4_S16       = 15108,
    GLTF_ACCESSOR_FORMAT_MAT4_FLOAT16   = 15109,

    GLTF_ACCESSOR_FORMAT_MAT2_U32       = 16107,
    GLTF_ACCESSOR_FORMAT_MAT2_S32       = 16108,
    GLTF_ACCESSOR_FORMAT_MAT2_FLOAT32   = 16109,

    GLTF_ACCESSOR_FORMAT_MAT3_U32       = 17107,
    GLTF_ACCESSOR_FORMAT_MAT3_S32       = 17108,
    GLTF_ACCESSOR_FORMAT_MAT3_FLOAT32   = 17109,

    GLTF_ACCESSOR_FORMAT_MAT4_U32       = 18107,
    GLTF_ACCESSOR_FORMAT_MAT4_S32       = 18108,
    GLTF_ACCESSOR_FORMAT_MAT4_FLOAT32   = 18109,
};

struct Gltf_Accessor {
    Gltf_Accessor_Format format;
    Gltf_Accessor_Type indices_component_type;

    int stride;

    int buffer_view;
    int byte_stride;
    int normalized;
    int count;

    // sparse
    int sparse_count;
    int indices_buffer_view;
    int values_buffer_view;
    u64 indices_byte_offset;
    u64 values_byte_offset;
    // end sparse

    u64 byte_offset;
    float *max;
    float *min;
};

enum Gltf_Animation_Path {
    GLTF_ANIMATION_PATH_NONE        = 0,
    GLTF_ANIMATION_PATH_TRANSLATION = 1,
    GLTF_ANIMATION_PATH_ROTATION    = 2,
    GLTF_ANIMATION_PATH_SCALE       = 3,
    GLTF_ANIMATION_PATH_WEIGHTS     = 4,
};
enum Gltf_Animation_Interp {
    GLTF_ANIMATION_INTERP_LINEAR,
    GLTF_ANIMATION_INTERP_STEP,
    GLTF_ANIMATION_INTERP_CUBICSPLINE,
};
struct Gltf_Animation_Channel {
    int sampler;
    int target_node;
    Gltf_Animation_Path path;
    // align this struct to 8 bytes, saves having to use a stride field, instead can just use
    // regular indexing (because the allocation in the linear allocator is aligned to 8 bytes)
    char pad[4];
};
struct Gltf_Animation_Sampler {
    int input;
    int output;
    Gltf_Animation_Interp interp;
    // align this struct to 8 bytes, saves having to use a stride field, instead can just use
    // regular indexing (because the allocation in the linear allocator is aligned to 8 bytes)
    char pad[4];
};
struct Gltf_Animation {
    int stride;

    // @AccessPattern I wonder if there is a nice way to pack these. Without use case, I dont know if interleaving
    // might be useful. For now it seems not to be.
    int channel_count;
    int sampler_count;
    Gltf_Animation_Channel *channels;
    Gltf_Animation_Sampler *samplers;
};

struct Gltf_Buffer {
    int stride; // accounts for the length of the uri string
    u64 byte_length;
    char *uri;
};

enum Gltf_Buffer_Type {
    GLTF_BUFFER_TYPE_ARRAY_BUFFER         = 34962,
    GLTF_BUFFER_TYPE_ELEMENT_ARRAY_BUFFER = 34963,
};
struct Gltf_Buffer_View {
    int stride;

    int buffer;
    int byte_stride;
    Gltf_Buffer_Type buffer_type; // I think I dont need this for vulkan, it seems OpenGL specific...
    u64 byte_offset;
    u64 byte_length;
};

struct Gltf_Camera {
    int stride;
    int ortho; // @BoolsInStructs int for bool, alignment
    float znear;
    float zfar;
    float x_factor;
    float y_factor;
};

struct Gltf_Image {
    int stride;
    int jpeg; // @BoolsInStructs int for bool, alignment
    int buffer_view;
    char *uri;
};

enum Gltf_Alpha_Mode {
    GLTF_ALPHA_MODE_OPAQUE = 0,
    GLTF_ALPHA_MODE_MASK   = 1,
    GLTF_ALPHA_MODE_BLEND  = 2,
};
struct Gltf_Material {
    int stride;

    // pbr_metallic_roughness
    float base_color_factor[4] = {1, 1, 1, 1};
    float metallic_factor      = 1;
    float roughness_factor     = 1;
    int base_color_texture_index = -1;
    int base_color_tex_coord = -1;
    int metallic_roughness_texture_index = -1;
    int metallic_roughness_tex_coord = -1;

    // normal_texture
    float normal_scale = 1;
    int normal_texture_index = -1;
    int normal_tex_coord = -1;

    // occlusion_texture
    float occlusion_strength = 1;
    int occlusion_texture_index = -1;
    int occlusion_tex_coord = -1;

    // emissive_texture
    float emissive_factor[3] = {0, 0, 0};
    int emissive_texture_index = -1;
    int emissive_tex_coord = -1;

    // alpha
    Gltf_Alpha_Mode alpha_mode = GLTF_ALPHA_MODE_OPAQUE;
    float alpha_cutoff = 0.5;
    int double_sided; // @BoolsInStructs big bool
};

enum Gltf_Mesh_Attribute_Type {
    GLTF_MESH_ATTRIBUTE_TYPE_POSITION = 1,
    GLTF_MESH_ATTRIBUTE_TYPE_NORMAL   = 2,
    GLTF_MESH_ATTRIBUTE_TYPE_TANGENT  = 3,
    GLTF_MESH_ATTRIBUTE_TYPE_TEXCOORD = 4,
    GLTF_MESH_ATTRIBUTE_TYPE_COLOR    = 5,
    GLTF_MESH_ATTRIBUTE_TYPE_JOINTS   = 6,
    GLTF_MESH_ATTRIBUTE_TYPE_WEIGHTS  = 7,
};
struct Gltf_Mesh_Attribute {
    Gltf_Mesh_Attribute_Type type;
    // really not sure about this n value. I just dont know how big n can get...
    int n;
    int accessor_index;
};
struct Gltf_Morph_Target {
    int stride;

    int attribute_count;
    Gltf_Mesh_Attribute *attributes;
};
enum Gltf_Primitive_Topology {
    GLTF_PRIMITIVE_TOPOLOGY_POINT_LIST     = 0,
    GLTF_PRIMITIVE_TOPOLOGY_LINE_LIST      = 1,
    GLTF_PRIMITIVE_TOPOLOGY_LINE_STRIP     = 2,
    GLTF_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST  = 3,
    GLTF_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP = 4,
    GLTF_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN   = 5,
};
struct Gltf_Mesh_Primitive {
    int stride;

    // @BigTodo This type needs to be rejigged to better support different counts of vertex attributes
    int extra_attribute_count;
    int target_count;
    int indices;
    int material;
    int topology = GLTF_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // This is a bit messy. Really everything should just be in the 'extra_attributes' array and renames 'attributes'
    int position = -1;
    int tangent = -1;
    int normal = -1;
    int tex_coord_0 = -1;

    Gltf_Mesh_Attribute *extra_attributes;
    Gltf_Morph_Target   *targets;
};
struct Gltf_Mesh {
    int stride;

    int primitive_count;
    int weight_count;
    Gltf_Mesh_Primitive *primitives;
    float *weights;
};

struct Gltf_Trs {
    Vec3 translation = {0.0, 0.0, 0.0};
    Vec4 rotation    = {0.0, 0.0, 0.0, 1.0};
    Vec3 scale       = {1.0, 1.0, 1.0};
};
struct Gltf_Node {
    int stride;
    int camera;
    int skin;
    int mesh;
    int child_count;
    int weight_count;

    union {
        Gltf_Trs trs;
        Mat4 matrix;
    };

    int *children;
    float *weights;
};

enum Gltf_Sampler_Filter {
    GLTF_SAMPLER_FILTER_NEAREST = 0, 
    GLTF_SAMPLER_FILTER_LINEAR  = 1, 
};
enum Gltf_Sampler_Mipmap_Mode {
    GLTF_SAMPLER_MIPMAP_MODE_NEAREST = 0,
    GLTF_SAMPLER_MIPMAP_MODE_LINEAR  = 1,
};
enum Gltf_Sampler_Address_Mode {
    GLTF_SAMPLER_ADDRESS_MODE_REPEAT          = 0, 
    GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT = 1, 
    GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE   = 2, 
};
struct Gltf_Sampler {
    int stride;
    Gltf_Sampler_Filter mag_filter = GLTF_SAMPLER_FILTER_LINEAR;
    Gltf_Sampler_Filter min_filter = GLTF_SAMPLER_FILTER_LINEAR;
    Gltf_Sampler_Address_Mode wrap_u = GLTF_SAMPLER_ADDRESS_MODE_REPEAT;
    Gltf_Sampler_Address_Mode wrap_v = GLTF_SAMPLER_ADDRESS_MODE_REPEAT;
};

struct Gltf_Scene {
    int stride;
    int node_count;
    int *nodes;
};

struct Gltf_Skin {
    int stride;
    int inverse_bind_matrices;
    int skeleton;
    int joint_count;
    int *joints;
};

struct Gltf_Texture {
    int stride;
    int sampler;
    int source_image;
};

struct Gltf {
    // Each arrayed field has a 'stride' member, which is the byte count required to reach 
    // the next array member;
    //
    // All strides can be calculated from the other info in the struct, but some of these algorithms 
    // are weird and incur unclear overhead. So for consistency's sake they will just be included, 
    // regardless of the ease with which the strides can be calculated. 
    u32 total_primitive_count;

    int scene;
    int *accessor_count;
    int *animation_count;
    int *buffer_count;
    int *buffer_view_count;
    int *camera_count;
    int *image_count;
    int *material_count;
    int *mesh_count;
    int *node_count;
    int *sampler_count;
    int *scene_count;
    int *skin_count;
    int *texture_count;
    Gltf_Accessor *accessors;
    Gltf_Animation *animations;
    Gltf_Buffer *buffers;
    Gltf_Buffer_View *buffer_views;
    Gltf_Camera *cameras;
    Gltf_Image *images;
    Gltf_Material *materials;
    Gltf_Mesh *meshes;
    Gltf_Node *nodes;
    Gltf_Sampler *samplers;
    Gltf_Scene *scenes;
    Gltf_Skin *skins;
    Gltf_Texture *textures;
};
Gltf parse_gltf(const char *file_name);

Gltf_Accessor* gltf_accessor_by_index(Gltf *gltf, int i);
Gltf_Animation* gltf_animation_by_index(Gltf *gltf, int i);
Gltf_Buffer* gltf_buffer_by_index(Gltf *gltf, int i);
Gltf_Buffer_View* gltf_buffer_view_by_index(Gltf *gltf, int i);
Gltf_Camera* gltf_camera_by_index(Gltf *gltf, int i);
Gltf_Image* gltf_image_by_index(Gltf *gltf, int i);
Gltf_Material* gltf_material_by_index(Gltf *gltf, int i);
Gltf_Mesh* gltf_mesh_by_index(Gltf *gltf, int i);
Gltf_Node* gltf_node_by_index(Gltf *gltf, int i);
Gltf_Sampler* gltf_sampler_by_index(Gltf *gltf, int i);
Gltf_Scene* gltf_scene_by_index(Gltf *gltf, int i);
Gltf_Skin* gltf_skin_by_index(Gltf *gltf, int i);
Gltf_Texture* gltf_texture_by_index(Gltf *gltf, int i);

int gltf_accessor_get_count(Gltf *gltf);
int gltf_animation_get_count(Gltf *gltf);
int gltf_buffer_get_count(Gltf *gltf);
int gltf_buffer_view_get_count(Gltf *gltf);
int gltf_camera_get_count(Gltf *gltf);
int gltf_image_get_count(Gltf *gltf);
int gltf_material_get_count(Gltf *gltf);
int gltf_mesh_get_count(Gltf *gltf);
int gltf_node_get_count(Gltf *gltf);
int gltf_sampler_get_count(Gltf *gltf);
int gltf_scene_get_count(Gltf *gltf);
int gltf_skin_get_count(Gltf *gltf);
int gltf_texture_get_count(Gltf *gltf);

#if TEST
    void test_gltf();
#endif

#endif // include guard
