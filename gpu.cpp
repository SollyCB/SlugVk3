#include "gpu.hpp"
#include "vulkan/vulkan_core.h"
#include "vulkan_errors.hpp"
#include "glfw.hpp"
#include "spirv.hpp"

#if DEBUG
static VkDebugUtilsMessengerEXT s_vk_debug_messenger;
VkDebugUtilsMessengerEXT* get_vk_debug_messenger_instance() { return &s_vk_debug_messenger; }
#endif

//
// @PipelineAllocation <- old note
// Best idea really is to calculate how big all the unchanging state settings are upfront, then make one
// allocation at load time large enough to hold everything, and just allocate everything into that.
// Just need to find a good way to count all this size...
//

static Gpu *s_Gpu;
Gpu* get_gpu_instance() { return s_Gpu; }

static VkFormat COLOR_ATTACHMENT_FORMAT;

void init_gpu()
{
    // keep struct data together (not pointing to random heap addresses)
    s_Gpu = (Gpu*)malloc_h(
            (sizeof(Gpu)                  * 1) +
            (sizeof(VkQueue)              * 3) +
            (sizeof(u32)                  * 3) +
            (sizeof(Gpu_Buf_Allocator) * 4), 8);

    Gpu *gpu = get_gpu_instance();

    gpu->vk_queues        = (VkQueue*)(gpu + 1);
    gpu->vk_queue_indices = (u32*)(gpu->vk_queues + 3);

    gpu->index_device_allocator   = (Gpu_Buf_Allocator*)(gpu->vk_queue_indices        + 3);
    gpu->vertex_device_allocator  = (Gpu_Buf_Allocator*)(gpu->index_device_allocator  + 1);
    gpu->index_host_allocator  = (Gpu_Buf_Allocator*)(gpu->vertex_device_allocator + 1);
    gpu->vertex_host_allocator = (Gpu_Buf_Allocator*)(gpu->index_host_allocator + 1);

    Create_Vk_Instance_Info create_instance_info = {};
    gpu->vk_instance = create_vk_instance(&create_instance_info);

#if DEBUG
    Create_Vk_Debug_Messenger_Info create_debug_messenger_info = {gpu->vk_instance};
    *get_vk_debug_messenger_instance() = create_debug_messenger(&create_debug_messenger_info);
#endif

    // creates queues and fills gpu struct with them
    // features and extensions lists defined in the function body
    gpu->vk_device = create_vk_device(gpu);
    //gpu->vma_allocator = create_vma_allocator(gpu);
}
void kill_gpu(Gpu *gpu) {
    // @Todo When I am creating more memory resources, clean properly.
    VkDevice device = gpu->vk_device;
    vkDestroyImageView(device, gpu->depth_views[0], ALLOCATION_CALLBACKS);
    vkDestroyImage(device, gpu->memory_resources.depth_attachments[0], ALLOCATION_CALLBACKS);
    vkFreeMemory(device, gpu->memory_resources.depth_mems[0], ALLOCATION_CALLBACKS);

    vkDestroyBuffer(device, gpu->memory_resources.index_bufs_device [0], ALLOCATION_CALLBACKS);
    vkDestroyBuffer(device, gpu->memory_resources.vertex_bufs_device[0], ALLOCATION_CALLBACKS);
    vkFreeMemory(device, gpu->memory_resources.index_vertex_mems_device[0], ALLOCATION_CALLBACKS);

    if ((gpu->memory_resources.flags & GPU_MEM_UMA_BIT) == 0) {
        vkDestroyBuffer(device, gpu->memory_resources.index_bufs_host [0], ALLOCATION_CALLBACKS);
        vkDestroyBuffer(device, gpu->memory_resources.vertex_bufs_host[0], ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory_resources.index_vertex_mems_host[0], ALLOCATION_CALLBACKS);
    }

    vkDestroyDevice(gpu->vk_device, ALLOCATION_CALLBACKS);
#if DEBUG
    vkDestroyDebugUtilsMessengerEXT(gpu->vk_instance, *get_vk_debug_messenger_instance(), ALLOCATION_CALLBACKS);
#endif
    vkDestroyInstance(gpu->vk_instance, ALLOCATION_CALLBACKS);
    free_h(gpu);
}

// `Instance
VkInstance create_vk_instance(Create_Vk_Instance_Info *info) {
    VkInstanceCreateInfo instance_create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
#if DEBUG
    Create_Vk_Debug_Messenger_Info debug_messenger_info = {};
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = fill_vk_debug_messenger_info(&debug_messenger_info);
    instance_create_info.pNext = &debug_messenger_create_info;
#endif

    // App Info
    VkApplicationInfo application_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    application_info.pApplicationName = info->application_name;
    application_info.applicationVersion = VK_MAKE_VERSION(info->application_version_major, info->application_version_middle, info->application_version_minor);
    application_info.pEngineName = info->application_name;
    application_info.engineVersion = VK_MAKE_VERSION(info->engine_version_major, info->engine_version_middle, info->engine_version_minor);
    application_info.apiVersion = info->vulkan_api_version;

    // assign app info to instance create info
    instance_create_info.pApplicationInfo = &application_info;

    // Preprocessor always ugly
#if DEBUG
    u32 layer_count = 1;
    const char *layer_names[] = {
        "VK_LAYER_KHRONOS_validation"
    };
    u32 ext_count = 2;
    const char *ext_names[] = {
        "VK_EXT_validation_features",
        "VK_EXT_debug_utils",
    };
#else
    u32 layer_count = 0;
    const char *layer_names[] = {};
    u32 ext_count = 0;
    const char *ext_names[] = {};
#endif

    u32 glfw_ext_count;
    const char **glfw_ext_names = glfwGetRequiredInstanceExtensions(&glfw_ext_count);

    u8 char_index = 0;
    u8 name_index = 0;
    char ext_name_buffer[250]; // Assume char total in ext names < 250
    char *ext_names_final[20]; // Assume fewer than 20 ext names

    char *name;
    for(int i = 0; i < glfw_ext_count; ++i) {
        name = strcpy(ext_name_buffer + char_index, glfw_ext_names[i]);
        ext_names_final[i] = name;
        char_index += strlen(name) + 1;
        name_index++;
    }
    for(int i = 0; i < ext_count; ++i) {
        name = strcpy(ext_name_buffer + char_index, ext_names[i]);
        ext_names_final[name_index] = name;
        char_index += strlen(name) + 1;
        name_index++;
    }

    instance_create_info.enabledExtensionCount = ext_count + glfw_ext_count;
    instance_create_info.ppEnabledExtensionNames = ext_names_final;

    instance_create_info.enabledLayerCount = layer_count;
    instance_create_info.ppEnabledLayerNames = layer_names;

    VkInstance vk_instance;
    auto check = vkCreateInstance(&instance_create_info, ALLOCATION_CALLBACKS, &vk_instance);
    DEBUG_OBJ_CREATION(vkCreateInstance, check);

    return vk_instance;
}

// `Device ///////////
VkDevice create_vk_device(Gpu *gpu) { // returns logical device, silently fills in gpu.physical_device

    uint32_t ext_count = 3;
    const char *ext_names[] = {
        "VK_KHR_swapchain",
        "VK_EXT_descriptor_buffer",
        "VK_EXT_memory_priority",
    };

    VkPhysicalDeviceFeatures vk1_features = {
        .samplerAnisotropy = VK_TRUE,
    };
    VkPhysicalDeviceVulkan12Features vk12_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .descriptorIndexing = VK_TRUE,
        .bufferDeviceAddress = VK_TRUE,
    };
    VkPhysicalDeviceVulkan13Features vk13_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .pNext = (void*)&vk12_features,
        .synchronization2 = VK_TRUE,
        //.dynamicRendering = VK_TRUE,
    };
    VkPhysicalDeviceMemoryPriorityFeaturesEXT mem_priority = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT,
        .pNext = &vk13_features,
        .memoryPriority = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features_full = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &mem_priority,
        .features = vk1_features,
    };

    VkPhysicalDeviceFeatures vk1_features_unfilled = vk1_features;
    VkPhysicalDeviceVulkan12Features vk12_features_unfilled = vk12_features;

    VkPhysicalDeviceVulkan13Features vk13_features_unfilled = vk13_features;
    vk13_features_unfilled.pNext = &vk12_features_unfilled;

    VkPhysicalDeviceMemoryPriorityFeaturesEXT mem_priority_empty =  mem_priority;
    mem_priority_empty.pNext = &vk13_features_unfilled;

    VkPhysicalDeviceFeatures2 features_full_unfilled = features_full;
    features_full_unfilled.pNext = &mem_priority_empty;

    features_full_unfilled.features = vk1_features_unfilled;

    // choose physical device
    u32 physical_device_count;
    vkEnumeratePhysicalDevices(gpu->vk_instance, &physical_device_count, NULL);
    VkPhysicalDevice *physical_devices =
        (VkPhysicalDevice*)malloc_t(sizeof(VkPhysicalDevice) * physical_device_count, 8);
    vkEnumeratePhysicalDevices(gpu->vk_instance, &physical_device_count, physical_devices);

    int graphics_queue_index;
    int transfer_queue_index;
    int presentation_queue_index;
    int physical_device_index = -1;

    int backup_graphics_queue_index = -1;
    int backup_presentation_queue_index = -1;
    int backup_physical_device_index = -1;

    // @Todo prefer certain gpus eg discrete
    for(int i = 0; i < physical_device_count; ++i) {

        vkGetPhysicalDeviceFeatures2(physical_devices[i], &features_full);

        bool incompatible = false;
        if (mem_priority.memoryPriority == VK_FALSE) {
            std::cerr << "Device Index " << i << " does not support Memory Priority\n";
            incompatible = true;
        }

        if (incompatible)
            continue;

        u32 queue_family_count;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, NULL);

        VkQueueFamilyProperties *queue_family_props =
            (VkQueueFamilyProperties*)malloc_t(sizeof(VkQueueFamilyProperties) * queue_family_count, 8);;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_devices[i], &queue_family_count, queue_family_props);

        graphics_queue_index = -1;
        transfer_queue_index = -1;
        presentation_queue_index = -1;

        bool break_outer = false;
        for(int j = 0; j < queue_family_count;++j) {
            if (glfwGetPhysicalDevicePresentationSupport(gpu->vk_instance, physical_devices[i], j) &&
                presentation_queue_index == -1)
            {
                presentation_queue_index = j;
            }
            if (queue_family_props[j].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
                queue_family_props[j].queueFlags & VK_QUEUE_TRANSFER_BIT &&
                graphics_queue_index == -1)
            {
                graphics_queue_index = j;
            }
            if (queue_family_props[j].queueFlags & VK_QUEUE_TRANSFER_BIT    &&
                !(queue_family_props[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                transfer_queue_index == -1)
            {
                transfer_queue_index = j;
            }
            if (transfer_queue_index != -1 && graphics_queue_index != -1 &&
                presentation_queue_index != -1)
            {
                physical_device_index = i;
                break_outer = true;
                break;
            }
        }

        if (break_outer)
            break;

        if (backup_physical_device_index == -1 && graphics_queue_index != -1 && presentation_queue_index != -1) {
            backup_presentation_queue_index = presentation_queue_index;
            backup_graphics_queue_index = graphics_queue_index;
            backup_physical_device_index = i;
        }

        continue;
    }

    if (physical_device_index == -1) {
        if (backup_physical_device_index == -1) {
            std::cerr << "Failed to choose suitable device, aborting...\n";
            HALT_EXECUTION();
        }
        physical_device_index = backup_physical_device_index;
        graphics_queue_index = backup_graphics_queue_index;
        transfer_queue_index = graphics_queue_index;
    }

    // @Todo query and store the important information for the device in a GpuInfo struct
    // Do this later when the info is actually required
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical_devices[physical_device_index], &props);

    VkDeviceQueueCreateInfo graphics_queue_create_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    graphics_queue_create_info.queueFamilyIndex = graphics_queue_index;

    // AMD article about multiple queues. Seems that one graphics queue and one async compute is a fine idea.
    // Using too many queues apparently sucks resources... This is smtg to come back to maybe. This article
    // is 2016...
    // https://gpuopen.com/learn/concurrent-execution-asynchronous-queues/
    graphics_queue_create_info.queueCount = 1;

    float queue_priorities = 1.0f;
    graphics_queue_create_info.pQueuePriorities = &queue_priorities;

    u32 queue_info_count = 1;
    VkDeviceQueueCreateInfo transfer_queue_create_info;

    if (transfer_queue_index != graphics_queue_index) {
        println("Selected Device (Primary Choice) %c", props.deviceName);

        queue_info_count++;
        transfer_queue_create_info = graphics_queue_create_info;
        transfer_queue_create_info.queueFamilyIndex = transfer_queue_index;
    } else {
        println("Selected Device (Backup) %c", props.deviceName);
    }

    VkDeviceQueueCreateInfo queue_infos[] = { graphics_queue_create_info, transfer_queue_create_info };

    VkDeviceCreateInfo device_create_info      = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_create_info.pNext                   = &features_full_unfilled;
    device_create_info.queueCreateInfoCount    = queue_info_count;
    device_create_info.pQueueCreateInfos       = queue_infos;
    device_create_info.enabledExtensionCount   = ext_count;
    device_create_info.ppEnabledExtensionNames = ext_names;
    device_create_info.pEnabledFeatures        = NULL;

    VkDevice vk_device;
    auto check = vkCreateDevice(physical_devices[physical_device_index], &device_create_info, ALLOCATION_CALLBACKS, &vk_device);
    DEBUG_OBJ_CREATION(vkCreateDevice, check);

    gpu->vk_physical_device = physical_devices[physical_device_index];
    gpu->vk_queue_indices[0] = graphics_queue_index;
    gpu->vk_queue_indices[1] = presentation_queue_index;
    gpu->vk_queue_indices[2] = transfer_queue_index;

    vkGetDeviceQueue(vk_device, graphics_queue_index, 0, &gpu->vk_queues[0]);

    // if queue indices are equivalent, dont get twice
    if (presentation_queue_index != graphics_queue_index) {
        vkGetDeviceQueue(vk_device, presentation_queue_index, 0, &gpu->vk_queues[1]);
    } else {
        gpu->vk_queues[1] = gpu->vk_queues[0];
    }

    // if queue indices are equivalent, dont get twice
    if (transfer_queue_index != graphics_queue_index) {
        vkGetDeviceQueue(vk_device, transfer_queue_index, 0, &gpu->vk_queues[2]);
    } else {
        gpu->vk_queues[2] = gpu->vk_queues[0];
    }

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(gpu->vk_physical_device, &physical_device_properties);
    gpu->info.properties = physical_device_properties;

    return vk_device;
} // func create_vk_device

