#include "asset.hpp"
#include "gltf.hpp"
#include "file.hpp"
#include "array.hpp"
#include "gpu.hpp"
#include "model.hpp"
#include "vulkan/vulkan_core.h"
#include "vulkan_errors.hpp"
#include "simd.hpp"

#if TEST
#include "test/test.hpp"
#include "image.hpp"
#endif

static Assets s_Assets;
Assets* get_assets_instance() { return &s_Assets; }

struct Model_Allocators_Config {
    // Vertex Allocator
    u64 vertex_allocator_config_allocation_cap         = 2048;
    u64 vertex_allocator_config_to_stage_cap           = 64;
    u64 vertex_allocator_config_to_upload_cap          = 32;
    u64 vertex_allocator_config_stage_bit_granularity  = 256;
    u64 vertex_allocator_config_upload_bit_granularity = 256;

    u64 vertex_allocator_config_staging_queue_byte_cap = VERTEX_STAGE_SIZE;
    u64 vertex_allocator_config_upload_queue_byte_cap  = VERTEX_DEVICE_SIZE;

    u64 vertex_allocator_config_stage_cap              = VERTEX_STAGE_SIZE;
    u64 vertex_allocator_config_upload_cap             = VERTEX_DEVICE_SIZE;

    String    vertex_allocator_config_disk_storage = cstr_to_string("allocator-files/vertex_allocator_file.bin");
    void     *vertex_allocator_config_stage_ptr    = get_gpu_instance()->memory.vertex_ptrs[0];
    VkBuffer  vertex_allocator_config_stage        = get_gpu_instance()->memory.vertex_bufs_stage[0];
    VkBuffer  vertex_allocator_config_upload       = get_gpu_instance()->memory.vertex_buf_device;

    // Index Allocator
    u64 index_allocator_config_allocation_cap         = 2048;
    u64 index_allocator_config_to_stage_cap           = 64;
    u64 index_allocator_config_to_upload_cap          = 32;
    u64 index_allocator_config_stage_bit_granularity  = 256;
    u64 index_allocator_config_upload_bit_granularity = 256;

    u64 index_allocator_config_staging_queue_byte_cap = INDEX_STAGE_SIZE;
    u64 index_allocator_config_upload_queue_byte_cap  = INDEX_DEVICE_SIZE;
    u64 index_allocator_config_stage_cap              = INDEX_STAGE_SIZE;
    u64 index_allocator_config_upload_cap             = INDEX_DEVICE_SIZE;

    String    index_allocator_config_disk_storage = cstr_to_string("allocator-files/index_allocator_file.bin");
    void     *index_allocator_config_stage_ptr    = get_gpu_instance()->memory.index_ptrs[0];
    VkBuffer  index_allocator_config_stage        = get_gpu_instance()->memory.index_bufs_stage[0];
    VkBuffer  index_allocator_config_upload       = get_gpu_instance()->memory.index_buf_device;

    // Tex Allocator
    u64 tex_allocator_config_allocation_cap         = 2048;
    u64 tex_allocator_config_to_stage_cap           = 64;
    u64 tex_allocator_config_to_upload_cap          = 32;
    u64 tex_allocator_config_stage_bit_granularity  = 256 * 4;
    u64 tex_allocator_config_upload_bit_granularity = 256 * 4;
    u64 tex_allocator_config_string_buffer_size     = 1024;

    u64 tex_allocator_config_staging_queue_byte_cap = TEXTURE_STAGE_SIZE;
    u64 tex_allocator_config_upload_queue_byte_cap  = TEXTURE_DEVICE_SIZE;
    u64 tex_allocator_config_stage_cap              = TEXTURE_STAGE_SIZE;
    u64 tex_allocator_config_upload_cap             = TEXTURE_DEVICE_SIZE;

    void           *tex_allocator_config_stage_ptr = get_gpu_instance()->memory.texture_ptrs[0];
    VkBuffer        tex_allocator_config_stage     = get_gpu_instance()->memory.texture_bufs_stage[0];
    VkDeviceMemory  tex_allocator_config_upload    = get_gpu_instance()->memory.texture_mem_device;

    // Image View and Sampler Allocators
    u64 sampler_allocator_cap    = 0; // 0 makes the allocator use the device cap
    u64 image_view_allocator_cap = 256;

    // Descriptor Allocator
    u64   descriptor_allocator_cap_sampler  = DESCRIPTOR_BUFFER_SIZE;
    u64   descriptor_allocator_cap_resource = DESCRIPTOR_BUFFER_SIZE;
    void *descriptor_allocator_ptr_sampler  = get_gpu_instance()->memory.sampler_descriptor_ptr;
    void *descriptor_allocator_ptr_resource = get_gpu_instance()->memory.resource_descriptor_ptr;

    VkBuffer descriptor_allocator_buffer_sampler  = get_gpu_instance()->memory.sampler_descriptor_buffer;
    VkBuffer descriptor_allocator_buffer_resource = get_gpu_instance()->memory.resource_descriptor_buffer;

    void     **uniform_allocator_ptrs    = get_gpu_instance()->memory.uniform_ptrs;
    VkBuffer  *uniform_allocator_buffers = get_gpu_instance()->memory.uniform_buffers;
};

static Model_Allocators create_model_allocators(const Model_Allocators_Config *config);
static void             destroy_model_allocators(Model_Allocators *model_allocators);

void init_assets() {
    Assets *g_assets = get_assets_instance();

    Model_Allocators_Config config = {}; // All defaults, see creation function
    g_assets->model_allocators     = create_model_allocators(&config);

    Model_Allocators *allocs = &g_assets->model_allocators;

    g_assets->model_buffer =    (u8*)malloc_h(g_model_buffer_size, 16);
    g_assets->models       = (Model*)malloc_h(sizeof(Model) * g_model_count, 16);

    u64 model_buffer_size_used      = 0;
    u64 model_buffer_size_available = g_model_buffer_size;

    #if MODEL_LOAD_INFO
    println("Loading %u models; model buffer size %u", g_model_count, g_model_buffer_size);
    #endif

    // Defaults must be the first allocations.
    String default_texture_name = cstr_to_string("models/default/default_texture.png");
    u32    default_texture_key; // Must be zero.

    Gpu_Allocator_Result allocator_result = tex_add_texture(&allocs->tex, &default_texture_name, &default_texture_key);
    assert(default_texture_key == 0);

    u64 tmp_size;
    for(u32 i = 0; i < g_model_count; ++i) {
        g_assets->models[i] = model_from_gltf(
                                   allocs,
                                  &g_model_dir_names[i],
                                  &g_model_file_names[i],
                                   model_buffer_size_available,
                                   g_assets->model_buffer,
                                  &tmp_size);

        model_buffer_size_used      += tmp_size;
        model_buffer_size_available -= model_buffer_size_used;
        assert(g_model_buffer_size  >= model_buffer_size_used)

        g_assets->model_count++;
    }

    g_assets->semaphores[0] = create_semaphore();
    g_assets->semaphores[1] = create_semaphore();
    g_assets->fences[0]     = create_fence(false);
    g_assets->fences[1]     = create_fence(false);

    Gpu *gpu        = get_gpu_instance();
    VkDevice device = gpu->device;

    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = gpu->transfer_queue_index;

    VkCommandBufferAllocateInfo cmd_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_info.commandBufferCount = 1;

    VkResult check;
    for(u32 i = 0; i < g_frame_count; ++i) {
        check = vkCreateCommandPool(device, &pool_info, ALLOCATION_CALLBACKS, &g_assets->cmd_pools[i]);
        DEBUG_OBJ_CREATION(vkCreateCommandPool, check);

        cmd_info.commandPool = g_assets->cmd_pools[i];
        check = vkAllocateCommandBuffers(device, &cmd_info, &g_assets->cmd_buffers[i]);
        DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);
    }
}

void kill_assets() {
    Assets *g_assets = get_assets_instance();

    free_h(g_assets->model_buffer);
    free_h(g_assets->models);
    destroy_model_allocators(&g_assets->model_allocators);

    destroy_semaphore(g_assets->semaphores[0]);
    destroy_semaphore(g_assets->semaphores[1]);
    destroy_fence(g_assets->fences[0]);
    destroy_fence(g_assets->fences[1]);

    VkDevice device = get_gpu_instance()->device;
    for(u32 i = 0; i < g_frame_count; ++i)
        vkDestroyCommandPool(device, g_assets->cmd_pools[i], ALLOCATION_CALLBACKS);
}

static Model_Allocators create_model_allocators(const Model_Allocators_Config *config) {
    Gpu *gpu = get_gpu_instance();

    // Vertex allocator
    Gpu_Allocator_Config vertex_allocator_config = {};

    vertex_allocator_config.allocation_cap         = config->vertex_allocator_config_allocation_cap;
    vertex_allocator_config.to_stage_cap           = config->vertex_allocator_config_to_stage_cap;
    vertex_allocator_config.to_upload_cap          = config->vertex_allocator_config_to_upload_cap;
    vertex_allocator_config.stage_bit_granularity  = config->vertex_allocator_config_stage_bit_granularity;
    vertex_allocator_config.upload_bit_granularity = config->vertex_allocator_config_upload_bit_granularity;

    vertex_allocator_config.staging_queue_byte_cap = config->vertex_allocator_config_staging_queue_byte_cap;
    vertex_allocator_config.upload_queue_byte_cap  = config->vertex_allocator_config_upload_queue_byte_cap;
    vertex_allocator_config.stage_cap              = config->vertex_allocator_config_stage_cap;
    vertex_allocator_config.upload_cap             = config->vertex_allocator_config_upload_cap;

    vertex_allocator_config.disk_storage           = config->vertex_allocator_config_disk_storage;
    vertex_allocator_config.stage_ptr              = config->vertex_allocator_config_stage_ptr;
    vertex_allocator_config.stage                  = config->vertex_allocator_config_stage;
    vertex_allocator_config.upload                 = config->vertex_allocator_config_upload;

    Gpu_Allocator vertex_allocator;
    Gpu_Allocator_Result creation_result = create_allocator(&vertex_allocator_config, &vertex_allocator);
    CHECK_GPU_ALLOCATOR_RESULT(creation_result);

    // Index allocator
    Gpu_Allocator_Config index_allocator_config = {};

    index_allocator_config.allocation_cap         = config->index_allocator_config_allocation_cap;
    index_allocator_config.to_stage_cap           = config->index_allocator_config_to_stage_cap;
    index_allocator_config.to_upload_cap          = config->index_allocator_config_to_upload_cap;
    index_allocator_config.stage_bit_granularity  = config->index_allocator_config_stage_bit_granularity;
    index_allocator_config.upload_bit_granularity = config->index_allocator_config_upload_bit_granularity;

    index_allocator_config.staging_queue_byte_cap = config->index_allocator_config_staging_queue_byte_cap;
    index_allocator_config.upload_queue_byte_cap  = config->index_allocator_config_upload_queue_byte_cap;
    index_allocator_config.stage_cap              = config->index_allocator_config_stage_cap;
    index_allocator_config.upload_cap             = config->index_allocator_config_upload_cap;

    index_allocator_config.disk_storage           = config->index_allocator_config_disk_storage;
    index_allocator_config.stage_ptr              = config->index_allocator_config_stage_ptr;
    index_allocator_config.stage                  = config->index_allocator_config_stage;
    index_allocator_config.upload                 = config->index_allocator_config_upload;

    Gpu_Allocator index_allocator;
    creation_result = create_allocator(&index_allocator_config, &index_allocator);
    CHECK_GPU_ALLOCATOR_RESULT(creation_result);

    // Tex allocator
    Gpu_Tex_Allocator_Config tex_allocator_config = {};

    tex_allocator_config.allocation_cap         = config->tex_allocator_config_allocation_cap;
    tex_allocator_config.to_stage_cap           = config->tex_allocator_config_to_stage_cap;
    tex_allocator_config.to_upload_cap          = config->tex_allocator_config_to_upload_cap;
    tex_allocator_config.stage_bit_granularity  = config->tex_allocator_config_stage_bit_granularity;
    tex_allocator_config.upload_bit_granularity = config->tex_allocator_config_upload_bit_granularity;
    tex_allocator_config.string_buffer_size     = config->tex_allocator_config_string_buffer_size;

    tex_allocator_config.staging_queue_byte_cap = config->tex_allocator_config_staging_queue_byte_cap;
    tex_allocator_config.upload_queue_byte_cap  = config->tex_allocator_config_upload_queue_byte_cap;
    tex_allocator_config.stage_cap              = config->tex_allocator_config_stage_cap;
    tex_allocator_config.upload_cap             = config->tex_allocator_config_upload_cap;

    tex_allocator_config.stage_ptr              = config->tex_allocator_config_stage_ptr;
    tex_allocator_config.stage                  = config->tex_allocator_config_stage;
    tex_allocator_config.upload                 = config->tex_allocator_config_upload;

    Gpu_Tex_Allocator tex_allocator;
    creation_result = create_tex_allocator(&tex_allocator_config, &tex_allocator);
    CHECK_GPU_ALLOCATOR_RESULT(creation_result);

    Sampler_Allocator    sampler    = create_sampler_allocator(config->sampler_allocator_cap);
    Image_View_Allocator image_view = create_image_view_allocator(config->image_view_allocator_cap);

    Model_Allocators ret = {
        .index      = index_allocator,
        .vertex     = vertex_allocator,
        .tex        = tex_allocator,
        .sampler    = sampler,
        .image_view = image_view,
    };

    u32 allocator_count = g_thread_count * g_frame_count;

    // @Todo This takes the lower bound, which ensures no overflow, but wastes some memory. So the
    // cap should be aligned to the thread count.
    u64 size_sampler  = config->descriptor_allocator_cap_sampler / allocator_count;
    u64 size_resource = config->descriptor_allocator_cap_resource / allocator_count;

    u64 ptr_offset_sampler  = 0;
    u64 ptr_offset_resource = 0;
    for(u32 i = 0; i < allocator_count; ++i) {
        ret.descriptor_sampler[i] = get_descriptor_allocator(
                                        size_sampler,
                                        (u8*)config->descriptor_allocator_ptr_sampler + ptr_offset_sampler,
                                        config->descriptor_allocator_buffer_sampler);
        ret.descriptor_resource[i] = get_descriptor_allocator(
                                         size_resource,
                                         (u8*)config->descriptor_allocator_ptr_resource + ptr_offset_resource,
                                         config->descriptor_allocator_buffer_resource);

        ptr_offset_sampler  += size_sampler;
        ptr_offset_resource += size_resource;
    }

    for(u32 i = 0; i < allocator_count; ++i) {
        ret.uniform[i] = get_uniform_allocator(
                             UNIFORM_BUFFER_SIZE,
                             config->uniform_allocator_ptrs[i],
                             config->uniform_allocator_buffers[i]);
    }

    return ret;
}
static void destroy_model_allocators(Model_Allocators *allocs) {
    destroy_allocator(&allocs->index);
    destroy_allocator(&allocs->vertex);
    destroy_tex_allocator(&allocs->tex);
    destroy_sampler_allocator(&allocs->sampler);
    destroy_image_view_allocator(&allocs->image_view);
}

