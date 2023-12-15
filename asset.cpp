#include "asset.hpp"
#include "gltf.hpp"
#include "file.hpp"
#include "array.hpp"
#include "gpu.hpp"
#include "model.hpp"
#include "vulkan/vulkan_core.h"
#include "vulkan_errors.hpp"
#include <cstring>

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
        g_assets->models[i] = model_from_gltf(allocs, &g_model_dir_names[i], &g_model_file_names[i], model_buffer_size_available,
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

static void model_load_gltf_textures(u32 count, Gltf_Texture *gltf_textures, Texture *textures) {
    // Bro wtf are the C compiler spec writers doing!! How is this not a compilation error!!
    // Gltf_Texture *gltf_texture = gltf_texture;

    Gltf_Texture *gltf_texture = gltf_textures;

    for(u32 i = 0; i < count; ++i) {
        // @Todo ktx2 textures for ready to go mipmaps
        textures[i] = {.texture_key = (u32)gltf_texture->source_image, .sampler_key = (u32)gltf_texture->sampler};
        
        gltf_texture = (Gltf_Texture*)((u8*)gltf_texture + gltf_texture->stride);
    }
}

static void model_load_gltf_materials(u32 count, Gltf_Material *gltf_materials, Texture *textures, Material *materials) {

    Gltf_Material *gltf_material = gltf_materials;

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
        materials[i].alpha_cutoff = gltf_material->alpha_cutoff;

        // Pbr
        materials[i].pbr = {};

        materials[i].pbr.base_color_factor[0] = gltf_material->base_color_factor[0];
        materials[i].pbr.base_color_factor[1] = gltf_material->base_color_factor[1];
        materials[i].pbr.base_color_factor[2] = gltf_material->base_color_factor[2];
        materials[i].pbr.base_color_factor[3] = gltf_material->base_color_factor[3];

        materials[i].pbr.metallic_factor  = gltf_material->metallic_factor;
        materials[i].pbr.roughness_factor = gltf_material->roughness_factor;

        materials[i].pbr.base_color_texture    = textures[gltf_material->base_color_texture_index];
        materials[i].pbr.base_color_tex_coord  = gltf_material->base_color_tex_coord;
        materials[i].pbr.metallic_roughness_texture   = textures[gltf_material->metallic_roughness_texture_index];
        materials[i].pbr.metallic_roughness_tex_coord = gltf_material->metallic_roughness_tex_coord;

        // Normal
        materials[i].normal.scale     = gltf_material->normal_scale;
        materials[i].normal.texture   = textures[gltf_material->normal_texture_index];
        materials[i].normal.tex_coord = gltf_material->normal_tex_coord;

        // Occlusion
        materials[i].occlusion.strength  = gltf_material->occlusion_strength;
        materials[i].occlusion.texture   = textures[gltf_material->occlusion_texture_index];
        materials[i].occlusion.tex_coord = gltf_material->occlusion_tex_coord;

        // Emissive
        materials[i].emissive.factor[0] = gltf_material->emissive_factor[0];
        materials[i].emissive.factor[1] = gltf_material->emissive_factor[1];
        materials[i].emissive.factor[2] = gltf_material->emissive_factor[2];
        materials[i].emissive.texture   = textures[gltf_material->emissive_texture_index];
        materials[i].emissive.tex_coord = gltf_material->emissive_tex_coord;

        gltf_material = (Gltf_Material*)((u8*)gltf_material + gltf_material->stride);
    }
}

struct Load_Mesh_Info {
    Accessor *accessors;
    Material *materials;
};

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

