// For shader resource interface, meaningful storage classes are:
//    Uniform, UniformConstant, StorageBuffer, or PushConstant.
//
// When considering only descriptor types, omit the PushConstant storage class.
//
// Sampler =              UniformConstant      , OpTypeSampler                               , No Decoration
// SampledImage =         UniformConstant      , OpTypeImage (sampled = 1)                   , No Decoration
// StorageImage =         UniformConstant      , OpTypeImage (sampled = 2)                   , No Decoration
// CombinedImageSampler = UniformConstant      , OpTypeSampledImage                          , No Decoration
// UniformTexelBuffer   = UniformConstant      , OpTypeImage (Dim = Buffer, Sampled = 1)     , No Decoration
// StorageTexelBuffer   = UniformConstant      , OpTypeImage (Dim = Buffer, Sampled = 2)     , No Decoration
// UniformBuffer        = Uniform              , OpTypeStruct                                , Block + Offset
// StorageBuffer        = Uniform/StorageBuffer, OpTypeStruct                                , Block/BufferBlock + Offset
// InputAttachment      = UniformConstant      , OpTypeImage (Dim = SubpassData, Sampled = 2), InputAttachmentIndex
// InlineUniformBlock   = UniformConstant      , OpTypeStruct <-- (confusing because it is identical to uniform buffer, I think the spirv spec it has decorations)

#include "spirv.hpp"
#include "gpu.hpp"
#include "builtin_wrappers.h"

#if TEST
#include "test.hpp"
#include "file.hpp"
#endif

// u32s to prevent compiler warnings when assigning values by derefing spirv
struct Spirv_Type {
    u32 id;
    u32 result_id;
    u32 binding;
    u32 descriptor_set;
    u32 descriptor_count;
    VkDescriptorType descriptor_type;

    //u32 input_attachment_index; <- I dont think that this is actually useful...?
};

inline static Spirv_Type* spirv_find_type(Spirv_Type *types, int *type_count, int id) {
    // If there is an element in the array with a matching id, return it. 
    // Else, return the next empty element in the array.
    for(int i = 0; i < *type_count; ++i) {
        if (types[i].id == id)
            return &types[i];
    }
    Spirv_Type *type = &types[*type_count];
    type->id = id;
    type->binding = Max_u32;
    type->descriptor_set = Max_u32; // to make finding types actually decorated with descriptor set easier
    type->descriptor_type = (VkDescriptorType)Max_u32;
    (*type_count)++;
    return type;
}

inline static Spirv_Type* spirv_find_descriptor(Spirv_Type *types, int type_count, int *last_descriptor_index) {
    for(int i = *last_descriptor_index; i < type_count; ++i) {
        if (types[i].descriptor_set != Max_u32) {
            *last_descriptor_index = i + 1;
            return &types[i];
        }
    }
    return NULL;
}

void spirv_sort_bindings(Spirv_Descriptor *array, int start, int end) {
    if (start < end) {
        int lower = start - 1;
        Spirv_Descriptor tmp;
        for(int i = start; i < end; ++i) {
            if (array[i].set < array[end].set) {
                lower++;
                tmp = array[i];
                array[i] = array[lower];
                array[lower] = tmp;
            }
        }
        lower++;
        tmp = array[lower];
        array[lower] = array[end];
        array[end] = tmp;
        spirv_sort_bindings(array, start, lower - 1);
        spirv_sort_bindings(array, lower + 1, end);
    }
}

