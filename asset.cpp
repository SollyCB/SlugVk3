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
        g_assets->models[i] = model_from_gltf(&g_model_file_names[i], model_buffer_size_available,
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

    Sampler_Allocator sampler       = create_sampler_allocator(0);
    Image_View_Allocator image_view = create_image_view_allocator(256);

    Descriptor_Allocator descriptor_sampler = get_descriptor_allocator(DESCRIPTOR_BUFFER_SIZE, gpu->memory.sampler_descriptor_ptr, gpu->memory.sampler_descriptor_buffer);

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

static u64 model_get_required_size_from_gltf(Gltf *gltf) {
    assert(gltf_buffer_get_count(gltf) == 1 && "Ugh, need to make buffers an array");

    // @Todo Animations, Skins, Cameras

    u32 accessor_count    = gltf_accessor_get_count(gltf);
    u32 material_count    = gltf_material_get_count(gltf);
    u32 texture_count     = gltf_texture_get_count(gltf);
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

            target_count    += gltf_primitive->target_count;

            gltf_morph_target = gltf_primitive->targets;
            for(u32 k = 0; k < gltf_primitive->target_count; ++k) {
                target_attribute_count += gltf_morph_target->attribute_count;

                gltf_morph_target = (Gltf_Morph_Target*)((u8*)gltf_morph_target + gltf_morph_target->stride);
            }

            gltf_primitive = (Gltf_Mesh_Primitive*)((u8*)gltf_primitive + gltf_primitive->stride);
        }

        gltf_mesh = (Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }

    u64 req_size = 0;

    req_size += sparse_count  * sizeof(Accessor_Sparse);
    req_size += max_min_count * sizeof(Accessor_Max_Min);

    req_size +=  weight_count           * sizeof(float);
    req_size +=  primitive_count        * sizeof(Mesh_Primitive);
    req_size +=  attribute_count        * sizeof(Mesh_Primitive_Attribute);
    req_size +=  target_count           * sizeof(Morph_Target);
    req_size +=  target_attribute_count * sizeof(Mesh_Primitive_Attribute);

    req_size += sizeof(Accessor) * accessor_count;
    req_size += sizeof(Material) * material_count;
    req_size += sizeof(Texture)  * texture_count;
    req_size += sizeof(Mesh)     * mesh_count;

    return req_size;
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

static void model_load_gltf_accessors(u32 count, Gltf_Accessor *gltf_accessors, Accessor *accessors, u8 *model_buffer, u64 *size_used) {
    u32 tmp_component_count;
    u32 tmp_component_width;
    u32 tmp;

    *size_used += sizeof(Accessor) * count;

    Gltf_Accessor *gltf_accessor = gltf_accessors;
    for(u32 i = 0; i < count; ++i) {
        accessors[i] = {};

        accessors[i].flags |= translate_gltf_accessor_type_to_bits(gltf_accessor->type,           &tmp_component_count);
        accessors[i].flags |= translate_gltf_accessor_type_to_bits(gltf_accessor->component_type, &tmp_component_width);

        accessors[i].flags |= ACCESSOR_NORMALIZED_BIT  & max32_if_true(gltf_accessor->normalized);

        accessors[i].allocation_key = gltf_accessor->buffer_view;
        accessors[i].byte_stride    = gltf_accessor->byte_stride;
        accessors[i].count          = gltf_accessor->count;

        if (gltf_accessor->max) {
            accessors[i].max_min = (Accessor_Max_Min*)(model_buffer + *size_used);
            *size_used += sizeof(Accessor_Max_Min);

            tmp = sizeof(float) * tmp_component_count;

            memcpy(accessors[i].max_min->max, gltf_accessor->max, tmp);
            memcpy(accessors[i].max_min->min, gltf_accessor->min, tmp);
        }

        if (gltf_accessor->sparse_count) {
            accessors[i].sparse = (Accessor_Sparse*)(model_buffer + *size_used);
            *size_used += sizeof(Accessor_Sparse);

            accessors[i].sparse->indices_component_type = translate_gltf_accessor_type_to_bits(gltf_accessor->indices_component_type, &tmp);
            accessors[i].sparse->count                  = gltf_accessor->sparse_count;
            accessors[i].sparse->indices_buffer_view    = gltf_accessor->indices_buffer_view;
            accessors[i].sparse->values_buffer_view     = gltf_accessor->values_buffer_view;
            accessors[i].sparse->indices_byte_offset    = gltf_accessor->indices_byte_offset;
            accessors[i].sparse->values_byte_offset     = gltf_accessor->values_byte_offset;
        }

        gltf_accessor = (Gltf_Accessor*)((u8*)gltf_accessor + gltf_accessor->stride);
    }
}

static void model_load_gltf_materials(u32 count, Gltf_Material *gltf_materials, Material *materials, u8 *model_buffer, u64 *size_used) {

    *size_used += sizeof(Material) * count;

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

        materials[i].pbr.base_color_texture           = gltf_material->base_color_texture_index;
        materials[i].pbr.base_color_tex_coord         = gltf_material->base_color_tex_coord;
        materials[i].pbr.metallic_roughness_texture   = gltf_material->metallic_roughness_texture_index;
        materials[i].pbr.metallic_roughness_tex_coord = gltf_material->metallic_roughness_tex_coord;

        // Normal
        materials[i].normal.scale     = gltf_material->normal_scale;
        materials[i].normal.texture   = gltf_material->normal_texture_index;
        materials[i].normal.tex_coord = gltf_material->normal_tex_coord;

        // Occlusion
        materials[i].occlusion.strength  = gltf_material->occlusion_strength;
        materials[i].occlusion.texture   = gltf_material->occlusion_texture_index;
        materials[i].occlusion.tex_coord = gltf_material->occlusion_tex_coord;

        // Emissive
        materials[i].emissive.factor[0] = gltf_material->emissive_factor[0];
        materials[i].emissive.factor[1] = gltf_material->emissive_factor[1];
        materials[i].emissive.factor[2] = gltf_material->emissive_factor[2];
        materials[i].emissive.texture   = gltf_material->emissive_texture_index;
        materials[i].emissive.tex_coord = gltf_material->emissive_tex_coord;

        gltf_material = (Gltf_Material*)((u8*)gltf_material + gltf_material->stride);
    }
}

