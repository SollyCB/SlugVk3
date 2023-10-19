#ifndef SOL_SPIRV_HPP_INCLUDE_GUARD_
#define SOL_SPIRV_HPP_INCLUDE_GUARD_

#include "basic.h"

#include <vulkan/vulkan_core.h>

struct Spirv_Descriptor {
    int set;
    int count;
    int binding;
    VkDescriptorType type;
};
struct Parsed_Spirv {
    VkShaderStageFlagBits stage;
    int binding_count;
    Spirv_Descriptor *bindings; // should be renamed descriptors, but they are synonymous pretty much so?
};
Parsed_Spirv parse_spirv(u64 byte_count, const u32 *spirv);

#if TEST
void test_spirv();
#endif

#endif
