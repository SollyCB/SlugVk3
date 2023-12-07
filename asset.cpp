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
    assert(creation_result == GPU_ALLOCATOR_RESULT_SUCCESS);

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
    assert(creation_result == GPU_ALLOCATOR_RESULT_SUCCESS);

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
    assert(creation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
    if (creation_result != GPU_ALLOCATOR_RESULT_SUCCESS) {
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

    Model_Allocators *model_allocators = &get_assets_instance()->model_allocators;
    *model_allocators = ret;

    return ret;
}
static void shutdown_model_allocators() {
    Model_Allocators *allocs = &get_assets_instance()->model_allocators;

    destroy_allocator(&allocs->index);
    destroy_allocator(&allocs->vertex);
    destroy_tex_allocator(&allocs->tex);
    destroy_sampler_allocator(&allocs->sampler);
}

void init_assets() {
    Model_Allocators_Config config = {};
    init_model_allocators(&config);

    Assets           *g_assets = get_assets_instance();
    Model_Allocators *allocs   = &g_assets->model_allocators;

    g_assets->model_count = g_model_count;
    g_assets->models      = (Model*)malloc_h(sizeof(Model) * g_model_count, 8);

    for(u32 i = 0; i < g_model_count; ++i) {
        g_assets->models[i] = load_model(g_model_identifiers[i]);
    }

    g_assets->semaphores[0] = create_semaphore();
    g_assets->semaphores[1] = create_semaphore();
    g_assets->fences[0]     = create_fence();
    g_assets->fences[1]     = create_fence();

    Gpu *gpu        = get_gpu_instance();
    VkDevice device = gpu->device;

    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = gpu->transfer_queue_index;

    auto check = vkCreateCommandPool(device, &pool_info, ALLOCATION_CALLBACKS, &g_assets->cmd_pools[0]);
    DEBUG_OBJ_CREATION(vkCreateCommandPool, check);
    auto check = vkCreateCommandPool(device, &pool_info, ALLOCATION_CALLBACKS, &g_assets->cmd_pools[1]);
    DEBUG_OBJ_CREATION(vkCreateCommandPool, check);

    VkCommandBufferAllocateInfo cmd_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_info.commandPool        = g_assets->cmd_pools[0];
    cmd_info.commandBufferCount = 1;

    check = vkAllocateCommandBuffers(device, &cmd_info, &g_assets->cmd_buffers[0]);
    DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

    cmd_info.commandPool = g_assets->cmd_pools[1];
    check = vkAllocateCommandBuffers(device, &cmd_info, &g_assets->cmd_buffers[1]);
    DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);
}
void kill_assets() {
    Assets *g_assets = get_assets_instance();

    for(u32 i = 0; i < g_assets->model_count; ++i)
        free_model(&g_assets->models[i]);

    free_h(g_assets->models);
    shutdown_model_allocators();

    destroy_semaphore(g_assets->semaphores[0]);
    destroy_semaphore(g_assets->semaphores[1]);
    destroy_fence(g_assets->fences[0]);
    destroy_fence(g_assets->fences[1]);
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

Model load_model(Model_Identifier model_id) {
    u32 model_index = (u32)model_id.id;

    Model ret;
    Model_Allocators *allocs = &get_assets_instance()->model_allocators;

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

void free_model(Model *model) {
    Model_Type type = model->cube.type; // @Note type must always appear as the first field in a model struct

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

static Gpu_Allocator_Result model_queue_cube(
    Model_Allocators                    *allocs,
    Model                               *model,
    Gpu_Allocator_Queue_Add_Func         queue_add_func,
    Gpu_Tex_Allocator_Queue_Add_Func     tex_queue_add_func,
    Gpu_Allocator_Queue_Remove_Func      queue_rm_func,
    Gpu_Tex_Allocator_Queue_Remove_Func  tex_queue_rm_func)
{
    Gpu_Allocator_Result res[4];

    res[0] = queue_add_func(&allocs->index,  model->cube.index_key);
    res[1] = queue_add_func(&allocs->vertex, model->cube.vertex_key);
    res[2] = tex_queue_add_func(&allocs->tex, model->cube.tex_key_base);
    res[3] = tex_queue_add_func(&allocs->tex, model->cube.tex_key_pbr);

    if (res[0] == GPU_ALLOCATOR_RESULT_QUEUE_FULL || res[1] == GPU_ALLOCATOR_RESULT_QUEUE_FULL ||
        res[2] == GPU_ALLOCATOR_RESULT_QUEUE_FULL || res[3] == GPU_ALLOCATOR_RESULT_QUEUE_FULL)
    {
        queue_rm_func(&allocs->index,  model->cube.index_key);
        queue_rm_func(&allocs->vertex, model->cube.vertex_key);
        tex_queue_rm_func(&allocs->tex, model->cube.tex_key_base);
        tex_queue_rm_func(&allocs->tex, model->cube.tex_key_pbr);

        return GPU_ALLOCATOR_RESULT_QUEUE_FULL;
    }

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

static u32 call_allocator_queue_functions_according_to_model_type(
    Model_Allocators                    *allocs,
    u32                                  count,
    Model                               *models,
    Gpu_Allocator_Queue_Add_Func         queue_add_func,
    Gpu_Tex_Allocator_Queue_Add_Func     tex_queue_add_func,
    Gpu_Allocator_Queue_Remove_Func      queue_rm_func,
    Gpu_Tex_Allocator_Queue_Remove_Func  tex_queue_rm_func)
{
    Gpu_Allocator_Result res = GPU_ALLOCATOR_RESULT_SUCCESS;
    u32 i;
    for(i = 0; i < count && res == GPU_ALLOCATOR_RESULT_SUCCESS; ++i) {
        switch(models[i].cube.type) { // Use any union member to find type
        case MODEL_TYPE_CUBE:
        {
            res = model_queue_cube(allocs, &models[i], queue_add_func, tex_queue_add_func,
                                   queue_rm_func, tex_queue_rm_func);
            break;
        }
        case MODEL_TYPE_PLAYER:
        {
            assert(false && "Unimplemented");
            return Max_u32;
        }
        case MODEL_TYPE_BUILDING:
        {
            assert(false && "Unimplemented");
            return Max_u32;
        }
        default: // Really this should not be an error, so I won't do so much to handle it
            assert(false && "Invalid Model Type");
            return Max_u32;
        }
    }
    return i;
}

enum Model_Upload_Phase {
    MODEL_UPLOAD_PHASE_QUEUE_STAGING  = 0,
    MODEL_UPLOAD_PHASE_SUBMIT_STAGING = 1,
    MODEL_UPLOAD_PHASE_QUEUE_UPLOAD   = 2,
    MODEL_UPLOAD_PHASE_SUBMIT_UPLOAD  = 3,
    MODEL_UPLOAD_PHASE_COMPLETE       = 4,
};
struct Model_Upload_Result {
    u32                count;
    Model_Upload_Phase phase;
};

static Model_Upload_Result upload_models(Model_Allocators *allocs, u32 count, Model *models,
                                         Model_Upload_Phase phase)
{
    Model_Upload_Result  ret = {};

    switch(phase) {
    case MODEL_UPLOAD_PHASE_QUEUE_STAGING: // Deliberate fall-through
    {
        ret.count = 0;
        ret.phase = MODEL_UPLOAD_PHASE_QUEUE_STAGING;

        if (staging_queue_begin(&allocs->index)   != GPU_ALLOCATOR_RESULT_SUCCESS ||
            staging_queue_begin(&allocs->vertex)  != GPU_ALLOCATOR_RESULT_SUCCESS ||
            tex_staging_queue_begin(&allocs->tex) != GPU_ALLOCATOR_RESULT_SUCCESS)
        {
            return ret;
        }

        ret.count = call_allocator_queue_functions_according_to_model_type(
                                   allocs,
                                   count,
                                   models,
                                   staging_queue_add,
                                   tex_staging_queue_add,
                                   staging_queue_remove,
                                   tex_staging_queue_remove);

        if (ret.count != count) {
            return ret;
        }
    }
    case MODEL_UPLOAD_PHASE_SUBMIT_STAGING: // Deliberate fall-through
    {
        ret.phase = MODEL_UPLOAD_PHASE_SUBMIT_STAGING;

        if (staging_queue_submit(&allocs->index)   != GPU_ALLOCATOR_RESULT_SUCCESS ||
            staging_queue_submit(&allocs->vertex)  != GPU_ALLOCATOR_RESULT_SUCCESS ||
            tex_staging_queue_submit(&allocs->tex) != GPU_ALLOCATOR_RESULT_SUCCESS)
        {
            return ret;
        }
    }
    case MODEL_UPLOAD_PHASE_QUEUE_UPLOAD: // Deliberate fall-through
    {
        ret.count = 0;
        ret.phase = MODEL_UPLOAD_PHASE_QUEUE_UPLOAD;

        if (upload_queue_begin(&allocs->index)   != GPU_ALLOCATOR_RESULT_SUCCESS ||
            upload_queue_begin(&allocs->vertex)  != GPU_ALLOCATOR_RESULT_SUCCESS ||
            tex_upload_queue_begin(&allocs->tex) != GPU_ALLOCATOR_RESULT_SUCCESS)
        {
            return ret;
        }

        ret.count = call_allocator_queue_functions_according_to_model_type(
                                   allocs,
                                   count,
                                   models,
                                   upload_queue_add,
                                   tex_upload_queue_add,
                                   upload_queue_remove,
                                   tex_upload_queue_remove);

        if (ret.count != count) {
            return ret;
        }
    }
    case MODEL_UPLOAD_PHASE_SUBMIT_UPLOAD: // Deliberate fall-through
    {
        ret.phase = MODEL_UPLOAD_PHASE_SUBMIT_UPLOAD;

        if (upload_queue_submit(&allocs->index)   != GPU_ALLOCATOR_RESULT_SUCCESS ||
            upload_queue_submit(&allocs->vertex)  != GPU_ALLOCATOR_RESULT_SUCCESS ||
            tex_upload_queue_submit(&allocs->tex) != GPU_ALLOCATOR_RESULT_SUCCESS)
        {
            return ret;
        }
    }
    case MODEL_UPLOAD_PHASE_COMPLETE:
    {
        break;
    }
    default:
        assert(false && "Invalid Upload Phase");
    } // switch upload phase

    ret.count = count;
    ret.phase = MODEL_UPLOAD_PHASE_COMPLETE;
    return ret;
}

void make_staging_queues_empty(Model_Allocators *allocs) {
    staging_queue_make_empty(&allocs->index);
    staging_queue_make_empty(&allocs->vertex);
    tex_staging_queue_make_empty(&allocs->tex);
}

Model_Upload_Result prepare_to_draw_models(u32 count, Model *models, Model_Upload_Result last_result) {
    Assets *g_assets = get_assets_instance();
    Model_Allocators *model_allocs = &g_assets->model_allocators;

    u32 tmp;
    Model_Upload_Result res;

    // Restore upload state from last call
    res = upload_models(model_allocs, count - last_result.count, models + last_result.count, last_result.phase);

    //
    // @Note This is just some arbitrary system to show I can consider handling cases upload fail.
    // When/If I have real use cases then I can better judge how I want to deal with failures. Right now
    // I will just take this system of "If something got into the queue, submit it; If submission fails, retry
    // with decreasing model count, and on each retry increase the decrement size". I know this is absolutely
    // not the most efficient method of doing this (emptying queues and then requeueing a number of times is ofc
    // not desirable), but it is fine for now.
    //
    // The idea behind this way of doing it is that the vast majority of the time, there should be very little
    // friction to upload allocations, as queue uploads and draw completions should be effectively staggered.
    // Therefore the expensive retries should be hit bery rarely; and if they are being hit then other areas
    // should be adjusted first, like whatever manages scheduling. It is also intended to be able to manage
    // varying memory sizes: so you likely have some model count that you optimally want upload at some
    // stage in the frame, and generally this will always work, but this system should the handle the case where
    // this model count needs to separated out across upload phases. Again, this current method is just a simple
    // way of doing that. I am sure there is a better way, especially if you properly know the use case for the
    // engine.
    //
    while(res.phase != MODEL_UPLOAD_PHASE_COMPLETE) {

        switch(res.phase) {
        case MODEL_UPLOAD_PHASE_QUEUE_STAGING: // Deliberate fall-through
        {
            // @Note @Test Maybe want a threshold here, as in the queue must be as large as some size in order
            // to warrant an upload, even if just to know if we are accidentally bottlenecking due to not
            // staggering drawing and queue uploads properly.
            if (res.count) {
                res = upload_models(model_allocs, count - last_result.count, models + last_result.count,
                                    MODEL_UPLOAD_PHASE_SUBMIT_STAGING);
            } else {
                return res;
            }
        }
        case MODEL_UPLOAD_PHASE_SUBMIT_STAGING: // Deliberate fall-through
        {
            tmp = 1;
            while(true) {
                if (((count - last_result.count) >> tmp) == 0)
                    return res;

                make_staging_queues_empty(model_allocs);

                res = upload_models(model_allocs, (count - last_result) >> tmp, models + last_result.count,
                                    MODEL_UPLOAD_PHASE_QUEUE_STAGING);

                res = upload_models(model_allocs, count, models, MODEL_UPLOAD_PHASE_SUBMIT_STAGING);

                if (res.phase > MODEL_UPLOAD_PHASE_SUBMIT_STAGING)
                    break;

                tmp++;
            }
        }
        case MODEL_UPLOAD_PHASE_QUEUE_UPLOAD: // Deliberate fall-through
        {
            // @Note @Test Maybe want a threshold here, as in the queue must be as large as some size in order
            // to warrant an upload, even if just to know if we are accidentally bottlenecking due to not
            // staggering drawing and queue uploads properly.
            if (res.count) {
                res = upload_models(model_allocs, count - last_result.count, models + last_result.count,
                                    MODEL_UPLOAD_PHASE_SUBMIT_STAGING);
            } else {
                return res;
            }
        }
        case MODEL_UPLOAD_PHASE_SUBMIT_UPLOAD: // Deliberate fall-through
        {
            tmp = 1;
            while(true) {
                if (((count - last_result.count) >> tmp) == 0)
                    return res;

                make_staging_queues_empty(model_allocs);

                res = upload_models(model_allocs, (count - last_result) >> tmp, models + last_result.count,
                                    MODEL_UPLOAD_PHASE_QUEUE_UPLOAD);

                res = upload_models(model_allocs, count, models, MODEL_UPLOAD_PHASE_SUBMIT_UPLOAD);

                if (res.phase > MODEL_UPLOAD_PHASE_SUBMIT_UPLOAD)
                    break;

                tmp++;
            }
        }
        case MODEL_UPLOAD_PHASE_COMPLETE: // Deliberate fall-through
        {
            // Continue to the rest of the function
        }
        }
    }

    Gpu *gpu = get_gpu_instance();
    if (gpu->memory.flags & GPU_MEMORY_UMA_BIT == 0) {
        VkCommandBufferBeginInfo cmd_begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkCommandBuffer cmd = g_assets->cmd_buffers[g_frame_index];
        vkBeginCommandBuffer(cmd, &cmd_begin_info);

        VkCommandBuffer secondary_cmds[] = {
            allocs->index.transfer_cmds[g_frame_index],
            allocs->vertex.transfer_cmds[g_frame_index],
            allocs->tex.transfer_cmds[g_frame_index],
        };

        vkCmdExecuteCommands(cmd, 3, secondary_cmds);

        vkEndCommandBuffer(cmd);
    }

    return res;
}