Parsed_Spirv parse_spirv(u64 byte_count, const u32 *spirv) {

    Parsed_Spirv ret = {};

    if (spirv[0] != 0x07230203) { // Magic number equivalency
        println("Cannot parse spirv! (Incorrect Magic Number)");
        return ret;
    }

    Spirv_Type types[64]; // Assume fewer that 64 type declarations
    int type_count = 0;
    memset(types, 0, 64 * sizeof(Spirv_Type));

    u64 descriptor_mask = 0x0; // Assume fewer than 64 descriptor sets
    int descriptor_count = 0;

    byte_count >>= 2; // convert to word count
    u64 offset = 5; // point to first word in instruction stream
    u16 *instruction_size;
    u16 *op_code;
    const u32 *instruction;
    Spirv_Type *type;
    while(offset < byte_count) {
        op_code = (u16*)(spirv + offset);
        instruction_size = op_code + 1;
        instruction = spirv + offset + 1;
        switch(*op_code) {
        case 15: // OpEntryPoint
        {
            switch(instruction[0]) {
            case 0:
                ret.stage = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            case 1:
                ret.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
                break;
            case 2:
                ret.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
                break;
            case 3:
                ret.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
                break;
            case 4:
                ret.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case 5: ret.stage = VK_SHADER_STAGE_COMPUTE_BIT;
                break;
            }
            break;
        }
        case 71: // OpDecorate
        {
            switch(instruction[1]) { // switch on Decoration
            case 2: // Block
                type = spirv_find_type(types, &type_count, instruction[0]);
                type->descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                break;
            case 3: // BufferBlock
                type = spirv_find_type(types, &type_count, instruction[0]);
                type->descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                break;
            case 33: // Binding
                type = spirv_find_type(types, &type_count, instruction[0]);
                type->binding = instruction[2];
                break;
            case 34: // DescriptorSet
                type = spirv_find_type(types, &type_count, instruction[0]);
                type->descriptor_set = instruction[2];

                descriptor_count++;
                descriptor_mask |= 1 << instruction[2]; // register the potentially new descriptor set

                break;
            case 43: // InputAttachmentIndex
            {
                type = spirv_find_type(types, &type_count, instruction[0]);
                type->descriptor_type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                //type->input_attachment_index = instruction[2]; <-  I dont think that this is actually useful
                break;
            }
            default:
                break;
            }
            break;
        }
        case 59: // OpVariable
        {
            type = spirv_find_type(types, &type_count, instruction[1]);
            type->result_id = instruction[0];
            switch(instruction[2]) { // switch on StorageClass
            /*case 9: // PushConstant
            {
                type->descriptor_type = SPIRV_SHADER_INTERFACE_PUSH_CONSTANT;
                break;
            }*/
            case 12:
            {
                type->descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // There can be two ways to define a storage buffer depending on the spec version...
                break;
            }
            default:
                break;
            }
            break;
        }
        case 32: // OpTypePointer
        {
            type = spirv_find_type(types, &type_count, instruction[0]);
            type->result_id = instruction[2]; // just connect the id chain to what it points to
            break;
        }
        case 43: // OpConstant (for array len - why is the literal not just encoded in the array instruction...)
        {
            type = spirv_find_type(types, &type_count, instruction[1]);
            type->descriptor_count = instruction[2];
            break;
        }
        case 28: // OpTypeArray
        {
            type = spirv_find_type(types, &type_count, instruction[0]);
            type->descriptor_count = spirv_find_type(types, &type_count, instruction[2])->descriptor_count; // get array len
            type->result_id = instruction[1];
            break;
        }
        case 27: // OpTypeSampledImage
        {
            type = spirv_find_type(types, &type_count, instruction[0]);
            type->descriptor_type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            break;
        }
        case 26: // OpTypeSampler
        {
            type = spirv_find_type(types, &type_count, instruction[0]);
            type->descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLER;
            break;
        }
        case 25: // OpTypeImage
        {
            type = spirv_find_type(types, &type_count, instruction[0]);
            switch(instruction[6]) { // switch Sampled
            case 1: // read only
            {
                switch(instruction[2]) { // switch Dim
                case 5: // Buffer
                    type->descriptor_type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
                    break;
                case 1: // 2D
                    type->descriptor_type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                    break;
                }
                break;
            }
            case 2: // read + write
            {
                switch(instruction[2]) { // switch Dim
                case 5: // Buffer
                    type->descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
                    break;
                case 1: // 2D
                    type->descriptor_type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                    break;
                }
                break;
            }
            default:
                break;
            } // switch Sampled
            break;
        }
        default:
            break;
        }
        offset += *instruction_size;
    }

    ret.binding_count = descriptor_count;
    ret.bindings = (Spirv_Descriptor*)malloc_t(sizeof(Spirv_Descriptor) * descriptor_count, 4);

    Spirv_Type *result_type;
    int last_descriptor_index = 0;
    for(int i = 0; i < descriptor_count; ++i) {
        type = spirv_find_descriptor(types, type_count, &last_descriptor_index);

        if (!type)
            continue;

        type->result_id = spirv_find_type(types, &type_count, type->result_id)->result_id; // type has to be OpVariable, so deref its pointer to get the real type
        result_type = spirv_find_type(types, &type_count, type->result_id);

        ret.bindings[i].binding = (int)type->binding;
        ret.bindings[i].set     = (int)type->descriptor_set;

        if (result_type->descriptor_count != 0) {
            ret.bindings[i].count = (int)result_type->descriptor_count;
            result_type = spirv_find_type(types, &type_count, result_type->result_id); // deref array to its element type
        } else {
            ret.bindings[i].count = 1;
        }

        if ((u32)type->descriptor_type != Max_u32)
            ret.bindings[i].type = type->descriptor_type;
        else
            ret.bindings[i].type = result_type->descriptor_type;
    }

    // sort bindings array to make linking bindings to their respective sets easier.
    spirv_sort_bindings(ret.bindings, 0, descriptor_count - 1);

    return ret;
}