struct Model_Req_Size_Info {
    u32 total;
    u32 accessors;
    u32 primitives;
    u32 weights;
};
static Model_Req_Size_Info model_get_required_size_from_gltf(Gltf *gltf) {
    assert(gltf_buffer_get_count(gltf) == 1 && "Ugh, need to make buffers an array");

    // @Todo Animations, Skins, Cameras

    u32 accessor_count    = gltf_accessor_get_count(gltf);
    u32 mesh_count        = gltf_mesh_get_count(gltf);

    // Accessors: min/max and sparse
    u32 sparse_count  = 0; // The number of accessor sparse structures
    u32 max_min_count = 0;

    const Gltf_Accessor *gltf_accessor = gltf->accessors;
    for(u32 i = 0; i < accessor_count; ++i) {
        sparse_count  += gltf_accessor->sparse_count != 0;
        max_min_count += gltf_accessor->max != NULL;

        gltf_accessor = (const Gltf_Accessor*)((u8*)gltf_accessor + gltf_accessor->stride);
    }

    // Meshes: weights, primitive attributes, morph targets
    u32 weight_count           = 0;
    u32 primitive_count        = 0;
    u32 attribute_count        = 0;
    u32 target_count           = 0;
    u32 target_attribute_count = 0;

    const Gltf_Mesh           *gltf_mesh = gltf->meshes;
    const Gltf_Mesh_Primitive *gltf_primitive;
    const Gltf_Morph_Target   *gltf_morph_target;
    for(u32 i = 0; i < mesh_count; ++i) {

        weight_count    += gltf_mesh->weight_count;
        primitive_count += gltf_mesh->primitive_count;

        gltf_primitive = gltf_mesh->primitives;

        for(u32 j = 0; j < gltf_mesh->primitive_count; ++j) {
            // I am glad that I dont *have* to redo the gltf parser, but I made some annoying decisions...
            attribute_count += gltf_primitive->position    != -1;
            attribute_count += gltf_primitive->normal      != -1;
            attribute_count += gltf_primitive->tangent     != -1;
            attribute_count += gltf_primitive->tex_coord_0 != -1;

            attribute_count += gltf_primitive->extra_attribute_count;

            target_count      += gltf_primitive->target_count;
            gltf_morph_target  = gltf_primitive->targets;

            for(u32 k = 0; k < gltf_primitive->target_count; ++k) {
                target_attribute_count += gltf_morph_target->attribute_count;

                gltf_morph_target = (const Gltf_Morph_Target*)((u8*)gltf_morph_target + gltf_morph_target->stride);
            }

            gltf_primitive = (const Gltf_Mesh_Primitive*)((u8*)gltf_primitive + gltf_primitive->stride);
        }

        gltf_mesh = (const Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }


    //
    // @Todo This count may not be completely accurate. Idk if there will the stuff counted that I will not
    // use?? Idk... need to check at some point. It is not a huge deal as I cannot see anything being counted
    // here that would be catastrophic.
    //
    u32 req_size_accessors  = 0;
    req_size_accessors     += sparse_count  * sizeof(Accessor_Sparse);
    req_size_accessors     += max_min_count * sizeof(Accessor_Max_Min);

    u32 req_size_weights  = 0;
    req_size_weights     += weight_count * sizeof(float);

    u32 req_size_primitives  = 0;
    req_size_primitives     +=  primitive_count        * sizeof(Mesh_Primitive);
    req_size_primitives     +=  attribute_count        * sizeof(Mesh_Primitive_Attribute);
    req_size_primitives     +=  target_count           * sizeof(Morph_Target);
    req_size_primitives     +=  target_attribute_count * sizeof(Mesh_Primitive_Attribute);

    u32 req_size  = 0;
    req_size     += req_size_accessors;
    req_size     += req_size_weights;
    req_size     += req_size_primitives;

    req_size += sizeof(Mesh) * mesh_count;

    Model_Req_Size_Info ret = {
        .total      = req_size,
        .accessors  = req_size_accessors,
        .primitives = req_size_primitives,
        .weights    = req_size_weights,
    };

    return ret;
}

inline static Accessor_Flag_Bits translate_gltf_accessor_type_to_bits(Gltf_Accessor_Type type, u32 *ret_size) {

    Accessor_Flags ret = 0x0;
    *ret_size = 0;

    ret |= (Accessor_Flags)(ACCESSOR_TYPE_SCALAR_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_SCALAR == type));
    *ret_size += 1 & max32_if_true(GLTF_ACCESSOR_TYPE_SCALAR == type);

    ret |= (Accessor_Flags)(ACCESSOR_TYPE_VEC2_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_VEC2 == type));
    *ret_size += 2 & max32_if_true(GLTF_ACCESSOR_TYPE_VEC2 == type);

    ret |= (Accessor_Flags)(ACCESSOR_TYPE_VEC3_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_VEC3 == type));
    *ret_size += 3 & max32_if_true(GLTF_ACCESSOR_TYPE_VEC3 == type);

    ret |= (Accessor_Flags)(ACCESSOR_TYPE_VEC4_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_VEC4 == type));
    *ret_size += 4 & max32_if_true(GLTF_ACCESSOR_TYPE_VEC4 == type);

    ret |= (Accessor_Flags)(ACCESSOR_TYPE_MAT2_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_MAT2 == type));
    *ret_size += 4 & max32_if_true(GLTF_ACCESSOR_TYPE_MAT2 == type);

    ret |= (Accessor_Flags)(ACCESSOR_TYPE_MAT3_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_MAT3 == type));
    *ret_size += 9 & max32_if_true(GLTF_ACCESSOR_TYPE_MAT3 == type);

    ret |= (Accessor_Flags)(ACCESSOR_TYPE_MAT4_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_MAT4 == type));
    *ret_size += 16 & max32_if_true(GLTF_ACCESSOR_TYPE_MAT4 == type);

    ret |= (Accessor_Flags)(ACCESSOR_COMPONENT_TYPE_SCHAR_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_BYTE == type));
    *ret_size += 1 & max32_if_true(GLTF_ACCESSOR_TYPE_BYTE == type);

    ret |= (Accessor_Flags)(ACCESSOR_COMPONENT_TYPE_UCHAR_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_UNSIGNED_BYTE == type));
    *ret_size += 1 & max32_if_true(GLTF_ACCESSOR_TYPE_UNSIGNED_BYTE == type);

    ret |= (Accessor_Flags)(ACCESSOR_COMPONENT_TYPE_S16_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_SHORT == type));
    *ret_size += 2 & max32_if_true(GLTF_ACCESSOR_TYPE_SHORT == type);

    ret |= (Accessor_Flags)(ACCESSOR_COMPONENT_TYPE_U16_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_UNSIGNED_SHORT == type));
    *ret_size += 2 & max32_if_true(GLTF_ACCESSOR_TYPE_UNSIGNED_SHORT == type);

    ret |= (Accessor_Flags)(ACCESSOR_COMPONENT_TYPE_U32_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_UNSIGNED_INT == type));
    *ret_size += 4 & max32_if_true(GLTF_ACCESSOR_TYPE_UNSIGNED_INT == type);

    ret |= (Accessor_Flags)(ACCESSOR_COMPONENT_TYPE_FLOAT_BIT & max32_if_true(GLTF_ACCESSOR_TYPE_FLOAT == type));
    *ret_size += 4 & max32_if_true(GLTF_ACCESSOR_TYPE_FLOAT == type);

    return (Accessor_Flag_Bits)ret;
}

static void model_load_gltf_accessors(u32 count, const Gltf_Accessor *gltf_accessors, Accessor *accessors, u8 *buffer) {
    u32 tmp_component_count;
    u32 tmp_component_width;
    u32 tmp;

    const Gltf_Accessor *gltf_accessor = gltf_accessors;

    u32 size_used = 0;
    for(u32 i = 0; i < count; ++i) {
        accessors[i] = {};

        accessors[i].flags |= translate_gltf_accessor_type_to_bits(gltf_accessor->type,           &tmp_component_count);
        accessors[i].flags |= translate_gltf_accessor_type_to_bits(gltf_accessor->component_type, &tmp_component_width);

        accessors[i].flags |= ACCESSOR_NORMALIZED_BIT  & max32_if_true(gltf_accessor->normalized);

        accessors[i].allocation_key = gltf_accessor->buffer_view;
        accessors[i].byte_stride    = gltf_accessor->byte_stride;
        accessors[i].byte_offset    = gltf_accessor->byte_offset;
        accessors[i].count          = gltf_accessor->count;

        if (gltf_accessor->max) {
            accessors[i].max_min  = (Accessor_Max_Min*)(buffer + size_used);
            size_used            += sizeof(Accessor_Max_Min);

            tmp = tmp_component_count * sizeof(float);
            memcpy(accessors[i].max_min->max, gltf_accessor->max, tmp);
            memcpy(accessors[i].max_min->min, gltf_accessor->min, tmp);
        }

        if (gltf_accessor->sparse_count) {
            accessors[i].sparse  = (Accessor_Sparse*)(buffer + size_used);
            size_used           += sizeof(Accessor_Sparse);

            accessors[i].sparse->indices_component_type = translate_gltf_accessor_type_to_bits(gltf_accessor->indices_component_type, &tmp);
            accessors[i].sparse->count                  = gltf_accessor->sparse_count;
            accessors[i].sparse->indices_allocation_key = gltf_accessor->indices_buffer_view;
            accessors[i].sparse->values_allocation_key  = gltf_accessor->values_buffer_view;
            accessors[i].sparse->indices_byte_offset    = gltf_accessor->indices_byte_offset;
            accessors[i].sparse->values_byte_offset     = gltf_accessor->values_byte_offset;
        }

        gltf_accessor = (const Gltf_Accessor*)((u8*)gltf_accessor + gltf_accessor->stride);
    }
}

static void model_load_gltf_textures(u32 count, const Gltf_Texture *gltf_textures, Texture *textures) {
    // Bro wtf are the C compiler spec writers doing!! How is this not a compilation error!!
    // Gltf_Texture *gltf_texture = gltf_texture;

    const Gltf_Texture *gltf_texture = gltf_textures;

    for(u32 i = 0; i < count; ++i) {
        // @Todo ktx2 textures for ready to go mipmaps
        textures[i] = {.texture_key = (u32)gltf_texture->source_image, .sampler_key = (u32)gltf_texture->sampler};

        gltf_texture = (const Gltf_Texture*)((u8*)gltf_texture + gltf_texture->stride);
    }
}

static void model_load_gltf_materials(u32 count, const Gltf_Material *gltf_materials, const Texture *textures, Material *materials) {

    const Gltf_Material *gltf_material = gltf_materials;

    u32 tmp;
    for(u32 i = 0; i < count; ++i) {
        materials[i] = {};

        materials[i].flags |= MATERIAL_BASE_BIT      & max32_if_true(gltf_material->base_color_texture_index         != -1);
        materials[i].flags |= MATERIAL_PBR_BIT       & max32_if_true(gltf_material->metallic_roughness_texture_index != -1);
        materials[i].flags |= MATERIAL_NORMAL_BIT    & max32_if_true(gltf_material->normal_texture_index             != -1);
        materials[i].flags |= MATERIAL_OCCLUSION_BIT & max32_if_true(gltf_material->occlusion_texture_index          != -1);
        materials[i].flags |= MATERIAL_EMISSIVE_BIT  & max32_if_true(gltf_material->emissive_texture_index           != -1);

        materials[i].flags |= MATERIAL_OPAQUE_BIT & max32_if_true(gltf_material->alpha_mode == GLTF_ALPHA_MODE_OPAQUE);
        materials[i].flags |= MATERIAL_MASK_BIT   & max32_if_true(gltf_material->alpha_mode == GLTF_ALPHA_MODE_MASK);
        materials[i].flags |= MATERIAL_BLEND_BIT  & max32_if_true(gltf_material->alpha_mode == GLTF_ALPHA_MODE_BLEND);

        // Misc
        materials[i].flags |= MATERIAL_DOUBLE_SIDED_BIT & max32_if_true(gltf_material->double_sided);
        materials[i].ubo.alpha_cutoff = gltf_material->alpha_cutoff;

        // Pbr
        materials[i].pbr = {};

        materials[i].ubo.base_color_factor[0] = gltf_material->base_color_factor[0];
        materials[i].ubo.base_color_factor[1] = gltf_material->base_color_factor[1];
        materials[i].ubo.base_color_factor[2] = gltf_material->base_color_factor[2];
        materials[i].ubo.base_color_factor[3] = gltf_material->base_color_factor[3];

        materials[i].ubo.metallic_factor  = gltf_material->metallic_factor;
        materials[i].ubo.roughness_factor = gltf_material->roughness_factor;

        // Base Color
        tmp = gltf_material->base_color_texture_index;
        materials[i].pbr.base_color_texture   = textures[tmp & max32_if_true(materials[i].flags & MATERIAL_BASE_BIT)];
        materials[i].pbr.base_color_tex_coord = gltf_material->base_color_tex_coord;

        // Metallic Roughness
        tmp = gltf_material->metallic_roughness_texture_index;
        materials[i].pbr.metallic_roughness_texture   = textures[tmp & max32_if_true(materials[i].flags & MATERIAL_PBR_BIT)];
        materials[i].pbr.metallic_roughness_tex_coord = gltf_material->metallic_roughness_tex_coord;

        // Normal
        tmp = gltf_material->normal_texture_index;
        materials[i].normal.texture   = textures[tmp & max32_if_true(materials[i].flags & MATERIAL_NORMAL_BIT)];
        materials[i].normal.tex_coord = gltf_material->normal_tex_coord;
        materials[i].ubo.normal_scale = gltf_material->normal_scale;

        // Occlusion
        tmp = gltf_material->occlusion_texture_index;
        materials[i].occlusion.texture      = textures[tmp & max32_if_true(materials[i].flags & MATERIAL_OCCLUSION_BIT)];
        materials[i].occlusion.tex_coord    = gltf_material->occlusion_tex_coord;
        materials[i].ubo.occlusion_strength = gltf_material->occlusion_strength;

        // Emissive
        tmp = gltf_material->emissive_texture_index;
        materials[i].emissive.texture   = textures[tmp & max32_if_true(materials[i].flags & MATERIAL_EMISSIVE_BIT)];
        materials[i].emissive.tex_coord = gltf_material->emissive_tex_coord;
        materials[i].ubo.emissive_factor[0] = gltf_material->emissive_factor[0];
        materials[i].ubo.emissive_factor[1] = gltf_material->emissive_factor[1];
        materials[i].ubo.emissive_factor[2] = gltf_material->emissive_factor[2];

        gltf_material = (const Gltf_Material*)((u8*)gltf_material + gltf_material->stride);
    }
}

struct Load_Mesh_Info {
    Accessor *accessors;
    Material *materials;
};

