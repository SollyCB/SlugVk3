#include "gltf.hpp"
#include "file.hpp"
#include "simd.hpp"
#include "builtin_wrappers.h"
#include "math.hpp"

#if TEST
    #include "test.hpp"
#endif

/*
    WARNING!! This parser has very strict memory alignment rules!!
*/

// @Note Notes on file implementation process and old code at the bottom of the file

Gltf parse_gltf(const char *filename);

Gltf_Animation* gltf_parse_animations(const char *data, u64 *offset, int *animation_count);
Gltf_Animation_Channel* gltf_parse_animation_channels(const char *data, u64 *offset, int *channel_count);
Gltf_Animation_Sampler* gltf_parse_animation_samplers(const char *data, u64 *offset, int *sampler_count);

Gltf_Accessor* gltf_parse_accessors(const char *data, u64 *offset, int *accessor_count);
void gltf_parse_accessor_sparse(const char *data, u64 *offset, Gltf_Accessor *accessor);

Gltf_Buffer* gltf_parse_buffers(const char *data, u64 *offset, int *buffer_count);
Gltf_Buffer_View* gltf_parse_buffer_views(const char *data, u64 *offset, int *buffer_view_count);

Gltf_Camera* gltf_parse_cameras(const char *data, u64 *offset, int *camera_count);

Gltf_Image* gltf_parse_images(const char *data, u64 *offset, int *image_count);

Gltf_Material* gltf_parse_materials(const char *data, u64 *offset, int *material_count);
void gltf_parse_texture_info(const char *data, u64 *offset, int *index, int *tex_coord, float *scale, float *strength);

Gltf_Mesh* gltf_parse_meshes(const char *data, u64 *offset, int *mesh_count);
Gltf_Mesh_Primitive* gltf_parse_mesh_primitives(const char *data, u64 *offset, int *primitive_count);
Gltf_Mesh_Attribute* gltf_parse_mesh_attributes(const char *data, u64 *offset, int *attribute_count, bool targets, int *position, int *tangent, int *normal, int *tex_coord_0);

Gltf_Node* gltf_parse_nodes(const char *data, u64 *offset, int *node_count);

Gltf_Sampler* gltf_parse_samplers(const char *data, u64 *offset, int *sampler_count);

Gltf_Scene* gltf_parse_scenes(const char *data, u64 *offset, int *scene_count);

Gltf_Skin* gltf_parse_skins(const char *data, u64 *offset, int *skin_count);

Gltf_Texture* gltf_parse_textures(const char *data, u64 *offset, int *texture_count);

/* **Implementation start** */
static inline int gltf_match_int(char c) {
    switch(c) {
    case '0':
        return 0;
    case '1':
        return 1;
    case '2':
        return 2;
    case '3':
        return 3;
    case '4':
        return 4;
    case '5':
        return 5;
    case '6':
        return 6;
    case '7':
        return 7;
    case '8':
        return 8;
    case '9':
        return 9;
    default:
        ASSERT(false && "not an int", "");
        return -1;
    }
}
inline int gltf_ascii_to_int(const char *data, u64 *offset) {
    u64 inc = 0;
    if (!simd_skip_to_int(data, &inc, 128))
        ASSERT(false, "Failed to find an integer in search range");

    int accum = 0;
    while(data[inc] >= '0' && data[inc] <= '9') {
        accum *= 10;
        accum += gltf_match_int(data[inc]);
        inc++;
    }
    *offset += inc;
    return accum;
}
inline u64 gltf_ascii_to_u64(const char *data, u64 *offset) {
    u64 inc = 0;
    if (!simd_skip_to_int(data, &inc, 128))
        ASSERT(false, "Failed to find an integer in search range");

    u64 accum = 0;
    while(data[inc] >= '0' && data[inc] <= '9') {
        accum *= 10;
        accum += gltf_match_int(data[inc]);
        inc++;
    }
    *offset += inc;
    return accum;
}

Gltf parse_gltf(const char *filename) {
    //
    // Function Method:
    //     While there is a '"' before a closing brace in the file, jump to the '"' as '"' means a key;
    //     match the key and call its parser function.
    //
    //     Each parser function increments the file offset to point to the end of whatver it parsed,
    //     so if a closing brace is ever found before a key, there must be no keys left in the file.
    //
    u64 size;
    const char *data = (const char*)file_read_char_temp_padded(filename, &size, 16);
    Gltf gltf;
    char buf[16];
    u64 offset = 0;

    int accessor_count = 0;
    int animation_count = 0;
    int buffer_count = 0;
    int buffer_view_count = 0;
    int camera_count = 0;
    int image_count = 0;
    int material_count = 0;
    int mesh_count = 0;
    int node_count = 0;
    int sampler_count = 0;
    int scene_count = 0;
    int skin_count = 0;
    int texture_count = 0;

    while (simd_find_char_interrupted(data + offset, '"', '}', &offset)) {
        offset++; // step into key
        if (simd_strcmp_short(data + offset, "accessorsxxxxxxx", 7) == 0) {
            gltf.accessors = gltf_parse_accessors(data + offset, &offset, &accessor_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "animationsxxxxxx", 6) == 0) {
            gltf.animations = gltf_parse_animations(data + offset, &offset, &animation_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "buffersxxxxxxxxx", 9) == 0) {
            gltf.buffers = gltf_parse_buffers(data + offset, &offset, &buffer_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "bufferViewsxxxxx", 5) == 0) {
            gltf.buffer_views = gltf_parse_buffer_views(data + offset, &offset, &buffer_view_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "camerasxxxxxxxxx", 9) == 0) {
            gltf.cameras = gltf_parse_cameras(data + offset, &offset, &camera_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "imagesxxxxxxxxxx", 10) == 0) {
            gltf.images = gltf_parse_images(data + offset, &offset, &image_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "materialsxxxxxxx", 7) == 0) {
            gltf.materials = gltf_parse_materials(data + offset, &offset, &material_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "meshesxxxxxxxxxx", 10) == 0) {
            gltf.meshes = gltf_parse_meshes(data + offset, &offset, &mesh_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "nodesxxxxxxxxxxx", 11) == 0) {
            gltf.nodes = gltf_parse_nodes(data + offset, &offset, &node_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "samplersxxxxxxxx", 8) == 0) {
            gltf.samplers = gltf_parse_samplers(data + offset, &offset, &sampler_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "scenesxxxxxxxxxx", 10) == 0) {
            gltf.scenes = gltf_parse_scenes(data + offset, &offset, &scene_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "skinsxxxxxxxxxxx", 11) == 0) {
            gltf.skins = gltf_parse_skins(data + offset, &offset, &skin_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "texturesxxxxxxxx", 8) == 0) {
            gltf.textures = gltf_parse_textures(data + offset, &offset, &texture_count);
            continue;
        } else if (simd_strcmp_short(data + offset, "assetxxxxxxxxxxx", 11) == 0) {
            simd_skip_passed_char(data + offset, &offset, '}');
            continue;
        } else if (simd_strcmp_short(data + offset, "scenexxxxxxxxxxx", 11) == 0) {
            gltf.scene = gltf_ascii_to_int(data + offset, &offset);
            continue;
        } else {
            ASSERT(false, "This is not a top level gltf key");
        }
    }

    //
    // OMFG!! I practically have to rewrite this thing!!! One day maybe I will idk...
    // This is sort of hack, sort of not, depending how you look at it (COPIUS-MAXIMUS!?)
    // My beautiful memory access patterns! Ruined! How did I not consider the fact that
    // this whole file format is about random access!!??
    //
    int total_stride = 0;

    gltf.accessor_count = (int*)malloc_t(sizeof(int) * accessor_count + 1, 4);
    gltf.accessor_count[0] = accessor_count;
    gltf.accessor_count++;
    Gltf_Accessor *accessor = gltf.accessors;
    for(int i = 0; i < accessor_count; ++i) {
        gltf.accessor_count[i] = total_stride;
        total_stride += accessor->stride;
        accessor = (Gltf_Accessor*)((u8*)accessor + accessor->stride);
    }

    total_stride = 0;

    gltf.animation_count = (int*)malloc_t(sizeof(int) * animation_count + 1, 4);
    gltf.animation_count[0] = animation_count;
    gltf.animation_count++;
    Gltf_Animation *animation = gltf.animations;
    for(int i = 0; i < animation_count; ++i) {
        gltf.animation_count[i] = total_stride;
        total_stride += animation->stride;
        animation = (Gltf_Animation*)((u8*)animation + animation->stride);
    }

    total_stride = 0;

    gltf.buffer_count = (int*)malloc_t(sizeof(int) * buffer_count + 1, 4);
    gltf.buffer_count[0] = buffer_count;
    gltf.buffer_count++;
    Gltf_Buffer *buffer = gltf.buffers;
    for(int i = 0; i < buffer_count; ++i) {
        gltf.buffer_count[i] = total_stride;
        total_stride += buffer->stride;
        buffer = (Gltf_Buffer*)((u8*)buffer + buffer->stride);
    }

    total_stride = 0;

    gltf.buffer_view_count = (int*)malloc_t(sizeof(int) * buffer_view_count + 1, 4);
    gltf.buffer_view_count[0] = buffer_view_count;
    gltf.buffer_view_count++;
    Gltf_Buffer_View *buffer_view = gltf.buffer_views;
    for(int i = 0; i < buffer_view_count; ++i) {
        gltf.buffer_view_count[i] = total_stride;
        total_stride += buffer_view->stride;
        buffer_view = (Gltf_Buffer_View*)((u8*)buffer_view + buffer_view->stride);
    }

    total_stride = 0;

    gltf.camera_count = (int*)malloc_t(sizeof(int) * camera_count + 1, 4);
    gltf.camera_count[0] = camera_count;
    gltf.camera_count++;
    Gltf_Camera *camera = gltf.cameras;
    for(int i = 0; i < camera_count; ++i) {
        gltf.camera_count[i] = total_stride;
        total_stride += camera->stride;
        camera = (Gltf_Camera*)((u8*)camera + camera->stride);
    }

    total_stride = 0;

    gltf.image_count = (int*)malloc_t(sizeof(int) * image_count + 1, 4);
    gltf.image_count[0] = image_count;
    gltf.image_count++;
    Gltf_Image *image = gltf.images;
    for(int i = 0; i < image_count; ++i) {
        gltf.image_count[i] = total_stride;
        total_stride += image->stride;
        image = (Gltf_Image*)((u8*)image + image->stride);
    }

    total_stride = 0;

    gltf.material_count = (int*)malloc_t(sizeof(int) * material_count + 1, 4);
    gltf.material_count[0] = material_count;
    gltf.material_count++;
    Gltf_Material *material = gltf.materials;
    for(int i = 0; i < material_count; ++i) {
        gltf.material_count[i] = total_stride;
        total_stride += material->stride;
        material = (Gltf_Material*)((u8*)material + material->stride);
    }

    total_stride = 0;

    gltf.mesh_count = (int*)malloc_t(sizeof(int) * mesh_count + 1, 4);
    gltf.mesh_count[0] = mesh_count;
    gltf.mesh_count++;
    Gltf_Mesh *mesh = gltf.meshes;
    for(int i = 0; i < mesh_count; ++i) {
        gltf.mesh_count[i] = total_stride;
        total_stride += mesh->stride;
        mesh = (Gltf_Mesh*)((u8*)mesh + mesh->stride);
    }

    total_stride = 0;

    gltf.node_count = (int*)malloc_t(sizeof(int) * node_count + 1, 4);
    gltf.node_count[0] = node_count;
    gltf.node_count++;
    Gltf_Node *node = gltf.nodes;
    for(int i = 0; i < node_count; ++i) {
        gltf.node_count[i] = total_stride;
        total_stride += node->stride;
        node = (Gltf_Node*)((u8*)node + node->stride);
    }

    total_stride = 0;

    gltf.sampler_count = (int*)malloc_t(sizeof(int) * sampler_count + 1, 4);
    gltf.sampler_count[0] = sampler_count;
    gltf.sampler_count++;
    Gltf_Sampler *sampler = gltf.samplers;
    for(int i = 0; i < sampler_count; ++i) {
        gltf.sampler_count[i] = total_stride;
        total_stride += sampler->stride;
        sampler = (Gltf_Sampler*)((u8*)sampler + sampler->stride);
    }

    total_stride = 0;

    gltf.scene_count = (int*)malloc_t(sizeof(int) * scene_count + 1, 4);
    gltf.scene_count[0] = scene_count;
    gltf.scene_count++;
    Gltf_Scene *scene = gltf.scenes;
    for(int i = 0; i < scene_count; ++i) {
        gltf.scene_count[i] = total_stride;
        total_stride += scene->stride;
        scene = (Gltf_Scene*)((u8*)scene + scene->stride);
    }

    total_stride = 0;

    gltf.skin_count = (int*)malloc_t(sizeof(int) * skin_count + 1, 4);
    gltf.skin_count[0] = skin_count;
    gltf.skin_count++;
    Gltf_Skin *skin = gltf.skins;
    for(int i = 0; i < skin_count; ++i) {
        gltf.skin_count[i] = total_stride;
        total_stride += skin->stride;
        skin = (Gltf_Skin*)((u8*)skin + skin->stride);
    }

    total_stride = 0;

    gltf.texture_count = (int*)malloc_t(sizeof(int) * texture_count + 1, 4);
    gltf.texture_count[0] = texture_count;
    gltf.texture_count++;
    Gltf_Texture *texture = gltf.textures;
    for(int i = 0; i < texture_count; ++i) {
        gltf.texture_count[i] = total_stride;
        total_stride += texture->stride;
        texture = (Gltf_Texture*)((u8*)texture + texture->stride);
    }

    //
    // @ERROR @Stride
    // @Note Idk what to do about stride here. I will wait and see if the validation
    // layers complain about stride being 0 later...
    //
    accessor = gltf.accessors;
    for(int i = 0; i < gltf.accessor_count[-1]; ++i) {
        buffer_view =
            (Gltf_Buffer_View*)((u8*)gltf.buffer_views +
                gltf.buffer_view_count[accessor->buffer_view]);

        if (buffer_view->byte_stride)
            accessor->byte_stride = buffer_view->byte_stride;

        accessor = (Gltf_Accessor*)((u8*)accessor + accessor->stride);
    }

    return gltf;
}

// helper algorithms start

float gltf_ascii_to_float(const char *data, u64 *offset) {
    u64 inc = 0;
    simd_skip_to_int(data, &inc, Max_u64);

    bool neg = data[inc - 1] == '-';
    bool seen_dot = false;
    int after_dot = 0;
    float accum = 0;
    int num;
    while((data[inc] >= '0' && data[inc] <= '9') || data[inc] == '.') {
        if (data[inc] == '.') {
            seen_dot = true;
            inc++;
        }

        if (seen_dot)
            after_dot++;

        num = gltf_match_int(data[inc]);
        accum *= 10;
        accum += num;

        inc++; // will point beyond the last int at the end of the loop, no need for +1
    }

    // The line below this comment used to say...
    //
    // for(int i = 0; i < after_dot; ++i)
    //     accum /= 10;
    //
    // ...It is little pieces of code like this that make me worry my code is actually slow while
    // I am believing it to be fast lol
    accum /= pow(10, after_dot);

    *offset += inc;

    if (neg)
        accum = -accum;

    return accum;
}

inline int gltf_parse_int_array(const char *data, u64 *offset, int *array) {
    int i = 0;
    u64 inc = 0;
    while(simd_find_int_interrupted(data + inc, ']', &inc)) {
        array[i] = gltf_ascii_to_int(data + inc, &inc);
        i++;
    }
    *offset += inc + 1; // +1 go beyond the cloasing square bracket (prevent early exit at accessors list level)
    return i;
}
inline int gltf_parse_float_array(const char *data, u64 *offset, float *array) {
    int i = 0;
    u64 inc = 0;
    while(simd_find_int_interrupted(data + inc, ']', &inc)) {
        array[i] = gltf_ascii_to_float(data + inc, &inc);
        i++;
    }
    *offset += inc + 1; // +1 go beyond the cloasing square bracket
    return i;
}

