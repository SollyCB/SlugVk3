#include "features_extensions.hpp"
#include "glfw.hpp"

void get_vk_instance_layer_names(uint32_t *count, char **name_ptrs, char *name_buffer) {

    if (DEBUG)
        *count = layer_name_count;
    else
        *count = layer_name_count - 1;

    return layer_names;
}

void get_vk_instance_ext_names(uint32_t *count, char **ext_names, char *ext_name_buffer) {



    // instance exts define above
    u32 actual_count = glfw_ext_count + instance_ext_count;

    if (DEBUG)
        *count = actual_count;
    else {
        actual_count -= 2;
        *count = actual_count;
    }


    return (const char**)name_ptrs;
}
//void features(uint32_t count, VkBaseOutStructure *features); 

/*
   CHECK COUNTS!!!!!!
*/

VkPhysicalDeviceFeatures2 get_vk_device_features() {
    return features_full;
}