static void model_load_gltf_meshes(const Load_Mesh_Info *info, u32 count, const Gltf_Mesh *gltf_meshes, Mesh *meshes,
                                   u8 *primitives_buffer, u8 *weights_buffer)
{
    u32 tmp;
    u32 idx;

    const Gltf_Mesh           *gltf_mesh = gltf_meshes;
    const Gltf_Mesh_Primitive *gltf_primitive;
    const Gltf_Morph_Target   *gltf_morph_target;

    u32 primitive_count;

    Mesh_Primitive           *primitive;
    Mesh_Primitive_Attribute *attribute;
    Morph_Target             *target;

    Accessor *accessors = info->accessors;
    Material *materials = info->materials;

    u32 size_used_primitives = 0;
    u32 size_used_weights    = 0;

    for(u32 i = 0; i < count; ++i) {
        primitive_count           = gltf_mesh->primitive_count;
        meshes[i].primitive_count = primitive_count;

        meshes[i].primitives  = (Mesh_Primitive*)(primitives_buffer + size_used_primitives);
        size_used_primitives  += sizeof(Mesh_Primitive) * primitive_count;

        gltf_primitive = gltf_mesh->primitives;
        for(u32 j = 0; j < primitive_count; ++j) {
            primitive = &meshes[i].primitives[j];

            primitive->topology     = (VkPrimitiveTopology)gltf_primitive->topology;
            primitive->indices      = accessors[gltf_primitive->indices];
            primitive->material     = materials[gltf_primitive->material];

            primitive->attribute_count  = gltf_primitive->extra_attribute_count;
            primitive->attribute_count += (u32)(gltf_primitive->position    != -1);
            primitive->attribute_count += (u32)(gltf_primitive->normal      != -1);
            primitive->attribute_count += (u32)(gltf_primitive->tangent     != -1);
            primitive->attribute_count += (u32)(gltf_primitive->tex_coord_0 != -1);

            primitive->attributes  = (Mesh_Primitive_Attribute*)(primitives_buffer + size_used_primitives);
            size_used_primitives   += sizeof(Mesh_Primitive_Attribute) * primitive->attribute_count;

            // When I set up this api in the gltf file, I had no idea how annoying it would be later...
            // If an attribute is not set, rather than branch, we just overwrite it by not incrementing the index.
            tmp = 0;

            idx = gltf_primitive->normal & max32_if_true(gltf_primitive->normal != -1);
            primitive->attributes[tmp] = {
                .accessor = accessors[idx],
                .type     = MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL,
            };
            tmp += (u32)(gltf_primitive->normal != -1);

            idx = gltf_primitive->position & max32_if_true(gltf_primitive->position != -1);
            primitive->attributes[tmp] = {
                .accessor = accessors[idx],
                .type     = MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION,
            };
            tmp += (u32)(gltf_primitive->position != -1);

            idx = gltf_primitive->tangent & max32_if_true(gltf_primitive->tangent != -1);
            primitive->attributes[tmp] = {
                .accessor = accessors[idx],
                .type     = MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT,
            };
            tmp += (u32)(gltf_primitive->tangent != -1);

            idx = gltf_primitive->tex_coord_0 & max32_if_true(gltf_primitive->tex_coord_0 != -1);
            primitive->attributes[tmp] = {
                .accessor = accessors[idx],
                .type     = MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS,
            };
            tmp += (u32)(gltf_primitive->tex_coord_0 != -1);

            for(u32 k = 0; k < gltf_primitive->extra_attribute_count; ++k) {
                attribute = &primitive->attributes[tmp + k];

                attribute->n        = gltf_primitive->extra_attributes[k].n;
                attribute->accessor = accessors[gltf_primitive->extra_attributes[k].accessor_index];
                attribute->type     = (Mesh_Primitive_Attribute_Type)gltf_primitive->extra_attributes[k].type;
            }

            primitive->target_count = gltf_primitive->target_count;
            primitive->targets      = (Morph_Target*)(primitives_buffer + size_used_primitives);
            size_used_primitives   += sizeof(Morph_Target) * primitive->target_count;

            gltf_morph_target = gltf_primitive->targets;
            for(u32 k = 0; k < gltf_primitive->target_count; ++k) {
                target = &primitive->targets[k];

                target->attribute_count  = gltf_morph_target->attribute_count;
                target->attributes       = (Mesh_Primitive_Attribute*)(primitives_buffer + size_used_primitives);
                size_used_primitives    += sizeof(Mesh_Primitive_Attribute) * gltf_morph_target->attribute_count;

                for(u32 l = 0; l < gltf_morph_target->attribute_count; ++l) {
                    attribute = &target->attributes[l];

                    attribute->n        = gltf_morph_target->attributes[l].n;
                    attribute->accessor = accessors[gltf_morph_target->attributes[l].accessor_index];
                    attribute->type     = (Mesh_Primitive_Attribute_Type)gltf_morph_target->attributes[l].type;
                }

                gltf_morph_target = (const Gltf_Morph_Target*)((u8*)gltf_morph_target + gltf_morph_target->stride);
            }

            gltf_primitive = (const Gltf_Mesh_Primitive*)((u8*)gltf_primitive + gltf_primitive->stride);
        }

        meshes[i].weight_count = gltf_mesh->weight_count;
        meshes[i].weights      = (float*)(weights_buffer + size_used_weights);
        size_used_weights     += sizeof(float) * meshes[i].weight_count;

        memcpy(meshes[i].weights, gltf_mesh->weights, sizeof(float) * meshes[i].weight_count);

        gltf_mesh = (const Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }
}

struct Buffer_View {
    u64 offset;
    u64 size;
};

inline static void add_buffer_view_index(u32 idx, u32 *count, u32 *indices, u32 mask_count, u64 *masks) {
    u32 mask_idx = idx >> 6;
    u64 mask     = masks[mask_idx];

    u64 one = 1;
    bool seen = (mask & (one << (idx & 63))) > 0;
    mask      =  mask | (one << (idx & 63));

    masks[mask_idx] = mask;

    indices[*count] = idx;
    *count += !seen;
}

//
// @Note This implementation looks a little weird, as lots of sections seem naively split apart (for
// instance, the gltf struct is looped a few different times) but this is intentional, as eventually
// I will split this thing into jobs for threading, like reading the gltf struct into the model struct,
// and dipatching work to the allocators.
//
// @Todo Skins, Animations, Cameras.
//
Model model_from_gltf(Model_Allocators *model_allocators, const String *model_dir, const String *gltf_file_name, u64 size_available,
                      u8 *model_buffer, u64 *ret_req_size)
{
    u64 temp_allocator_mark = get_mark_temp(); // Reset to mark at end of function

    char uri_buf[127];
    memcpy(uri_buf +              0, model_dir->str,      model_dir->len);
    memcpy(uri_buf + model_dir->len, gltf_file_name->str, gltf_file_name->len + 1);
    Gltf gltf = parse_gltf(uri_buf);

    // Get required bytes
    Model_Req_Size_Info req_size = model_get_required_size_from_gltf(&gltf);

    #if MODEL_LOAD_INFO
    println("Size required for model %s: %u, Bytes remaining in buffer: %u", gltf_file_name->str, req_size.total, size_available);
    #endif

    *ret_req_size = req_size.total;
    if (req_size.total > size_available) {

        #if !MODEL_LOAD_INFO
        println("Size required for model %s: %u, Bytes remaining in buffer: %u", gltf_file_name->str, req_size.total, size_available);
        #endif

        println("Insufficient size remaining in model buffer. Failed to load models.");
        assert(false && "See above...");

        return {};
    }

    Model ret = {};
    ret.mesh_count = gltf_mesh_get_count(&gltf);

    // model_buffer layout: (@Todo This will change when I add skins, animations, etc.)
    // | meshes | primitives | extra primitive data | extra accessor data | mesh weights |

    u32 buffer_offset_primitives    = sizeof(Mesh) * ret.mesh_count;
    u32 buffer_offset_accessor_data = buffer_offset_primitives    + req_size.primitives;
    u32 buffer_offset_weights       = buffer_offset_accessor_data + req_size.accessors;

    // @Multithreading accessors and textures are independent. Materials depends on textures.

    // Accessor
    u32       accessor_count = gltf_accessor_get_count(&gltf);
    Accessor *accessors      = (Accessor*)malloc_t(sizeof(Accessor) * accessor_count, 8);
    model_load_gltf_accessors(accessor_count, gltf.accessors, accessors, model_buffer + buffer_offset_accessor_data);

    // Texture - no extra data, so no buffer argument
    u32      texture_count = gltf_texture_get_count(&gltf);
    Texture *textures      = (Texture*)malloc_t(sizeof(Texture) * texture_count, 8);
    model_load_gltf_textures(texture_count, gltf.textures, textures);

    // Material - no extra data, so no buffer argument
    u32       material_count = gltf_material_get_count(&gltf);
    Material *materials      = (Material*)malloc_t(sizeof(Material) * material_count, 8);
    model_load_gltf_materials(material_count, gltf.materials, textures, materials);

    Load_Mesh_Info load_mesh_info = {
        .accessors = accessors,
        .materials = materials,
    };

    // Mesh
    ret.meshes = (Mesh*)(model_buffer);
    model_load_gltf_meshes(&load_mesh_info, ret.mesh_count, gltf.meshes, ret.meshes,
                           model_buffer + buffer_offset_primitives, model_buffer + buffer_offset_weights);

    // Each texture/buffer view becomes an allocation.
    // @Note One day I will join buffer views together, so that an allocation is the data referenced by a primitive.

                                    /* Buffer View Allocations */
    u32  buffer_view_count          = gltf_buffer_view_get_count(&gltf);
    u32  index_buffer_view_count    = 0;
    u32  vertex_buffer_view_count   = 0;
    u32 *index_buffer_view_indices  = (u32*)malloc_t(sizeof(Buffer_View) * buffer_view_count, 8);
    u32 *vertex_buffer_view_indices = (u32*)malloc_t(sizeof(Buffer_View) * buffer_view_count, 8);

    // Assume fewer than 8 * 64 (512) buffer views
    u32 mask_count = 8;
    u64 masks[mask_count];
    memset(masks, 0, sizeof(u64) * mask_count);
    assert(buffer_view_count < (8 * 64) && "Increase mask count");

    // Separate buffer views into vertex and index data (add_buffer_view_index() tracks dupes, branchless)
    Mesh_Primitive *primitive;
    Morph_Target   *target;
    for(u32 i = 0; i < ret.mesh_count; ++i) {
        for(u32 j = 0; j < ret.meshes[i].primitive_count; ++j) {
            primitive = &ret.meshes[i].primitives[j];

            add_buffer_view_index(primitive->indices.allocation_key, &index_buffer_view_count,
                                  index_buffer_view_indices, mask_count, masks);

            for(u32 k = 0; k < primitive->attribute_count; ++k) {
                add_buffer_view_index(primitive->attributes[k].accessor.allocation_key, &vertex_buffer_view_count,
                                      vertex_buffer_view_indices, mask_count, masks);

                if (primitive->attributes[k].accessor.sparse) {
                    add_buffer_view_index(primitive->attributes[k].accessor.sparse->indices_allocation_key,
                                          &index_buffer_view_count, index_buffer_view_indices, mask_count, masks);
                    add_buffer_view_index(primitive->attributes[k].accessor.sparse->values_allocation_key,
                                          &vertex_buffer_view_count, vertex_buffer_view_indices, mask_count, masks);
                }
            }

            for(u32 k = 0; k < primitive->target_count; ++k) {
                target = &primitive->targets[k];
                for(u32 l = 0; l < target->attribute_count; ++l) {
                   add_buffer_view_index(target->attributes[l].accessor.allocation_key, &vertex_buffer_view_count,
                                         vertex_buffer_view_indices, mask_count, masks);

                    if (primitive->attributes[k].accessor.sparse) {
                        add_buffer_view_index(target->attributes[l].accessor.sparse->indices_allocation_key,
                                              &index_buffer_view_count, index_buffer_view_indices, mask_count, masks);
                        add_buffer_view_index(target->attributes[l].accessor.sparse->values_allocation_key,
                                              &vertex_buffer_view_count, vertex_buffer_view_indices, mask_count, masks);
                    }

                }
            }
        }
    }

    // Make the access pattern in the below loop a bit more predictable. There should only
    // be a relatively small number off indices in each array, so it should be fast.
    sort_indices(index_buffer_view_count, index_buffer_view_indices);
    sort_indices(vertex_buffer_view_count, vertex_buffer_view_indices);

    // Repeated assert ik, a reminder for a different section...
    assert(gltf_buffer_get_count(&gltf) == 1 && "Have to loop buffers *rolls eyes*");

    const Gltf_Buffer *gltf_buffer = gltf.buffers;

    u32 model_dir_len = model_dir->len;
    strcpy(uri_buf + model_dir_len, gltf_buffer->uri); // Build the buffer uri.

    u8 *buffer = (u8*)file_read_bin_temp_large(uri_buf, gltf_buffer->byte_length);

    u32 *allocation_keys = (u32*)malloc_t(sizeof(u32) * buffer_view_count);

    u32 tmp;
    const Gltf_Buffer_View     *gltf_buffer_view;
    Gpu_Allocator_Result  allocator_result;
    for(u32 i = 0; i < index_buffer_view_count; ++i) {
        allocator_result = begin_allocation(&model_allocators->index);
        CHECK_GPU_ALLOCATOR_RESULT(allocator_result);

        tmp = index_buffer_view_indices[i];
        gltf_buffer_view = gltf_buffer_view_by_index(&gltf, tmp); // Lame, I do not like what this function represents...

        allocator_result = continue_allocation(&model_allocators->index, gltf_buffer_view->byte_length,
                                               buffer + gltf_buffer_view->byte_offset);
        CHECK_GPU_ALLOCATOR_RESULT(allocator_result);

        allocator_result = submit_allocation(&model_allocators->index, &allocation_keys[tmp]);
        CHECK_GPU_ALLOCATOR_RESULT(allocator_result);
    }

    for(u32 i = 0; i < vertex_buffer_view_count; ++i) {
        allocator_result = begin_allocation(&model_allocators->vertex);
        CHECK_GPU_ALLOCATOR_RESULT(allocator_result);

        tmp = vertex_buffer_view_indices[i];
        gltf_buffer_view = gltf_buffer_view_by_index(&gltf, tmp); // Lame, I do not like what this function represents...

        allocator_result = continue_allocation(&model_allocators->vertex, gltf_buffer_view->byte_length,
                                               buffer + gltf_buffer_view->byte_offset);
        CHECK_GPU_ALLOCATOR_RESULT(allocator_result);

        allocator_result = submit_allocation(&model_allocators->vertex, &allocation_keys[tmp]);
        CHECK_GPU_ALLOCATOR_RESULT(allocator_result);
    }

                                        /* Texture Allocations */

    u32  image_count              = gltf_image_get_count(&gltf);
    u32 *tex_allocation_keys  = (u32*)malloc_t(sizeof(u32) * image_count);

    String image_file_name;
    const Gltf_Image *gltf_image = gltf.images;
    for(u32 i = 0; i < image_count; ++i) {
        // Fixing the below assert is trivial, but I do not need to right now. It will be done
        // when it actually fires.
        assert(gltf_image->uri && "@Todo @Unimplemented Support reading textures from buffer views");

        // Idk about strlens and cstrs. It seems to compile to an avx intrinsic, so worries about speed is
        // whatever, its just robustness (I am just debating in my head converting the gltf parser to use
        // a real string type...)
        strcpy(uri_buf + model_dir_len, gltf_image->uri); // Build the image uri
        image_file_name = cstr_to_string(uri_buf);

        // replace indices into images array with texture allocation keys
        allocator_result = tex_add_texture(&model_allocators->tex, &image_file_name, &tex_allocation_keys[i]);
        CHECK_GPU_ALLOCATOR_RESULT(allocator_result);

        gltf_image = (const Gltf_Image*)((u8*)gltf_image + gltf_image->stride);
    }

    u32  sampler_count = gltf_sampler_get_count(&gltf);
    u32 *sampler_keys  = (u32*)malloc_t(sizeof(u32) * sampler_count);

    Get_Sampler_Info         get_sampler_info;
    Sampler_Allocator_Result sampler_result;

    const Gltf_Sampler *gltf_sampler = gltf.samplers;
    for(u32 i = 0; i < sampler_count; ++i) {
        get_sampler_info = {};
        get_sampler_info.wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
        get_sampler_info.wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
        get_sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
        get_sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;

        sampler_result = add_sampler(&model_allocators->sampler, &get_sampler_info, &sampler_keys[i]);
        assert(sampler_result == SAMPLER_ALLOCATOR_RESULT_SUCCESS);
        CHECK_SAMPLER_ALLOCATOR_RESULT(sampler_result);

        gltf_sampler = (const Gltf_Sampler*)((u8*)gltf_sampler + gltf_sampler->stride);
    }

    // @Todo Do something with the information that a texture will always/have need a sampler, so I do not need
    // a sampler count. Right now it is fine because I would pad the sampler count in primitive.key_counts anyway
    // for simd, so right now it is useful for that. But it is a free 4 bytes that might be useful.

    // Point model back at the allocation keys
    Accessor *accessor;
    for(u32 i = 0; i < ret.mesh_count; ++i) {
        for(u32 j = 0; j < ret.meshes[i].primitive_count; ++j) {
            primitive = &ret.meshes[i].primitives[j];

            // Indices
            primitive->indices.allocation_key = allocation_keys[primitive->indices.allocation_key];
            primitive->key_counts.index++;

                                                    /* Material */
            // Base Color
            primitive->material.pbr.base_color_texture.texture_key = tex_allocation_keys[primitive->material.pbr.base_color_texture.texture_key];
            primitive->material.pbr.base_color_texture.sampler_key = sampler_keys       [primitive->material.pbr.base_color_texture.sampler_key];

            primitive->key_counts.tex     += (primitive->material.flags & MATERIAL_BASE_BIT) > 0;
            primitive->key_counts.sampler += (primitive->material.flags & MATERIAL_BASE_BIT) > 0;

            // Metallic Roughness
            primitive->material.pbr.metallic_roughness_texture.texture_key = tex_allocation_keys[primitive->material.pbr.metallic_roughness_texture.texture_key];
            primitive->material.pbr.metallic_roughness_texture.sampler_key = sampler_keys       [primitive->material.pbr.metallic_roughness_texture.sampler_key];

            primitive->key_counts.tex     += (primitive->material.flags & MATERIAL_PBR_BIT) > 0;
            primitive->key_counts.sampler += (primitive->material.flags & MATERIAL_PBR_BIT) > 0;

            // Normal
            primitive->material.normal.texture.texture_key = tex_allocation_keys[primitive->material.normal.texture.texture_key];
            primitive->material.normal.texture.sampler_key = sampler_keys       [primitive->material.normal.texture.sampler_key];

            primitive->key_counts.tex     += (primitive->material.flags & MATERIAL_NORMAL_BIT) > 0;
            primitive->key_counts.sampler += (primitive->material.flags & MATERIAL_NORMAL_BIT) > 0;

            // Occlusion
            primitive->material.occlusion.texture.texture_key = tex_allocation_keys[primitive->material.occlusion.texture.texture_key];
            primitive->material.occlusion.texture.sampler_key = sampler_keys       [primitive->material.occlusion.texture.sampler_key];

            primitive->key_counts.tex     += (primitive->material.flags & MATERIAL_OCCLUSION_BIT) > 0;
            primitive->key_counts.sampler += (primitive->material.flags & MATERIAL_OCCLUSION_BIT) > 0;

            // Emissive
            primitive->material.emissive.texture.texture_key  = tex_allocation_keys[primitive->material.emissive.texture.texture_key];
            primitive->material.emissive.texture.sampler_key  = sampler_keys       [primitive->material.emissive.texture.sampler_key];

            primitive->key_counts.tex     += (primitive->material.flags & MATERIAL_EMISSIVE_BIT) > 0;
            primitive->key_counts.sampler += (primitive->material.flags & MATERIAL_EMISSIVE_BIT) > 0;

            // Attributes
            for(u32 k = 0; k < primitive->attribute_count; ++k) {
                accessor = &primitive->attributes[k].accessor;

                accessor->allocation_key = allocation_keys[accessor->allocation_key];
                primitive->key_counts.vertex++;

                if (accessor->sparse) {
                    accessor->sparse->indices_allocation_key = allocation_keys[accessor->sparse->indices_allocation_key];
                    accessor->sparse->values_allocation_key  = allocation_keys[accessor->sparse->values_allocation_key];

                    primitive->key_counts.index++;
                    primitive->key_counts.vertex++;
                }
            }

            // Targets
            for(u32 k = 0; k < primitive->target_count; ++k) {
                for(u32 l = 0; l < primitive->targets[k].attribute_count; ++l) {
                    accessor = &primitive->targets[k].attributes[l].accessor;

                    accessor->allocation_key = allocation_keys[accessor->allocation_key];
                    primitive->key_counts.vertex++;

                    if (accessor->sparse) {
                        accessor->sparse->indices_allocation_key = allocation_keys[accessor->sparse->indices_allocation_key];
                        accessor->sparse->values_allocation_key  = allocation_keys[accessor->sparse->values_allocation_key];

                        primitive->key_counts.index++;
                        primitive->key_counts.vertex++;
                    }
                }
            }
        }
    }

    reset_to_mark_temp(temp_allocator_mark); // Mark at function beginning

    return ret;
}

// These will be implemented when I run into a bottle neck on model structures taking up too much space:
// the plan is to write these 'Model' structs to disk, and then avoid using gltf all together. Better load times
// not having to parse the text files, just have to reset pointers.
Model_Storage_Info store_model(Model *model) { // @Unimplemented
    return {};
}
Model load_model(Model_Storage_Info *strorage_info) { // @Unimplemented
    Model  ret;
    return ret;
}

inline static void add_accessor_index(Array<u32> *array_index, Array<u32> *array_vertex, const Accessor *accessor) {
    array_add(array_index, accessor->allocation_key);

    if (accessor->sparse) {
        array_add(array_index,  accessor->sparse->indices_allocation_key);
        array_add(array_vertex, accessor->sparse->values_allocation_key);
    }
}
inline static void add_accessor_vertex(Array<u32> *array_index, Array<u32> *array_vertex, Accessor *accessor) {
    array_add(array_vertex, accessor->allocation_key);

    if (accessor->sparse) {
        array_add(array_index,  accessor->sparse->indices_allocation_key);
        array_add(array_vertex, accessor->sparse->values_allocation_key);
    }
}

inline static VkFormat get_format_from_accessor_flags(Accessor_Flags flags) {
    u32 ret;

    flags &= ACCESSOR_TYPE_BITS | ACCESSOR_COMPONENT_TYPE_BITS;

    // I dont know for sure if this is more efficient that a switch statement, but I am pretty sure it is.
    // For the switch statement you still have to do loads of compare ops, plus the fact that the branch predictor
    // has not got a fucking chance. The only benefit that you get is the early out. But you do not know when that
    // early out will come, you may have to compare all the way down which means your early really is not very early.
    // This way to have to do a shit tonne of adds, subs, ands and compares, but really what is that to modern pc,
    // compared to a branch prediction failure. In the intel optimisation manual, they recommend stuff like this
    // over branching, I do not know if this crosses a line, but I expect that it does not. (Plus it was a fun
    // editor stress test, literally took helix 2 secs for these 50 lines lol).

    ret -= ret                                    & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT));
    ret += static_cast<u32>(VK_FORMAT_R8_SINT)    & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT));
    ret -= ret                                    & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT));
    ret += static_cast<u32>(VK_FORMAT_R8_UINT)    & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT));
    ret -= ret                                    & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT));
    ret += static_cast<u32>(VK_FORMAT_R16_SINT)   & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT));
    ret -= ret                                    & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT));
    ret += static_cast<u32>(VK_FORMAT_R16_UINT)   & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT));
    ret -= ret                                    & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT));
    ret += static_cast<u32>(VK_FORMAT_R32_UINT)   & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT));
    ret -= ret                                    & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT));
    ret += static_cast<u32>(VK_FORMAT_R32_SFLOAT) & max32_if_true(flags == (ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT));

    ret -= ret                                       & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT));
    ret += static_cast<u32>(VK_FORMAT_R8G8_SINT)     & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT));
    ret -= ret                                       & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT));
    ret += static_cast<u32>(VK_FORMAT_R8G8_UINT)     & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT));
    ret -= ret                                       & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT));
    ret += static_cast<u32>(VK_FORMAT_R16G16_SINT)   & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT));
    ret -= ret                                       & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT));
    ret += static_cast<u32>(VK_FORMAT_R16G16_UINT)   & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT));
    ret -= ret                                       & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT));
    ret += static_cast<u32>(VK_FORMAT_R32G32_UINT)   & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT));
    ret -= ret                                       & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT));
    ret += static_cast<u32>(VK_FORMAT_R32G32_SFLOAT) & max32_if_true(flags == (ACCESSOR_TYPE_VEC2_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT));

    ret -= ret                                          & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT));
    ret += static_cast<u32>(VK_FORMAT_R8G8B8_SINT)      & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT));
    ret -= ret                                          & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT));
    ret += static_cast<u32>(VK_FORMAT_R8G8B8_UINT)      & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT));
    ret -= ret                                          & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT));
    ret += static_cast<u32>(VK_FORMAT_R16G16B16_SINT)   & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT));
    ret -= ret                                          & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT));
    ret += static_cast<u32>(VK_FORMAT_R16G16B16_UINT)   & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT));
    ret -= ret                                          & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT));
    ret += static_cast<u32>(VK_FORMAT_R32G32B32_UINT)   & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT));
    ret -= ret                                          & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT));
    ret += static_cast<u32>(VK_FORMAT_R32G32B32_SFLOAT) & max32_if_true(flags == (ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT));

    ret -= ret                                             & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT));
    ret += static_cast<u32>(VK_FORMAT_R8G8B8A8_SINT)       & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT));
    ret -= ret                                             & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT));
    ret += static_cast<u32>(VK_FORMAT_R8G8B8A8_UINT)       & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT));
    ret -= ret                                             & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT));
    ret += static_cast<u32>(VK_FORMAT_R16G16B16A16_SINT)   & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT));
    ret -= ret                                             & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT));
    ret += static_cast<u32>(VK_FORMAT_R16G16B16A16_UINT)   & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT));
    ret -= ret                                             & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT));
    ret += static_cast<u32>(VK_FORMAT_R32G32B32A32_UINT)   & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT));
    ret -= ret                                             & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT));
    ret += static_cast<u32>(VK_FORMAT_R32G32B32A32_SFLOAT) & max32_if_true(flags == (ACCESSOR_TYPE_VEC4_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT));

    return (VkFormat)ret;

    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT2_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT);

    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT3_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT);

    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_SCHAR_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_UCHAR_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_S16_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT);
    // ret -= ret             & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT);
    // ret += (u32)vk_format_ & max32_if_true(ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT);
}