inline Mat4 gltf_ascii_to_mat4(const char *data, u64 *offset) {
    u64 inc = 0;
    Mat4 ret;

    float vec[4];
    for(int i = 0; i < 4; ++i)
        vec[i] = gltf_ascii_to_float(data + inc, &inc);
    ret.row0 = {vec[0], vec[1], vec[2], vec[3]};

    for(int i = 0; i < 4; ++i)
        vec[i] = gltf_ascii_to_float(data + inc, &inc);
    ret.row1 = {vec[0], vec[1], vec[2], vec[3]};

    for(int i = 0; i < 4; ++i)
        vec[i] = gltf_ascii_to_float(data + inc, &inc);
    ret.row2 = {vec[0], vec[1], vec[2], vec[3]};

    for(int i = 0; i < 4; ++i)
        vec[i] = gltf_ascii_to_float(data + inc, &inc);
    ret.row3 = {vec[0], vec[1], vec[2], vec[3]};

    simd_skip_passed_char(data + inc, &inc, ']'); // go beyond array close
    return ret;
}
// algorithms end


// `Accessors
// @Todo check that all defaults are being properly set
Gltf_Accessor* gltf_parse_accessors(const char *data, u64 *offset, int *accessor_count) {
    u64 inc = 0; // track position in file
    simd_skip_passed_char(data, &inc, '[', Max_u64);

    int count = 0; // accessor count
    float max[16];
    float min[16];
    int min_max_len;
    int temp;
    bool min_found;
    bool max_found;

    Gltf_Accessor_Type accessor_type           = GLTF_ACCESSOR_TYPE_NONE;
    Gltf_Accessor_Type accessor_component_type = GLTF_ACCESSOR_TYPE_NONE;

    // aligned pointer to return
    Gltf_Accessor *ret = (Gltf_Accessor*)malloc_t(0, 8);
    // pointer for allocating to in loops
    Gltf_Accessor *accessor;

    //
    // Function Method:
    //     outer loop jumps through the objects in the list,
    //     inner loop jumps through the keys in the objects
    //

    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++; // increment accessor count

        // Temp allocation made for every accessor struct. Keeps shit packed, linear allocators are fast...
        accessor = (Gltf_Accessor*)malloc_t(sizeof(Gltf_Accessor), 8);
        *accessor = {};
        accessor->indices_component_type = GLTF_ACCESSOR_TYPE_NONE;
        accessor->format = GLTF_ACCESSOR_FORMAT_UNKNOWN;
        min_max_len = 0;
        min_found = false;
        max_found = false;

        while (simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // go beyond the '"' found by find_char_inter...
            //simd_skip_passed_char(data + inc, &inc, '"', Max_u64); // skip to beginning of key

            //
            // I do not like all these branch misses, but I cant see a better way. Even if I make a system
            // to remove options if they have already been chosen, there would still be at least one branch
            // with no improvement in predictability... (I think)
            //
            // ...I think this is just how parsing text goes...
            //

            // match keys to parse methods
            if (simd_strcmp_short(data + inc, "bufferViewxxxxxx",  6) == 0) {
                accessor->buffer_view = gltf_ascii_to_int(data + inc, &inc);
                continue; // go to next key
            } else if (simd_strcmp_short(data + inc, "byteOffsetxxxxxx",  6) == 0) {
                accessor->byte_offset = gltf_ascii_to_u64(data + inc, &inc);
                continue; // go to next key
            } else if (simd_strcmp_short(data + inc, "countxxxxxxxxxxx", 11) == 0) {
                accessor->count = gltf_ascii_to_int(data + inc, &inc);
                continue; // go to next key
            } else if (simd_strcmp_short(data + inc, "componentTypexxx",  3) == 0) {
                accessor_component_type = (Gltf_Accessor_Type)gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "typexxxxxxxxxxxx", 12) == 0) {

                simd_skip_passed_char_count(data + inc, '"', 2, &inc); // jump into value string

                if (simd_strcmp_short(data + inc, "SCALARxxxxxxxxxx", 10) == 0)
                    accessor_type = GLTF_ACCESSOR_TYPE_SCALAR;
                else if (simd_strcmp_short(data + inc, "VEC2xxxxxxxxxxxx", 12) == 0)
                    accessor_type = GLTF_ACCESSOR_TYPE_VEC2;
                else if (simd_strcmp_short(data + inc, "VEC3xxxxxxxxxxxx", 12) == 0)
                    accessor_type = GLTF_ACCESSOR_TYPE_VEC3;
                else if (simd_strcmp_short(data + inc, "VEC4xxxxxxxxxxxx", 12) == 0)
                    accessor_type = GLTF_ACCESSOR_TYPE_VEC4;
                else if (simd_strcmp_short(data + inc, "MAT2xxxxxxxxxxxx", 12) == 0)
                    accessor_type = GLTF_ACCESSOR_TYPE_MAT2;
                else if (simd_strcmp_short(data + inc, "MAT3xxxxxxxxxxxx", 12) == 0)
                    accessor_type = GLTF_ACCESSOR_TYPE_MAT3;
                else if (simd_strcmp_short(data + inc, "MAT4xxxxxxxxxxxx", 12) == 0)
                    accessor_type = GLTF_ACCESSOR_TYPE_MAT4;

                simd_skip_passed_char(data + inc, &inc, '"'); // skip passed the value string
                continue;
            } else if (simd_strcmp_short(data + inc, "normalizedxxxxxx", 6) == 0) {
                simd_skip_passed_char(data + inc, &inc, ':', Max_u64);
                simd_skip_whitespace(data + inc, &inc); // go the beginning of 'true' || 'false' ascii string
                if (simd_strcmp_short(data + inc, "truexxxxxxxxxxxx", 12) == 0) {
                    accessor->normalized = 1;
                    inc += 5; // go passed the 'true' in the file (no quotation marks to skip)
                }
                else {
                    accessor->normalized = 0;
                    inc += 6; // go passed the 'false' in the file
                }

                continue; // go to next key
            } else if (simd_strcmp_short(data + inc, "sparsexxxxxxxxxx", 10) == 0) {
                gltf_parse_accessor_sparse(data + inc, &inc, accessor);
                continue; // go to next key
            } else if (simd_strcmp_short(data + inc, "maxxxxxxxxxxxxxx", 13) == 0) {
                min_max_len = gltf_parse_float_array(data + inc, &inc, max);
                max_found = true;
                continue; // go to next key
            } else if (simd_strcmp_short(data + inc, "minxxxxxxxxxxxxx", 13) == 0) {
                min_max_len = gltf_parse_float_array(data + inc, &inc, min);
                min_found = true;
                continue; // go to next key
            }

        }
        if (min_found && max_found) {
            // @MemAlign careful here
            temp = align(sizeof(float) * min_max_len * 2, 8);
            accessor->max = (float*)malloc_t(temp, 8);
            accessor->min = accessor->max + min_max_len;

            memcpy(accessor->max, max, sizeof(float) * min_max_len);
            memcpy(accessor->min, min, sizeof(float) * min_max_len);

            accessor->stride = sizeof(Gltf_Accessor) + (temp);
        } else {
            accessor->stride = sizeof(Gltf_Accessor);
        }
        switch(accessor_type) {
        case GLTF_ACCESSOR_TYPE_SCALAR:
        {
            switch(accessor_component_type) {
            case GLTF_ACCESSOR_TYPE_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_SCALAR_S8;
                accessor->byte_stride = 1;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_SCALAR_U8;
                accessor->byte_stride = 1;
                break;
            }
            case GLTF_ACCESSOR_TYPE_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_SCALAR_S16;
                accessor->byte_stride = 2;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_SCALAR_U16;
                accessor->byte_stride = 2;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_INT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_SCALAR_U32;
                accessor->byte_stride = 4;
                break;
            }
            case GLTF_ACCESSOR_TYPE_FLOAT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_SCALAR_FLOAT32;
                accessor->byte_stride = 4;
                break;
            }
            default:
                ASSERT(false, "Not a valid accessor component type");
                break;
            } // switch component_type

            break;

        } // case SCALAR
        case GLTF_ACCESSOR_TYPE_VEC2:
        {
            switch(accessor_component_type) {
            case GLTF_ACCESSOR_TYPE_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC2_S8;
                accessor->byte_stride = 2;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC2_U8;
                accessor->byte_stride = 2;
                break;
            }
            case GLTF_ACCESSOR_TYPE_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC2_S16;
                accessor->byte_stride = 4;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC2_U16;
                accessor->byte_stride = 4;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_INT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC2_U32;
                accessor->byte_stride = 8;
                break;
            }
            case GLTF_ACCESSOR_TYPE_FLOAT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC2_FLOAT32;
                accessor->byte_stride = 8;
                break;
            }
            default:
                ASSERT(false, "Not a valid accessor component type");
                break;
            } // switch component_type

            break;

        } // case VEC2
        case GLTF_ACCESSOR_TYPE_VEC3:
        {
            switch(accessor_component_type) {
            case GLTF_ACCESSOR_TYPE_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC3_S8;
                accessor->byte_stride = 3;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC3_U8;
                accessor->byte_stride = 3;
                break;
            }
            case GLTF_ACCESSOR_TYPE_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC3_S16;
                accessor->byte_stride = 6;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC3_U16;
                accessor->byte_stride = 6;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_INT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC3_U32;
                accessor->byte_stride = 12;
                break;
            }
            case GLTF_ACCESSOR_TYPE_FLOAT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC3_FLOAT32;
                accessor->byte_stride = 12;
                break;
            }
            default:
                ASSERT(false, "Not a valid accessor component type");
                break;
            } // switch component_type

            break;

        } // case VEC3
        case GLTF_ACCESSOR_TYPE_VEC4:
        {
            switch(accessor_component_type) {
            case GLTF_ACCESSOR_TYPE_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC4_S8;
                accessor->byte_stride = 4;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC4_U8;
                accessor->byte_stride = 4;
                break;
            }
            case GLTF_ACCESSOR_TYPE_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC4_S16;
                accessor->byte_stride = 8;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC4_U16;
                accessor->byte_stride = 8;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_INT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC4_U32;
                accessor->byte_stride = 16;
                break;
            }
            case GLTF_ACCESSOR_TYPE_FLOAT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_VEC4_FLOAT32;
                accessor->byte_stride = 16;
                break;
            }
            default:
                ASSERT(false, "Not a valid accessor component type");
                break;
            } // switch component_type

            break;

        } // case VEC4
        case GLTF_ACCESSOR_TYPE_MAT2:
        {
            switch(accessor_component_type) {
            case GLTF_ACCESSOR_TYPE_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT2_S8;
                accessor->byte_stride = 4;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT2_U8;
                accessor->byte_stride = 4;
                break;
            }
            case GLTF_ACCESSOR_TYPE_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT2_S16;
                accessor->byte_stride = 8;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT2_U16;
                accessor->byte_stride = 8;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_INT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT2_U32;
                accessor->byte_stride = 16;
                break;
            }
            case GLTF_ACCESSOR_TYPE_FLOAT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT2_FLOAT32;
                accessor->byte_stride = 16;
                break;
            }
            default:
                ASSERT(false, "Not a valid accessor component type");
                break;
            } // switch component_type

            break;

        } // case MAT2
        case GLTF_ACCESSOR_TYPE_MAT3:
        {
            switch(accessor_component_type) {
            case GLTF_ACCESSOR_TYPE_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT3_S8;
                accessor->byte_stride = 9;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT3_U8;
                accessor->byte_stride = 9;
                break;
            }
            case GLTF_ACCESSOR_TYPE_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT3_S16;
                accessor->byte_stride = 18;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT3_U16;
                accessor->byte_stride = 18;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_INT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT3_U32;
                accessor->byte_stride = 36;
                break;
            }
            case GLTF_ACCESSOR_TYPE_FLOAT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT3_FLOAT32;
                accessor->byte_stride = 36;
                break;
            }
            default:
                ASSERT(false, "Not a valid accessor component type");
                break;
            } // switch component_type

            break;

        } // case MAT3
        case GLTF_ACCESSOR_TYPE_MAT4:
        {
            switch(accessor_component_type) {
            case GLTF_ACCESSOR_TYPE_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT4_S8;
                accessor->byte_stride = 16;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_BYTE:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT4_U8;
                accessor->byte_stride = 16;
                break;
            }
            case GLTF_ACCESSOR_TYPE_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT4_S16;
                accessor->byte_stride = 32;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_SHORT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT4_U16;
                accessor->byte_stride = 32;
                break;
            }
            case GLTF_ACCESSOR_TYPE_UNSIGNED_INT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT4_U32;
                accessor->byte_stride = 64;
                break;
            }
            case GLTF_ACCESSOR_TYPE_FLOAT:
            {
                accessor->format = GLTF_ACCESSOR_FORMAT_MAT4_FLOAT32;
                accessor->byte_stride = 64;
                break;
            }
            default:
                    ASSERT(false, "Invalid Accessor Type");
            } // switch component_type

            break;

        } // case MAT4
        default:
            ASSERT(false, "Not a valud accessor type");
            break;
        } // switch type
    }
    *accessor_count = count;
    *offset += inc;
    return ret;
}
void gltf_parse_accessor_sparse(const char *data, u64 *offset, Gltf_Accessor *accessor) {
    u64 inc = 0;
    simd_find_char_interrupted(data + inc, '{', '}', &inc); // find sparse start
    while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
        inc++; // go beyond the '"'
        if (simd_strcmp_short(data + inc, "countxxxxxxxxxxx", 11) == 0)  {
            accessor->sparse_count = gltf_ascii_to_int(data + inc, &inc);
            continue;
        } else if (simd_strcmp_short(data + inc, "indicesxxxxxxxxx", 9) == 0) {
            simd_find_char_interrupted(data + inc, '{', '}', &inc); // find indices start
            while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
                inc++; // go passed the '"'
                if (simd_strcmp_short(data + inc, "bufferViewxxxxxx", 6) == 0) {
                    accessor->indices_buffer_view = gltf_ascii_to_int(data + inc, &inc);
                    continue;
                }
                if (simd_strcmp_short(data + inc, "byteOffsetxxxxxx", 6) == 0) {
                    accessor->indices_byte_offset = gltf_ascii_to_u64(data + inc, &inc);
                    continue;
                }
                if (simd_strcmp_short(data + inc, "componentTypexxx", 3) == 0) {
                    accessor->indices_component_type = (Gltf_Accessor_Type)gltf_ascii_to_int(data + inc, &inc);
                    continue;
                }
            }
            simd_find_char_interrupted(data + inc, '}', '{', &inc); // find indices end
            inc++; // go beyond
            continue;
        } else if (simd_strcmp_short(data + inc, "valuesxxxxxxxxx", 10) == 0) {
            simd_find_char_interrupted(data + inc, '{', '}', &inc); // find values start
            while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
                inc++; // go passed the '"'
                if (simd_strcmp_short(data + inc, "bufferViewxxxxxx", 6) == 0) {
                    accessor->values_buffer_view = gltf_ascii_to_int(data + inc, &inc);
                    continue;
                }
                if (simd_strcmp_short(data + inc, "byteOffsetxxxxxx", 6) == 0) {
                    accessor->values_byte_offset = gltf_ascii_to_u64(data + inc, &inc);
                    continue;
                }
            }
            simd_find_char_interrupted(data + inc, '}', '{', &inc); // find indices end
            inc++; // go beyond
            continue;
        }
    }
    *offset += inc + 1; // +1 go beyond the last curly brace in sparse object
}

