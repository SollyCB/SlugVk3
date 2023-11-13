#include "gpu.hpp"
#include "vulkan/vulkan_core.h"
#include "vulkan_errors.hpp"
#include "glfw.hpp"
#include "spirv.hpp"
#include "file.hpp"
#include "builtin_wrappers.h"
#include "gltf.hpp"
#include "image.hpp"
#include "simd.hpp"

namespace gpu {

#if DEBUG
static VkDebugUtilsMessengerEXT s_debug_messenger;
VkDebugUtilsMessengerEXT* get_debug_messenger_instance() { return &s_debug_messenger; }
#endif

//
// @PipelineAllocation <- old note
// Best idea really is to calculate how big all the unchanging state settings are upfront, then make one
// allocation at load time large enough to hold everything, and just allocate everything into that.
// Just need to find a good way to count all this size...
//

static Gpu s_Gpu;
Gpu* get_gpu_instance() { return &s_Gpu; }

static VkFormat COLOR_ATTACHMENT_FORMAT;

void init_gpu()
{
    // keep struct data together (not pointing to random heap addresses)

    Gpu *gpu = get_gpu_instance();

    Create_Instance_Info create_instance_info = {};
    gpu->instance = create_instance(&create_instance_info);

#if DEBUG
    Create_Debug_Messenger_Info create_debug_messenger_info = {gpu->instance};
    *get_debug_messenger_instance() = create_debug_messenger(&create_debug_messenger_info);
#endif

    // creates queues and fills gpu struct with them
    // features and extensions lists defined in the function body
    gpu->device = create_device(gpu);

    allocate_memory();
}
void kill_gpu(Gpu *gpu) {
    free_memory();

    vkDestroyDevice(gpu->device, ALLOCATION_CALLBACKS);
#if DEBUG
    DestroyDebugUtilsMessengerEXT(gpu->instance, *get_debug_messenger_instance(), ALLOCATION_CALLBACKS);
#endif
    vkDestroyInstance(gpu->instance, ALLOCATION_CALLBACKS);
}

// `Instance
VkInstance create_instance(Create_Instance_Info *info) {
    VkInstanceCreateInfo instance_create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
#if DEBUG
    Create_Debug_Messenger_Info debug_messenger_info = {};
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
VkDevice create_device(Gpu *gpu) { // returns logical device, silently fills in gpu.physical_device

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
    vkEnumeratePhysicalDevices(gpu->instance, &physical_device_count, NULL);
    VkPhysicalDevice *physical_devices =
        (VkPhysicalDevice*)malloc_t(sizeof(VkPhysicalDevice) * physical_device_count, 8);
    vkEnumeratePhysicalDevices(gpu->instance, &physical_device_count, physical_devices);

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
            println("Device Index %u does not support Memory Priority", i);
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
            if (glfwGetPhysicalDevicePresentationSupport(gpu->instance, physical_devices[i], j) &&
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
        gpu->memory.flags |= GPU_MEMORY_DISCRETE_TRANSFER_QUEUE;
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

    VkDevice device;
    auto check = vkCreateDevice(physical_devices[physical_device_index], &device_create_info, ALLOCATION_CALLBACKS, &device);
    DEBUG_OBJ_CREATION(vkCreateDevice, check);

    gpu->phys_device = physical_devices[physical_device_index];
    gpu->graphics_queue_index = graphics_queue_index;
    gpu->present_queue_index  = presentation_queue_index;
    gpu->transfer_queue_index = transfer_queue_index;

    vkGetDeviceQueue(device, graphics_queue_index, 0, &gpu->graphics_queue);

    // if queue indices are equivalent, dont get twice
    if (presentation_queue_index != graphics_queue_index) {
        vkGetDeviceQueue(device, presentation_queue_index, 0, &gpu->present_queue);
    } else {
        gpu->present_queue = gpu->graphics_queue;
    }

    // if queue indices are equivalent, dont get twice
    if (transfer_queue_index != graphics_queue_index) {
        vkGetDeviceQueue(device, transfer_queue_index, 0, &gpu->transfer_queue);
    } else {
        gpu->transfer_queue = gpu->graphics_queue;
    }

    VkPhysicalDeviceProperties physical_device_properties;
    vkGetPhysicalDeviceProperties(gpu->phys_device, &physical_device_properties);
    gpu->info.props = physical_device_properties;

    return device;
} // func create_device()

// `Surface and `Swapchain
static Window s_Window;
Window* get_window_instance() { return &s_Window; }

void init_window(Gpu *gpu, glfw::Glfw *glfw) {
    Window *window = get_window_instance();
    *window = {};

    VkSurfaceKHR surface = create_surface(gpu->instance, glfw);
    VkSwapchainKHR swapchain = create_swapchain(gpu, surface);
    window->swapchain = swapchain;
}
void kill_window(Gpu *gpu, Window *window) {
    destroy_swapchain(gpu->device, window);
    destroy_surface(gpu->instance, window->info.surface);
    free_h(window->images);
}

VkSurfaceKHR create_surface(VkInstance vk_instance, glfw::Glfw *glfw) {
    VkSurfaceKHR surface;
    auto check = glfwCreateWindowSurface(vk_instance, glfw->window, NULL, &surface);

    DEBUG_OBJ_CREATION(glfwCreateWindowSurface, check);
    return surface;
}
void destroy_surface(VkInstance instance, VkSurfaceKHR surface) {
    vkDestroySurfaceKHR(instance, surface, ALLOCATION_CALLBACKS);
}

VkSwapchainKHR recreate_swapchain(Gpu *gpu, Window *window) {
    for(uint i = 0; i < window->image_count; ++i)
        vkDestroyImageView(gpu->device, window->views[i], ALLOCATION_CALLBACKS);

    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu->phys_device, window->info.surface, &surface_capabilities);

    window->info.imageExtent = surface_capabilities.currentExtent;
    window->info.preTransform = surface_capabilities.currentTransform;

    //
    // This might error with some stuff in the createinfo not properly define,
    // I made the refactor while sleepy!
    //
    auto check = vkCreateSwapchainKHR(gpu->device, &window->info, ALLOCATION_CALLBACKS, &window->swapchain);

    DEBUG_OBJ_CREATION(vkCreateSwapchainKHR, check);
    window->info.oldSwapchain = window->swapchain;

    // Image setup
    auto img_check = vkGetSwapchainImagesKHR(gpu->device, window->swapchain, &window->image_count, window->images);
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

    for(u32 i = 0; i < window->image_count; ++i) {
        view_info.image = window->images[i];
        check = vkCreateImageView(gpu->device, &view_info, ALLOCATION_CALLBACKS, &window->views[i]);
        DEBUG_OBJ_CREATION(vkCreateImageView, check);
    }

    return window->swapchain;
}

VkSwapchainKHR create_swapchain(Gpu *gpu, VkSurfaceKHR surface) {
    Window *window = get_window_instance();
    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu->phys_device, surface, &surface_capabilities);

    VkSwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.surface = surface;
    swapchain_info.imageExtent = surface_capabilities.currentExtent;
    swapchain_info.preTransform = surface_capabilities.currentTransform;

    u32 format_count;
    VkSurfaceFormatKHR *formats;
    u32 present_mode_count;
    VkPresentModeKHR *present_modes;

    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu->phys_device, swapchain_info.surface, &format_count, NULL);
    formats = (VkSurfaceFormatKHR*)malloc_t(sizeof(VkSurfaceFormatKHR) * format_count, 8);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu->phys_device, swapchain_info.surface, &format_count, formats);

    swapchain_info.imageFormat = formats[0].format;
    COLOR_ATTACHMENT_FORMAT = swapchain_info.imageFormat;
    swapchain_info.imageColorSpace = formats[0].colorSpace;

    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu->phys_device, swapchain_info.surface, &present_mode_count, NULL);
    present_modes = (VkPresentModeKHR*)malloc_t(sizeof(VkPresentModeKHR) * present_mode_count, 8);
    vkGetPhysicalDeviceSurfacePresentModesKHR(gpu->phys_device, swapchain_info.surface, &present_mode_count, present_modes);

    for(int i = 0; i < present_mode_count; ++i) {
        if (present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            // @Todo immediate presentation
            println("Mailbox Presentation Supported, but using FIFO (@Todo)...");
    }
    swapchain_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;

    swapchain_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;

    window->image_count = surface_capabilities.minImageCount < 2 ? 2 : surface_capabilities.minImageCount;

    swapchain_info.minImageCount = window->image_count;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.clipped = VK_TRUE;

    swapchain_info.queueFamilyIndexCount = 1;
    swapchain_info.pQueueFamilyIndices = &gpu->present_queue_index;

    VkSwapchainKHR swapchain;
    auto check = vkCreateSwapchainKHR(gpu->device, &swapchain_info, ALLOCATION_CALLBACKS, &swapchain);
    DEBUG_OBJ_CREATION(vkCreateSwapchainKHR, check);

    // Image setup
    u32 image_count = surface_capabilities.minImageCount < 2 ? 2 : surface_capabilities.minImageCount;

    // Is this better than just continuing to use s_Window? who cares...
    window->swapchain = swapchain;
    window->info = swapchain_info;
    window->info.oldSwapchain = swapchain;

    window->images = (VkImage*)malloc_h(sizeof(VkImage) * window->image_count * 2, 8);
    window->views = (VkImageView*)(window->images + window->image_count);

    u32 image_count_check;
    vkGetSwapchainImagesKHR(gpu->device, window->swapchain, &image_count_check, NULL);
    ASSERT(image_count_check == image_count, "Incorrect return value from GetSwapchainImages");

    auto check_swapchain_img_count =
        vkGetSwapchainImagesKHR(gpu->device, window->swapchain, &image_count, window->images);
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

    for(u32 i = 0; i < window->image_count; ++i) {
        view_info.image = window->images[i];
        check = vkCreateImageView(gpu->device, &view_info, ALLOCATION_CALLBACKS, &window->views[i]);
        DEBUG_OBJ_CREATION(vkCreateImageView, check);
    }

    return swapchain;
}
void destroy_swapchain(VkDevice device, Window *window) {
    for(uint i = 0; i < window->image_count; ++i)
        vkDestroyImageView(device, window->views[i], ALLOCATION_CALLBACKS);

    vkDestroySwapchainKHR(device, window->swapchain, ALLOCATION_CALLBACKS);
}