struct Material_Ubo_Allocators {
    Uniform_Allocator    *uniform;
    Descriptor_Allocator *descriptor;
};

// base color factor (array), metallic factor, roughness factor, normal scale,
// occlusion strength, emissive factor (array), alpha cutoff
static constexpr u32 MAX_RESOURCE_DESCRIPTOR_COUNT_PER_PRIMITIVE = 7;

static void load_primitive_info(
    u32                      count,
    const Mesh_Primitive    *primitives,
    Allocation_Key_Arrays   *arrays,
    Pl_Primitive_Info       *pl_infos,
    Primitive_Draw_Info     *draw_infos,
    Material_Ubo_Allocators *material_ubo_allocators)
{
    Assets *g_assets = get_assets_instance();

    Array<u32> array_index   = new_array_from_ptr(arrays->index,   arrays->lens.index);
    Array<u32> array_vertex  = new_array_from_ptr(arrays->vertex,  arrays->lens.vertex);
    Array<u32> array_tex     = new_array_from_ptr(arrays->tex,     arrays->lens.tex);
    Array<u32> array_sampler = new_array_from_ptr(arrays->sampler, arrays->lens.sampler);

    u32 descriptor_offset_count = count * MAX_RESOURCE_DESCRIPTOR_COUNT_PER_PRIMITIVE;

    Gpu *gpu          = get_gpu_instance();
    u64  ubo_set_size = gpu->shader_memory.material_ubo_set_size; // Already aligned to descriptor buffer alignment
    u64  ubo_size     = sizeof(Material_Ubo); // The uniform allocator should be aligning to 16 bytes.
    assert(ubo_size == 48);

    Descriptor_Allocator  *descriptor_allocator  = material_ubo_allocators->descriptor;
    Descriptor_Allocation  descriptor_allocation = descriptor_allocate_layout(descriptor_allocator, ubo_set_size * count);
    Uniform_Allocation     uniform_allocation    = uniform_malloc(material_ubo_allocators->uniform, ubo_size * count);

    // This is pretty lame, this double indirection is pretty dumb 
    VkDescriptorAddressInfoEXT descriptor_address_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT};
    descriptor_address_info.range = ubo_size;

    VkDescriptorDataEXT descriptor_data = {.pUniformBuffer = &descriptor_address_info};

    // @Warn If there is a funny bug, come back here. Easy to make a mistake in this descriptor area.
    VkDevice device = gpu->device;
    for(u32 i = 0; i < count; ++i) {
        descriptor_address_info.address = uniform_allocation.address + (ubo_size * i);
        descriptor_write_uniform_buffer(device, descriptor_allocator, descriptor_data,
                                        (u8*)descriptor_allocation.ptr + ubo_set_size);
    }

    u32 attribute_count;
    u32 target_count;
    Morph_Target *target;
    for(u32 i = 0; i < count; ++i) {
        // Indices
        add_accessor_index(&array_index, &array_vertex, &primitives[i].indices);

        // index type u16 = 0, u32 = 1.
        draw_infos[i].index_type = static_cast<VkIndexType>((primitives[i].indices.flags & ACCESSOR_COMPONENT_TYPE_U32_BIT) > 0);
        draw_infos[i].count      = primitives[i].indices.count;

        // Vertex Attributes
        attribute_count = primitives[i].attribute_count;

        pl_infos[i].count   = attribute_count;
        pl_infos[i].strides =      (u32*)malloc_t(sizeof(u32)      * attribute_count); // Maybe these should be arrays, idk
        pl_infos[i].formats = (VkFormat*)malloc_t(sizeof(VkFormat) * attribute_count);

        for(u32 j = 0; j < attribute_count; ++j) {
            add_accessor_vertex(&array_index, &array_vertex, &primitives[i].attributes[j].accessor);

            pl_infos[i].strides[j] = primitives[i].attributes[j].accessor.byte_stride;
            pl_infos[i].formats[j] = get_format_from_accessor_flags(primitives[i].attributes[j].accessor.flags);
        }

        // Morph Targets
        target_count = primitives[i].target_count;
        for(u32 j = 0; j < target_count; ++j) {
            target = &primitives[i].targets[j];

            attribute_count = target->attribute_count;
            for(u32 k = 0; k < attribute_count; ++k)
                add_accessor_vertex(&array_index, &array_vertex, &target->attributes[k].accessor);
        }

                                                        /* Materials */

        memcpy(uniform_allocation.ptr + (ubo_size * i), &primitives[i].material.ubo, ubo_size);

        // Base Color
        array_add_if_true(&array_tex,     primitives[i].material.pbr.base_color_texture.texture_key, primitives[i].material.flags & MATERIAL_BASE_BIT);
        array_add_if_true(&array_sampler, primitives[i].material.pbr.base_color_texture.sampler_key, primitives[i].material.flags & MATERIAL_BASE_BIT);

        // Metallic Roughness
        array_add_if_true(&array_tex,     primitives[i].material.pbr.metallic_roughness_texture.texture_key, primitives[i].material.flags & MATERIAL_PBR_BIT);
        array_add_if_true(&array_sampler, primitives[i].material.pbr.metallic_roughness_texture.sampler_key, primitives[i].material.flags & MATERIAL_PBR_BIT);

        // Normal
        array_add_if_true(&array_tex,     primitives[i].material.normal.texture.texture_key, primitives[i].material.flags & MATERIAL_NORMAL_BIT);
        array_add_if_true(&array_sampler, primitives[i].material.normal.texture.sampler_key, primitives[i].material.flags & MATERIAL_NORMAL_BIT);

        // Occlusion
        array_add_if_true(&array_tex,     primitives[i].material.occlusion.texture.texture_key, primitives[i].material.flags & MATERIAL_OCCLUSION_BIT);
        array_add_if_true(&array_sampler, primitives[i].material.occlusion.texture.sampler_key, primitives[i].material.flags & MATERIAL_OCCLUSION_BIT);

        // Emissive
        array_add_if_true(&array_tex,     primitives[i].material.emissive.texture.texture_key, primitives[i].material.flags & MATERIAL_EMISSIVE_BIT);
        array_add_if_true(&array_sampler, primitives[i].material.emissive.texture.sampler_key, primitives[i].material.flags & MATERIAL_EMISSIVE_BIT);
    }
}