// `Allocator


    /* Host -> Device buffer upload function defs (definition below memory_resources_init() func */
VkSubmitInfo2 gpu_device_buffer_upload_uma(Gpu_Buffer_Copy_Info *info);
VkSubmitInfo2 gpu_device_buffer_upload_non_uma_unified_transfer(Gpu_Buffer_Copy_Info *info);
VkSubmitInfo2 gpu_device_buffer_upload_non_uma_discrete_transfer(Gpu_Buffer_Copy_Info *info);

// @Todo Unimplemented allocator / memory resource setups:
//     - Color Attachment
//     - Uniform
//     - Texture
//     - Storage Image
//     - Storage Buffer
//     - Recreate depth and color attachments on swapchain recreation (window resize)
// Complete Setups:
//     - Vertex / Index
//     - Depth Attachment
void gpu_init_memory_resources(Gpu *gpu)
{
    gpu->memory_resources.flags = 0x0;
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(gpu->vk_physical_device, &props);
    // This does not work. For instance, on my laptop there are two heaps, despite uma. One of them
    // is just listed as zero size.
    //bool uma = props.memoryHeapCount == 1;

    bool uma = gpu->info.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;

    int device_heap_index = -1;
    int host_heap_index   = -1;
    u64 host_heap_size    = 0;
    u64 device_heap_size  = 0;


    if (uma)
        goto memory_type_selection;

    for(uint i = 0; i < props.memoryHeapCount; ++i)
        if (props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            if (device_heap_size < props.memoryHeaps[i].size) {
                device_heap_size = props.memoryHeaps[i].size;
                device_heap_index = i;
            }
        } else {
            if (host_heap_size < props.memoryHeaps[i].size) {
                host_heap_size = props.memoryHeaps[i].size;
                host_heap_index = i;
            }
        }

    memory_type_selection: // goto label;

    int uniform_index    = -1;
    int attachment_index = -1;

    // @Note Assumes that any memory type which is host visible is also coherent.
    for(uint i = 0; i < props.memoryTypeCount; ++i)
        if (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT  &&
            props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)  {
            if (uma && props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
                uniform_index    = i;
                attachment_index = i;
                break;
            } else {
                if (props.memoryTypes[i].heapIndex == host_heap_index)
                    uniform_index = i;
                else if (uniform_index == -1)
                    uniform_index = i;
            }
        } else if (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            if (props.memoryTypes[i].heapIndex == device_heap_index)
                attachment_index = i;
            else if (attachment_index == -1)
                attachment_index = i;
        }

                                 /* Attachments Begin */

    // @Unused Color allocation currently unimplemented.
    // - To be added with threading and deferred rendering.
    float render_target_priority = 1.0;
    VkImageCreateInfo color_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};

    VkExtent2D screen = get_window_instance()->info.imageExtent;
    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType     = VK_IMAGE_TYPE_2D;
    image_info.format        = VK_FORMAT_D16_UNORM;
    // allocate depth attachment large enough for entire screen
    image_info.extent        = {.width = 1920, .height = 1080, .depth = 1};
    image_info.mipLevels     = 1;
    image_info.arrayLayers   = 1;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    image_info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkDevice device = gpu->vk_device;
    VkImage depth;
    auto check = vkCreateImage(device, &image_info, ALLOCATION_CALLBACKS, &depth);
    DEBUG_OBJ_CREATION(vkCreateImage, check);

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(device, depth, &mem_req);

    VkMemoryPriorityAllocateInfoEXT priority = {VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT};
    priority.priority = render_target_priority;

    VkMemoryDedicatedAllocateInfo dedicate_allocation = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicate_allocation.image = depth;
    dedicate_allocation.pNext = &priority;

    VkMemoryAllocateInfo allocation_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation_info.pNext = &dedicate_allocation;
    allocation_info.allocationSize = mem_req.size;
    allocation_info.memoryTypeIndex = attachment_index;

    VkDeviceMemory depth_mem;
    check = vkAllocateMemory(device, &allocation_info, ALLOCATION_CALLBACKS, &depth_mem);
    DEBUG_OBJ_CREATION(vkAllocateMemory, check);

    VkBindImageMemoryInfo image_bind = {VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO};
    image_bind.image  = depth;
    image_bind.memory = depth_mem;
    image_bind.memoryOffset = 0;
    vkBindImageMemory2(device, 1, &image_bind);

    VkImageView depth_view = gpu_create_depth_attachment_view(device, depth);
    gpu->depth_views[0] = depth_view;

                            /* Attachments End */

                          /* Vertex Index Begin */

    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer index_device_buffer;
    VkBuffer vertex_device_buffer;
    VkBuffer index_host_buffer;
    VkBuffer vertex_host_buffer;
    VkDeviceMemory index_vertex_device_mem;
    VkDeviceMemory index_vertex_host_mem;

    VkMemoryRequirements index_vertex_mem_req[4];
    VkBindBufferMemoryInfo buffer_bind = {VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO};

    void *mapped_ptr;
    float index_vertex_priority_device = 0.5;
    float index_vertex_priority_host   = 0.0;
    if (uma) {
        gpu->device_buffer_upload_fn = &gpu_device_buffer_upload_uma;
        gpu->memory_resources.flags |= GPU_MEM_UMA_BIT;

        buffer_info.size = GPU_INDEX_ALLOCATOR_SIZE;
        buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkCreateBuffer(device, &buffer_info, ALLOCATION_CALLBACKS, &index_device_buffer);

        buffer_info.size = GPU_VERTEX_ALLOCATOR_SIZE;
        buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkCreateBuffer(device, &buffer_info, ALLOCATION_CALLBACKS, &vertex_device_buffer);

        vkGetBufferMemoryRequirements(device, index_device_buffer,  &index_vertex_mem_req[0]);
        vkGetBufferMemoryRequirements(device, vertex_device_buffer, &index_vertex_mem_req[1]);

        priority.priority = index_vertex_priority_device;

        allocation_info.pNext = &priority;
        allocation_info.allocationSize  = index_vertex_mem_req[0].size + index_vertex_mem_req[1].size;
        allocation_info.memoryTypeIndex = attachment_index;
        vkAllocateMemory(device, &allocation_info, ALLOCATION_CALLBACKS, &index_vertex_device_mem);

        buffer_bind.buffer = index_device_buffer;
        buffer_bind.memory = index_vertex_device_mem;
        buffer_bind.memoryOffset = 0;
        vkBindBufferMemory2(device, 1, &buffer_bind);

        buffer_bind.buffer = vertex_device_buffer;
        buffer_bind.memory = index_vertex_device_mem;
        buffer_bind.memoryOffset = index_vertex_mem_req[0].size;
        vkBindBufferMemory2(device, 1, &buffer_bind);

        vkMapMemory(device, index_vertex_device_mem, 0, VK_WHOLE_SIZE, 0x0, &mapped_ptr);

        *gpu->index_device_allocator =
            gpu_get_buf_allocator(
                index_device_buffer,
                mapped_ptr,
                GPU_INDEX_ALLOCATOR_SIZE,
                32);

        *gpu->vertex_device_allocator =
            gpu_get_buf_allocator(
                vertex_device_buffer,
                (u8*)mapped_ptr + index_vertex_mem_req[0].size,
                GPU_VERTEX_ALLOCATOR_SIZE,
                32);

        gpu->index_host_allocator  = gpu->index_device_allocator;
        gpu->vertex_host_allocator = gpu->vertex_device_allocator;
        index_vertex_host_mem = NULL;
        index_host_buffer = NULL;
        vertex_host_buffer = NULL;

    } else {

        if (gpu->vk_queue_indices[0] == gpu->vk_queue_indices[2])
            gpu->device_buffer_upload_fn = &gpu_device_buffer_upload_non_uma_unified_transfer;
        else
            gpu->device_buffer_upload_fn = &gpu_device_buffer_upload_non_uma_discrete_transfer;

        buffer_info.size = GPU_INDEX_ALLOCATOR_SIZE;
        buffer_info.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkCreateBuffer(device, &buffer_info, ALLOCATION_CALLBACKS, &index_device_buffer);

        buffer_info.size = GPU_VERTEX_ALLOCATOR_SIZE;
        buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        vkCreateBuffer(device, &buffer_info, ALLOCATION_CALLBACKS, &vertex_device_buffer);

        buffer_info.size  = GPU_INDEX_ALLOCATOR_SIZE;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        vkCreateBuffer(device, &buffer_info, ALLOCATION_CALLBACKS, &index_host_buffer);

        buffer_info.size  = GPU_VERTEX_ALLOCATOR_SIZE;
        buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        vkCreateBuffer(device, &buffer_info, ALLOCATION_CALLBACKS, &vertex_host_buffer);

        vkGetBufferMemoryRequirements(device, index_device_buffer,  &index_vertex_mem_req[0]);
        vkGetBufferMemoryRequirements(device, vertex_device_buffer, &index_vertex_mem_req[1]);
        vkGetBufferMemoryRequirements(device, index_host_buffer,    &index_vertex_mem_req[2]);
        vkGetBufferMemoryRequirements(device, vertex_host_buffer,   &index_vertex_mem_req[3]);

        priority.priority = index_vertex_priority_device;

        allocation_info.pNext = &priority;
        allocation_info.allocationSize  = index_vertex_mem_req[0].size + index_vertex_mem_req[1].size;
        allocation_info.memoryTypeIndex = attachment_index;
        vkAllocateMemory(device, &allocation_info, ALLOCATION_CALLBACKS, &index_vertex_device_mem);

        priority.priority = index_vertex_priority_host;

        allocation_info.allocationSize  = index_vertex_mem_req[2].size + index_vertex_mem_req[3].size;
        allocation_info.memoryTypeIndex = uniform_index;
        vkAllocateMemory(device, &allocation_info, ALLOCATION_CALLBACKS, &index_vertex_host_mem);

        buffer_bind.buffer = index_device_buffer;
        buffer_bind.memory = index_vertex_device_mem;
        buffer_bind.memoryOffset = 0;
        vkBindBufferMemory2(device, 1, &buffer_bind);

        buffer_bind.buffer = vertex_device_buffer;
        buffer_bind.memory = index_vertex_device_mem;
        buffer_bind.memoryOffset = index_vertex_mem_req[0].size;
        vkBindBufferMemory2(device, 1, &buffer_bind);

        buffer_bind.buffer = index_host_buffer;
        buffer_bind.memory = index_vertex_host_mem;
        buffer_bind.memoryOffset = 0;
        vkBindBufferMemory2(device, 1, &buffer_bind);

        buffer_bind.buffer = vertex_host_buffer;
        buffer_bind.memory = index_vertex_host_mem;
        buffer_bind.memoryOffset = index_vertex_mem_req[2].size;
        vkBindBufferMemory2(device, 1, &buffer_bind);

        vkMapMemory(device, index_vertex_host_mem, 0, VK_WHOLE_SIZE, 0x0, &mapped_ptr);

        *gpu->index_device_allocator =
            gpu_get_buf_allocator(
                index_device_buffer,
                NULL,
                GPU_INDEX_ALLOCATOR_SIZE,
                32);

        *gpu->vertex_device_allocator =
            gpu_get_buf_allocator(
                vertex_device_buffer,
                NULL,
                GPU_VERTEX_ALLOCATOR_SIZE,
                32);

        *gpu->index_host_allocator =
            gpu_get_buf_allocator(
                index_device_buffer,
                mapped_ptr,
                GPU_INDEX_ALLOCATOR_SIZE,
                32);

        *gpu->vertex_host_allocator =
            gpu_get_buf_allocator(
                vertex_device_buffer,
                (u8*)mapped_ptr + index_vertex_mem_req[2].size,
                GPU_VERTEX_ALLOCATOR_SIZE,
                32);
    }
                          /* Vertex Index End */

    gpu->memory_resources.index_vertex_mems_device[0] = index_vertex_device_mem;
    gpu->memory_resources.index_vertex_mems_host[0]   = index_vertex_host_mem;
    //gpu->memory_resources.uniform_mems             ;//= ;
    //gpu->memory_resources.color_mems               ;//= ;
    gpu->memory_resources.depth_mems[0]               = depth_mem;
    //gpu->memory_resources.texture_mems_stage       ;//= ;
    //gpu->memory_resources.texture_mems_device      ;//= ;

    gpu->memory_resources.index_bufs_device[0]        = index_device_buffer;
    gpu->memory_resources.vertex_bufs_device[0]       = vertex_device_buffer;
    gpu->memory_resources.index_bufs_host[0]          = index_host_buffer;
    gpu->memory_resources.vertex_bufs_host[0]         = vertex_host_buffer;
    //gpu->memory_resources.uniform_bufs             ;//= ;
    //gpu->memory_resources.color_attachments        ;//= ;
    gpu->memory_resources.depth_attachments[0]        = depth;
    //gpu->memory_resources.texture_stages           ;//= ;

    u64 image_alignment;
}

    /* Host -> Device buffer upload functions*/
