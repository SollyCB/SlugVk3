#ifndef SOL_FEATURE_EXTENSIONS_HPP_INCLUDE_GUARD_
#define SOL_FEATURE_EXTENSIONS_HPP_INCLUDE_GUARD_

#include <vulkan/vulkan_core.h>

const char** get_vk_instance_layer_names(uint32_t *count); 
const char** get_vk_instance_ext_names(uint32_t *count); 

const char ** get_vk_device_ext_names(uint32_t *count); 
VkPhysicalDeviceFeatures2 get_vk_device_features();

//VkBaseOutStructure *features get_vkdevice_features(uint32_t count);

#endif