inline static bool mask_off_result(u32 idx, u64 *masks, Gpu_Allocator_Result result) {
    u64 one = 1;
    u64 check = result == GPU_ALLOCATOR_RESULT_SUCCESS;
    masks[idx >> 6] &= ~(one << (idx & 63));
    masks[idx >> 6] |= check << (idx & 63);
    return check;
}

// Just copying and pasting the below functions is easier, trust me.
bool attempt_to_queue_allocations_staging(
    Gpu_Allocator *allocator,
    u32            count,
    u32           *keys,
    u64           *result_masks,
    bool           adjust_weights)
{
    Gpu_Allocator_Result result = staging_queue_begin(allocator);
    CHECK_GPU_ALLOCATOR_RESULT(result);

    bool check;
    bool ret = true;
    u64 tmp;
    for(u32 i = 0; i < count; ++i) {
        result = staging_queue_add(allocator, keys[i], adjust_weights);
        ret    = mask_off_result(i, result_masks, result) && ret;
    }

    return ret;
}

bool attempt_to_queue_allocations_upload(
    Gpu_Allocator *allocator,
    u32            count,
    u32           *keys,
    u64           *result_masks,
    bool           adjust_weights)
{
    Gpu_Allocator_Result result = upload_queue_begin(allocator);
    CHECK_GPU_ALLOCATOR_RESULT(result);

    bool check;
    bool ret = true;
    for(u32 i = 0; i < count; ++i) {
        result = upload_queue_add(allocator, keys[i], adjust_weights);
        ret    = mask_off_result(i, result_masks, result) && ret;
    }

    return ret;
}

bool attempt_to_queue_tex_allocations_staging(
    Gpu_Tex_Allocator *allocator,
    u32                count,
    u32               *keys,
    u64               *result_masks,
    bool               adjust_weights)
{
    Gpu_Allocator_Result result = tex_staging_queue_begin(allocator);
    CHECK_GPU_ALLOCATOR_RESULT(result);

    bool check;
    bool ret = true;
    for(u32 i = 0; i < count; ++i) {
        result = tex_staging_queue_add(allocator, keys[i], adjust_weights);
        ret    = mask_off_result(i, result_masks, result) && ret;
    }

    return ret;
}

bool attempt_to_queue_tex_allocations_upload(
    Gpu_Tex_Allocator *allocator,
    u32                count,
    u32               *keys,
    u64               *result_masks,
    bool               adjust_weights)
{
    Gpu_Allocator_Result result = tex_upload_queue_begin(allocator);
    CHECK_GPU_ALLOCATOR_RESULT(result);

    bool check;
    bool ret = true;
    for(u32 i = 0; i < count; ++i) {
        result = tex_upload_queue_add(allocator, keys[i], adjust_weights);
        ret    = mask_off_result(i, result_masks, result) && ret;
    }

    return ret;
}

inline static bool check_upload_stage_results_against_req_count(u32 count, u32 bit_pos, u64 stage_masks[2], u64 upload_masks[2]) {
    u64 one = 1;
    u64 max = Max_u64;

    // number of results overflowed to the second mask
    u32 result_overflow = ((bit_pos + count) - 64) & max64_if_true(bit_pos + count > 64);

    u64 overflow_mask;
    u64 primary_mask;

    // create a mask of the overflow bits
    overflow_mask   =   stage_masks[1] & upload_masks[1];
    overflow_mask  &= ~(max << result_overflow);
    overflow_mask <<=   count - result_overflow; // make room at the beginning for the primary mask bits

    // create a mask of the bits in the first mask
    primary_mask  =  (stage_masks[0] & upload_masks[0]) >> bit_pos;
    primary_mask &= ~(max << (count - result_overflow));
    primary_mask |= overflow_mask;

    // if all allocations of this type (e.g. index) for this primitive were successfully staged and uploaded, the primary
    // mask should contain a contiguous section of set bits equivalent in length to the number of staged/uploaded allocations.
    primary_mask = count_trailing_zeros_u64(~primary_mask) & max64_if_true(pop_count64(primary_mask));
    assert(primary_mask <= count && "Algorithm Mistake: more successful results than possible results");

    return primary_mask == count;
}

/*
    As per VkSpec:
        "If image is non-sparse then it must be bound completely and contiguously to a single
        VkDeviceMemory object"

    So image views and their descriptors can only be acquired after queue upload...
    @Todo Check out sparse to avoid this burden, it might help idk.
*/
Primitive_Draw_Prep_Result prepare_to_draw_primitives(
    u32                          count,
    const Mesh_Primitive        *primitives,
    const Allocation_Key_Counts *key_counts,
    const Pl_Config             *pipeline_configs,
    bool                         adjust_weights)
{
    Assets *g_assets = get_assets_instance();

    // @Multithreading Each thread will take an offset into the primitives array and some
    // number of primitives, and offsets into each of the key arrays in g_assets where it
    // will write primitives' corresponding keys.

    u32 primitive_job_size = count / g_thread_count;
    u32 remainder_job_size = count % g_thread_count;

    u32                    primitive_job_offsets   [g_thread_count];
    u32                    primitive_job_sizes     [g_thread_count]; // The number of primitives in a job
    Allocation_Key_Arrays  primitive_job_key_arrays[g_thread_count];

    u32 *keys_index   = (u32*)malloc_t(sizeof(u32) * g_assets_keys_array_tex_len);
    u32 *keys_vertex  = (u32*)malloc_t(sizeof(u32) * g_assets_keys_array_index_len);
    u32 *keys_tex     = (u32*)malloc_t(sizeof(u32) * g_assets_keys_array_vertex_len);
    u32 *keys_sampler = (u32*)malloc_t(sizeof(u32) * g_assets_keys_array_sampler_len);

    // @Multithreading I may need to pad these jobs to prevent false sharing idk... @Test
    Pl_Primitive_Info   *pl_primitive_infos   =   (Pl_Primitive_Info*)malloc_t(sizeof(Pl_Primitive_Info)  * count);
    Primitive_Draw_Info *primitive_draw_infos = (Primitive_Draw_Info*)malloc_t(sizeof(Primitive_Draw_Info) * count);

    // Doubles as the total key count for each key type once filled in
    alignas(16) Allocation_Key_Counts accum_offsets = {};

    __m128i  a;
    __m128i  b;
    __m128i *tmp_addr;

    u32 idx = 0;
    u32 job_offset_accum = 0;
    for(u32 i = 0; i < g_thread_count; ++i) {
        primitive_job_sizes[i]  = primitive_job_size;
        primitive_job_sizes[i] += remainder_job_size > 0;
        remainder_job_size     -= remainder_job_size > 0;

        primitive_job_offsets[i]  = job_offset_accum;
        job_offset_accum         += primitive_job_sizes[i];

        // @Note Sampler keys are accounted for, but are not used at this stage. I really just
        // use them for now to pad out the 16 bytes for SIMD. I may make acquiring them part
        // of this function, maybe just to warm up the cache, but for now the keys are unused.
        // I cannot see any over head to them so whatever.

        // @SIMD This could be simd too, but it would be awkward since the pointers are a different
        // size to the offsets...
        primitive_job_key_arrays[i].index   = keys_index   + accum_offsets.index;
        primitive_job_key_arrays[i].vertex  = keys_vertex  + accum_offsets.vertex;
        primitive_job_key_arrays[i].tex     = keys_tex     + accum_offsets.tex;
        primitive_job_key_arrays[i].sampler = keys_sampler + accum_offsets.sampler;

        // For every primitive in the job, add its key counts to the to accumulator in order to
        // offset the key arrays.
        for(u32 j = 0; j < primitive_job_sizes[i]; ++j) {
            // accum_offsets.index += key_counts[idx].index; accum_offsets.vertex  += key_counts[idx].vertex;
            // accum_offsets.tex   += key_counts[idx].tex;   accum_offsets.sampler += key_counts[idx].sampler;

            a = _mm_load_si128((__m128i*)(key_counts + idx));
            b = _mm_load_si128((__m128i*)(&accum_offsets));
            a = _mm_add_epi32(a, b);
            _mm_store_si128((__m128i*)(&accum_offsets), a);

            idx++;
        }
        // primitive_job_key_arrays[i].cap_index = accum_offsets.index; primitive_job_key_arrays[i].cap_vertex  = accum_offsets.vertex;
        // primitive_job_key_arrays[i].cap_tex   = accum_offsets.tex;   primitive_job_key_arrays[i].cap_sampler = accum_offsets.sampler;
        //
        // Store the accumulator offsets of the next job as the caps of the current job
        a = _mm_load_si128((__m128i*)(&accum_offsets));

        tmp_addr = (__m128i*)((u8*)(primitive_job_key_arrays + i) + offsetof(Allocation_Key_Arrays, lens));

        _mm_store_si128(tmp_addr, a);
    }
    assert(idx == count && "We should have visited every primitive in the 'accum_offsets' loop");


    // @Multithreading The below loop simulates multhreading without me having to yet go through the
    // rigmarole of setting it all up completely. This should be behave in the exact same fashion as
    // the real threaded version, as synchronisation has no effect here, only the range which is written.

    Material_Ubo_Allocators material_ubo_allocators[g_thread_count];

    for(u32 i = 0; i < g_thread_count; ++i) {
        material_ubo_allocators[i].uniform    = &g_assets->model_allocators.uniform[i];
        material_ubo_allocators[i].descriptor = &g_assets->model_allocators.descriptor_resource[i];

        // I do not love this formatting (the non contiguous offsets) but return locations must be last...
        load_primitive_info(
            primitive_job_sizes[i],
            primitives           + primitive_job_offsets[i],
           &primitive_job_key_arrays[i],
            pl_primitive_infos   + primitive_job_offsets[i],
            primitive_draw_infos + primitive_job_offsets[i],
           &material_ubo_allocators[i]);
    }

    // I do not want to fabricate all the stuff required to make the tests successfully compile the pipelines.
    // It is better to test that with real use case stuff. I can see that it is trying to compile the pipelines
    // and that is good enough.
    #if !(TEST)
    VkPipeline *pipelines = (VkPipeline*)malloc_t(sizeof(VkPipeline) * count);
    for(u32 i = 0; i < g_thread_count; ++i) {
        pl_create_pipelines(
            primitive_job_sizes[i],
            pl_primitive_infos   + primitive_job_offsets[i],
            pipeline_configs     + primitive_job_offsets[i],
            pipelines            + primitive_job_offsets[i]);
    }
    #endif

    // @Multithreading Wait for threads to return, and signal allocators to queue allocation keys in their
    // respective array. Each allocator queue will be controlled by a thread.

    const u32 result_mask_count = 17; // must be large enough to allow out of bounds indexing by one in the below loop.

    assert(result_mask_count * 64 > accum_offsets.index  + 1 && "Insufficient Result Count");
    assert(result_mask_count * 64 > accum_offsets.vertex + 1 && "Insufficient Result Count");
    assert(result_mask_count * 64 > accum_offsets.tex    + 1 && "Insufficient Result Count");

    u64 results_index_stage  [result_mask_count];
    u64 results_vertex_stage [result_mask_count];
    u64 results_tex_stage    [result_mask_count];
    u64 results_index_upload [result_mask_count];
    u64 results_vertex_upload[result_mask_count];
    u64 results_tex_upload   [result_mask_count];

    // @Note Not currently acquiring samplers in this phase. Although I could, I think it would
    // be more appropriate to get them at the image view phase. Idk though, maybe I should acquire
    // them here just to warm up the cache.

    Gpu_Allocator     *index_allocator    = &g_assets->model_allocators.index;
    Gpu_Allocator     *vertex_allocator   = &g_assets->model_allocators.vertex;
    Gpu_Tex_Allocator *tex_allocator      = &g_assets->model_allocators.tex;

    // @Multithreading I want to remiplement the allocators in a fashion closer to how the uniform
    // and descriptor allocators exist in the model allocators struct: single buffers with multiple
    // user structs operating at offsets. At some point I can add work stealing to this as well.
    // But than can some later, I want to operate without sync for as long as possible, and I assume
    // that you need it for work stealing (threads would need to know where it is safe to take from
    // I assume).

    // Staging Queues
    bool result_stage_index  = attempt_to_queue_allocations_staging(index_allocator, accum_offsets.index,
                                   keys_index, results_index_stage, adjust_weights);
    bool result_stage_vertex = attempt_to_queue_allocations_staging(vertex_allocator, accum_offsets.vertex,
                                   keys_vertex, results_vertex_stage, adjust_weights);
    bool result_stage_tex    = attempt_to_queue_tex_allocations_staging(tex_allocator, accum_offsets.tex,
                                   keys_tex, results_tex_stage, adjust_weights);

    // Upload Queues
    bool result_upload_index  = attempt_to_queue_allocations_upload(index_allocator,   accum_offsets.index,
                                    keys_index,  results_index_upload, adjust_weights);
    bool result_upload_vertex = attempt_to_queue_allocations_upload(vertex_allocator,  accum_offsets.vertex,
                                    keys_vertex, results_vertex_upload, adjust_weights);
    bool result_upload_tex    = attempt_to_queue_tex_allocations_upload(tex_allocator, accum_offsets.tex,
                                    keys_tex,    results_tex_upload,    adjust_weights);

    // @Note the above bool results are unused, but later they can be used to just return the function,
    // as a true result would mean that all allocations successfully queued, so no need for next step.

    // Parse upload results to understand which primitives can be drawn and which need to be requeued.
    // Remove allocations from queues if other allocations for that primitive were not queued. This avoids
    // the case where some primitive late in the array successfully queues some of its allocations, then
    // on a requeue, earlier allocations are unable to queue all of their dependencies because of space
    // being take up later allocations which also cannot draw because queue space is taken first by earlier
    // primitives.

    // Really I could just reuse the result masks here, but this is a bit clearer to follow.
    u64 success_masks_index [result_mask_count];
    u64 success_masks_vertex[result_mask_count];
    u64 success_masks_tex   [result_mask_count];

    // Below loop assumes that per primitive, the number of allocations of each type (index, or vertex, or tex)
    // is less than 64.

    const u64 one = 1;
    u32 result_mask_idx;
    u32 result_count;
    u32 result_pos = 0;
    u64 result64; // a big bool that I can shift by more that 31

    // @Multithreading This looks dumb as I loop keys_counts 3 times, but these loops will become 3 jobs,
    // and separating out the loops like this makes that easier.
    for(u32 i = 0; i < count; ++i) {
        result_count    = key_counts[i].index;
        result_mask_idx = result_pos >> 6;

        // Idk how to format this...
        result64 = check_upload_stage_results_against_req_count(result_count, result_pos & 63,
                                                                results_index_stage  + result_mask_idx,
                                                                results_index_upload + result_mask_idx);

        success_masks_index[i >> 6] &= ~(one << (i & 63));
        success_masks_index[i >> 6] |= result64 << (i & 63);

        result_pos += result_count;
    }

    result_pos = 0;
    for(u32 i = 0; i < count; ++i) {
        result_count    = key_counts[i].vertex;
        result_mask_idx = result_pos >> 6;

        // Idk how to format this...
        result64 = check_upload_stage_results_against_req_count(result_count, result_pos & 63,
                                                                results_vertex_stage  + result_mask_idx,
                                                                results_vertex_upload + result_mask_idx);

        success_masks_vertex[i >> 6] &= ~(one << (i & 63));
        success_masks_vertex[i >> 6] |= result64 << (i & 63);

        result_pos += result_count;
    }

    result_pos = 0;
    for(u32 i = 0; i < count; ++i) {
        result_count    = key_counts[i].tex;
        result_mask_idx = result_pos >> 6;

        // Idk how to format this...
        result64 = check_upload_stage_results_against_req_count(result_count, result_pos & 63,
                                                                results_tex_stage  + result_mask_idx,
                                                                results_tex_upload + result_mask_idx);

        success_masks_tex[i >> 6] &= ~(one << (i & 63));
        success_masks_tex[i >> 6] |= result64 << (i & 63);

        result_pos += result_count;
    }

    u64 *success_masks = (u64*)malloc_t(sizeof(u64) * (align(count, 64) / 64));

    // Collapse allocation results
    __m128i c;
    u32 success_mask_count = align(count, 64) >> 6;
    for(u32 i = 0; i < success_mask_count; i += 2) {
        a = _mm_load_si128((__m128i*)(success_masks_index  + i));
        b = _mm_load_si128((__m128i*)(success_masks_vertex + i));
        c = _mm_load_si128((__m128i*)(success_masks_tex    + i));
        a = _mm_and_si128(a, b);
        a = _mm_and_si128(a, c);
        _mm_store_si128((__m128i*)(success_masks + i), a);
    }

    // Below Arrays: growable == false, temp_array == true

    // Keys to remove from queues, as they were successfully queued, but cannot be drawn as other
    // allocations for the primitive they belong to were not queued.
    Array<u32> to_remove_keys_index  = new_array<u32>(accum_offsets.index,  false, true);
    Array<u32> to_remove_keys_vertex = new_array<u32>(accum_offsets.vertex, false, true);
    Array<u32> to_remove_keys_tex    = new_array<u32>(accum_offsets.tex,    false, true);

    // To reload allocations without having to parse the primitives again.
    Array<u32> failed_keys_index   = new_array<u32>(accum_offsets.index,   false, true);
    Array<u32> failed_keys_vertex  = new_array<u32>(accum_offsets.vertex,  false, true);
    Array<u32> failed_keys_tex     = new_array<u32>(accum_offsets.tex,     false, true);
    Array<u32> failed_keys_sampler = new_array<u32>(accum_offsets.sampler, false, true);

    // To call get allocation for upload offsets without having to reparse primitives.
    Array<u32> success_keys_index   = new_array<u32>(accum_offsets.index,   false, true);
    Array<u32> success_keys_vertex  = new_array<u32>(accum_offsets.vertex,  false, true);
    Array<u32> success_keys_tex     = new_array<u32>(accum_offsets.tex,     false, true);
    Array<u32> success_keys_sampler = new_array<u32>(accum_offsets.sampler, false, true);

    // @Multithreading See previous multithreading comment (two loops up).
    // @Multithreading @Test False sharing on the jobs below.
    //
    // @SIMD I would like to make this simd, but I cannot see how to do so without using unaligned loads.
    // Maybe that would still be faster, but since I am not certain of the benefit, I will still to this for now.
    result_pos = 0;
    bool tmp_bool;
    for(u32 i = 0; i < count; ++i) {
        result_count = key_counts[i].index;

        result64 = success_masks[i >> 6] & (one << (i & 63));

        // Add the keys for the failed primitives to the the corresponding array. If an allocation was successfully
        // queued, but other required allocations for this primitive were not queued, add its key to 'to_remove'.
        for(u32 j = result_pos; j < result_pos + result_count; ++j) {

            tmp_bool = (results_index_stage[j >> 6] & results_index_upload[j >> 6]) & (one << (j & 63));

            array_add_if_true(&to_remove_keys_index, keys_index[j], tmp_bool);
            array_add(&failed_keys_index, keys_index[j]);
        }

        // If every allocation for this primitive was successfully loaded, add the key to the 'ready' array.
        for(u32 j = result_pos; j < result_pos + (result_count & max64_if_true(result64)); ++j) {
            array_add(&success_keys_index, keys_index[j]);
        }

        result_pos += result_count;
    }

    result_pos = 0;
    for(u32 i = 0; i < count; ++i) {
        result_count = key_counts[i].vertex;

        result64 = success_masks[i >> 6] & (one << (i & 63));

        // Add the keys for the failed primitives to the the corresponding array. If an allocation was successfully
        // queued, but other required allocations for this primitive were not queued, add its key to 'to_remove'.
        for(u32 j = result_pos; j < result_pos + result_count; ++j) {

            tmp_bool = (results_vertex_stage[j >> 6] & results_vertex_upload[j >> 6]) & (one << (j & 63));

            array_add_if_true(&to_remove_keys_vertex, keys_vertex[j], tmp_bool);
            array_add(&failed_keys_vertex, keys_vertex[j]);
        }

        // If every allocation for this primitive was successfully loaded, add the key to the 'ready' array.
        for(u32 j = result_pos; j < result_pos + (result_count & max64_if_true(result64)); ++j) {
            array_add(&success_keys_vertex, keys_vertex[j]);
        }

        result_pos += result_count;
    }

    result_pos = 0;
    for(u32 i = 0; i < count; ++i) {
        result_count = key_counts[i].tex;

        result64 = success_masks[i >> 6] & (one << (i & 63));

        // Add the keys for the failed primitives to the the corresponding array. If an allocation was successfully
        // queued, but other required allocations for this primitive were not queued, add its key to 'to_remove'.
        for(u32 j = result_pos; j < result_pos + result_count; ++j) {

            tmp_bool = (results_tex_stage[j >> 6] & results_tex_upload[j >> 6]) & (one << (j & 63));

            array_add_if_true(&to_remove_keys_tex, keys_tex[j], tmp_bool);
            array_add(&failed_keys_tex,     keys_tex[j]);
            array_add(&failed_keys_sampler, keys_sampler[j]);
        }

        // If every allocation for this primitive was successfully loaded, add the key to the 'ready' array.
        for(u32 j = result_pos; j < result_pos + (result_count & max64_if_true(result64)); ++j) {
            array_add(&success_keys_tex,     keys_tex[j]);
            array_add(&success_keys_sampler, keys_sampler[j]);
        }

        result_pos += result_count;
    }

    // @Multithreading These will each be a job for the corresponding allocator threads.

    for(u32 i = 0; i < to_remove_keys_index.len; ++i) {
        staging_queue_upload_queue_remove(index_allocator, to_remove_keys_index.data[i]);
    }

    for(u32 i = 0; i < to_remove_keys_vertex.len; ++i) {
        staging_queue_upload_queue_remove(vertex_allocator, to_remove_keys_vertex.data[i]);
    }

    for(u32 i = 0; i < to_remove_keys_tex.len; ++i) {
        tex_staging_queue_upload_queue_remove(tex_allocator, to_remove_keys_tex.data[i]);
    }

    Primitive_Draw_Prep_Result ret = {};

    ret.success_masks        = success_masks;
    ret.primitive_draw_infos = primitive_draw_infos;

    ret.failed_keys.index       = failed_keys_index.data;
    ret.failed_keys.vertex      = failed_keys_vertex.data;
    ret.failed_keys.tex         = failed_keys_tex.data;
    ret.failed_keys.lens.index  = failed_keys_index.len;
    ret.failed_keys.lens.vertex = failed_keys_vertex.len;
    ret.failed_keys.lens.tex    = failed_keys_tex.len;

    ret.success_keys.index        = success_keys_index.data;
    ret.success_keys.vertex       = success_keys_vertex.data;
    ret.success_keys.tex          = success_keys_tex.data;
    ret.success_keys.sampler      = success_keys_sampler.data;
    ret.success_keys.lens.index   = success_keys_index.len;
    ret.success_keys.lens.vertex  = success_keys_vertex.len;
    ret.success_keys.lens.tex     = success_keys_tex.len;
    ret.success_keys.lens.sampler = success_keys_sampler.len;

    bool primitive_load_result = result_stage_index  && result_stage_vertex  && result_stage_tex &&
                                 result_upload_index && result_upload_vertex && result_upload_tex;

    ret.result = static_cast<Primitive_Load_Result>(primitive_load_result == false);

    return ret;
}