VkSubmitInfo2 gpu_device_buffer_upload_uma(Gpu_Buffer_Copy_Info *info) {
    VkCommandBufferBeginInfo cmd_begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmd_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(info->graphics_cmd, &cmd_begin_info);

    VkCommandBufferSubmitInfo *cmd_info =
        (VkCommandBufferSubmitInfo*)malloc_t(
                sizeof(VkCommandBufferSubmitInfo), 8);
    *cmd_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info->commandBuffer = info->graphics_cmd;

    VkSubmitInfo2 submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit_info.waitSemaphoreInfoCount = 1;
    submit_info.pWaitSemaphoreInfos = info->wait_semaphore;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = cmd_info;
    return submit_info;
}
VkSubmitInfo2 gpu_device_buffer_upload_non_uma_unified_transfer(Gpu_Buffer_Copy_Info *info)
{
    VkCommandBufferBeginInfo cmd_begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmd_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkMemoryBarrier2 barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
    barrier.dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR;

    VkDependencyInfo dependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;

    vkBeginCommandBuffer(info->graphics_cmd, &cmd_begin_info);
        for(u32 i = 0; i < info->buffer_count; ++i)
            vkCmdCopyBuffer2(info->graphics_cmd, &info->copy_infos[i]);

    VkCommandBufferSubmitInfo *cmd_info =
        (VkCommandBufferSubmitInfo*)malloc_t(
                sizeof(VkCommandBufferSubmitInfo), 8);
    *cmd_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info->commandBuffer = info->graphics_cmd;

    VkSubmitInfo2 submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit_info.waitSemaphoreInfoCount = 1;
    submit_info.pWaitSemaphoreInfos = info->wait_semaphore;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = cmd_info;
    return submit_info;
}

VkSubmitInfo2 gpu_device_buffer_upload_non_uma_discrete_transfer(Gpu_Buffer_Copy_Info *info)
{
    VkCommandBufferBeginInfo cmd_begin_info {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmd_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    auto *barriers = (VkBufferMemoryBarrier2*)
            malloc_t(
                    sizeof(VkBufferMemoryBarrier2) * info->buffer_count * 2, 8);
    auto transfer_barriers = barriers;
    auto graphics_barriers = barriers + info->buffer_count;

    Gpu *gpu = get_gpu_instance();
    for(int i = 0; i < info->buffer_count; ++i) {
        transfer_barriers[i] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        transfer_barriers[i].srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        transfer_barriers[i].srcAccessMask       = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        transfer_barriers[i].srcQueueFamilyIndex = gpu->vk_queue_indices[2];
        transfer_barriers[i].dstQueueFamilyIndex = gpu->vk_queue_indices[0];
        transfer_barriers[i].buffer              = info->buffers[i];
        transfer_barriers[i].offset              = 0;
        transfer_barriers[i].size                = VK_WHOLE_SIZE;
    }

    for(int i = 0; i < info->buffer_count; ++i) {
        graphics_barriers[i] = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        graphics_barriers[i].dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        graphics_barriers[i].dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR;
        graphics_barriers[i].srcQueueFamilyIndex = gpu->vk_queue_indices[2];
        graphics_barriers[i].dstQueueFamilyIndex = gpu->vk_queue_indices[0];
        graphics_barriers[i].buffer              = info->buffers[i];
        graphics_barriers[i].offset              = 0;
        graphics_barriers[i].size                = VK_WHOLE_SIZE;
    }
        // @Note Unsure as whether to separate out vertex and index stages...

    VkDependencyInfo transfer_dependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    transfer_dependency.bufferMemoryBarrierCount = info->buffer_count;
    transfer_dependency.pBufferMemoryBarriers    = transfer_barriers;

    vkBeginCommandBuffer(info->transfer_cmd, &cmd_begin_info);

        for(int i = 0; i < info->buffer_count; ++i)
            vkCmdCopyBuffer2(info->transfer_cmd, &info->copy_infos[i]);

        vkCmdPipelineBarrier2(info->transfer_cmd, &transfer_dependency);

    vkEndCommandBuffer(info->transfer_cmd);

    VkSemaphoreSubmitInfo semaphore_submission = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    semaphore_submission.semaphore = info->transfer_signal;
    semaphore_submission.stageMask = VK_PIPELINE_STAGE_2_COPY_BIT;

    VkCommandBufferSubmitInfo cmd_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_info.commandBuffer = info->transfer_cmd;

    VkSubmitInfo2 submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_info;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &semaphore_submission;

    vkQueueSubmit2(gpu->vk_queues[2], 1, &submit_info, NULL);

    VkDependencyInfo graphics_dependency = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    graphics_dependency.bufferMemoryBarrierCount = info->buffer_count;
    graphics_dependency.pBufferMemoryBarriers    = graphics_barriers;

    vkBeginCommandBuffer(info->graphics_cmd, &cmd_begin_info);

        vkCmdPipelineBarrier2(info->graphics_cmd, &graphics_dependency);

    VkCommandBufferSubmitInfo *cmd_submit_info =
        (VkCommandBufferSubmitInfo*)malloc_t(
                sizeof(VkCommandBufferSubmitInfo), 8);

    VkSemaphoreSubmitInfo *semaphore_info =
        (VkSemaphoreSubmitInfo*)malloc_t(
                sizeof(VkSemaphoreSubmitInfo) * 2, 8);

    *cmd_submit_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmd_submit_info->commandBuffer = info->graphics_cmd;

    semaphore_info[0] = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    semaphore_info[0].semaphore = info->transfer_signal;
    semaphore_info[0].stageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;

    semaphore_info[1] = *info->wait_semaphore;

    VkSubmitInfo2 ret = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    ret.commandBufferInfoCount = 1;
    ret.pCommandBufferInfos = cmd_submit_info;
    ret.waitSemaphoreInfoCount = 2;
    ret.pWaitSemaphoreInfos = semaphore_info;
    return ret;
}
// `Surface and `Swapchain
static Window *s_Window;
Window* get_window_instance() { return s_Window; }

void init_window(Gpu *gpu, Glfw *glfw) {
    VkSurfaceKHR surface = create_vk_surface(gpu->vk_instance, glfw);

    VkSwapchainKHR swapchain = create_vk_swapchain(gpu, surface);
    Window *window = get_window_instance();
    window->vk_swapchain = swapchain;
}
void kill_window(Gpu *gpu, Window *window) {
    destroy_vk_swapchain(gpu->vk_device, window);
    destroy_vk_surface(gpu->vk_instance, window->info.surface);
    free_h(window);
}

VkSurfaceKHR create_vk_surface(VkInstance vk_instance, Glfw *glfw) {
    VkSurfaceKHR vk_surface;
    auto check = glfwCreateWindowSurface(vk_instance, glfw->window, NULL, &vk_surface);

    DEBUG_OBJ_CREATION(glfwCreateWindowSurface, check);
    return vk_surface;
}
void destroy_vk_surface(VkInstance vk_instance, VkSurfaceKHR vk_surface) {
    vkDestroySurfaceKHR(vk_instance, vk_surface, ALLOCATION_CALLBACKS);
}

VkSwapchainKHR recreate_vk_swapchain(Gpu *gpu, Window *window) {
    vkDestroyImageView(gpu->vk_device, window->vk_image_views[0], ALLOCATION_CALLBACKS);
    vkDestroyImageView(gpu->vk_device, window->vk_image_views[1], ALLOCATION_CALLBACKS);

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu->vk_physical_device, window->info.surface, &surface_capabilities);

    window->info.imageExtent = surface_capabilities.currentExtent;
    window->info.preTransform = surface_capabilities.currentTransform;

    //
    // This might error with some stuff in the createinfo not properly define,
    // I made the refactor while sleepy!
    //
    auto check = vkCreateSwapchainKHR(gpu->vk_device, &window->info, ALLOCATION_CALLBACKS, &window->vk_swapchain);

    DEBUG_OBJ_CREATION(vkCreateSwapchainKHR, check);
    window->info.oldSwapchain = window->vk_swapchain;

    // Image setup
    auto img_check = vkGetSwapchainImagesKHR(gpu->vk_device, window->vk_swapchain, &window->image_count, window->vk_images);
    DEBUG_OBJ_CREATION(vkGetSwapchainImagesKHR, img_check);

    VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = window->info.imageFormat;

    view_info.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
    };
    view_info.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, // base mip level
        1, // mip level count
        0, // base array layer
        1, // array layer count
    };

    view_info.image = window->vk_images[0];
    check = vkCreateImageView(gpu->vk_device, &view_info, ALLOCATION_CALLBACKS, window->vk_image_views);
    DEBUG_OBJ_CREATION(vkCreateImageView, check);

    view_info.image = window->vk_images[1];
    check = vkCreateImageView(gpu->vk_device, &view_info, ALLOCATION_CALLBACKS, window->vk_image_views + 1);
    DEBUG_OBJ_CREATION(vkCreateImageView, check);

    return window->vk_swapchain;
}

VkSwapchainKHR create_vk_swapchain(Gpu *gpu, VkSurfaceKHR vk_surface) {
    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu->vk_physical_device, vk_surface, &surface_capabilities);

    VkSwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.surface = vk_surface;
    swapchain_info.imageExtent = surface_capabilities.currentExtent;
    swapchain_info.preTransform = surface_capabilities.currentTransform;

    u32 format_count;
    VkSurfaceFormatKHR *formats;
    u32 present_mode_count;
    VkPresentModeKHR *present_modes;

    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu->vk_physical_device, swapchain_info.surface, &format_count, NULL);
    formats = (VkSurfaceFormatKHR*)malloc_t(sizeof(VkSurfaceFormatKHR) * format_count, 8);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu->vk_physical_device, swapchain_info.surface, &format_count, formats);

    swapchain_info.imageFormat = formats[0].format;
    COLOR_ATTACHMENT_FORMAT = swapchain_info.imageFormat;
    swapchain_info.imageColorSpace = formats[0].colorSpace;

    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu->vk_physical_device, swapchain_info.surface, &present_mode_count, NULL);
    present_modes = (VkPresentModeKHR*)malloc_t(sizeof(VkPresentModeKHR) * present_mode_count, 8);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu->vk_physical_device, swapchain_info.surface, &present_mode_count, present_modes);

    for(int i = 0; i < present_mode_count; ++i) {
        if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            // @Todo immediate presentation
            println("Mailbox Presentation Supported, but using FIFO (@Todo)...");
    }
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;

    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;

    swapchain_info.minImageCount = surface_capabilities.minImageCount < 2 ? 2 : surface_capabilities.minImageCount;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.clipped = VK_TRUE;

    swapchain_info.queueFamilyIndexCount = 1;
    swapchain_info.pQueueFamilyIndices = &gpu->vk_queue_indices[1];

    VkSwapchainKHR swapchain;
    auto check = vkCreateSwapchainKHR(gpu->vk_device, &swapchain_info, ALLOCATION_CALLBACKS, &swapchain);
    DEBUG_OBJ_CREATION(vkCreateSwapchainKHR, check);

    // Image setup
    u32 image_count = surface_capabilities.minImageCount < 2 ? 2 : surface_capabilities.minImageCount;

    // keep struct data together (not pointing to random heap addresses)
    s_Window = (Window*)malloc_h(
            sizeof(Window)                       +
            (sizeof(VkImage)     * image_count)  +
            (sizeof(VkImageView) * image_count), 8);

    // Is this better than just continuing to use s_Window? who cares...
    Window *window = get_window_instance();
    window->vk_swapchain = swapchain;
    window->info = swapchain_info;
    window->info.oldSwapchain = swapchain;

    window->image_count = image_count;
    window->vk_images = (VkImage*)(window + 1);
    window->vk_image_views = (VkImageView*)(window->vk_images + image_count);

    u32 image_count_check;
    vkGetSwapchainImagesKHR(gpu->vk_device, window->vk_swapchain, &image_count_check, NULL);
    ASSERT(image_count_check == image_count, "Incorrect return value from GetSwapchainImages");

    auto check_swapchain_img_count =
        vkGetSwapchainImagesKHR(gpu->vk_device, window->vk_swapchain, &image_count, window->vk_images);
    DEBUG_OBJ_CREATION(vkGetSwapchainImagesKHR, check_swapchain_img_count);

    // Create Views
    VkImageViewCreateInfo view_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format   = swapchain_info.imageFormat;
    // @Todo Properly choose any gamma corrected format. Currently I just choose the very first one.
    // Luckily this is B8G8R8A8_SRGB
    // println("Swapchain Image Format: %u", view_info.format);

    view_info.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY,
    };
    view_info.subresourceRange = {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, // base mip level
        1, // mip level count
        0, // base array layer
        1, // array layer count
    };

    view_info.image = window->vk_images[0];
    check = vkCreateImageView(gpu->vk_device, &view_info, ALLOCATION_CALLBACKS, window->vk_image_views);
    DEBUG_OBJ_CREATION(vkCreateImageView, check);

    view_info.image = window->vk_images[1];
    check = vkCreateImageView(gpu->vk_device, &view_info, ALLOCATION_CALLBACKS, window->vk_image_views + 1);
    DEBUG_OBJ_CREATION(vkCreateImageView, check);

    reset_temp(); // end of basic initializations so clear temp
    return swapchain;
}
void destroy_vk_swapchain(VkDevice device, Window *window) {
    vkDestroyImageView(device, window->vk_image_views[0], ALLOCATION_CALLBACKS);
    vkDestroyImageView(device, window->vk_image_views[1], ALLOCATION_CALLBACKS);
    vkDestroySwapchainKHR(device, window->vk_swapchain, ALLOCATION_CALLBACKS);
}

