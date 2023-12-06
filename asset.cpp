#include "asset.hpp"
#include "gltf.hpp"
#include "file.hpp"

static Assets s_Assets;
Assets* get_assets_instance() { return &s_Assets; }

static Model_Allocators init_model_allocators(Model_Allocators_Config *config) {
    Gpu *gpu = get_gpu_instance();

    // Vertex allocator
    Gpu_Allocator_Config vertex_allocator_config = {};

    vertex_allocator_config.allocation_cap         = 512;
    vertex_allocator_config.to_stage_cap           = 64;
    vertex_allocator_config.to_upload_cap          = 32;
    vertex_allocator_config.stage_bit_granularity  = 256;
    vertex_allocator_config.upload_bit_granularity = 256;

    vertex_allocator_config.staging_queue_byte_cap = VERTEX_STAGE_SIZE;
    vertex_allocator_config.upload_queue_byte_cap  = VERTEX_DEVICE_SIZE;
    vertex_allocator_config.stage_cap              = VERTEX_STAGE_SIZE;
    vertex_allocator_config.upload_cap             = VERTEX_DEVICE_SIZE;

    vertex_allocator_config.disk_storage           = cstr_to_string("allocator-files/vertex_allocator_file.bin");
    vertex_allocator_config.stage_ptr              = gpu->memory.vertex_ptrs[0];
    vertex_allocator_config.stage                  = gpu->memory.vertex_bufs_stage[0];
    vertex_allocator_config.upload                 = gpu->memory.vertex_buf_device;

    Gpu_Allocator vertex_allocator;
    Gpu_Allocator_Result creation_result = create_allocator(&vertex_allocator_config, &vertex_allocator);
    assert(creation_result == ALLOCATOR_RESULT_SUCCESS);

    // Index allocator
    Gpu_Allocator_Config index_allocator_config = {};

    index_allocator_config.allocation_cap         = 512;
    index_allocator_config.to_stage_cap           = 64;
    index_allocator_config.to_upload_cap          = 32;
    index_allocator_config.stage_bit_granularity  = 256;
    index_allocator_config.upload_bit_granularity = 256;

    index_allocator_config.staging_queue_byte_cap = INDEX_STAGE_SIZE;
    index_allocator_config.upload_queue_byte_cap  = INDEX_DEVICE_SIZE;
    index_allocator_config.stage_cap              = INDEX_STAGE_SIZE;
    index_allocator_config.upload_cap             = INDEX_DEVICE_SIZE;

    index_allocator_config.disk_storage           = cstr_to_string("allocator-files/index_allocator_file.bin");
    index_allocator_config.stage_ptr              = gpu->memory.index_ptrs[0];
    index_allocator_config.stage                  = gpu->memory.index_bufs_stage[0];
    index_allocator_config.upload                 = gpu->memory.index_buf_device;

    Gpu_Allocator index_allocator;
    creation_result = create_allocator(&index_allocator_config, &index_allocator);
    assert(creation_result == ALLOCATOR_RESULT_SUCCESS);

    // Tex allocator
    Gpu_Tex_Allocator_Config tex_allocator_config = {};

    tex_allocator_config.allocation_cap         = 512;
    tex_allocator_config.to_stage_cap           = 64;
    tex_allocator_config.to_upload_cap          = 32;
    tex_allocator_config.stage_bit_granularity  = 256 * 4;
    tex_allocator_config.upload_bit_granularity = 256 * 4;
    tex_allocator_config.string_buffer_size     = 1024;

    tex_allocator_config.staging_queue_byte_cap = TEXTURE_STAGE_SIZE;
    tex_allocator_config.upload_queue_byte_cap  = TEXTURE_DEVICE_SIZE;
    tex_allocator_config.stage_cap              = TEXTURE_STAGE_SIZE;
    tex_allocator_config.upload_cap             = TEXTURE_DEVICE_SIZE;

    tex_allocator_config.stage_ptr              = gpu->memory.texture_ptrs[0];
    tex_allocator_config.stage                  = gpu->memory.texture_bufs_stage[0];
    tex_allocator_config.upload                 = gpu->memory.texture_mem_device;

    Gpu_Tex_Allocator tex_allocator;
    creation_result = create_tex_allocator(&tex_allocator_config, &tex_allocator);
    assert(creation_result == ALLOCATOR_RESULT_SUCCESS);
    if (creation_result != ALLOCATOR_RESULT_SUCCESS) {
        println("Tex_Allocator creation result: %u", creation_result);
        return {};
    }

    Sampler_Allocator sampler = create_sampler_allocator(0);

    Model_Allocators ret = {
        .index   = index_allocator,
        .vertex  = vertex_allocator,
        .tex     = tex_allocator,
        .sampler = sampler,
    };

    Model_Allocators *model_allocators = &get_assets_instance()->allocators;
    *model_allocators = ret;

    return ret;
}
static void shutdown_model_allocators() {
    Model_Allocators *allocs = &get_assets_instance()->allocators;

    destroy_allocator(&allocs->index);
    destroy_allocator(&allocs->vertex);
    destroy_tex_allocator(&allocs->tex);
    destroy_sampler_allocator(&allocs->sampler);
}