// `Memory
static void integrated_gpu_get_memory_type(
    VkPhysicalDeviceMemoryProperties *props,
    u32 largest_heap_index_device,
    u32 largest_heap_index_host,
    u32 *device_mem_type,
    u32 *host_mem_type,
    u32 *final_heap_device,
    u32 *final_heap_host)
{
    for(int i = 0; i < props->memoryTypeCount; ++i)
        if (props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT &&
            props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT &&
            props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        {
            if (props->memoryTypes[i].heapIndex == largest_heap_index_device) {
                *device_mem_type = i;
                break;
            } else {
                *device_mem_type = i;
            }
        }

    for(int i = 0; i < props->memoryTypeCount; ++i)
        if ((props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0 &&
             props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT       &&
             props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        {
            if (props->memoryTypes[i].heapIndex == largest_heap_index_host) {
                *host_mem_type = i;
                break;
            } else {
                *host_mem_type = i;
            }
        }
}
static void discrete_gpu_get_memory_type(
    VkPhysicalDeviceMemoryProperties *props,
    u32 largest_heap_index_device,
    u32 largest_heap_index_host,
    u32 *device_mem_type,
    u32 *host_mem_type,
    u32 *device_host_mem_type,
    u32 *final_heap_device,
    u32 *final_heap_host,
    u32 *final_heap_both)
{
    for(int i = 0; i < props->memoryTypeCount; ++i)
        if (props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            if (props->memoryTypes[i].heapIndex == largest_heap_index_device) {
                *device_mem_type = i;
                *final_heap_device = props->memoryTypes[i].heapIndex;
                break;
            } else {
                *device_mem_type = i;
                *final_heap_device = props->memoryTypes[i].heapIndex;
            }
        }

    for(int i = 0; i < props->memoryTypeCount; ++i)
        if ((props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == 0 &&
             props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT       &&
             props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        {
            if (props->memoryTypes[i].heapIndex == largest_heap_index_host) {
                *host_mem_type = i;
                *final_heap_host = props->memoryTypes[i].heapIndex;
                break;
            } else {
                *host_mem_type = i;
                *final_heap_host = props->memoryTypes[i].heapIndex;
            }
        }
    for(int i = 0; i < props->memoryTypeCount; ++i)
        if (props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT &&
            props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT &&
            props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        {
            // probably a pointless 'or' due to the logic in the functions before this,
            // but I dont have the brain space to check rn...
            if (props->memoryTypes[i].heapIndex == largest_heap_index_host ||
                props->memoryTypes[i].heapIndex == largest_heap_index_device)
            {
                *device_host_mem_type = i;
                *final_heap_both = props->memoryTypes[i].heapIndex;
                break;
            } else {
                *device_host_mem_type = i;
                *final_heap_both = props->memoryTypes[i].heapIndex;
            }
        }
}
void create_attachments(Gpu *gpu, VkDevice device, u32 device_mem_type) {
    VkResult check;
    VkMemoryRequirements mem_req;
    VkMemoryDedicatedAllocateInfo dedicate_info = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    VkMemoryPriorityAllocateInfoEXT priority_info = {VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT};

    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.pNext = &priority_info;

    VkImageCreateInfo attachment_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    attachment_info.imageType = VK_IMAGE_TYPE_2D;
    attachment_info.extent = {.width = 1920, .height = 1080, .depth = 1};
    attachment_info.mipLevels = 1;
    attachment_info.arrayLayers = 1;
    attachment_info.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    attachment_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    for(u32 i = 0; i < COLOR_ATTACHMENT_COUNT; ++i) {
        attachment_info.format = VK_FORMAT_R8G8B8A8_SRGB;
        attachment_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        check = vkCreateImage(device, &attachment_info, ALLOCATION_CALLBACKS, &gpu->memory.color_attachments[i]);
        DEBUG_OBJ_CREATION(vkCreateImage, check);

        vkGetImageMemoryRequirements(device, gpu->memory.color_attachments[i], &mem_req);

        dedicate_info.image = gpu->memory.color_attachments[i];

        priority_info.priority = 1.0;
        priority_info.pNext = &dedicate_info;

        allocate_info.allocationSize = mem_req.size;
        allocate_info.memoryTypeIndex = device_mem_type;

        check = vkAllocateMemory(device, &allocate_info, ALLOCATION_CALLBACKS, &gpu->memory.color_mem[i]);
        DEBUG_OBJ_CREATION(vkAllocateMemory, check);

        vkBindImageMemory(device, gpu->memory.color_attachments[i], gpu->memory.color_mem[i], 0);
    }
    for(u32 i = 0; i < DEPTH_ATTACHMENT_COUNT; ++i) {
        attachment_info.format = VK_FORMAT_D16_UNORM;
        attachment_info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        check = vkCreateImage(device, &attachment_info, ALLOCATION_CALLBACKS, &gpu->memory.depth_attachments[i]);
        DEBUG_OBJ_CREATION(vkCreateImage, check);

        vkGetImageMemoryRequirements(device, gpu->memory.depth_attachments[i], &mem_req);

        dedicate_info.image = gpu->memory.depth_attachments[i];

        priority_info.priority = 1.0;
        priority_info.pNext = &dedicate_info;

        allocate_info.allocationSize = mem_req.size;
        allocate_info.memoryTypeIndex = device_mem_type;

        check = vkAllocateMemory(device, &allocate_info, ALLOCATION_CALLBACKS, &gpu->memory.depth_mem[i]);
        DEBUG_OBJ_CREATION(vkAllocateMemory, check);

        vkBindImageMemory(device, gpu->memory.depth_attachments[i], gpu->memory.depth_mem[i], 0);
    }
}
void create_buffers(Gpu *gpu, u32 mem_type, u32 count, VkBuffer *bufs, VkDeviceMemory *mems, void **ptrs, u64 size,
                    VkBufferUsageFlags usage, float priority, bool map) {

    VkResult check;
    VkBufferCreateInfo buf_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    buf_info.size = align(size, gpu->info.props.limits.nonCoherentAtomSize);
    buf_info.usage = usage;
    for(u32 i = 0; i < count; ++i) {
        check = vkCreateBuffer(gpu->device, &buf_info, ALLOCATION_CALLBACKS, &bufs[i]);
        DEBUG_OBJ_CREATION(vkCreateBuffer, check);
    }

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(gpu->device, bufs[0], &mem_req);

    // Dedicated allocations because these are large linear allocators
    VkMemoryDedicatedAllocateInfo dedicate_info = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};

    VkMemoryPriorityAllocateInfoEXT priority_info = {VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT};
    priority_info.pNext = &dedicate_info;
    priority_info.priority = priority;

    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.pNext = &priority_info;
    allocate_info.allocationSize = mem_req.size;
    allocate_info.memoryTypeIndex = mem_type;
    for(u32 i = 0; i < count; ++i) {
        check = vkAllocateMemory(gpu->device, &allocate_info, ALLOCATION_CALLBACKS, &mems[i]);
        DEBUG_OBJ_CREATION(vkAllocateMemory, check);

        vkBindBufferMemory(gpu->device, bufs[i], mems[i], 0);

        if (map)
            vkMapMemory(gpu->device, mems[i], 0, VK_WHOLE_SIZE, 0x0, &ptrs[i]);
    }
}
void create_mem(Gpu *gpu, u32 mem_type, u32 count, u64 size, VkDeviceMemory *mem, float priority) {
    VkMemoryDedicatedAllocateInfo dedicate_info = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};

    VkMemoryPriorityAllocateInfoEXT priority_info = {VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT};
    priority_info.pNext = &dedicate_info;
    priority_info.priority = priority;

    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.pNext = &priority_info;
    allocate_info.allocationSize = size;
    allocate_info.memoryTypeIndex = mem_type;

    VkResult check;
    for(u32 i = 0; i < count; ++i) {
        check = vkAllocateMemory(gpu->device, &allocate_info, ALLOCATION_CALLBACKS, &mem[i]);
        DEBUG_OBJ_CREATION(vkAllocateMemory, check);
    }
}
void setup_memory_integrated(u32 device_mem_type, u32 host_mem_type) {
    Gpu *gpu = get_gpu_instance();
    VkDevice device = gpu->device;

    /*
        Staging buffers which do not require upload (index, vertex, uniform) use device memory.
        Others use host mem (textures).
    */

    gpu->memory.vertex_mem_device = NULL;
    gpu->memory.index_mem_device = NULL;

    create_attachments(gpu, device, device_mem_type);

    // No Upload Buffers
    create_buffers(gpu,
        device_mem_type,
        VERTEX_STAGE_COUNT,
        gpu->memory.vertex_bufs_stage,
        gpu->memory.vertex_mem_stage,
        gpu->memory.vert_ptrs,
        VERTEX_STAGE_SIZE,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        0.4,
        true);
    create_buffers(gpu,
        device_mem_type,
        INDEX_STAGE_COUNT,
        gpu->memory.index_bufs_stage,
        gpu->memory.index_mem_stage,
        gpu->memory.index_ptrs,
        INDEX_STAGE_SIZE,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        0.4,
        true);
    create_buffers(gpu,
        device_mem_type,
        UNIFORM_BUFFER_COUNT,
        gpu->memory.uniform_bufs,
        gpu->memory.uniform_mem,
        gpu->memory.uniform_ptrs,
        UNIFORM_BUFFER_SIZE,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        0,
        true);

    // Other Buffers
    create_buffers(gpu,
        host_mem_type,
        TEXTURE_STAGE_COUNT,
        gpu->memory.texture_bufs_stage,
        gpu->memory.texture_mem_stage,
        gpu->memory.tex_ptrs,
        TEXTURE_STAGE_SIZE,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        0,
        true);
    create_mem(gpu, device_mem_type, 1, TEXTURE_DEVICE_SIZE, &gpu->memory.texture_mem_device, 0.6);
}
void setup_memory_discrete(u32 device_mem_type, u32 host_mem_type, u32 both_mem_type) {
    Gpu *gpu = get_gpu_instance();
    VkDevice device = gpu->device;

    create_attachments(gpu, device, device_mem_type);

    // Staging Buffers
    create_buffers(gpu,
        host_mem_type,
        VERTEX_STAGE_COUNT,
        gpu->memory.vertex_bufs_stage,
        gpu->memory.vertex_mem_stage,
        gpu->memory.vert_ptrs,
        VERTEX_STAGE_SIZE,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        0.0,
        true);
    create_buffers(gpu,
        host_mem_type,
        INDEX_STAGE_COUNT,
        gpu->memory.index_bufs_stage,
        gpu->memory.index_mem_stage,
        gpu->memory.index_ptrs,
        INDEX_STAGE_SIZE,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        0.0,
        true);
    create_buffers(gpu,
        host_mem_type,
        TEXTURE_STAGE_COUNT,
        gpu->memory.texture_bufs_stage,
        gpu->memory.texture_mem_stage,
        gpu->memory.tex_ptrs,
        TEXTURE_STAGE_SIZE,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        0,
        true);

    // Device Buffers
    create_buffers(gpu,
        device_mem_type,
        1,
        &gpu->memory.vertex_buf_device,
        &gpu->memory.vertex_mem_device,
        NULL,
        VERTEX_STAGE_SIZE,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        0.4,
        false);
    create_buffers(gpu,
        device_mem_type,
        1,
        &gpu->memory.index_buf_device,
        &gpu->memory.index_mem_device,
        NULL,
        INDEX_STAGE_SIZE,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        0.4,
        false);

    // Shared heaps
    create_buffers(gpu,
        both_mem_type,
        UNIFORM_BUFFER_COUNT,
        gpu->memory.uniform_bufs,
        gpu->memory.uniform_mem,
        gpu->memory.uniform_ptrs,
        UNIFORM_BUFFER_SIZE,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        0,
        true);

    // Device Mem
    create_mem(gpu, device_mem_type, 1, TEXTURE_DEVICE_SIZE, &gpu->memory.texture_mem_device, 0.6);
}
void allocate_memory() {
    Gpu *gpu = get_gpu_instance();
    VkPhysicalDevice device = gpu->phys_device;
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(device, &mem_props);

    // Select largest heaps for device and host
    u32 largest_heap_device;
    u32 largest_heap_host;
    u64 heap_size_device = 0;
    u64 heap_size_host = 0;
    for(u32 i = 0; i < mem_props.memoryHeapCount; ++i)
        if (mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            if (mem_props.memoryHeaps[i].size > heap_size_device) {
                heap_size_device = mem_props.memoryHeaps[i].size;
                largest_heap_device = i;
            }
        else
            if (mem_props.memoryHeaps[i].size > heap_size_host) {
                heap_size_host = mem_props.memoryHeaps[i].size;
                largest_heap_host = i;
            }

    // @Unused these final heap indices were intended for allocating proportions of device memory rather than
    // fixed sizes because it is sooo variable and can be adapted to by the allocator implmentations, but I am
    // not going bother with that yet. (The allocators already totally work with this, but I am not implementing
    // actually allocating the proportion way yet. These values are set, but do nothing.)
    u32 final_heap_device;
    u32 final_heap_host;
    u32 final_heap_both;

    u32 host_mem_type;
    u32 device_mem_type;
    u32 both_mem_type;
    if (gpu->info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        gpu->memory.flags |= GPU_MEMORY_UMA;
        integrated_gpu_get_memory_type(
            &mem_props,
            largest_heap_device,
            largest_heap_host,
            &device_mem_type,
            &host_mem_type,
            &final_heap_device,
            &final_heap_host);
        setup_memory_integrated(device_mem_type, host_mem_type);
    }
    else if (gpu->info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        discrete_gpu_get_memory_type( // @Note Assumes there is some shared heap (for uniform buffers)
            &mem_props,
            largest_heap_device,
            largest_heap_host,
            &device_mem_type,
            &host_mem_type,
            &both_mem_type,
            &final_heap_device,
            &final_heap_host,
            &final_heap_both);
        setup_memory_discrete(device_mem_type, host_mem_type, both_mem_type);
    }
}
void free_memory() {
    Gpu *gpu = get_gpu_instance();
    VkDevice device = gpu->device;

    // Attachments
    for(u32 i = 0; i < COLOR_ATTACHMENT_COUNT; ++i) {
        vkDestroyImage(device, gpu->memory.color_attachments[i], ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory.color_mem[i], ALLOCATION_CALLBACKS);
    }
    for(u32 i = 0; i < DEPTH_ATTACHMENT_COUNT; ++i) {
        vkDestroyImage(device, gpu->memory.depth_attachments[i], ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory.depth_mem[i], ALLOCATION_CALLBACKS);
    }

    // Vertex attribute mem
    if (gpu->info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        vkDestroyBuffer(device, gpu->memory.vertex_buf_device, ALLOCATION_CALLBACKS);
        vkDestroyBuffer(device, gpu->memory.index_buf_device, ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory.vertex_mem_device, ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory.index_mem_device, ALLOCATION_CALLBACKS);
    }
    for(u32 i= 0; i < VERTEX_STAGE_COUNT; ++i) {
        vkDestroyBuffer(device, gpu->memory.vertex_bufs_stage[i], ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory.vertex_mem_stage[i], ALLOCATION_CALLBACKS);
    }
    for(u32 i= 0; i < INDEX_STAGE_COUNT; ++i) {
        vkDestroyBuffer(device, gpu->memory.index_bufs_stage[i], ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory.index_mem_stage[i], ALLOCATION_CALLBACKS);
    }

    // Texture mem
    for(u32 i= 0; i < TEXTURE_STAGE_COUNT; ++i) {
        vkDestroyBuffer(device, gpu->memory.texture_bufs_stage[i], ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory.texture_mem_stage[i], ALLOCATION_CALLBACKS);
    }
    vkFreeMemory(device, gpu->memory.texture_mem_device, ALLOCATION_CALLBACKS);

    // Uniform mem
    for(u32 i= 0; i < UNIFORM_BUFFER_COUNT; ++i) {
        vkDestroyBuffer(device, gpu->memory.uniform_bufs[i], ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory.uniform_mem[i], ALLOCATION_CALLBACKS);
    }
}

// `Shaders
Shader_Map create_shader_map(u32 size) {
    Shader_Map ret;
    size = align(size, 16); // align to hashmap group width
    ret.map = HashMap<u64, Shader_Set>::get(size);
    return ret;
}
void destroy_shader_map(Shader_Map *map) {
    auto iter = map->map.iter();
    auto next = iter.next();
    VkDevice device = get_gpu_instance()->device;
    while(next) {
        for(u32 i = 0; i < next->value.shader_count; ++i)
            vkDestroyShaderModule(device, next->value.shaders[i].module, ALLOCATION_CALLBACKS);
        //for(u32 i = 0; i < next->set_count; ++i)
        //    vkDestroyDescriptorSet(device, next->value.sets[i], ALLOCATION_CALLBACKS);
        vkDestroyPipelineLayout(device, next->value.pl_layout, ALLOCATION_CALLBACKS);
        free_h(next->value.shaders);
        free_h(next->value.sets);

        next = iter.next();
    }
    map->map.kill();
}
Set_Allocate_Info insert_shader_set(String *set_name, u32 count, String *files, Shader_Map *map) {
    // Create Shaders
    Shader_Info shader_info = create_shaders(count, files);
    Shader *shaders = shader_info.shaders;

    // Create Layouts
    u32 layout_count;
    Set_Layout_Info *layout_infos = group_spirv(2, shader_info.spirv, &layout_count);

    VkDescriptorSetLayout *layouts = create_set_layouts(layout_count, layout_infos);

    Shader_Set set;
    set.shader_count = count;
    set.set_count = layout_count;
    set.shaders = shaders;
    set.sets = (VkDescriptorSet*)malloc_h(sizeof(VkDescriptorSet) * layout_count, 8);

    // @Todo push constant ranges
    VkPipelineLayoutCreateInfo pl_layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl_layout_info.setLayoutCount = layout_count;
    pl_layout_info.pSetLayouts    = layouts;

    VkDevice device = get_gpu_instance()->device;
    vkCreatePipelineLayout(device, &pl_layout_info, ALLOCATION_CALLBACKS, &set.pl_layout);

    u64 hash = get_string_hash(set_name);
    map->map.insert_hash(hash, &set);

    Set_Allocate_Info ret;
    ret.count = layout_count;
    ret.infos = layout_infos;
    ret.layouts = layouts;
    ret.sets = set.sets;

    return ret;
}

Shader_Set* get_shader_set(String *set_name, Shader_Map *map) {
    u64 hash = get_string_hash(set_name);
    return map->map.find_hash(hash);
}

Shader_Info create_shaders(u32 count, String *strings) {
    Shader *shaders = (Shader*)malloc_h(sizeof(Shader) * count, 8);
    Parsed_Spirv *parsed_spirv = (Parsed_Spirv*)malloc_t(sizeof(Parsed_Spirv) * count, 8);

    u64 size;
    const u32 *spirv;
    VkShaderModuleCreateInfo mod_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    VkDevice device = get_gpu_instance()->device;
    for(u32 i = 0; i < count; ++i) {
        spirv = (const u32*)file_read_char_temp(strings[i].str, &size);
        parsed_spirv[i] = parse_spirv(size, spirv);

        mod_info.codeSize = size;
        mod_info.pCode = spirv;
        vkCreateShaderModule(device, &mod_info, ALLOCATION_CALLBACKS, &shaders[i].module);
        shaders[i].stage = parsed_spirv[i].stage;
    }
    Shader_Info ret;
    ret.count = count;
    ret.shaders = shaders;
    ret.spirv = parsed_spirv;
    return ret;
}
void destroy_shaders(u32 count, Shader *shaders) {
    VkDevice device = get_gpu_instance()->device;
    for(u32 i = 0; i < count; ++i)
        vkDestroyShaderModule(device, shaders[i].module, ALLOCATION_CALLBACKS);
    free_h(shaders);
}

Set_Layout_Info* group_spirv(u32 count, Parsed_Spirv *parsed_spirv, u32 *returned_set_count) {
    // Find total number of descriptor sets
    u64 set_mask = 0x0; // Assume fewer than 64 sets
    for(u32 i = 0; i < count; ++i) {
        for(u32 j = 0; j < parsed_spirv[i].binding_count; ++j)
            set_mask |= 1 << parsed_spirv[i].bindings[j].set;
    }
    u32 set_count = pop_count64(set_mask);

    // Find unique bindings per mask
    u64 *binding_mask;
    u64 *binding_masks = (u64*)malloc_t(sizeof(u64) * set_count, 8);
    memset(binding_masks, 0, sizeof(u64) * set_count);

    for(u32 i = 0; i < count; ++i) {
        for(u32 j = 0; j < parsed_spirv[i].binding_count; ++j)
            binding_masks[parsed_spirv[i].bindings[j].set] |= 1 << parsed_spirv[i].bindings[j].binding;
    }

    // Allocate memory to each bindings array (this could be done as one allocation, and then bind offsets into it,
    // but since this is a linear allocator the difference would be negligible...)
    gpu::Set_Layout_Info *sets =
        (gpu::Set_Layout_Info*)malloc_t(sizeof(gpu::Set_Layout_Info) * set_count, 8);
    u32 total_binding_count = 0;
    for(u32 i = 0; i < set_count; ++i) {
        sets[i].count = pop_count64(binding_masks[i]);
        total_binding_count += sets[i].count;

        sets[i].bindings =
            (VkDescriptorSetLayoutBinding*)malloc_t(
                sizeof(VkDescriptorSetLayoutBinding) * sets[i].count, 8);
    }
    memset(sets->bindings, 0x0, sizeof(VkDescriptorSetLayoutBinding) * total_binding_count);

    Parsed_Spirv *spirv;
    Spirv_Descriptor *descriptor;
    VkDescriptorSetLayoutBinding *binding;

    // merge parsed spirv
    for(u32 i = 0; i < count; ++i) {
         spirv = &parsed_spirv[i];
         for(u32 j = 0; j < spirv->binding_count; ++j) {
            descriptor                 = &spirv->bindings[j];
            binding                    = &sets[descriptor->set].bindings[descriptor->binding];
            binding->binding           = descriptor->binding;
            binding->descriptorCount   = descriptor->count;
            binding->descriptorType    = descriptor->type;
            binding->stageFlags       |= spirv->stage;
         }
    }

    *returned_set_count = set_count;
    return sets;
}

// Descriptors

void count_descriptors(u32 count, Set_Layout_Info *infos, u32 descriptor_counts[11]) {
    u32 index;
    for(int i = 0; i < count; ++i)
        for(int j = 0; j < infos[i].count; ++j)
            descriptor_counts[(u32)infos[i].bindings[j].descriptorType] += infos[i].bindings[j].descriptorCount;
}

VkDescriptorSetLayout* create_set_layouts(u32 count, Set_Layout_Info *infos) {
    VkDescriptorSetLayout *layouts =
        (VkDescriptorSetLayout*)malloc_t(sizeof(VkDescriptorSetLayout) * count, 8);

    VkDevice device = get_gpu_instance()->device;
    VkDescriptorSetLayoutCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    for(u32 i = 0; i < count; ++i) {
        info.bindingCount = infos[i].count;
        info.pBindings = infos[i].bindings;
        vkCreateDescriptorSetLayout(device, &info, ALLOCATION_CALLBACKS, &layouts[i]);
    }
    return layouts;
}
void destroy_set_layouts(u32 count, VkDescriptorSetLayout *layouts) {
    VkDevice device = get_gpu_instance()->device;
    for(u32 i = 0; i < count; ++i)
        vkDestroyDescriptorSetLayout(device, layouts[i], ALLOCATION_CALLBACKS);
}

Descriptor_Allocation create_descriptor_sets(u32 count, Set_Allocate_Info *infos) {
    u32 max_sets = 0;
    u32 counts[11];
    memset(counts, 0, sizeof(u32) * 11);
    for(u32 i = 0; i < count; ++i) {
        max_sets += infos[i].count;
        count_descriptors(infos[i].count, infos[i].infos, counts);
    }

    VkDescriptorPoolSize *pool_sizes = (VkDescriptorPoolSize*)malloc_t(sizeof(VkDescriptorPoolSize) * 11, 8);
    u32 pos = 0;
    for(u32 i = 0; i < 11; ++i) {
        if (counts[i] == 0)
            continue;
        pool_sizes[pos].type = (VkDescriptorType)i;
        pool_sizes[pos].descriptorCount = counts[i];
        pos++;
    }

    VkResult check;
    VkDescriptorPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.maxSets = max_sets;
    pool_info.poolSizeCount = pos;
    pool_info.pPoolSizes = pool_sizes;
    VkDevice device = get_gpu_instance()->device;
    Descriptor_Allocation ret;
    check = vkCreateDescriptorPool(device, &pool_info, ALLOCATION_CALLBACKS, &ret.pool);
    DEBUG_OBJ_CREATION(vkCreateDescriptorPool, check);

    VkDescriptorSetAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc_info.descriptorPool = ret.pool;
    for(u32 i = 0; i < count; ++i) {
        alloc_info.descriptorSetCount = infos[i].count;
        alloc_info.pSetLayouts = infos[i].layouts;
        vkAllocateDescriptorSets(device, &alloc_info, infos[i].sets);
        DEBUG_OBJ_CREATION(vkAllocateDescriptorSets, check);

        for(u32 j = 0; j < infos[i].count; ++j)
            vkDestroyDescriptorSetLayout(device, infos[i].layouts[j], ALLOCATION_CALLBACKS);
    }

    return ret;
}
void destroy_descriptor_sets(Descriptor_Allocation *allocation) {
    VkDevice device = get_gpu_instance()->device;
    vkDestroyDescriptorPool(device, allocation->pool, ALLOCATION_CALLBACKS);
}

// `PipelineLayout
VkPipelineLayout create_pl_layout(VkDevice device, Pl_Layout_Info *info) {
    VkPipelineLayoutCreateInfo create_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    create_info.setLayoutCount         = info->layout_count;
    create_info.pSetLayouts            = info->layouts;
    create_info.pushConstantRangeCount = info->push_constant_count;
    create_info.pPushConstantRanges    = info->push_constants;

    VkPipelineLayout layout;
    auto check = vkCreatePipelineLayout(device, &create_info, ALLOCATION_CALLBACKS, &layout);
    DEBUG_OBJ_CREATION(vkCreatePipelineLayout, check);
    return layout;
}
void destroy_pl_layout(VkDevice device, VkPipelineLayout pl_layout) {
    vkDestroyPipelineLayout(device, pl_layout, ALLOCATION_CALLBACKS);
}

// Pipeline
VkPipelineShaderStageCreateInfo* create_pl_shaders(u32 count, Shader *shaders) {
    VkPipelineShaderStageCreateInfo *ret =
        (VkPipelineShaderStageCreateInfo*)malloc_t(sizeof(VkPipelineShaderStageCreateInfo*) * count, 8);
    for(int i = 0; i < count; ++i) {
        ret[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        ret[i].stage = shaders[i].stage;
        ret[i].module = shaders[i].module;
        ret[i].pName = "main";
        ret[i].pSpecializationInfo = NULL;
    }
    return ret;
}

// `Model Loading
namespace model {
#if 1
u32 get_accessor_byte_stride(Gltf_Accessor_Format accessor_format);

/* @Todo Update these notes.
    New Model Allocator Plan:
        The allocator's device buffer is represented by a large bit mask (an array of u64s)
        where each bit is some memory granularity (each bit could represent a Kb, Mb, etc).
        When a queue is submitted, allocations array is searched for DRAWN states, these ranges
        are then marked as free in the bit mask. Next, the mask is searched for a free range large
        enough to hold the queue (a queue can only be submitted if there is one large enough contiguous
        block i.e. the block holds all allocations, or none.) The memory range that the queue fills
        is then marked as full in the bit mask. The DRAWN allocations are then again checked against
        the bit mask. If a drawn allocations range has moved from free to full, then its memory has
        been overwritten by the most recent queue, therefore it is marked as staged.

        When the buffer copies have been **VK QUEUE SUBMITTED**, the allocation's states are set to UPLOADED.
        Now a new queue is allowed to begin.
*/

        /* Begin Allocator Helper Algorithms */
struct Weight_Args {
    u32 count;
    u8 *weights;
    u8 *states;
    u32 *indices;
    Allocation *allocations;
    u32 idx;
    u8 inc;
    u8 dec;
};
static u32 adjust_allocation_weights(Weight_Args *args) {
    u32 idx = args->indices[args->idx];

    // Find incremented weight
    u8 w = args->weights[idx];
    u8 tmp = Max_u8 + (u8)(w + args->inc > w);
    w = (w + args->inc) | tmp;
    w &= 0b0111'1111;

    args->weights[idx] = Max_u8; // prevent matching itself
    __m128i a;
    __m128i b = _mm_set1_epi8(args->dec);
    __m128i c = _mm_set1_epi8(w);
    __m128i d;
    __m128i e;
    u32 inc = 0;
    u32 pos = Max_u32;
    u32 tmp32;
    u16 mask;
    // decrement all values
    while(inc < args->count) {
        a = _mm_load_si128((__m128i*)(args->weights + inc));
        d = _mm_cmpgt_epi8(a, b);
        e = _mm_and_si128(b, d);
        a = _mm_sub_epi8(a, e);
        a = _mm_and_si128(a, d);
        _mm_store_si128((__m128i*)(args->weights + inc), a);

        d = _mm_cmplt_epi8(a, c);
        mask = _mm_movemask_epi8(d);
        tmp32 = 0 - (u32)(pop_count16(mask) > 0 && pos == Max_u32); // deliberate wrap
        pos -= pos & tmp32;
        pos += (count_trailing_zeros_u16(mask) + inc) & tmp32;

        inc += 16;
    }

    Allocation allocation = args->allocations[idx];
    u8 state = args->states[idx];
    for(u32 i = idx; i > pos; --i) {
        args->weights[i] = args->weights[i-1];
        args->allocations[i] = args->allocations[i-1];
        args->states[i] = args->states[i-1];
    }
    args->weights[pos] = w;
    args->allocations[pos] = allocation;
    args->states[pos] = state;

    b = _mm_set1_epi32(pos - 1);
    c = _mm_set1_epi32(idx);
    inc = 0;
    while(inc < args->count) {
        a = _mm_load_si128((__m128i*)(args->indices + inc));
        d = _mm_cmpgt_epi32(a, b);
        e = _mm_cmplt_epi32(a, c);
        d = _mm_and_si128(d, e);
        e = _mm_set1_epi32(0x01);
        d = _mm_and_si128(d, e);
        a = _mm_add_epi32(a, d);
        _mm_store_si128((__m128i*)(args->indices + inc), a);
        inc += 4;
    }
    args->indices[args->idx] = pos;
    return pos;
}
u32 eject_repeat_indices(u32 count, u32 *indices) {
    __m128i a;
    __m128i b;
    u16 mask;
    u32 inc = 0;
    u32 dec = 0;
    u32 tmp;
    u32 idx;
    u16 mask_mask = 0b0001000100010001;
    u32 adj_count = align(count, 4);
    u32 move;
    u32 mov_t;
    u32 mov_f;
    for(u32 i = 0; i < count - dec; ++i) {
        inc = 4 * (i >> 2);
        b = _mm_set1_epi32(indices[i]);
        a = _mm_load_si128((__m128i*)(indices + inc));
        a = _mm_cmpeq_epi32(a, b);
        mask = _mm_movemask_epi8(a);
        mask &= mask_mask;
        mask ^= 1 << (4 * (i & 3)); // clear self match
        mask &= 0xffff >> (((4 - ((count - dec) & 3)) * ((u32)(count - dec < inc + 4))) << 2); // clear overflow matches beyond count

        tmp = (u32)(pop_count16(mask) >= 1);
        dec += tmp;
        idx = inc + (count_trailing_zeros_u16(mask) >> 2);
        idx *= tmp;

        // shuffle everything backwards if a dupe was found
        move = (u32)(inc >= idx) & tmp;
        mov_t = inc * move;
        mov_f = max_clamp32((count - dec) - 1, inc + 1) * move;
        indices[mov_t] = indices[mov_f];

        move = (u32)(inc + 1 >= idx) & tmp;
        mov_t = (inc + 1) * move;
        mov_f = max_clamp32(count - 1, inc + 2) * move;
        indices[mov_t] = indices[mov_f];

        move = (u32)(inc + 2 >= idx) & tmp;
        mov_t = (inc + 2) * move;
        mov_f = max_clamp32(count - 1, inc + 3) * move;
        indices[mov_t] = indices[mov_f];

        move = (u32)(inc + 3 >= idx) & tmp;
        mov_t = (inc + 3) * move;
        mov_f = max_clamp32(count - 1, inc + 4) * move;
        indices[mov_t] = indices[mov_f];

        inc += 4;
        while(inc + 4 < count - dec) { // do not check into potential overflow range in loop
            a = _mm_load_si128((__m128i*)(indices + inc));
            a = _mm_cmpeq_epi32(a, b);
            mask = _mm_movemask_epi8(a);
            mask &= mask_mask;

            tmp = (u32)(pop_count16(mask) >= 1);
            dec += tmp;
            idx = inc + (count_trailing_zeros_u16(mask) >> 2);
            idx *= tmp;

            move |= (u32)(inc >= idx) & tmp;
            mov_t = inc * move;
            mov_f = max_clamp32(count - 1, inc + 1) * move;
            indices[mov_t] = indices[mov_f];

            move |= (u32)(inc + 1 >= idx) & tmp;
            mov_t = (inc + 1) * move;
            mov_f = max_clamp32(count - 1, inc + 2) * move;
            indices[mov_t] = indices[mov_f];

            move |= (u32)(inc + 2 >= idx) & tmp;
            mov_t = (inc + 2) * move;
            mov_f = max_clamp32(count - 1, inc + 3) * move;
            indices[mov_t] = indices[mov_f];

            move |= (u32)(inc + 3 >= idx) & tmp;
            mov_t = (inc + 3) * move;
            mov_f = max_clamp32(count - 1, inc + 4) * move;
            indices[mov_t] = indices[mov_f];

            inc += 4;
        }
        // deal with potential overflow outside the loop
        // (avoids having to check loop iteration relative to inc)
        a = _mm_load_si128((__m128i*)(indices + inc));
        a = _mm_cmpeq_epi32(a, b);
        mask = _mm_movemask_epi8(a);
        mask &= mask_mask * ((u32)(inc < count - dec));
        mask &= 0xffff >> (((4 - ((count - dec) & 3)) << 2) * ((count - dec) & 3));

        tmp = (u32)(pop_count16(mask) >= 1);
        dec += tmp;
        idx = inc + (count_trailing_zeros_u16(mask) >> 2);
        idx *= tmp;

        move |= (u32)(inc >= idx) & tmp;
        mov_t = inc * move;
        mov_f = max_clamp32(count - 1, inc + 1) * move;
        indices[mov_t] = indices[mov_f];

        move |= (u32)(inc + 1 >= idx) & tmp;
        mov_t = (inc + 1) * move;
        mov_f = max_clamp32(count - 1, inc + 2) * move;
        indices[mov_t] = indices[mov_f];

        move |= (u32)(inc + 2 >= idx) & tmp;
        mov_t = (inc + 2) * move;
        mov_f = max_clamp32(count - 1, inc + 3) * move;
        indices[mov_t] = indices[mov_f];

        move |= (u32)(inc + 3 >= idx) & tmp;
        mov_t = (inc + 3) * move;
        mov_f = max_clamp32(count - 1, inc + 4) * move;
        indices[mov_t] = indices[mov_f];

        inc += 4;
    }
    return count - dec;
}
static u32 find_contiguous_free(u32 count, u64 *bits, u32 req_count) {
    u32 tz = 0;
    u32 inc = 0;
    u32 tail = 0;
    u32 shift = 0;
    u64 mask;

    for(u32 i = 0; i < count; ++i)
        tz += pop_count64(~bits[i]);

    if (tz < req_count)
        return Max_u32;

    for(u32 i = 0; i < count; ++i) {
        mask = bits[i];
        if (mask == 0) {
            tail += 64;
            if (tail >= req_count)
                return inc;
            else
                continue;
        } else if (mask == Max_u64) {
            inc += 64;
            continue;
        }

        tz = count_trailing_zeros_u64(mask);
        if (tz + tail >= req_count)
            return inc;

        tail = count_leading_zeros_u64(mask);

        mask >>= tz;
        shift = tz;
        inc = (i << 6) + shift;

        tz = count_trailing_zeros_u64(~mask);
        mask >>= tz;
        shift += tz;
        inc += tz;
        mask |= 0x8000000000000000 >> (shift - 1);

        while(shift < 64 - tail) {
            tz = count_trailing_zeros_u64(mask);
            if (tz >= req_count)
                return inc;

            mask >>= tz;
            shift += tz;
            inc += tz;

            tz = count_trailing_zeros_u64(~mask);
            mask >>= tz;
            shift += tz;
            inc += tz;
        }
    }
    return Max_u32;
}
u32 get_block_size(u32 count, u64 *masks, u32 offset) {
    u32 mask_idx = offset >> 6;
    u32 bit_idx = offset & 63;
    u64 restore = masks[mask_idx];

    u64 mask = masks[mask_idx];
    mask >>= bit_idx;
    mask <<= bit_idx;
    u32 pc = pop_count64(mask);
    u32 tc = 64 & (Max_u32 + pc);
    tc += count_trailing_zeros_u64(mask) & ~(Max_u32 + pc);
    tc -= bit_idx;
    if (tc + bit_idx == 64) {
        for(u32 i = mask_idx + 1; i < count; ++i) {
            if (pop_count64(masks[i]))
                return tc + count_trailing_zeros_u64(masks[i]);
            tc += 64;
        }
    }
    return tc;
}
inline static void make_full(u32 count, u64 *bits, u32 offset, u32 range) {
    u64 mask = 0xffffffffffffffff;
    if ((offset & 63) + range < 64) {
        mask >>= offset & 63;
        mask <<= offset & 63;

        mask <<= 64 - (range + (offset & 63));
        mask >>= 64 - (range + (offset & 63));

        bits[offset >> 6] |= mask;
        return;
    }
    u32 tmp32;
    while(range + (offset & 63) > 64) {
        mask = 0xffffffffffffffff;
        mask >>= offset & 63;
        mask <<= offset & 63;

        tmp32 = 64 - (offset & 63);

        mask <<= 64 - (tmp32 + (offset & 63));
        mask >>= 64 - (tmp32 + (offset & 63));

        bits[offset >> 6] |= mask;

        offset += 64 - (offset & 63);
        range -= tmp32;
    }
    mask = 0xffffffffffffffff;
    mask >>= offset & 63;
    mask <<= offset & 63;

    mask <<= 64 - (range + (offset & 63));
    mask >>= 64 - (range + (offset & 63));

    bits[offset >> 6] |= mask;
}
inline static void make_free(u32 count, u64 *bits, u32 offset, u32 range) {
    u64 mask = 0xffffffffffffffff;
    if ((offset & 63) + range < 64) {
        mask >>= offset & 63;
        mask <<= offset & 63;

        mask <<= 64 - (range + (offset & 63));
        mask >>= 64 - (range + (offset & 63));

        bits[offset >> 6] &= ~mask;
        return;
    }
    u32 tmp32;
    while(range + (offset & 63) > 64) {
        mask = 0xffffffffffffffff;
        mask >>= offset & 63;
        mask <<= offset & 63;

        tmp32 = 64 - (offset & 63);

        mask <<= 64 - (tmp32 + (offset & 63));
        mask >>= 64 - (tmp32 + (offset & 63));

        bits[offset >> 6] &= ~mask;

        offset += 64 - (offset & 63);
        range -= tmp32;
    }
    mask = 0xffffffffffffffff;
    mask >>= offset & 63;
    mask <<= offset & 63;

    mask <<= 64 - (range + (offset & 63));
    mask >>= 64 - (range + (offset & 63));

    bits[offset >> 6] &= ~mask;
}
bool is_range_free(u32 count, u64 *bits, u32 offset, u32 range) {
    u64 mask = 0xffffffffffffffff;
    if ((offset & 63) + range < 64) {
        mask >>= offset & 63;
        mask <<= offset & 63;

        mask <<= 64 - (range + (offset & 63));
        mask >>= 64 - (range + (offset & 63));

        if ((bits[offset >> 6] | ~mask) != ~mask)
            return false;
        return true;
    }
    u32 tmp32;
    while(range + (offset & 63) > 64) {
        mask = 0xffffffffffffffff;
        mask >>= offset & 63;
        mask <<= offset & 63;

        tmp32 = 64 - (offset & 63);

        mask <<= 64 - (tmp32 + (offset & 63));
        mask >>= 64 - (tmp32 + (offset & 63));

        if ((bits[offset >> 6] | ~mask) != ~mask)
            return false;

        offset += 64 - (offset & 63);
        range -= tmp32;
    }
    mask = 0xffffffffffffffff;
    mask >>= offset & 63;
    mask <<= offset & 63;

    mask <<= 64 - (range + (offset & 63));
    mask >>= 64 - (range + (offset & 63));

    if ((bits[offset >> 6] | ~mask) != ~mask)
        return false;
    return true;
}
u32 find_hash_idx(u32 count, u64 *hashes, u64 hash) {
    __m128i a = _mm_load_si128((__m128i*)hashes);
    __m128i b = _mm_set1_epi64((__m64)hash);
    a = _mm_cmpeq_epi64(a, b);
    u16 mask = _mm_movemask_epi8(a);
    u32 inc = 2;
    while(!mask) {
        if (inc > count)
            return Max_u32;

        a = _mm_load_si128((__m128i*)(hashes + inc));
        a = _mm_cmpeq_epi64(a, b);
        mask = _mm_movemask_epi8(a);

        inc += 2;
    }
    return (count_trailing_zeros_u16(mask) >> 3) + (inc - 2);
}
u32 find_lowest_flagged_weight(u32 count, u8 *weights) {
    u32 offset = count - (count & 15);
    offset -= 16 & (Max_u32 + ((count & 15) > 0));
    __m128i a = _mm_load_si128((__m128i*)(weights + offset));
    __m128i b = _mm_set1_epi8(0b1000'0000);
    a = _mm_and_si128(a, b);
    u16 mask = _mm_movemask_epi8(a);
    while(!mask) { // static predict that lowest flagged is not immediate
        if (offset == 0)
            return Max_u32;
        offset -= 16;

        a = _mm_load_si128((__m128i*)(weights + offset));
        a = _mm_and_si128(a, b);
        mask = _mm_movemask_epi8(a);
    }
    return offset + (15 - count_leading_zeros_u16(mask));
}
void recreate_images(Tex_Allocator *alloc, u32 count, u32 *indices) {
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkDevice device = get_gpu_instance()->device;
    VkResult check;
    Tex_Allocation *tex;
    for(u32 i = 0; i < count; ++i) {
        tex = &alloc->allocations[indices[i]];
        info.imageType = VK_IMAGE_TYPE_2D;
        info.extent = {.width = tex->width, .height = tex->height, .depth = 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.format = VK_FORMAT_R8G8B8A8_SRGB;
        vkDestroyImage(device, tex->img, ALLOCATION_CALLBACKS);
        check = vkCreateImage(device, &info, ALLOCATION_CALLBACKS, &tex->img);
        DEBUG_OBJ_CREATION(vkCreateImage, check);
    }
}
    /* End Allocator Helper Algorithms */

    /* Model Texture and Vertex/Index Attribute Allocators */

//
//  ** Model Allocation implementation details (grep marker: ~MAID) **
//  @Todo Explain the implementation.
//

// begin/continue/submit_allocation(..) explanation:
//
// The way that data is described in a gltf file makes it easier to incrementally
// add an allocation to an allocator: we read the descriptions in each accessor
// and buffer view, understand where vertex data/index data is. In this case,
// it is helpful to track the allocation's size as we go. Furthermore, data
// may be fragmented in the buffer (unlikely, but I do not trust the real world)
// and so this way we can make like data contiguous, in case it is fragmented.
// You begin an allocation, read the accessors and buffer views, and add to the
// allocation at whatever stage is sensible (preferably when you encounter fragmentation,
// or when the whole size is understood).
Allocator_Result begin_allocation(Allocator *alloc) { // @TODO CURRENT TASK!! Read the code you wrote lazy bones.
    ASSERT(alloc->to_stage_count == Max_u32, "");
    // Upon completing an allocation, '.to_stage_count' is set to Max_u32.
    if (alloc->to_stage_count != Max_u32)
        return QUEUE_IN_USE;
    // '.allocations' is not a dynamic array.
    if (alloc->allocation_count == alloc->allocation_cap)
        return ALLOCATOR_FULL;

    // Add the new allocation. The allocation count is only incremented
    // once submit has been called. Therefore, if this allocation is cancelled,
    // (which really it never should be) the left over data will just get overwritten.
    Allocation *tmp = &alloc->allocations[alloc->allocation_count];
    *tmp = {};

    // Allocations are also written to a file managed by the allocator. In case allocations have to be evicted
    // from the staging queue, they can be quickly reloaded from this disk storage, rather than the model file.
    // (In a release ready production app, I would get rid of the gltf buffer files completely, and just have
    // model data live in the allocator buffers, so no reading and rewriting, but this is a fine implementation
    // for now. It proves the concept and is easily switched to the version described).
    tmp->disk_offset = alloc->disk_size;
    alloc->disk = fopen(alloc->disk_storage, "wb");

    // staging queue must not be used during the allocation phase of the program. So it is safe to
    // reuse it here for tracking in-progress allocations.
    alloc->staging_queue_byte_count = 0;
    return SUCCESS;
}
// The pointer should be to a gltf buffer file read into memory. The allocator
// then copies from this pointer into its staging buffer (if there is room),
// and writes the data to its internal storage (I agree that this disk
// copy is lame and slow. See the begin/continue/submit... explanation above
// for what I would do for release).
Allocator_Result continue_allocation(Allocator *alloc, u64 size, void *ptr) {
    // @Note Test against upload cap assuming that the stage cap is at least as large
    // as the upload cap.
    u64 queue_size = align(alloc->staging_queue_byte_count + size, alloc->upload_bit_granularity);
    if (queue_size > alloc->upload_queue_byte_cap) {
        // Remove the current allocation from the allocator if overflow. (@Note There is probably
        // a better way to handle this failure, but this is currently the simplest.)
        alloc->bytes_staged -= alloc->staging_queue_byte_count;
        alloc->disk_size -= alloc->staging_queue_byte_count;
        return ALLOCATION_TOO_LARGE;
    }

    // Adding allocations to a the allocators should happen at a specific stage
    // in the program, a stage which happens before using the '..queue..' functions
    // below. Therefore, at this stage we can treat the allocator as a simple
    // linear allocator. Since we already have the data in memory, if there is still
    // room in the staging buffer, copy it in. (Note that allocations which are expected
    // to be needed often should be added to the allocators first to take advantage of
    // this pseudo caching.)
    //
    // @Note Sizes are not aligned here as these are *continued* allocations. Only
    // the final allocation size should be aligned, obvs.
    //
    // 'size' is added to 'bytes_staged' everytime, so that in 'submit_allocation(..)'
    // we can check if the size overflowed. If so, we can then remove the whole allocation
    // from the stage buffer. Trying to track that here is more uglier.
    alloc->bytes_staged += size;
    if (align(alloc->bytes_staged, alloc->staging_queue_bit_granularity) < alloc->stage_cap) {
        // ptr should be a pointer to model data pertinent to the in progress allocation, so we
        // copy from the pointer, into the allocator.
        memcpy(alloc->stage_ptr + (alloc->bytes_staged - size), ptr, size);
    }

    // Write the allocation to file (see 'begin/continue... explanation' above for explanation).
    fseek(alloc->disk_storage, 0, alloc->disk_size);
    fwrite(alloc->disk_storage, ptr, size); // offset at disk size;
    alloc->disk_size += size;

    alloc->allocations[alloc->allocation_count] += size;
    alloc->staging_queue_byte_count += size;
    return SUCCESS;
}
Allocator_Result submit_allocation(Allocator *alloc, u8 weight, u32 *key) {
    u64 aligned_size = align(alloc->bytes_staged, alloc->staging_queue_bit_granularity);
    if (aligned_size > alloc->stage_cap)
        alloc->bytes_staged -= alloc->staging_queue_byte_count;
    else
        alloc->bytes_staged = aligned_size;

    // '*key' is an index which corresponds to this allocation's index in the
    // '.indices' field of the allocator. As the allocator is used, allocations
    // will be shuffled around; this key is maintained to point to the correct
    // allocation. (Read the implementation details above for details - grep for ~MAID.)
    *key = alloc->allocation_count;
    alloc->allocation_count++;

    // Max_u32 indicates that the queue is safe to use again. This is used by 'begin_allocation(..)'.
    alloc->to_stage_count = Max_u32;
    fclose(alloc->disk_storage);
    return SUCCESS;
}
Allocator_Result staging_queue_begin(Allocator *alloc) {
    // '.to_stage_count' set to max by queue submission, indicating it
    // is safe to use again.
    if (alloc->to_stage_count != Max_u32)
        return QUEUE_IN_USE;
    alloc->to_stage_count = 0;
    alloc->staging_queue_byte_count = 0;
    return SUCCESS;
}
Allocator_Result staging_queue_add(Allocator *alloc, u32 key) {
    Weight_Args w_args = {
        .count = alloc->allocation_count,
        .weights = alloc->allocation_weights,
        .states = alloc->allocation_allocation_states,
        .indices = alloc->allocation_indices,
        .allocations = alloc->allocations,
        .idx = key,
        .inc = 3,
        .dec = 1 // @Test Find effective inc and dec values
    };
    // Increment the weight of the desired allocation, decrease other weights.
    // Shuffle data according to weights (don't worry, this is cheap and pays
    // off. See the function implementation to understand better). Return the
    // index of the desired allocation and its attributes (weight and state).
    u32 idx = adjust_allocation_weights(&w_args);
    alloc->allocation_states[idx] |= TO_DRAW;
    // If the allocation is already staged or marked to be staged, early return.
    if (alloc->allocation_states[idx] & STAGED || alloc->allocation_states[idx] & TO_STAGE)
        return SUCCESS;

    // An allocation's location in staging memory is represented by a bit array (some u64s),
    // with each bit representing some number of real bytes. Later in the submission function,
    // in order to make space for new data, allocations can be evicted. This is done by clearing
    // the bit representation of their data range (offset to size). If allocations' positions
    // are not aligned to their bit representation, they are not accurately represented.
    u64 bit_align_size = align(alloc->allocations[idx].size, alloc->stage_bit_granularity);
    if (bit_align_size + alloc->staging_queue_byte_count > alloc->staging_queue_byte_cap) {
        // If the queue add fails, we do not want stuff marked as to draw that is not also part of a queue.
        alloc->allocation_states[idx] &= ~TO_DRAW;
        return QUEUE_FULL;
    }

    alloc->allocation_states[idx] |= TO_STAGE;
    alloc->staging_queue_byte_count += bit_align_size;
    alloc->to_stage_count++;
    return SUCCESS;
}
Allocator_Result staging_queue_submit(Allocator *alloc) {
    // If the to stage count is zero on queue submission, just assume that everything queued was already
    // cached, and we need not do anything. This is most likely, as vertex data should just be able to live in
    // memory (I am pretty sure). However, if vertex data is ever too large for the staging buffer,
    // allocations can be evicted and reloaded from the allocator's disk storage (see implementation above).
    if (alloc->to_stage_count == 0) {
        alloc->to_stage_count = Max_u32;
        return SUCCESS;
    }

    Allocation  *tmp;
    Allocation  *allocations = alloc->allocations;
    Alloc_State *states      = alloc->allocation_states;
    u64          queue_size  = alloc->staging_queue_byte_count;

    u32 mask_count = alloc->stage_mask_count;
    u64 *masks     = alloc->stage_masks;

    u32 g          = alloc->stage_bit_granularity;
    u32 req_bits   = alloc->staging_queue_byte_count / g; // This size is aligned to g (being the sum of aligned sizes), so need to worry about remainder

    u32 indices_count;
    u32 *indices   = (u32*)malloc_t(sizeof(u32) * alloc->allocation_count, 16); // Align 16 for SIMD

    // @Note Although I would like to, this section cannot really be moved into its own function
    // cleanly, as the internal logic has to be so slightly different each time (such as which size to use, or
    // how to calculate the size). So it is easier to just inline it and not fuss...
    //
    // Section Explanation:
    // If no contiguous block of free memory sufficient to hold the size of the stage queue is available in
    // the stage buffer (as represented by the bit masks), find the allocations flagged as stageed, but which
    // are not flagged for staging, uploading or drawing; loop these allocations, starting at the allocation
    // with the lowest weight (see implementation details above for what 'weight' means); mark the
    // allocation's range as free in the bit mask, check if there is now a large enough size, and if so,
    // break; if we have otherwise looped all allocations and there is no room, so return error code.
    u32 free_block = find_contiguous_free(alloc->stage_mask_count, alloc->stage_masks, req_bits);
    if (free_block == Max_u32) {
        // In case of failure, we need to restore the masks initial states. (Failure should be incredibly unlikely,
        // if it ever happens at all. The code using the allocators should use them efficiently.)
        u64 mask_copies = (u64*)malloc_t(sizeof(u64) * mask_count, 16);
        memcpy(mask_copies, masks, sizeof(u64) * mask_count);

        u32 size;
        u32 offset;
        indices_count = simd_find_flags_u8(states, STAGED, TO_UPLOAD | TO_DRAW | TO_STAGE, indices);
        for(u32 i = indices_count - 1; i != Max_u32; --i) {
            tmp = &allocations[indices[i]];

            // @Note Really g should always be power of 2, so these should be bit shifts, not divides. I really do not
            // like these divides...
            size   = align(tmp->size,   g) / g;
            offset = align(tmp->offset, g) / g;

            // Clear the allocation's range in the bit masks, and check if there is now a large enough free block.
            make_free(mask_count, masks, offset, size);
            free_block = find_contiguous_free(mask_count, masks, req_bits);

            if (free_block != Max_u32) {
                // Only mark allocations as having been evicted from staging buffer if they are actually going to be
                // evicted (i.e. only if a sufficient free block is actually available).
                //
                // @Todo This should be implemented as simd_update_flags_u8(..) but with the ability to start
                // from an offset. Doing this as a loop over individual u8s is very very lame.
                for(u32 j = i; j < count; ++j)
                    states[indices[j]] &= ~STAGED;

                evict_idx = i;
                goto free_block_found; // jump over early return
            }
        }
        // Restore the masks to before allocations were marked as free.
        memcpy(masks, mask_copies, sizeof(u64) * mask_count);
        return STAGE_FULL; // Too many allocations waiting to draw
    }

    free_block_found: // goto

    // Find how large free_block is.
    u64 block_size = get_block_size(mask_count, masks, free_block) * g;

    // Find the indices of all allocations which are not staged and not marked as to stage.
    indices_count = simd_find_flags_u8(states, 0x00, STAGED | TO_STAGE, indices);

    // Loop the un-staged allocations (which are not already flagged as to stage), starting at the highest
    // weight (lowest index). If there is size available in the free block for the queued allocations and the
    // current allocation in loop, add the allocation to the list of allocations that we want to stage.
    u64 size = 0;
    for(u32 i = 0; i < indices_count; ++i) {
        size += align(allocations[indices[i]].size, g);
        // @Test This static predicts that there will be room for at least one allocation, it might be
        // best to switch it around.
        if (size > block_size - queue_size)
            break;
        else
            states[indices[i]] |= TO_UPLOAD;
    }
    // Get the final list of allocations to stage.
    indices_count = simd_find_flags_u8(states, TO_STAGE, 0x00, indices);

    // @Note @Todo I do not know if this is a naive implementation. The way the allocator works is it takes a
    // parsed gltf file, and then groups accessors/buffer views into one allocation. So all of a model's
    // vertex attribute data is put into one contiguous allocation which is rewritten to disk storage managed
    // by the allocator, in case of an eviction. This may be pointless, as it seems sensible that the data
    // would already be laid out like this in the file. But I do not want to always rely on that fact (in a
    // real company this would just be enforced, but wild world I will not trust). It's sort of dumb in the
    // current state of the app, as these gltf files are read and rewritten every time the program runs (but
    // ofc in production I would ditch the gltf files completely and just ship with the allocator files,
    // obvs). It will stay like this for now, as it is just a proof of concept and can easily be turned into
    // the production equivalent. Copying around data is the essence of computing *shrug*.
    u64 stage_offset   = free_block * g;
    FILE *disk_storage = fopen(alloc->disk_storage);
    void *stage_ptr    = alloc->stage_ptr;
    for(u32 i = 0; i < count; ++i) {
        tmp = allocations[indices[i]];

        // Read the allocation's data from the allocator's disk copy of the data.
        fseek(disk_storage, 0, tmp->disk_offset);
        fread(stage_ptr + stage_offset, 1, tmp->size, disk_storage);

        // Set the allocation's new stage offset and update its state.
        tmp->stage_offset = stage_offset;

        // Increment offset to point beyond the most recent allocation, aligned to the stage
        // bit granularity.
        stage_offset += align(tmp->size, g);

        ASSERT(stage_offset + tmp->size <= alloc->stage_cap, "Allocator Stage Overflow");
    }
    fclose(disk_storage);

    // Mark allocations as staged.
    simd_update_flags(allocation_count, states, TO_STAGE, 0x0, STAGED, TO_STAGE);

    alloc->to_stage_count = Max_u32; // Indicate that it is safe to begin a new queue.
    return SUCCESS;
}
// @Note Upload queue functions behave very similarly to the staging queue functions.
// The implementation system and reasoning is almost entirely the same.
// Although the differences are pretty trivial, they are significant and
// annoying enough to collapse that the staging and upload functions should be separate (imo).
Allocator_Result upload_queue_begin(Allocator *alloc) {
    if (alloc->to_upload_count != Max_u32)
        return QUEUE_IN_USE;
    alloc->to_upload_count = 0;
    alloc->upload_queue_byte_count = 0;
    return SUCCESS;
}
Allocator_Result upload_queue_add(Allocator *alloc, u32 idx) {
    Weight_Args w_args = {
        .count = alloc->allocation_count,
        .weights = alloc->allocation_weights,
        .states = alloc->allocation_allocation_states,
        .indices = alloc->allocation_indices,
        .allocations = alloc->allocations,
        .idx = key,
        .inc = 3,
        .dec = 1 // @Test Find effective inc and dec values
    };
    // Increment the weight of the desired allocation, decrease other weights.
    // Shuffle data according to weights (don't worry, this is cheap and pays
    // off. See the function implementation to understand better). Return the
    // index of the desired allocation and its attributes (weight and state).
    u32 idx = adjust_allocation_weights(&w_args);
    alloc->allocation_states[idx] |= TO_DRAW;
    // If the allocation is already uploaded or marked to be uploaded, early return.
    if (alloc->allocation_states[idx] & UPLOADED || alloc->allocation_states[idx] & TO_UPLOAD)
        return SUCCESS;

    // An allocation's location in device memory is represented by a bit array (some u64s),
    // with each bit representing some number of real bytes. Later in the submission function,
    // in order to make space for new data, allocations can be evicted. This is done by clearing
    // the bit representation of their data range (offset to size). If allocations' positions
    // are not aligned to their bit representation, they are not accurately represented.
    u64 bit_align_size = align(alloc->allocations[idx].size, alloc->upload_bit_granularity);
    if (bit_align_size + alloc->upload_queue_byte_count > alloc->upload_queue_byte_cap) {
        // If the queue add fails, we do not want stuff marked as to draw that is not also part of a queue.
        alloc->allocation_states[idx] &= ~TO_DRAW;
        return QUEUE_FULL;
    }

    alloc->allocation_states[idx] |= TO_UPLOAD;
    alloc->upload_queue_byte_count += bit_align_size;
    alloc->to_upload_count++;
    return SUCCESS;
}
Allocator_Result upload_queue_submit(Allocator *alloc) {
    // If the to upload count is zero on queue submission, just assume that everything queued was already
    // cached, and we need not do anything.
    if (alloc->to_upload_count == 0) {
        alloc->to_upload_count = Max_u32;
        return SUCCESS;
    }

    Allocation  *tmp;
    Allocation  *allocations = alloc->allocations;
    Alloc_State *states      = alloc->allocation_states;
    u64          queue_size  = alloc->upload_queue_byte_count;

    u32 mask_count = alloc->upload_mask_count;
    u64 *masks     = alloc->upload_masks;

    u32 g          = alloc->upload_bit_granularity;
    u32 req_bits   = alloc->upload_queue_byte_count / g; // This size is aligned to g (being the sum of aligned sizes), so need to worry about remainder

    u32 indices_count;
    u32 *indices   = (u32*)malloc_t(sizeof(u32) * alloc->allocation_count, 16); // Align 16 for SIMD

    // @Note Although I would like to, this section cannot really be moved into its own function
    // cleanly, as the internal logic has to be so slightly different each time (such as which size to use, or
    // how to calculate the size). So it is easier to just inline it and not fuss...
    //
    // Section Explanation:
    // If no contiguous block of free memory sufficient to hold the size of the upload queue is available in
    // the upload buffer (as represented by the bit masks), find the allocations flagged as uploaded, but
    // which are not flagged for uploading or drawing; loop these allocations, starting at the allocation with
    // the lowest weight (see implementation details above for what 'weight' means); mark the allocation's
    // range as free in the bit mask, check if there is now a large enough size, and if so, break; if we have
    // otherwise looped all allocations and there is no room, so return error code.
    u32 free_block = find_contiguous_free(alloc->upload_mask_count, alloc->upload_masks, req_bits);
    if (free_block == Max_u32) {
        // In case of failure, we need to restore the masks initial states. (Failure should be incredibly unlikely,
        // if it ever happens at all. The code using the allocators should use them efficiently.)
        u64 mask_copies = (u64*)malloc_t(sizeof(u64) * mask_count, 16);
        memcpy(mask_copies, masks, sizeof(u64) * mask_count);

        u32 size;
        u32 offset;
        indices_count = simd_find_flags_u8(states, UPLOADED, TO_UPLOAD | TO_DRAW, indices);
        for(u32 i = indices_count - 1; i != Max_u32; --i) {
            tmp = &allocations[indices[i]];

            // @Note Really g should always be power of 2, so these should be bit shifts, not divides. I really do not
            // like these divides...
            //
            // Find the allocations range (adjusted to the range in bits).
            size   = align(tmp->size,   g) / g;
            offset = align(tmp->offset, g) / g;

            // Clear the allocation's range in the bit masks, and check if there is now a large enough free block.
            make_free(mask_count, masks, offset, size);
            free_block = find_contiguous_free(mask_count, masks, req_bits);

            if (free_block != Max_u32) {
                // Only mark allocations as having been evicted from upload buffer if they are actually going to be
                // evicted (i.e. only if a sufficient free block is actually available).
                //
                // @Todo This should be implemented as simd_update_flags_u8(..) but with the ability to start
                // from an offset. Doing this as a loop over individual u8s is very very lame.
                for(u32 j = i; j < indices_count; ++j)
                    states[indices[j]] &= ~UPLOADED;

                goto free_block_found; // jump over early return
            }
        }
        // Restore the masks to before allocations were marked as free.
        memcpy(masks, mask_copies, sizeof(u64) * mask_count);
        return UPLOAD_FULL; // Too many allocations waiting to draw
    }

    free_block_found: // goto

    // Find how large free_block is.
    u64 block_size = get_block_size(mask_count, masks, free_block) * g;

    // Find the indices of all allocations which are not uploaded and not marked as to upload.
    indices_count = simd_find_flags_u8(states, 0x00, TO_UPLOAD | UPLOADED, indices);

    // Loop the un-uploaded allocations (which are not already flagged as to upload, starting at the highest
    // weight (lowest index). If there is size available in the free block for the queued allocations and the
    // current allocation in loop, add the allocation to the list of allocations that we want to upload.
    u64 size = 0;
    for(u32 i = 0; i < indices_count; ++i) {
        size += align(allocations[indices[i]].size, g);
        // @Test This static predicts that there will be room for at least one allocation, it might be
        // best to switch it around.
        if (size > block_size - queue_size)
            break;
        else
            states[indices[i]] |= TO_UPLOAD;
    }
    // Get the final list of allocations to upload.
    indices_count = simd_find_flags_u8(states, TO_UPLOAD, 0x00, indices);

    // Create the upload regions information
    VkBufferCopy2 *regions = (VkBufferCopy2*)malloc_t(sizeof(VkBufferCopy2) * indices_count, 8);
    u64 g = alloc->upload_bit_granularity;
    u64 upload_offset = free_block * g;
    for(u32 i = 0; i < indices_count; ++i) {
        tmp = &alloc->allocations[indices[i]];
        regions[i] = {VK_STRUCTURE_TYPE_BUFFER_COPY_2};
        // @Note I would like to break up the 'Allocation' struct even further, separating out stage
        // and upload offsets. But this loop deters me. Specifically the below two lines...
        regions[i].srcOffset = tmp->stage_offset;
        regions[i].dstOffset = upload_offset;
        regions[i].size = tmp->size;
        tmp->upload_offset = upload_offset;
        upload_offset = align(tmp->size, g);
    }

    // Copy info
    VkCopyBufferInfo2 copy_info = {VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2};
    copy_info.srcBuffer = alloc->stage;
    copy_info.dstBuffer = alloc->upload;
    copy_info.regionCount = region_count;
    copy_info.pRegions = regions;

    VkDevice device = gpu->device;

    // Allocate graphics command buffers
    VkCommandBufferAllocateInfo cmd_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc_info.commandPool = alloc->graphics_pool;
    cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    cmd_alloc_info.commandBufferCount = 1;
    auto check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->graphics_cmd);
    DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

    VkCommandBufferInheritanceInfo inheritance = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    VkCommandBufferBeginInfo cmd_begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmd_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    cmd_begin_info.pInheritanceInfo = &inheritance;

    if ((mem_flags & GPU_MEMORY_DISCRETE_TRANSFER_BIT) == 0) { // static predict discrete transfer
        vkBeginCommandBuffer(alloc->graphics_cmd, &cmd_begin_info);

        VkMemoryBarrier2 mem_barr = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mem_barr.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        mem_barr.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        mem_barr.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR;
        mem_barr.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR;

        VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &mem_barr;
        vkCmdPipelineBarrier2(alloc->graphics_cmd, &dep);

        vkEndCommandBuffer(alloc->graphics_cmd);

    } else {
        // Allocate transfer command buffers
        cmd_alloc_info.commandPool = alloc->transfer_pool;
        check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->transfer_cmd);
        DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

        vkBeginCommandBuffer(alloc->transfer_cmd, &cmd_begin_info);

        vkCmdCopyBuffer2(alloc->transfer_cmd, &copy_info);

        // Transfer queue release barrier
        VkBufferMemoryBarrier2 buf_barr = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        buf_barr.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        buf_barr.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        buf_barr.srcQueueFamilyIndex = gpu->transfer_queue_index;
        buf_barr.dstQueueFamilyIndex = gpu->graphics_queue_index;
        buf_barr.buffer = alloc->upload;
        buf_barr.offset = free_block * g; // start of first allocation
        buf_barr.size = upload_offset; // has been incremented beyond final allocation

        VkDependencyInfo dep_info = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep_info.bufferMemoryBarrierCount = 1;
        dep_info.pBufferMemoryBarriers = &buf_barr;
        vkCmdPipelineBarrier2(alloc->transfer_cmd, &dep_info);

        vkEndCommandBuffer(alloc->transfer_cmd);

        vkBeginCommandBuffer(alloc->graphics_cmd, &cmd_begin_info);

        // Graphics queue acquire barrier
        buf_barr.srcStageMask = 0x0;
        buf_barr.srcAccessMask = 0x0;
        buf_barr.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR;
        buf_barr.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR;
        vkCmdPipelineBarrier2(alloc->graphics_cmd, &dep_info);

        vkEndCommandBuffer(alloc->graphics_cmd);
    }

    // @Note This could be perceived as a little premature, as the commands have not actually been submitted.
    // However, the commands having been recorded constitutes upload complete from the ALLOCATOR's point of
    // view: the draw commands will happen later in the same queue submission section later down the pipeline,
    // which the allocator is not a part of. Furthermore, from the allocation's perspective, as they will be
    // truly uploaded in the same point of the pipeline as the draw commands (the upload cmds will even be in
    // the same batch as the draw commands if unified transfer), they cannot react to a TO_UPLOAD state. I
    // cannot, call queue submit, wait for the semaphore, then set UPLOADED state, then call draw, as the
    // draws are already submitted with the submission that the semaphore wait was signalling.
    simd_update_flags_u8(allocation_count, states, TO_UPLOAD, 0x0, UPLOADED, TO_UPLOAD);

    alloc->to_upload_count = Max_u32;
    return SUCCESS;
}
Allocator_Result tex_add_texture(Tex_Allocator *alloc, String *file_name, u32 *key) {
    u64 hash = get_string_hash(file_name);
    for(u32 i = 0; i < alloc->allocation_count; ++i)
        if (hash == alloc->hashes[i]) {
            adjust_allocation_weights();
            *key = i;
            return SUCCESS;
        }

    Image img = load_image(file_name);
    u64 img_size = img.width * img.height * 4;
    ASSERT(img_size <= alloc->stage_cap, "Image Too Large");
    if (img_size > alloc->stage_cap)
        return STAGE_FULL;

    Settings *settings = get_global_settings(); // sample count
    VkImageCreateInfo img_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_R8G8B8A8_SRGB;
    img_info.extent = {.width = img.width, .height = img.height, .depth = 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = settings->sample_count;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage vk_img;
    VkDevice dvc = get_gpu_instance()->device;
    auto check = vkCreateImage(dvc, &img_info, ALLOCATION_CALLBACKS, &vk_img);
    DEBUG_OBJ_CREATION(vkCreateImage, check);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dvc, vk_img, &req);
    ASSERT(req.alignment & (alloc->upload_bit_granularity - 1) == 0,
           "Bit Granularity MUST have sufficient alignment for any image");
    if (req.alignment & (alloc->upload_bit_granularity - 1) != 0)
        return MISALIGNED_BIT_GRANULARITY_UPLOAD;

    Tex_Allocation *tmp = alloc->allocations[alloc->allocation_count];
    tmp->file_name = string_buffer_get_string(&alloc->string_buffer, file_name);
    tmp->width = img.width;
    tmp->height = img.height;
    tmp->image = vk_img;
    tmp->size = align(req.size, alloc->upload_bit_granularity);

    u64 align_size = align(img_size, alloc->stage_bit_granularity);
    if (alloc->staging_queue_byte_count + align_size < alloc->staging_queue_byte_cap) {
        memcpy(alloc->stage_ptr + alloc->staging_queue_byte_count, img.data, img_size);
        tmp->stage_offset = alloc->staging_queue_byte_count;
        alloc->allocation_states[alloc->allocation_count] |= STAGED;
        alloc->staging_queue_byte_count += align_size;
    }
    *key = alloc->allocation_count;
    alloc->allocation_count++;
    return SUCCESS;
}
Allocator_Result tex_staging_queue_begin(Allocator *alloc) {
    if (alloc->to_stage_count != Max_u32)
        return QUEUE_IN_USE;
    alloc->to_stage_count = 0;
    alloc->staging_queue_byte_count = 0;
    return SUCCESS;
}
Allocator_Result tex_staging_queue_add(Allocator *alloc, u32 idx) {
    Weight_Args w_args = {
        .count = alloc->allocation_count,
        .weights = alloc->allocation_weights,
        .states = alloc->allocation_allocation_states,
        .indices = alloc->allocation_indices,
        .allocations = alloc->allocations,
        .idx = idx,
        .inc = 3,
        .dec = 1 // @Test Find effective inc and dec values
    };
    idx = adjust_allocation_weights(&w_args);
    alloc->allocation_states[idx] |= TO_DRAW;
    if (alloc->allocation_states[idx] & STAGED || alloc->allocation_states[idx] & TO_STAGE)
        return SUCCESS;

    Tex_Allocation *tmp = &alloc->allocations[idx];
    u64 img_size = tmp->width * tmp->height * 4;
    // Ensure that allocations will not overlap into another's bit representation
    u64 bit_align_size = align(img_size, alloc->stage_bit_granularity);
    if (bit_align_size + alloc->staging_queue_byte_count > alloc->staging_queue_byte_cap) {
        alloc->allocation_states[idx] &= ~TO_DRAW;
        return QUEUE_FULL;
    }

    alloc->allocation_states[idx] |= TO_STAGE;
    alloc->staging_queue_byte_count += bit_align_size;
    alloc->to_stage_count++;
    return SUCCESS;
}
Allocator_Result tex_staging_queue_submit(Tex_Allocator *alloc) {
    // If the to stage count is zero on queue submission, just assume that everything queued was already
    // cached, and we need not do anything. This is most likely, as vertex data should just be able to live in
    // memory I am pretty certain. In case it can't (I would like to support even shit hardware, as everyone
    // should) the same range searching functionality is implemented here for disk->stage (in the same way as
    // stage->device)
    if (alloc->to_stage_count == 0) {
        alloc->to_stage_count = Max_u32;
        return SUCCESS;
    }

    u32 g = alloc->stage_bit_granularity;
    u32 req_bits = alloc->staging_queue_byte_count / g; // This size is aligned to g, so need to worry about remainder
    u32 free_block = find_contiguous_free(alloc->stage_mask_count, alloc->stage_masks, req_bits);
    Allocation *tmp;
    u32 *indices = alloc;
    u32 evict_idx;
    u32 count;
    u64 img_size;
    if (free_block == Max_u32) {
        // @Note '| TO_STAGE' is unnecessary for now, see equivalent note in staging_queue_submit for justification
        count = simd_find_flags_u8(alloc->allocation_states, STAGED, TO_DRAW | TO_STAGE, indices);
        u32 size;
        u32 offset;
        for(u32 i = count - 1; i != Max_u32; --i) {
            tmp = alloc->allocations[indices[i]];
            size = align(tmp->width * tmp->height * 3, g) / g;
            offset = align(tmp->stage_offset, g) / g;
            make_free(alloc->stage_mask_count, alloc->stage_masks, offset, size);
            free_block = find_contiguous_free(alloc->stage_mask_count, alloc->stage_masks, req_bits);
            if (free_block != Max_u32) {
                evict_idx = i;
                // Only mark as evicted if allocations will be overwritten (i.e. if free block is available)
                for(u32 j = i; j < count; ++j)
                    alloc->allocation_states[indices[i]] &= ~STAGED;
                goto free_block_found; // jump over early return
            }
        }
        return STAGE_FULL; // Too many allocations waiting to draw
    }

    free_block_found: // goto label

    u64 block_size = get_block_size(alloc->stage_mask_count, alloc->stage_masks, free_block) * g;
    count = simd_find_flags_u8(alloc->allocation_states, 0x00, STAGED, indices);
    u32 indices_final = alloc;
    u64 size = 0;
    for(u32 i = 0; i < count; ++i) {
        tmp = alloc->allocations[indices[i]];
        img_size = tmp->width * tmp->height * 4;
        size += align(img_size, alloc->stage_bit_granularity);
        if (size <= block_size - alloc->staging_queue_byte_count)
            indices_final[i] = indices[i];
        else
            break;
    }
    u32 tmp = simd_find_flags_u8(alloc->allocation_states, TO_STAGE, 0x00, indices);
    memcpy(indices_final + count, indices, tmp * sizeof(u32));
    count = eject_repeat_indices(count + tmp, indices_final);

    u64 stage_offset = free_block * alloc->stage_bit_granularity;
    Image img;
    for(u32 i = 0; i < count; ++i) {
        tmp = alloc->allocations[indices[i]];
        img = load_image(&tmp->file_name);
        img_size = img.width * img.height * 4;
        memcpy(alloc->stage_ptr + stage_offset, img.data, img_size);
        tmp->stage_offset = stage_offset;
        alloc->allocation_states[indices[i]] &= ~TO_STAGE;
        alloc->allocation_states[indices[i]] |= STAGED;
        stage_offset += align(tmp->size, alloc->stage_bit_granularity);
        ASSERT(stage_offset + tmp->size <= alloc->stage_cap, "Allocator Stage Overflow");
    }
    alloc->to_stage_count = Max_u32;
    return SUCCESS;
}
Allocator_Result tex_upload_queue_begin(Tex_Allocator *alloc) {
    if (alloc->to_upload_count != Max_u32)
        return QUEUE_IN_USE;
    alloc->to_upload_count = 0;
    alloc->upload_queue_byte_count = 0;
    return SUCCESS;
}
Allocator_Result tex_upload_queue_add(Tex_Allocator *alloc, u32 idx) {
    Weight_Args w_args = {
        .count = alloc->allocation_count,
        .weights = alloc->allocation_weights,
        .states = alloc->allocation_allocation_states,
        .indices = alloc->allocation_indices,
        .allocations = alloc->allocations,
        .idx = idx,
        .inc = 3,
        .dec = 1 // @Test Find effective inc and dec values
    };
    idx = adjust_allocation_weights(&w_args);
    alloc->allocation_states[idx] |= TO_DRAW;
    if (alloc->allocation_states[idx] & UPLOADED || alloc->allocation_states[idx] & TO_UPLOAD)
        return SUCCESS;

    // Ensure that allocations will not overlap into another's bit representation
    Tex_Allocation *tmp = &alloc->allocations[idx];
    if (tmp->size + alloc->upload_queue_byte_count > alloc->upload_queue_byte_cap) {
        alloc->allocation_states[idx] &= ~TO_DRAW;
        return QUEUE_FULL;
    }

    alloc->allocation_states[idx] |= TO_UPLOAD;
    alloc->upload_queue_byte_count += bit_align_size;
    alloc->to_upload_count++;
    return SUCCESS;
}
Allocator_Result tex_upload_queue_submit(Tex_Allocator *alloc) {
    // If the to upload count is zero on queue submission, just assume that everything queued was already
    // cached, and we need not do anything. This is most likely, as vertex data should just be able to live in
    // memory I am pretty certain. In case it can't (I would like to support even shit hardware, as everyone
    // should) the same range searching functionality is implemented here for disk->upload (in the same way as
    // upload->device)
    if (alloc->to_upload_count == 0) {
        alloc->to_upload_count = Max_u32;
        return SUCCESS;
    }

    // @Todo I think these find free blocks have template args
    u32 g = alloc->upload_bit_granularity;
    u32 req_bits = alloc->upload_queue_byte_count / g;
    u32 free_block = find_contiguous_free(alloc->upload_mask_count, alloc->upload_masks, req_bits);
    Allocation *tmp;
    u32 *indices = (u32*)malloc_t(sizeof(u32) * alloc->allocation_count, 16);
    u32 evict_idx;
    u32 count;
    if (free_block == Max_u32) {
        // @Note '| TO_UPLOAD' is unnecessary for now, see equivalent note in staging_queue_submit for justification.
        count = simd_find_flags_u8(alloc->allocation_states, UPLOADED, TO_DRAW | TO_UPLOAD, indices);
        u32 size;
        u32 offset;
        for(u32 i = count - 1; i != Max_u32; --i) {
            tmp = alloc->allocations[indices[i]];
            size = align(tmp->size, g);
            offset = align(tmp->stage_offset, g);
            make_free(alloc->upload_mask_count, alloc->upload_masks, offset, size);
            free_block = find_contiguous_free(alloc->upload_mask_count, alloc->upload_masks, req_bits);
            if (free_block != Max_u32) {
                evict_idx = i;
                // Only mark as evicted if allocations will be overwritten (i.e. if free block is available)
                for(u32 j = i; j < count; ++j)
                    alloc->allocation_states[indices[i]] &= ~UPLOADED;
                goto free_block_found; // jump over early return
            }
        }
        return STAGE_FULL; // Too many allocations waiting to draw
    }

    free_block_found: // goto label

    // @Todo This is not the correct value, this is just the offset to the block, not the size of the block.
    // I need to write another function to get the size of the block.
    u64 block_size = get_block_size(alloc->upload_mask_count, alloc->upload_masks, free_block) * g;
    count = simd_find_flags_u8(alloc->allocation_states, 0x00, UPLOADED, indices);
    u32 *indices_final = (u32*)malloc_t(sizeof(u32) * alloc->allocation_count, 16);
    u64 size = 0;
    for(u32 i = 0; i < count; ++i) {
        tmp = alloc->allocations[indices[i]];
        size += align(tmp->size, alloc->upload_bit_granularity);
        if (size > block_size - alloc->upload_queue_byte_count)
            break;

        indices_final[i] = indices[i];
        alloc->allocation_states[indices[i]] |= TO_UPLOAD;
    }

    simd_find_flags_u8(alloc->allocation_states, TO_UPLOAD, 0x00, indices);
    memcpy(indices_final + count, indices, tmp * sizeof(u32));
    count = eject_repeat_indices(count + alloc->to_upload_count, indices_final);

    u64 upload_offset = free_block * alloc->upload_bit_granularity;
    Gpu *gpu = get_gpu_instance();
    VkDevice device = gpu->device;
    VkBindImageMemoryInfo *bind_infos = (VkBindImageMemoryInfo*)malloc_t(sizeof(VkBindImageMemoryInfo) *
                                                                         alloc->allocation_count, 8);
    u64 alignment;
    VkMemoryRequirements mem_req;
    for(u32 i = 0; i < count; ++i) {
        tmp = alloc->allocations[indices_final[i]];
        tmp->upload_offset = upload_offset;
        bind_infos[i] = {VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO};
        bind_infos[i].image = tmp->img;
        bind_infos[i].memory = alloc->upload;
        // Already aligned to bit granularity which must be sufficient
        bind_infos[i].memoryOffset = upload_offset;
        upload_offset += alloc->bind_infos[i].size; // This size is aligned to bit granularity already
    }
    if (vkBindImageMemory2(device, count, bind_infos) != VK_SUCCESS) {
        // @Note Not quite sure what to do with failure here. Returning like this leaves the queue still
        // full, which means the client can call retry and this point should just be reached instantly again.
        // And then if the bind fails again they can reset the queue themselves. You could argue that the allocator
        // should empty the queue and reset itself. But I will stick with this for now. Idk how serious a bind
        // failure is really. In my case it should never happen as all my memory is allocated once.
        recreate_images(alloc, count, indices);
        return BIND_IMAGE_FAIL;
    } else {
        simd_update_flags_u8(alloc->allocation_count, alloc->allocation_states, TO_UPLOAD, 0x0, UPLOADED, TO_UPLOAD);
    }

    // Record copies and transitions
    VkCommandBufferAllocateInfo cmd_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc_info.commandPool = alloc->graphics_pool;
    cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    cmd_alloc_info.commandBufferCount = 1;

    auto check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->graphics_cmd);
    DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

    u32 transfer_queue_idx = gpu->transfer_queue_index;
    u32 graphics_queue_idx = gpu->graphics_queue_index;

    if (transfer_queue_idx != graphics_queue_idx) {
        cmd_alloc_info.commandPool = alloc->transfer_pool;
        check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->transfer_cmd);
        DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);
    }

    VkCommandBufferInheritanceInfo inheritance = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = &inheritance;

    VkImageMemoryBarrier2 *barrs =
        (VkImageMemoryBarrier2*)malloc_t(sizeof(VkImageMemoryBarrier2) * alloc->to_upload_count, 8);

    // Transition for buffer copy
    for(u32 i = 0; i < count; ++i) {
        tmp = &alloc->allocations[indices[i]];
        barrs[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrs[i].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        barrs[i].dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        barrs[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrs[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrs[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrs[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrs[i].image = tmp->img;
        barrs[i].subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
    }

    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = alloc->to_upload_count;
    dep.pImageMemoryBarriers = barrs;

    VkBufferImageCopy2 region = {VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_2};
    VkCopyBufferToImageInfo2 copy_info = {VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_2};
    copy_info.srcBuffer = alloc->stage;
    copy_info.regionCount = 1; // Can I store multiple textures in one image??
    copy_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copy_info.pRegions = &region;

    // if NOT discrete transfer queue (static predict discrete transfer)
    if (gpu->memory.flags & GPU_MEMORY_DISCRETE_TRANSFER_BIT == 0) {
        vkBeginCommandBuffer(alloc->graphics_cmd, &begin_info);

        vkCmdPipelineBarrier2(alloc->graphics_cmd, &dep);
        for(u32 i = 0; i < count; ++i) {
            tmp = &alloc->allocations[indices[i]];
            region.bufferOffset = tmp->stage_offset;
            region.bufferRowLength = tmp->width;
            region.bufferImageHeight = tmp->height;
            region.subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            region.imageOffset = {.x = 0, .y = 0, .z = 0};
            region.imageExtent = {.width = tmp->width, .height = tmp->height, .depth = 1};
            copy_info.dstImage = tmp->image;
            vkCmdCopyBufferToImage2(alloc->graphics_cmd, &copy_info);
        }
        // Transition for reading in shader
        for(u32 i = 0; i < alloc->to_upload_count; ++i) {
            barrs[i].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
            barrs[i].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
            barrs[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
            barrs[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR;
            barrs[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs[i].newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        }
        // Shader read transition
        vkCmdPipelineBarrier2(alloc->graphics_cmd, &dep);

        vkEndCommandBuffer(alloc->graphics_cmd);
    } else {
        vkBeginCommandBuffer(alloc->transfer_cmd, &begin_info);

        vkCmdPipelineBarrier2(alloc->transfer_cmd, &dep);
        for(u32 i = 0; i < count; ++i) {
            tmp = &alloc->allocations[indices[i]];
            region.bufferOffset = tmp->stage_offset;
            region.bufferRowLength = tmp->width;
            region.bufferImageHeight = tmp->height;
            region.subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = 0,
                .layerCount = 1,
            };
            region.imageOffset = {.x = 0, .y = 0, .z = 0};
            region.imageExtent = {.width = tmp->width, .height = tmp->height, .depth = 1};
            copy_info.dstImage = tmp->image;
            vkCmdCopyBufferToImage2(alloc->transfer_cmd, &copy_info);
        }
        // Transition for reading in shader
        for(u32 i = 0; i < alloc->to_upload_count; ++i) {
            barrs[i].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
            barrs[i].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
            barrs[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs[i].newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            barrs[i].srcQueueFamilyIndex = gpu->transfer_queue_idx;
            barrs[i].dstQueueFamilyIndex = gpu->graphics_queue_idx;
        }
        // Shader read transition
        vkCmdPipelineBarrier2(alloc->transfer_cmd, &dep);

        vkEndCommandBuffer(alloc->transfer_cmd);

        vkBeginCommandBuffer(alloc->graphics_cmd, &begin_info);

        // Transition for reading in shader
        for(u32 i = 0; i < alloc->to_upload_count; ++i) {
            barrs[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
            barrs[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR;
            barrs[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs[i].newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            barrs[i].srcQueueFamilyIndex = gpu->transfer_queue_idx;
            barrs[i].dstQueueFamilyIndex = gpu->graphics_queue_idx;
        }
        vkCmdPipelineBarrier2(alloc->graphics_cmd, &dep);

        vkEndCommandBuffer(alloc->graphics_cmd);
    }
    // @Note Potentially premature but justified (see equivalent note in regular allocator)
    for(u32 i = 0; i < count; ++i) {
        alloc->allocations[indices[i]] &= ~TO_UPLOAD;
        alloc->allocations[indices[i]] |= UPLOADED;
    }

    alloc->to_upload_count = Max_u32;
    return SUCCESS;
}
    /* Model Texture and Vertex/Index Attribute Allocators */
    /* Sampler Allocator */
Sampler_Allocator create_sampler_allocator(u32 cap) {
    Sampler_Allocator ret = {};

    u64 device_cap = get_gpu_instance()->info.props.limits.maxSamplerAllocationCount;
    if (cap)
        ret.cap = align(cap, 16);
    else
        ret.cap = align(device_cap, 16); // malloc'd size must be aligned to 16 for correct_weights() simd

    ret.device_cap = device_cap;
    ret.map = HashMap<u64, Sampler>::get(ret.cap);

    // Align 16 for SIMD
    ret.hashes = (u64*)malloc_h(sizeof(u64) * ret.cap, 16);
    ret.weights = malloc_h(ret.cap, 16);
    memset(ret.weights, 0, ret.cap);

    return ret;
}
void destroy_sampler_allocator(Sampler_Allocator *alloc) {
    free_h(alloc->hashes);
    free_h(alloc->weights);
    alloc->map.kill();
}
/*
   Sampler Map Implementation:

   Keep a list of keys and weights. The weights are sorted from highest to lowest. When a sampler is add to the
   map, if a sampler which matches it is already in the map, then its weight is increased (in order to reflect
   how commonly this sampler is referenced by models). When a sampler is called up with get_sampler(), its weight
   is increased, and all other weights are decreased (prevent all weights slowly becoming max). After these two
   operations, the weight is moved to its appropriate place in the array. The corresponding key is then also
   moved to corresponding position in its array.

   If the number of active samplers is equivalent to the device's active sampler capacity, and the sampler being
   requested is inactive, the sampler with the lowest weight in the weights array which also contains an active
   sampler has its top bit cleared (the rest of the bits are preserved) to reflect its inactive status. A new sampler
   can then be created, and this weight is marked as having an active sampler linked to it (its top bit is set).

*/
u64 add_sampler(Sampler_Allocator *alloc, Sampler *sampler_info) {
    //
    // @Note Ik that the hash will change when the sampler handle in the 'Sampler' type
    // changes, but calling 'insert_hash()' doesnt actually do a rehash, so the hash that the
    // sampler is inserted with will always be its key.
    //
    sampler_info->sampler = NULL; // Ensure only type data influences hash, not the handle

    u64 hash = hash_bytes(sampler_info, sizeof(Sampler));
    u32 h_idx = find_hash_idx(alloc->count, alloc->hashes, hash);
    if (h_idx != Max_u32) {
        correct_weights(alloc->count, alloc->weights, alloc->hashes, h_idx, 1, 0);
        return hash;
    }

    ASSERT(alloc->count <= alloc->cap, "");
    if (alloc->count >= alloc->cap)
        return Max_u64;

    alloc->map.insert_hash(hash, sampler_info);
    alloc->hashes[alloc->count] = hash;
    alloc->count++;

    return hash;
}
VkSampler get_sampler(Sampler_Allocator *alloc, u64 hash) {
    u32 h_idx = find_hash_idx(alloc->count, alloc->hashes, hash);

    ASSERT(h_idx != Max_u32, "Invalid Sampler Hash");
    if (h_idx == Max_u32)
        return NULL;

    correct_weights(alloc->count, alloc->weights, alloc->hashes, h_idx, 5, 1);
    Sampler *info = alloc->map.find_hash(hash);
    float anisotropy = get_global_settings()->anisotropy;
    if (!info->sampler) {
        VkSamplerCreateInfo create_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        create_info.magFilter = info->mag_filter;
        create_info.minFilter = info->min_filter;
        create_info.addressModeU = info->wrap_s;
        create_info.addressModeV = info->wrap_t;
        create_info.anisotropyEnable = anisotropy > 0 ? VK_TRUE : VK_FALSE;
        create_info.maxAnisotropy = anisotropy;

        VkDevice device = get_gpu_instance()->device;
        if (alloc->active == alloc->device_cap) {
            u32 evict_idx = find_lowest_flagged_weight(alloc->count, alloc->weights);
            alloc->weights[evict_idx] &= 0b01111111;
            VkSampler *to_evict = &alloc->map.find_hash(alloc->hashes[evict_idx])->sampler;
            vkDestroySampler(device, *to_evict, ALLOCATION_CALLBACKS);
            *to_evict = NULL;
            alloc->active--;
        }
        auto check = vkCreateSampler(device, &create_info, ALLOCATION_CALLBACKS, &info->sampler);
        DEBUG_OBJ_CREATION(vkCreateSampler, check);
        alloc->active++;
    }

    return info->sampler;
}
        /* End Sampler Allocation */

            /* Model Loading */
Model_Allocators init_allocators() {
    Gpu *gpu = get_gpu_instance();

    Allocator_Create_Info allocator_info = {};
    allocator_info.stage_cap = gpu::VERTEX_STAGE_SIZE;
    allocator_info.upload_cap = gpu::VERTEX_DEVICE_SIZE;
    allocator_info.stage = gpu->memory.vertex_bufs_stage[0];
    allocator_info.stage_ptr = gpu->memory.vert_ptrs[0];
    allocator_info.upload = gpu->memory.vertex_buf_device;
    allocator_info.bit_granularity = 256;
    allocator_info.alloc_cap = 128;

    Allocator vertex_allocator = create_allocator(&allocator_info);

    allocator_info.stage_cap = gpu::INDEX_STAGE_SIZE;
    allocator_info.upload_cap = gpu::INDEX_DEVICE_SIZE;
    allocator_info.stage = gpu->memory.index_bufs_stage[0];
    allocator_info.stage_ptr = gpu->memory.index_ptrs[0];
    allocator_info.upload = gpu->memory.index_buf_device;
    Allocator index_allocator = create_allocator(&allocator_info);

    Tex_Allocator_Create_Info tex_allocator_info = {};
    tex_allocator_info.allocation_cap = 256;
    tex_allocator_info.stage_byte_cap = gpu::TEXTURE_STAGE_SIZE;
    tex_allocator_info.upload_byte_cap = gpu::TEXTURE_DEVICE_SIZE;
    tex_allocator_info.stage = gpu->memory.texture_bufs_stage[0];
    tex_allocator_info.stage_ptr = gpu->memory.tex_ptrs[0];
    tex_allocator_info.upload = gpu->memory.texture_mem_device;

    tex_allocator_info.stage_bit_granularity = 256;
    tex_allocator_info.upload_bit_granularity = 256;

    tex_allocator_info.to_stage_allocation_cap =  128;
    tex_allocator_info.to_upload_allocation_cap = 128;;

    Tex_Allocator tex_allocator = create_tex_allocator(&tex_allocator_info);

    Sampler_Allocator sampler = create_sampler_allocator(0);

    Model_Allocators ret = {
        .index = index_allocator,
        .vert = vertex_allocator,
        .tex = tex_allocator,
        .sampler = sampler,
    };

    return ret;
}
void shutdown_allocators(Model_Allocators *allocs) {
    destroy_allocator(&allocs->index);
    destroy_allocator(&allocs->vert);
    destroy_tex_allocator(&allocs->tex);
    destroy_sampler_allocator(&allocs->sampler);
}

enum class Data_Type : u32 {
    NONE = 0,
    VERTEX = 1,
    INDEX = 2,
    UNIFORM = 3,
};
struct Buffer_View {
    u64 offset;
    Data_Type type;
};
// @Todo load_skinned_models(); <- this will also need to load animations
Static_Model load_static_model(Model_Allocators *allocs, String *model_name, String *dir) {
    u64 mark = get_mark_temp();

    String tmp_uri;
    char uri_buffer[127];
    memcpy(uri_buffer, dir->str, dir->len);
    memcpy(&uri_buffer[0] + dir->len, model_name->str, model_name->len);
    uri_buffer[dir->len + model_name->len] = '\0';

    Gltf gltf = parse_gltf(uri_buffer);

    Gltf_Mesh *gltf_mesh;
    Gltf_Mesh_Primitive *gltf_prim;
    Gltf_Accessor *accessor;

    u32 accessor_count = gltf_accessor_get_count(&gltf);
    u32 view_count = gltf_buffer_view_get_count(&gltf);
    u32 node_count = gltf_node_get_count(&gltf);
    u32 mat_count = gltf_material_get_count(&gltf);
    u32 mesh_count = gltf_mesh_get_count(&gltf);
    u32 prim_count = gltf.total_primitive_count;

    Static_Model ret = {};

    ret.node_count = node_count;
    ret.mesh_count = mesh_count;
    ret.mat_count = mat_count;

    //ret.nodes = (Node*)malloc_h(sizeof(Node) * node_count, 8); @Unused
    ret.meshes = (Mesh*)malloc_h(sizeof(Mesh) * mesh_count, 8);
    ret.mats = (Material*)malloc_h(sizeof(Material) * mat_count, 8);

    ret.meshes[0].primitives = (Primitive*)malloc_h(sizeof(Primitive) * prim_count, 8);
    ret.meshes[0].pl_infos = (Pl_Prim_Info*)malloc_h(sizeof(Pl_Prim_Info) * prim_count, 8);

    // Allow summing offset
    memset(ret.meshes[0].primitives, 0, sizeof(Primitive) * prim_count);
    memset(ret.meshes[0].pl_infos, 0, sizeof(Pl_Prim_Info) * prim_count);

    /*
        Vertex Attribute Load method:
            1. Loop primitives - mark data
            2. Loop buffer views - load data
            3. Loop primitives - set offsets
    */

    Buffer_View *views = (Buffer_View*)malloc_t(sizeof(Buffer_View) * view_count, 8);
    memset(views, 0, sizeof(Buffer_View) * view_count); // ensure type is not wrongly matched
    u32 *view_indices = (u32*)malloc_t(sizeof(u32) * accessor_count, 8); // no more deref accessors

    Primitive *prim;
    Pl_Prim_Info *pl_info;

    // 1. Loop primitives - mark data
    gltf_mesh = gltf.meshes;
    u32 prim_track = 0;
    for(u32 i = 0; i < mesh_count; ++i) {
        gltf_prim = gltf_mesh->primitives;

        ret.meshes[i].count = gltf_mesh->primitive_count;
        ret.meshes[i].primitives = ret.meshes[0].primitives + prim_track;
        ret.meshes[i].pl_infos = ret.meshes[0].pl_infos + prim_track;

        prim_track += ret.meshes[i].count;
        for(u32 j = 0; j < gltf_mesh->primitive_count; ++j) {
            prim = &ret.meshes[i].primitives[j];
            pl_info = &ret.meshes[i].pl_infos[j];

            // Set Index
            accessor = gltf_accessor_by_index(&gltf, gltf_prim->indices);

            view_indices[gltf_prim->indices] = accessor->buffer_view;
            views[accessor->buffer_view].type = Data_Type::INDEX;

            prim->offset_index = accessor->byte_offset;
            prim->count = accessor->count;
            prim->material = &ret.mats[gltf_prim->material];

            switch(accessor->format) {
            case GLTF_ACCESSOR_FORMAT_SCALAR_U16:
                prim->index_type = VK_INDEX_TYPE_UINT16;
                break;
            case GLTF_ACCESSOR_FORMAT_SCALAR_U32:
                prim->index_type = VK_INDEX_TYPE_UINT32;
                break;
            default:
                ASSERT(false, "Invalid Index Type");
            }

            if (gltf_prim->position != -1) {
                accessor = gltf_accessor_by_index(&gltf, gltf_prim->position);

                prim->offset_pos = accessor->byte_offset;

                pl_info->strides[0] = get_accessor_byte_stride(accessor->format);
                pl_info->formats[0] = (VkFormat)accessor->format;

                view_indices[gltf_prim->position] = accessor->buffer_view;
                views[accessor->buffer_view].type = Data_Type::VERTEX;
            }
            if (gltf_prim->normal != -1) {
                accessor = gltf_accessor_by_index(&gltf, gltf_prim->normal);

                prim->offset_norm = accessor->byte_offset;

                pl_info->strides[1] = get_accessor_byte_stride(accessor->format);
                pl_info->formats[1] = (VkFormat)accessor->format;

                view_indices[gltf_prim->normal] = accessor->buffer_view;
                views[accessor->buffer_view].type = Data_Type::VERTEX;
            }
            if (gltf_prim->tangent != -1) {
                accessor = gltf_accessor_by_index(&gltf, gltf_prim->tangent);
                prim->offset_tang = accessor->byte_offset;

                pl_info->strides[2] = get_accessor_byte_stride(accessor->format);
                pl_info->formats[2] = (VkFormat)accessor->format;

                view_indices[gltf_prim->tangent] = accessor->buffer_view;
                views[accessor->buffer_view].type = Data_Type::VERTEX;
            }
            if (gltf_prim->tex_coord_0 != -1) {
                accessor = gltf_accessor_by_index(&gltf, gltf_prim->tex_coord_0);
                prim->offset_tex = accessor->byte_offset;

                pl_info->strides[3] = get_accessor_byte_stride(accessor->format);
                pl_info->formats[3] = (VkFormat)accessor->format;

                view_indices[gltf_prim->tex_coord_0] = accessor->buffer_view;
                views[accessor->buffer_view].type = Data_Type::VERTEX;
            }

            gltf_prim = (Gltf_Mesh_Primitive*)((u8*)gltf_prim + gltf_prim->stride);
        }
        gltf_mesh = (Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }

    // 2. Loop buffer views - load data
    ASSERT(gltf_buffer_get_count(&gltf) == 1, "Too Many Buffers");
    Gltf_Buffer *gltf_buf = gltf.buffers;

    memcpy(uri_buffer, dir->str, dir->len);
    strcpy(&uri_buffer[0] + dir->len, gltf_buf->uri);

    const u8 *buf = file_read_bin_temp_large(uri_buffer, gltf_buf->byte_length);

    Gltf_Buffer_View *gltf_view = gltf.buffer_views;
    void *ptr;
    staging_queue_begin(&allocs->index);
    staging_queue_begin(&allocs->vert);
    for(u32 i = 0; i < view_count; ++i) {
    // This switch seems lame, but in reality gltf views are likely packed by type, so it will be predicted.
        switch(views[i].type) {
        case Data_Type::VERTEX:
            ptr = staging_queue_add(&allocs->vert, gltf_view->byte_length, &views[i].offset);
            memcpy(ptr, buf + gltf_view->byte_offset, gltf_view->byte_length);
            break;
        case Data_Type::INDEX:
            ptr = staging_queue_add(&allocs->index, gltf_view->byte_length, &views[i].offset);
            memcpy(ptr, buf + gltf_view->byte_offset, gltf_view->byte_length);
            break;
        case Data_Type::NONE:
            break;
        case Data_Type::UNIFORM:
            ASSERT(false, "No Uniform Data Allowed In Static Model");
            break;
        default:
            ASSERT(false, "Invalid Buffer View Type");
        }

        gltf_view = (Gltf_Buffer_View*)((u8*)gltf_view + gltf_view->stride);
    }
    ret.index_allocation = staging_queue_submit(&allocs->index);
    ret.vert_allocation = staging_queue_submit(&allocs->vert);

    // 3. Loop primitives - set offsets
    gltf_mesh = gltf.meshes;
    for(u32 i = 0; i < mesh_count; ++i) {
        gltf_prim = gltf_mesh->primitives;
        for(u32 j = 0; j < gltf_mesh->primitive_count; ++j) {

            //
            // Previously acquired the offsets of buffer views into their respective allocation.
            // Now add these offsets to the offsets of the primitives into their respective
            // buffer view; this gives the total offset of the primitive data into the
            // model's allocation (vertex or index allocation).
            //

            ret.meshes[i].primitives[j].offset_index +=
                views[view_indices[gltf_prim->indices]].offset;

            if (gltf_prim->position != -1)
                ret.meshes[i].primitives[j].offset_pos +=
                    views[view_indices[gltf_prim->position]].offset;

            if (gltf_prim->normal != -1)
                ret.meshes[i].primitives[j].offset_norm +=
                    views[view_indices[gltf_prim->normal]].offset;

            if (gltf_prim->tangent != -1)
                ret.meshes[i].primitives[j].offset_tang +=
                    views[view_indices[gltf_prim->tangent]].offset;

            if (gltf_prim->tex_coord_0 != -1)
                ret.meshes[i].primitives[j].offset_tex +=
                    views[view_indices[gltf_prim->tex_coord_0]].offset;

            gltf_prim = (Gltf_Mesh_Primitive*)((u8*)gltf_prim + gltf_prim->stride);
        }

        gltf_mesh = (Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }

    #if 1
    // Load Material Data
    Gltf_Material *gltf_mat = gltf.materials;
    Gltf_Texture *gltf_tex;
    Gltf_Sampler *gltf_sampler;
    Gltf_Image *gltf_image;
    Sampler sampler_info;
    for(u32 i = 0; i < mat_count; ++i) {

        ret.mats[i].base_factors[0] = gltf_mat->base_color_factor[0];
        ret.mats[i].base_factors[1] = gltf_mat->base_color_factor[1];
        ret.mats[i].base_factors[2] = gltf_mat->base_color_factor[2];
        ret.mats[i].base_factors[3] = gltf_mat->base_color_factor[3];

        ret.mats[i].emissive_factors[0] = gltf_mat->emissive_factor[0];
        ret.mats[i].emissive_factors[1] = gltf_mat->emissive_factor[1];
        ret.mats[i].emissive_factors[2] = gltf_mat->emissive_factor[2];

        ret.mats[i].metal_factor = gltf_mat->metallic_factor;
        ret.mats[i].rough_factor = gltf_mat->roughness_factor;
        ret.mats[i].norm_scale = gltf_mat->normal_scale;
        ret.mats[i].occlusion_strength = gltf_mat->occlusion_strength;

        //
        // @Todo alpha settings
        //

        // *** Older comment: ***
        // Texture method:
        //     Add textures to an array.
        //     Material stores the index to the texture. As textures are quite expensive,
        //     I think it will be sensible to store and manage them elsewhere: I imagine
        //     that I can store vertex data indefinitely in memory, but texture data would
        //     likely often need to be streamed from disk and cached well etc. and the model
        //     allocator system does not facilitate this super well.
        //
        //     I like the vertex data being handled by the model fine, but textures should
        //     work differently.
        memcpy(uri_buffer, dir->str, dir->len);


        // base
        if (gltf_mat->base_color_texture_index != -1) {
            gltf_tex = gltf_texture_by_index(&gltf, gltf_mat->base_color_texture_index);
            gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
            gltf_image = gltf_image_by_index(&gltf, gltf_tex->source_image);

            strcpy(&uri_buffer[0] + dir->len, gltf_image->uri); // @Todo update the gltf uris to use String type
            tmp_uri = cstr_to_string((const char*)uri_buffer);

            sampler_info.wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
            sampler_info.wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
            sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
            sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;
            ret.mats[i].tex_base.sampler_key = add_sampler(&allocs->sampler, &sampler_info);
            ret.mats[i].tex_base.allocation = tex_add(&allocs->tex, &tmp_uri);
        }


        // metallic roughness
        if (gltf_mat->metallic_roughness_texture_index != -1) {
            gltf_tex = gltf_texture_by_index(&gltf, gltf_mat->metallic_roughness_texture_index);
            gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
            gltf_image = gltf_image_by_index(&gltf, gltf_tex->source_image);

            strcpy(&uri_buffer[0] + dir->len, gltf_image->uri);
            tmp_uri = cstr_to_string((const char*)uri_buffer);

            sampler_info.wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
            sampler_info.wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
            sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
            sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;
            ret.mats[i].tex_pbr.sampler_key = add_sampler(&allocs->sampler, &sampler_info);
            ret.mats[i].tex_pbr.allocation = tex_add(&allocs->tex, &tmp_uri);
        }


        // normal
        if (gltf_mat->normal_texture_index != -1) {
            gltf_tex = gltf_texture_by_index(&gltf, gltf_mat->normal_texture_index);
            gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
            gltf_image = gltf_image_by_index(&gltf, gltf_tex->source_image);

            strcpy(&uri_buffer[0] + dir->len, gltf_image->uri);
            tmp_uri = cstr_to_string((const char*)uri_buffer);

            sampler_info.wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
            sampler_info.wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
            sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
            sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;
            ret.mats[i].tex_norm.sampler_key = add_sampler(&allocs->sampler, &sampler_info);
            ret.mats[i].tex_norm.allocation = tex_add(&allocs->tex, &tmp_uri);
        }


        // occlusion
        if (gltf_mat->occlusion_texture_index != -1) {
            gltf_tex = gltf_texture_by_index(&gltf, gltf_mat->occlusion_texture_index);
            gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
            gltf_image = gltf_image_by_index(&gltf, gltf_tex->source_image);

            strcpy(&uri_buffer[0] + dir->len, gltf_image->uri);
            tmp_uri = cstr_to_string((const char*)uri_buffer);

            sampler_info.wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
            sampler_info.wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
            sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
            sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;
            ret.mats[i].tex_occlusion.sampler_key = add_sampler(&allocs->sampler, &sampler_info);
            ret.mats[i].tex_occlusion.allocation = tex_add(&allocs->tex, &tmp_uri);
        }


        // emissive
        if (gltf_mat->emissive_texture_index != -1) {
            gltf_tex = gltf_texture_by_index(&gltf, gltf_mat->emissive_texture_index);
            gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
            gltf_image = gltf_image_by_index(&gltf, gltf_tex->source_image);

            strcpy(&uri_buffer[0] + dir->len, gltf_image->uri);
            tmp_uri = cstr_to_string((const char*)uri_buffer);

            sampler_info.wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
            sampler_info.wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
            sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
            sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;
            ret.mats[i].tex_emissive.sampler_key = add_sampler(&allocs->sampler, &sampler_info);
            ret.mats[i].tex_emissive.allocation = tex_add(&allocs->tex, &tmp_uri);
        }

        gltf_mat = (Gltf_Material*)((u8*)gltf_mat + gltf_mat->stride);
    }
    #endif

    reset_to_mark_temp(mark);
    return ret;
}
void free_static_model(Static_Model *model) {
    free_h(model->meshes[0].primitives);
    free_h(model->meshes[0].pl_infos);
    free_h(model->meshes);
    free_h(model->mats);
    //free_h(model->nodes);
}
// functions like this are such a waste of time to write...
u32 get_accessor_byte_stride(Gltf_Accessor_Format accessor_format) {
        switch(accessor_format) {
            case GLTF_ACCESSOR_FORMAT_SCALAR_U8:
            case GLTF_ACCESSOR_FORMAT_SCALAR_S8:
                return 1;

            case GLTF_ACCESSOR_FORMAT_VEC2_U8:
            case GLTF_ACCESSOR_FORMAT_VEC2_S8:
                return 2;

            case GLTF_ACCESSOR_FORMAT_VEC3_U8:
            case GLTF_ACCESSOR_FORMAT_VEC3_S8:
                return 3;

            case GLTF_ACCESSOR_FORMAT_VEC4_U8:
            case GLTF_ACCESSOR_FORMAT_VEC4_S8:
                return 4;

            case GLTF_ACCESSOR_FORMAT_SCALAR_U16:
            case GLTF_ACCESSOR_FORMAT_SCALAR_S16:
            case GLTF_ACCESSOR_FORMAT_SCALAR_FLOAT16:
                return 2;

            case GLTF_ACCESSOR_FORMAT_VEC2_U16:
            case GLTF_ACCESSOR_FORMAT_VEC2_S16:
            case GLTF_ACCESSOR_FORMAT_VEC2_FLOAT16:
                return 4;

            case GLTF_ACCESSOR_FORMAT_VEC3_U16:
            case GLTF_ACCESSOR_FORMAT_VEC3_S16:
            case GLTF_ACCESSOR_FORMAT_VEC3_FLOAT16:
                return 6;

            case GLTF_ACCESSOR_FORMAT_VEC4_U16:
            case GLTF_ACCESSOR_FORMAT_VEC4_S16:
            case GLTF_ACCESSOR_FORMAT_VEC4_FLOAT16:
                 return 8;

            case GLTF_ACCESSOR_FORMAT_SCALAR_U32:
            case GLTF_ACCESSOR_FORMAT_SCALAR_S32:
            case GLTF_ACCESSOR_FORMAT_SCALAR_FLOAT32:
                return 4;

            case GLTF_ACCESSOR_FORMAT_VEC2_U32:
            case GLTF_ACCESSOR_FORMAT_VEC2_S32:
            case GLTF_ACCESSOR_FORMAT_VEC2_FLOAT32:
                return 8;

            case GLTF_ACCESSOR_FORMAT_VEC3_U32:
            case GLTF_ACCESSOR_FORMAT_VEC3_S32:
            case GLTF_ACCESSOR_FORMAT_VEC3_FLOAT32:
                return 12;

            case GLTF_ACCESSOR_FORMAT_VEC4_U32:
            case GLTF_ACCESSOR_FORMAT_VEC4_S32:
            case GLTF_ACCESSOR_FORMAT_VEC4_FLOAT32:
                return 16;

            case GLTF_ACCESSOR_FORMAT_MAT2_U8:
            case GLTF_ACCESSOR_FORMAT_MAT2_S8:
                return 4;

            case GLTF_ACCESSOR_FORMAT_MAT3_U8:
            case GLTF_ACCESSOR_FORMAT_MAT3_S8:
                return 9;

            case GLTF_ACCESSOR_FORMAT_MAT4_U8:
            case GLTF_ACCESSOR_FORMAT_MAT4_S8:
                return 16;

            case GLTF_ACCESSOR_FORMAT_MAT2_U16:
            case GLTF_ACCESSOR_FORMAT_MAT2_S16:
            case GLTF_ACCESSOR_FORMAT_MAT2_FLOAT16:
                return 8;

            case GLTF_ACCESSOR_FORMAT_MAT3_U16:
            case GLTF_ACCESSOR_FORMAT_MAT3_S16:
            case GLTF_ACCESSOR_FORMAT_MAT3_FLOAT16:
                return 18;

            case GLTF_ACCESSOR_FORMAT_MAT4_U16:
            case GLTF_ACCESSOR_FORMAT_MAT4_S16:
            case GLTF_ACCESSOR_FORMAT_MAT4_FLOAT16:
                return 32;

            case GLTF_ACCESSOR_FORMAT_MAT2_U32:
            case GLTF_ACCESSOR_FORMAT_MAT2_S32:
            case GLTF_ACCESSOR_FORMAT_MAT2_FLOAT32:
                return 16;

            case GLTF_ACCESSOR_FORMAT_MAT3_U32:
            case GLTF_ACCESSOR_FORMAT_MAT3_S32:
            case GLTF_ACCESSOR_FORMAT_MAT3_FLOAT32:
                return 36;

            case GLTF_ACCESSOR_FORMAT_MAT4_U32:
            case GLTF_ACCESSOR_FORMAT_MAT4_S32:
            case GLTF_ACCESSOR_FORMAT_MAT4_FLOAT32:
                return 64;

            default:
                ASSERT(false, "Invalid Accessor Format");
                return Max_u32;
        }
}

} // namespace model
#if DEBUG
VkDebugUtilsMessengerEXT create_debug_messenger(Create_Debug_Messenger_Info *info) {
    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info = fill_vk_debug_messenger_info(info);

    VkDebugUtilsMessengerEXT debug_messenger;
    auto check = CreateDebugUtilsMessengerEXT(info->instance, &debug_messenger_create_info, NULL, &debug_messenger);

    DEBUG_OBJ_CREATION(vkCreateDebugUtilsMessengerEXT, check)
    return debug_messenger;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}
void DestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT messenger,
        const VkAllocationCallbacks *pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr)
        return func(instance, messenger, pAllocator);
}
#endif
} // namespace Gpu