// A mostly implemented compressed method of pipeline creation. As I would imagine that the vast marjority of
// pipeline info for each shader will be the same, the below method uses a simple compression system to require
// less config data. But I sort of realised that it is more of a pain to multithread (it would still be simple)
// but I more importantly realised that memory is cheap and I just dont need to worry in this case. Just make
// a couple of configs and broadcast them for each primitive. Memory access pattern becomes more predictable,
// writing the usage code becomes easier. More memory holding duplicate data, but that does not matter.
#if 0
struct Pl_Shader_Info {
    u32          count;
    Shader_Info *infos;
};
struct Pl_Renderpass_Info {
    u32          subpass;
    VkRenderPass renderpass;
};
struct Pl_Stencil_Ops {
    VkStencilOp front;
    VkStencilOp back;
};
struct Pl_Blend_Info {
    u32 count;
    VkPipelineColorBlendAttachmentState *blends;
};

struct Primitive_Pipelines_Config {
    u32                 unique_shaders_count;
    u32                 unique_flags_count;
    u32                 unique_renderpass_count;
    u32                 unique_layout_count;
    u32                 unique_stencil_count; // This is really a subset of flags, hence no count. I just wanted it in a more dynamic storage as it is a lot of bytes that are often unnecessary.
    u32                 unique_blend_count;
    u32                *shader_counts;
    u32                *flags_counts;
    u32                *renderpass_counts;
    u32                *layout_counts;
    u32                *blend_counts;
    Pl_Config_Flags    *flags;
    Pl_Shader_Info     *shaders;
    Pl_Renderpass_Info *renderpasses;
    Pl_Blend_Info      *blends;
    Pl_Stencil_Ops     *stencil_ops;
};

void create_primitive_pipelines(
    u32                         count,
    Pl_Primitive_Info          *primitive_infos,
    Primitive_Pipelines_Config *config,
    VkPipeline                 *pipelines)
{
    VkGraphicsPipelineCreateInfo *infos =
        (VkGraphicsPipelineCreateInfo*)malloc_t(sizeof(VkGraphicsPipelineCreateInfo) * count);

    VkPipelineShaderStageCreateInfo *shader_stages =
         (VkPipelineShaderStageCreateInfo*)malloc_t(sizeof(VkPipelineShaderStageCreateInfo) * config->unique_shader_count);

    VkPipelineVertexInputStateCreateInfo *vertex_input =
        (VkPipelineVertexInputStateCreateInfo*)malloc_t(sizeof(VkPipelineVertexInputStateCreateInfo) * count);

    VkPipelineInputAssemblyStateCreateInfo *input_assembly =
        (VkPipelineInputAssemblyStateCreateInfo*)malloc_t(sizeof(VkPipelineInputAssemblyStateCreateInfo) * count);

    VkPipelineViewportStateCreateInfo viewport;
    pl_get_viewport_and_scissor(&viewport);

    VkPipelineRasterizationStateCreateInfo *rasterization =
        (VkPipelineRasterizationStateCreateInfo*)malloc_t(sizeof(VkPipelineRasterizationStateCreateInfo) * config->unique_flags_count);

    // @Todo Multisampling
    VkPipelineMultisamplerStageCreateInfo multisample;
    pl_get_multisample(&multisample);

    VkPipelineDepthStencilStateCreateInfo *depth_stencil =
        (VkPipelineDepthStencilStateCreateInfo*)malloc_t(sizeof(VkPipelineDepthStencilStateCreateInfo) * config->unique_flags_count);

    VkPipelineColorBlendStateCreateInfo *blend =
        (VkPipelineColorBlendStateCreateInfo*)malloc_t(sizeof(VkPipelineColorBlendStateCreateInfo) * config->unique_blend_count);

    VkPipelineDynamicStateCreateInfo dyn;
    pl_get_dynamic(&dyn);

    u32 tmp = 0;
    for(u32 i = 0; i < config->unique_shaders_count; ++i) {
        pl_get_shader_stages(config->shaders[i].count, config->shaders[i].shader_infos, shader_stages + tmp);

        tmp += config->shaders[i].count;
    }

    for(u32 i = 0; i < count; ++i) {
        pl_get_vertex_input_and_assembly(&primitive_infos[i], &vertex_input[i], &input_assembly[i]);
    }

    for(u32 i = 0; i < config->unique_flag_count; ++i) {
        pl_get_rasterization(config->flags[i], &rasterization[i]);
    }

    tmp = 0;
    for(u32 i = 0; i < config->unique_flag_count; ++i) {
        pl_get_depth_stencil(config->flags[i], config->stencil_ops + tmp, depth_stencil[i]);

        // stencil tests being enabled is the criteria to consume an op in the array.
        tmp += (config->flags[i] & PL_CONFIG_STENCIL_TEST_ENABLE_BIT) > 0;
    }

    for(u32 i = 0; i < unique_blend_count; ++i) {
        pl_get_color_blend(&config->blends[i], &blend[i]);
    }

    VkGraphicsPipelineCreateInfo *pl_create_infos =
        (VkGraphicsPipelineCreateInfo*)malloc_t(sizeof(VkGraphicsPipelineCreateInfo) * count);

    for(u32 i = 0; i < count; ++i) {
        pl_create_infos[i].flags               = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        pl_create_infos[i].pVertexInputState   = &vertex_input[i];
        pl_create_infos[i].pInputAssemblyState = &input_assembly[i];
        pl_create_infos[i].pViewportState      = &viewport;
        pl_create_infos[i].pMultisampleState   = &multisample;
        pl_create_infos[i].pDynamicState       = &dyn;
    }

    u32 flags_index      = 0;
    u32 shader_index     = 0;
    u32 renderpass_index = 0;
    u32 layout_index     = 0;
    u32 blend_index      = 0;

    tmp = 0;
    for(u32 i = 0; i < config->unique_stage; ++i) {
        for(u32 j = 0; j < config->shader_counts[i]; ++j) {
            pl_create_infos[tmp].stageCount = config->shaders[i].count;
            pl_create_infos[tmp].pStages    = &shader_stages[i];
            tmp++;
        }
    }
    assert(tmp == count);

    tmp = 0;
    for(u32 i = 0; i < config->unique_flags_count; ++i) {
        for(u32 j = 0; j < config->flags_counts[i]; ++j) {
            pl_create_infos[tmp].pDepthStencilState  = &depth_stencil[i]
            pl_create_infos[tmp].pRasterizationState = &rasterization[i];
            tmp++;
        }
    }
}
#endif