// `Animations
Gltf_Animation_Channel* gltf_parse_animation_channels(const char *data, u64 *offset, int *channel_count) {
    // Aligned pointer to return
    Gltf_Animation_Channel *channels = (Gltf_Animation_Channel*)malloc_t(0, 8);
    // pointer for allocating to in loops
    Gltf_Animation_Channel *channel;

    //
    // Function Method:
    //     outer loop to jump through the list of objects
    //     inner loop to jump through the keys in an object
    //

    u64 inc = 0;   // track pos in file
    int count = 0; // track object count

    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        channel = (Gltf_Animation_Channel*)malloc_t(sizeof(Gltf_Animation_Channel), 8);
        *channel = {};
        count++;
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) { // channel loop
            inc++; // go beyond opening '"' in key
            if (simd_strcmp_short(data + inc, "samplerxxxxxxxxx",  9) == 0) {
                channel->sampler = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "targetxxxxxxxxxx", 10) == 0) {
                simd_skip_passed_char(data + inc, &inc, '{');
                // loop through 'target' object's keys
                while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
                    inc++;
                    if (simd_strcmp_short(data + inc, "nodexxxxxxxxxxxx", 12) == 0) {
                       channel->target_node = gltf_ascii_to_int(data + inc, &inc);
                       continue;
                    } else if (simd_strcmp_short(data + inc, "pathxxxxxxxxxxxx", 12) == 0) {

                        //
                        // string keys + string values are brutal for error proneness...
                        // have to consider everytime what you are skipping
                        //

                        // skip passed closing '"' in 'path' key, skip passed opening '"' in the value string
                        simd_skip_passed_char_count(data + inc, '"', 2, &inc);
                        if(simd_strcmp_short(data + inc, "translationxxxxx", 5) == 0) {
                            channel->path = GLTF_ANIMATION_PATH_TRANSLATION;
                        } else if(simd_strcmp_short(data + inc, "rotationxxxxxxxx", 8) == 0) {
                            channel->path = GLTF_ANIMATION_PATH_ROTATION;
                        } else if(simd_strcmp_short(data + inc, "scalexxxxxxxxxxx", 11) == 0) {
                            channel->path = GLTF_ANIMATION_PATH_SCALE;
                        } else if(simd_strcmp_short(data + inc, "weightsxxxxxxxxx", 9) == 0) {
                            channel->path = GLTF_ANIMATION_PATH_WEIGHTS;
                        }
                        simd_skip_passed_char(data + inc, &inc, '"');
                    }
                }
                inc++; // go passed closing brace of 'target' object to avoid early 'channel' loop exit
            }
        }
    }

    *channel_count = count;
    *offset += inc + 1; // go beyond array closing char
    return channels;
}
Gltf_Animation_Sampler* gltf_parse_animation_samplers(const char *data, u64 *offset, int *sampler_count) {
    //
    // Function Method:
    //     outer loop to jump through the list of objects
    //     inner loop to jump through the keys in an object
    //
    Gltf_Animation_Sampler *samplers = (Gltf_Animation_Sampler*)malloc_t(0, 8); // get pointer to beginning of sampler allocations
    Gltf_Animation_Sampler *sampler; // temp pointer to allocate to in loops

    u64 inc = 0;   // track file pos
    int count = 0; // track sampler count

    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        sampler = (Gltf_Animation_Sampler*)malloc_t(sizeof(Gltf_Animation_Sampler), 8);
        *sampler = {};
        count++;
        sampler->interp = GLTF_ANIMATION_INTERP_LINEAR;
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // go beyond opening '"' of key
            if (simd_strcmp_short(data + inc, "inputxxxxxxxxxxx", 11) == 0) {
                sampler->input = gltf_ascii_to_int(data + inc, &inc);
                continue; // Idk if continue and else if is destroying some branch predict algorithm, I hope not...
            } else if (simd_strcmp_short(data + inc, "outputxxxxxxxxxx", 10) == 0) {
                sampler->output = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "interpolationxxx", 3) == 0) {
                simd_skip_passed_char_count(data + inc, '"', 2, &inc); // jump into value string
                if (simd_strcmp_short(data + inc, "LINEARxxxxxxxxxx", 10) == 0) {
                    sampler->interp = GLTF_ANIMATION_INTERP_LINEAR;
                    simd_skip_passed_char(data + inc, &inc, '"'); // skip passed the end of the value
                    continue;
                } else if (simd_strcmp_short(data + inc, "STEPxxxxxxxxxxxx", 12) == 0) {
                    sampler->interp = GLTF_ANIMATION_INTERP_STEP;
                    simd_skip_passed_char(data + inc, &inc, '"'); // skip passed the end of the value
                    continue;
                } else if (simd_strcmp_short(data + inc, "CUBICSPLINExxxxx", 5) == 0) {
                    sampler->interp = GLTF_ANIMATION_INTERP_CUBICSPLINE;
                    simd_skip_passed_char(data + inc, &inc, '"'); // skip passed the end of the value
                    continue;
                } else {
                    ASSERT(false, "This is not a valid interpolation type");
                }
                simd_skip_passed_char(data + inc, &inc, '"');
                continue;
            }
        }
    }

    *sampler_count = count;
    *offset += inc + 1; // go beyond array closing char
    return samplers;
}
Gltf_Animation* gltf_parse_animations(const char *data, u64 *offset, int *animation_count) {
    //
    // Function Method:
    //     outer loop jumps through the list of animation objects
    //     inner loop jumps through the keys in each object
    //

    Gltf_Animation *animations = (Gltf_Animation*)malloc_t(0, 8); // get aligned pointer to return
    Gltf_Animation *animation; // pointer for allocating to in loops

    u64 inc = 0;   // track pos in file
    int count = 0; // track object count

    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) { // jump to object start
        ++count;
        animation = (Gltf_Animation*)malloc_t(sizeof(Gltf_Animation), 8);
        *animation = {};
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // enter the key
            if (simd_strcmp_short(data + inc, "namexxxxxxxxxxxx", 12) == 0) {
                // skip "name" key. Have to jump 3 quotation marks: key end, value both
                simd_skip_passed_char_count(data + inc, '"', 3, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "channelsxxxxxxxx", 8) == 0) {
                animation->channels = gltf_parse_animation_channels(data + inc, &inc, &animation->channel_count);
                continue;
            } else if (simd_strcmp_short(data + inc, "samplersxxxxxxxx", 8) == 0) {
                animation->samplers = gltf_parse_animation_samplers(data + inc, &inc, &animation->sampler_count);
                continue;
            }
        }
        animation->stride =  sizeof(Gltf_Animation) +
                            (sizeof(Gltf_Animation_Channel) * animation->channel_count) +
                            (sizeof(Gltf_Animation_Sampler) * animation->sampler_count);
    }

    inc++; // go passed closing ']'
    *offset += inc;
    *animation_count = count;
    return animations;
}

// `Buffers
Gltf_Buffer* gltf_parse_buffers(const char *data, u64 *offset, int *buffer_count) {
    Gltf_Buffer *buffers = (Gltf_Buffer*)malloc_t(0, 8); // pointer to start of array to return
    Gltf_Buffer *buffer; // temp pointer to allocate to while parsing

    u64 inc = 0; // track file pos locally
    int count = 0; // track obj count locally
    int uri_len;

    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        buffer = (Gltf_Buffer*)malloc_t(sizeof(Gltf_Buffer), 8);
        *buffer = {};
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // go beyond opening '"'
            if (simd_strcmp_short(data + inc, "byteLengthxxxxxx", 6) == 0) {
                buffer->byte_length = gltf_ascii_to_u64(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "urixxxxxxxxxxxxx", 13) == 0) {
                simd_skip_passed_char_count(data + inc, '"', 2, &inc); // step inside value string
                uri_len = simd_strlen(data + inc, '"') + 1; // +1 for null termination
                buffer->uri = (char*)malloc_t(uri_len, 1);
                memcpy(buffer->uri, data + inc, uri_len);
                buffer->uri[uri_len - 1] = '\0';
                simd_skip_passed_char(data + inc, &inc, '"'); // step inside value string
                continue;
            }
        }
        buffer->stride = align(sizeof(Gltf_Buffer) + uri_len, 8);
    }

    *offset += inc;
    *buffer_count = count;
    return buffers;
}

// `BufferViews
Gltf_Buffer_View* gltf_parse_buffer_views(const char *data, u64 *offset, int *buffer_view_count) {
    Gltf_Buffer_View *buffer_views = (Gltf_Buffer_View*)malloc_t(0, 8);
    Gltf_Buffer_View *buffer_view;
    u64 inc = 0;
    int count = 0;

    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        buffer_view = (Gltf_Buffer_View*)malloc_t(sizeof(Gltf_Buffer_View), 8);
        *buffer_view = {};
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // step beyond key's opening '"'
            if (simd_strcmp_short(data + inc, "bufferxxxxxxxxxx", 10) == 0) {
                buffer_view->buffer = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "byteOffsetxxxxxx",  6) == 0) {
                buffer_view->byte_offset = gltf_ascii_to_u64(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "byteLengthxxxxxx",  6) == 0) {
                buffer_view->byte_length = gltf_ascii_to_u64(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "byteStridexxxxxx",  6) == 0) {
                buffer_view->byte_stride = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "targetxxxxxxxxxx", 10) == 0) {
                buffer_view->buffer_type = (Gltf_Buffer_Type)gltf_ascii_to_int(data + inc, &inc);
                continue;
            }
        }
        buffer_view->stride = sizeof(Gltf_Buffer_View);
    }

    *offset += inc;
    *buffer_view_count = count;
    return buffer_views;
}

// `Cameras
Gltf_Camera* gltf_parse_cameras(const char *data, u64 *offset, int *camera_count) {
    Gltf_Camera *cameras = (Gltf_Camera*)malloc_t(0, 8);
    Gltf_Camera *camera;

    u64 inc = 0;
    int count = 0;
    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        camera = (Gltf_Camera*)malloc_t(sizeof(Gltf_Camera), 8);
        *camera = {};
        count++;
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // step into key
            if (simd_strcmp_short(data + inc, "typexxxxxxxxxxxx", 12) == 0) {
                simd_skip_passed_char_count(data + inc, '"', 2, &inc);
                if (simd_strcmp_short(data + inc, "orthographicxxxx", 4) == 0) {
                    camera->ortho = true;
                    simd_skip_passed_char(data + inc, &inc, '"');
                    continue;
                } else {
                    camera->ortho = false;
                    simd_skip_passed_char(data + inc, &inc, '"');
                    continue;
                }
            } else if (simd_strcmp_short(data + inc, "orthographicxxxx", 4) == 0) {
                simd_skip_passed_char(data + inc, &inc, '"');
                while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
                    inc++;
                    if (simd_strcmp_short(data + inc, "xmagxxxxxxxxxxxx", 12) == 0) {
                        camera->x_factor = gltf_ascii_to_float(data + inc, &inc);
                        continue;
                    } else if (simd_strcmp_short(data + inc, "ymagxxxxxxxxxxxx", 12) == 0) {
                        camera->y_factor = gltf_ascii_to_float(data + inc, &inc);
                        continue;
                    } else if (simd_strcmp_short(data + inc, "zfarxxxxxxxxxxxx", 12) == 0) {
                        camera->zfar = gltf_ascii_to_float(data + inc, &inc);
                        continue;
                    } else if (simd_strcmp_short(data + inc, "znearxxxxxxxxxxx", 11) == 0) {
                        camera->znear = gltf_ascii_to_float(data + inc, &inc);
                        continue;
                    }
                }
            } else if (simd_strcmp_short(data + inc, "perspectivexxxxx", 5) == 0) {
                simd_skip_passed_char(data + inc, &inc, '"');
                while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
                    inc++;
                    if (simd_strcmp_short(data + inc, "aspectRatioxxxxx", 5) == 0) {
                        camera->x_factor = gltf_ascii_to_float(data + inc, &inc);
                        continue;
                    } else if (simd_strcmp_short(data + inc, "yfovxxxxxxxxxxxx", 12) == 0) {
                        camera->y_factor = gltf_ascii_to_float(data + inc, &inc);
                        continue;
                    } else if (simd_strcmp_short(data + inc, "zfarxxxxxxxxxxxx", 12) == 0) {
                        camera->zfar = gltf_ascii_to_float(data + inc, &inc);
                        continue;
                    } else if (simd_strcmp_short(data + inc, "znearxxxxxxxxxxx", 11) == 0) {
                        camera->znear = gltf_ascii_to_float(data + inc, &inc);
                        continue;
                    }
                }
            }
        }
        camera->stride = sizeof(Gltf_Camera);
    }

    *offset += inc;
    *camera_count = count;
    return cameras;
}

// `Images
Gltf_Image* gltf_parse_images(const char *data, u64 *offset, int *image_count) {
    Gltf_Image *images = (Gltf_Image*)malloc_t(0, 8);
    Gltf_Image *image;

    u64 inc = 0;
    int count = 0;
    int uri_len;
    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        image = (Gltf_Image*)malloc_t(sizeof(Gltf_Image), 8);
        *image = {};
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++;
            if (simd_strcmp_short(data + inc, "urixxxxxxxxxxxxx", 13) == 0) {
                simd_skip_passed_char_count(data + inc, '"', 2, &inc);
                uri_len = simd_strlen(data + inc, '"') + 1;
                image->uri = (char*)malloc_t(uri_len, 1);
                memcpy(image->uri, data + inc, uri_len);
                image->uri[uri_len - 1] = '\0';
                simd_skip_passed_char(data + inc, &inc, '"');
                continue;
            } else if (simd_strcmp_short(data + inc, "mimeTypexxxxxxxx", 8) == 0) {
                uri_len = 0;
                image->uri = NULL;
                simd_skip_passed_char_count(data + inc, '"', 2, &inc);
                if (simd_strcmp_short(data + inc, "image/jpegxxxxxx", 6) == 0) {
                    image->jpeg = 1;
                } else {
                    image->jpeg = 0;
                }
                simd_skip_passed_char(data + inc, &inc, '"');
                continue;
            } else if (simd_strcmp_short(data + inc, "bufferViewxxxxxx", 6) == 0) {
                image->buffer_view = gltf_ascii_to_int(data + inc, &inc);
                continue;
            }
        }
        image->stride = align(sizeof(Gltf_Image) + uri_len, 8);
    }
    *offset += inc;
    *image_count = count;
    return images;
}

// `Materials
Gltf_Material* gltf_parse_materials(const char *data, u64 *offset, int *material_count) {
    Gltf_Material *materials = (Gltf_Material*)malloc_t(0, 8);
    Gltf_Material *material;

    u64 inc = 0;
    int count = 0;
    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        material = (Gltf_Material*)malloc_t(sizeof(Gltf_Material), 8);
        *material = {}; // make sure defaults are properly initialized
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++;
            if (simd_strcmp_long(data + inc, "pbrMetallicRoughnessxxxxxxxxxxxx", 12) == 0) {
                simd_skip_passed_char(data + inc, &inc, '"');
                while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
                    inc++;
                    if (simd_strcmp_short(data + inc, "baseColorFactorx", 1) == 0) {
                        gltf_parse_float_array(data + inc, &inc, &material->base_color_factor[0]);
                        continue;
                    } else if (simd_strcmp_short(data + inc, "metallicFactorxx", 2) == 0) {
                        material->metallic_factor = gltf_ascii_to_float(data + inc, &inc);
                        continue;
                    } else if (simd_strcmp_short(data + inc, "roughnessFactorx", 1) == 0) {
                        material->roughness_factor = gltf_ascii_to_float(data + inc, &inc);
                        continue;
                    } else if (simd_strcmp_short(data + inc, "baseColorTexture", 0) == 0) {
                        simd_skip_passed_char(data + inc, &inc, '"');
                        gltf_parse_texture_info(data + inc, &inc, &material->base_color_texture_index,
                                                &material->base_color_tex_coord, NULL, NULL);
                        continue;
                    } else if (simd_strcmp_long(data + inc, "metallicRoughnessTexturexxxxxxxx", 8) == 0) {
                        simd_skip_passed_char(data + inc, &inc, '"');
                        gltf_parse_texture_info(data + inc, &inc, &material->metallic_roughness_texture_index,
                                                &material->metallic_roughness_tex_coord, NULL, NULL);
                        continue;
                    }
                }
                inc++; // go beyond closing curly
                continue;
            } else if (simd_strcmp_short(data + inc, "normalTexturexxx", 3) == 0) {
                simd_skip_passed_char(data + inc, &inc, '"');
                gltf_parse_texture_info(data + inc, &inc, &material->normal_texture_index, &material->normal_tex_coord,
                                        &material->normal_scale, NULL);
                continue;
            } else if (simd_strcmp_short(data + inc, "occlusionTexture", 0) == 0) {
                simd_skip_passed_char(data + inc, &inc, '"');
                gltf_parse_texture_info(data + inc, &inc, &material->occlusion_texture_index, &material->occlusion_tex_coord,
                                        NULL, &material->occlusion_strength);
                continue;
            } else if (simd_strcmp_short(data + inc, "emissiveFactorxx", 2) == 0) {
                gltf_parse_float_array(data + inc, &inc, &material->emissive_factor[0]);
                continue;
            } else if (simd_strcmp_short(data + inc, "emissiveTexturex", 1) == 0) {
                simd_skip_passed_char(data + inc, &inc, '"');
                gltf_parse_texture_info(data + inc, &inc, &material->emissive_texture_index,
                                        &material->emissive_tex_coord, NULL, NULL);
                continue;
            } else if (simd_strcmp_short(data + inc, "alphaModexxxxxxx", 7) == 0) {
                simd_skip_passed_char_count(data + inc, '"', 2, &inc);
                if (simd_strcmp_short(data + inc, "OPAQUExxxxxxxxxx", 10) == 0) {
                    material->alpha_mode = GLTF_ALPHA_MODE_OPAQUE;
                } else if (simd_strcmp_short(data + inc, "MASKxxxxxxxxxxxx", 12) == 0) {
                    material->alpha_mode = GLTF_ALPHA_MODE_MASK;
                } else if (simd_strcmp_short(data + inc, "BLENDxxxxxxxxxxxx", 11) == 0) {
                    material->alpha_mode = GLTF_ALPHA_MODE_BLEND;
                }
                simd_skip_passed_char(data + inc, &inc, '"');
                continue;
            } else if (simd_strcmp_short(data + inc, "alphaCutoffxxxxx", 5) == 0) {
                material->alpha_cutoff = gltf_ascii_to_float(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "doubleSidedxxxxx", 5) == 0) {
                simd_skip_passed_char(data + inc, &inc, ':');
                simd_skip_whitespace(data + inc, &inc);
                if (simd_strcmp_short(data + inc, "truexxxxxxxxxxxx", 12) == 0) {
                    material->double_sided = 1;
                    continue;
                } else {
                    material->double_sided = 0;
                    continue;
                }
            }
        }
        material->stride = align(sizeof(Gltf_Material), 8);
    }

    *offset += inc;
    *material_count = count;
    return materials;
}
void gltf_parse_texture_info(const char *data, u64 *offset, int *index, int *tex_coord, float *scale, float *strength) {
    u64 inc = 0;
    while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
        inc++;
        if (simd_strcmp_short(data + inc, "indexxxxxxxxxxxx", 11) == 0) {
            *index = gltf_ascii_to_int(data + inc, &inc);
            continue;
        } else if (simd_strcmp_short(data + inc, "texCoordxxxxxxxx", 8) == 0) {
            *tex_coord = gltf_ascii_to_int(data + inc, &inc);
            continue;
        } else if (simd_strcmp_short(data + inc, "scalexxxxxxxxxxx", 11) == 0) {
            *scale = gltf_ascii_to_float(data + inc, &inc);
            continue;
        } else if (simd_strcmp_short(data + inc, "strengthxxxxxxxx", 8) == 0) {
            *strength = gltf_ascii_to_float(data + inc, &inc);
            continue;
        }
    }
    *offset += inc + 1; // +1 go beyond closing curly
}

