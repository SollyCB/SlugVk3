#include "asset.hpp"
#include "gltf.hpp"
#include "file.hpp"
#include "array.hpp"
#include "vulkan_errors.hpp"

#if TEST
#include "test/test.hpp"
#endif

static Assets s_Assets;
Assets* get_assets_instance() { return &s_Assets; }

struct Model_Allocators_Config {}; // @Unused I am just setting some arbitrary size defaults set in gpu.hpp atm.

static Model_Allocators init_model_allocators(Model_Allocators_Config *config);
static void             shutdown_model_allocators();

void init_assets() {
    Model_Allocators_Config config = {};
    init_model_allocators(&config);

    Assets           *g_assets = get_assets_instance();
    Model_Allocators *allocs   = &g_assets->model_allocators;

    g_assets->model_buffer =    (u8*)malloc_h(g_model_buffer_size, 16);
    g_assets->models       = (Model*)malloc_h(sizeof(Model) * g_model_count, 16);

    u64 model_buffer_size_used      = 0;
    u64 model_buffer_size_available = g_model_buffer_size;

    u64 tmp_size;
    for(u32 i = 0; i < g_model_count; ++i) {
        g_assets->models[i] = model_from_gltf(allocs, &g_model_file_names[i], model_buffer_size_available,
                                              g_assets->model_buffer, &tmp_size);

        assert(model_buffer_size_available >= model_buffer_size_used)
        model_buffer_size_used      += tmp_size;
        model_buffer_size_available -= model_buffer_size_used;

        g_assets->model_count++;
    }

    bool growable_array = true;
    bool temp_array     = false;

    g_assets->keys_tex           = new_array<u32>(g_assets_keys_array_tex_len,        growable_array, temp_array);
    g_assets->keys_index         = new_array<u32>(g_assets_keys_array_index_len,      growable_array, temp_array);
    g_assets->keys_vertex        = new_array<u32>(g_assets_keys_array_vertex_len,     growable_array, temp_array);
    g_assets->keys_sampler       = new_array<u64>(g_assets_keys_array_sampler_len,    growable_array, temp_array);
    g_assets->keys_image_view    = new_array<u64>(g_assets_keys_array_image_view_len, growable_array, temp_array);

    g_assets->results_tex        = new_array<bool>(g_assets_keys_array_tex_len,        growable_array, temp_array);
    g_assets->results_index      = new_array<bool>(g_assets_keys_array_index_len,      growable_array, temp_array);
    g_assets->results_vertex     = new_array<bool>(g_assets_keys_array_vertex_len,     growable_array, temp_array);
    g_assets->results_sampler    = new_array<bool>(g_assets_keys_array_sampler_len,    growable_array, temp_array);
    g_assets->results_image_view = new_array<bool>(g_assets_keys_array_image_view_len, growable_array, temp_array);

    g_assets->pipelines          = new_array<VkPipeline>(256, true, false);

    g_assets->model_count = g_model_count;
    g_assets->models      = (Model*)malloc_h(sizeof(Model) * g_model_count, 8);

    g_assets->semaphores[0] = create_semaphore();
    g_assets->semaphores[1] = create_semaphore();
    g_assets->fences[0]     = create_fence(false);
    g_assets->fences[1]     = create_fence(false);

    Gpu *gpu        = get_gpu_instance();
    VkDevice device = gpu->device;

    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = gpu->transfer_queue_index;

    auto check = vkCreateCommandPool(device, &pool_info, ALLOCATION_CALLBACKS, &g_assets->cmd_pools[0]);
    DEBUG_OBJ_CREATION(vkCreateCommandPool, check);
    check = vkCreateCommandPool(device, &pool_info, ALLOCATION_CALLBACKS, &g_assets->cmd_pools[1]);
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

    free_h(g_assets->models);
    shutdown_model_allocators();

    free_array<u32>(&g_assets->keys_tex       );
    free_array<u32>(&g_assets->keys_index     );
    free_array<u32>(&g_assets->keys_vertex    );
    free_array<u64>(&g_assets->keys_sampler   );
    free_array<u64>(&g_assets->keys_image_view);

    free_array<bool>(&g_assets->results_tex       );
    free_array<bool>(&g_assets->results_index     );
    free_array<bool>(&g_assets->results_vertex    );
    free_array<bool>(&g_assets->results_sampler   );
    free_array<bool>(&g_assets->results_image_view);

    free_array<VkPipeline>(&g_assets->pipelines);


    destroy_semaphore(g_assets->semaphores[0]);
    destroy_semaphore(g_assets->semaphores[1]);
    destroy_fence(g_assets->fences[0]);
    destroy_fence(g_assets->fences[1]);
}

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

    Sampler_Allocator    sampler    = create_sampler_allocator(0);
    Image_View_Allocator image_view = create_image_view_allocator(256);

    Descriptor_Allocator descriptor_sampler  = get_descriptor_allocator(DESCRIPTOR_BUFFER_SIZE, gpu->memory.sampler_descriptor_ptr,  gpu->memory.sampler_descriptor_buffer);
    Descriptor_Allocator descriptor_resource = get_descriptor_allocator(DESCRIPTOR_BUFFER_SIZE, gpu->memory.resource_descriptor_ptr, gpu->memory.resource_descriptor_buffer);

    Model_Allocators ret = {
        .index               = index_allocator,
        .vertex              = vertex_allocator,
        .tex                 = tex_allocator,
        .sampler             = sampler,
        .image_view          = image_view,
        .descriptor_sampler  = descriptor_sampler,
        .descriptor_resource = descriptor_resource,
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

    Gltf_Accessor *gltf_accessor = gltf->accessors;
    for(u32 i = 0; i < accessor_count; ++i) {
        sparse_count  += gltf_accessor->sparse_count != 0;
        max_min_count += gltf_accessor->max != NULL;

        gltf_accessor = (Gltf_Accessor*)((u8*)gltf_accessor + gltf_accessor->stride);
    }

    // Meshes: weights, primitive attributes, morph targets
    u32 weight_count           = 0;
    u32 primitive_count        = 0;
    u32 attribute_count        = 0;
    u32 target_count           = 0;
    u32 target_attribute_count = 0;

    Gltf_Mesh           *gltf_mesh = gltf->meshes;
    Gltf_Mesh_Primitive *gltf_primitive;
    Gltf_Morph_Target   *gltf_morph_target;
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

                gltf_morph_target = (Gltf_Morph_Target*)((u8*)gltf_morph_target + gltf_morph_target->stride);
            }

            gltf_primitive = (Gltf_Mesh_Primitive*)((u8*)gltf_primitive + gltf_primitive->stride);
        }

        gltf_mesh = (Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }


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