// Command Buffers
Gpu_Command_Allocator gpu_create_command_allocator(
    VkDevice vk_device, int queue_family_index, bool transient, int size) {
    VkCommandPoolCreateInfo create_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    create_info.flags |= transient; // transient flag == 0x01, other flags are unsupported
    create_info.queueFamilyIndex = queue_family_index;

    VkCommandPool pool;
    auto check = vkCreateCommandPool(vk_device, &create_info, ALLOCATION_CALLBACKS, &pool);
    DEBUG_OBJ_CREATION(vkCreateCommandPool, check);

    Gpu_Command_Allocator allocator = {};
    allocator.pool         = pool;
    allocator.buffer_count = 0;
    allocator.cap          = size;
    allocator.buffers      = (VkCommandBuffer*)malloc_h(sizeof(VkCommandBuffer) * size, 8);
    return allocator;
}
void gpu_destroy_command_allocator(VkDevice vk_device, Gpu_Command_Allocator *allocator) {
    vkResetCommandPool(vk_device, allocator->pool, 0x0);
    vkDestroyCommandPool(vk_device, allocator->pool, ALLOCATION_CALLBACKS);
    free_h(allocator->buffers);
}
void gpu_reset_command_allocator(VkDevice vk_device, Gpu_Command_Allocator *allocator) {
    vkResetCommandPool(vk_device, allocator->pool, 0x0);
    allocator->buffer_count = 0;
}
VkCommandBuffer* gpu_allocate_command_buffers(
    VkDevice vk_device, Gpu_Command_Allocator *allocator, int count, bool secondary) {
    VkCommandBufferAllocateInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    info.commandPool = allocator->pool;
    info.level = (VkCommandBufferLevel)secondary;
    info.commandBufferCount = count;

    // @Todo handle overflow better? Do I want the allocator to be able to grow? Or should the
    // client know how many are necessary (I expect and prefer the second one...)
    ASSERT(allocator->buffer_count + count <= allocator->cap, "Command Allocator Overflow");
    auto check =
        vkAllocateCommandBuffers(vk_device, &info, allocator->buffers + allocator->buffer_count);
    DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

    allocator->buffer_count += count;
    return allocator->buffers + (allocator->buffer_count -  count);
}

/* Queue */