// `Meshes
Gltf_Mesh* gltf_parse_meshes(const char *data, u64 *offset, int *mesh_count) {
    Gltf_Mesh *meshes = (Gltf_Mesh*)malloc_t(0, 8);
    Gltf_Mesh *mesh;

    u64 inc = 0;
    int count = 0;
    u64 mark;
    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        mark = get_mark_temp();
        mesh = (Gltf_Mesh*)malloc_t(sizeof(Gltf_Mesh), 8);
        *mesh = {};
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // step into key
            if (simd_strcmp_short(data + inc, "primitivesxxxxxx", 6) == 0) {
                mesh->primitives = gltf_parse_mesh_primitives(data + inc, &inc, &mesh->primitive_count);
                continue;
            } else if (simd_strcmp_short(data + inc, "weightsxxxxxxxxx", 9) == 0) {
                simd_skip_to_char(data + inc, &inc, '[');
                mesh->weight_count = simd_get_ascii_array_len(data + inc);
                // @MemAlign careful with this alignment (the 4 I mean)
                // Aligning to 4 means I can align the entire stride later, rather than its pieces
                mesh->weights = (float*)malloc_t(sizeof(float) * mesh->weight_count, 4);
                gltf_parse_float_array(data + inc, &inc, mesh->weights);
                continue;
            }
        }
        mesh->stride = align(get_mark_temp() - mark, 8);
    }
    *offset += inc;
    *mesh_count = count;
    return meshes;
}
Gltf_Mesh_Primitive* gltf_parse_mesh_primitives(const char *data, u64 *offset, int *primitive_count) {
    Gltf_Mesh_Primitive *primitives = (Gltf_Mesh_Primitive*)malloc_t(0, 8);
    Gltf_Mesh_Primitive *primitive;
    Gltf_Morph_Target *target;

    u64 inc = 0;
    u64 mark;
    u64 target_mark;
    int count = 0;
    int target_count;
    int mode;
    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        mark = get_mark_temp();
        primitive = (Gltf_Mesh_Primitive*)malloc_t(sizeof(Gltf_Mesh_Primitive), 8);
        *primitive = {};
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // step into key
            if (simd_strcmp_short(data + inc, "indicesxxxxxxxxx", 9) == 0) {
                primitive->indices = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "materialxxxxxxxx", 8) == 0) {
                primitive->material = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "modexxxxxxxxxxxx", 12) == 0) {
                mode = gltf_ascii_to_int(data + inc, &inc);
                switch(mode) {
                case 0:
                    primitive->topology = GLTF_PRIMITIVE_TOPOLOGY_POINT_LIST;
                    break;
                case 1:
                    primitive->topology = GLTF_PRIMITIVE_TOPOLOGY_LINE_LIST;
                    break;
                case 2:
                    ASSERT(false, "Vulkan does not seem to support this topology type");
                    primitive->topology = GLTF_PRIMITIVE_TOPOLOGY_LINE_LIST;
                    break;
                case 3:
                    primitive->topology = GLTF_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                    break;
                case 4:
                    primitive->topology = GLTF_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
                    break;
                case 5:
                    primitive->topology = GLTF_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                    break;
                case 6:
                    primitive->topology = GLTF_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
                    break;
                }
                continue;
            } else if (simd_strcmp_short(data + inc, "targetsxxxxxxxxx", 9) == 0) {
                primitive->targets = (Gltf_Morph_Target*)malloc_t(0, 8);
                target_count = 0;
                while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
                    target_count++;

                    target_mark = get_mark_temp();
                    target =
                        (Gltf_Morph_Target*)malloc_t(sizeof(Gltf_Morph_Target), 8);
                    target->attributes =
                        gltf_parse_mesh_attributes(data + inc, &inc, &target->attribute_count, true, NULL, NULL, NULL, NULL);

                    target->stride = align(get_mark_temp() - target_mark, 8);
                }
                primitive->target_count = target_count;
                continue;
            } else if (simd_strcmp_short(data + inc, "attributesxxxxxx", 6) == 0) {
                simd_skip_passed_char(data + inc, &inc, '{');
                primitive->extra_attributes = gltf_parse_mesh_attributes(data + inc, &inc, &primitive->extra_attribute_count, false,
                &primitive->position,
                &primitive->tangent,
                &primitive->normal,
                &primitive->tex_coord_0);
                continue;
            }
        }
        primitive->stride = align(get_mark_temp() - mark, 8);
    }
    inc++; // go beyond closing primitives square brace
    *offset += inc;
    *primitive_count = count;
    return primitives;
}
Gltf_Mesh_Attribute* gltf_parse_mesh_attributes(const char *data, u64 *offset, int *attribute_count, bool targets /* HACK */, int *position, int *tangent, int *normal, int *tex_coord_0) {
    Gltf_Mesh_Attribute *attributes = (Gltf_Mesh_Attribute*)malloc_t(0, 4);
    Gltf_Mesh_Attribute *attribute;

    u64 inc = 0;
    int count = 0;
    int n;
    while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
        inc++; // step into key
        // aligning to 4 allows for regular indexing
        if      (simd_strcmp_short(data + inc, "NORMALxxxxxxxxxx", 10) == 0) {
            if (targets) {
                attribute = (Gltf_Mesh_Attribute*)malloc_t(sizeof(Gltf_Mesh_Attribute), 4);
                attribute->type           = GLTF_MESH_ATTRIBUTE_TYPE_NORMAL;
                attribute->accessor_index = gltf_ascii_to_int(data + inc, &inc);
                count++;
            } else {
                *normal = gltf_ascii_to_int(data + inc, &inc);
            }
            continue;
        }
        else if (simd_strcmp_short(data + inc, "POSITIONxxxxxxxx",  8) == 0) {
            if (targets) {
                attribute = (Gltf_Mesh_Attribute*)malloc_t(sizeof(Gltf_Mesh_Attribute), 4);
                attribute->type = GLTF_MESH_ATTRIBUTE_TYPE_POSITION;
                attribute->accessor_index = gltf_ascii_to_int(data + inc, &inc);
                count++;
            } else {
                *position = gltf_ascii_to_int(data + inc, &inc);
            }
            continue;
        }
        else if (simd_strcmp_short(data + inc, "TANGENTxxxxxxxxx",  9) == 0) {
            if (targets) {
                attribute = (Gltf_Mesh_Attribute*)malloc_t(sizeof(Gltf_Mesh_Attribute), 4);
                attribute->type = GLTF_MESH_ATTRIBUTE_TYPE_TANGENT;
                attribute->accessor_index = gltf_ascii_to_int(data + inc, &inc);
                count++;
            } else {
                *tangent = gltf_ascii_to_int(data + inc, &inc);
            }
            continue;
        }
        else if (simd_strcmp_short(data + inc, "TEXCOORDxxxxxxxx",  8) == 0) {
            n = gltf_ascii_to_int(data + inc, &inc);
            if (n != 0 || targets) {
                attribute = (Gltf_Mesh_Attribute*)malloc_t(sizeof(Gltf_Mesh_Attribute), 4);
                attribute->type = GLTF_MESH_ATTRIBUTE_TYPE_TEXCOORD;
                attribute->n    = n;
                attribute->accessor_index = gltf_ascii_to_int(data + inc, &inc);
                count++;
            } else if (n == 0 && !targets) {
                *tex_coord_0 = gltf_ascii_to_int(data + inc, &inc);
            }
            continue;
        }
        else if (simd_strcmp_short(data + inc, "COLORxxxxxxxxxxx", 11) == 0) {
            attribute = (Gltf_Mesh_Attribute*)malloc_t(sizeof(Gltf_Mesh_Attribute), 4);
            attribute->type = GLTF_MESH_ATTRIBUTE_TYPE_COLOR;
            attribute->n    = gltf_ascii_to_int(data + inc, &inc);
            attribute->accessor_index = gltf_ascii_to_int(data + inc, &inc);
            count++;
            continue;
        }
        else if (simd_strcmp_short(data + inc, "JOINTSxxxxxxxxxx", 10) == 0) {
            attribute = (Gltf_Mesh_Attribute*)malloc_t(sizeof(Gltf_Mesh_Attribute), 4);
            attribute->type = GLTF_MESH_ATTRIBUTE_TYPE_JOINTS;
            attribute->n    = gltf_ascii_to_int(data + inc, &inc);
            attribute->accessor_index = gltf_ascii_to_int(data + inc, &inc);
            count++;
            continue;
        }
        else if (simd_strcmp_short(data + inc, "WEIGHTSxxxxxxxxx",  9) == 0) {
            attribute = (Gltf_Mesh_Attribute*)malloc_t(sizeof(Gltf_Mesh_Attribute), 4);
            attribute->type = GLTF_MESH_ATTRIBUTE_TYPE_WEIGHTS;
            attribute->n    = gltf_ascii_to_int(data + inc, &inc);
            attribute->accessor_index = gltf_ascii_to_int(data + inc, &inc);
            count++;
            continue;
        }
    }
    inc++; // go passed closing curly on attributes

    *offset += inc;
    *attribute_count = count;
    return attributes;
}

// `Nodes
Gltf_Node* gltf_parse_nodes(const char *data, u64 *offset, int *node_count) {
    Gltf_Node *nodes = (Gltf_Node*)malloc_t(0, 8);
    Gltf_Node *node;

    u64 inc = 0;
    int count = 0;
    u64 mark;
    float temp_array[4];
    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        mark = get_mark_temp();
        node = (Gltf_Node*)malloc_t(sizeof(Gltf_Node), 8);
        *node = {};
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // step into key
            if (simd_strcmp_short(data + inc, "cameraxxxxxxxxxx", 10) == 0) {
                node->camera = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "skinxxxxxxxxxxxx", 12) == 0) {
                node->skin = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "meshxxxxxxxxxxxx", 12) == 0) {
                node->mesh = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "matrixxxxxxxxxxx", 10) == 0) {
                node->matrix = gltf_ascii_to_mat4(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "rotationxxxxxxxx", 8) == 0) {
                gltf_parse_float_array(data + inc, &inc, temp_array);
                node->trs.rotation = {temp_array[0], temp_array[1], temp_array[2], temp_array[3]};
                continue;
            } else if (simd_strcmp_short(data + inc, "scalexxxxxxxxxxx", 11) == 0) {
                gltf_parse_float_array(data + inc, &inc, temp_array);
                node->trs.scale = {temp_array[0], temp_array[1], temp_array[2]};
                continue;
            } else if (simd_strcmp_short(data + inc, "translationxxxxx", 5) == 0) {
                gltf_parse_float_array(data + inc, &inc, temp_array);
                node->trs.translation = {temp_array[0], temp_array[1], temp_array[2]};
                continue;
            } else if (simd_strcmp_short(data + inc, "childrenxxxxxxxx", 8) == 0) {
                node->child_count = simd_get_ascii_array_len(data + inc);
                node->children = (int*)malloc_t(sizeof(int) * node->child_count, 4);
                gltf_parse_int_array(data + inc, &inc, node->children);
                continue;
            } else if (simd_strcmp_short(data + inc, "weightsxxxxxxxxx", 9) == 0) {
                node->weight_count = simd_get_ascii_array_len(data + inc);
                node->weights = (float*)malloc_t(sizeof(float) * node->weight_count, 4);
                gltf_parse_float_array(data + inc, &inc, node->weights);
                continue;
            }
        }
        node->stride = align(get_mark_temp() - mark, 8);
    }
    *offset += inc;
    *node_count = count;
    return nodes;
}

Gltf_Sampler* gltf_parse_samplers(const char *data, u64 *offset, int *sampler_count) {
    // @MemAlign being dangerous with a 4 align...
    Gltf_Sampler *samplers = (Gltf_Sampler*)malloc_t(0, 4);
    Gltf_Sampler *sampler;

    u64 inc = 0;
    int count = 0;
    int temp_int;
    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        sampler = (Gltf_Sampler*)malloc_t(sizeof(Gltf_Sampler), 4);
        *sampler = {};
        sampler->stride = sizeof(Gltf_Sampler);
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // step into key
            if (simd_strcmp_short(data + inc, "magFilterxxxxxxx", 7) == 0) {
                temp_int = gltf_ascii_to_int(data + inc, &inc);
                switch (temp_int) {
                case 9728:
                    sampler->mag_filter = GLTF_SAMPLER_FILTER_NEAREST;
                    break;
                case 9729:
                    sampler->mag_filter = GLTF_SAMPLER_FILTER_LINEAR;
                    break;
                default:
                    ASSERT(false, "This is not a valid filter setting");
                }
            } else if (simd_strcmp_short(data + inc, "minFilterxxxxxxx", 7) == 0) {
                temp_int = gltf_ascii_to_int(data + inc, &inc);
                switch (temp_int) {
                case 9728:
                    sampler->min_filter = GLTF_SAMPLER_FILTER_NEAREST;
                    break;
                case 9729:
                    sampler->min_filter = GLTF_SAMPLER_FILTER_LINEAR;
                    break;
                case 9984:
                    sampler->min_filter = GLTF_SAMPLER_FILTER_NEAREST;
                    break;
                case 9985:
                    sampler->min_filter = GLTF_SAMPLER_FILTER_NEAREST;
                    break;
                case 9986:
                    sampler->min_filter = GLTF_SAMPLER_FILTER_LINEAR;
                    break;
                case 9987:
                    sampler->min_filter = GLTF_SAMPLER_FILTER_LINEAR;
                    break;
                default:
                    ASSERT(false, "This is not a valid filter setting");
                }
            }  else if (simd_strcmp_short(data + inc, "wrapSxxxxxxxxxxx", 11) == 0) {
                temp_int = gltf_ascii_to_int(data + inc, &inc);
                switch (temp_int) {
                case 10497:
                    sampler->wrap_u = GLTF_SAMPLER_ADDRESS_MODE_REPEAT;
                    break;
                case 33648:
                    sampler->wrap_u = GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                    break;
                case 33071:
                    sampler->wrap_u = GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    break;
                default:
                    ASSERT(false, "This is not a valid wrap setting");
                }
            }  else if (simd_strcmp_short(data + inc, "wrapTxxxxxxxxxxx", 11) == 0) {
                temp_int = gltf_ascii_to_int(data + inc, &inc);
                switch (temp_int) {
                case 10497:
                    sampler->wrap_v = GLTF_SAMPLER_ADDRESS_MODE_REPEAT;
                    break;
                case 33648:
                    sampler->wrap_v = GLTF_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                    break;
                case 33071:
                    sampler->wrap_v = GLTF_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                    break;
                default:
                    ASSERT(false, "This is not a valid wrap setting");
                }
            }
        }
    }
    *offset += inc;
    *sampler_count = count;
    return samplers;
}