#if TEST // 700 lines of tests EOF
static void test_model_from_gltf();
static void test_load_primitive_allocations();
static void test_get_format_from_accessor_flags();

void test_asset() {
    test_model_from_gltf();
    test_load_primitive_allocations();
    test_get_format_from_accessor_flags();
}


static void test_get_format_from_accessor_flags() {
    BEGIN_TEST_MODULE("Accessor Flags to VkFormat", false, false);

    TEST_EQ("scalar u16",   get_format_from_accessor_flags(ACCESSOR_COMPONENT_TYPE_U16_BIT   | ACCESSOR_TYPE_SCALAR_BIT), VK_FORMAT_R16_UINT,            false);
    TEST_EQ("scalar u32",   get_format_from_accessor_flags(ACCESSOR_COMPONENT_TYPE_U32_BIT   | ACCESSOR_TYPE_SCALAR_BIT), VK_FORMAT_R32_UINT,            false);
    TEST_EQ("scalar float", get_format_from_accessor_flags(ACCESSOR_COMPONENT_TYPE_FLOAT_BIT | ACCESSOR_TYPE_SCALAR_BIT), VK_FORMAT_R32_SFLOAT,          false);
    TEST_EQ("vec2 float",   get_format_from_accessor_flags(ACCESSOR_COMPONENT_TYPE_FLOAT_BIT | ACCESSOR_TYPE_VEC2_BIT),   VK_FORMAT_R32G32_SFLOAT,       false);
    TEST_EQ("vec3 float",   get_format_from_accessor_flags(ACCESSOR_COMPONENT_TYPE_FLOAT_BIT | ACCESSOR_TYPE_VEC3_BIT),   VK_FORMAT_R32G32B32_SFLOAT,    false);
    TEST_EQ("vec4 float",   get_format_from_accessor_flags(ACCESSOR_COMPONENT_TYPE_FLOAT_BIT | ACCESSOR_TYPE_VEC4_BIT),   VK_FORMAT_R32G32B32A32_SFLOAT, false);

    END_TEST_MODULE();
}

// @Note This test function *temporarily* reassigns the g_assets model allocators with ones created from a slightly
// different config (the only change is smaller allocator queues to make testing easier). It reassigns the model
// allocators created by init_assets() at the end.
static void test_load_primitive_allocations() {
    Assets *g_assets = get_assets_instance();
    Model_Allocators model_allocators_from_assets_initialization = g_assets->model_allocators;

    Model_Allocators_Config model_allocators_config = {};

    // Shrink the queues to make the testing simpler.
    model_allocators_config.index_allocator_config_staging_queue_byte_cap = 1024 * 2;
    model_allocators_config.index_allocator_config_upload_queue_byte_cap  = 1024 * 1;
    model_allocators_config.index_allocator_config_stage_bit_granularity  = 64;
    model_allocators_config.index_allocator_config_upload_bit_granularity = 64;

    model_allocators_config.vertex_allocator_config_staging_queue_byte_cap = 1024 * 4;
    model_allocators_config.vertex_allocator_config_upload_queue_byte_cap  = 1024 * 2;
    model_allocators_config.vertex_allocator_config_stage_bit_granularity  = 64;
    model_allocators_config.vertex_allocator_config_upload_bit_granularity = 64;

    model_allocators_config.tex_allocator_config_staging_queue_byte_cap = 1024 * 1024 * 4;
    model_allocators_config.tex_allocator_config_upload_queue_byte_cap  = 1024 * 1024 * 4;
    model_allocators_config.tex_allocator_config_stage_bit_granularity  = 64;
    model_allocators_config.tex_allocator_config_upload_bit_granularity = 64;

    Model_Allocators reassigned_model_allocators = create_model_allocators(&model_allocators_config);
    g_assets->model_allocators = reassigned_model_allocators;

    Model_Allocators *allocators = &g_assets->model_allocators;

    u64 allocation_size = 128;
    u8 *allocation_mem  = malloc_t(allocation_size);

    u32 primitive_count = 128;
    Mesh_Primitive *primitives = (Mesh_Primitive*)malloc_t(sizeof(Mesh_Primitive) * primitive_count);

    String image_names[10] = {
        cstr_to_string("test/images/base1"),
        cstr_to_string("test/images/pbr1"),
        cstr_to_string("test/images/normal1"),
        cstr_to_string("test/images/occlusion1"),
        cstr_to_string("test/images/emissive1"),
    };

    Allocation_Key_Counts *key_counts = (Allocation_Key_Counts*)malloc_t(sizeof(Allocation_Key_Counts) * primitive_count);

    u32 tmp = primitive_count;
    String image_name;
    Gpu_Allocator_Result result;
    for(u32 i = 0; i < primitive_count; ++i) {
        primitives[i] = {};
        primitives[i].key_counts = {1, 4, 5, 5};

        key_counts[i] = primitives[i].key_counts;

        primitives[i].material = {};
        primitives[i].material.flags = MATERIAL_BASE_BIT      | MATERIAL_PBR_BIT       | MATERIAL_NORMAL_BIT    |
                                       MATERIAL_OCCLUSION_BIT | MATERIAL_EMISSIVE_BIT;

        tex_add_texture(&allocators->tex, &image_names[0], &primitives[i].material.pbr.base_color_texture.texture_key);
        tex_add_texture(&allocators->tex, &image_names[1], &primitives[i].material.pbr.metallic_roughness_texture.texture_key);
        tex_add_texture(&allocators->tex, &image_names[2], &primitives[i].material.normal.texture.texture_key);
        tex_add_texture(&allocators->tex, &image_names[3], &primitives[i].material.occlusion.texture.texture_key);
        tex_add_texture(&allocators->tex, &image_names[4], &primitives[i].material.emissive.texture.texture_key);

        result = begin_allocation(&allocators->index);
        assert(result == GPU_ALLOCATOR_RESULT_SUCCESS);

        result = continue_allocation(&allocators->index, allocation_size, allocation_mem);
        assert(result == GPU_ALLOCATOR_RESULT_SUCCESS);

        primitives[i].indices = {};
        result = submit_allocation(&allocators->index, &primitives[i].indices.allocation_key);
        assert(result == GPU_ALLOCATOR_RESULT_SUCCESS);

        primitives[i].attribute_count = 4;
        primitives[i].attributes = (Mesh_Primitive_Attribute*)malloc_t(sizeof(Mesh_Primitive_Attribute) * 4);

        for(u32 j = 0; j < 4; ++j) {
            result = begin_allocation(&allocators->vertex);
            assert(result == GPU_ALLOCATOR_RESULT_SUCCESS);

            result = continue_allocation(&allocators->vertex, allocation_size, allocation_mem);
            assert(result == GPU_ALLOCATOR_RESULT_SUCCESS);

            primitives[i].attributes[j].accessor = {};
            result = submit_allocation(&allocators->vertex, &primitives[i].attributes[j].accessor.allocation_key);
            assert(result == GPU_ALLOCATOR_RESULT_SUCCESS);
        }
    }

    Pl_Config *pl_configs = (Pl_Config*)malloc_t(sizeof(Pl_Config) * primitive_count);
    memset(pl_configs, 0, sizeof(Pl_Config) * primitive_count);

    Primitive_Draw_Prep_Result draw_prep_result = prepare_to_draw_primitives(primitive_count, primitives, key_counts, pl_configs, true);

    BEGIN_TEST_MODULE("Load Primitive Allocations", false, false);

    TEST_EQ("successfully_loaded_primitive_masks[0]", draw_prep_result.success_masks[0], 0x1 | 0x2 | 0x4 | 0x8, false);
    TEST_EQ("successfully_loaded_primitive_masks[1]", draw_prep_result.success_masks[1], 0x0, false);

    TEST_EQ("result code is partial", draw_prep_result.result, PRIMITIVE_LOAD_RESULT_PARTIAL, false);

    Gpu_Tex_Allocation *tex_allocation;
    char buf[127];
    for(u32 i = 0; i < primitive_count; ++i) {
        tex_allocation = gpu_get_tex_allocation(&allocators->tex, i % 5);
        string_format(buf, "get_tex_allocation[%u]", i);
        TEST_STREQ(buf, tex_allocation->file_name.str, image_names[i % 5].str, false);
    }

    END_TEST_MODULE();

    destroy_model_allocators(&g_assets->model_allocators);

    // Restore the actual model allocators (they were reassigned at function start to simplify testing)
    g_assets->model_allocators = model_allocators_from_assets_initialization;
}

static void test_accessor(Gpu_Allocator *allocator, u8 *buf, char *name, Accessor *accessor0, Accessor *accessor1,
                          bool index)
{
    TEST_EQ(name, accessor0->allocation_key, accessor1->allocation_key, false);
    TEST_EQ(name, accessor0->byte_offset,    accessor1->byte_offset,    false);
    TEST_EQ(name, accessor0->flags,          accessor1->flags,          false);
    TEST_EQ(name, accessor0->count,          accessor1->count,          false);

    staging_queue_begin(allocator);
    staging_queue_add(allocator, accessor0->allocation_key, true);
    staging_queue_submit(allocator);
    Gpu_Allocation *allocation = gpu_get_allocation(allocator, accessor0->allocation_key);

    u64 offset = accessor0->byte_offset + allocation->stage_offset;
    if (!index) {
        TEST_EQ(name, memcmp((u8*)allocator->stage_ptr + offset, buf + 2176, 4 * 3 * 16), 0, false);
    } else {
        TEST_EQ(name, memcmp((u8*)allocator->stage_ptr + offset, buf + accessor1->byte_offset, 25 * 2), 0, false);
    }

    if (accessor0->max_min || accessor1->max_min) {
        TEST_FEQ(name, accessor0->max_min->max[0], accessor1->max_min->max[0], false);
        TEST_FEQ(name, accessor0->max_min->min[0], accessor1->max_min->min[0], false);
    }
    if (accessor0->sparse || accessor1->sparse) {
        TEST_EQ(name, accessor0->sparse->indices_component_type, accessor1->sparse->indices_component_type, false);
        TEST_EQ(name, accessor0->sparse->count,                  accessor1->sparse->count,                  false);
        TEST_EQ(name, accessor0->sparse->indices_allocation_key, accessor1->sparse->indices_allocation_key, false);
        TEST_EQ(name, accessor0->sparse->indices_byte_offset,    accessor1->sparse->indices_byte_offset,    false);
        TEST_EQ(name, accessor0->sparse->values_allocation_key,  accessor1->sparse->values_allocation_key,  false);
        TEST_EQ(name, accessor0->sparse->values_byte_offset,     accessor1->sparse->values_byte_offset,     false);
    }
}

static void test_material(Gpu_Tex_Allocator *allocator, char *name, Material *material0, Material *material1, bool one) {

    TEST_EQ(name, material0->flags, material1->flags, false);

    for(u32 i = 0; i < 4; ++i) {
        TEST_FEQ(name, material0->ubo.base_color_factor[i], material1->ubo.base_color_factor[i], false);
    }
    for(u32 i = 0; i < 3; ++i) {
        TEST_FEQ(name, material0->ubo.emissive_factor[i], material1->ubo.emissive_factor[i], false);
    }

    TEST_FEQ(name, material0->ubo.metallic_factor,    material1->ubo.metallic_factor,    false);
    TEST_FEQ(name, material0->ubo.roughness_factor,   material1->ubo.roughness_factor,   false);
    TEST_FEQ(name, material0->ubo.normal_scale,       material1->ubo.normal_scale,       false);
    TEST_FEQ(name, material0->ubo.occlusion_strength, material1->ubo.occlusion_strength, false);

    Gpu_Allocator_Result result = tex_staging_queue_begin(allocator);
    TEST_EQ("tex_staging_queue_begin_result", result, GPU_ALLOCATOR_RESULT_SUCCESS, false);

    if (material0->flags & MATERIAL_BASE_BIT) {
        TEST_EQ(name, material0->pbr.base_color_tex_coord,           material1->pbr.base_color_tex_coord,           false);
        TEST_EQ(name, material0->pbr.base_color_texture.texture_key, material1->pbr.base_color_texture.texture_key, false);

        result = tex_staging_queue_add(allocator, material0->pbr.base_color_texture.texture_key, true);

        if (result != GPU_ALLOCATOR_RESULT_SUCCESS)
            println("Failed to queue base texture");
        // else
        //     println("Queued base texture");
    }
    if (material0->flags & MATERIAL_PBR_BIT) {
        TEST_EQ(name, material0->pbr.metallic_roughness_tex_coord,           material1->pbr.metallic_roughness_tex_coord,           false);
        TEST_EQ(name, material0->pbr.metallic_roughness_texture.texture_key, material1->pbr.metallic_roughness_texture.texture_key, false);

        result = tex_staging_queue_add(allocator, material0->pbr.metallic_roughness_texture.texture_key, true);

        if (result != GPU_ALLOCATOR_RESULT_SUCCESS)
            println("Failed to queue metallic roughness texture");
        // else
        //     println("Queued metallic roughness texture");
    }
    if (material0->flags & MATERIAL_NORMAL_BIT) {
        TEST_EQ(name, material0->normal.tex_coord,           material1->normal.tex_coord,                 false);
        TEST_EQ(name, material0->normal.texture.texture_key, material1->normal.texture.texture_key, false);

        result = tex_staging_queue_add(allocator, material0->normal.texture.texture_key, true);

        if (result != GPU_ALLOCATOR_RESULT_SUCCESS)
            println("Failed to queue normal texture");
        // else
        //     println("Queued normal texture");
    }
    if (material0->flags & MATERIAL_OCCLUSION_BIT) {
        TEST_EQ(name, material0->occlusion.tex_coord,           material1->occlusion.tex_coord,   false);
        TEST_EQ(name, material0->occlusion.texture.texture_key, material1->occlusion.texture.texture_key, false);

        result = tex_staging_queue_add(allocator, material0->occlusion.texture.texture_key, true);

        if (result != GPU_ALLOCATOR_RESULT_SUCCESS)
            println("Failed to queue occlusion texture");
        // else
        //     println("Queued occlusion texture");
    }
    if (material0->flags & MATERIAL_EMISSIVE_BIT) {
        TEST_EQ(name, material0->emissive.tex_coord,           material1->emissive.tex_coord,   false);
        TEST_EQ(name, material0->emissive.texture.texture_key, material1->emissive.texture.texture_key, false);

        result = tex_staging_queue_add(allocator, material0->emissive.texture.texture_key, true);

        if (result != GPU_ALLOCATOR_RESULT_SUCCESS)
            println("Failed to queue emissive texture");
        // else
        //     println("Queued emissive texture");
    }

    result = tex_staging_queue_submit(allocator);
    TEST_EQ("tex_staging_queue_submit_result", result, GPU_ALLOCATOR_RESULT_SUCCESS, false);

    Gpu_Tex_Allocation *allocation;
    Image image;
    String image_name;
    if (material0->flags & MATERIAL_BASE_BIT) {
        allocation = gpu_get_tex_allocation(allocator, material0->pbr.base_color_texture.texture_key);

        if (one)
            image_name = cstr_to_string("test/images/base1");
        else
            image_name = cstr_to_string("test/images/base2");

        image = load_image(&image_name);
        assert(image.width * image.height * 4);

        TEST_EQ("tex_base_staged_data", memcmp((u8*)allocator->stage_ptr + allocation->stage_offset, image.data, image.width * image.height * 4), 0, false);

        free_image(&image);
    }
    if (material0->flags & MATERIAL_PBR_BIT) {
        allocation = gpu_get_tex_allocation(allocator, material0->pbr.base_color_texture.texture_key);

        if (one)
            cstr_to_string("test/images/pbr1");
        else
            cstr_to_string("test/images/pbr2");

        image = load_image(&image_name);
        assert(image.width * image.height * 4);

        TEST_EQ("tex_pbr_staged_data", memcmp((u8*)allocator->stage_ptr + allocation->stage_offset, image.data, image.width * image.height * 4), 0, false);

        free_image(&image);
    }
    if (material0->flags & MATERIAL_NORMAL_BIT) {
        allocation = gpu_get_tex_allocation(allocator, material0->pbr.base_color_texture.texture_key);

        if (one)
            cstr_to_string("test/images/normal1");
        else
            cstr_to_string("test/images/normal2");

        image = load_image(&image_name);
        assert(image.width * image.height * 4);

        TEST_EQ("tex_normal_staged_data", memcmp((u8*)allocator->stage_ptr + allocation->stage_offset, image.data, image.width * image.height * 4), 0, false);

        free_image(&image);
    }
    if (material0->flags & MATERIAL_OCCLUSION_BIT) {
        allocation = gpu_get_tex_allocation(allocator, material0->pbr.base_color_texture.texture_key);

        if (one)
            cstr_to_string("test/images/occlusion1");
        else
            cstr_to_string("test/images/occlusion2");

        image = load_image(&image_name);
        assert(image.width * image.height * 4);

        TEST_EQ("tex_occlusion_staged_data", memcmp((u8*)allocator->stage_ptr + allocation->stage_offset, image.data, image.width * image.height * 4), 0, false);

        free_image(&image);
    }
    if (material0->flags & MATERIAL_EMISSIVE_BIT) {
        allocation = gpu_get_tex_allocation(allocator, material0->pbr.base_color_texture.texture_key);

        if (one)
            cstr_to_string("test/images/emissive1");
        else
            cstr_to_string("test/images/emissive2");

        image = load_image(&image_name);
        assert(image.width * image.height * 4);

        TEST_EQ("tex_emissive_staged_data", memcmp((u8*)allocator->stage_ptr + allocation->stage_offset, image.data, image.width * image.height * 4), 0, false);

        free_image(&image);
    }

}