VkSemaphoreSubmitInfo gpu_define_semaphore_submission(
    VkSemaphore semaphore, Gpu_Pipeline_Stage_Flags stages) {
    VkSemaphoreSubmitInfo ret = {VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    ret.semaphore = semaphore;
    ret.stageMask = (VkPipelineStageFlags)stages;
    return ret;
}
VkSubmitInfo2 gpu_get_submit_info(Gpu_Queue_Submit_Info *info) {
    VkSubmitInfo2 ret = {VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    ret.waitSemaphoreInfoCount = info->wait_count;
    ret.pWaitSemaphoreInfos   =  info->wait_infos;
    ret.signalSemaphoreInfoCount = info->signal_count;
    ret.pSignalSemaphoreInfos   =  info->signal_infos;

    VkCommandBufferSubmitInfo *cmd_infos =
        (VkCommandBufferSubmitInfo*)malloc_t(
            sizeof(VkCommandBufferSubmitInfo) * info->cmd_count, 8);
    for(int i = 0; i < info->cmd_count; ++i) {
        cmd_infos[i] = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmd_infos[i].commandBuffer = info->command_buffers[i];
    }
    ret.commandBufferInfoCount = info->cmd_count;
    ret.pCommandBufferInfos    = cmd_infos;

    return ret;
}

/* `Sync */

// MemoryBarriers
VkBufferMemoryBarrier2 gpu_get_buffer_barrier(Gpu_Buffer_Barrier_Info *info) {
    VkBufferMemoryBarrier2 ret = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    ret.srcQueueFamilyIndex = info->src_queue;
    ret.dstQueueFamilyIndex = info->dst_queue;
    ret.buffer              = info->buffer;
    ret.offset              = info->offset;
    ret.size                = info->size;

    switch(info->setting) {
    case GPU_MEMORY_BARRIER_SETTING_TRANSFER_SRC:
        ret.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        ret.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        break;
    case GPU_MEMORY_BARRIER_SETTING_VERTEX_INDEX_OWNERSHIP_TRANSFER:
        ret.dstStageMask  =
            VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
            VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        ret.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        break;
    case GPU_MEMORY_BARRIER_SETTING_VERTEX_BUFFER_TRANSFER_DST:
        ret.dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
        ret.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        break;
    case GPU_MEMORY_BARRIER_SETTING_INDEX_BUFFER_TRANSFER_DST:
        ret.dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
        ret.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
        break;
    }

    return ret;
}
VkDependencyInfo gpu_get_pipeline_barrier(Gpu_Pipeline_Barrier_Info *info) {
    VkDependencyInfo ret = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    ret.dependencyFlags                    = VK_DEPENDENCY_BY_REGION_BIT;
    ret.memoryBarrierCount       = info->memory_count;
    ret.pMemoryBarriers          = info->memory_barriers;
    ret.bufferMemoryBarrierCount = info->buffer_count;
    ret.pBufferMemoryBarriers    = info->buffer_barriers;
    ret.imageMemoryBarrierCount  = info->image_count;
    ret.pImageMemoryBarriers     = info->image_barriers;
    return ret;
}

// @Todo for these sync pools, maybe implement a free mask and simd checking, so if there is a large enough
// block of free objects in the middle of the pool, they can be used instead of appending. I have no idea if this
// will be useful, as my intendedm use case for this system is to have a persistent pool and a temp pool. This
// would eliminate the requirement for space saving.
Gpu_Fence_Pool gpu_create_fence_pool(VkDevice vk_device, u32 size) {
    Gpu_Fence_Pool pool;
    pool.len = size;
    pool.in_use = 0;
    pool.vk_fences = (VkFence*)malloc_h(sizeof(VkFence) * size, 8);

    VkFenceCreateInfo info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkResult check;
    for(int i = 0; i < size; ++i) {
        check = vkCreateFence(vk_device, &info, ALLOCATION_CALLBACKS, &pool.vk_fences[i]);
        DEBUG_OBJ_CREATION(vkCreateFence, check);
    }

    return pool;
}
void gpu_destroy_fence_pool(VkDevice vk_device, Gpu_Fence_Pool *pool) {
    ASSERT(pool->in_use == 0, "Fences cannot be in use when pool is destroyed");
    for(int i = 0; i < pool->len; ++i) {
        vkDestroyFence(vk_device, pool->vk_fences[i], ALLOCATION_CALLBACKS);
    }
    free_h((void*)pool->vk_fences);
}
VkFence* gpu_get_fences(Gpu_Fence_Pool *pool, u32 count) {
    ASSERT(pool->in_use + count <= pool-> len, "Fence Pool Overflow");
    pool->in_use += count;
    return pool->vk_fences + (pool->in_use - count);
}
void gpu_reset_fence_pool(VkDevice vk_device, Gpu_Fence_Pool *pool) {
    vkResetFences(vk_device, pool->in_use, pool->vk_fences);
    pool->in_use = 0;
}
void gpu_cut_tail_fences(Gpu_Fence_Pool *pool, u32 size) {
    pool->in_use -= size;
}

// Semaphores
Gpu_Binary_Semaphore_Pool gpu_create_binary_semaphore_pool(VkDevice vk_device, u32 size) {
    Gpu_Binary_Semaphore_Pool pool;
    pool.len = size;
    pool.in_use = 0;
    pool.vk_semaphores = (VkSemaphore*)malloc_h(sizeof(VkSemaphore) * size, 8);

    VkSemaphoreCreateInfo info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkResult check;
    for(int i = 0; i < size; ++i) {
        check = vkCreateSemaphore(vk_device, &info, ALLOCATION_CALLBACKS, &pool.vk_semaphores[i]);
        DEBUG_OBJ_CREATION(vkCreateSemaphore, check);
    }

    return pool;
}
void gpu_destroy_semaphore_pool(VkDevice vk_device, Gpu_Binary_Semaphore_Pool *pool) {
    for(int i = 0; i < pool->len; ++i) {
        vkDestroySemaphore(vk_device, pool->vk_semaphores[i], ALLOCATION_CALLBACKS);
    }
    free_h((void*)pool->vk_semaphores);
}
VkSemaphore* gpu_get_binary_semaphores(Gpu_Binary_Semaphore_Pool *pool, u32 count) {
    ASSERT(pool->in_use + count <= pool->len, "Semaphore Pool Overflow");
    pool->in_use += count;
    return pool->vk_semaphores + (pool->in_use - count);
}
// poopoo <- do not delete this comment - Sol & Jenny, Sept 16 2023
void gpu_reset_binary_semaphore_pool(Gpu_Binary_Semaphore_Pool *pool) {
    pool->in_use = 0;
}
void gpu_cut_tail_binary_semaphores(Gpu_Binary_Semaphore_Pool *pool, u32 size) {
    pool->in_use -= size;
}

// `Descriptors -- static / pool allocated

// @Todo Get an idea for average descriptor allocation patterns, so that pools can be created with
// descriptor counts which are representative of the application's usage patterns...
VkDescriptorPool create_vk_descriptor_pool(VkDevice vk_device, int max_set_count, int counts[11]) {
    VkDescriptorPoolSize pool_sizes[11];
    int size_count = 0;
    for(int i = 0; i < 11; ++i) {
        if (counts[i] != 0) {
            pool_sizes[size_count].descriptorCount = counts[i];
            pool_sizes[size_count].type = (VkDescriptorType)i;
            size_count++;
        }
    }

    VkDescriptorPoolCreateInfo create_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    create_info.maxSets       = max_set_count;
    create_info.pPoolSizes    = pool_sizes;
    create_info.poolSizeCount     = size_count;

    VkDescriptorPool pool;
    auto check = vkCreateDescriptorPool(vk_device, &create_info, ALLOCATION_CALLBACKS, &pool);
    DEBUG_OBJ_CREATION(vkCreateDescriptorPool, check);
    return pool;
}
VkResult reset_vk_descriptor_pool(VkDevice vk_device, VkDescriptorPool pool) {
    return vkResetDescriptorPool(vk_device, pool, 0x0);
}
void destroy_vk_descriptor_pool(VkDevice vk_device, VkDescriptorPool pool) {
    vkDestroyDescriptorPool(vk_device, pool, ALLOCATION_CALLBACKS);
}

Gpu_Descriptor_Allocator gpu_create_descriptor_allocator(VkDevice vk_device, int max_sets, int counts[11]) {
    Gpu_Descriptor_Allocator allocator = {};
    for(int i = 0; i < 11; ++i)
        allocator.cap[i] = counts[i];

    allocator.pool =
        create_vk_descriptor_pool(vk_device, max_sets, counts);
    allocator.set_cap = max_sets;

    u8 *memory_block = malloc_h(
       (sizeof(VkDescriptorSetLayout) * max_sets) +
       (      sizeof(VkDescriptorSet) * max_sets), 8);

    allocator.layouts = (VkDescriptorSetLayout*)(memory_block);
    allocator.sets    = (VkDescriptorSet*)(allocator.layouts + max_sets);

    return allocator;
}
void gpu_destroy_descriptor_allocator(VkDevice vk_device, Gpu_Descriptor_Allocator *allocator)
{
    vkDestroyDescriptorPool(vk_device, allocator->pool, ALLOCATION_CALLBACKS);
    free_h(allocator->layouts);
    *allocator = {};
}
void gpu_reset_descriptor_allocator(VkDevice vk_device, Gpu_Descriptor_Allocator *allocator)
{
    allocator->sets_queued    = 0;
    allocator->sets_allocated = 0;
    // Zeroing this way seems dumb, but I dont want to memset...
    for(int i = 0; i < 11; ++i)
        allocator->counts[i] = 0;
    vkResetDescriptorPool(vk_device, allocator->pool, 0x0);
}
VkDescriptorSet* gpu_queue_descriptor_set_allocation(
    Gpu_Descriptor_Allocator *allocator, Gpu_Queue_Descriptor_Set_Allocation_Info *info, VkResult *result)
{
    ASSERT(allocator->sets_queued + info->layout_count >= allocator->sets_allocated, "Overflow Check");

    for(int i = 0; i < 11; ++i) {
        ASSERT(allocator->counts[i] + info->descriptor_counts[i] >= allocator->counts[i],
               "Overflow Check");
        allocator->counts[i] += info->descriptor_counts[i];

        #if DEBUG // Check for individual descriptor overflow
        if (allocator->counts[i] > allocator->cap[i]) {
            *result = VK_ERROR_OUT_OF_POOL_MEMORY;
            ASSERT(false, "Descriptor Allocator Descriptor Overflow");
            return NULL;
        }
        #endif
    }
    ASSERT(info->descriptor_counts[(int)VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC] == 0,
           "Type Not Supported");
    ASSERT(info->descriptor_counts[(int)VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC] == 0,
           "Type Not Supported");

    for(int i = 0; i < info->layout_count; ++i) {
            allocator->layouts[allocator->sets_queued] = info->layouts[i];
            allocator->sets_queued++;
            break;
    }
    ASSERT(allocator->sets_queued <= allocator->set_cap, "Descriptor Allocator Set Overflow");

    return allocator->sets + (allocator->sets_queued - info->layout_count);
}
// This should never fail with 'OUT_OF_POOL_MEMORY' or 'FRAGMENTED_POOL' because of the way the pools
// are managed by the allocator... I will manage the out of host and device memory with just an
// assert for now, as handling an error like that would require a large management op...
void gpu_allocate_descriptor_sets(VkDevice vk_device, Gpu_Descriptor_Allocator *allocator)
{
    VkDescriptorSetAllocateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    info.descriptorPool     = allocator->pool;
    info.descriptorSetCount = allocator->sets_queued - allocator->sets_allocated;
    info.pSetLayouts        = allocator->layouts     + allocator->sets_allocated;

    if (!info.descriptorSetCount)
        return;

    auto check =
        vkAllocateDescriptorSets(vk_device, &info, allocator->sets + allocator->sets_allocated);
    DEBUG_OBJ_CREATION(vkAllocateDescriptorSets, check);
    allocator->sets_allocated = allocator->sets_queued;
}

//
// @Goal For descriptor allocation, I would like all descriptors (for a thread) to allocated in one go:
//     When smtg wants to get a descriptor set, it gets put in a buffer, and then this buffer is at
//     whatever fill level and all the sets are allocated and returned... - sol 4 oct 2023
//
// -- (Really, I dont know why you cant just create all the descriptors necessary for shaders in one
// go store them for the lifetime of the program, tbf big programs can have A LOT of shaders, but many
// sets should be usable across many shaders and pipelines etc. I feel like with proper planning,
// this would be possible... idk maybe I am miles off) -- - sol 4 oct 2023
//
VkDescriptorSetLayout*
create_vk_descriptor_set_layouts(VkDevice vk_device, int count, Create_Vk_Descriptor_Set_Layout_Info *infos)
{
    // @Todo think about the lifetime of this allocation
    VkDescriptorSetLayout *layouts =
        (VkDescriptorSetLayout*)malloc_h(
            sizeof(VkDescriptorSetLayout) * count, 8);

    VkDescriptorSetLayoutCreateInfo create_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VkResult check;
    for(int i = 0; i < count; ++i) {
        create_info.bindingCount = infos[i].count;
        create_info.pBindings    = infos[i].bindings;
        check = vkCreateDescriptorSetLayout(
                    vk_device, &create_info, ALLOCATION_CALLBACKS, &layouts[i]);
        DEBUG_OBJ_CREATION(vkCreateDescriptorSetLayout, check);
    }
    return layouts;
}
void gpu_destroy_descriptor_set_layouts(VkDevice vk_device, int count, VkDescriptorSetLayout *layouts) {
    for(int i = 0; i < count; ++i)
        vkDestroyDescriptorSetLayout(vk_device, layouts[i], ALLOCATION_CALLBACKS);
    free_h(layouts);
}

Gpu_Descriptor_List gpu_make_descriptor_list(int count, Create_Vk_Descriptor_Set_Layout_Info *infos) {
    Gpu_Descriptor_List ret = {};
    VkDescriptorSetLayoutBinding *binding;
    for(int i = 0; i < count; ++i) {
        for(int j = 0; j < infos[i].count; ++j) {
            binding = &infos[i].bindings[j];
            ret.counts[(int)binding->descriptorType] += binding->descriptorCount;
        }
    }
    return ret;
}

// `PipelineSetup
// `ShaderStages
VkPipelineShaderStageCreateInfo* create_vk_pipeline_shader_stages(VkDevice vk_device, u32 count, Create_Vk_Pipeline_Shader_Stage_Info *infos) {
    // @Todo like with other aspects of pipeline creation, I think that shader stage infos can all be allocated
    // and loaded at startup and the  not freed for the duration of the program as these are not changing state
    u8 *memory_block = malloc_h(sizeof(VkPipelineShaderStageCreateInfo) * count, 8);

    VkResult check;
    VkShaderModuleCreateInfo          module_info;
    VkPipelineShaderStageCreateInfo  *stage_info;
    for(int i = 0; i < count; ++i) {

        module_info = {
            VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            NULL,
            0x0,
            infos[i].code_size,
            infos[i].shader_code,
        };

        VkShaderModule mod;
        check = vkCreateShaderModule(vk_device, &module_info, ALLOCATION_CALLBACKS, &mod);
        DEBUG_OBJ_CREATION(vkCreateShaderModule, check);

        stage_info = (VkPipelineShaderStageCreateInfo*)(memory_block + (sizeof(VkPipelineShaderStageCreateInfo) * i));

       *stage_info = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};

        stage_info->pNext  = NULL;
        stage_info->stage  = infos[i].stage;
        stage_info->module = mod;
        stage_info->pName  = "main";

        // @Todo add specialization support
        stage_info->pSpecializationInfo = NULL;
    }
    return (VkPipelineShaderStageCreateInfo*)memory_block;
}

void destroy_vk_pipeline_shader_stages(VkDevice vk_device, u32 count, VkPipelineShaderStageCreateInfo *stages) {
    for(int i = 0; i < count; ++i) {
        vkDestroyShaderModule(vk_device, stages[i].module, ALLOCATION_CALLBACKS);
    }
    free_h((void*)stages);
}

// `VertexInputState
VkVertexInputBindingDescription create_vk_vertex_binding_description(Create_Vk_Vertex_Input_Binding_Description_Info *info) {
    VkVertexInputBindingDescription binding_description = { info->binding, info->stride };
    return binding_description;
}

VkVertexInputAttributeDescription create_vk_vertex_attribute_description(Create_Vk_Vertex_Input_Attribute_Description_Info *info) {
    VkVertexInputAttributeDescription attribute_description = {
        info->location,
        info->binding,
        info->format,
        info->offset,
    };
    return attribute_description;
}
VkPipelineVertexInputStateCreateInfo create_vk_pipeline_vertex_input_states(Create_Vk_Pipeline_Vertex_Input_State_Info *info) {
    // @Todo @PipelineAllocation same as above ^^
    VkPipelineVertexInputStateCreateInfo input_state_info = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input_state_info.vertexBindingDescriptionCount   = info->binding_count;
    input_state_info.pVertexBindingDescriptions      = info->binding_descriptions;
    input_state_info.vertexAttributeDescriptionCount = info->attribute_count;
    input_state_info.pVertexAttributeDescriptions    = info->attribute_descriptions;

    return input_state_info;
}

// `InputAssemblyState
VkPipelineInputAssemblyStateCreateInfo create_vk_pipeline_input_assembly_state(VkPrimitiveTopology topology, VkBool32 primitive_restart) {
    // @Todo @PipelineAllocation same as above ^^
    // ^^ not sure about these todos now that I have gltf use cases ... - sol, sept 30 2023

    VkPipelineInputAssemblyStateCreateInfo assembly_state_info = {};

    assembly_state_info = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly_state_info.topology = topology;
    assembly_state_info.primitiveRestartEnable = primitive_restart;

    return assembly_state_info;
}

// `TessellationState
// @Todo support Tessellation

// `Viewport
VkPipelineViewportStateCreateInfo create_vk_pipeline_viewport_state(Window *window) {
    VkViewport viewport = {
        0.0f, 0.0f, // x, y
        (float)window->info.imageExtent.width,
        (float)window->info.imageExtent.height,
        0.0f, 1.0f, // mindepth, maxdepth
    };
    VkRect2D scissor = {
        {0, 0}, // offsets
        {
            window->info.imageExtent.width,
            window->info.imageExtent.height,
        },
    };
    VkPipelineViewportStateCreateInfo viewport_info = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport_info.viewportCount = 0; // must be zero if dyn state set viewport with count is set
    /*viewport_info.pViewports = &viewport;
    viewport_info.scissorCount = 1;
    viewport_info.pScissors = &scissor;*/

    return viewport_info;
}

// `RasterizationState
VkPipelineRasterizationStateCreateInfo create_vk_pipeline_rasterization_state(VkPolygonMode polygon_mode, VkCullModeFlags cull_mode, VkFrontFace front_face) {
    VkPipelineRasterizationStateCreateInfo state = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    state.polygonMode = polygon_mode;
    ASSERT(cull_mode == VK_CULL_MODE_NONE, "");
    state.cullMode    = cull_mode;
    state.frontFace   = front_face;
    state.lineWidth   = 1.0f;
    return state;
}
void vkCmdSetDepthClampEnableEXT(VkCommandBuffer commandBuffer, VkBool32 depthClampEnable) {
    VkDevice device = get_gpu_instance()->vk_device;
    auto func = (PFN_vkCmdSetDepthClampEnableEXT) vkGetDeviceProcAddr(device, "vkCmdSetDepthClampEnableEXT");

    ASSERT(func != nullptr, "Depth Clamp Enable Cmd not found");
    return func(commandBuffer, depthClampEnable);
}
void vkCmdSetPolygonModeEXT(VkCommandBuffer commandBuffer, VkPolygonMode polygonMode) {
    VkDevice device = get_gpu_instance()->vk_device;
    auto func = (PFN_vkCmdSetPolygonModeEXT) vkGetDeviceProcAddr(device, "vkCmdSetPolygonModeEXT");

    ASSERT(func != nullptr, "Polygon Mode Cmd not found");
    return func(commandBuffer, polygonMode);
}

// `MultisampleState // @Todo actually support setting multisampling functions
VkPipelineMultisampleStateCreateInfo create_vk_pipeline_multisample_state(VkSampleCountFlagBits sample_count) {
    VkPipelineMultisampleStateCreateInfo state = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    state.rasterizationSamples = sample_count;
    state.sampleShadingEnable = VK_FALSE;
    return state;
}

// `DepthStencilState
VkPipelineDepthStencilStateCreateInfo create_vk_pipeline_depth_stencil_state(Create_Vk_Pipeline_Depth_Stencil_State_Info *info) {
    VkPipelineDepthStencilStateCreateInfo state = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    state.depthTestEnable       = info->depth_test_enable;
    state.depthWriteEnable      = info->depth_write_enable;
    state.depthBoundsTestEnable = info->depth_bounds_test_enable;
    state.depthCompareOp        = info->depth_compare_op;
    state.minDepthBounds        = info->min_depth_bounds;
    state.maxDepthBounds        = info->max_depth_bounds;
    return state;
}

// `BlendState - Lots of inlined dyn states
VkPipelineColorBlendStateCreateInfo create_vk_pipeline_color_blend_state(Create_Vk_Pipeline_Color_Blend_State_Info *info) {
    // @PipelineAllocations I think this state is one that contrasts to the others, these will likely be super
    // ephemeral. I do not know to what extent I can effect color blending. Not very much I assume without
    // extended dyn state 3... so I think this will require recompilations or state explosion...
    VkPipelineColorBlendStateCreateInfo blend_state = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend_state.attachmentCount = info->attachment_count;
    blend_state.pAttachments    = info->attachment_states;

    // just set to whatever cos this can be set dyn
    blend_state.blendConstants[0] = 1;
    blend_state.blendConstants[1] = 1;
    blend_state.blendConstants[2] = 1;
    blend_state.blendConstants[3] = 1;

    return blend_state;
}

void vkCmdSetLogicOpEnableEXT(VkCommandBuffer commandBuffer, VkBool32 logicOpEnable) {
    VkDevice device = get_gpu_instance()->vk_device;
    auto func = (PFN_vkCmdSetLogicOpEnableEXT) vkGetDeviceProcAddr(device, "vkCmdSetLogicOpEnableEXT");

    ASSERT(func != nullptr, "Logic Op Enable Cmd not found");
    return func(commandBuffer, logicOpEnable);
}
void vkCmdSetColorBlendEnableEXT(VkCommandBuffer commandBuffer, u32 firstAttachment, u32 attachmentCount, VkBool32 *pColorBlendEnables) {
    VkDevice device = get_gpu_instance()->vk_device;
    auto func = (PFN_vkCmdSetColorBlendEnableEXT) vkGetDeviceProcAddr(device, "vkCmdSetColorBlendEnableEXT");

    ASSERT(func != nullptr, "Color Blend Enable Cmd not found");
    return func(commandBuffer, firstAttachment, attachmentCount, pColorBlendEnables);
}
void vkCmdSetColorBlendEquationEXT(VkCommandBuffer commandBuffer, u32 firstAttachment, u32 attachmentCount, const VkColorBlendEquationEXT* pColorBlendEquations) {
    VkDevice device = get_gpu_instance()->vk_device;
    auto func = (PFN_vkCmdSetColorBlendEquationEXT) vkGetDeviceProcAddr(device, "vkCmdSetColorBlendEquationEXT");

    ASSERT(func != nullptr, "Color Blend Equation Cmd not found");
    return func(commandBuffer, firstAttachment, attachmentCount, pColorBlendEquations);
}
void vkCmdSetColorWriteMaskEXT(VkCommandBuffer commandBuffer, u32 firstAttachment, u32 attachmentCount, const VkColorComponentFlags* pColorWriteMasks) {
    VkDevice device = get_gpu_instance()->vk_device;
    auto func = (PFN_vkCmdSetColorWriteMaskEXT) vkGetDeviceProcAddr(device, "vkCmdSetColorWriteMaskEXT");

    ASSERT(func != nullptr, "Color Write Mask Cmd not found");
    return func(commandBuffer, firstAttachment, attachmentCount, pColorWriteMasks);
}
// `DynamicState
const VkDynamicState dyn_state_list[] = {
    VK_DYNAMIC_STATE_LINE_WIDTH,
    VK_DYNAMIC_STATE_DEPTH_BIAS,
    VK_DYNAMIC_STATE_BLEND_CONSTANTS,
    VK_DYNAMIC_STATE_DEPTH_BOUNDS,
    VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
    VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
    VK_DYNAMIC_STATE_STENCIL_REFERENCE,

    // Provided by VK_VERSION_1_3 - all below
    VK_DYNAMIC_STATE_CULL_MODE,
    VK_DYNAMIC_STATE_FRONT_FACE,
    VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
    VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
    VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE,
    VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_COMPARE_OP,
    VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE,
    VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
    VK_DYNAMIC_STATE_STENCIL_OP,
    VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE,
    VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
    VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
};
const u32 dyn_state_count = 22; //  This list is 23 last time I counted
    // @Todo @DynState list of possible other dyn states
    //      vertex input
    //      multisampling
VkPipelineDynamicStateCreateInfo create_vk_pipeline_dyn_state() {
    VkPipelineDynamicStateCreateInfo dyn_state = {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        NULL,
        0x0,
        dyn_state_count,
        dyn_state_list,
    };
    return dyn_state;
}

// `PipelineLayout
VkPipelineLayout create_vk_pipeline_layout(VkDevice vk_device, Create_Vk_Pipeline_Layout_Info *info) {
    VkPipelineLayoutCreateInfo create_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    create_info.setLayoutCount         = info->descriptor_set_layout_count;
    create_info.pSetLayouts            = info->descriptor_set_layouts;
    create_info.pushConstantRangeCount = info->push_constant_count;
    create_info.pPushConstantRanges    = info->push_constant_ranges;

    VkPipelineLayout layout;
    auto check = vkCreatePipelineLayout(vk_device, &create_info, ALLOCATION_CALLBACKS, &layout);
    DEBUG_OBJ_CREATION(vkCreatePipelineLayout, check);
    return layout;
}
void destroy_vk_pipeline_layout(VkDevice vk_device, VkPipelineLayout pl_layout) {
    vkDestroyPipelineLayout(vk_device, pl_layout, ALLOCATION_CALLBACKS);
}

// PipelineRenderingInfo
VkPipelineRenderingCreateInfo create_vk_pipeline_rendering_info(Create_Vk_Pipeline_Rendering_Info_Info *info) {
    VkPipelineRenderingCreateInfo create_info = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    create_info.viewMask = info->view_mask;
    create_info.colorAttachmentCount    = info->color_attachment_count;
    create_info.pColorAttachmentFormats = info->color_attachment_formats;
    create_info.depthAttachmentFormat   = info->depth_attachment_format;
    create_info.stencilAttachmentFormat = info->stencil_attachment_format;
    return create_info;
}

// `Pipeline Final - static / descriptor pools
void create_vk_graphics_pipelines(VkDevice vk_device, VkPipelineCache cache, int count, Create_Vk_Pipeline_Info *info, VkPipeline *pipelines) {

    // @Todo wrap this state shit in a loop (multiple pipeline creation)

    // `Vertex Input Stage 1
    VkVertexInputBindingDescription *vertex_binding_descriptions =
        (VkVertexInputBindingDescription*)malloc_t(
            sizeof(VkVertexInputBindingDescription) * info->vertex_input_state->input_binding_description_count, 8);

    Create_Vk_Vertex_Input_Binding_Description_Info binding_info;
    for(int i = 0; i < info->vertex_input_state->input_binding_description_count; ++i) {
        binding_info = {
            (u32)info->vertex_input_state->binding_description_bindings[i],
            (u32)info->vertex_input_state->binding_description_strides[i],
        };
        vertex_binding_descriptions[i] = create_vk_vertex_binding_description(&binding_info);
    }

    VkVertexInputAttributeDescription *vertex_attribute_descriptions =
        (VkVertexInputAttributeDescription*)malloc_t(
            sizeof(VkVertexInputAttributeDescription) * info->vertex_input_state->input_attribute_description_count, 8);

    Create_Vk_Vertex_Input_Attribute_Description_Info attribute_info;
    for(int i = 0; i < info->vertex_input_state->input_attribute_description_count; ++i) {
        attribute_info = {
            .location = (u32)info->vertex_input_state->attribute_description_locations[i],
            .binding = (u32)info->vertex_input_state->attribute_description_bindings[i],
            .format = info->vertex_input_state->formats[i],
            .offset = 0,
        };
        vertex_attribute_descriptions[i] = create_vk_vertex_attribute_description(&attribute_info);
    }

    Create_Vk_Pipeline_Vertex_Input_State_Info create_input_state_info = {
        (u32)info->vertex_input_state->input_binding_description_count,
        (u32)info->vertex_input_state->input_attribute_description_count,
        vertex_binding_descriptions,
        vertex_attribute_descriptions,
    };
    VkPipelineVertexInputStateCreateInfo vertex_input = create_vk_pipeline_vertex_input_states(&create_input_state_info);

    VkPipelineInputAssemblyStateCreateInfo input_assembly = create_vk_pipeline_input_assembly_state(info->vertex_input_state->topology, VK_FALSE);


    // `Rasterization Stage 2
    Window *window = get_window_instance();
    VkPipelineViewportStateCreateInfo viewport = create_vk_pipeline_viewport_state(window);

    // @Todo setup multiple pipeline compilation for differing topology state
    VkPipelineRasterizationStateCreateInfo rasterization = create_vk_pipeline_rasterization_state(info->rasterization_state->polygon_modes[0], info->rasterization_state->cull_mode, info->rasterization_state->front_face);


    // `Fragment Shader Stage 3
    VkPipelineMultisampleStateCreateInfo multisample = create_vk_pipeline_multisample_state(info->fragment_shader_state->sample_count);

    Create_Vk_Pipeline_Depth_Stencil_State_Info depth_stencil_info = {
        (VkBool32)(info->fragment_shader_state->flags & GPU_FRAGMENT_SHADER_DEPTH_TEST_ENABLE_BIT),
        (VkBool32)(info->fragment_shader_state->flags & GPU_FRAGMENT_SHADER_DEPTH_WRITE_ENABLE_BIT >> 1),
        (VkBool32)(info->fragment_shader_state->flags & GPU_FRAGMENT_SHADER_DEPTH_WRITE_ENABLE_BIT >> 2),
        info->fragment_shader_state->depth_compare_op,
        info->fragment_shader_state->min_depth_bounds,
        info->fragment_shader_state->max_depth_bounds,
    };
    VkPipelineDepthStencilStateCreateInfo depth_stencil = create_vk_pipeline_depth_stencil_state(&depth_stencil_info);

    // `Output Stage 4
    Create_Vk_Pipeline_Color_Blend_State_Info blend_info = {
        1,
        &info->fragment_output_state->blend_state,
    };
    VkPipelineColorBlendStateCreateInfo blending = create_vk_pipeline_color_blend_state(&blend_info);

    // `Dynamic
    VkDynamicState dyn_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,
        VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT
    };
    VkPipelineDynamicStateCreateInfo dynamic = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates    = dyn_states;

    VkGraphicsPipelineCreateInfo pl_create_info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pl_create_info.stageCount          = info->shader_stage_count;
    pl_create_info.pStages             = info->shader_stages;
    pl_create_info.pVertexInputState   = &vertex_input;
    pl_create_info.pInputAssemblyState = &input_assembly;
    pl_create_info.pViewportState      = &viewport;
    pl_create_info.pRasterizationState = &rasterization;
    pl_create_info.pMultisampleState   = &multisample;
    pl_create_info.pDepthStencilState  = &depth_stencil;
    pl_create_info.pColorBlendState    = &blending;
    pl_create_info.pDynamicState       = &dynamic;
    pl_create_info.layout              = info->layout;
    pl_create_info.renderPass          = info->renderpass;
    pl_create_info.subpass             = 0;

    auto check = vkCreateGraphicsPipelines(vk_device, cache, 1, &pl_create_info, ALLOCATION_CALLBACKS, pipelines);
    DEBUG_OBJ_CREATION(vkCreateGraphicsPipelines, check);
}
void gpu_destroy_pipeline(VkDevice vk_device, VkPipeline pipeline) {
    vkDestroyPipeline(vk_device, pipeline, ALLOCATION_CALLBACKS);
}