Gltf_Scene* gltf_parse_scenes(const char *data, u64 *offset, int *scene_count) {
    Gltf_Scene *scenes = (Gltf_Scene*)malloc_t(0, 8);
    Gltf_Scene *scene;

    u64 inc = 0;
    int count = 0;
    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        scene = (Gltf_Scene*)malloc_t(sizeof(Gltf_Scene), 8);
        *scene = {};
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // step into key
            if (simd_strcmp_short(data + inc, "nodesxxxxxxxxxxx", 11) == 0) {
                // It annoys me that sometimes I have to do look aheads like this. Maybe
                // I should change the instances of this pattern to just temp allocate for
                // every array elem, even though they are so small (sizeof int or float); The
                // temp allocator would just be super cache hot. I guess it just matters how big
                // the look ahead in the file is. Tbh for this node array it can probably be
                // really large... (Idk how big nodes get, whether scenes are made up of lots of small
                // nodes, or a couple big ones. Tbf these are only root nodes so maybe the list isnt that long??)
                scene->node_count = simd_get_ascii_array_len(data + inc);
                scene->nodes = (int*)malloc_t(sizeof(int) * scene->node_count, 4);
                gltf_parse_int_array(data + inc, &inc, scene->nodes);
                continue;
            }
        }
        scene->stride = align(sizeof(Gltf_Scene) + sizeof(int) * scene->node_count, 8);
    }
    *offset += inc;
    *scene_count = count;
    return scenes;
}

Gltf_Skin* gltf_parse_skins(const char *data, u64 *offset, int *skin_count) {
    Gltf_Skin *skins = (Gltf_Skin*)malloc_t(0, 8);
    Gltf_Skin *skin;

    u64 inc = 0;
    int count = 0;
    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        skin = (Gltf_Skin*)malloc_t(sizeof(Gltf_Skin), 8);
        *skin = {};
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // step into key
            if (simd_strcmp_long(data + inc, "inverseBindMatricesxxxxxxxxxxxxxxxxxxxxx", 13) == 0) {
                skin->inverse_bind_matrices = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "skeletonxxxxxxxx", 8) == 0) {
                skin->skeleton = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "jointsxxxxxxxxxx", 10) == 0) {
                skin->joint_count = simd_get_ascii_array_len(data + inc);
                skin->joints = (int*)malloc_t(sizeof(int) * skin->joint_count, 4);
                gltf_parse_int_array(data + inc, &inc, skin->joints);
                continue;
            }
        }
        skin->stride = align(sizeof(Gltf_Skin) + skin->joint_count * sizeof(int), 8);
    }
    *offset += inc;
    *skin_count = count;
    return skins;
}

Gltf_Texture* gltf_parse_textures(const char *data, u64 *offset, int *texture_count) {
    Gltf_Texture *textures = (Gltf_Texture*)malloc_t(0, 4);
    Gltf_Texture *texture;

    u64 inc = 0;
    int count = 0;
    while(simd_find_char_interrupted(data + inc, '{', ']', &inc)) {
        count++;
        texture = (Gltf_Texture*)malloc_t(sizeof(Gltf_Texture), 4);
        *texture = {};
        texture->stride = sizeof(Gltf_Texture);
        while(simd_find_char_interrupted(data + inc, '"', '}', &inc)) {
            inc++; // step into key
            if (simd_strcmp_short(data + inc, "samplerxxxxxxxxx", 9) == 0) {
                texture->sampler = gltf_ascii_to_int(data + inc, &inc);
                continue;
            } else if (simd_strcmp_short(data + inc, "sourcexxxxxxxxxx", 10) == 0) {
                texture->source_image = gltf_ascii_to_int(data + inc, &inc);
                continue;
            }
        }
    }
    *offset += inc;
    *texture_count = count;
    return textures;
}

Gltf_Accessor* gltf_accessor_by_index(Gltf *gltf, int i) {
    return (Gltf_Accessor*)((u8*)gltf->accessors + gltf->accessor_count[i]);
}
Gltf_Animation* gltf_animation_by_index(Gltf *gltf, int i) {
    return (Gltf_Animation*)((u8*)gltf->animations + gltf->animation_count[i]);
}
Gltf_Buffer* gltf_buffer_by_index(Gltf *gltf, int i) {
    return (Gltf_Buffer*)((u8*)gltf->buffers + gltf->buffer_count[i]);
}
Gltf_Buffer_View* gltf_buffer_view_by_index(Gltf *gltf, int i) {
    return (Gltf_Buffer_View*)((u8*)gltf->buffer_views + gltf->buffer_view_count[i]);
}
Gltf_Camera* gltf_camera_by_index(Gltf *gltf, int i) {
    return (Gltf_Camera*)((u8*)gltf->cameras + gltf->camera_count[i]);
}
Gltf_Image* gltf_image_by_index(Gltf *gltf, int i) {
    return (Gltf_Image*)((u8*)gltf->images + gltf->image_count[i]);
}
Gltf_Material* gltf_material_by_index(Gltf *gltf, int i) {
    return (Gltf_Material*)((u8*)gltf->materials + gltf->material_count[i]);
}
Gltf_Mesh* gltf_mesh_by_index(Gltf *gltf, int i) {
    return (Gltf_Mesh*)((u8*)gltf->meshes + gltf->mesh_count[i]);
}
Gltf_Node* gltf_node_by_index(Gltf *gltf, int i) {
    return (Gltf_Node*)((u8*)gltf->nodes + gltf->node_count[i]);
}
Gltf_Sampler* gltf_sampler_by_index(Gltf *gltf, int i) {
    return (Gltf_Sampler*)((u8*)gltf->samplers + gltf->sampler_count[i]);
}
Gltf_Scene* gltf_scene_by_index(Gltf *gltf, int i) {
    return (Gltf_Scene*)((u8*)gltf->scenes + gltf->scene_count[i]);
}
Gltf_Skin* gltf_skin_by_index(Gltf *gltf, int i) {
    return (Gltf_Skin*)((u8*)gltf->skins + gltf->skin_count[i]);
}
Gltf_Texture* gltf_texture_by_index(Gltf *gltf, int i) {
    return (Gltf_Texture*)((u8*)gltf->textures + gltf->texture_count[i]);
}

int gltf_accessor_get_count(Gltf *gltf) {
    return gltf->accessor_count[-1];
}
int gltf_animation_get_count(Gltf *gltf) {
    return gltf->animation_count[-1];
}
int gltf_buffer_get_count(Gltf *gltf) {
    return gltf->buffer_count[-1];
}
int gltf_buffer_view_get_count(Gltf *gltf) {
    return gltf->buffer_view_count[-1];
}
int gltf_camera_get_count(Gltf *gltf) {
    return gltf->camera_count[-1];
}
int gltf_image_get_count(Gltf *gltf) {
    return gltf->image_count[-1];
}
int gltf_material_get_count(Gltf *gltf) {
    return gltf->material_count[-1];
}
int gltf_mesh_get_count(Gltf *gltf) {
    return gltf->mesh_count[-1];
}
int gltf_node_get_count(Gltf *gltf) {
    return gltf->node_count[-1];
}
int gltf_sampler_get_count(Gltf *gltf) {
    return gltf->sampler_count[-1];
}
int gltf_scene_get_count(Gltf *gltf) {
    return gltf->scene_count[-1];
}
int gltf_skin_get_count(Gltf *gltf) {
    return gltf->skin_count[-1];
}
int gltf_texture_get_count(Gltf *gltf) {
    return gltf->texture_count[-1];
}

#if TEST
static void test_accessors(Gltf_Accessor *accessor);
static void test_animations(Gltf_Animation *animation);
static void test_buffers(Gltf_Buffer *buffers);
static void test_buffer_views(Gltf_Buffer_View *buffer_views);
static void test_cameras(Gltf_Camera *cameras);
static void test_images(Gltf_Image *images);
static void test_materials(Gltf_Material *materials);
static void test_meshes(Gltf_Mesh *meshes);
static void test_nodes(Gltf_Node *nodes);
static void test_samplers(Gltf_Sampler *samplers);
static void test_scenes(Gltf_Scene *scenes);
static void test_skins(Gltf_Skin *skins);
static void test_textures(Gltf_Texture *textures);

void test_gltf() {
    Gltf gltf = parse_gltf("test/test_gltf.gltf");

    test_accessors(gltf.accessors);
    ASSERT(gltf.accessor_count[-1] == 3, "Incorrect Accessor Count");
    test_animations(gltf.animations);
    ASSERT(gltf.animation_count[-1] == 4, "Incorrect Animation Count");
    test_buffers(gltf.buffers);
    ASSERT(gltf.buffer_count[-1] == 5, "Incorrect Buffer Count");
    test_buffer_views(gltf.buffer_views);
    ASSERT(gltf.buffer_view_count[-1] == 4, "Incorrect Buffer View Count");
    test_cameras(gltf.cameras);
    ASSERT(gltf.camera_count[-1] == 3, "Incorrect Camera View Count");
    test_images(gltf.images);
    ASSERT(gltf.image_count[-1] == 3, "Incorrect Image Count");
    test_materials(gltf.materials);
    ASSERT(gltf.material_count[-1] == 2, "Incorrect Material Count");
    test_meshes(gltf.meshes);
    ASSERT(gltf.mesh_count[-1] == 2, "Incorrect Mesh Count");
    test_nodes(gltf.nodes);
    ASSERT(gltf.node_count[-1] == 7, "Incorrect Node Count");
    test_samplers(gltf.samplers);
    ASSERT(gltf.sampler_count[-1] == 3, "Incorrect Sampler Count");
    test_scenes(gltf.scenes);
    ASSERT(gltf.scene_count[-1] == 3, "Incorrect Scene Count");
    test_skins(gltf.skins);
    ASSERT(gltf.skin_count[-1] == 4, "Incorrect Skin Count");
    test_textures(gltf.textures);
    ASSERT(gltf.texture_count[-1] == 4, "Incorrect Texture Count");

    BEGIN_TEST_MODULE("Gltf_Indexing", true, false);

    Gltf_Accessor *accessor = gltf_accessor_by_index(&gltf, 2);
    TEST_EQ("accessor[2].format", accessor->format, GLTF_ACCESSOR_FORMAT_VEC3_U32, false);
    TEST_EQ("accessor[2].buffer_view", accessor->buffer_view, 3, false);
    TEST_EQ("accessor[2].byte_offset", accessor->byte_offset, (u64)300, false);
    TEST_EQ("accessor[2].count", accessor->count, 12001, false);

    TEST_EQ("accessor[2].sparse_count", accessor->sparse_count, 10, false);
    TEST_EQ("accessor[2].indices_comp_type", (int)accessor->indices_component_type, 5123, false);
    TEST_EQ("accessor[2].indices_buffer_view", accessor->indices_buffer_view, 7, false);
    TEST_EQ("accessor[2].values_buffer_view", accessor->values_buffer_view, 4, false);
    TEST_EQ("accessor[2].indices_byte_offset", accessor->indices_byte_offset, (u64)8888, false);
    TEST_EQ("accessor[2].values_byte_offset", accessor->values_byte_offset, (u64)9999, false);

    Gltf_Animation *animation = gltf_animation_by_index(&gltf, 3);
    TEST_EQ("animation[3].channels[0].sampler", animation->channels    [0].sampler,     24, false);
    TEST_EQ("animation[3].channels[0].target_node", animation->channels[0].target_node, 27, false);
    TEST_EQ("animation[3].channels[0].path", animation->channels       [0].path, GLTF_ANIMATION_PATH_ROTATION, false);
    TEST_EQ("animation[3].channels[1].sampler", animation->channels    [1].sampler,     31, false);
    TEST_EQ("animation[3].channels[1].target_node", animation->channels[1].target_node, 36, false);
    TEST_EQ("animation[3].channels[1].path", animation->channels       [1].path, GLTF_ANIMATION_PATH_WEIGHTS, false);
    TEST_EQ("animation[3].samplers[0].input",  animation->samplers[0].input,  999, false);
    TEST_EQ("animation[3].samplers[0].output", animation->samplers[0].output, 753, false);
    TEST_EQ("animation[3].samplers[0].interp", animation->samplers[0].interp, GLTF_ANIMATION_INTERP_LINEAR, false);
    TEST_EQ("animation[3].samplers[1].input",  animation->samplers[1].input,  4, false);
    TEST_EQ("animation[3].samplers[1].output", animation->samplers[1].output, 6, false);
    TEST_EQ("animation[3].samplers[1].interp", animation->samplers[1].interp, GLTF_ANIMATION_INTERP_LINEAR, false);

    Gltf_Material *material = gltf_material_by_index(&gltf, 1);
    TEST_FEQ("materials[1].metallic_factor",      material->metallic_factor    , 5.0   , false);
    TEST_FEQ("materials[1].roughness_factor",     material->roughness_factor   , 6.0   , false);
    TEST_FEQ("materials[1].normal_scale",         material->normal_scale       , 1.0   , false);
    TEST_FEQ("materials[1].occlusion_strength",   material->occlusion_strength , 0.679 , false);

    TEST_FEQ("materials[1].base_color_factor[0]", material->base_color_factor[0] ,  2.5, false);
    TEST_FEQ("materials[1].base_color_factor[1]", material->base_color_factor[1] ,  4.5, false);
    TEST_FEQ("materials[1].base_color_factor[2]", material->base_color_factor[2] ,  2.5, false);
    TEST_FEQ("materials[1].base_color_factor[3]", material->base_color_factor[3] ,  1.0, false);

    TEST_FEQ("materials[1].emissive_factor[0]", material->emissive_factor[0] , 11.2, false);
    TEST_FEQ("materials[1].emissive_factor[1]", material->emissive_factor[1] ,  0.1, false);
    TEST_FEQ("materials[1].emissive_factor[2]", material->emissive_factor[2] ,  0.0, false);

    Gltf_Node *node = gltf_node_by_index(&gltf, 0);
    TEST_FEQ("nodes[0].matrix[0]",  node->matrix.row0.x, -0.99975   , false);
    TEST_FEQ("nodes[0].matrix[1]",  node->matrix.row0.y, -0.00679829, false);
    TEST_FEQ("nodes[0].matrix[2]",  node->matrix.row0.z, 0.0213218  , false);
    TEST_FEQ("nodes[0].matrix[3]",  node->matrix.row0.w, 0          , false);
    TEST_FEQ("nodes[0].matrix[4]",  node->matrix.row1.x, 0.00167596 , false);
    TEST_FEQ("nodes[0].matrix[5]",  node->matrix.row1.y, 0.927325   , false);
    TEST_FEQ("nodes[0].matrix[6]",  node->matrix.row1.z, 0.374254   , false);
    TEST_FEQ("nodes[0].matrix[7]",  node->matrix.row1.w, 0          , false);
    TEST_FEQ("nodes[0].matrix[8]",  node->matrix.row2.x, -0.0223165 , false);
    TEST_FEQ("nodes[0].matrix[9]",  node->matrix.row2.y, 0.374196   , false);
    TEST_FEQ("nodes[0].matrix[10]", node->matrix.row2.z, -0.927081  , false);
    TEST_FEQ("nodes[0].matrix[11]", node->matrix.row2.w, 0          , false);
    TEST_FEQ("nodes[0].matrix[12]", node->matrix.row3.x, -0.0115543 , false);
    TEST_FEQ("nodes[0].matrix[13]", node->matrix.row3.y, 0.194711   , false);
    TEST_FEQ("nodes[0].matrix[14]", node->matrix.row3.z, -0.478297  , false);
    TEST_FEQ("nodes[0].matrix[15]", node->matrix.row3.w, 1          , false);

    TEST_FEQ("nodes[0].weights[0]", node->weights[0], 0.5, false);
    TEST_FEQ("nodes[0].weights[1]", node->weights[1], 0.6, false);
    TEST_FEQ("nodes[0].weights[2]", node->weights[2], 0.7, false);
    TEST_FEQ("nodes[0].weights[3]", node->weights[3], 0.8, false);

    END_TEST_MODULE();
}