void init_assets() {
    Model_Allocators_Config config = {};
    init_model_allocators(&config);

    Assets           *g_assets = get_assets_instance();
    Model_Allocators *allocs   = &g_assets->allocators;

    g_assets->models = (Model*)malloc_h(sizeof(Model) * g_model_count, 8);

    for(u32 i = 0; i < g_model_count; ++i) {
        g_assets->models[i] = load_model(allocs, g_model_identifiers[i]);
    }
}
void kill_assets() {
    Assets *g_assets = get_assets_instance();

    for(u32 i = 0; i < g_assets->count; ++i)
        free_model(&g_assets->models[i], g_model_identifiers[i].type);

    free_h(g_assets->models);
    shutdown_model_allocators();
}

// These types are used for model loading
enum Buffer_View_Data_Type {
    BUFFER_VIEW_DATA_TYPE_NONE    = 0,
    BUFFER_VIEW_DATA_TYPE_VERTEX  = 1,
    BUFFER_VIEW_DATA_TYPE_INDEX   = 2,
    BUFFER_VIEW_DATA_TYPE_UNIFORM = 3,
};
struct Buffer_View {
    u64 offset;
    u64 size;
    Buffer_View_Data_Type type;
};

//
// The below function is a simulation of what a real loading function would look like. For instance, in a more
// developed app with a broader set of models, it would still be true that sets of models have specific
// characteristics: even though you may have 200 buildings, each building model may share an equivalent shader, etc.
// and so loading the buildings/map can be grouped to a 'load_buildings()' function. I just currently do not have
// well defined subsets of models yet, so 'load_cube()' and 'load_player()' (<- coming soon, Cesium_Man) will do for
// now.
//
// @Note Maybe I should go through and revert gltf parser to use counts, rather than the procedural temp allocation
// method. It is cool in principle, but I cannot tell if it is worth it, need to @Test.
//
static Model_Cube load_model_cube(Model_Allocators *allocs, String *model_file, String *model_dir) {
    Model_Cube ret = {};

    // Setup model directory
    char uri_buf[127];
    memcpy(uri_buf, model_dir->str,  model_dir->len);
    memcpy(uri_buf + model_dir->len, model_file->str, model_file->len);

    Gltf gltf = parse_gltf(uri_buf);

    // Load base texture
    Gltf_Material *gltf_mat     = gltf.materials;
    Gltf_Texture  *gltf_tex     = gltf_texture_by_index(&gltf, gltf_mat->base_color_texture_index);
    Gltf_Sampler  *gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
    Gltf_Image    *gltf_image   = gltf_image_by_index  (&gltf, gltf_tex->source_image);

    // @Ugly This is a bit ugly
    String tmp_uri;
    strcpy(uri_buf + model_dir->len, gltf_image->uri); // @Todo update the gltf uris to use String type
    tmp_uri = cstr_to_string((const char*)uri_buf);

    Gpu_Allocator_Result res;
    Sampler sampler_info = {};

    sampler_info.wrap_s     = (VkSamplerAddressMode)gltf_sampler->wrap_u;
    sampler_info.wrap_t     = (VkSamplerAddressMode)gltf_sampler->wrap_v;
    sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
    sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;

    res                      = tex_add_texture(&allocs->tex, &tmp_uri, &ret.tex_key_base);
    ret.sampler_key_base     = add_sampler(&allocs->sampler, &sampler_info);

    // Load pbr texture
    gltf_tex     = gltf_texture_by_index(&gltf, gltf_mat->metallic_roughness_texture_index);
    gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
    gltf_image   = gltf_image_by_index  (&gltf, gltf_tex->source_image);

    // @Ugly This is a bit ugly
    strcpy(uri_buf + model_dir->len, gltf_image->uri); // @Todo update the gltf uris to use String type
    tmp_uri = cstr_to_string((const char*)uri_buf);

    sampler_info.wrap_s     = (VkSamplerAddressMode)gltf_sampler->wrap_u;
    sampler_info.wrap_t     = (VkSamplerAddressMode)gltf_sampler->wrap_v;
    sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
    sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;

    res                  = tex_add_texture(&allocs->tex, &tmp_uri, &ret.tex_key_pbr);
    ret.sampler_key_pbr  = add_sampler(&allocs->sampler, &sampler_info);

    // Load vertex/index data
    Gltf_Buffer         *gltf_buffer;
    Gltf_Accessor       *gltf_accessor;
    Gltf_Buffer_View    *gltf_buffer_view;
    Gltf_Mesh_Primitive *gltf_primitive;

    u32          buffer_view_count = gltf_buffer_view_get_count(&gltf);
    Buffer_View *buffer_views      = (Buffer_View*)malloc_t(sizeof(Buffer_View) * buffer_view_count, 8);
    memset(buffer_views, 0, sizeof(Buffer_View) * buffer_view_count);

    u32 buffer_view_index;
    u32 buffer_view_position;
    u32 buffer_view_normal;
    u32 buffer_view_tangent;
    u32 buffer_view_tex_coords;

    // Index data
    u32 tmp          = gltf.meshes[0].primitives[0].indices;
    ret.topology     = (VkPrimitiveTopology)gltf.meshes[0].primitives[0].topology;
    gltf_accessor    = gltf_accessor_by_index(&gltf, tmp);
    ret.offset_index = gltf_accessor->byte_offset; // This will later be summed with the allocation offset
    ret.count        = gltf_accessor->count;

    switch(gltf_accessor->format) {
    case GLTF_ACCESSOR_FORMAT_SCALAR_U16:
    {
        ret.index_type = VK_INDEX_TYPE_UINT16;
        break;
    }
    case GLTF_ACCESSOR_FORMAT_SCALAR_U32:
    {
        ret.index_type = VK_INDEX_TYPE_UINT32;
        break;
    }
    default:
        assert(false && "Invalid Index Type");
    }

    tmp                      = gltf_accessor->buffer_view;
    gltf_buffer_view         = gltf_buffer_view_by_index(&gltf, tmp);
    buffer_views[tmp].type   = BUFFER_VIEW_DATA_TYPE_INDEX;
    buffer_views[tmp].offset = gltf_buffer_view->byte_offset;
    buffer_views[tmp].size   = gltf_buffer_view->byte_length;

    buffer_view_index = tmp;

    // Position data
    tmp = gltf.meshes[0].primitives[0].position;

    gltf_accessor       = gltf_accessor_by_index(&gltf, tmp);
    ret.offset_position = gltf_accessor->byte_offset; // This will later be summed with the allocation offset
    ret.fmt_position    = (VkFormat)gltf_accessor->format;
    ret.stride_position = gltf_accessor->byte_stride;
    ret.count           = gltf_accessor->count;

    tmp                      = gltf_accessor->buffer_view;
    gltf_buffer_view         = gltf_buffer_view_by_index(&gltf, tmp);
    buffer_views[tmp].type   = BUFFER_VIEW_DATA_TYPE_VERTEX;
    buffer_views[tmp].offset = gltf_buffer_view->byte_offset;
    buffer_views[tmp].size   = gltf_buffer_view->byte_length;

    buffer_view_position = tmp;

    // Normal data
    tmp = gltf.meshes[0].primitives[0].normal;

    gltf_accessor     = gltf_accessor_by_index(&gltf, tmp);
    ret.offset_normal = gltf_accessor->byte_offset; // This will later be summed with the allocation offset
    ret.fmt_normal    = (VkFormat)gltf_accessor->format;
    ret.stride_normal = gltf_accessor->byte_stride;
    ret.count         = gltf_accessor->count;

    tmp = gltf_accessor->buffer_view;
    gltf_buffer_view         = gltf_buffer_view_by_index(&gltf, tmp);
    buffer_views[tmp].type   = BUFFER_VIEW_DATA_TYPE_VERTEX;
    buffer_views[tmp].offset = gltf_buffer_view->byte_offset;
    buffer_views[tmp].size   = gltf_buffer_view->byte_length;

    buffer_view_normal = tmp;

    // Tangent data
    tmp = gltf.meshes[0].primitives[0].tangent;

    gltf_accessor      = gltf_accessor_by_index(&gltf, tmp);
    ret.offset_tangent = gltf_accessor->byte_offset; // This will later be summed with the allocation offset
    ret.fmt_tangent    = (VkFormat)gltf_accessor->format;
    ret.stride_tangent = gltf_accessor->byte_stride;
    ret.count          = gltf_accessor->count;

    tmp                      = gltf_accessor->buffer_view;
    gltf_buffer_view         = gltf_buffer_view_by_index(&gltf, tmp);
    buffer_views[tmp].type   = BUFFER_VIEW_DATA_TYPE_VERTEX;
    buffer_views[tmp].offset = gltf_buffer_view->byte_offset;
    buffer_views[tmp].size   = gltf_buffer_view->byte_length;

    buffer_view_tangent = tmp;

    // Tex Coords
    tmp = gltf.meshes[0].primitives[0].tex_coord_0;

    gltf_accessor         = gltf_accessor_by_index(&gltf, tmp);
    ret.offset_tex_coords = gltf_accessor->byte_offset; // This will later be summed with the allocation offset
    ret.fmt_tex_coords    = (VkFormat)gltf_accessor->format;
    ret.stride_tex_coords = gltf_accessor->byte_stride;
    ret.count             = gltf_accessor->count;

    tmp                      = gltf_accessor->buffer_view;
    gltf_buffer_view         = gltf_buffer_view_by_index(&gltf, tmp);
    buffer_views[tmp].type   = BUFFER_VIEW_DATA_TYPE_VERTEX;
    buffer_views[tmp].offset = gltf_buffer_view->byte_offset;
    buffer_views[tmp].size   = gltf_buffer_view->byte_length;

    buffer_view_tex_coords = tmp;

    // Allocate vertex/index data
    res = begin_allocation(&allocs->index);
    CHECK_GPU_ALLOCATOR_RESULT(res);
    res = begin_allocation(&allocs->vertex);
    CHECK_GPU_ALLOCATOR_RESULT(res);

    u64 current_index_allocation_offset  = 0;
    u64 current_vertex_allocation_offset = 0;

    u8 *buffer_data = (u8*)file_read_bin_temp_large(gltf.buffers[0].uri, gltf.buffers[0].byte_length);
    u64 tmp_offset;
    for(u32 i = 0; i < buffer_view_count; ++i) {
        switch(buffer_views[i].type) {
        case BUFFER_VIEW_DATA_TYPE_INDEX:
        {
            res = continue_allocation(&allocs->index, buffer_views[i].size, buffer_data + buffer_views[i].offset);
            CHECK_GPU_ALLOCATOR_RESULT(res);

            buffer_views[i].offset           = current_index_allocation_offset;
            current_index_allocation_offset += buffer_views[i].size;

            break;
        }
        case BUFFER_VIEW_DATA_TYPE_VERTEX:
        {
            res = continue_allocation(&allocs->vertex, buffer_views[i].size, buffer_data + buffer_views[i].offset);
            CHECK_GPU_ALLOCATOR_RESULT(res);

            buffer_views[i].offset            = current_vertex_allocation_offset;
            current_vertex_allocation_offset += buffer_views[i].size;

            break;
        }
        default:
            continue;
        }
    }

    res = submit_allocation(&allocs->index,  &ret.index_key);
    CHECK_GPU_ALLOCATOR_RESULT(res);
    res = submit_allocation(&allocs->vertex, &ret.vertex_key);
    CHECK_GPU_ALLOCATOR_RESULT(res);

    // Offset the data into the allocation.
    ret.offset_index      += buffer_views[buffer_view_index].offset;
    ret.offset_position   += buffer_views[buffer_view_position].offset;
    ret.offset_normal     += buffer_views[buffer_view_normal].offset;
    ret.offset_tangent    += buffer_views[buffer_view_tangent].offset;
    ret.offset_tex_coords += buffer_views[buffer_view_tex_coords].offset;

    return ret;
}