static void model_load_gltf_accessors(u32 count, Gltf_Accessor *gltf_accessors, Accessor *accessors, u8 *buffer) {
    u32 tmp_component_count;
    u32 tmp_component_width;
    u32 tmp;

    Gltf_Accessor *gltf_accessor = gltf_accessors;

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

        gltf_accessor = (Gltf_Accessor*)((u8*)gltf_accessor + gltf_accessor->stride);
    }
}

static void model_load_gltf_materials(u32 count, Gltf_Material *gltf_materials, Material *materials) {

    Gltf_Material *gltf_material = gltf_materials;

    for(u32 i = 0; i < count; ++i) {
        materials[i] = {};

        materials[i].flags |= MATERIAL_BASE_BIT      & max32_if_true(gltf_material->base_color_texture_index         != -1);
        materials[i].flags |= MATERIAL_PBR_BIT       & max32_if_true(gltf_material->metallic_roughness_texture_index != -1);
        materials[i].flags |= MATERIAL_NORMAL_BIT    & max32_if_true(gltf_material->normal_texture_index             != -1);
        materials[i].flags |= MATERIAL_OCCLUSION_BIT & max32_if_true(gltf_material->occlusion_texture_index          != -1);
        materials[i].flags |= MATERIAL_EMISSIVE_BIT  & max32_if_true(gltf_material->emissive_texture_index           != -1);

        materials[i].flags |= MATERIAL_BASE_TEX_COORD_BIT      & max32_if_true(gltf_material->base_color_tex_coord         != -1);
        materials[i].flags |= MATERIAL_PBR_TEX_COORD_BIT       & max32_if_true(gltf_material->metallic_roughness_tex_coord != -1);
        materials[i].flags |= MATERIAL_NORMAL_TEX_COORD_BIT    & max32_if_true(gltf_material->normal_tex_coord             != -1);
        materials[i].flags |= MATERIAL_OCCLUSION_TEX_COORD_BIT & max32_if_true(gltf_material->occlusion_tex_coord          != -1);
        materials[i].flags |= MATERIAL_EMISSIVE_TEX_COORD_BIT  & max32_if_true(gltf_material->emissive_tex_coord           != -1);

        materials[i].flags |= MATERIAL_OPAQUE_BIT & max32_if_true(gltf_material->alpha_mode == GLTF_ALPHA_MODE_OPAQUE);
        materials[i].flags |= MATERIAL_MASK_BIT   & max32_if_true(gltf_material->alpha_mode == GLTF_ALPHA_MODE_MASK);
        materials[i].flags |= MATERIAL_BLEND_BIT  & max32_if_true(gltf_material->alpha_mode == GLTF_ALPHA_MODE_BLEND);

        // Misc
        materials[i].flags |= MATERIAL_DOUBLE_SIDED_BIT & max32_if_true(gltf_material->double_sided);
        materials[i].alpha_cutoff = gltf_material->alpha_cutoff;

        // Pbr
        materials[i].pbr = {};

        materials[i].pbr.base_color_factor[0] = gltf_material->base_color_factor[0];
        materials[i].pbr.base_color_factor[1] = gltf_material->base_color_factor[1];
        materials[i].pbr.base_color_factor[2] = gltf_material->base_color_factor[2];
        materials[i].pbr.base_color_factor[3] = gltf_material->base_color_factor[3];

        materials[i].pbr.metallic_factor  = gltf_material->metallic_factor;
        materials[i].pbr.roughness_factor = gltf_material->roughness_factor;

        materials[i].pbr.base_color_texture           = {.texture_key = (u32)gltf_material->base_color_texture_index};
        materials[i].pbr.base_color_tex_coord         = gltf_material->base_color_tex_coord;
        materials[i].pbr.metallic_roughness_texture   = {.texture_key = (u32)gltf_material->metallic_roughness_texture_index};
        materials[i].pbr.metallic_roughness_tex_coord = gltf_material->metallic_roughness_tex_coord;

        // Normal
        materials[i].normal.scale     = gltf_material->normal_scale;
        materials[i].normal.texture   = {.texture_key = (u32)gltf_material->normal_texture_index};
        materials[i].normal.tex_coord = gltf_material->normal_tex_coord;

        // Occlusion
        materials[i].occlusion.strength  = gltf_material->occlusion_strength;
        materials[i].occlusion.texture   = {.texture_key = (u32)gltf_material->occlusion_texture_index};
        materials[i].occlusion.tex_coord = gltf_material->occlusion_tex_coord;

        // Emissive
        materials[i].emissive.factor[0] = gltf_material->emissive_factor[0];
        materials[i].emissive.factor[1] = gltf_material->emissive_factor[1];
        materials[i].emissive.factor[2] = gltf_material->emissive_factor[2];
        materials[i].emissive.texture   = {.texture_key = (u32)gltf_material->emissive_texture_index};
        materials[i].emissive.tex_coord = gltf_material->emissive_tex_coord;

        gltf_material = (Gltf_Material*)((u8*)gltf_material + gltf_material->stride);
    }
}