static void test_model_from_gltf() {
    float min[16] = {
        -0.9999089241027832,
        -4.371139894487897e-8,
        -0.9999366402626038,
        0,
        -4.3707416352845037e-8,
        1,
        -4.37086278282095e-8,
        0,
        -0.9996265172958374,
        0,
        -0.9999089241027832,
        0,
        -1.189831018447876,
        -0.45450031757354736,
        -1.058603048324585,
        1,
    };
    float max[16] = {
        0.9971418380737304,
        -4.371139894487897e-8,
        0.9996265172958374,
        0,
        4.3586464215650273e-8,
        1,
        4.3695074225524884e-8,
        0,
        0.9999366402626038,
        0,
        0.9971418380737304,
        0,
        1.1374080181121828,
        0.44450080394744873,
        1.0739599466323853,
        1
    };

    Accessor_Max_Min max_min[2];
    max_min[0].max[0] = 4212;
    max_min[0].min[0] = 0;
    for(u32 i = 0; i < 16; ++i) {
        max_min[1].max[i] = max[i];
        max_min[1].min[i] = min[i];
    }

    Accessor_Sparse sparse[2] = {
        {
            .indices_component_type = ACCESSOR_COMPONENT_TYPE_U16_BIT,
            .count = 25,
            .indices_allocation_key = 0,
            .values_allocation_key = 0,
            .indices_byte_offset = 0,
            .values_byte_offset = 0,
        },
        {
            .indices_component_type = ACCESSOR_COMPONENT_TYPE_U16_BIT,
            .count = 25,
            .indices_allocation_key = 1,
            .values_allocation_key = 1,
            .indices_byte_offset = 50,
            .values_byte_offset = 192,
        }
    };

    Accessor accessors[6] = {
        {
            .flags = ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT,
            .allocation_key = 0,
            .byte_offset = 0,
            .count = 25,
            .max_min = &max_min[0]
        },
        {
            .flags = ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT,
            .allocation_key = 1,
            .byte_offset = 50,
            .count = 25,
            .max_min = &max_min[0]
        },
        { // @Unused Matrix allocations untested
            .flags = ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT,
            .allocation_key = 0,
            .byte_offset = 0,
            .count = 16,
            .max_min = &max_min[1]
        },
        { // @Unused Matrix allocations untested
            .flags = ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT,
            .allocation_key = 0,
            .byte_offset = 1024,
            .count = 16,
            .max_min = &max_min[1]
        },
        {
            .flags = ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT,
            .allocation_key = 0,
            .byte_offset = 0,
            .count = 16,
            .sparse = &sparse[0],
        },
        {
            .flags = ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT,
            .allocation_key = 1,
            .byte_offset = 192,
            .count = 16,
            .sparse = &sparse[1],
        },
    };

    Mesh_Primitive_Attribute attributes0[4] = {
        {
            .n = 0,
            .accessor = accessors[4],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL,
        },
        {
            .n = 0,
            .accessor = accessors[5],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION,
        },
        {
            .n = 0,
            .accessor = accessors[4],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT,
        },
        {
            .n = 0,
            .accessor = accessors[5],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS,
        },
    };

    Mesh_Primitive_Attribute attributes1[4] = {
        {
            .n = 0,
            .accessor = accessors[5],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL,
        },
        {
            .n = 0,
            .accessor = accessors[4],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION,
        },
        {
            .n = 0,
            .accessor = accessors[5],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT,
        },
        {
            .n = 0,
            .accessor = accessors[4],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS,
        },
    };

    Mesh_Primitive_Attribute targets0[4] = {
        {
            .n = 0,
            .accessor = accessors[4],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL,
        },
        {
            .n = 0,
            .accessor = accessors[5],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION,
        },
        {
            .n = 0,
            .accessor = accessors[4],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT,
        },
    };

    Mesh_Primitive_Attribute targets1[4] = {
        {
            .n = 0,
            .accessor = accessors[5],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION,
        },
        {
            .n = 1,
            .accessor = accessors[4],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS,
        },
        {
            .n = 1,
            .accessor = accessors[5],
            .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS,
        },
    };

    float weights0[2] = {2, 1};
    float weights1[2] = {1, 2};

    Get_Sampler_Info sampler = {
        .wrap_s = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .wrap_t = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mag_filter = VK_FILTER_LINEAR,
        .min_filter = VK_FILTER_LINEAR,
    };

    Material materials[2] = {
        {
            .flags = MATERIAL_BASE_BIT | MATERIAL_PBR_BIT | MATERIAL_NORMAL_BIT | MATERIAL_OPAQUE_BIT,
            .pbr = {
                .base_color_texture = {.texture_key = 0},
                .metallic_roughness_texture = {.texture_key = 1, .sampler_key = 0},
                .base_color_tex_coord = 1,
                .metallic_roughness_tex_coord = 0,
            },
            .normal = {
                .texture = {.texture_key = 2},
                .tex_coord = 1,
            },
            .emissive = {},
            .ubo = {
                .base_color_factor = {0.5,0.5,0.5,1.0},
                .metallic_factor = 1,
                .roughness_factor = 1,
                .normal_scale = 2,
                .occlusion_strength = 1,
                .emissive_factor = {0.2, 0.1, 0.0}
            },
        },
        {
            .flags = MATERIAL_BASE_BIT      | MATERIAL_PBR_BIT      | MATERIAL_NORMAL_BIT |
                     MATERIAL_OCCLUSION_BIT | MATERIAL_EMISSIVE_BIT | MATERIAL_OPAQUE_BIT,
            .pbr = {
                .base_color_texture = {.texture_key = 5, .sampler_key = 0},
                .metallic_roughness_texture = {.texture_key = 6, .sampler_key = 0},
                .base_color_tex_coord = 0,
                .metallic_roughness_tex_coord = 1,
            },
            .normal = {
                .texture = {.texture_key = 7, .sampler_key = 0},
                .tex_coord = 1,
            },
            .occlusion = {
                .texture = {.texture_key = 8, .sampler_key = 0},
                .tex_coord = 1,
            },
            .emissive = {
                .texture = {.texture_key = 9, .sampler_key = 0},
                .tex_coord = 0,
            },
            .ubo = {
                .base_color_factor = {2.5,4.5,2.5,1.0},
                .metallic_factor = 5,
                .roughness_factor = 6,
                .normal_scale = 1,
                .occlusion_strength = 0.679,
                .emissive_factor = {0.2, 0.1, 0.0},
            },
        },
    };

    Model_Allocators_Config model_allocators_config = {};
    Model_Allocators model_allocators = create_model_allocators(&model_allocators_config);

    u32 size = 1024 * 16;
    u8 *model_buffer = malloc_t(size);

    u64 req_size;
    String model_dir  = cstr_to_string("test/");
    String model_name = cstr_to_string("test_gltf2.gltf");
    Model  model      = model_from_gltf(&model_allocators, &model_dir, &model_name, size, model_buffer, &req_size);

    u8 *buf = (u8*)file_read_bin_temp_large("test/buf.bin", 10'000);

    Gpu_Allocator     *index_allocator   = &model_allocators.index;
    Gpu_Allocator     *vertex_allocator  = &model_allocators.vertex;
    Gpu_Tex_Allocator *texture_allocator = &model_allocators.tex;

    BEGIN_TEST_MODULE("Asset_Test_Meshes", false, false);

    char name_buf[127];

    Mesh *meshes = model.meshes;

    // Meshes[0]
    test_accessor(index_allocator, buf, name_buf, &meshes[0].primitives[0].indices, &accessors[0], true);
    test_accessor(index_allocator, buf, name_buf, &meshes[0].primitives[1].indices, &accessors[1], true);

    string_format(name_buf, "meshes[0].primitives[0].material");
    test_material(texture_allocator, name_buf, &meshes[0].primitives[0].material, &materials[0], true);
    string_format(name_buf, "meshes[0].primitives[1].material");
    test_material(texture_allocator, name_buf, &meshes[0].primitives[1].material, &materials[1], true);

    for(u32 i = 0; i < 4; ++i) {
        string_format(name_buf, "meshes[0].primitives[0].attributes[%u]", i);
        TEST_EQ(name_buf, meshes[0].primitives[0].attributes[i].n,    attributes0[i].n, false);
        TEST_EQ(name_buf, meshes[0].primitives[0].attributes[i].type, attributes0[i].type, false);

        test_accessor(vertex_allocator, buf, name_buf, &meshes[0].primitives[0].attributes[i].accessor, &attributes0[i].accessor, false);
    }
    for(u32 i = 0; i < 4; ++i) {
        string_format(name_buf, "meshes[0].primitives[1].attributes[%u]", i);
        TEST_EQ(name_buf, meshes[0].primitives[1].attributes[i].n,    attributes1[i].n, false);
        TEST_EQ(name_buf, meshes[0].primitives[1].attributes[i].type, attributes1[i].type, false);

        test_accessor(vertex_allocator, buf, name_buf, &meshes[0].primitives[1].attributes[i].accessor, &attributes1[i].accessor, false);
    }

    // Targets
    for(u32 i = 0; i < 3; ++i) {
        string_format(name_buf, "meshes[0].primitives[1].targets[0].attributes[%u]", i);
        TEST_EQ(name_buf, meshes[0].primitives[1].targets[0].attributes[i].n,    targets0[i].n, false);
        TEST_EQ(name_buf, meshes[0].primitives[1].targets[0].attributes[i].type, targets0[i].type, false);

        test_accessor(vertex_allocator, buf, name_buf, &meshes[0].primitives[1].targets[0].attributes[i].accessor, &targets0[i].accessor, false);
    }
    #if 1
    for(u32 i = 0; i < 3; ++i) {
        string_format(name_buf, "meshes[0].primitives[1].targets[1].attributes[%u]", i);
        TEST_EQ(name_buf, meshes[0].primitives[1].targets[1].attributes[i].n,    targets1[i].n, false);
        TEST_EQ(name_buf, meshes[0].primitives[1].targets[1].attributes[i].type, targets1[i].type, false);

        test_accessor(vertex_allocator, buf, name_buf, &meshes[0].primitives[1].targets[1].attributes[i].accessor, &targets1[i].accessor, false);
    }
    #endif

    // Meshes[1]
    test_accessor(index_allocator, buf, name_buf, &meshes[1].primitives[0].indices, &accessors[1], true);
    test_accessor(index_allocator, buf, name_buf, &meshes[1].primitives[1].indices, &accessors[0], true);
    test_accessor(index_allocator, buf, name_buf, &meshes[1].primitives[2].indices, &accessors[1], true);

    string_format(name_buf, "meshes[1].primitives[0].material");
    test_material(texture_allocator, name_buf, &meshes[1].primitives[0].material, &materials[0], true);
    string_format(name_buf, "meshes[1].primitives[1].material");
    test_material(texture_allocator, name_buf, &meshes[1].primitives[1].material, &materials[1], true);
    string_format(name_buf, "meshes[1].primitives[2].material");
    test_material(texture_allocator, name_buf, &meshes[1].primitives[2].material, &materials[0], true);

    for(u32 i = 0; i < 4; ++i) {
        string_format(name_buf, "meshes[1].primitives[0].attributes[%u]", i);
        TEST_EQ(name_buf, meshes[1].primitives[0].attributes[i].n,    attributes0[i].n, false);
        TEST_EQ(name_buf, meshes[1].primitives[0].attributes[i].type, attributes0[i].type, false);

        test_accessor(vertex_allocator, buf, name_buf, &meshes[0].primitives[0].attributes[i].accessor, &attributes0[i].accessor, false);
    }
    for(u32 i = 0; i < 4; ++i) {
        string_format(name_buf, "meshes[1].primitives[1].attributes[%u]", i);
        TEST_EQ(name_buf, meshes[1].primitives[1].attributes[i].n,    attributes1[i].n, false);
        TEST_EQ(name_buf, meshes[1].primitives[1].attributes[i].type, attributes1[i].type, false);

        test_accessor(vertex_allocator, buf, name_buf, &meshes[1].primitives[1].attributes[i].accessor, &attributes1[i].accessor, false);
    }

    for(u32 i = 0; i < 4; ++i) {
        string_format(name_buf, "meshes[1].primitives[0].attributes[%u]", i);
        TEST_EQ(name_buf, meshes[1].primitives[0].attributes[i].n,    attributes0[i].n,    false);
        TEST_EQ(name_buf, meshes[1].primitives[0].attributes[i].type, attributes0[i].type, false);

        test_accessor(vertex_allocator, buf, name_buf, &meshes[0].primitives[0].attributes[i].accessor, &attributes0[i].accessor, false);
    }

    destroy_model_allocators(&model_allocators);

    END_TEST_MODULE();
}

#endif // if TEST