static void test_accessors(Gltf_Accessor *accessor) {
    BEGIN_TEST_MODULE("Gltf_Accessor", true, false);

    TEST_EQ("accessor[0].format", accessor->format, GLTF_ACCESSOR_FORMAT_SCALAR_U16, false);
    TEST_EQ("accessor[0].buffer_view", accessor->buffer_view, 1, false);
    TEST_EQ("accessor[0].byte_offset", accessor->byte_offset, (u64)100, false);
    TEST_EQ("accessor[0].count", accessor->count, 12636, false);
    TEST_EQ("accessor[0].max[0]", accessor->max[0], 4212, false);
    TEST_EQ("accessor[0].min[0]", accessor->min[0], 0, false);

    accessor = (Gltf_Accessor*)((u8*)accessor + accessor->stride);
    TEST_EQ("accessor[1].format", accessor->format, GLTF_ACCESSOR_FORMAT_VEC2_FLOAT32, false);
    TEST_EQ("accessor[1].buffer_view", accessor->buffer_view, 2, false);
    TEST_EQ("accessor[1].byte_offset", accessor->byte_offset, (u64)200, false);
    TEST_EQ("accessor[1].count", accessor->count, 2399, false);

    TEST_FEQ("accessor[1].max[0]", accessor->max[0], 0.961799 , false);
    TEST_FEQ("accessor[1].max[1]", accessor->max[1], -1.6397  , false);
    TEST_FEQ("accessor[1].max[2]", accessor->max[2], 0.539252 , false);
    TEST_FEQ("accessor[1].min[0]", accessor->min[0], -0.692985, false);
    TEST_FEQ("accessor[1].min[1]", accessor->min[1], 0.0992937, false);
    TEST_FEQ("accessor[1].min[2]", accessor->min[2], -0.613282, false);

    accessor = (Gltf_Accessor*)((u8*)accessor + accessor->stride);
    TEST_EQ("accessor[2].format", accessor->format, GLTF_ACCESSOR_FORMAT_VEC3_U32, false);
    TEST_EQ("accessor[2].buffer_view", accessor->buffer_view, 3, false);
    TEST_EQ("accessor[2].byte_offset", accessor->byte_offset, (u64)300, false);
    TEST_EQ("accessor[2].count", accessor->count, 12001, false);

    TEST_EQ("accessor[2].sparse_count", accessor->sparse_count, 10, false);
    TEST_EQ("accessor[2].indices_comp_type", (int)accessor->indices_component_type, 5123, false);
    TEST_EQ("accessor[2].indices_buffer_view", accessor->indices_buffer_view, 7, false);
    TEST_EQ("accessor[2].values_buffer_view", accessor->values_buffer_view, 4, false);
    TEST_EQ("accessor[2].indices_byte_offset", accessor->indices_byte_offset, (u64)8888, false);
    TEST_EQ("accessor[2].values_byte_offset", accessor->values_byte_offset, (u64)9999, false);

    END_TEST_MODULE();
}