struct Load_Mesh_Info {
    Accessor *accessors;
    Material *materials;
};

inline static void add_buffer_view_index(Accessor *accessor, u32 *count, u32 *indices, u32 mask_count, u64 *masks) {
    // allocation_key equals its buffer view index before allocation stage
    u32 mask_idx = accessor->allocation_key >> 6;

    bool seen       = masks[mask_idx] & (1 << (accessor->allocation_key & 63));
    masks[mask_idx] = masks[mask_idx] | (1 << (accessor->allocation_key & 63));

    indices[*count] = accessor->allocation_key;
    *count += !seen;
}
static void model_load_gltf_meshes(Load_Mesh_Info *info, u32 count, Gltf_Mesh *gltf_meshes, Mesh *meshes,
                                   u8 *primitives_buffer, u8 *weights_buffer)
{
    u32 tmp;
    u32 idx;

    Gltf_Mesh           *gltf_mesh = gltf_meshes;
    Gltf_Mesh_Primitive *gltf_primitive;
    Gltf_Morph_Target   *gltf_morph_target;

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

                gltf_morph_target = (Gltf_Morph_Target*)((u8*)gltf_morph_target + gltf_morph_target->stride);
            }

            gltf_primitive = (Gltf_Mesh_Primitive*)((u8*)gltf_primitive + gltf_primitive->stride);
        }

        meshes[i].weight_count = gltf_mesh->weight_count;
        meshes[i].weights      = (float*)(weights_buffer + size_used_weights);
        size_used_weights     += sizeof(float) * meshes[i].weight_count;

        memcpy(meshes[i].weights, gltf_mesh->weights, sizeof(float) * meshes[i].weight_count);

        gltf_mesh = (Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }
}