// @Todo pipeline: increase possible use of dyn states, eg. vertex input, raster states etc.
// `Pipeline Final - dynamic + descriptor buffers
VkPipeline* create_vk_graphics_pipelines_heap(VkDevice vk_device, VkPipelineCache cache, u32 count, VkGraphicsPipelineCreateInfo *create_infos) {
    for(int i = 0; i < count; ++i) {
        create_infos[i].sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        create_infos[i].flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    }

    // @Todo @Pipeline @Allocation @Speed in future I need a separate creation function for temp and heap allocated pipelines:
    // (a better explanation is do not assume all pipelines should be allocated like this)
    // I can imagine that some pipelines will be more persistent than others and therefore might want to be put on the heap
    // instead.
    // Potential Options:
    //     1. make a large heap allocation where all the persistent pipelines are allocated, this keeps more room in temp allocation
    //        to do what it was made for: short lifetime allocations
    //     2. allocate persistent pipelines first in temp allocation and reset up to this mark, this is a faster allocation, but as it
    //        would only happen once in option 1, not worth an unshifting lump in the linear allocator?
    //     3. Unless I find something, option 1 looks best
    VkPipeline *pipelines = (VkPipeline*)malloc_h(sizeof(VkPipeline) * count, 8);
    auto check = vkCreateGraphicsPipelines(vk_device, cache, count, create_infos, ALLOCATION_CALLBACKS, pipelines);

    DEBUG_OBJ_CREATION(vkCreateGraphicsPipelines, check);
    return pipelines;
}

void destroy_vk_pipelines_heap(VkDevice vk_device, u32 count, VkPipeline *pipelines) {
    for(int i = 0; i < count; ++i) {
        vkDestroyPipeline(vk_device, pipelines[i], ALLOCATION_CALLBACKS);
    }
    free_h(pipelines);
}

// `Static Rendering (passes, subpassed, framebuffer)

/* Begin Better Automate Rendering */