#if TEST
static void test_parser();
static void test_grouper(); 

void test_spirv() {
    test_parser();
    test_grouper();
}

void test_parser() {
    BEGIN_TEST_MODULE("Parsed_Spirv", true, false);

    u64 byte_count;
    const u32 *spirv_blob = (const u32*)file_read_bin_temp("test/test.spv", &byte_count);

    Parsed_Spirv spirv = parse_spirv(byte_count, spirv_blob);

    TEST_EQ("binding_count", spirv.binding_count, 6, false);
    TEST_EQ("bindings[0].set"    , spirv.bindings[0].set    , 0, false);
    TEST_EQ("bindings[0].count"  , spirv.bindings[0].count  , 2, false);
    TEST_EQ("bindings[0].binding", spirv.bindings[0].binding, 1, false);
    TEST_EQ("bindings[0].type"   , spirv.bindings[0].type   , VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, false);

    TEST_EQ("bindings[1].set"    , spirv.bindings[1].set    , 1, false);
    TEST_EQ("bindings[1].count"  , spirv.bindings[1].count  , 1, false);
    TEST_EQ("bindings[1].binding", spirv.bindings[1].binding, 0, false);
    TEST_EQ("bindings[1].type"   , spirv.bindings[1].type   , VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, false);

    TEST_EQ("bindings[2].set"    , spirv.bindings[2].set    , 1, false);
    TEST_EQ("bindings[2].count"  , spirv.bindings[2].count  , 4, false);
    TEST_EQ("bindings[2].binding", spirv.bindings[2].binding, 1, false);
    TEST_EQ("bindings[2].type"   , spirv.bindings[2].type   , VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, false);

    TEST_EQ("bindings[3].set"    , spirv.bindings[3].set    , 2, false);
    TEST_EQ("bindings[3].count"  , spirv.bindings[3].count  , 4, false);
    TEST_EQ("bindings[3].binding", spirv.bindings[3].binding, 3, false);
    TEST_EQ("bindings[3].type"   , spirv.bindings[3].type   , VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, false);

    TEST_EQ("bindings[4].set"    , spirv.bindings[4].set    , 3, false);
    TEST_EQ("bindings[4].count"  , spirv.bindings[4].count  , 4, false);
    TEST_EQ("bindings[4].binding", spirv.bindings[4].binding, 1, false);
    TEST_EQ("bindings[4].type"   , spirv.bindings[4].type   , VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, false);

    TEST_EQ("bindings[5].set"    , spirv.bindings[5].set    , 4, false);
    TEST_EQ("bindings[5].count"  , spirv.bindings[5].count  , 1, false);
    TEST_EQ("bindings[5].binding", spirv.bindings[5].binding, 0, false);
    TEST_EQ("bindings[5].type"   , spirv.bindings[5].type   , VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, false);

    END_TEST_MODULE();
}