struct Buffer_View {
    u64 offset;
    u64 size;
};

//
// @Note This implementation looks a little weird, as lots of sections seem naively split apart (for
// instance, the gltf struct is looped a few different times) but this is intentional, as eventually
// I will split this thing into jobs for threading, like reading the gltf struct into the model struct,
// and dipatching work to the allocators.
//
// @Todo Skins, Animations, Cameras.
//
Model model_from_gltf(Model_Allocators *model_allocators, String *gltf_file_name, u64 size_available,
                      u8 *model_buffer, u64 *ret_req_size)
{
    Gltf gltf = parse_gltf(gltf_file_name->str);

    // Get required bytes
    Model_Req_Size_Info req_size = model_get_required_size_from_gltf(&gltf);

    println("Size required for model %s: %u, Bytes remaining in buffer: %u", gltf_file_name->str, req_size.total, size_available);

    *ret_req_size = req_size.total;
    if (req_size.total > size_available) {
        println("Insufficient size remaining in buffer!");
        return {};
    }

    Model ret = {};

    u64 size_used = 0;
    u32 accessor_count = gltf_accessor_get_count(&gltf);
    u32 material_count = gltf_material_get_count(&gltf);

    ret.mesh_count     = gltf_mesh_get_count(&gltf);

    u64 tmp_size;

    // model_buffer layout: (@Todo This will change when I add skins, animations, etc.)
    // | meshes | primitives | extra primitive data | extra accessor data | mesh weights |

    u32 buffer_offset_primitives    = sizeof(Mesh) * ret.mesh_count;
    u32 buffer_offset_accessor_data = buffer_offset_primitives    + req_size.primitives;
    u32 buffer_offset_weights       = buffer_offset_accessor_data + req_size.accessors;

    // @Multithreading each model_load_<> can be a job

    // Accessor
    Accessor *accessors = (Accessor*)malloc_t(sizeof(Accessor) * accessor_count, 8);
    model_load_gltf_accessors(accessor_count, gltf.accessors, accessors, model_buffer + buffer_offset_accessor_data);

    // Material - no extra data, so no buffer argument
    Material *materials = (Material*)malloc_t(sizeof(Material) * material_count, 8);
    model_load_gltf_materials(material_count, gltf.materials, materials);

    Load_Mesh_Info load_mesh_info = {
        .accessors = accessors,
        .materials = materials,
    };

    // Mesh
    ret.meshes = (Mesh*)(model_buffer);
    model_load_gltf_meshes(&load_mesh_info, ret.mesh_count, gltf.meshes, ret.meshes,
                           model_buffer + buffer_offset_primitives, model_buffer + buffer_offset_weights);

    // Each texture/buffer view becomes an allocation.
    // @Note One day I will join buffer views together, so that an allocation
    // is the data referenced by a primitive.


    #if !TEST
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
    assert(buffer_view_count < 512 && "Increase mask count");

    u32  mask_idx = 0;
    u32  tmp;
    bool seen;
    Accessor *accessor;

    // I think this is pretty clean, track seen buffer view indices branchless, lots of indexing
    // off the stack.

    // Separate buffer views into vertex and index data; track dupes
    Mesh_Primitive *primitive;
    Morph_Target   *target;
    for(u32 i = 0; i < ret.mesh_count; ++i) {
        for(u32 j = 0; j < ret.meshes[i].primitive_count; ++j) {
            primitive = &ret.meshes[i].primitives[j];

            add_buffer_view_index(&primitive->indices, &index_buffer_view_count,
                                  index_buffer_view_indices, mask_count, masks);

            for(u32 k = 0; k < primitive->attribute_count; ++k)
                add_buffer_view_index(&primitive->attributes[k].accessor, &vertex_buffer_view_count,
                                      vertex_buffer_view_indices, mask_count, masks);

            for(u32 k = 0; k < primitive->target_count; ++k) {
                target = &primitive->targets[k];
                for(u32 l = 0; l < target->attribute_count; ++l)
                    add_buffer_view_index(&target->attributes[l].accessor, &vertex_buffer_view_count,
                                          vertex_buffer_view_indices, mask_count, masks);
            }
        }
    }

    Gltf_Buffer *gltf_buffer = gltf.buffers;
    const u8 *buffer = file_read_bin_temp_large(gltf_buffer->uri, gltf_buffer->byte_length);

    u32 *buffer_view_allocation_keys = (u32*)malloc_t(sizeof(u32) * buffer_view_count, 8);

    Gltf_Buffer_View *gltf_buffer_view;
    Gpu_Allocator_Result result;
    for(u32 i = 0; i < index_buffer_view_count; ++i) {
        result = begin_allocation(&model_allocators->index);
        CHECK_GPU_ALLOCATOR_RESULT(result);

        tmp = index_buffer_view_indices[i];
        gltf_buffer_view = gltf_buffer_view_by_index(&gltf, tmp); // Lame

        result = continue_allocation(&model_allocators->index, gltf_buffer_view->byte_length,
                                     gltf_buffer + gltf_buffer_view->byte_offset);
        CHECK_GPU_ALLOCATOR_RESULT(result);

        result = submit_allocation(&model_allocators->index, &buffer_view_allocation_keys[tmp]);
        CHECK_GPU_ALLOCATOR_RESULT(result);
    }

    for(u32 i = 0; i < vertex_buffer_view_count; ++i) {
        result = begin_allocation(&model_allocators->vertex);
        CHECK_GPU_ALLOCATOR_RESULT(result);

        tmp = vertex_buffer_view_indices[i];
        gltf_buffer_view = gltf_buffer_view_by_index(&gltf, tmp); // Lame

        result = continue_allocation(&model_allocators->vertex, gltf_buffer_view->byte_length,
                                     gltf_buffer + gltf_buffer_view->byte_offset);
        CHECK_GPU_ALLOCATOR_RESULT(result);

        result = submit_allocation(&model_allocators->vertex, &buffer_view_allocation_keys[tmp]);
        CHECK_GPU_ALLOCATOR_RESULT(result);
    }

                                        /* Texture Allocations */

    //
    // @TODO CURRENT TASK!!
    // Setup texture allocations, add buffer view allocation keys to accessors.
    //

    #endif // if !TEST
    
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

#if TEST // 300 lines of tests
static void test_model_from_gltf();

void test_asset() {
    test_model_from_gltf();
}

static void accessor_tests(char *name, Accessor *accessor1, u32 idx, char *prim_buf) {    
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

    Accessor_Sparse sparse = {
        .indices_component_type = ACCESSOR_COMPONENT_TYPE_U16_BIT,
        .count = 10,
        .indices_allocation_key = 7,
        .values_allocation_key = 4,
        .indices_byte_offset = 8888,
        .values_byte_offset = 9999,
    };

    Accessor accessors[3] = {
        {
            .flags = ACCESSOR_TYPE_SCALAR_BIT | ACCESSOR_COMPONENT_TYPE_U16_BIT,
            .allocation_key = 1,
            .byte_offset = 100,
            .count = 12636,
            .max_min = &max_min[0]
        },
        {
            .flags = ACCESSOR_TYPE_MAT4_BIT | ACCESSOR_COMPONENT_TYPE_FLOAT_BIT,
            .allocation_key = 2,
            .byte_offset = 200,
            .count = 2399,
            .max_min = &max_min[1]
        },
        {
            .flags = ACCESSOR_TYPE_VEC3_BIT | ACCESSOR_COMPONENT_TYPE_U32_BIT,
            .allocation_key = 3,
            .byte_offset = 300,
            .count = 12001,
            .sparse = &sparse,
        },
    };

    BEGIN_TEST_MODULE(name, false, false);

    Accessor *accessor2 = &accessors[idx];

    TEST_EQ(prim_buf, accessor1->flags         , accessor2->flags         , false);
    TEST_EQ(prim_buf, accessor1->allocation_key, accessor2->allocation_key, false);
    TEST_EQ(prim_buf, accessor1->byte_offset   , accessor2->byte_offset   , false);
    TEST_EQ(prim_buf, accessor1->count         , accessor2->count         , false);

    switch(accessor2->allocation_key) {
    case 1: 
        TEST_FEQ(prim_buf, accessor1->max_min->max[0], accessor2->max_min->max[0], false);
        TEST_FEQ(prim_buf, accessor1->max_min->min[0], accessor2->max_min->min[0], false);

        break;
    case 2:
        for(u32 k = 0; k < 16; ++k) {
            TEST_FEQ(prim_buf, accessor1->max_min->max[k], accessor2->max_min->max[k], false);
            TEST_FEQ(prim_buf, accessor1->max_min->min[k], accessor2->max_min->min[k], false);
        }

        break;
    case 3:
        TEST_EQ(prim_buf, accessor1->sparse->count, accessor2->sparse->count,  false);
        TEST_EQ(prim_buf, accessor1->sparse->indices_component_type, accessor2->sparse->indices_component_type, false);

        TEST_EQ(prim_buf, accessor1->sparse->values_allocation_key,  accessor2->sparse->values_allocation_key,  false);
        TEST_EQ(prim_buf, accessor1->sparse->indices_allocation_key, accessor2->sparse->indices_allocation_key, false);
        TEST_EQ(prim_buf, accessor1->sparse->values_byte_offset,     accessor2->sparse->values_byte_offset,     false);
        TEST_EQ(prim_buf, accessor1->sparse->indices_byte_offset,    accessor2->sparse->indices_byte_offset,    false);

        break;
    } // switch allocation key

    END_TEST_MODULE();
}

void test_model_from_gltf() {
    BEGIN_TEST_MODULE("Model_From_Gltf", false, false);
    u32 size = 1024 * 16;
    u8 *model_buffer = malloc_t(size, 16);

    u64 req_size;
    String model_name = cstr_to_string("test/test_gltf2.gltf");
    Model model = model_from_gltf(NULL, &model_name, size, model_buffer, &req_size);

    float weights[][2] = {
        {2, 1},
        {1, 2},
    };

    Material materials[2] = {
        {
            .pbr = {
                .base_color_factor = {0.5,0.5,0.5,1.0},
                .metallic_factor = 1,
                .roughness_factor = 1,
                .base_color_tex_coord = 1,
                .metallic_roughness_tex_coord = 0,
                .base_color_texture = {.texture_key = 1},
                .metallic_roughness_texture = {.texture_key = 2},
            },
            .normal = {
                .scale = 2,
                .texture = {.texture_key = 1},
                .tex_coord = 1,
            },
            .emissive = {.factor = {0.2, 0.1, 0.0}},
        },
        {
            .pbr = {
                .base_color_factor = {2.5,4.5,2.5,1.0},
                .metallic_factor = 5,
                .roughness_factor = 6,
                .base_color_tex_coord = 0,
                .metallic_roughness_tex_coord = 1,
                .base_color_texture = {.texture_key = 2},
                .metallic_roughness_texture = {.texture_key = 1},
            },
            .normal = {
                .scale = 1,
                .texture = {.texture_key = 0},
                .tex_coord = 1,
            },
            .occlusion = {
                .strength = 0.679,
                .texture = {.texture_key = 0},
                .tex_coord = 1,
            },
            .emissive = {
                .factor = {0.2, 0.1, 0.0},
                .texture = {.texture_key = 2},
                .tex_coord = 0,
            },
        },
    };

    Mesh_Primitive_Attribute_Type attrib_types[] = {
        MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL,
        MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION,
        MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT,
        MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS,
    };

    char weight_buf[128];
    char mesh_buf[128];
    char prim_buf[128];
    char attrib_buf[128];
    char accessor_buf[128];

    u32 mat_idx      = 0;
    u32 index_idx    = 0;
    u32 accessor_idx = 0;
    u32 weights_idx  = 0;
    u32 attribute_idx = 0;

    Accessor *accessor1;
    Accessor *accessor2;
    Material *material1;
    Material *material2;
    Mesh_Primitive *prim;
    for(u32 i = 0; i < model.mesh_count; ++i) {
        string_format(mesh_buf, "meshes[%u]", i);

        for(u32 j = 0; j < model.meshes[i].weight_count; ++j) {
            string_format(weight_buf, "%s.weights[%u]", mesh_buf, j);
            TEST_FEQ(weight_buf, model.meshes[i].weights[j], weights[weights_idx][j], false);
        }
        weights_idx++;
        weights_idx %= 2;

        for(u32 j = 0; j < model.meshes[i].primitive_count; ++j) {
            string_format(prim_buf, "%s.primitives[%u]", mesh_buf, j);

            prim = &model.meshes[i].primitives[j];
            switch(i) {
            case 0:
                accessor1 = &prim->indices;

                string_format(accessor_buf, "Accessor_Tests_Mesh_%u_Primitive_%u", i, j);
                accessor_tests(accessor_buf, accessor1, index_idx, prim_buf);

                material1 = &prim->material;
                material2 = &materials[mat_idx];

                TEST_EQ(prim_buf, material1->pbr.base_color_factor[0], material2->pbr.base_color_factor[0], false);
                TEST_EQ(prim_buf, material1->pbr.base_color_factor[1], material2->pbr.base_color_factor[1], false);
                TEST_EQ(prim_buf, material1->pbr.base_color_factor[2], material2->pbr.base_color_factor[2], false);
                TEST_EQ(prim_buf, material1->pbr.base_color_factor[3], material2->pbr.base_color_factor[3], false);

                TEST_EQ(prim_buf, material1->pbr.metallic_factor,  material2->pbr.metallic_factor, false);
                TEST_EQ(prim_buf, material1->pbr.roughness_factor, material2->pbr.roughness_factor, false);

                TEST_EQ(prim_buf, material1->pbr.base_color_tex_coord, material2->pbr.base_color_tex_coord, false);
                TEST_EQ(prim_buf, material1->pbr.metallic_roughness_tex_coord, material2->pbr.metallic_roughness_tex_coord, false);

                for(u32 k = 0; k < prim->attribute_count; ++k) {
                    string_format(attrib_buf, "%s.attributes[%u]", prim_buf, k);

                    TEST_EQ(attrib_buf, prim->attributes[k].n, 0, false);
                    TEST_EQ(attrib_buf, prim->attributes[k].type, attrib_types[k], false);

                    accessor_tests(accessor_buf, &prim->attributes[k].accessor, attribute_idx % 3, attrib_buf);
                    attribute_idx++;
                }

                if (j == 1) {
                    for(u32 k = 0; k < prim->targets[0].attribute_count; ++k) {
                        string_format(attrib_buf, "%s.targets[0].attributes[%u]", prim_buf, k);

                        TEST_EQ(attrib_buf, prim->targets[0].attributes[k].n, 0, false);
                        TEST_EQ(attrib_buf, prim->targets[0].attributes[k].type, attrib_types[k], false);

                        accessor_tests(accessor_buf, &prim->targets[0].attributes[k].accessor, attribute_idx % 3, attrib_buf);
                        attribute_idx++;
                    }

                    for(u32 k = 0; k < prim->targets[1].attribute_count; ++k) {
                        string_format(attrib_buf, "%s.targets[1].attributes[%u]", prim_buf, k);

                        TEST_EQ(attrib_buf, prim->targets[0].attributes[k].n, 0, false);
                        TEST_EQ(attrib_buf, prim->targets[0].attributes[k].type, attrib_types[k], false);

                        accessor_tests(accessor_buf, &prim->targets[1].attributes[k].accessor, attribute_idx % 3, attrib_buf);
                        attribute_idx++;
                    }
                }

                break;
            case 1:
                accessor1 = &prim->indices;

                string_format(accessor_buf, "Accessor_Tests_Mesh_%u_Primitive_%u", i, j);
                accessor_tests(accessor_buf, accessor1, index_idx, prim_buf);

                material1 = &prim->material;
                material2 = &materials[mat_idx];

                TEST_EQ(prim_buf, material1->pbr.base_color_factor[0], material2->pbr.base_color_factor[0], false);
                TEST_EQ(prim_buf, material1->pbr.base_color_factor[1], material2->pbr.base_color_factor[1], false);
                TEST_EQ(prim_buf, material1->pbr.base_color_factor[2], material2->pbr.base_color_factor[2], false);
                TEST_EQ(prim_buf, material1->pbr.base_color_factor[3], material2->pbr.base_color_factor[3], false);

                TEST_EQ(prim_buf, material1->pbr.metallic_factor,  material2->pbr.metallic_factor, false);
                TEST_EQ(prim_buf, material1->pbr.roughness_factor, material2->pbr.roughness_factor, false);

                TEST_EQ(prim_buf, material1->pbr.base_color_tex_coord, material2->pbr.base_color_tex_coord, false);
                TEST_EQ(prim_buf, material1->pbr.metallic_roughness_tex_coord, material2->pbr.metallic_roughness_tex_coord, false);

                for(u32 k = 0; k < prim->attribute_count; ++k) {
                    string_format(attrib_buf, "%s.attributes[%u]", prim_buf, k);

                    if (attrib_types[k] == MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS && j == 1) {
                        TEST_EQ(attrib_buf, prim->attributes[k].n, 1, false);
                    } else {
                        TEST_EQ(attrib_buf, prim->attributes[k].n, 0, false);
                    }

                    TEST_EQ(attrib_buf, prim->attributes[k].type, attrib_types[k], false);

                    accessor_tests(accessor_buf, &prim->attributes[k].accessor, attribute_idx % 3, attrib_buf);
                    attribute_idx++;
                }

                if (j == 1) {
                    for(u32 k = 0; k < prim->targets[0].attribute_count; ++k) {
                        string_format(attrib_buf, "%s.targets[0].attributes[%u]", prim_buf, k);

                        TEST_EQ(attrib_buf, prim->targets[0].attributes[k].n, 0, false);
                        TEST_EQ(attrib_buf, prim->targets[0].attributes[k].type, attrib_types[k], false);

                        accessor_tests(accessor_buf, &prim->targets[0].attributes[k].accessor, attribute_idx % 3, attrib_buf);
                        attribute_idx++;
                    }

                    for(u32 k = 0; k < prim->targets[1].attribute_count; ++k) {
                        string_format(attrib_buf, "%s.targets[1].attributes[%u]", prim_buf, k);

                        TEST_EQ(attrib_buf, prim->targets[0].attributes[k].n, 0, false);
                        TEST_EQ(attrib_buf, prim->targets[0].attributes[k].type, attrib_types[k], false);

                        accessor_tests(accessor_buf, &prim->targets[1].attributes[k].accessor, attribute_idx % 3, attrib_buf);
                        attribute_idx++;
                    }
                }

                break;
            } // switch mesh index

            index_idx++;
            mat_idx++;
            index_idx %= 3;
            mat_idx %= 2;
        }
    }

    
    END_TEST_MODULE()
}

#endif // if TEST