inline static void add_buffer_view_index(u32 idx, u32 *count, u32 *indices, u32 mask_count, u64 *masks) {
    u32 mask_idx = idx >> 6;
    u64 mask     = masks[mask_idx];

    bool seen = (mask & (1 << (idx & 63))) > 0;
    mask      =  mask | (1 << (idx & 63));

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
Model model_from_gltf(Model_Allocators *model_allocators, String *model_dir, String *gltf_file_name, u64 size_available,
                      u8 *model_buffer, u64 *ret_req_size)
{
    u64 temp_allocator_mark = get_mark_temp(); // Reset to mark at end of function

    char uri_buf[127];
    memcpy(uri_buf +              0, model_dir->str,      model_dir->len);
    memcpy(uri_buf + model_dir->len, gltf_file_name->str, gltf_file_name->len + 1);
    Gltf gltf = parse_gltf(uri_buf);

    // Get required bytes
    Model_Req_Size_Info req_size = model_get_required_size_from_gltf(&gltf);

    println("Size required for model %s: %u, Bytes remaining in buffer: %u", gltf_file_name->str, req_size.total, size_available);

    *ret_req_size = req_size.total;
    if (req_size.total > size_available) {
        println("Insufficient size remaining in buffer!");
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

    Gltf_Buffer *gltf_buffer = gltf.buffers;

    memcpy(uri_buf + model_dir->len, gltf_buffer->uri, strlen(gltf_buffer->uri) + 1);

    u8 *buffer = (u8*)file_read_bin_temp_large(uri_buf, gltf_buffer->byte_length);

    u32 *allocation_keys = (u32*)malloc_t(sizeof(u32) * buffer_view_count);

    u32 tmp;
    Gltf_Buffer_View     *gltf_buffer_view;
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
    Gltf_Image *gltf_image = gltf.images;
    for(u32 i = 0; i < image_count; ++i) {
        // Fixing the below assert is trivial, but I do not need to right now. It will be done
        // when it actually fires.
        assert(gltf_image->uri && "@Todo @Unimplemented Support reading textures from buffer views");

        image_file_name = cstr_to_string(gltf_image->uri);

        // replace indices into images array with texture allocation keys
        allocator_result = tex_add_texture(&model_allocators->tex, &image_file_name, &tex_allocation_keys[i]);
        CHECK_GPU_ALLOCATOR_RESULT(allocator_result);

        gltf_image = (Gltf_Image*)((u8*)gltf_image + gltf_image->stride);
    }

    u32  sampler_count = gltf_sampler_get_count(&gltf);
    u32 *sampler_keys  = (u32*)malloc_t(sizeof(u32) * sampler_count);

    Get_Sampler_Info         get_sampler_info;
    Sampler_Allocator_Result sampler_result;

    Gltf_Sampler *gltf_sampler = gltf.samplers;
    for(u32 i = 0; i < sampler_count; ++i) {
        get_sampler_info = {};
        get_sampler_info.wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
        get_sampler_info.wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
        get_sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
        get_sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;

        sampler_result = add_sampler(&model_allocators->sampler, &get_sampler_info, &sampler_keys[i]);
        assert(sampler_result == SAMPLER_ALLOCATOR_RESULT_SUCCESS);
        CHECK_SAMPLER_ALLOCATOR_RESULT(sampler_result);

        gltf_sampler = (Gltf_Sampler*)((u8*)gltf_sampler + gltf_sampler->stride);
    }

    // Point model back at the allocation keys
    Accessor       *accessor;
    for(u32 i = 0; i < ret.mesh_count; ++i) {
        for(u32 j = 0; j < ret.meshes[i].primitive_count; ++j) {
            primitive = &ret.meshes[i].primitives[j];

            // Indices
            primitive->indices.allocation_key = allocation_keys[primitive->indices.allocation_key];

            // Material
            if (primitive->material.flags & MATERIAL_BASE_BIT) {
                primitive->material.pbr.base_color_texture.texture_key         = tex_allocation_keys[primitive->material.pbr.base_color_texture.texture_key];
                primitive->material.pbr.base_color_texture.sampler_key         = sampler_keys       [primitive->material.pbr.base_color_texture.sampler_key];
            }
            if (primitive->material.flags & MATERIAL_PBR_BIT) {
                primitive->material.pbr.metallic_roughness_texture.texture_key = tex_allocation_keys[primitive->material.pbr.metallic_roughness_texture.texture_key];
                primitive->material.pbr.metallic_roughness_texture.sampler_key = sampler_keys       [primitive->material.pbr.metallic_roughness_texture.sampler_key];
            }

            if (primitive->material.flags & MATERIAL_NORMAL_BIT) {
                primitive->material.normal.texture.texture_key    = tex_allocation_keys[primitive->material.normal.texture.texture_key];
                primitive->material.normal.texture.sampler_key    = sampler_keys       [primitive->material.normal.texture.sampler_key];
            }

            if (primitive->material.flags & MATERIAL_OCCLUSION_BIT) {
                primitive->material.occlusion.texture.texture_key = tex_allocation_keys[primitive->material.occlusion.texture.texture_key];
                primitive->material.occlusion.texture.sampler_key = sampler_keys       [primitive->material.occlusion.texture.sampler_key];
            }

            if (primitive->material.flags & MATERIAL_EMISSIVE_BIT) {
                primitive->material.emissive.texture.texture_key  = tex_allocation_keys[primitive->material.emissive.texture.texture_key];
                primitive->material.emissive.texture.sampler_key  = sampler_keys       [primitive->material.emissive.texture.sampler_key];
            }

            // Attributes
            for(u32 k = 0; k < primitive->attribute_count; ++k) {
                accessor = &primitive->attributes[k].accessor;

                accessor->allocation_key = allocation_keys[accessor->allocation_key];

                if (accessor->sparse) {
                    accessor->sparse->indices_allocation_key = allocation_keys[accessor->sparse->indices_allocation_key];
                    accessor->sparse->values_allocation_key  = allocation_keys[accessor->sparse->values_allocation_key];
                }
            }

            // Targets
            for(u32 k = 0; k < primitive->target_count; ++k) {
                for(u32 l = 0; l < primitive->targets[k].attribute_count; ++l) {
                    accessor = &primitive->targets[k].attributes[l].accessor;

                    accessor->allocation_key = allocation_keys[accessor->allocation_key];

                    if (accessor->sparse) {
                        accessor->sparse->indices_allocation_key = allocation_keys[accessor->sparse->indices_allocation_key];
                        accessor->sparse->values_allocation_key  = allocation_keys[accessor->sparse->values_allocation_key];
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

#if TEST // 300 lines of tests
static void test_model_from_gltf();

void test_asset() {
    test_model_from_gltf();
}

static void test_accessor(Gpu_Allocator *allocator, u8 *buf, char *name, Accessor *accessor0, Accessor *accessor1, bool index) {
    BEGIN_TEST_MODULE(name, false, false);

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
    
    END_TEST_MODULE();
}

static void test_material(Gpu_Tex_Allocator *allocator, char *name, Material *material0, Material *material1) {
    BEGIN_TEST_MODULE(name, false, false);

    TEST_EQ(name, material0->flags, material1->flags, false);

    for(u32 i = 0; i < 4; ++i) {
        TEST_FEQ(name, material0->pbr.base_color_factor[i], material1->pbr.base_color_factor[i], false);
    }
    for(u32 i = 0; i < 3; ++i) {
        TEST_FEQ(name, material0->emissive.factor[i], material1->emissive.factor[i], false);
    }

    TEST_FEQ(name, material0->pbr.metallic_factor,  material1->pbr.metallic_factor,  false);
    TEST_FEQ(name, material0->pbr.roughness_factor, material1->pbr.roughness_factor, false);
    TEST_FEQ(name, material0->normal.scale,         material1->normal.scale,         false);
    TEST_FEQ(name, material0->occlusion.strength,   material1->occlusion.strength,   false);

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

    END_TEST_MODULE();
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

    const char *images[10] = {
        "test/images/base1",
        "test/images/pbr1",
        "test/images/normal1",
        "test/images/occlusion1",
        "test/images/emissive1",
        "test/images/base2",
        "test/images/pbr2",
        "test/images/normal2",
        "test/images/occlusion2",
        "test/images/emissive2",
    };

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
                .base_color_factor = {0.5,0.5,0.5,1.0},
                .metallic_factor = 1,
                .roughness_factor = 1,
                .base_color_tex_coord = 1,
                .metallic_roughness_tex_coord = 0,
                .base_color_texture = {.texture_key = 0},
                .metallic_roughness_texture = {.texture_key = 1, .sampler_key = 0},
            },
            .normal = {
                .scale = 2,
                .texture = {.texture_key = 2},
                .tex_coord = 1,
            },
            .emissive = {.factor = {0.2, 0.1, 0.0}},
        },
        {
            .flags = MATERIAL_BASE_BIT      | MATERIAL_PBR_BIT      | MATERIAL_NORMAL_BIT |
                     MATERIAL_OCCLUSION_BIT | MATERIAL_EMISSIVE_BIT | MATERIAL_OPAQUE_BIT,
            .pbr = {
                .base_color_factor = {2.5,4.5,2.5,1.0},
                .metallic_factor = 5,
                .roughness_factor = 6,
                .base_color_tex_coord = 0,
                .metallic_roughness_tex_coord = 1,
                .base_color_texture = {.texture_key = 5, .sampler_key = 0},
                .metallic_roughness_texture = {.texture_key = 6, .sampler_key = 0},
            },
            .normal = {
                .scale = 1,
                .texture = {.texture_key = 7, .sampler_key = 0},
                .tex_coord = 1,
            },
            .occlusion = {
                .strength = 0.679,
                .texture = {.texture_key = 8, .sampler_key = 0},
                .tex_coord = 1,
            },
            .emissive = {
                .factor = {0.2, 0.1, 0.0},
                .texture = {.texture_key = 9, .sampler_key = 0},
                .tex_coord = 0,
            },
        },
    };

    Model_Allocators_Config model_allocators_config = {};
    Model_Allocators model_allocators = init_model_allocators(&model_allocators_config);
    
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
    test_material(texture_allocator, name_buf, &meshes[0].primitives[0].material, &materials[0]);
    string_format(name_buf, "meshes[0].primitives[1].material");
    test_material(texture_allocator, name_buf, &meshes[0].primitives[1].material, &materials[1]);

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
    test_material(texture_allocator, name_buf, &meshes[1].primitives[0].material, &materials[0]);
    string_format(name_buf, "meshes[1].primitives[1].material");
    test_material(texture_allocator, name_buf, &meshes[1].primitives[1].material, &materials[1]);
    string_format(name_buf, "meshes[1].primitives[2].material");
    test_material(texture_allocator, name_buf, &meshes[1].primitives[2].material, &materials[0]);

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
        TEST_EQ(name_buf, meshes[1].primitives[0].attributes[i].n,    attributes0[i].n, false);
        TEST_EQ(name_buf, meshes[1].primitives[0].attributes[i].type, attributes0[i].type, false);

        test_accessor(vertex_allocator, buf, name_buf, &meshes[0].primitives[0].attributes[i].accessor, &attributes0[i].accessor, false);
    }

    END_TEST_MODULE();
}

#endif // if TEST