/*
                                    ** WARNING **
    This function is not tested yet using input attachments, resolve attachments etc.
    If things are behaving weirdly while using these types of attachment, look here...
    It is intended for basic drawing, with the other functionality added more as a
    placeholder, intended to keep the function flexible, and make an example and layout
    for setup functions for more complicated renderpasses.

*/
Gpu_Renderpass_Framebuffer gpu_create_renderpass_framebuffer_graphics_single(
    VkDevice vk_device, Gpu_Renderpass_Info *info) {

    // @Todo reduce the branching in this function. There are multiple sections that branch
    // on the same predicate. Collapse these sections into single sections which exist under
    // the same branch predicate. (e.g. multiple tests for there being a depth attachment.
    // This can probably be collasped to just test for it once).
    // Also, arrange the branches to be consistent with static branch checking...

    /* Begin Attachment Descriptions */

    int attachment_description_count = 0;
    attachment_description_count += info->input_attachment_count + info->color_attachment_count;

    if (info->resolve_flags)
        attachment_description_count += info->color_attachment_count;

    VkAttachmentDescription *description;
    VkAttachmentDescription *attachment_descriptions =
        (VkAttachmentDescription*)malloc_t( // +1 in case of depth attachment
            sizeof(VkAttachmentDescription) * (attachment_description_count + 1), 8);

    VkClearValue *clear_values =
        (VkClearValue*)malloc_h(
            sizeof(VkClearValue) * (attachment_description_count + 1), 8);

    // For now, assume no depth attachment, so write to descriptions
    // beyond its slot;
    clear_values++;
    attachment_descriptions++;

    VkSwapchainCreateInfoKHR *color_info = &get_window_instance()->info;
    for(int i = 0; i < info->color_attachment_count; ++i) {

        description = &attachment_descriptions[i];

        *description = {};
        description->format         = color_info->imageFormat;
        description->samples        = (VkSampleCountFlagBits)info->sample_count;
        description->loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        description->storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        description->stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        description->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        description->initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        description->finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        clear_values[i] = info->color_clear_value;
    }

    VkImageLayout *resolve_layouts;
    if (info->resolve_flags) {
        switch(info->resolve_flags) {
        case GPU_ATTACHMENT_COLOR_BIT:
        {
            resolve_layouts =
                    (VkImageLayout*)malloc_t(
                        sizeof(VkImageLayout) * info->color_attachment_count, 8);

            memset(resolve_layouts,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        sizeof(VkImageLayout) * info->color_attachment_count);
            break;
        }
        case GPU_ATTACHMENT_DEPTH_STENCIL_BIT:
        {
            resolve_layouts =
                    (VkImageLayout*)malloc_t(
                        sizeof(VkImageLayout) * info->color_attachment_count, 8);

            memset(resolve_layouts,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    sizeof(VkImageLayout) * info->color_attachment_count);
            break;
        }
        case GPU_ATTACHMENT_COLOR_BIT | GPU_ATTACHMENT_DEPTH_STENCIL_BIT:
        {
            resolve_layouts = (VkImageLayout*)info->resolve_layouts;
            break;
        }
        default:
            ASSERT(false, "Invalid Resolve Flags");
        } // switch info->resolve_flags

        for(int i = 0; i < info->color_attachment_count; ++i) {

            description = &attachment_descriptions[i + info->color_attachment_count];

            *description = {};
            description->samples        = (VkSampleCountFlagBits)info->sample_count;
            description->loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
            description->storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
            description->stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            description->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            description->initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            description->finalLayout    = resolve_layouts[i];

            // Leave the resource in the same layout as it was transitioned to in the subpass
            if (resolve_layouts[i] == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                description->format = color_info->imageFormat;
                clear_values[i + info->color_attachment_count] = info->color_clear_value;
            } else {
                description->format = VK_FORMAT_D16_UNORM;
                clear_values[i + info->color_attachment_count] = info->depth_clear_value;
            }
        }
    }

    VkImageLayout *input_layouts;
    if (info->input_attachment_count) {
        switch(info->input_flags) {
        case 0x0: // Intentional fall through
        case GPU_ATTACHMENT_COLOR_BIT:
        {
            input_layouts =
                (VkImageLayout*)malloc_t(
                    sizeof(VkImageLayout) * info->input_attachment_count, 8);

            memset(input_layouts,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    sizeof(VkImageLayout) * info->input_attachment_count);
            break;
        }
        case GPU_ATTACHMENT_DEPTH_STENCIL_BIT:
        {
            input_layouts =
                (VkImageLayout*)malloc_t(
                    sizeof(VkImageLayout) * info->input_attachment_count, 8);

            memset(input_layouts, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                sizeof(VkImageLayout) * info->input_attachment_count);
            break;
        }
        case GPU_ATTACHMENT_COLOR_BIT | GPU_ATTACHMENT_DEPTH_STENCIL_BIT:
        {
            input_layouts = (VkImageLayout*)info->input_layouts;
            break;
        }
        default:
            ASSERT(false, "Invalid Input Flags");
            input_layouts = NULL; // @CrashMe
            break;
        }
    }

    int input_layout_offset =
        info->resolve_flags ? info->color_attachment_count * 2 : info->color_attachment_count;

    for(int i = 0; i < info->input_attachment_count; ++i) {

        description = &attachment_descriptions[i + input_layout_offset];

        *description = {};
        description->samples        = (VkSampleCountFlagBits)info->sample_count;
        description->loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        description->storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        description->stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        description->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        description->initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        description->finalLayout    = input_layouts[i];

        // Leave the resource in the same layout as it was transitioned to in the subpass
        if (resolve_layouts[i] == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            description->format = color_info->imageFormat;
            clear_values[i + input_layout_offset] = info->color_clear_value;
        } else {
            description->format = VK_FORMAT_D16_UNORM;
            clear_values[i + input_layout_offset] = info->depth_clear_value;
        }
    }

    if (!info->no_depth_attachment) {
        attachment_descriptions--; // return depth slot to stack
        description = &attachment_descriptions[0];

        clear_values--;
        clear_values[0] = info->depth_clear_value;

        *description = {};
        description->samples        = (VkSampleCountFlagBits)info->sample_count;
        description->loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        description->storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        description->stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        description->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        description->initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        description->finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        description->format         = VK_FORMAT_D16_UNORM;

        attachment_description_count++;
    }

    /* End Attachment Descriptions */

    /* Begin Subpass Descriptions */

    VkAttachmentReference *attachment_references =
        (VkAttachmentReference*)malloc_t(
            sizeof(VkAttachmentReference) * attachment_description_count, 8);

    VkAttachmentReference *depth_attachment;
    VkAttachmentReference *color_attachments;
    VkAttachmentReference *resolve_attachments;
    VkAttachmentReference *input_attachments;

    int attachment_reference_index = 0;
    if (!info->no_depth_attachment) {

        attachment_references[0] = {0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        depth_attachment  = &attachment_references[0];
        color_attachments = &attachment_references[1];

        attachment_reference_index++;
    } else {
        depth_attachment  = NULL;
        color_attachments = &attachment_references[0];
    }

    if (info->resolve_flags) {
        resolve_attachments = color_attachments + info->color_attachment_count;
        input_attachments   = resolve_attachments + info->color_attachment_count;
    } else {
        resolve_attachments = NULL;
        input_attachments   = color_attachments + info->color_attachment_count;
    }

    for(int i = 0; i < info->color_attachment_count; ++i) {
        color_attachments[i].attachment = attachment_reference_index;
        color_attachments[i].layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachment_reference_index++;
    }
    if (info->resolve_flags) {
        for(int i = 0; i < info->color_attachment_count; ++i) {
            resolve_attachments[i].attachment = attachment_reference_index;
            resolve_attachments[i].attachment = resolve_layouts[i];

            attachment_reference_index++;
        }
    }
    for(int i = 0; i < info->input_attachment_count; ++i) {
        input_attachments[i].attachment = attachment_reference_index;
        input_attachments[i].attachment = input_layouts[i];

        attachment_reference_index++;
    }

    VkSubpassDescription subpass_description = {};
    subpass_description.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_description.pDepthStencilAttachment = depth_attachment;
    subpass_description.colorAttachmentCount    = info->color_attachment_count;
    subpass_description.pColorAttachments       = color_attachments;
    subpass_description.pResolveAttachments     = resolve_attachments;
    subpass_description.inputAttachmentCount    = info->input_attachment_count;
    subpass_description.pInputAttachments       = input_attachments;

    /* End Subpass Descriptions */

    /* Begin Subpass Dependencies */

    VkPipelineStageFlags src_stage;
    VkPipelineStageFlags dst_stage;
    if (!info->no_depth_attachment) {
        src_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst_stage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else {
        src_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dst_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }

    VkAccessFlags src_access_flags;
    VkAccessFlags dst_access_flags;
    if (!info->no_depth_attachment) {
        src_access_flags = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dst_access_flags = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    } else {
        src_access_flags = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dst_access_flags = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if (info->input_attachment_count) {
        src_access_flags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
        dst_access_flags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    }

    VkSubpassDependency subpass_dependency = {};
    subpass_dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
    subpass_dependency.dstSubpass    = 0;
    subpass_dependency.srcStageMask  = src_stage;
    subpass_dependency.srcAccessMask = src_access_flags;
    subpass_dependency.dstStageMask  = dst_stage;
    subpass_dependency.dstAccessMask = dst_access_flags;
    subpass_dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    /* End Subpass Dependencies */

    /* Create Renderpass */
    VkRenderPassCreateInfo create_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    create_info.attachmentCount = attachment_description_count;
    create_info.pAttachments    = attachment_descriptions;
    create_info.subpassCount    = 1; // Basic renderpass, use other functions for deferred etc.
    create_info.pSubpasses      = &subpass_description;
    create_info.dependencyCount = 1;
    create_info.pDependencies   = &subpass_dependency;

    VkRenderPass renderpass;
    auto check = vkCreateRenderPass(vk_device, &create_info, ALLOCATION_CALLBACKS, &renderpass);
    DEBUG_OBJ_CREATION(vkCreateRenderPass, check);

    VkImageView *attachment_views =
        (VkImageView*)malloc_t(
            sizeof(VkImageView) * attachment_description_count, 8);

    int attachment_index = 0;
    if (!info->no_depth_attachment) {
        attachment_views[0] = *info->depth_view;
        attachment_index++;
    }

    memcpy(attachment_views + attachment_index,
           info->color_views,
           sizeof(VkImageView) * info->color_attachment_count);
    attachment_index += info->color_attachment_count;

    if (info->resolve_flags) {
        memcpy(attachment_views + attachment_index,
               info->resolve_views,
               sizeof(VkImageView) * info->color_attachment_count);
        attachment_index += info->color_attachment_count;
    }

    if (info->input_attachment_count) {
        memcpy(attachment_views + attachment_index,
               info->input_views,
               sizeof(VkImageView) * info->input_attachment_count);
        attachment_index += info->input_attachment_count;
    }

    VkFramebufferCreateInfo framebuffer_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebuffer_info.renderPass         = renderpass;
    framebuffer_info.attachmentCount    = attachment_description_count;
    framebuffer_info.pAttachments       = attachment_views;
    framebuffer_info.width              = color_info->imageExtent.width;
    framebuffer_info.height             = color_info->imageExtent.height;
    framebuffer_info.layers             = 1;

    ASSERT(attachment_description_count == attachment_index, "");

    VkFramebuffer framebuffer;
    check = vkCreateFramebuffer(vk_device, &framebuffer_info, ALLOCATION_CALLBACKS, &framebuffer);
    DEBUG_OBJ_CREATION(vkCreateFramebuffer, check);

    Gpu_Renderpass_Framebuffer ret = {};
    ret.renderpass   = renderpass;
    ret.framebuffer  = framebuffer;
    ret.clear_count  = attachment_description_count;
    ret.clear_values = clear_values;

    return ret;
}

void gpu_destroy_renderpass_framebuffer(
    VkDevice vk_device, Gpu_Renderpass_Framebuffer *renderpass_framebuffer) {
    vkDestroyRenderPass(vk_device, renderpass_framebuffer->renderpass, ALLOCATION_CALLBACKS);
    vkDestroyFramebuffer(vk_device, renderpass_framebuffer->framebuffer, ALLOCATION_CALLBACKS);

    free_h(renderpass_framebuffer->clear_values);
}

void gpu_cmd_primary_begin_renderpass(VkCommandBuffer command_buffer, Gpu_Renderpass_Begin_Info *info) {
    VkRenderPassBeginInfo begin_info = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin_info.renderPass = info->renderpass_framebuffer->renderpass;
    begin_info.framebuffer = info->renderpass_framebuffer->framebuffer;
    begin_info.clearValueCount = info->renderpass_framebuffer->clear_count;
    begin_info.pClearValues = info->renderpass_framebuffer->clear_values;

    VkRect2D rect;
    if (info->render_area) {
        begin_info.renderArea = *info->render_area;
    } else {
        rect.offset = {0, 0};
        rect.extent =  get_window_instance()->info.imageExtent;
        begin_info.renderArea = rect;
    }

    vkCmdBeginRenderPass(command_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

/* End Better Automate Rendering */

VkAttachmentDescription gpu_get_attachment_description(Gpu_Attachment_Description_Info *info) {
    VkAttachmentDescription description = {};
    switch(info->setting) {
    case GPU_ATTACHMENT_DESCRIPTION_SETTING_DEPTH_LOAD_UNDEFINED_STORE:
        description.format         = info->format;
        description.samples        = VK_SAMPLE_COUNT_1_BIT;
        description.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        description.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        description.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        description.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        description.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        break;
    case GPU_ATTACHMENT_DESCRIPTION_SETTING_DEPTH_LOAD_OPTIMAL_STORE:
        description.format         = info->format;
        description.samples        = VK_SAMPLE_COUNT_1_BIT;
        description.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        description.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        description.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        description.initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        description.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        break;
    case GPU_ATTACHMENT_DESCRIPTION_SETTING_COLOR_LOAD_UNDEFINED_STORE:
        description.format         = info->format;
        description.samples        = VK_SAMPLE_COUNT_1_BIT;
        description.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        description.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        description.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        description.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        description.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        break;
    case GPU_ATTACHMENT_DESCRIPTION_SETTING_COLOR_LOAD_OPTIMAL_STORE:
        description.format         = info->format;
        description.samples        = VK_SAMPLE_COUNT_1_BIT;
        description.loadOp         = VK_ATTACHMENT_LOAD_OP_LOAD;
        description.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        description.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        description.initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        description.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        break;
    }
    return description;
}

VkSubpassDescription create_vk_graphics_subpass_description(Create_Vk_Subpass_Description_Info *info) {
    VkSubpassDescription description    = {};
    description.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    description.inputAttachmentCount    = info->input_attachment_count;
    description.colorAttachmentCount    = info->color_attachment_count;
    description.pInputAttachments       = info->input_attachments;
    description.pColorAttachments       = info->color_attachments;
    description.pResolveAttachments     = info->resolve_attachments;
    description.pDepthStencilAttachment = info->depth_stencil_attachment;

    // @Todo preserve attachments
    return description;
}

VkSubpassDependency create_vk_subpass_dependency(Create_Vk_Subpass_Dependency_Info *info) {
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = info->src_subpass;
    dependency.dstSubpass = info->dst_subpass;

    switch(info->access_rules) {
    // @Todo These mainly come from the vulkan sync wiki. The todo part is to implement more of them
    case GPU_SUBPASS_DEPENDENCY_SETTING_ACQUIRE_TO_RENDER_TARGET_BASIC:
        // This one is just super basic for now...
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        break;
    case GPU_SUBPASS_DEPENDENCY_SETTING_COLOR_DEPTH_BASIC_DRAW:
        dependency.srcStageMask  =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask  =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        break;
    case GPU_SUBPASS_DEPENDENCY_SETTING_WRITE_READ_COLOR_FRAGMENT:
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
        dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        break;
    case GPU_SUBPASS_DEPENDENCY_SETTING_WRITE_READ_DEPTH_FRAGMENT:
        dependency.srcStageMask  =
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT_KHR |
            VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT_KHR;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        break;
    }
    return dependency;
}

VkRenderPass create_vk_renderpass(VkDevice vk_device, Create_Vk_Renderpass_Info *info) {
    VkRenderPassCreateInfo create_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    create_info.attachmentCount        = info->attachment_count;
    create_info.pAttachments           = info->attachments;
    create_info.subpassCount           = info->subpass_count;
    create_info.pSubpasses             = info->subpasses;
    create_info.dependencyCount        = info->dependency_count;
    create_info.pDependencies          = info->dependencies;
    VkRenderPass renderpass;
    auto check = vkCreateRenderPass(vk_device, &create_info, ALLOCATION_CALLBACKS, &renderpass);
    DEBUG_OBJ_CREATION(vkCreateRenderPass, check);
    return renderpass;
}
void destroy_vk_renderpass(VkDevice vk_device, VkRenderPass renderpass) {
    vkDestroyRenderPass(vk_device, renderpass, ALLOCATION_CALLBACKS);
}

VkFramebuffer gpu_create_framebuffer(VkDevice vk_device, Gpu_Framebuffer_Info *info) {
    VkFramebufferCreateInfo create_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    create_info.renderPass      = info->renderpass;
    create_info.attachmentCount = info->attachment_count;
    create_info.pAttachments    = info->attachments;
    create_info.width           = info->width;
    create_info.height          = info->height;
    create_info.layers          = 1;
    VkFramebuffer framebuffer;
    auto check = vkCreateFramebuffer(vk_device, &create_info, ALLOCATION_CALLBACKS, &framebuffer);
    DEBUG_OBJ_CREATION(vkCreateFramebuffer, check);
    return framebuffer;
}
void gpu_destroy_framebuffer(VkDevice vk_device, VkFramebuffer framebuffer) {
    vkDestroyFramebuffer(vk_device, framebuffer, ALLOCATION_CALLBACKS);
}

/* `Memory Resources */

// `Gpu Buf Allocator
Gpu_Buf_Allocator
gpu_get_buf_allocator(VkBuffer buffer, void *ptr, u64 size, u32 count)
{
    Gpu_Buf_Allocator ret = {};
    ret.alloc_cap = count;

    VkPhysicalDeviceLimits *lim = &get_gpu_instance()->info.properties.limits;
    // @Note Assumes that this is great enough alignment for any type of allocator.
    ret.alignment = lim->nonCoherentAtomSize;
    ret.cap = align(size, ret.alignment);
    ret.buf = buffer;
    ret.ptr = ptr;

    return ret;
}
void gpu_reset_buf_allocator(VkDevice device, Gpu_Buf_Allocator *allocator)
{
    allocator->alloc_cnt = 0;
    allocator->used = 0;
}
void* gpu_make_buf_allocation(Gpu_Buf_Allocator *allocator, u64 size, u64 *ret_offset)
{
    allocator->used = align(allocator->used, allocator->alignment);
    u64 offset = allocator->used;

    allocator->alloc_cnt++;
    ASSERT(allocator->alloc_cnt <= allocator->alloc_cap, "Gpu Buf Overflow");

    allocator->used += align(size, allocator->alignment);
    ASSERT(allocator->used <= allocator->cap, "Gpu Buf Overflow");

    if (ret_offset)
        *ret_offset = offset;

    return (void*)((u8*)allocator->ptr + offset);
}
VkCopyBufferInfo2 gpu_buf_allocator_setup_copy(
    Gpu_Buf_Allocator *to_allocator,
    Gpu_Buf_Allocator *from_allocator,
    u64 src_offset, u64 size)
{
    VkBufferCopy2 *copy = (VkBufferCopy2*)malloc_t(sizeof(VkBufferCopy2), 8);
    *copy = {VK_STRUCTURE_TYPE_BUFFER_COPY_2};
    copy->srcOffset    = src_offset;
    copy->dstOffset    = to_allocator->used;
    copy->size         = size;

    gpu_make_buf_allocation(to_allocator, size, NULL);

    VkCopyBufferInfo2 ret = {VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2};
    ret.srcBuffer = from_allocator->buf;
    ret.dstBuffer = to_allocator->buf;
    ret.regionCount = 1;
    ret.pRegions    = copy;

    return ret;
}

// `Gpu Tex Allocator
Gpu_Tex_Allocator gpu_create_tex_allocator(VkDeviceMemory img_mem, VkBuffer stage, void *mapped_ptr, u64 byte_cap, u32 img_cap)
{
    Gpu_Tex_Allocator alloc = {};
    alloc.img_cap = img_cap;
    alloc.cap = byte_cap;
    alloc.stage = stage;
    alloc.mem = img_mem;
    alloc.ptr = mapped_ptr;

    // @Todo add row pitch alignment, not just the host buffer alignment offset
    alloc.alignment = get_gpu_instance()->info.properties.limits.optimalBufferCopyOffsetAlignment;

    alloc.imgs = (VkImage*)malloc_h(sizeof(VkImage) * img_cap, 8);
    alloc.offsets = (u64*)malloc_h(sizeof(u64) * img_cap, 8);

    return alloc;
}
void gpu_destroy_tex_allocator(Gpu_Tex_Allocator *alloc)
{
    free_h(alloc->imgs);
}
void* gpu_make_tex_allocation(VkDevice device, Gpu_Tex_Allocator *alloc, u32 width, u32 height, VkImage *image)
{
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType     = VK_IMAGE_TYPE_2D;
    info.format        = VK_FORMAT_R8G8B8A8_SRGB;
    info.extent        = {.width = width, .height = height, .depth = 1};
    info.mipLevels     = 1; // @Todo mip mapping
    info.arrayLayers   = 1;
    info.samples       = VK_SAMPLE_COUNT_1_BIT; // @Todo multisampling
    info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    info.usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    auto check = vkCreateImage(device, &info, ALLOCATION_CALLBACKS, image);
    DEBUG_OBJ_CREATION(vkCreateImage, check);

    alloc->buf_used = align(alloc->buf_used, alloc->alignment);
    void *ptr = (u8*)alloc->ptr + alloc->buf_used;
    alloc->offsets[alloc->img_cnt] = alloc->buf_used;

    alloc->buf_used += width * height;
    alloc->imgs[alloc->img_cnt] = *image;
    alloc->img_cnt++;
    // @Todo Handle failure better here, or not? Not because it should never happen
    ASSERT(alloc->img_cnt  <= alloc->img_cap, "Tex Allocator Overflow");
    ASSERT(alloc->buf_used <= alloc->cap, "Tex Allocator Overflow");

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, *image, &req);
    alloc->mem_used = align(alloc->mem_used, req.alignment);

    vkBindImageMemory(device, *image, alloc->mem, alloc->mem_used);
    alloc->mem_used += req.size;
    ASSERT(alloc->mem_used <= alloc->cap, "Tex Allocator Overflow");

    return ptr;
}
void gpu_reset_tex_allocator(Gpu_Tex_Allocator *alloc)
{
    alloc->img_cnt  = 0;
    alloc->buf_used = 0;
    alloc->mem_used = 0;
}

// `Attachments
VkImageView gpu_create_depth_attachment_view(VkDevice vk_device, VkImage vk_image)
{
    VkImageViewCreateInfo create_info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    create_info.image            = vk_image;
    create_info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format           = VK_FORMAT_D16_UNORM;
    create_info.components       = {};
    create_info.subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .baseMipLevel   = 0, .levelCount = 1,
        .baseArrayLayer = 0, .layerCount = 1,
    };
    VkImageView view;
    auto check = vkCreateImageView(vk_device, &create_info, ALLOCATION_CALLBACKS, &view);
    DEBUG_OBJ_CREATION(vkCreateImageView, check);
    return view;
}
void gpu_destroy_image_view(VkDevice vk_device, VkImageView view)
{
    vkDestroyImageView(vk_device, view, ALLOCATION_CALLBACKS);
}

// `Samplers
Gpu_Sampler_Storage gpu_create_sampler_storage(int size)
{
    Gpu_Sampler_Storage storage;
    storage.cap = size;
    storage.stored = 0;
    storage.samplers = (VkSampler*)malloc_h(sizeof(VkSampler) * size, 8);
    return storage;
}
void gpu_free_sampler_storage(Gpu_Sampler_Storage *storage)
{
    storage->cap = 0;
    for(u32 i = 0; i < storage->stored; ++i)
        vkDestroySampler(storage->device, storage->samplers[i], ALLOCATION_CALLBACKS);
    free_h(storage->samplers);
    storage->stored = 0;
}
VkSampler* gpu_create_samplers(
        Gpu_Sampler_Storage *storage, int count, Gpu_Sampler_Info *infos)
{
    ASSERT(storage->stored + count <= storage->cap, "Sampler Storage Overflow");

    VkSamplerCreateInfo create_info;
    for(u32 i = 0; i < count; ++i) {
        create_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

        switch(infos->filter_address_setting) {
        case GPU_SAMPLER_SETTING_LINEAR_REPEAT:
            create_info.magFilter = VK_FILTER_LINEAR;
            create_info.minFilter = VK_FILTER_LINEAR;
            create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            break;
        default:
            ASSERT(false, "Invalid Sampler Setting");
        } // filter address switch

        switch(infos->mip_map_setting) {
        case GPU_SAMPLER_SETTING_MIP_LINEAR_ZERO:
            create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            create_info.mipLodBias = 0.0f;
            create_info.minLod = 0.0f;
            create_info.maxLod = 0.0f;
            break;
        default:
            break;
        } // mip map switch

        auto check =
            vkCreateSampler(storage->device,
                &create_info,
                ALLOCATION_CALLBACKS,
                &storage->samplers[storage->stored]);
            storage->stored++;
        DEBUG_OBJ_CREATION(vkCreateSampler, check);
    }
    return storage->samplers + (storage->stored - count);
}

#if DEBUG
VkDebugUtilsMessengerEXT create_debug_messenger(Create_Vk_Debug_Messenger_Info *info) {
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = fill_vk_debug_messenger_info(info);

    VkDebugUtilsMessengerEXT debug_messenger;
    auto check = vkCreateDebugUtilsMessengerEXT(info->vk_instance, &debug_messenger_create_info, NULL, &debug_messenger);

    DEBUG_OBJ_CREATION(vkCreateDebugUtilsMessengerEXT, check)
    return debug_messenger;
}

VkResult vkCreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}
void vkDestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT messenger,
        const VkAllocationCallbacks *pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
        return func(instance, messenger, pAllocator);
}
#endif