inline static void model_load_gltf_textures(u32 count, Gltf_Texture *gltf_textures, Texture *textures, u8 *model_buffer, u64 *size_used) {
    // Just reserve space for now. Actual allocation work done later.
    *size_used += sizeof(Texture) * count;
}

static void model_load_gltf_meshes(u32 count, Gltf_Mesh *gltf_meshes, Mesh *meshes, u8 *model_buffer, u64 *size_used) {

    *size_used += sizeof(Mesh) * count;

    u32 tmp;

    Gltf_Mesh           *gltf_mesh = gltf_meshes;
    Gltf_Mesh_Primitive *gltf_primitive;
    Gltf_Morph_Target   *gltf_morph_target;

    u32 primitive_count;

    Mesh_Primitive           *primitive;
    Mesh_Primitive_Attribute *attribute;
    Morph_Target             *target;

    for(u32 i = 0; i < count; ++i) {
        primitive_count           = gltf_mesh->primitive_count;
        meshes[i].primitive_count = primitive_count;

        meshes[i].primitives = (Mesh_Primitive*)(model_buffer + *size_used);
        *size_used          += sizeof(Mesh_Primitive) * primitive_count;

        gltf_primitive = gltf_mesh->primitives;
        for(u32 j = 0; j < primitive_count; ++j) {
            primitive = &meshes[i].primitives[j];

            primitive->topology     = (VkPrimitiveTopology)gltf_primitive->topology;
            primitive->indices      = gltf_primitive->indices;
            primitive->material     = gltf_primitive->material;

            primitive->attribute_count  = gltf_primitive->extra_attribute_count;
            primitive->attribute_count += (u32)(gltf_primitive->position    != -1);
            primitive->attribute_count += (u32)(gltf_primitive->normal      != -1);
            primitive->attribute_count += (u32)(gltf_primitive->tangent     != -1);
            primitive->attribute_count += (u32)(gltf_primitive->tex_coord_0 != -1);

            primitive->attributes  = (Mesh_Primitive_Attribute*)(model_buffer + *size_used);

            *size_used += sizeof(Mesh_Primitive_Attribute) * primitive->attribute_count;

            // When I set up this api in the gltf file, I had no idea how annoying it would be later...
            // If an attribute is not set, rather than branch, we just overwrite it by not incrementing the index.
            tmp = 0;

            primitive->attributes[tmp] = {.accessor = (u32)gltf_primitive->normal,      .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL};
            tmp += (u32)(gltf_primitive->normal != -1);

            primitive->attributes[tmp] = {.accessor = (u32)gltf_primitive->position,    .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION};
            tmp += (u32)(gltf_primitive->position != -1);

            primitive->attributes[tmp] = {.accessor = (u32)gltf_primitive->tangent,     .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT};
            tmp += (u32)(gltf_primitive->tangent != -1);

            primitive->attributes[tmp] = {.accessor = (u32)gltf_primitive->tex_coord_0, .type = MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS};
            tmp += (u32)(gltf_primitive->tex_coord_0 != -1);

            for(u32 k = 0; k < gltf_primitive->extra_attribute_count; ++k) {
                attribute = &primitive->attributes[tmp + k];
                attribute->n        = gltf_primitive->extra_attributes[k].n;
                attribute->accessor = gltf_primitive->extra_attributes[k].accessor_index;
                attribute->type     = (Mesh_Primitive_Attribute_Type)gltf_primitive->extra_attributes[k].type;
            }

            primitive->target_count = gltf_primitive->target_count;
            primitive->targets      = (Morph_Target*)(model_buffer + *size_used);

            *size_used += sizeof(Morph_Target) * primitive->target_count;

            gltf_morph_target = gltf_primitive->targets;
            for(u32 k = 0; k < gltf_primitive->target_count; ++k) {
                target = &primitive->targets[k];

                target->attribute_count = gltf_morph_target->attribute_count;
                target->attributes      = (Mesh_Primitive_Attribute*)(model_buffer + *size_used);

                *size_used += sizeof(Mesh_Primitive_Attribute) * gltf_morph_target->attribute_count;

                for(u32 l = 0; l < gltf_morph_target->attribute_count; ++l) {
                    attribute = &target->attributes[l];
                    attribute->n        = gltf_morph_target->attributes[l].n;
                    attribute->accessor = gltf_morph_target->attributes[l].accessor_index;
                    attribute->type     = (Mesh_Primitive_Attribute_Type)gltf_morph_target->attributes[l].type;
                }

                gltf_morph_target = (Gltf_Morph_Target*)((u8*)gltf_morph_target + gltf_morph_target->stride);
            }

            gltf_primitive = (Gltf_Mesh_Primitive*)((u8*)gltf_primitive + gltf_primitive->stride);
        }

        meshes[i].weights      = (float*)(model_buffer + *size_used);
        meshes[i].weight_count = gltf_mesh->weight_count;
        *size_used += sizeof(float) * meshes[i].weight_count;

        memcpy(meshes[i].weights, gltf_mesh->weights, sizeof(float) * meshes[i].weight_count);

        gltf_mesh = (Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }
}

enum View_Type {
    VIEW_TYPE_INDEX,
    VIEW_TYPE_VERTEX,
};
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
                      u8 *model_buffer, u64 *ret_size_used)
{
    Gltf gltf = parse_gltf(gltf_file_name->str);

    // Get required bytes
    u64 req_size = model_get_required_size_from_gltf(&gltf);

    println("Size required for model %s: %u, Bytes remaining in buffer: %u", gltf_file_name->str, req_size, size_available);

    if (req_size > size_available) {
        *ret_size_used = req_size;
        println("Insufficient size remaining in buffer!");
        return {};
    } else {
        println("Loading Model...");
        println("Size remaining after load: %u", size_available - req_size);
    }

    Model ret = {};

    u64 size_used = 0;
    ret.accessor_count = gltf_accessor_get_count(&gltf);
    ret.material_count = gltf_material_get_count(&gltf);
    ret.texture_count  = gltf_texture_get_count(&gltf);
    ret.mesh_count     = gltf_mesh_get_count(&gltf);

    ret.textures       = (Texture*) (ret.materials + ret.material_count);
    ret.meshes         = (Mesh*)    (ret.textures  + ret.texture_count);

    u64 tmp_size;

    // model_buffer layout:
    // | Accessor * count | extra accessor data | Material * count | extra material data | ... | Mesh * count | Primitive * count | extra primitive data | weights | ...

    // Accessor
    ret.accessors = (Accessor*)(model_buffer + size_used);
    model_load_gltf_accessors(ret.accessor_count, gltf.accessors, ret.accessors, model_buffer, &size_used);

    // Material
    ret.materials = (Material*)(model_buffer + size_used);
    model_load_gltf_materials(ret.material_count, gltf.materials, ret.materials, model_buffer, &size_used);

    // Texture
    ret.textures = (Texture*)(model_buffer + size_used);
    model_load_gltf_textures(ret.texture_count, gltf.textures, ret.textures, model_buffer, &size_used);

    // Mesh
    ret.meshes = (Mesh*)(model_buffer + size_used);
    model_load_gltf_meshes(ret.mesh_count, gltf.meshes, ret.meshes, model_buffer, &size_used);

    assert(size_used == req_size);
    *ret_size_used = size_used;

    // @TODO CURRENT TASK!! Allocate the model resources.

    // Each texture/buffer view becomes an allocation.
    // @Note One day I will join buffer views together, so that an allocation
    // is the data referenced by a primitive.

    u32  index_accessor_count  = 0;
    u32  vertex_accessor_count = 0;
    u32 *index_accessor_indices  = (u32*)malloc_t(sizeof(u32) * ret.accessor_count, 8);
    u32 *vertex_accessor_indices = (u32*)malloc_t(sizeof(u32) * ret.accessor_count, 8);

    // The below method is inefficient, as it relies on the primitives to be done parsing, but this
    // not a performance critical area, or an interesting area, so I will not be threading it for a long
    // time if ever (plus it already has the dependency on the gltf struct so whatever, for now).
    // The reason I am doing this less efficient way is I cannot stand my gltf interface, and it is a
    // waste of time atm to update it.
    //
    // @Note I want duplicate accessor indices here, as then I can loop them and match them to their
    // respective buffer view allocation later.
    //

    Mesh_Primitive *primitive;
    Morph_Target   *target;
    for(u32 i = 0; i < ret.mesh_count; ++i) {
        for(u32 j = 0; j < ret.meshes[i].primitive_count; ++j) {
            primitive = &ret.meshes[i].primitives[j];

            index_accessor_indices[index_accessor_count] = primitive->indices;
            index_accessor_count++;

            for(u32 k = 0; k < primitive->attribute_count; ++k) {
                vertex_accessor_indices[vertex_accessor_count] = primitive->attributes[k].accessor;
                vertex_accessor_count++;
            }

            for(u32 k = 0; k < primitive->target_count; ++k) {
                target = &primitive->targets[k];
                for(u32 l = 0; l < target->attribute_count; ++l) {
                    vertex_accessor_indices[vertex_accessor_count] = target->attributes[l].accessor;
                    vertex_accessor_count++;
                }
            }
        }
    }


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

    // Find all buffer views containing index data
    for(u32 i = 0; i < index_accessor_count; ++i) {
        accessor = ret.accessors[index_accessor_indices[i]];

        // allocation_key equals its buffer view index before allocation stage
        mask_idx = accessor->allocation_key >> 6;

        seen           = mask[mask_idx] & (1 << (accessor->allocation_key & 63));
        mask[mask_idx] = mask[mask_idx] | (1 << (accessor->allocation_key & 63));

        index_buffer_view_indices[index_buffer_view_count] = accessor->allocation_key;
        index_buffer_view_count += !seen;
    }

    memset(masks, 0, sizeof(u64) * mask_count);

    // Find all buffer views containing vertex data
    for(u32 i = 0; i < vertex_accessor_count; ++i) {
        accessor = ret.accessors[vertex_accessor_indices[i]];

        // allocation_key equals its buffer view index before allocation stage
        mask_idx = accessor->allocation_key >> 6;

        seen           = mask[mask_idx] & (1 << (accessor->allocation_key & 63));
        mask[mask_idx] = mask[mask_idx] | (1 << (accessor->allocation_key & 63));

        vertex_buffer_view_indices[vertex_buffer_view_count] = accessor->allocation_key;
        vertex_buffer_view_count += !seen;
    }

    Gltf_Buffer *gltf_buffer = gltf.buffers;
    u8 *buffer = file_read_bin_temp_large(gltf_buffer->uri, gltf_buffer->byte_length);

    u32 *buffer_view_allocation_keys = (u32*)malloc_t(sizeof(u32) * buffer_view_count, 8);

    // The above code is so nice, now this... I am glad I have found a way to move
    // away from my gltf interface.
    Gltf_Buffer_view *gltf_buffer_view;
    Gpu_Allocator_Result result;
    for(u32 i = 0; i < index_buffer_view_count; ++i) {
        result = begin_allocation(&model_allocators->index);
        CHECK_GPU_ALLOCATOR_RESULT(result);

        tmp = index_buffer_view_indices[i];
        gltf_buffer_view = gltf_buffer_view_by_index(&gltf, tmp);

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
        gltf_buffer_view = gltf_buffer_view_by_index(&gltf, tmp);

        result = continue_allocation(&model_allocators->vertex, gltf_buffer_view->byte_length,
                                     gltf_buffer + gltf_buffer_view->byte_offset);
        CHECK_GPU_ALLOCATOR_RESULT(result);

        result = submit_allocation(&model_allocators->vertex, &buffer_view_allocation_keys[tmp]);
        CHECK_GPU_ALLOCATOR_RESULT(result);
    }
    
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

void test_model_from_gltf() {
    BEGIN_TEST_MODULE("Model_From_Gltf", true, false);

    u64 size = 1024 * 8;
    u8 *model_buffer = malloc_h(size, 16);

    u64 size_used = 0;
    String file_name = cstr_to_string("test/test_gltf.gltf");
    Model model = model_from_gltf(&file_name, size, model_buffer, &size_used);

    // Accessors
    Accessor *accessors = model.accessors;
    TEST_EQ("accessor_count", model.accessor_count, 3, false);

    TEST_EQ("accessors[0].type",      (accessors[0].flags & ACCESSOR_TYPE_BITS),           ACCESSOR_TYPE_SCALAR_BIT, false);
    TEST_EQ("accessors[0].comp_type", (accessors[0].flags & ACCESSOR_COMPONENT_TYPE_BITS), ACCESSOR_COMPONENT_TYPE_U16_BIT,    false);

    TEST_EQ("accessors[0].count",  accessors[0].count,             12636, false);
    TEST_EQ("accessors[0].max[0]", accessors[0].max_min->max[0],    4212, false);
    TEST_EQ("accessors[0].min[0]", accessors[0].max_min->min[0],       0, false);

    TEST_EQ("accessors[1].type",      accessors[1].flags & ACCESSOR_TYPE_BITS,           ACCESSOR_TYPE_MAT4_BIT,  false);
    TEST_EQ("accessors[1].comp_type", accessors[1].flags & ACCESSOR_COMPONENT_TYPE_BITS, ACCESSOR_COMPONENT_TYPE_FLOAT_BIT, false);
    TEST_EQ("accessors[1].count",     accessors[1].count, 2399, false);

    TEST_FEQ("accessors[1].max[0]",  accessors[1].max_min->max[0],  0.9971418380737304    , false);
    TEST_FEQ("accessors[1].max[1]",  accessors[1].max_min->max[1],  -4.371139894487897e-8 , false);
    TEST_FEQ("accessors[1].max[2]",  accessors[1].max_min->max[2],  0.9996265172958374    , false);
    TEST_FEQ("accessors[1].max[3]",  accessors[1].max_min->max[3],  0                     , false);
    TEST_FEQ("accessors[1].max[4]",  accessors[1].max_min->max[4],  4.3586464215650273e-8 , false);
    TEST_FEQ("accessors[1].max[5]",  accessors[1].max_min->max[5],  1                     , false);
    TEST_FEQ("accessors[1].max[6]",  accessors[1].max_min->max[6],  4.3695074225524884e-8 , false);
    TEST_FEQ("accessors[1].max[7]",  accessors[1].max_min->max[7],  0                     , false);
    TEST_FEQ("accessors[1].max[8]",  accessors[1].max_min->max[8],  0.9999366402626038    , false);
    TEST_FEQ("accessors[1].max[9]",  accessors[1].max_min->max[9],  0                     , false);
    TEST_FEQ("accessors[1].max[10]", accessors[1].max_min->max[10], 0.9971418380737304    , false);
    TEST_FEQ("accessors[1].max[11]", accessors[1].max_min->max[11], 0                     , false);
    TEST_FEQ("accessors[1].max[12]", accessors[1].max_min->max[12], 1.1374080181121828    , false);
    TEST_FEQ("accessors[1].max[13]", accessors[1].max_min->max[13], 0.44450080394744873   , false);
    TEST_FEQ("accessors[1].max[14]", accessors[1].max_min->max[14], 1.0739599466323853    , false);
    TEST_FEQ("accessors[1].max[15]", accessors[1].max_min->max[15], 1                     , false);

    TEST_FEQ("accessors[1].min[0]",  accessors[1].max_min->min[0],  -0.9999089241027832    , false);
    TEST_FEQ("accessors[1].min[1]",  accessors[1].max_min->min[1],  -4.371139894487897e-8  , false);
    TEST_FEQ("accessors[1].min[2]",  accessors[1].max_min->min[2],  -0.9999366402626038    , false);
    TEST_FEQ("accessors[1].min[3]",  accessors[1].max_min->min[3],  0                      , false);
    TEST_FEQ("accessors[1].min[4]",  accessors[1].max_min->min[4],  -4.3707416352845037e-8 , false);
    TEST_FEQ("accessors[1].min[5]",  accessors[1].max_min->min[5],  1                      , false);
    TEST_FEQ("accessors[1].min[6]",  accessors[1].max_min->min[6],  -4.37086278282095e-8   , false);
    TEST_FEQ("accessors[1].min[7]",  accessors[1].max_min->min[7],  0                      , false);
    TEST_FEQ("accessors[1].min[8]",  accessors[1].max_min->min[8],  -0.9996265172958374    , false);
    TEST_FEQ("accessors[1].min[9]",  accessors[1].max_min->min[9],  0                      , false);
    TEST_FEQ("accessors[1].min[10]", accessors[1].max_min->min[10], -0.9999089241027832    , false);
    TEST_FEQ("accessors[1].min[11]", accessors[1].max_min->min[11], 0                      , false);
    TEST_FEQ("accessors[1].min[12]", accessors[1].max_min->min[12], -1.189831018447876     , false);
    TEST_FEQ("accessors[1].min[13]", accessors[1].max_min->min[13], -0.45450031757354736   , false);
    TEST_FEQ("accessors[1].min[14]", accessors[1].max_min->min[14], -1.058603048324585     , false);
    TEST_FEQ("accessors[1].min[15]", accessors[1].max_min->min[15], 1                      , false);

    TEST_EQ("accessors[2].type",      accessors[2].flags & ACCESSOR_TYPE_BITS,           ACCESSOR_TYPE_VEC3_BIT,  false);
    TEST_EQ("accessors[2].comp_type", accessors[2].flags & ACCESSOR_COMPONENT_TYPE_BITS, ACCESSOR_COMPONENT_TYPE_U32_BIT, false);
    TEST_EQ("accessors[2].count",     accessors[2].count, 12001, false);

    TEST_EQ("accessors[2].sparse.indices_comp_type", accessors[2].sparse->indices_component_type, ACCESSOR_COMPONENT_TYPE_U16_BIT, false);
    TEST_EQ("accessors[2].sparse.count",             accessors[2].sparse->count, 10, false);

    // Materials
    Material *materials = model.materials;
    TEST_EQ("model.material_count", model.material_count, 2, false);

    TEST_EQ("materials[0].base_bit",         materials[0].flags & MATERIAL_BASE_BIT,        MATERIAL_BASE_BIT,   false);
    TEST_EQ("materials[0].pbr_bit",          materials[0].flags & MATERIAL_PBR_BIT,         MATERIAL_PBR_BIT,    false);
    TEST_EQ("materials[0].normal_bit",       materials[0].flags & MATERIAL_NORMAL_BIT,      MATERIAL_NORMAL_BIT, false);
    TEST_EQ("materials[0].occlusion_bit",    materials[0].flags & MATERIAL_OCCLUSION_BIT,              0x0, false);
    TEST_EQ("materials[0].emissive_bit",     materials[0].flags & MATERIAL_EMISSIVE_BIT,               0x0, false);
    TEST_EQ("materials[0].double_sided_bit", materials[0].flags & MATERIAL_DOUBLE_SIDED_BIT,           0x0, false);
    TEST_EQ("materials[0].opaque_bit",       materials[0].flags & MATERIAL_OPAQUE_BIT, MATERIAL_OPAQUE_BIT, false);
    TEST_EQ("materials[0].opaque_bit",       materials[0].flags & MATERIAL_MASK_BIT,                   0x0, false);
    TEST_EQ("materials[0].opaque_bit",       materials[0].flags & MATERIAL_BLEND_BIT,                  0x0, false);

    TEST_EQ("materials[1].base_bit",         materials[1].flags & MATERIAL_BASE_BIT,         MATERIAL_BASE_BIT,   false);
    TEST_EQ("materials[1].pbr_bit",          materials[1].flags & MATERIAL_PBR_BIT,          MATERIAL_PBR_BIT,    false);
    TEST_EQ("materials[1].normal_bit",       materials[1].flags & MATERIAL_NORMAL_BIT,       MATERIAL_NORMAL_BIT, false);
    TEST_EQ("materials[1].occlusion_bit",    materials[1].flags & MATERIAL_OCCLUSION_BIT,    MATERIAL_OCCLUSION_BIT, false);
    TEST_EQ("materials[1].emissive_bit",     materials[1].flags & MATERIAL_EMISSIVE_BIT,     MATERIAL_EMISSIVE_BIT, false);
    TEST_EQ("materials[1].double_sided_bit", materials[1].flags & MATERIAL_DOUBLE_SIDED_BIT,           0x0, false);
    TEST_EQ("materials[1].opaque_bit",       materials[1].flags & MATERIAL_OPAQUE_BIT,       MATERIAL_OPAQUE_BIT, false);
    TEST_EQ("materials[1].opaque_bit",       materials[1].flags & MATERIAL_MASK_BIT,                   0x0, false);
    TEST_EQ("materials[1].opaque_bit",       materials[1].flags & MATERIAL_BLEND_BIT,                  0x0, false);


    TEST_EQ("materials[0].base_color_texture_index",         materials[0].pbr.base_color_texture,          1, false);
    TEST_EQ("materials[0].base_color_tex_coord",             materials[0].pbr.base_color_tex_coord,        1, false);
    TEST_EQ("materials[0].metallic_roughness_texture_index", materials[0].pbr.metallic_roughness_texture,  2, false);
    TEST_EQ("materials[0].metallic_roughness_tex_coord",     materials[0].pbr.metallic_roughness_tex_coord,1, false);

    TEST_EQ("materials[0].normal_texture_index",             materials[0].normal.texture,                   3, false);
    TEST_EQ("materials[0].normal_tex_coord",                 materials[0].normal.tex_coord,                 1, false);

    TEST_EQ("materials[1].base_color_texture_index",         materials[1].pbr.base_color_texture,           3, false);
    TEST_EQ("materials[1].base_color_tex_coord",             materials[1].pbr.base_color_tex_coord,         4, false);
    TEST_EQ("materials[1].metallic_roughness_texture_index", materials[1].pbr.metallic_roughness_texture,   8, false);
    TEST_EQ("materials[1].metallic_roughness_tex_coord",     materials[1].pbr.metallic_roughness_tex_coord, 8, false);

    TEST_EQ("materials[1].normal_texture_index",             materials[1].normal.texture,                   12, false);
    TEST_EQ("materials[1].normal_tex_coord",                 materials[1].normal.tex_coord,                 11, false);

    TEST_EQ("materials[1].emissive_texture_index",           materials[1].emissive.texture,                  3, false);
    TEST_EQ("materials[1].emissive_tex_coord",               materials[1].emissive.tex_coord,            56070, false);

    TEST_EQ("materials[1].occlusion_texture_index",          materials[1].occlusion.texture,                79, false);
    TEST_EQ("materials[1].occlusion_tex_coord",              materials[1].occlusion.tex_coord,            9906, false);

    TEST_FEQ("materials[0].metallic_factor",      materials[0].pbr.metallic_factor , 1   , false);
    TEST_FEQ("materials[0].roughness_factor",     materials[0].pbr.roughness_factor, 1   , false);
    TEST_FEQ("materials[0].normal_scale",         materials[0].normal.scale        , 2   , false);

    TEST_FEQ("materials[0].base_color_factor[0]", materials[0].pbr.base_color_factor[0],  0.5, false);
    TEST_FEQ("materials[0].base_color_factor[1]", materials[0].pbr.base_color_factor[1],  0.5, false);
    TEST_FEQ("materials[0].base_color_factor[2]", materials[0].pbr.base_color_factor[2],  0.5, false);
    TEST_FEQ("materials[0].base_color_factor[3]", materials[0].pbr.base_color_factor[3],  1.0, false);

    TEST_FEQ("materials[0].emissive_factor[0]",   materials[0].emissive.factor[0],  0.2, false);
    TEST_FEQ("materials[0].emissive_factor[1]",   materials[0].emissive.factor[1],  0.1, false);
    TEST_FEQ("materials[0].emissive_factor[2]",   materials[0].emissive.factor[2],  0.0, false);

    TEST_FEQ("materials[1].metallic_factor",      materials[1].pbr.metallic_factor , 5.0  , false);
    TEST_FEQ("materials[1].roughness_factor",     materials[1].pbr.roughness_factor, 6.0  , false);
    TEST_FEQ("materials[1].normal_scale",         materials[1].normal.scale        , 1.0  , false);
    TEST_FEQ("materials[1].occlusion_strength",   materials[1].occlusion.strength  , 0.679, false);

    TEST_FEQ("materials[1].base_color_factor[0]", materials[1].pbr.base_color_factor[0] ,  2.5, false);
    TEST_FEQ("materials[1].base_color_factor[1]", materials[1].pbr.base_color_factor[1] ,  4.5, false);
    TEST_FEQ("materials[1].base_color_factor[2]", materials[1].pbr.base_color_factor[2] ,  2.5, false);
    TEST_FEQ("materials[1].base_color_factor[3]", materials[1].pbr.base_color_factor[3] ,  1.0, false);

    TEST_FEQ("materials[1].emissive_factor[0]", materials[1].emissive.factor[0] , 11.2, false);
    TEST_FEQ("materials[1].emissive_factor[1]", materials[1].emissive.factor[1] ,  0.1, false);
    TEST_FEQ("materials[1].emissive_factor[2]", materials[1].emissive.factor[2] ,  0.0, false);

    // Meshes
    TEST_EQ("model.mesh_count", model.mesh_count, 2, false);

    Mesh *meshes = &model.meshes[0];

    TEST_EQ("meshes[0].primitive_count", meshes[0].primitive_count, 2, false);
    TEST_EQ("meshes[0].weight_count"   , meshes[0].weight_count   , 2, false);
    TEST_EQ("meshes[1].primitive_count", meshes[1].primitive_count, 3, false);
    TEST_EQ("meshes[1].weight_count"   , meshes[1].weight_count   , 2, false);

    TEST_FEQ("meshes[0].weights[0]"   , meshes[0].weights[0], 0,   false);
    TEST_FEQ("meshes[0].weights[1]"   , meshes[0].weights[1], 0.5, false);

    TEST_FEQ("meshes[1].weights[0]"   , meshes[1].weights[0], 0,   false);
    TEST_FEQ("meshes[1].weights[1]"   , meshes[1].weights[1], 0.5, false);

    TEST_EQ("meshes[0].primitives[0].indices",  meshes[0].primitives[0].indices, 21, false);
    TEST_EQ("meshes[0].primitives[1].indices",  meshes[0].primitives[1].indices, 31, false);
    TEST_EQ("meshes[0].primitives[0].material", meshes[0].primitives[0].material, 3, false);

    TEST_EQ("meshes[0].primitives[1].material", meshes[0].primitives[1].material, 33, false);
    TEST_EQ("meshes[0].primitives[0].topology", meshes[0].primitives[0].topology, (VkPrimitiveTopology)1, false);
    TEST_EQ("meshes[0].primitives[1].topology", meshes[0].primitives[1].topology, (VkPrimitiveTopology)3, false);

    TEST_EQ("meshes[1].primitives[0].indices",  meshes[1].primitives[0].indices, 11, false);
    TEST_EQ("meshes[1].primitives[1].indices",  meshes[1].primitives[1].indices, 11, false);
    TEST_EQ("meshes[1].primitives[2].indices",  meshes[1].primitives[2].indices,  1, false);

    TEST_EQ("meshes[1].primitives[0].material", meshes[1].primitives[0].material, 13, false);
    TEST_EQ("meshes[1].primitives[1].material", meshes[1].primitives[1].material, 13, false);
    TEST_EQ("meshes[1].primitives[2].material", meshes[1].primitives[2].material,  3, false);

    TEST_EQ("meshes[1].primitives[0].topology", meshes[1].primitives[0].topology, (VkPrimitiveTopology)2, false);
    TEST_EQ("meshes[1].primitives[1].topology", meshes[1].primitives[1].topology, (VkPrimitiveTopology)3, false);
    TEST_EQ("meshes[1].primitives[2].topology", meshes[1].primitives[2].topology, (VkPrimitiveTopology)0, false);

    // Attributes, targets
    Mesh_Primitive *primitives = meshes[0].primitives;
    TEST_EQ("meshes[0].primitives[0].attribute_count", primitives[0].attribute_count, 4, false);

    TEST_EQ("meshes[0].primitives[0].attributes[0]", primitives[0].attributes[0].n,         0, false);
    TEST_EQ("meshes[0].primitives[0].attributes[0]", primitives[0].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[0].primitives[0].attributes[0]", primitives[0].attributes[0].accessor, 23, false);

    TEST_EQ("meshes[0].primitives[0].attributes[1]", primitives[0].attributes[1].n,         0, false);
    TEST_EQ("meshes[0].primitives[0].attributes[1]", primitives[0].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[0].primitives[0].attributes[1]", primitives[0].attributes[1].accessor, 22, false);

    TEST_EQ("meshes[0].primitives[0].attributes[2]", primitives[0].attributes[2].n,         0, false);
    TEST_EQ("meshes[0].primitives[0].attributes[2]", primitives[0].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("meshes[0].primitives[0].attributes[2]", primitives[0].attributes[2].accessor, 24, false);

    TEST_EQ("meshes[0].primitives[0].attributes[3]", primitives[0].attributes[3].n,         0, false);
    TEST_EQ("meshes[0].primitives[0].attributes[3]", primitives[0].attributes[3].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS, false);
    TEST_EQ("meshes[0].primitives[0].attributes[3]", primitives[0].attributes[3].accessor, 25, false);

    TEST_EQ("meshes[0].primitives[1].attribute_count", primitives[1].attribute_count, 4, false);

    TEST_EQ("meshes[0].primitives[1].attributes[0]", primitives[1].attributes[0].n,         0, false);
    TEST_EQ("meshes[0].primitives[1].attributes[0]", primitives[1].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[0].primitives[1].attributes[0]", primitives[1].attributes[0].accessor, 33, false);

    TEST_EQ("meshes[0].primitives[1].attributes[1]", primitives[1].attributes[1].n,         0, false);
    TEST_EQ("meshes[0].primitives[1].attributes[1]", primitives[1].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[0].primitives[1].attributes[1]", primitives[1].attributes[1].accessor, 32, false);

    TEST_EQ("meshes[0].primitives[1].attributes[2]", primitives[1].attributes[2].n,         0, false);
    TEST_EQ("meshes[0].primitives[1].attributes[2]", primitives[1].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("meshes[0].primitives[1].attributes[2]", primitives[1].attributes[2].accessor, 34, false);

    TEST_EQ("meshes[0].primitives[1].attributes[3]", primitives[1].attributes[3].n,         0, false);
    TEST_EQ("meshes[0].primitives[1].attributes[3]", primitives[1].attributes[3].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS, false);
    TEST_EQ("meshes[0].primitives[1].attributes[3]", primitives[1].attributes[3].accessor, 35, false);

    Morph_Target *targets = primitives[1].targets;
    TEST_EQ("meshes[0].primitives[0].target_count", primitives[0].target_count, 0, false);
    TEST_EQ("meshes[0].primitives[1].target_count", primitives[1].target_count, 2, false);

    TEST_EQ("meshes[0].targets[0].attributes[0]", targets[0].attributes[0].n,         0, false);
    TEST_EQ("meshes[0].targets[0].attributes[0]", targets[0].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[0].targets[0].attributes[0]", targets[0].attributes[0].accessor, 33, false);

    TEST_EQ("meshes[0].targets[0].attributes[1]", targets[0].attributes[1].n,         0, false);
    TEST_EQ("meshes[0].targets[0].attributes[1]", targets[0].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[0].targets[0].attributes[1]", targets[0].attributes[1].accessor, 32, false);

    TEST_EQ("meshes[0].targets[0].attributes[2]", targets[0].attributes[2].n,         0, false);
    TEST_EQ("meshes[0].targets[0].attributes[2]", targets[0].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("meshes[0].targets[0].attributes[2]", targets[0].attributes[2].accessor, 34, false);

    TEST_EQ("meshes[0].targets[1].attributes[0]", targets[1].attributes[0].n,         0, false);
    TEST_EQ("meshes[0].targets[1].attributes[0]", targets[1].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[0].targets[1].attributes[0]", targets[1].attributes[0].accessor, 43, false);

    TEST_EQ("meshes[0].targets[1].attributes[1]", targets[1].attributes[1].n,         0, false);
    TEST_EQ("meshes[0].targets[1].attributes[1]", targets[1].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[0].targets[1].attributes[1]", targets[1].attributes[1].accessor, 42, false);

    TEST_EQ("meshes[0].targets[1].attributes[2]", targets[1].attributes[2].n,         0, false);
    TEST_EQ("meshes[0].targets[1].attributes[2]", targets[1].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("meshes[0].targets[1].attributes[2]", targets[1].attributes[2].accessor, 44, false);

    primitives = meshes[1].primitives;
    TEST_EQ("meshes[1].primitives[0].attribute_count", primitives[0].attribute_count, 4, false);

    TEST_EQ("meshes[1].primitives[0].attributes[0]", primitives[0].attributes[0].n,         0, false);
    TEST_EQ("meshes[1].primitives[0].attributes[0]", primitives[0].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].primitives[0].attributes[0]", primitives[0].attributes[0].accessor, 13, false);

    TEST_EQ("meshes[1].primitives[0].attributes[1]", primitives[0].attributes[1].n,         0, false);
    TEST_EQ("meshes[1].primitives[0].attributes[1]", primitives[0].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].primitives[0].attributes[1]", primitives[0].attributes[1].accessor, 12, false);

    TEST_EQ("meshes[1].primitives[0].attributes[2]", primitives[0].attributes[2].n,         0, false);
    TEST_EQ("meshes[1].primitives[0].attributes[2]", primitives[0].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("meshes[1].primitives[0].attributes[2]", primitives[0].attributes[2].accessor, 14, false);

    TEST_EQ("meshes[1].primitives[0].attributes[3]", primitives[0].attributes[3].n,         0, false);
    TEST_EQ("meshes[1].primitives[0].attributes[3]", primitives[0].attributes[3].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS, false);
    TEST_EQ("meshes[1].primitives[0].attributes[3]", primitives[0].attributes[3].accessor, 15, false);

    TEST_EQ("meshes[1].primitives[1].attribute_count", primitives[1].attribute_count, 4, false);

    TEST_EQ("meshes[1].primitives[1].attributes[0]", primitives[1].attributes[0].n,         0, false);
    TEST_EQ("meshes[1].primitives[1].attributes[0]", primitives[1].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].primitives[1].attributes[0]", primitives[1].attributes[0].accessor, 13, false);

    TEST_EQ("meshes[1].primitives[1].attributes[1]", primitives[1].attributes[1].n,         0, false);
    TEST_EQ("meshes[1].primitives[1].attributes[1]", primitives[1].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].primitives[1].attributes[1]", primitives[1].attributes[1].accessor, 12, false);

    TEST_EQ("meshes[1].primitives[1].attributes[2]", primitives[1].attributes[2].n,         1, false);
    TEST_EQ("meshes[1].primitives[1].attributes[2]", primitives[1].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_JOINTS, false);
    TEST_EQ("meshes[1].primitives[1].attributes[2]", primitives[1].attributes[2].accessor, 14, false);

    TEST_EQ("meshes[1].primitives[1].attributes[3]", primitives[1].attributes[3].n,         1, false);
    TEST_EQ("meshes[1].primitives[1].attributes[3]", primitives[1].attributes[3].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_WEIGHTS, false);
    TEST_EQ("meshes[1].primitives[1].attributes[3]", primitives[1].attributes[3].accessor, 15, false);

    TEST_EQ("meshes[1].primitives[2].attributes[0]", primitives[2].attributes[0].n,         0, false);
    TEST_EQ("meshes[1].primitives[2].attributes[0]", primitives[2].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].primitives[2].attributes[0]", primitives[2].attributes[0].accessor,  3, false);

    TEST_EQ("meshes[1].primitives[2].attributes[1]", primitives[2].attributes[1].n,         0, false);
    TEST_EQ("meshes[1].primitives[2].attributes[1]", primitives[2].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].primitives[2].attributes[1]", primitives[2].attributes[1].accessor,  2, false);

    TEST_EQ("meshes[1].primitives[2].attributes[2]", primitives[2].attributes[2].n,         0, false);
    TEST_EQ("meshes[1].primitives[2].attributes[2]", primitives[2].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("meshes[1].primitives[2].attributes[2]", primitives[2].attributes[2].accessor,  4, false);

    TEST_EQ("meshes[1].primitives[2].attributes[3]", primitives[2].attributes[3].n,         1, false);
    TEST_EQ("meshes[1].primitives[2].attributes[3]", primitives[2].attributes[3].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TEX_COORDS, false);
    TEST_EQ("meshes[1].primitives[2].attributes[3]", primitives[2].attributes[3].accessor,  5, false);

    // Targets
    TEST_EQ("meshes[1].primitives[0].attributes[2]", primitives[1].target_count, 2, false);

    targets = primitives[1].targets;
    TEST_EQ("meshes[1].primitives[0].target_count", primitives[0].target_count, 0, false);
    TEST_EQ("meshes[1].primitives[1].target_count", primitives[1].target_count, 2, false);

    TEST_EQ("meshes[1].targets[0].attributes[0]", targets[0].attributes[0].n,         0, false);
    TEST_EQ("meshes[1].targets[0].attributes[0]", targets[0].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].targets[0].attributes[0]", targets[0].attributes[0].accessor, 13, false);

    TEST_EQ("meshes[1].targets[0].attributes[1]", targets[0].attributes[1].n,         0, false);
    TEST_EQ("meshes[1].targets[0].attributes[1]", targets[0].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].targets[0].attributes[1]", targets[0].attributes[1].accessor, 12, false);

    TEST_EQ("meshes[1].targets[0].attributes[2]", targets[0].attributes[2].n,         0, false);
    TEST_EQ("meshes[1].targets[0].attributes[2]", targets[0].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("meshes[1].targets[0].attributes[2]", targets[0].attributes[2].accessor, 14, false);

    TEST_EQ("meshes[1].targets[1].attributes[0]", targets[1].attributes[0].n,         0, false);
    TEST_EQ("meshes[1].targets[1].attributes[0]", targets[1].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].targets[1].attributes[0]", targets[1].attributes[0].accessor, 23, false);

    TEST_EQ("meshes[1].targets[1].attributes[1]", targets[1].attributes[1].n,         0, false);
    TEST_EQ("meshes[1].targets[1].attributes[1]", targets[1].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].targets[1].attributes[1]", targets[1].attributes[1].accessor, 22, false);

    TEST_EQ("meshes[1].targets[1].attributes[2]", targets[1].attributes[2].n,         0, false);
    TEST_EQ("meshes[1].targets[1].attributes[2]", targets[1].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("meshes[1].targets[1].attributes[2]", targets[1].attributes[2].accessor, 24, false);

    targets = primitives[2].targets;
    TEST_EQ("meshes[1].primitives[0].target_count", primitives[0].target_count, 0, false);
    TEST_EQ("meshes[1].primitives[1].target_count", primitives[1].target_count, 2, false);
    TEST_EQ("meshes[1].primitives[2].target_count", primitives[2].target_count, 2, false);

    TEST_EQ("meshes[1].targets[0].attributes[0]", targets[0].attributes[0].n,         0, false);
    TEST_EQ("meshes[1].targets[0].attributes[0]", targets[0].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].targets[0].attributes[0]", targets[0].attributes[0].accessor,  3, false);

    TEST_EQ("meshes[1].targets[0].attributes[1]", targets[0].attributes[1].n,         0, false);
    TEST_EQ("meshes[1].targets[0].attributes[1]", targets[0].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].targets[0].attributes[1]", targets[0].attributes[1].accessor,  2, false);

    TEST_EQ("meshes[1].targets[0].attributes[2]", targets[0].attributes[2].n,         0, false);
    TEST_EQ("meshes[1].targets[0].attributes[2]", targets[0].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("meshes[1].targets[0].attributes[2]", targets[0].attributes[2].accessor,  4, false);

    TEST_EQ("meshes[1].targets[1].attributes[0]", targets[1].attributes[0].n,         0, false);
    TEST_EQ("meshes[1].targets[1].attributes[0]", targets[1].attributes[0].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_NORMAL, false);
    TEST_EQ("meshes[1].targets[1].attributes[0]", targets[1].attributes[0].accessor, 9, false);

    TEST_EQ("meshes[1].targets[1].attributes[1]", targets[1].attributes[1].n,         0, false);
    TEST_EQ("meshes[1].targets[1].attributes[1]", targets[1].attributes[1].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_POSITION, false);
    TEST_EQ("meshes[1].targets[1].attributes[1]", targets[1].attributes[1].accessor,  7, false);

    TEST_EQ("meshes[1].targets[1].attributes[2]", targets[1].attributes[2].n,         0, false);
    TEST_EQ("meshes[1].targets[1].attributes[2]", targets[1].attributes[2].type,     MESH_PRIMITIVE_ATTRIBUTE_TYPE_TANGENT, false);
    TEST_EQ("meshes[1].targets[1].attributes[2]", targets[1].attributes[2].accessor,  6, false);


    END_TEST_MODULE();
}
#endif