void test_grouper() {
    BEGIN_TEST_MODULE("Spirv_Set", true, false);

    u64 byte_counts[4];
    const u32 *spirv_blob_vert_1 = (const u32*)file_read_bin_temp("test/test_grouper_1.vert.spv", &byte_counts[0]);
    const u32 *spirv_blob_frag_1 = (const u32*)file_read_bin_temp("test/test_grouper_1.frag.spv", &byte_counts[1]);
    //const u32 *spirv_blob = (const u32*)file_read_bin_temp("shaders/test_grouper_2.vert.spv", &byte_counts[2]);
    //const u32 *spirv_blob = (const u32*)file_read_bin_temp("shaders/test_grouper_2.frag.spv", &byte_counts[3]);

    Parsed_Spirv vert_spirv = parse_spirv(byte_counts[0], spirv_blob_vert_1);
    Parsed_Spirv frag_spirv = parse_spirv(byte_counts[1], spirv_blob_frag_1);

    Parsed_Spirv parsed[] = {vert_spirv, frag_spirv};
    u32 set_count;
    gpu::Set_Layout_Info *sets = gpu::group_spirv(2, parsed, &set_count);

    TEST_EQ("set_count", set_count, 4, false);
    TEST_EQ("sets[0].count", sets[0].count, 3, false);
    TEST_EQ("sets[1].count", sets[1].count, 2, false);
    TEST_EQ("sets[2].count", sets[2].count, 2, false);
    TEST_EQ("sets[3].count", sets[3].count, 1, false);

    TEST_EQ("sets[0].bindings[0].binding"        , sets[0].bindings[0].binding        , 0, false);
    TEST_EQ("sets[0].bindings[0].descriptorType" , sets[0].bindings[0].descriptorType , VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, false);
    TEST_EQ("sets[0].bindings[0].descriptorCount", sets[0].bindings[0].descriptorCount, 1, false);
    TEST_EQ("sets[0].bindings[0].stageFlags"     , sets[0].bindings[0].stageFlags     , VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, false);

    TEST_EQ("sets[0].bindings[1].binding"        , sets[0].bindings[1].binding        , 1, false);
    TEST_EQ("sets[0].bindings[1].descriptorType" , sets[0].bindings[1].descriptorType , VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, false);
    TEST_EQ("sets[0].bindings[1].descriptorCount", sets[0].bindings[1].descriptorCount, 2, false);
    TEST_EQ("sets[0].bindings[1].stageFlags"     , sets[0].bindings[1].stageFlags     , VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, false);

    TEST_EQ("sets[0].bindings[2].binding"        , sets[0].bindings[2].binding        , 2, false);
    TEST_EQ("sets[0].bindings[2].descriptorType" , sets[0].bindings[2].descriptorType , VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, false);
    TEST_EQ("sets[0].bindings[2].descriptorCount", sets[0].bindings[2].descriptorCount, 3, false);
    TEST_EQ("sets[0].bindings[2].stageFlags"     , sets[0].bindings[2].stageFlags     , VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, false);

    TEST_EQ("sets[1].bindings[0].binding"        , sets[1].bindings[0].binding        , 0, false);
    TEST_EQ("sets[1].bindings[0].descriptorType" , sets[1].bindings[0].descriptorType , VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, false);
    TEST_EQ("sets[1].bindings[0].descriptorCount", sets[1].bindings[0].descriptorCount, 1, false);
    TEST_EQ("sets[1].bindings[0].stageFlags"     , sets[1].bindings[0].stageFlags     , VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, false);

    TEST_EQ("sets[1].bindings[1].binding"        , sets[1].bindings[1].binding        , 1, false);
    TEST_EQ("sets[1].bindings[1].descriptorType" , sets[1].bindings[1].descriptorType , VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, false);
    TEST_EQ("sets[1].bindings[1].descriptorCount", sets[1].bindings[1].descriptorCount, 1, false);
    TEST_EQ("sets[1].bindings[1].stageFlags"     , sets[1].bindings[1].stageFlags     , VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, false);

    TEST_EQ("sets[2].bindings[0].binding"        , sets[2].bindings[0].binding        , 0, false);
    TEST_EQ("sets[2].bindings[0].descriptorType" , sets[2].bindings[0].descriptorType , VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, false);
    TEST_EQ("sets[2].bindings[0].descriptorCount", sets[2].bindings[0].descriptorCount, 1, false);
    TEST_EQ("sets[2].bindings[0].stageFlags"     , sets[2].bindings[0].stageFlags     , VK_SHADER_STAGE_VERTEX_BIT, false);

    TEST_EQ("sets[2].bindings[1].binding"        , sets[2].bindings[1].binding        , 1, false);
    TEST_EQ("sets[2].bindings[1].descriptorType" , sets[2].bindings[1].descriptorType , VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, false);
    TEST_EQ("sets[2].bindings[1].descriptorCount", sets[2].bindings[1].descriptorCount, 1, false);
    TEST_EQ("sets[2].bindings[1].stageFlags"     , sets[2].bindings[1].stageFlags     , VK_SHADER_STAGE_VERTEX_BIT, false);

    TEST_EQ("sets[3].bindings[2].binding"        , sets[3].bindings[0].binding        , 0, false);
    TEST_EQ("sets[3].bindings[2].descriptorType" , sets[3].bindings[0].descriptorType , VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, false);
    TEST_EQ("sets[3].bindings[2].descriptorCount", sets[3].bindings[0].descriptorCount, 4, false);
    TEST_EQ("sets[3].bindings[2].stageFlags"     , sets[3].bindings[0].stageFlags     , VK_SHADER_STAGE_FRAGMENT_BIT, false);

    END_TEST_MODULE();
}
#endif