static void test_animations(Gltf_Animation *animation) {
    BEGIN_TEST_MODULE("Gltf_Accessor", true, false);

    TEST_EQ("animation[0].channels[0].sampler", animation->channels    [0].sampler,     0, false);
    TEST_EQ("animation[0].channels[0].target_node", animation->channels[0].target_node, 1, false);
    TEST_EQ("animation[0].channels[0].path", animation->channels       [0].path, GLTF_ANIMATION_PATH_ROTATION, false);
    TEST_EQ("animation[0].channels[1].sampler", animation->channels    [1].sampler,     1, false);
    TEST_EQ("animation[0].channels[1].target_node", animation->channels[1].target_node, 2, false);
    TEST_EQ("animation[0].channels[1].path", animation->channels       [1].path, GLTF_ANIMATION_PATH_SCALE, false);
    TEST_EQ("animation[0].channels[2].sampler", animation->channels    [2].sampler,     2, false);
    TEST_EQ("animation[0].channels[2].target_node", animation->channels[2].target_node, 3, false);
    TEST_EQ("animation[0].channels[2].path", animation->channels       [2].path, GLTF_ANIMATION_PATH_TRANSLATION,false);
    TEST_EQ("animation[0].samplers[0].input",  animation->samplers[0].input,  888, false);
    TEST_EQ("animation[0].samplers[0].output", animation->samplers[0].output, 5, false);
    TEST_EQ("animation[0].samplers[0].interp", animation->samplers[0].interp, GLTF_ANIMATION_INTERP_LINEAR, false);
    TEST_EQ("animation[0].samplers[1].input",  animation->samplers[1].input,  4, false);
    TEST_EQ("animation[0].samplers[1].output", animation->samplers[1].output, 6, false);
    TEST_EQ("animation[0].samplers[1].interp", animation->samplers[1].interp, GLTF_ANIMATION_INTERP_CUBICSPLINE, false);
    TEST_EQ("animation[0].samplers[2].input",  animation->samplers[2].input,  4, false);
    TEST_EQ("animation[0].samplers[2].output", animation->samplers[2].output, 7, false);
    TEST_EQ("animation[0].samplers[2].interp", animation->samplers[2].interp, GLTF_ANIMATION_INTERP_STEP, false);

    animation = (Gltf_Animation*)((u8*)animation + animation->stride);
    TEST_EQ("animation[1].channels[0].sampler", animation->channels    [0].sampler,     0, false);
    TEST_EQ("animation[1].channels[0].target_node", animation->channels[0].target_node, 0, false);
    TEST_EQ("animation[1].channels[0].path", animation->channels       [0].path, GLTF_ANIMATION_PATH_ROTATION, false);
    TEST_EQ("animation[1].channels[1].sampler", animation->channels    [1].sampler,     1, false);
    TEST_EQ("animation[1].channels[1].target_node", animation->channels[1].target_node, 1, false);
    TEST_EQ("animation[1].channels[1].path", animation->channels       [1].path, GLTF_ANIMATION_PATH_ROTATION, false);
    TEST_EQ("animation[1].samplers[0].input",  animation->samplers[0].input,  0, false);
    TEST_EQ("animation[1].samplers[0].output", animation->samplers[0].output, 1, false);
    TEST_EQ("animation[1].samplers[0].interp", animation->samplers[0].interp, GLTF_ANIMATION_INTERP_LINEAR, false);
    TEST_EQ("animation[1].samplers[1].input",  animation->samplers[1].input,  2, false);
    TEST_EQ("animation[1].samplers[1].output", animation->samplers[1].output, 3, false);
    TEST_EQ("animation[1].samplers[1].interp", animation->samplers[1].interp, GLTF_ANIMATION_INTERP_LINEAR, false);

    animation = (Gltf_Animation*)((u8*)animation + animation->stride);
    TEST_EQ("animation[2].channels[0].sampler", animation->channels    [0].sampler,     1000, false);
    TEST_EQ("animation[2].channels[0].target_node", animation->channels[0].target_node, 2000, false);
    TEST_EQ("animation[2].channels[0].path", animation->channels       [0].path, GLTF_ANIMATION_PATH_TRANSLATION,false);
    TEST_EQ("animation[2].channels[1].sampler", animation->channels    [1].sampler,     799, false);
    TEST_EQ("animation[2].channels[1].target_node", animation->channels[1].target_node, 899, false);
    TEST_EQ("animation[2].channels[1].path", animation->channels       [1].path, GLTF_ANIMATION_PATH_WEIGHTS, false);
    TEST_EQ("animation[2].samplers[0].input",  animation->samplers[0].input,  676, false);
    TEST_EQ("animation[2].samplers[0].output", animation->samplers[0].output, 472, false);
    TEST_EQ("animation[2].samplers[0].interp", animation->samplers[0].interp, GLTF_ANIMATION_INTERP_STEP, false);

    animation = (Gltf_Animation*)((u8*)animation + animation->stride);
    TEST_EQ("animation[3].channels[0].sampler", animation->channels    [0].sampler,     24, false);
    TEST_EQ("animation[3].channels[0].target_node", animation->channels[0].target_node, 27, false);
    TEST_EQ("animation[3].channels[0].path", animation->channels       [0].path, GLTF_ANIMATION_PATH_ROTATION, false);
    TEST_EQ("animation[3].channels[1].sampler", animation->channels    [1].sampler,     31, false);
    TEST_EQ("animation[3].channels[1].target_node", animation->channels[1].target_node, 36, false);
    TEST_EQ("animation[3].channels[1].path", animation->channels       [1].path, GLTF_ANIMATION_PATH_WEIGHTS, false);
    TEST_EQ("animation[3].samplers[0].input",  animation->samplers[0].input,  999, false);
    TEST_EQ("animation[3].samplers[0].output", animation->samplers[0].output, 753, false);
    TEST_EQ("animation[3].samplers[0].interp", animation->samplers[0].interp, GLTF_ANIMATION_INTERP_LINEAR, false);
    TEST_EQ("animation[3].samplers[1].input",  animation->samplers[1].input,  4, false);
    TEST_EQ("animation[3].samplers[1].output", animation->samplers[1].output, 6, false);
    TEST_EQ("animation[3].samplers[1].interp", animation->samplers[1].interp, GLTF_ANIMATION_INTERP_LINEAR, false);

    END_TEST_MODULE();
}
static void test_buffers(Gltf_Buffer *buffers) {
    BEGIN_TEST_MODULE("Gltf_Buffer", true, false);

    Gltf_Buffer *buffer = buffers;
       TEST_EQ("buffers[0].byteLength", buffer->byte_length, (u64)10001, false);
    TEST_STREQ("buffers[0].uri", buffer->uri, "duck1.bin", false);

    buffer = (Gltf_Buffer*)((u8*)buffer + buffer->stride);
       TEST_EQ("buffers[1].byteLength", buffer->byte_length, (u64)10002, false);
    TEST_STREQ("buffers[1].uri", buffer->uri, "duck2.bin", false);

    buffer = (Gltf_Buffer*)((u8*)buffer + buffer->stride);
       TEST_EQ("buffers[2].byteLength", buffer->byte_length, (u64)10003, false);
    TEST_STREQ("buffers[2].uri", buffer->uri,   "duck3.bin", false);

    buffer = (Gltf_Buffer*)((u8*)buffer + buffer->stride);
       TEST_EQ("buffers[3].byteLength", buffer->byte_length, (u64)10004, false);
    TEST_STREQ("buffers[3].uri", buffer->uri, "duck4.bin", false);

    buffer = (Gltf_Buffer*)((u8*)buffer + buffer->stride);
       TEST_EQ("buffers[4].byteLength", buffer->byte_length, (u64)10005, false);
    TEST_STREQ("buffers[4].uri", buffer->uri, "duck5.bin", false);

    END_TEST_MODULE();
}
static void test_buffer_views(Gltf_Buffer_View *buffer_views) {
    BEGIN_TEST_MODULE("Gltf_Buffer_Views", true, false);

    Gltf_Buffer_View *view = buffer_views;
    TEST_EQ("buffer_views[0].buffer",           view->buffer, 1, false);
    TEST_EQ("buffer_views[0].byte_offset", view->byte_offset, (u64)2, false);
    TEST_EQ("buffer_views[0].byte_length", view->byte_length, (u64)25272, false);
    TEST_EQ("buffer_views[0].byte_stride", view->byte_stride, 0, false);
    TEST_EQ("buffer_views[0].buffer_type", view->buffer_type, 34963, false);

    view = (Gltf_Buffer_View*)((u8*)view + view->stride);
    TEST_EQ("buffer_views[1].buffer",           view->buffer, 6, false);
    TEST_EQ("buffer_views[1].byte_offset", view->byte_offset, (u64)25272, false);
    TEST_EQ("buffer_views[1].byte_length", view->byte_length, (u64)76768, false);
    TEST_EQ("buffer_views[1].byte_stride", view->byte_stride, 32, false);
    TEST_EQ("buffer_views[1].buffer_type", view->buffer_type, 34962, false);

    view = (Gltf_Buffer_View*)((u8*)view + view->stride);
    TEST_EQ("buffer_views[2].buffer",           view->buffer,  9999, false);
    TEST_EQ("buffer_views[2].byte_offset", view->byte_offset,  (u64)6969, false);
    TEST_EQ("buffer_views[2].byte_length", view->byte_length,  (u64)99907654, false);
    TEST_EQ("buffer_views[2].byte_stride", view->byte_stride,  0, false);
    TEST_EQ("buffer_views[2].buffer_type", view->buffer_type,  34962, false);

    view = (Gltf_Buffer_View*)((u8*)view + view->stride);
    TEST_EQ("buffer_views[3].buffer",           view->buffer, 9, false);
    TEST_EQ("buffer_views[3].byte_offset", view->byte_offset, (u64)25272, false);
    TEST_EQ("buffer_views[3].byte_length", view->byte_length, (u64)76768, false);
    TEST_EQ("buffer_views[3].byte_stride", view->byte_stride, 32, false);
    TEST_EQ("buffer_views[3].buffer_type", view->buffer_type, 34963, false);

    END_TEST_MODULE();
}
static void test_cameras(Gltf_Camera *cameras) {
    BEGIN_TEST_MODULE("Gltf_Camera", true, false);

    float inaccuracy = 0.0000001;

    Gltf_Camera *camera = cameras;
    TEST_FEQ("cameras[0].ortho",        camera->ortho          , 0        , false);
    TEST_FEQ("cameras[0].aspect_ratio", camera->x_factor       , 1.5      , false);
    TEST_FEQ("cameras[0].yfov",         camera->y_factor       , 0.646464 , false);
    TEST_FEQ("cameras[0].zfar",         camera->zfar           , 100      , false);
    TEST_FEQ("cameras[0].znear",        camera->znear          , 0.01     , false);

    camera = (Gltf_Camera*)((u8*)       camera + camera->stride);
    TEST_FEQ("cameras[1].ortho",        camera->ortho           , 0       ,  false);
    TEST_FEQ("cameras[1].aspect_ratio", camera->x_factor        , 1.9     ,  false);
    TEST_FEQ("cameras[1].yfov",         camera->y_factor        , 0.797979,  false);
    TEST_FEQ("cameras[1].znear",        camera->znear           , 0.02    ,  false);

    camera = (Gltf_Camera*)((u8*)camera + camera->stride);
    TEST_FEQ("cameras[2].ortho", camera->ortho   , 1     , false);
    TEST_FEQ("cameras[2].xmag",  camera->x_factor, 1.822 , false);
    TEST_FEQ("cameras[2].ymag",  camera->y_factor, 0.489 , false);
    TEST_FEQ("cameras[2].znear", camera->znear   , 0.01  , false);

    END_TEST_MODULE();
}
static void test_images(Gltf_Image *images) {
    BEGIN_TEST_MODULE("Gltf_Image", true, false);

    Gltf_Image *image = images;
    TEST_STREQ("images[0].uri", image->uri, "duckCM.png", false);

    image = (Gltf_Image*)((u8*)image + image->stride);
    TEST_EQ("images[1].jpeg", image->jpeg, 1, false);
    TEST_EQ("images[1].bufferView", image->buffer_view, 14, false);
    TEST_EQ("images[1].uri", image->uri, nullptr, false);

    image = (Gltf_Image*)((u8*)image + image->stride);
    TEST_STREQ("images[2].uri", image->uri, "duck_but_better.jpeg", false);

    END_TEST_MODULE();
}
static void test_materials(Gltf_Material *materials) {
    BEGIN_TEST_MODULE("Gltf_Image", true, false);

    float inaccuracy = 0.0000001;

    Gltf_Material *material = materials;

    material = (Gltf_Material*)((u8*)material + material->stride);
    TEST_EQ("materials[1].base_color_texture_index",         material->base_color_texture_index,          3, false);
    TEST_EQ("materials[1].base_color_tex_coord",             material->base_color_tex_coord,              4, false);
    TEST_EQ("materials[1].metallic_roughness_texture_index", material->metallic_roughness_texture_index,  8, false);
    TEST_EQ("materials[1].metallic_roughness_tex_coord",     material->metallic_roughness_tex_coord,      8, false);
    TEST_EQ("materials[1].normal_texture_index",             material->normal_texture_index,             12, false);
    TEST_EQ("materials[1].normal_tex_coord",                 material->normal_tex_coord,                 11, false);
    TEST_EQ("materials[1].emissive_texture_index",           material->emissive_texture_index,            3, false);
    TEST_EQ("materials[1].emissive_tex_coord",               material->emissive_tex_coord,            56070, false);
    TEST_EQ("materials[1].occlusion_texture_index",          material->occlusion_texture_index,          79, false);
    TEST_EQ("materials[1].occlusion_tex_coord",              material->occlusion_tex_coord,            9906, false);

    TEST_FEQ("materials[1].metallic_factor",      material->metallic_factor    , 5.0   , false);
    TEST_FEQ("materials[1].roughness_factor",     material->roughness_factor   , 6.0   , false);
    TEST_FEQ("materials[1].normal_scale",         material->normal_scale       , 1.0   , false);
    TEST_FEQ("materials[1].occlusion_strength",   material->occlusion_strength , 0.679 , false);

    TEST_FEQ("materials[1].base_color_factor[0]", material->base_color_factor[0] ,  2.5, false);
    TEST_FEQ("materials[1].base_color_factor[1]", material->base_color_factor[1] ,  4.5, false);
    TEST_FEQ("materials[1].base_color_factor[2]", material->base_color_factor[2] ,  2.5, false);
    TEST_FEQ("materials[1].base_color_factor[3]", material->base_color_factor[3] ,  1.0, false);

    TEST_FEQ("materials[1].emissive_factor[0]", material->emissive_factor[0] , 11.2, false);
    TEST_FEQ("materials[1].emissive_factor[1]", material->emissive_factor[1] ,  0.1, false);
    TEST_FEQ("materials[1].emissive_factor[2]", material->emissive_factor[2] ,  0.0, false);

    END_TEST_MODULE();
}
void test_meshes(Gltf_Mesh *meshes) {
    BEGIN_TEST_MODULE("Gltf_Mesh", true, false);

    Gltf_Mesh *mesh = meshes;
    TEST_EQ("meshes[0].primitive_count", mesh->primitive_count, 2, false);
    TEST_EQ("meshes[0].weight_count",    mesh->weight_count,    2, false);

    float inaccuracy = 0.0000001;
    TEST_FEQ("meshes[0].weights[0]", mesh->weights[0] , 0  , false);
    TEST_FEQ("meshes[0].weights[1]", mesh->weights[1] , 0.5, false);

    Gltf_Mesh_Primitive *primitive = mesh->primitives;
    TEST_EQ("meshes[0].primitives[0]", primitive->indices, 21, false);
    TEST_EQ("meshes[0].primitives[0]", primitive->material, 3, false);
    //TEST_EQ("meshes[0].primitives[0]", primitive->mode,     (Gltf_Primitive_Topology)1, false);
    TEST_EQ("meshes[0].primitives[0].position", primitive->position, 22, false);
    TEST_EQ("meshes[0].primitives[0].normal", primitive->normal, 23, false);
    TEST_EQ("meshes[0].primitives[0].tangent", primitive->tangent, 24, false);
    TEST_EQ("meshes[0].primitives[0].tex_coord_0", primitive->tex_coord_0, 25, false);

    Gltf_Mesh_Attribute *attribute = primitive->extra_attributes;
    TEST_EQ("meshes[0].primitives[0].extra_attribute_count", primitive->extra_attribute_count,0, false);

    primitive = (Gltf_Mesh_Primitive*)((u8*)primitive + primitive->stride);
    TEST_EQ("meshes[0].primitives[1]", primitive->indices,  31, false);
    TEST_EQ("meshes[0].primitives[1]", primitive->material, 33, false);
    //TEST_EQ("meshes[0].primitives[1]", primitive->mode,      (Gltf_Primitive_Topology)1, false);

    TEST_EQ("meshes[0].primitives[1].position", primitive->position, 32, false);
    TEST_EQ("meshes[0].primitives[1].normal", primitive->normal, 33, false);
    TEST_EQ("meshes[0].primitives[1].tangent", primitive->tangent, 34, false);
    TEST_EQ("meshes[0].primitives[1].tex_coord_0", primitive->tex_coord_0, 35, false);

    attribute = primitive->extra_attributes;
    TEST_EQ("meshes[0].primitives[1].extra_attribute_count", primitive->extra_attribute_count,0, false);

    Gltf_Morph_Target *target = primitive->targets;
    TEST_EQ("meshes[0].primitives[1].target_count", primitive->target_count, 2, false);
    TEST_EQ("meshes[0].primitives[1].targets[0].attributes[0].accessor_index", target->attributes[0].accessor_index, 33, false);
    TEST_EQ("meshes[0].primitives[1].targets[0].attributes[0].type",           target->attributes[0].type, GLTF_MESH_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[0].primitives[1].targets[0].attributes[1].accessor_index", target->attributes[1].accessor_index, 32, false);
    TEST_EQ("meshes[0].primitives[1].targets[0].attributes[1].type",           target->attributes[1].type, GLTF_MESH_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[0].primitives[1].targets[0].attributes[2].accessor_index", target->attributes[2].accessor_index, 34, false);
    TEST_EQ("meshes[0].primitives[1].targets[0].attributes[2].type",           target->attributes[2].type, GLTF_MESH_ATTRIBUTE_TYPE_TANGENT, false);

    target = (Gltf_Morph_Target*)((u8*)target + target->stride);
    TEST_EQ("meshes[1].primitives[1].target_count", primitive->target_count, 2, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[0].accessor_index", target->attributes[0].accessor_index, 43, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[0].type",           target->attributes[0].type, GLTF_MESH_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[1].accessor_index", target->attributes[1].accessor_index, 42, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[1].type",           target->attributes[1].type, GLTF_MESH_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[2].accessor_index", target->attributes[2].accessor_index, 44, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[2].type",           target->attributes[2].type, GLTF_MESH_ATTRIBUTE_TYPE_TANGENT, false);

    mesh = (Gltf_Mesh*)((u8*)mesh + mesh->stride);
    TEST_EQ("meshes[1].primitive_count", mesh->primitive_count, 3, false);
    TEST_EQ("meshes[1].weight_count",    mesh->weight_count,    2, false);

    TEST_FEQ("meshes[1].weights[0]", mesh->weights[0], 0  , false);
    TEST_FEQ("meshes[1].weights[1]", mesh->weights[1], 0.5, false);

    primitive = mesh->primitives;
    TEST_EQ("meshes[1].primitives[0].indices",  primitive->indices,  11, false);
    TEST_EQ("meshes[1].primitives[0].material", primitive->material, 13, false);

    TEST_EQ("meshes[1].primitives[0].position", primitive->position, 12, false);
    TEST_EQ("meshes[1].primitives[0].normal", primitive->normal, 13, false);
    TEST_EQ("meshes[1].primitives[0].tangent", primitive->tangent, 14, false);
    TEST_EQ("meshes[1].primitives[0].tex_coord_0", primitive->tex_coord_0, 15, false);

    attribute = primitive->extra_attributes;
    TEST_EQ("meshes[1].primitives[0].extra_attribute_count", primitive->extra_attribute_count,0, false);

    primitive = (Gltf_Mesh_Primitive*)((u8*)primitive + primitive->stride);
    TEST_EQ("meshes[1].primitives[1]", primitive->indices,  11, false);
    TEST_EQ("meshes[1].primitives[1]", primitive->material, 13, false);

    TEST_EQ("meshes[1].primitives[1].position", primitive->position, 12, false);
    TEST_EQ("meshes[1].primitives[1].normal", primitive->normal, 13, false);
    TEST_EQ("meshes[1].primitives[1].tangent", primitive->tangent, 14, false);
    TEST_EQ("meshes[1].primitives[1].tex_coord_0", primitive->tex_coord_0, 15, false);

    target = primitive->targets;
    TEST_EQ("meshes[1].primitives[1].target_count", primitive->target_count, 2, false);
    TEST_EQ("meshes[1].primitives[1].targets[0].attributes[0].accessor_index", target->attributes[0].accessor_index, 13, false);
    TEST_EQ("meshes[1].primitives[1].targets[0].attributes[0].type",           target->attributes[0].type, GLTF_MESH_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].primitives[1].targets[0].attributes[1].accessor_index", target->attributes[1].accessor_index, 12, false);
    TEST_EQ("meshes[1].primitives[1].targets[0].attributes[1].type",           target->attributes[1].type, GLTF_MESH_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].primitives[1].targets[0].attributes[2].accessor_index", target->attributes[2].accessor_index, 14, false);
    TEST_EQ("meshes[1].primitives[1].targets[0].attributes[2].type",           target->attributes[2].type, GLTF_MESH_ATTRIBUTE_TYPE_TANGENT, false);

    target = (Gltf_Morph_Target*)((u8*)target + target->stride);
    TEST_EQ("meshes[1].primitives[1].target_count", primitive->target_count, 2, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[0].accessor_index", target->attributes[0].accessor_index, 23, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[0].type",           target->attributes[0].type, GLTF_MESH_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[1].accessor_index", target->attributes[1].accessor_index, 22, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[1].type",           target->attributes[1].type, GLTF_MESH_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[2].accessor_index", target->attributes[2].accessor_index, 24, false);
    TEST_EQ("meshes[1].primitives[1].targets[1].attributes[2].type",           target->attributes[2].type, GLTF_MESH_ATTRIBUTE_TYPE_TANGENT, false);

    primitive = (Gltf_Mesh_Primitive*)((u8*)primitive + primitive->stride);
    TEST_EQ("meshes[1].primitives[2]", primitive->indices,  1, false);
    TEST_EQ("meshes[1].primitives[2]", primitive->material, 3, false);

    TEST_EQ("meshes[1].primitives[2].position",    primitive->position,    2, false);
    TEST_EQ("meshes[1].primitives[2].normal",      primitive->normal,      3, false);
    TEST_EQ("meshes[1].primitives[2].tangent",     primitive->tangent,     4, false);
    TEST_EQ("meshes[1].primitives[2].tex_coord_0", primitive->tex_coord_0, -1, false);

    attribute = primitive->extra_attributes;
    TEST_EQ("meshes[1].primitives[2].extra_attribute_count", primitive->extra_attribute_count, 1, false);
    TEST_EQ("meshes[1].primitives[2].attributes[0]", attribute[0].n, 1, false);
    TEST_EQ("meshes[1].primitives[2].attributes[0]", attribute[0].accessor_index, 5, false);

    target = primitive->targets;
    TEST_EQ("meshes[1].primitives[2].target_count", primitive->target_count, 2, false);
    TEST_EQ("meshes[1].primitives[2].targets[0].attributes[0].accessor_index", target->attributes[0].accessor_index, 3, false);
    TEST_EQ("meshes[1].primitives[2].targets[0].attributes[0].type",           target->attributes[0].type, GLTF_MESH_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].primitives[2].targets[0].attributes[1].accessor_index", target->attributes[1].accessor_index, 2, false);
    TEST_EQ("meshes[1].primitives[2].targets[0].attributes[1].type",           target->attributes[1].type, GLTF_MESH_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].primitives[2].targets[0].attributes[2].accessor_index", target->attributes[2].accessor_index, 4, false);
    TEST_EQ("meshes[1].primitives[2].targets[0].attributes[2].type",           target->attributes[2].type, GLTF_MESH_ATTRIBUTE_TYPE_TANGENT, false);

    target = (Gltf_Morph_Target*)((u8*)target + target->stride);
    TEST_EQ("meshes[1].primitives[2].target_count", primitive->target_count, 2, false);
    TEST_EQ("meshes[1].primitives[2].targets[1].attributes[0].accessor_index", target->attributes[0].accessor_index, 9, false);
    TEST_EQ("meshes[1].primitives[2].targets[1].attributes[0].type",           target->attributes[0].type, GLTF_MESH_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].primitives[2].targets[1].attributes[1].accessor_index", target->attributes[1].accessor_index, 7, false);
    TEST_EQ("meshes[1].primitives[2].targets[1].attributes[1].type",           target->attributes[1].type, GLTF_MESH_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].primitives[2].targets[1].attributes[2].accessor_index", target->attributes[2].accessor_index, 6, false);
    TEST_EQ("meshes[1].primitives[2].targets[1].attributes[2].type",           target->attributes[2].type, GLTF_MESH_ATTRIBUTE_TYPE_TANGENT, false);

    END_TEST_MODULE();
}
void test_nodes(Gltf_Node *nodes) {
    BEGIN_TEST_MODULE("Gltf_Node", true, false);

    // @Todo shuffle the nodes around in the test file

    Gltf_Node *node = nodes;
    TEST_EQ("nodes[0].camera", node->camera, 1, false);

    float inaccuracy =      0.0000001;
    TEST_FEQ("nodes[0].matrix[0]",  node->matrix.row0.x, -0.99975   , false);
    TEST_FEQ("nodes[0].matrix[1]",  node->matrix.row0.y, -0.00679829, false);
    TEST_FEQ("nodes[0].matrix[2]",  node->matrix.row0.z, 0.0213218  , false);
    TEST_FEQ("nodes[0].matrix[3]",  node->matrix.row0.w, 0          , false);
    TEST_FEQ("nodes[0].matrix[4]",  node->matrix.row1.x, 0.00167596 , false);
    TEST_FEQ("nodes[0].matrix[5]",  node->matrix.row1.y, 0.927325   , false);
    TEST_FEQ("nodes[0].matrix[6]",  node->matrix.row1.z, 0.374254   , false);
    TEST_FEQ("nodes[0].matrix[7]",  node->matrix.row1.w, 0          , false);
    TEST_FEQ("nodes[0].matrix[8]",  node->matrix.row2.x, -0.0223165 , false);
    TEST_FEQ("nodes[0].matrix[9]",  node->matrix.row2.y, 0.374196   , false);
    TEST_FEQ("nodes[0].matrix[10]", node->matrix.row2.z, -0.927081  , false);
    TEST_FEQ("nodes[0].matrix[11]", node->matrix.row2.w, 0          , false);
    TEST_FEQ("nodes[0].matrix[12]", node->matrix.row3.x, -0.0115543 , false);
    TEST_FEQ("nodes[0].matrix[13]", node->matrix.row3.y, 0.194711   , false);
    TEST_FEQ("nodes[0].matrix[14]", node->matrix.row3.z, -0.478297  , false);
    TEST_FEQ("nodes[0].matrix[15]", node->matrix.row3.w, 1          , false);

    TEST_FEQ("nodes[0].weights[0]", node->weights[0], 0.5, false);
    TEST_FEQ("nodes[0].weights[1]", node->weights[1], 0.6, false);
    TEST_FEQ("nodes[0].weights[2]", node->weights[2], 0.7, false);
    TEST_FEQ("nodes[0].weights[3]", node->weights[3], 0.8, false);

    node = (Gltf_Node*)((u8*)node + node->stride);
    TEST_FEQ("nodes[1].rotation.x", node->trs.rotation.x, 0, false);
    TEST_FEQ("nodes[1].rotation.y", node->trs.rotation.y, 0, false);
    TEST_FEQ("nodes[1].rotation.z", node->trs.rotation.z, 0, false);
    TEST_FEQ("nodes[1].rotation.w", node->trs.rotation.w, 1, false);

    TEST_FEQ("nodes[1].translation.x", node->trs.translation.x, -17.7082, false);
    TEST_FEQ("nodes[1].translation.y", node->trs.translation.y, -11.4156, false);
    TEST_FEQ("nodes[1].translation.z", node->trs.translation.z,   2.0922, false);

    TEST_FEQ("nodes[1].scale.x", node->trs.scale.x, 1, false);
    TEST_FEQ("nodes[1].scale.y", node->trs.scale.y, 1, false);
    TEST_FEQ("nodes[1].scale.z", node->trs.scale.z, 1, false);

    node = (Gltf_Node*)((u8*)node + node->stride);
    TEST_EQ("nodes[2].children[0]", node->children[0], 1, false);
    TEST_EQ("nodes[2].children[1]", node->children[1], 2, false);
    TEST_EQ("nodes[2].children[2]", node->children[2], 3, false);
    TEST_EQ("nodes[2].children[3]", node->children[3], 4, false);


    END_TEST_MODULE();
}
void test_samplers(Gltf_Sampler *samplers) {
    BEGIN_TEST_MODULE("Gltf_Sampler", true, false);

    Gltf_Sampler *sampler = samplers;
    TEST_EQ("samplers[0].mag_filter", sampler->mag_filter, 1, false);
    TEST_EQ("samplers[0].min_filter", sampler->min_filter, 1, false);
    TEST_EQ("samplers[0].wrap_u",     sampler->wrap_u,     0, false);
    TEST_EQ("samplers[0].wrap_v",     sampler->wrap_v,     0, false);

    sampler = (Gltf_Sampler*)((u8*)sampler + sampler->stride);
    TEST_EQ("samplers[1].mag_filter", sampler->mag_filter, 1, false);
    TEST_EQ("samplers[1].min_filter", sampler->min_filter, 1, false);
    TEST_EQ("samplers[1].wrap_u",     sampler->wrap_u,     0, false);
    TEST_EQ("samplers[1].wrap_v",     sampler->wrap_v,     0, false);

    sampler = (Gltf_Sampler*)((u8*)sampler + sampler->stride);
    TEST_EQ("samplers[2].mag_filter", sampler->mag_filter, 1, false);
    TEST_EQ("samplers[2].min_filter", sampler->min_filter, 1, false);
    TEST_EQ("samplers[2].wrap_u",     sampler->wrap_u,     0, false);
    TEST_EQ("samplers[2].wrap_v",     sampler->wrap_v,     0, false);

    END_TEST_MODULE();
}
void test_scenes(Gltf_Scene *scenes) {
    BEGIN_TEST_MODULE("Gltf_Scene", true, false);

    Gltf_Scene *scene = scenes;
    TEST_EQ("scenes[0].nodes[0]", scene->nodes[0], 0, false);
    TEST_EQ("scenes[0].nodes[1]", scene->nodes[1], 1, false);
    TEST_EQ("scenes[0].nodes[2]", scene->nodes[2], 2, false);
    TEST_EQ("scenes[0].nodes[3]", scene->nodes[3], 3, false);
    TEST_EQ("scenes[0].nodes[4]", scene->nodes[4], 4, false);

    scene = (Gltf_Scene*)((u8*)scene + scene->stride);
    TEST_EQ("scenes[1].nodes[0]", scene->nodes[0], 5, false);
    TEST_EQ("scenes[1].nodes[1]", scene->nodes[1], 6, false);
    TEST_EQ("scenes[1].nodes[2]", scene->nodes[2], 7, false);
    TEST_EQ("scenes[1].nodes[3]", scene->nodes[3], 8, false);
    TEST_EQ("scenes[1].nodes[4]", scene->nodes[4], 9, false);

    scene = (Gltf_Scene*)((u8*)scene + scene->stride);
    TEST_EQ("scenes[2].nodes[0]", scene->nodes[0], 10, false);
    TEST_EQ("scenes[2].nodes[1]", scene->nodes[1], 11, false);
    TEST_EQ("scenes[2].nodes[2]", scene->nodes[2], 12, false);
    TEST_EQ("scenes[2].nodes[3]", scene->nodes[3], 13, false);
    TEST_EQ("scenes[2].nodes[4]", scene->nodes[4], 14, false);

    END_TEST_MODULE();
}
void test_skins(Gltf_Skin *skins) {
    BEGIN_TEST_MODULE("Gltf_Skin", true, false);

    Gltf_Skin *skin = skins;
    TEST_EQ("skins[0].inverse_bind_matrices", skin->inverse_bind_matrices, 0, false);
    TEST_EQ("skins[0].skeleton",              skin->skeleton,              1, false);
    TEST_EQ("skins[0].joint_count",           skin->joint_count,           2, false);
    TEST_EQ("skins[0].joints[0]",             skin->joints[0],             1, false);
    TEST_EQ("skins[0].joints[1]",             skin->joints[1],             2, false);

    skin = (Gltf_Skin*)((u8*)skin + skin->stride);
    TEST_EQ("skins[1].inverse_bind_matrices", skin->inverse_bind_matrices, 1, false);
    TEST_EQ("skins[1].skeleton",              skin->skeleton,              2, false);
    TEST_EQ("skins[1].joint_count",           skin->joint_count,           2, false);
    TEST_EQ("skins[1].joints[0]",             skin->joints[0],             3, false);
    TEST_EQ("skins[1].joints[1]",             skin->joints[1],             4, false);

    skin = (Gltf_Skin*)((u8*)skin + skin->stride);
    TEST_EQ("skins[2].inverse_bind_matrices", skin->inverse_bind_matrices, 2, false);
    TEST_EQ("skins[2].skeleton",              skin->skeleton,              3, false);
    TEST_EQ("skins[2].joint_count",           skin->joint_count,           2, false);
    TEST_EQ("skins[2].joints[0]",             skin->joints[0],             5, false);
    TEST_EQ("skins[2].joints[1]",             skin->joints[1],             6, false);

    skin = (Gltf_Skin*)((u8*)skin + skin->stride);
    TEST_EQ("skins[3].inverse_bind_matrices", skin->inverse_bind_matrices, 3, false);
    TEST_EQ("skins[3].skeleton",              skin->skeleton,              4, false);
    TEST_EQ("skins[3].joint_count",           skin->joint_count,           2, false);
    TEST_EQ("skins[3].joints[0]",             skin->joints[0],             7, false);
    TEST_EQ("skins[3].joints[1]",             skin->joints[1],             8, false);

    END_TEST_MODULE();
}
void test_textures(Gltf_Texture *textures) {
    BEGIN_TEST_MODULE("Gltf_Texture", true, false);

    Gltf_Texture *texture = textures;
    TEST_EQ("textures[0].sampler", texture->sampler,       0, false);
    TEST_EQ("textures[0].source",  texture->source_image,  1, false);

    texture = (Gltf_Texture*)((u8*)texture + texture->stride);
    TEST_EQ("textures[1].sampler", texture->sampler,       2, false);
    TEST_EQ("textures[1].source",  texture->source_image,  3, false);

    texture = (Gltf_Texture*)((u8*)texture + texture->stride);
    TEST_EQ("textures[2].sampler", texture->sampler,       4, false);
    TEST_EQ("textures[2].source",  texture->source_image,  5, false);

    texture = (Gltf_Texture*)((u8*)texture + texture->stride);
    TEST_EQ("textures[3].sampler", texture->sampler,       6, false);
    TEST_EQ("textures[3].source",  texture->source_image,  7, false);

    END_TEST_MODULE();
}
#endif

// This file is gltf file parser. It reads a gltf file and turns the information into usable C++.
// It does so in a potentially unorthodox way, in the interest of consistency, simplicity, speed and code size.
// There are not more general helper functions such as "find_key(..)". The reason for this is that for functions
// like these to work, much more jumping back and forth in the file takes place, for an equivalent number of
// compare and branch operations, if not more.
//
// The implementation instead uses helper functions for jumping to characters. In this way, the file never stops
// being traversed forwards. And as the file is traversed, the C++ structs are filled at the same time.
//
// The workflow takes the form: define a char of interest, define a break char, jump to the next char of interest
// while they come before the closing char in the file. This is more complicated and error prone than saying "find
// me this key", but such function would be expensive, as you could not go through the whole file in one clean parse:
// you have to look ahead for the string matching the key, and then return it. Then to get the next key you have to
// do the same, then when all keys are collected, do a big jump passed all the keys. This can be a big jump for some
// objects.
//
// While my method is potentially error prone, it is super clean to debug and understand, as you know exactly
// where it is in the file at any point, how much progress it has made, what char or string has tripped it up etc.
// It is simple to understand as the same short clear functions are used everywhere: "jump to char x, unless y is
// in the way", simple. The call stack is super small, so you only have to check a few functions at any time for a bug.
//
// Finally it seems fast. One pass through the file, when you reach the end everything is done, no more work. And the
// work during the file is super cheap and cache friendly: match the current key against a small list of the possible
// keys that can exist in the current object, call the specific parse function for its specific value. Move forward.
// Zero Generality (which is what makes it error prone, as everything has to be considered its own thing, and then
// coded as such, but man up! GO FAST!)


/*** Below Comments: Notes On Design of this file (Often Rambling, Stream of thoughts) ***/

/*
    Final Solution:
        Loop the file once, parse as I go. No need to loop once to tokenize or get other info. Memory access
        and branching will never be that clean for this task regardless of the method, and since I expect that
        these files can get pretty large, only needing one pass seems like it would be fastest.
*/

/* ** Old Note See Above **
    After working on the problem more, I hav realised that a json parser would be very difficult. It would not be
    hard to serialize the text data into a big block of mem with pointers into that memory mapped to keys.
    Just iterate through keys or do a hash look up to find the matching key, deref its pointer to find its data.
    By just knowing what type of data the key is pointing to, it is trivial to interpret the data on the end of the
    pointer.

    However I am going to use my original plan but just in one loop. Because this is much faster than the other option.
    To serialize all the text data into a memory block meaningful for C++ would be a waste of time, because I would
    then have to turn that memory block into each of the types of structs which meaningfully map to my application.
    This process is easier with the memory block than just the text data, but it would be interesting work to make the
    parser/serialiser, then tiresome boring work to turn that into Gltf_<> structs. It would be slow, as it is
    essentially two programs, one of which will incur minor cache minsses (the data will be parsed in order, but
    I will still have to jump around in look ups). As such I will turn the data into Gltf_<> structs straight from
    the text. This will be more difficult work, as a general task becomes more specific (each gltf property becomes
    its own task to serialize straight from text, rather than text being largely general (objects, arrays, numbers)
    and then specific to create from the serialized block).

    This will be much faster: I just pass through the data once, all in order. And at the end I have a bunch
    of C++ structs all mapped to gltf properties. Cool. Let's begin!
*/

/* ** Old Note, See Above **
    I am unsure how to implement this file. But I think I have come to a working solution:

    I have a working idea:

        Make a list of function pointers. This list is ordered by the order of the gltf keys
        in the file. Then I do not have to branch for each key. I just loop through the file once
        to get the memory required for each first level type. As I do this, I push the correct function
        pointers to the array. Then I loop through this list calling each function, which will
        use the file data in order, keeping it all in cache.

    e.g.

    ~~~~~~~~~~~~~~~~~~~~

    Array<FnPtr> parse_functions;

    Key key = match_key(<str>);
    get_size_<key>() {
        gltf-><key>_count = ...
        gltf-><key>s = memory_allocate(...)
        parse_functions.push(&parse_<key>())
    }

    char *data;
    u64 file_offset;
    for(int i = 0; i < parse_functions) {
        // start at the beginning of the file again, offset get incremented to point at the start
        // of the next key inside the parse_<key> function
        parse_fucntions[i](data, offset);
    }

    ~~~~~~~~~~~

    This way I loop the entire file 2 times, and each time in entirety, so the prefectcher is only
    interrupted once. I only have to match keys in order to dispatch correct fucntions once, as the
    second pass can just call the functions in order.
*/


/* Below is old code related to this file. I am preserving it because it is cool code imo, does some cool things... */

/* **** Code example for calculating the length of json arrays with simd *****

// string assumed 16 len
inline static u16 simd_match_char(const char *string, char c) {
    __m128i a = _mm_loadu_si128((__m128i*)string);
    __m128i b = _mm_set1_epi8(c);
    a = _mm_cmpeq_epi8(a, b);
    return _mm_movemask_epi8(a);
}

inline static int pop_count(u16 mask) {
    return __builtin_popcount(mask);
}
inline static int ctz(u16 mask) {
    return __builtin_ctz(mask);
}
inline static int clzs(u16 mask) {
    return __builtin_clzs(mask);
}

int resolve_depth(u16 mask1, u16 mask2, int current_depth) {
    u8 tz;
    int open;
    while(mask1) {
        tz = ctz(mask1);
        open = pop_count(mask2 << (16 - tz));

        current_depth -= 1;
        if (current_depth + open == 0)
            return tz;

        mask1 ^= 1 << tz;
    }
    return -1;
}

int get_obj_array_len(const char *string, u64 *offset) {
    u16 mask1;
    u16 mask2;
    int array_depth = 0;
    int ret;
    while(true) {
        mask1 = simd_match_char(string + *offset, ']');
        mask2 = simd_match_char(string + *offset, '[');
        if (array_depth - pop_count(mask1) <= 0) {
            ret = resolve_depth(mask1, mask2, array_depth);
            if(ret != -1)
                return *offset + ret;
        }
        array_depth += pop_count(mask2) - pop_count(mask1);
        *offset += 16;
    }
}
int main() {
    const char *d = "[[[[xxxxxxxxxxxxx]]]xxxxxxxxxxx]";

    u64 offset = 0;
    int x = get_obj_array_len(d, &offset);
    printf("%i", x);

    return 0;
}
*/

/*
    ** Template Key Pad **

    Key_Pad keys[] = {
        {"bufferViewxxxxxx",  6},
        {"byteOffsetxxxxxx",  6},
        {"componentTypexxx",  3},
        {"countxxxxxxxxxxx", 11},
        {"maxxxxxxxxxxxxxx", 13},
        {"minxxxxxxxxxxxxx", 13},
        {"typexxxxxxxxxxxx", 12},
        {"sparsexxxxxxxxxx", 10},
    };
    int key_count = 8;


// Old but imo cool code, so I am preserving it
int resolve_depth(u16 mask1, u16 mask2, int current_depth, int *results) {
    u8 tz;
    int open;
    int index = 0;
    while(mask1) {
        tz = count_trailing_zeros_u16(mask1);
        open = pop_count16(mask2 << (16 - tz));

        current_depth -= 1;
        if (current_depth + open == 0) {
            results[index] = tz;
            ++index;
        }

        mask1 ^= 1 << tz;
    }
    return index;
}

int parse_object_array_len(const char *string, u64 *offset, u64 *object_offsets) {
    u64 inc = 0;
    int array_depth  = 0;
    int object_depth = 0;
    int object_count = 0;
    int rd;
    int rds[8];
    u16 mask1;
    u16 mask2;
    u16 temp_count;
    while(true) {
        // check array
        mask1 = simd_match_char(string + inc, ']');
        mask2 = simd_match_char(string + inc, '[');
        if (array_depth - pop_count16(mask1) <= 0) {
            rd = resolve_depth(mask1, mask2, array_depth, rds);
            if(rd != 0) {
                // check object within the end of the array
                mask1 = simd_match_char(string + inc, '}') << (16 - rds[0]);
                mask2 = simd_match_char(string + inc, '{') << (16 - rds[0]);
                if (object_depth - pop_count16(mask1) <= 0) {
                    temp_count = object_count;
                    object_count += resolve_depth(mask1, mask2, object_depth, rds);
                    for(int i = temp_count; i < object_count; ++i)
                        object_offsets[i] = inc + rds[i - temp_count];
                }
                inc += rds[0];
                *offset += inc; // deref offset only once just to keep cache as clean as possible
                return object_count;
            }
        }
        array_depth += pop_count16(mask2) - pop_count16(mask1);

        // check object
        mask1 = simd_match_char(string + inc, '}');
        mask2 = simd_match_char(string + inc, '{');
        if (object_depth - pop_count16(mask1) <= 0) {
            temp_count = object_count;
            object_count += resolve_depth(mask1, mask2, object_depth, rds);
            for(int i = temp_count; i < object_count; ++i)
                object_offsets[i] = inc + rds[i - temp_count];
        }
        object_depth += pop_count16(mask2) - pop_count16(mask1);

        inc += 16;
    }
}
enum Gltf_Key {
    GLTF_KEY_BUFFER_VIEW,
    GLTF_KEY_BYTE_OFFSET,
    GLTF_KEY_COMPONENT_TYPE,
    GLTF_KEY_COUNT,
    GLTF_KEY_MAX,
    GLTF_KEY_MIN,
    GLTF_KEY_TYPE,
    GLTF_KEY_SPARSE,
    GLTF_KEY_INDICES,
    GLTF_KEY_VALUES,
};
// increments beyond end char
void collect_string(const char *data, u64 *offset, char c, char *buf) {
    int i = 0;
    u64 inc = 0;
    while(data[inc] != c) {
        buf[i] = data[inc];
        ++inc;
        ++i;
    }
    buf[i] = '\0';
    *offset += inc + 1;
}


// string must be assumed 16 in len
static int find_string_in_list(const char *string, const Key_Pad *keys, int count) {
    for(int i = 0; i < count; ++i) {
        if(simd_strcmp_short(string, keys[i].key, keys[i].padding) == 0)
            return i;
    }
    return -1;
}

struct Key_Pad {
    const char *key;
    u8 padding;
};

*/