// @Unimplemented
Model_Player load_model_player(Model_Allocators *allocs, String *model_file_name, String *model_dir_name) {
    return {};
}
// @Unimplemented
void free_model_player(Model *model) {}

Model load_model(Model_Allocators *allocs, Model_Identifier model_id) {
    u32 model_index = (u32)model_id.id;

    Model ret;

    switch(model_id.type) {
    case MODEL_TYPE_CUBE:
    {
        ret.cube = load_model_cube(allocs, &g_model_file_names[model_index], &g_model_dir_names[model_index]);
        ret.cube.type = MODEL_TYPE_CUBE;
        break;
    }
    case MODEL_TYPE_PLAYER:
    {
        // @Unimplemented
        ret.player = load_model_player(allocs, &g_model_file_names[model_index], &g_model_dir_names[model_index]);
        ret.player.type = MODEL_TYPE_PLAYER;
        break;
    }
    default:
        assert(false && "Invalid Model Type");
    }
    return ret;
}

void free_model(Model *model, Model_Type type) {
    switch(type) {
    case MODEL_TYPE_CUBE:
    {
        // Cube does not require any allocation
        break;
    }
    case MODEL_TYPE_PLAYER:
    {
        // @Unimplemented
        // free_model_player(model);
        break;
    }
    default:
        assert(false && "Invalid Model Type");
    }
}
