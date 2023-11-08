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

/*
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
static void adjust_allocation_weights(Weight_Args *args) {
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
    return;
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
static u32 find_contiguous_free(u32 count, u64 *bits, u32 offset, u32 req_count) {
    u32 restore = bits[offset >> 6];
    u64 mask = 0x01;
    mask <<= offset & 63;
    bits[offset >> 6] |= mask - 1;

    u32 tz = 0;
    u32 inc = (offset >> 6) << 6;
    u32 tail = 0;
    u32 shift = 0;

    for(u32 i = offset >> 6; i < count; ++i)
        tz += pop_count64(~bits[i]);

    if (tz < req_count)
        goto not_found;

    for(u32 i = offset >> 6; i < count; ++i) {
        if (bits[i] == 0) {
            tail += 64;
            if (tail >= req_count)
                goto found;
            else
                continue;
        } else if (bits[i] == Max_u64) {
            inc += 64;
            continue;
        }

        mask = bits[i];
        tz = count_trailing_zeros_u64(mask);
        if (tz + tail >= req_count)
            goto found;

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
                goto found;

            mask >>= tz;
            shift += tz;
            inc += tz;

            tz = count_trailing_zeros_u64(~mask);
            mask >>= tz;
            shift += tz;
            inc += tz;
        }
    }

    not_found: // goto label
    bits[offset >> 6] = restore;
    return Max_u32;

    found: // goto label
    bits[offset >> 6] = restore;
    return inc;
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
void correct_weights(u32 count, u8 *weights, u64 *hashes, u32 idx, u32 inc, u32 dec) {
    //
    // @Todo Add a little simd thing to search for a contiguous block of equal weights:
    // It will almost certainly be the case that there will be loads of zero weights
    // at the end of the array, and so when you load one of these, there has to be loads
    // of pointless swaps, when you can just do one swap. I cba to add it right now...
    //
    u8 flag_bit = weights[idx] & 0b10000000;
    weights[idx] &= 0b01111111;
    if ((weights[idx] + inc + dec) <= 127)
        weights[idx] += inc + dec;
    else
        weights[idx] = 127;
    weights[idx] |= flag_bit;

    // @Note Clamps like this should compile without branches, but if there is a way
    // to do this without the 'if' (just to always be sure) I would like that...
    for(u32 i = 0; i < count; ++i) {
        flag_bit = weights[i] & 0b10000000;
        weights[i] &= 0b01111111;
        if ((weights[i] - dec) > weights[i])
            weights[i] = 0;
        else
            weights[i] -= dec;
        weights[i] |= flag_bit;
    }

    // Find the index where the weight should be
    // @Note higher weights exist closer to the beginning of the list, so loop backwards
    u32 shift = idx & 15;
    u32 pos = idx - shift;
    __m128i a = _mm_load_si128((__m128i*)(weights + pos));
    __m128i b = _mm_set1_epi8(weights[idx]);
    __m128i c = _mm_set1_epi8(0b01111111);
    a = _mm_and_si128(a, c); // flag bit irrelevant cmping weights
    a = _mm_cmplt_epi8(a, b);
    u16 mask = _mm_movemask_epi8(a);
    mask <<= 16 - shift;
    u32 new_idx = idx;
    new_idx -= pop_count16(mask);
    while(mask && pos) {
        pos -= 16;
        a = _mm_load_si128((__m128i*)(weights + pos));
        a = _mm_and_si128(a, c);
        a = _mm_cmplt_epi8(a, b);
        mask = _mm_movemask_epi8(a);
        new_idx -= pop_count16(mask);
    }

    // Shift all lt weights towards the end of the list
    u8 w = weights[idx];
    for(u32 i = idx - 1; i >= new_idx && i != Max_u32; --i)
        weights[i + 1] = weights[i];
    weights[new_idx] = w;

    // Repeat weights for hashes
    u64 h = hashes[idx];
    for(u32 i = idx - 1; i >= new_idx && i != Max_u32; --i)
        hashes[i + 1] = hashes[i];
    hashes[new_idx] = h;
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
    u32 shift = count & 15;
    u32 pos = count - shift;
    __m128i a = _mm_load_si128((__m128i*)(weights + pos));
    __m128i b = _mm_set1_epi8(0b10000000);
    a = _mm_and_si128(a, b);
    u16 mask = _mm_movemask_epi8(a);
    mask <<= 16 - shift; // Just in case weights beyond count are not zero'd
    while(!mask) {
        if (pos == 0)
            return Max_u32;
        pos -= 16;

        a = _mm_load_si128((__m128i*)(weights + pos));
        a = _mm_and_si128(a, b);
        mask = _mm_movemask_epi8(a);
    }
    return pos + (15 - count_leading_zeros_u16(mask)); // 15 not 16 because leading zeros not inclusive
}
        /* End Allocator Helper Algorithms */

        /* Implement Vertex Attribute Allocator */
Allocator create_allocator(Allocator_Create_Info *info) {
    ASSERT(info->upload_cap % (info->bit_granularity * 64) == 0,
            "upload_cap \% bit_granularity * 64 must be equivalent to 0");

    Allocator ret = {};
    ret.stage_cap = info->stage_cap;
    ret.upload_cap = info->upload_cap;
    ret.stage = info->stage;
    ret.stage_ptr = info->stage_ptr;
    ret.upload = info->upload;

    ret.alloc_cap = info->alloc_cap;
    ret.allocs = (Allocation*)malloc_h(sizeof(Allocation) * ret.alloc_cap, 8);

    ret.bit_granularity = info->bit_granularity;

    u64 c = ret.upload_cap;
    u64 g = ret.bit_granularity;
    ret.mask_count = c / (g * 64);
    ret.masks = (u64*)malloc_h(sizeof(u64) * ret.mask_count, 8);
    memset(ret.masks, 0, sizeof(u64) * ret.mask_count);

    Gpu *gpu = get_gpu_instance();
    // Choose the most optimal alignment
    if (gpu->info.props.limits.nonCoherentAtomSize % gpu->info.props.limits.optimalBufferCopyOffsetAlignment == 0 ||
        gpu->info.props.limits.optimalBufferCopyOffsetAlignment % gpu->info.props.limits.nonCoherentAtomSize == 0)
    {
        if (gpu->info.props.limits.nonCoherentAtomSize > gpu->info.props.limits.optimalBufferCopyOffsetAlignment)
            ret.alignment = gpu->info.props.limits.nonCoherentAtomSize;
        else
            ret.alignment = gpu->info.props.limits.optimalBufferCopyOffsetAlignment;
    }
    ret.staging_queue = Max_u64;

    if (gpu->info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        ret.flags |= (u8)Flags::UNIFIED_MEM;
        // Could early return here, as the transfer queue info is not needed if memory
        // is unified...
    }

    VkCommandPoolCreateInfo cmd_pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    cmd_pool_info.queueFamilyIndex = gpu->graphics_queue_index;
    auto check = vkCreateCommandPool( gpu->device, &cmd_pool_info, ALLOCATION_CALLBACKS, &ret.graphics_pool);
    DEBUG_OBJ_CREATION(vkCreateCommandPool, check);

    if (gpu->transfer_queue_index == gpu->graphics_queue_index) {
        cmd_pool_info.queueFamilyIndex = gpu->transfer_queue_index;
        check = vkCreateCommandPool( gpu->device, &cmd_pool_info, ALLOCATION_CALLBACKS, &ret.transfer_pool);

        ret.flags |= (u8)Flags::UNIFIED_TRANSFER;
        return ret;
    }


    return ret;
}
void destroy_allocator(Allocator *alloc) {
    free_h(alloc->masks);
    free_h(alloc->allocs);

    Gpu *gpu = get_gpu_instance();
    VkDevice device = gpu->device;
    vkDestroyCommandPool(device, alloc->graphics_pool, ALLOCATION_CALLBACKS);
    if (gpu->transfer_queue_index != gpu->graphics_queue_index)
        vkDestroyCommandPool(device, alloc->transfer_pool, ALLOCATION_CALLBACKS);

    *alloc = {};
}

bool staging_queue_begin(Allocator *alloc) {
    if (alloc->staging_queue != Max_u64)
        return false;

    alloc->bytes_staged = align(alloc->bytes_staged, alloc->alignment);
    alloc->staging_queue = alloc->bytes_staged;

    return true;
}
void* staging_queue_add(Allocator *alloc, u64 size, u64 *offset) {
    if (alloc->staging_queue + size > alloc->stage_cap)
        return NULL;

    *offset = alloc->staging_queue;
    alloc->staging_queue += size;

    return (u8*)alloc->stage_ptr + *offset;
}
Allocation* staging_queue_submit(Allocator *alloc) {
    alloc->staging_queue = align(alloc->staging_queue, alloc->alignment);
    if (alloc->staging_queue + alloc->bytes_staged > alloc->stage_cap)
        return NULL;

    Allocation *ret = &alloc->allocs[alloc->alloc_count];
    ret->state = Alloc_State_Bits::STAGED;
    ret->stage_offset = alloc->bytes_staged;

    ret->size = alloc->staging_queue - alloc->bytes_staged;

    #if 0
    println("Vertex Attribute Allocation Size: %u", alloc->staging_queue);
    #endif

    alloc->bytes_staged += alloc->staging_queue;
    alloc->alloc_count++;
    alloc->staging_queue = Max_u64;

    return ret;
}

bool upload_queue_begin(Allocator *alloc) {
    if (alloc->upload_queue != Max_u64)
        return false;

    alloc->upload_queue = 0;

    return true;
}
VkBuffer upload_queue_add(Allocator *alloc, Allocation *allocation) {
    if (alloc->upload_queue + allocation->size > alloc->upload_cap)
        return NULL;

    switch(allocation->state) {
    case Alloc_State_Bits::STAGED:
    {
        allocation->state = Alloc_State_Bits::TO_UPLOAD;
        alloc->upload_queue += allocation->size;
        alloc->to_upload_count++;
        break;
    }
    case Alloc_State_Bits::DRAWN:
    {
        allocation->state = Alloc_State_Bits::UPLOADED;
        break;
    }
    default:
        break;
    }

    return alloc->upload;
}
bool upload_queue_submit(Allocator *alloc) {
    if (alloc->flags & (u8)Flags::UNIFIED_MEM)
        return true;

    u32 g = alloc->bit_granularity;
    u64 adj_offset;
    u64 adj_size;
    u32 free_block;

    //
    // @Todo This state system is useful for the model to know whether it is
    // ready for drawing, but switching on it in the loop in this function
    // is a bit jank. Really allocations to be uploaded and ones already
    // uploaded should be in their own lists. But if you want coherency between
    // the model and the allocator, then you need some single source of truth,
    // so then after using the other lists, you would still have to go through
    // and update the truth source, which would require some sort of indexing/derefing,
    // so then you trade a dirty loop with a switch potentially for cache hits???
    // Idk the best solution off the top of my head.
    //
    // UPDATE: A really easy solution would be to move the states into their own array, as in the tex allocator.
    // Then search these states and only visit the indices that we actually need to visit. This will be a super easy
    // thing to add, but I will do it another time. I am bored of working on these allocators for now. - 4 Nov 2023 Sol

    // Test large enough block from cursor without evicting
    adj_size = (alloc->upload_queue / g) + 1;
    free_block = find_contiguous_free(
                        alloc->mask_count,
                        alloc->masks,
                        alloc->bit_cursor,
                        adj_size);

    bool evict = false;
    if (free_block == Max_u32) {
        // Test large enough block from buffer start without evicting
        free_block = find_contiguous_free(
                        alloc->mask_count,
                        alloc->masks,
                        0,
                        adj_size);

        if (free_block == Max_u32) {
            evict = true;

            // Do evictions
            for(u32 i = 0; i < alloc->alloc_count; ++i) {
                switch(alloc->allocs[i].state) {
                case Alloc_State_Bits::DRAWN:
                {
                    adj_size = (alloc->allocs[i].size / g) + 1;
                    adj_offset = alloc->allocs[i].upload_offset / g;
                    make_free(alloc->mask_count, alloc->masks, adj_offset, adj_size);
                    break;
                }
                default:
                    break;
                }
            }

            // Test large enough block post evictions from cursor
            adj_size = (alloc->upload_queue / g) + 1;
            free_block = find_contiguous_free(
                                alloc->mask_count,
                                alloc->masks,
                                alloc->bit_cursor,
                                adj_size);

            // Test large enough block post evictions from buffer start
            if (free_block == Max_u32) {
                alloc->bit_cursor = 0;
                free_block = find_contiguous_free(
                                alloc->mask_count,
                                alloc->masks,
                                alloc->bit_cursor,
                                adj_size);

                if (free_block == Max_u32)
                    return false;
            }
        }
    }

    make_full(alloc->mask_count, alloc->masks, free_block, adj_size);

    u32 region_count = 0;
    u64 upload_offset = free_block * g;
    u64 upload_begin = upload_offset; // save value for cmd copy info
    u64 upload_end;

    VkBufferCopy2 *regions = (VkBufferCopy2*)malloc_t(sizeof(VkBufferCopy2) * alloc->to_upload_count, 8);
    for(u32 i = 0; i < alloc->alloc_count; ++i) {
        if (region_count == alloc->to_upload_count)
            break;

        switch(alloc->allocs[i].state) {
        case Alloc_State_Bits::TO_UPLOAD:
        {
            regions[region_count] = {VK_STRUCTURE_TYPE_BUFFER_COPY_2};
            regions[region_count].srcOffset = alloc->allocs[i].stage_offset;
            regions[region_count].dstOffset = upload_offset;
            regions[region_count].size = alloc->allocs[i].size;

            alloc->allocs[i].state = Alloc_State_Bits::UPLOADED;
            alloc->allocs[i].upload_offset = upload_offset;
            upload_end = alloc->allocs[i].size + upload_offset;

            upload_offset += alloc->allocs[i].size;
            region_count++;
            break;
        }
        case Alloc_State_Bits::DRAWN:
        {
            // Only set the state of drawn allocations to staged if they were actually overwritten.
            adj_size = (alloc->allocs[i].size / g) + 1;
            adj_offset = alloc->allocs[i].upload_offset / g;
            if (!is_range_free(alloc->mask_count, alloc->masks, adj_offset, adj_size)) {
                alloc->allocs[i].state = Alloc_State_Bits::STAGED;
                //alloc->allocs[i].prev_offset = alloc->allocs[i].upload_offset;
            }
            break;
        }
        default:
            break;
        }
    }
    alloc->bit_cursor = (upload_offset / g) + 1; // Point cursor at the end of the final upload region

    VkCopyBufferInfo2 copy_info = {VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2};
    copy_info.srcBuffer = alloc->stage;
    copy_info.dstBuffer = alloc->upload;
    copy_info.regionCount = region_count;
    copy_info.pRegions = regions;

    Gpu *gpu = get_gpu_instance();
    VkDevice device = gpu->device;

    VkCommandBufferAllocateInfo cmd_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc_info.commandPool = alloc->graphics_pool;
    cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    cmd_alloc_info.commandBufferCount = 1;
    auto check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->graphics_cmd);
    DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

    if (alloc->flags & (u8)Flags::UNIFIED_TRANSFER == 0) {
        cmd_alloc_info.commandPool = alloc->transfer_pool;
        check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->transfer_cmd);
        DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);
    }

    VkCommandBufferInheritanceInfo inheritance = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    VkCommandBufferBeginInfo cmd_begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmd_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    cmd_begin_info.pInheritanceInfo = &inheritance;

    // Submit copies later with other commands that use the graphics queue
    if (alloc->flags & (u8)Flags::UNIFIED_TRANSFER) {
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

    // Submit copies now since transfer queue is discrete
    } else {
        vkBeginCommandBuffer(alloc->transfer_cmd, &cmd_begin_info);

        vkCmdCopyBuffer2(alloc->transfer_cmd, &copy_info);

        VkBufferMemoryBarrier2 buf_barr = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        buf_barr.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        buf_barr.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        buf_barr.srcQueueFamilyIndex = gpu->transfer_queue_index;
        buf_barr.dstQueueFamilyIndex = gpu->graphics_queue_index;
        buf_barr.buffer = alloc->upload;
        buf_barr.offset = upload_begin;
        buf_barr.size = align(upload_end, gpu->info.props.limits.nonCoherentAtomSize);

        VkDependencyInfo dep_info = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep_info.bufferMemoryBarrierCount = 1;
        dep_info.pBufferMemoryBarriers = &buf_barr;

        vkCmdPipelineBarrier2(alloc->transfer_cmd, &dep_info);

        vkEndCommandBuffer(alloc->transfer_cmd);

        vkBeginCommandBuffer(alloc->graphics_cmd, &cmd_begin_info);

        buf_barr.srcStageMask = 0x0;
        buf_barr.srcAccessMask = 0x0;
        buf_barr.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR;
        buf_barr.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR;

        vkCmdPipelineBarrier2(alloc->graphics_cmd, &dep_info);

        vkEndCommandBuffer(alloc->graphics_cmd);
    }

    alloc->upload_queue = Max_u64;
    return true;
}
    /* End Vertex Attribute Allocator */

        /* Texture Allocator */

/*
   @Note @Todo @Speed This note also applies to the vertex attribute version of this allocator.

       Do I want to unpack allocation structs into their own arrays?

    Now I am thinking about it, I think I should unpack 'Allocation' and 'Tex_Allocation' structs
    into SOA form. Previously I thought that it would be better to keep this stuff all in the same
    structs, because although it made looping more expensive, it meant that in the loops, all the
    data was right there, so when I saw an allocation in TO_UPLOAD state, for instance, the copy
    info was already present in cache. However, now I am thinking that I might be better off
    separating the stuff out more. The plan would be to put state and size stuff in their own
    arrays. Then when I loop the states to see what stuff needs uploading etc, I can either call
    up the data right there by index, or save the indices and loop them later if necessary.

    Not only is the most pertinent data all close together, but state searching can also be
    well simderised. However, now I have to pretty much random access into the size arrays
    to setup uploads, or set offsets, so the cache is less friendly. But then again, when
    uploading data, the use case should be that contiguous allocations are queued up (allocations
    which will be drawn together should be uploaded/added/staged together), so
    the cache is fine, as the random access will not be random, but should instead be contiguous
    access of a small section of the allocator. This also makes the previous layout less attractive,
    because now you spend most time pointlessly looping large structs with mostly useless data (the data
    is only useful if the allocation is in a relevant state), then come across a few state
    hits, then nothing useful again, in which case I would rather quickly filter out the stuff I need.

    I will implement the Tex_Allocator with this new style. And I am pretty sure it will also be applied
    to the vertex attribute Allocator, since I will need to update it to use the new Alloc_State flags.
    Plus I do really have to change it to use the double layered stage and upload syste like the tex
    allocator, because in the case that the stage is always large enough to never evict, then there
    really is not any overhead, to the double layer. But in the (likely? 16gb RAM tho...) case that
    sometimes stuff will have to be evicted to disk, then I am completely unprepared with the current
    system. So ye, when I have the experience from implementing double layer for the Tex_Allocator,
    I will add it to the other Allocator. I am sure I will learn some shit from doing it here, and
    doing another iteration for the other allocator will be doubly good anyway.

        - 31 Oct 2023, Sol

    UPDATE:
    Completed upload and staging tex allocator code as far as queueing and submitting is concerned. I think
    I ended up with a pretty good improvement, but I cannot be certain. I have some pretty nice loops,
    and lots of nicely packed data, but there are some little ugly bits which are niggling me. I worry that
    although it seems good, as now there is far less branching; and the cache seems to be better used do
    to more focused structures, better SOA; that the little things will pay huge prices.

    For instance, although the old system used switches on state and larger structs with less focused data,
    when queueing stuff, the idea was that the data being queued would be used together, allocated together
    etc. and so the branches would be very predictable. Also the cache should be very predictable: although
    each struct was larger, the loop just trundled through them all without ever skipping. Now my algorithms
    are a little more complex, which has created significantly shorter, tighter loops, but these loops also seem
    more unpredictable, as sections of arrays with irrelevant data can be skipped, for instance.

    Although I have a pretty strong inclination that the new system is plenty more rapid, I will have to test
    later when I have a good use case. Therefore I will keep the vertex allocator as it is (with the state switching,
    one source of truth) as an example of the other method, so when I come to testing I can more easily reimplement
    either one.

        - 2 Nov 2023, Sol
*/
Tex_Allocator create_tex_allocator(Tex_Allocator_Create_Info *info) {
    Tex_Allocator ret = {};
    ret.allocation_cap = info->allocation_cap;
    ret.stage_cap = info->stage_byte_cap;
    ret.to_stage_cap = info->to_stage_allocation_cap;
    ret.to_upload_cap = info->to_upload_allocation_cap;
    ret.upload_cap = info->upload_byte_cap;
    ret.stage = info->stage;
    ret.stage_ptr = info->stage_ptr;
    ret.upload = info->upload;

    ASSERT(info->stage_byte_cap %  info->stage_bit_granularity == 0, "");
    ASSERT(info->upload_byte_cap % info->upload_bit_granularity == 0, "");
    ret.stage_bit_granularity = info->stage_bit_granularity;
    ret.upload_bit_granularity = info->upload_bit_granularity;

    ret.stage_mask_count = ret.stage_cap / (ret.stage_bit_granularity * 64);
    ret.upload_mask_count = ret.upload_cap / (ret.upload_bit_granularity * 64);

    //println("stage_mask_count %u", ret.stage_mask_count);

    ret.stage_masks              = (u64*)               malloc_h(sizeof(u64)               * ret.stage_mask_count, 8);
    ret.upload_masks             = (u64*)               malloc_h(sizeof(u64)               * ret.upload_mask_count, 8);

    memset(ret.stage_masks, 0, sizeof(u64) * ret.stage_mask_count);
    memset(ret.upload_masks, 0, sizeof(u64) * ret.upload_mask_count);

    ret.string_buffer            = create_string_buffer(1024);
    ret.textures                 = (Tex_Allocation*)    malloc_h(sizeof(Tex_Allocation)    * ret.allocation_cap, 8);
    ret.allocation_states        = (Alloc_State*)       malloc_h(sizeof(Alloc_State)       * ret.allocation_cap, 8);
    ret.hashes                   = (u64*)               malloc_h(sizeof(u64)               * ret.allocation_cap, 8);

    ret.to_stage_uris            = (String*)            malloc_h(sizeof(String)            * ret.to_stage_cap,   8);
    ret.to_update_stage_offsets  = (u64**)              malloc_h(sizeof(u64*)              * ret.to_stage_cap,   8);
    ret.to_update_upload_offsets = (u64**)              malloc_h(sizeof(u64*)              * ret.to_upload_cap,  8);

    ret.bind_infos               = (Tex_Bind_Info*)     malloc_h(sizeof(Tex_Bind_Info)     * ret.to_upload_cap,  8);
    ret.regions                  = (VkBufferImageCopy*) malloc_h(sizeof(VkBufferImageCopy) * ret.to_upload_cap,  8);

    #if 0
    println("Tex_Allocator Memory Footprint:");
    println("               string_buffer: %u", 1024);
    println("                 stage_masks: %u", sizeof(u64) * ret.stage_mask_count);
    println("                upload_masks: %u", sizeof(u64) * ret.upload_mask_count);
    println("                    textures: %u", sizeof(Tex_Allocation)    * ret.allocation_cap);
    println("           allocation_states: %u", sizeof(Alloc_State)       * ret.allocation_cap);

    println("               to_stage_uris: %u", sizeof(String)            * ret.to_stage_cap);
    println("     to_update_stage_offsets: %u", sizeof(u64*)              * ret.to_stage_cap);
    println("    to_update_upload_offsets: %u", sizeof(u64*)              * ret.to_upload_cap);
    println("                  bind_infos: %u", sizeof(Tex_Bind_Info)     * ret.to_upload_cap);
    println("                     regions: %u", sizeof(VkBufferImageCopy) * ret.to_upload_cap);

    u64 total_mem_footprint = 0;

    total_mem_footprint += 1024;

    total_mem_footprint += sizeof(u64) * ret.stage_mask_count;
    total_mem_footprint += sizeof(u64) * ret.upload_mask_count;

    total_mem_footprint += sizeof(Tex_Allocation)    * ret.allocation_cap;
    total_mem_footprint += sizeof(Alloc_State)       * ret.allocation_cap;

    total_mem_footprint += sizeof(String)            * ret.to_stage_cap;
    total_mem_footprint += sizeof(u64*)              * ret.to_stage_cap;
    total_mem_footprint += sizeof(u64*)              * ret.to_upload_cap;
    total_mem_footprint += sizeof(Tex_Bind_Info)     * ret.to_upload_cap;
    total_mem_footprint += sizeof(VkBufferImageCopy) * ret.to_upload_cap;

    println("    Total Memory Footprint: %u", total_mem_footprint);
    #endif

    Gpu *gpu = get_gpu_instance();
    ret.optimal_copy_alignment = gpu->info.props.limits.optimalBufferCopyOffsetAlignment;

    VkDevice device = gpu->device;
    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = gpu->graphics_queue_index;
    auto check = vkCreateCommandPool(device, &pool_info, ALLOCATION_CALLBACKS, &ret.graphics_pool);

    if (gpu->graphics_queue_index != gpu->transfer_queue_index) {
        pool_info.queueFamilyIndex = gpu->transfer_queue_index;
        check = vkCreateCommandPool(device, &pool_info, ALLOCATION_CALLBACKS, &ret.transfer_pool);
    }

    return ret;
}
void destroy_tex_allocator(Tex_Allocator *alloc) {
    Gpu *gpu = get_gpu_instance();
    VkDevice device = gpu->device;

    vkDestroyCommandPool(device, alloc->graphics_pool, ALLOCATION_CALLBACKS);
    if (gpu->transfer_queue_index != gpu->graphics_queue_index)
        vkDestroyCommandPool(device, alloc->transfer_pool, ALLOCATION_CALLBACKS);

    for(u32 i = 0; i < alloc->allocation_count; ++i)
        vkDestroyImage(device, alloc->textures[i].img, ALLOCATION_CALLBACKS);

    destroy_string_buffer(&alloc->string_buffer);

    free_h(alloc->stage_masks);
    free_h(alloc->upload_masks);
    free_h(alloc->textures);
    free_h(alloc->allocation_states);
    free_h(alloc->hashes);

    free_h(alloc->to_stage_uris);
    free_h(alloc->to_update_stage_offsets);
    free_h(alloc->to_update_upload_offsets);

    free_h(alloc->bind_infos);
    free_h(alloc->regions);

    *alloc = {};
}
/*
   @Note I need to properly consider how to multithread these allocators. For instance, if a texture is used by
   muliple models, then you only want that tex allocation to be active in one allocator. There is no benefit to
   having this memory available to all threads as it is not actually read by them, only the gpu. So perhaps I have
   just one texture allocator, which manages the free block masks and the unique texture hashes (there is no
   benefit to multithreading those aspects, as the whole point of the them is that they are zero overhead to
   traverse). Then when a submit function is called on the allocator, it can send the work of actually opening
   image files, memcpying data, recording upload commands etc to a thread pool. Then no synchronisation is
   required, as all the upload offsets can be determined by the main thread cheaply. Just have to dispatch the
   thread saying "take this data, put it here, dont worry about anyone else".

   Sounds good lol, this should actually be really easy to implement when I come to multithreading!
   I did not think a plan for multithreading such a significant section of the engine would come so easily.
   Hopefully there is nothing unforeseen...

   - 5 Nov 2023 Sol.
*/
Tex_Allocation* tex_add(Tex_Allocator *alloc, String *uri) {
    // The same texture might be used by multiple things, therefore we must track unique ones.
    // No point storing copies of the same texture.
    u64 hash = get_string_hash(uri);
    for(u32 i = 0; i < alloc->allocation_count; ++i)
        if (hash == alloc->hashes[i])
            return &alloc->textures[i];

    ASSERT(alloc->allocation_cap != alloc->allocation_count, "Tex Allocator Overflow");
    if (alloc->allocation_cap == alloc->allocation_count)
        return NULL;

    Tex_Allocation *tex = &alloc->textures[alloc->allocation_count];
    tex->state = alloc->allocation_count;

    Image img = load_image(uri);
    tex->uri = string_buffer_get_string(&alloc->string_buffer, uri);
    tex->width = img.width;
    tex->height = img.height;

    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.extent = {.width = img.width, .height = img.height, .depth = 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.format = VK_FORMAT_R8G8B8A8_SRGB;

    VkDevice device = get_gpu_instance()->device;
    auto check = vkCreateImage(device, &info, ALLOCATION_CALLBACKS, &tex->img);
    DEBUG_OBJ_CREATION(vkCreateImage, check);

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(device, tex->img, &mem_req);
    tex->size = mem_req.size;
    tex->alignment = mem_req.alignment;

    // If there is enough space in the staging buffer, then copy in the image data since it is already in memory.
    // Add first used textures first, as here they are sort of cached.
    u32 g = alloc->stage_bit_granularity;

    // should not need to worry about the divide rounding down and cutting off size, as g should be aligned to
    // the optimal copy offset also, so remainder is 0.
    u32 adj_size = align(tex->width * tex->height * 4, alloc->optimal_copy_alignment) / g;
    u32 free_block = find_contiguous_free(alloc->stage_mask_count, alloc->stage_masks, alloc->stage_cursor, adj_size);

    #if 0
    println("Image Size %u", tex->width * tex->height * 4);
    println("Free Block Byte Index = %u", free_block * 256);
    #endif

    if (free_block != Max_u32) {
        memcpy((u8*)alloc->stage_ptr + (free_block * g), img.data, img.width * img.height * 4);
        make_full(alloc->stage_mask_count, alloc->stage_masks, free_block, adj_size);
        // Mark these allocations as drawn to allow them to be evicted.
        alloc->allocation_states[alloc->allocation_count] |= (Alloc_State)Alloc_State_Bits::STAGED;
        alloc->allocation_states[alloc->allocation_count] |= (Alloc_State)Alloc_State_Bits::DRAWN;
        alloc->stage_cursor = free_block + adj_size;
    }

    free_image(&img);
    alloc->allocation_states[alloc->allocation_count] = (Alloc_State)Alloc_State_Bits::SEEN;
    alloc->allocation_count++;
    return tex;
}
bool tex_staging_queue_begin(Tex_Allocator *alloc) {
    if (alloc->staging_queue != Max_u64)
        return false;
    alloc->staging_queue = 0;
    return true;
}
bool tex_staging_queue_add(Tex_Allocator *alloc, Tex_Allocation *tex, bool upload_check) {
    if (alloc->allocation_states[tex->state] & (Alloc_State)Alloc_State_Bits::STAGED) {
        alloc->allocation_states[tex->state] &= ~(Alloc_State)Alloc_State_Bits::DRAWN;
        return true;
    }

    u64 size = align(tex->width * tex->height * 4, alloc->optimal_copy_alignment);
    alloc->staging_queue = align(alloc->staging_queue, alloc->optimal_copy_alignment);
    if (alloc->staging_queue + size > alloc->stage_cap || alloc->to_stage_cap == alloc->to_stage_count)
        return false;

    if (upload_check) {
        if (align(alloc->upload_check, tex->alignment) + tex->size > alloc->upload_cap ||
            alloc->to_stage_cap == alloc->to_upload_cap)
        {
            return false;
        } else {
            alloc->upload_check = align(alloc->upload_check, tex->alignment);
            alloc->upload_check += tex->size;
        }
    }

    alloc->staging_queue += size;
    alloc->to_stage_uris[alloc->to_stage_count] = tex->uri;
    alloc->to_update_stage_offsets[alloc->to_stage_count] = &tex->stage_offset;

    alloc->to_stage_count++;
    return true;
}
bool tex_staging_queue_submit(Tex_Allocator *alloc) {
    // Find textures in drawn + uploaded state (used further below without branch, so find once here, I can't really
    // see anywhere better in the function to put this, but it does seem weird right at function start...)
    u32 *indices = (u32*)malloc_t(sizeof(u32) * alloc->allocation_count, 4);
    u32 drawn_staged_count = simd_find_flags_u8(
                                 alloc->allocation_count,
                                 alloc->allocation_states,
                                 (Alloc_State)Alloc_State_Bits::DRAWN | (Alloc_State)Alloc_State_Bits::STAGED,
                                 indices);

    u32 g = alloc->stage_bit_granularity;
    u64 adj_size = (alloc->staging_queue / g) + 1;
    u32 free_block = find_contiguous_free(
                        alloc->stage_mask_count,
                        alloc->stage_masks,
                        alloc->stage_cursor,
                        adj_size);

    u64 adj_offset;
    bool evict = false;
    if (free_block == Max_u32) {
        free_block = find_contiguous_free(
                        alloc->stage_mask_count,
                        alloc->stage_masks,
                        0, // search from beginning of allocator
                        adj_size);
        if (free_block == Max_u32) {
            evict = true;

            // Find drawn ranges - Cache unfriendly
            // @Note although this looks pretty cache unfriendly, in reality allocations should be drawn together
            // and uploaded together, so the indices in 'indices' should actually be a tight group.
            for(u32 i = 0; i < drawn_staged_count; ++i) {
                adj_size = (align(alloc->textures[indices[i]].width * alloc->textures[indices[i]].height * 4,
                            alloc->optimal_copy_alignment) / g) + 1;
                adj_offset = alloc->textures[indices[i]].stage_offset / g;
                make_free(alloc->stage_mask_count, alloc->stage_masks, adj_offset, adj_size);
            }

            adj_size = (alloc->staging_queue / g) + 1; // reset adj_size as it is overwritten in the loop above
            free_block = find_contiguous_free(
                                alloc->stage_mask_count,
                                alloc->stage_masks,
                                alloc->stage_cursor,
                                adj_size);

            if (free_block == Max_u32) {
                alloc->stage_cursor = 0; // reset to allocator start
                free_block = find_contiguous_free(
                                alloc->stage_mask_count,
                                alloc->stage_masks,
                                alloc->stage_cursor,
                                adj_size);

                if (free_block == Max_u32)
                    return false;
            }
        }
    }

    make_full(alloc->stage_mask_count, alloc->stage_masks, free_block, adj_size);
    alloc->stage_cursor = free_block + adj_size; // point cursor at the end of the most recent allocation

    // Check if drawn allocation ranges were overwritten - Cache unfriendly | Branching
    // @Note although this looks pretty cache unfriendly, in reality allocations should be drawn together
    // and uploaded together, so the indices in 'indices' should actually be a tight group.
    // This also means that the overwritten ranges should contain tight allocations, so the branch should be
    // relatively predictable (nothing for while, a few hits, nothing to loop end).
    for(u32 i = 0; i < drawn_staged_count; ++i) {
        adj_size = (align(alloc->textures[indices[i]].width * alloc->textures[indices[i]].height * 4,
                    alloc->optimal_copy_alignment) / g) + 1;
        adj_offset = alloc->textures[indices[i]].stage_offset / g;

        if (!is_range_free(alloc->stage_mask_count, alloc->stage_masks, adj_offset, adj_size))
            alloc->allocation_states[indices[i]] &= ~(Alloc_State)Alloc_State_Bits::STAGED;
    }

    align_temp(alloc->optimal_copy_alignment); // align allocator before loading img
    Image img = load_image(&alloc->to_stage_uris[0]);
    u8 *img_mem_start = img.data;

    u64 stage_offset = free_block * g; // g must be a size which fulfills any alignment requirements
    u64 tmp = stage_offset;
    *alloc->to_update_stage_offsets[0] = tmp;
    tmp += align(img.height * img.width, alloc->optimal_copy_alignment);

    u64 mark = get_mark_temp();
    for(u32 i = 1; i < alloc->to_stage_count; ++i) {
        align_temp(alloc->optimal_copy_alignment); // align allocator before img load
        img = load_image(&alloc->to_stage_uris[i]);

        *alloc->to_update_stage_offsets[i] = tmp;
        tmp += align(img.height * img.width, alloc->optimal_copy_alignment);
    }

    // load_image() writes to the temp allocator, therefore each allocation is contiguous in memory,
    // so by aligning each allocation in the above loop, the data can all be copied out to the staging
    // buffer in one memcpy call.
    alloc->staging_queue = align(alloc->staging_queue, alloc->optimal_copy_alignment);
    memcpy((u8*)alloc->stage_ptr + stage_offset, img_mem_start, alloc->staging_queue);

    reset_to_mark_temp(mark);

    simd_find_and_update_flags_u8(
            alloc->allocation_count,
            alloc->allocation_states,
            (Alloc_State)Alloc_State_Bits::TO_STAGE,
            (Alloc_State)Alloc_State_Bits::TO_STAGE,
            (Alloc_State)Alloc_State_Bits::STAGED);

    alloc->to_stage_count = 0;
    alloc->staging_queue = Max_u64; // Indicate it is safe to use stage queue again
    alloc->upload_check = 0;
    return true;
}
bool tex_upload_queue_begin(Tex_Allocator *alloc) {
    if (alloc->upload_queue != Max_u64)
        return false;

    alloc->upload_queue = 0;
    return true;
}
bool tex_upload_queue_add(Tex_Allocator *alloc, Tex_Allocation *tex) {
    if (alloc->allocation_states[tex->state] & (Alloc_State)Alloc_State_Bits::UPLOADED) {
        alloc->allocation_states[tex->state] &= ~(Alloc_State)Alloc_State_Bits::DRAWN;
        return true;
    }

    alloc->upload_queue = align(alloc->upload_queue, tex->alignment);
    if (alloc->upload_queue + tex->size > alloc->upload_cap || alloc->to_upload_cap == alloc->to_upload_count)
        return false;

    alloc->upload_queue += tex->size;
    alloc->to_update_upload_offsets[alloc->to_upload_count] = &tex->upload_offset;

    alloc->bind_infos[alloc->to_upload_count].img = tex->img;
    alloc->bind_infos[alloc->to_upload_count].alignment = tex->alignment;
    alloc->bind_infos[alloc->to_upload_count].size = tex->size;

    alloc->regions[alloc->to_upload_count].bufferOffset = tex->stage_offset;
    alloc->regions[alloc->to_upload_count].bufferRowLength = tex->width;
    alloc->regions[alloc->to_upload_count].bufferImageHeight = tex->height;
    alloc->regions[alloc->to_upload_count].imageSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0,
        .baseArrayLayer = 0,
        .layerCount = 1,
    };
    alloc->regions[alloc->to_upload_count].imageOffset = {.x = 0, .y = 0, .z = 0};
    alloc->regions[alloc->to_upload_count].imageExtent = {
        .width = tex->width,
        .height = tex->height,
        .depth = 1,
    };

    alloc->to_upload_count++;
    return true;
}
static void recreate_images(Tex_Allocator *alloc); // Defined below submit func
bool tex_upload_queue_submit(Tex_Allocator *alloc) {

    // Find textures in drawn + uploaded state (used further below without branch, so find once here, I can't really
    // see anywhere better in the function to put this, but it does seem weird right at function start...)
    u32 *indices = (u32*)malloc_t(sizeof(u32) * alloc->allocation_count, 4);
    u32 drawn_uploaded_count = simd_find_flags_u8(
                                 alloc->allocation_count,
                                 alloc->allocation_states,
                                 (Alloc_State)Alloc_State_Bits::DRAWN | (Alloc_State)Alloc_State_Bits::UPLOADED,
                                 indices);

    u32 g = alloc->upload_bit_granularity;
    u64 adj_size = (alloc->upload_queue / g) + 1;

    u32 free_block = find_contiguous_free(
                        alloc->upload_mask_count,
                        alloc->upload_masks,
                        alloc->upload_cursor,
                        adj_size);

    u64 adj_offset;
    bool evict = false;
    if (free_block == Max_u32) {
        free_block = find_contiguous_free(
                        alloc->upload_mask_count,
                        alloc->upload_masks,
                        0, // search from beginning of allocator
                        adj_size);
        if (free_block == Max_u32) {
            evict = true;

            // Find drawn ranges - Cache unfriendly
            // @Note although this looks pretty cache unfriendly, in reality allocations should be drawn together
            // and uploaded together, so the indices in 'indices' should actually be a tight group.
            for(u32 i = 0; i < drawn_uploaded_count; ++i) {
                adj_size = (alloc->textures[indices[i]].size / g) + 1;
                adj_offset = alloc->textures[indices[i]].upload_offset / g;
                make_free(alloc->upload_mask_count, alloc->upload_masks, adj_offset, adj_size);
            }

            adj_size = (alloc->upload_queue / g) + 1; // reset adj_size as it is overwritten in the loop above
            free_block = find_contiguous_free(
                                alloc->upload_mask_count,
                                alloc->upload_masks,
                                alloc->upload_cursor,
                                adj_size);

            if (free_block == Max_u32) {
                alloc->upload_cursor = 0; // reset to allocator start
                free_block = find_contiguous_free(
                                alloc->upload_mask_count,
                                alloc->upload_masks,
                                alloc->upload_cursor,
                                adj_size);

                if (free_block == Max_u32)
                    return false;
            }
        }
    }

    u64 upload_offset = free_block * g; // g must be aligned to a size which fulfills any alignment requirement
    Gpu *gpu = get_gpu_instance();
    VkDevice device = gpu->device;
    VkBindImageMemoryInfo *bind_infos =
        (VkBindImageMemoryInfo*)malloc_t(sizeof(VkBindImageMemoryInfo) * alloc->allocation_count, 8);
    for(u32 i = 0; i < alloc->to_upload_count; ++i) {
        bind_infos[i] = {VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO};
        bind_infos[i].image = alloc->bind_infos[i].img;
        bind_infos[i].memory = alloc->upload;

        upload_offset = align(upload_offset, alloc->bind_infos[i].alignment);
        bind_infos[i].memoryOffset = upload_offset;

        *alloc->to_update_upload_offsets[i] = upload_offset; // double deref
        upload_offset += alloc->bind_infos[i].size;
    }
    // VkSpec "If vkBindImageMemoryInfo2 fails and count > 1, images are in an undefined
    // state and should be destroyed".
    if (vkBindImageMemory2(device, alloc->to_upload_count, bind_infos) != VK_SUCCESS)
        recreate_images(alloc);

    make_full(alloc->upload_mask_count, alloc->upload_masks, free_block, adj_size);
    alloc->upload_cursor = free_block + adj_size; // point cursor at the end of the most recent allocation

    // Check if drawn allocation ranges were overwritten - Cache unfriendly | Branching
    // @Note although this looks pretty cache unfriendly, in reality allocations should be drawn together
    // and uploaded together, so the indices in 'indices' should actually be a tight group.
    // This also means that the overwritten ranges should contain tight allocations, so the branch should be
    // relatively predictable (nothing for while, a few hits, nothing to loop end).
    for(u32 i = 0; i < drawn_uploaded_count; ++i) {
        // @Note This could be optimized better by separating out 'Texture' data into some other storage so
        // that it is packed better for this check. The way it is, lots of useless memory will be being loaded.
        // Something like this would be more efficient:
        /*
            struct Tex_Range {
                u64 offset;
                u64 size;
            };
        */
        //
        // This loop and the one in the above section which marks drawn ranges are the only really ugly sections
        // of this allocator now. There is a double deref when updating offsets, but that is not soo bad I think.
        // That double deref would the second port of call. But right now this loop and the one mentioned above
        // are a really gripe. I would very much like to make a better thing for this... Tbf it isnt awful,
        // as these indices will be increasing (they are not random), and the drawn + uploaded textures should be
        // close in memory, but still.
        adj_size = (alloc->textures[indices[i]].size / g) + 1;
        adj_offset = alloc->textures[indices[i]].upload_offset / g;

        if (!is_range_free(alloc->upload_mask_count, alloc->upload_masks, adj_offset, adj_size))
            alloc->allocation_states[indices[i]] &= ~(Alloc_State)Alloc_State_Bits::UPLOADED;
    }

    simd_find_and_update_flags_u8(
            alloc->allocation_count,
            alloc->allocation_states,
            (Alloc_State)Alloc_State_Bits::TO_UPLOAD,
            (Alloc_State)Alloc_State_Bits::TO_UPLOAD,
            (Alloc_State)Alloc_State_Bits::UPLOADED);

    //
    // @Todo I really need to learn about buffer image copies, the sync example comments
    // make it seem like I can use one image for multiple textures, so then I can do buffer copies
    // with single copy commands, but I cannot tell if this is the case: I can't figure out how
    // you would do this? Because you can specify image copy regions in a buffer copy, but then
    // in order to use the image, you would then need to use offsets, but I cannot see where
    // these offsets can be used. Plus I do not know what effects optimal tiling has etc...
    //

    // @Note Should this be changed to record barriers into secondary command buffers rather than spitting
    // out the barrier info?

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

    VkImageMemoryBarrier2 *img_barrs =
        (VkImageMemoryBarrier2*)malloc_t(sizeof(VkImageMemoryBarrier2) * alloc->to_upload_count, 8);

    // Transition for buffer copy
    for(u32 i = 0; i < alloc->to_upload_count; ++i) {
        img_barrs[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        img_barrs[i].dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        img_barrs[i].dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        img_barrs[i].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        img_barrs[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        img_barrs[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        img_barrs[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        img_barrs[i].image = alloc->bind_infos[i].img;
        img_barrs[i].subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };
    }

    // Record copy commands + transitions
    VkCommandBufferInheritanceInfo inheritance = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = &inheritance;

    VkDependencyInfo dep = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = alloc->to_upload_count;
    dep.pImageMemoryBarriers = img_barrs;

    if (transfer_queue_idx == graphics_queue_idx) {

        vkBeginCommandBuffer(alloc->graphics_cmd, &begin_info);

        vkCmdPipelineBarrier2(alloc->graphics_cmd, &dep);

        for(u32 i = 0; i < alloc->to_upload_count; ++i)
            vkCmdCopyBufferToImage(
                    alloc->graphics_cmd,
                    alloc->stage,
                    alloc->bind_infos[i].img,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &alloc->regions[i]); // @Note Can multiple textures be stored in one img?

        // Transition for reading in shader
        for(u32 i = 0; i < alloc->to_upload_count; ++i) {
            img_barrs[i].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
            img_barrs[i].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
            img_barrs[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
            img_barrs[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR;
            img_barrs[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            img_barrs[i].newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        }

        // Shader read transition
        vkCmdPipelineBarrier2(alloc->graphics_cmd, &dep);

        vkEndCommandBuffer(alloc->graphics_cmd);

    } else {

        vkBeginCommandBuffer(alloc->transfer_cmd, &begin_info);

        // Copy to transition
        vkCmdPipelineBarrier2(alloc->transfer_cmd, &dep);

        for(u32 i = 0; i < alloc->to_upload_count; ++i)
            vkCmdCopyBufferToImage(
                    alloc->transfer_cmd,
                    alloc->stage,
                    alloc->bind_infos[i].img,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1,
                    &alloc->regions[i]); // @Note Can multiple textures be stored in one img?

        // Transition for reading in shader + begin queue transfer
        for(u32 i = 0; i < alloc->to_upload_count; ++i) {
            img_barrs[i].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
            img_barrs[i].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
            img_barrs[i].dstStageMask = 0x0;
            img_barrs[i].dstAccessMask = 0x0;
            img_barrs[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            img_barrs[i].newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            img_barrs[i].srcQueueFamilyIndex = transfer_queue_idx;
            img_barrs[i].dstQueueFamilyIndex = graphics_queue_idx;
        }

        // Shader read transition + queue transfer
        vkCmdPipelineBarrier2(alloc->transfer_cmd, &dep);

        vkEndCommandBuffer(alloc->transfer_cmd);

        // Record queue ownership transfer barrier finalisation
        vkBeginCommandBuffer(alloc->graphics_cmd, &begin_info);

        // Complete shader read transition + queue transfer
        for(u32 i = 0; i < alloc->to_upload_count; ++i) {
            img_barrs[i].srcStageMask = 0x0;
            img_barrs[i].srcAccessMask = 0x0;
            img_barrs[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
            img_barrs[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR;
            img_barrs[i].srcQueueFamilyIndex = transfer_queue_idx;
            img_barrs[i].dstQueueFamilyIndex = graphics_queue_idx;
        }

        // Shader read transition + queue transfer
        vkCmdPipelineBarrier2(alloc->graphics_cmd, &dep);

        vkEndCommandBuffer(alloc->graphics_cmd);
    }

    alloc->to_upload_count = 0;
    alloc->upload_queue = Max_u64; // Indicate it is safe to use upload queue again
    return true;
}
void recreate_images(Tex_Allocator *alloc) {
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkDevice device = get_gpu_instance()->device;
    VkResult check;
    Tex_Allocation *tex;
    for(u32 i = 0; i < alloc->allocation_count; ++i) {
        tex = &alloc->textures[i];

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

//
// @CurrentTask Allocator Redo (I have the hang of it now, this is the final system)
//

/*
   Allocator To Implement:
       fopening + freading + fwrite at correct file offset

   Tex_Allocator To Implement:
    @Todo
*/

Allocator_Result begin_allocation(Allocator *alloc) {
    ASSERT(alloc->to_stage_count == Max_u32, "");
    if (alloc->to_stage_count != Max_u32)
        return QUEUE_IN_USE;

    if (alloc->allocation_count == alloc->allocation_cap)
        return ALLOCATOR_FULL;
    Allocation *tmp = &alloc->allocations[alloc->allocation_count];
    *tmp = {};
    tmp->disk_offset = alloc->disk_size;
    alloc->disk = fopen(alloc->disk_storage); // offset to disk size

    // staging queue must not be used during the allocation phase of the program. So it is safe to reuse it here
    // for tracking in-progress allocations.
    alloc->staging_queue_byte_count = 0;
    return SUCCESS;
}
Allocator_Result continue_allocation(Allocator *alloc, u64 size, void *ptr) {
    u64 current_size = alloc->staging_queue_byte_count + size;
    if (current_size > alloc->staging_queue_byte_cap)
        return STAGE_FULL;

    fwrite(file, ptr, size); // offset at disk size;
    if (current_size + alloc->bytes_staged < alloc->stage_cap) {
        memcpy(alloc->stage_ptr + alloc->staging_queue_byte_count, ptr, size);
        alloc->staging_queue_byte_count += size;
    }
    return SUCCESS;
}
Allocator_Result submit_allocation(Allocator *alloc, u8 weight, u32 *key) {
    *key = alloc->allocation_count;
    alloc->allocation_count++;
    alloc->to_stage_count = Max_u32;
    adjust_allocation_weights();
    return SUCCESS;
}

Allocator_Result staging_queue_begin(Allocator *alloc) {
    if (alloc->to_stage_count != Max_u32)
        return QUEUE_IN_USE;
    alloc->to_stage_count = 0;
    alloc->staging_queue_byte_count = 0;
    return SUCCESS;
}
Allocator_Result staging_queue_add(Allocator *alloc, u32 idx) {
    idx = alloc->allocation_indices[idx];
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
    alloc->allocation_states[new_idx] |= TO_DRAW;
    if (alloc->allocation_states[new_idx] & STAGED)
        return SUCCESS;

    // Ensure that allocations will not overlap into another's bit representation
    u64 bit_align_size = align(alloc->allocations[new_idx].size, alloc->stage_bit_granularity);
    if (bit_align_size + alloc->staging_queue_byte_count > alloc->staging_queue_byte_cap)
        return QUEUE_FULL;

    alloc->staging_queue_byte_count += bit_align_size;
    alloc->to_stage_count++;
    return SUCCESS;
}
Allocator_Result staging_queue_submit(Allocator *alloc) {
    // If the to stage count is zero on queue submission, just assume that everything queued was already cached,
    // and we need not do anything. This is most likely, as vertex data should just be able to live in memory
    // I am pretty certain. In case it can't (I would like to support even shit hardware, as everyone should)
    // the same range searching functionality is implemented here for disk->stage (in the same way as stage->device)
    if (alloc->to_stage_count == 0) {
        alloc->to_stage_count == Max_u32;
        return SUCCESS;
    }

    u32 free_block = find_contiguous_free(alloc->stage_masks, alloc->staging_queue_byte_count);
    Allocation *tmp;
    u32 *indices = alloc;
    u32 evict_idx;
    u32 count;
    if (free_block == Max_u32) {
        count = find_flags(alloc->allocation_states, indices, STAGED, TO_DRAW);
        for(u32 i = count - 1; i != Max_u32; --i) {
            tmp = alloc->allocations[indices[i]];
            alloc->allocation_states[indices[i]] ^= STAGED; // mark as having been evicted from stage buffer
            make_free(alloc->stage_masks, tmp->stage_offset, tmp->size);
            free_block = find_contiguous_free(alloc->stage_masks, alloc->staging_queue_byte_count);
            if (free_block != Max_u32) {
                evict_idx = i;
                goto free_block_found;
            }
        }
        return STAGE_FULL; // Too many allocations waiting to draw
    }

    free_block_found: // goto

    u64 block_size = free_block * alloc->stage_bit_granularity;
    count = simd_find_flags_u8(alloc->allocation_states, indices, 0xff, STAGED);
    u32 indices_final = alloc;
    u64 size = 0;
    for(u32 i = 0; i < count; ++i) {
        size += align(alloc->allocations[indices[i]].size, alloc->stage_bit_granularity);
        if (block_size - size > alloc->staging_queue_byte_cap)
            indices_final[i] = indices[i];
        else
            break;
    }
    u32 tmp = find_flags_any(alloc->allocation_states, indices, TO_DRAW, STAGED);
    memcpy(indices_final + count, indices, tmp * sizeof(u32));
    eject_repeat_indices(count + tmp, indices_final);


    // I do not know if this is a naive implementation. The way the allocator works is it takes a parsed gltf
    // file, and then groups accessors/buffer views into one allocation. So all of a model's vertex attribute data
    // is put into one contiguous allocation. This may be pointless, as it seems sensible that the data would
    // already be laid out like this in the file. But I do not want to always rely on that fact (in a real company
    // this would just be enforced, but wild world I will not trust). So I group the data together, then write
    // it all into one file used by the allocator if its memory allocation is going to overflow. It's sort of dumb
    // in the current state of the app, as these gltf files are read on every load, and then rewritten every load.
    // However in production I would ditch the gltf files completely and just ship with the allocator files, obvs).
    // So it will stay like this for now. Copying around data is the essence of computing *shrug*.
    u64 stage_offset = free_block * alloc->stage_bit_granularity;
    FILE *disk_storage = fopen(alloc->disk_storage);
    for(u32 i = 0; i < count; ++i) {
        tmp = alloc->allocations[indices[i]];
        alloc->allocation_states[indices[i]] |= STAGED;
        fread(alloc->stage_ptr + stage_offset, 1, tmp->size, disk_storage + tmp->disk_offset);

        tmp->stage_offset = stage_offset;
        stage_offset += align(tmp->size, alloc->stage_bit_granularity);
        ASSERT(stage_offset + tmp->size <= alloc->stage_cap, "Allocator Stage Overflow");
    }
    fclose(alloc->disk_storage);
    return SUCCESS;
}
Allocator_Result upload_queue_begin(Allocator *alloc) {
    if (alloc->to_upload_count != Max_u32)
        return QUEUE_IN_USE;
    alloc->to_upload_count = 0;
    alloc->upload_queue_byte_count = 0;
    return SUCCESS;
}
Allocator_Result upload_queue_add(Allocator *alloc, u32 idx) {
    idx = alloc->allocation_indices[idx];
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
    if (alloc->allocation_states[idx] & UPLOADED)
        return SUCCESS;

    // Alignment ensures that allocations will not overlap into another's bit representation
    u64 bit_align_size = align(alloc->allocations[idx].size, alloc->upload_bit_granularity);
    if (bit_align_size + alloc->upload_queue_byte_count > alloc->upload_queue_byte_cap)
        return UPLOAD_QUEUE_FULL;

    alloc->upload_queue_byte_count += bit_align_size;
    alloc->to_upload_count++;
    return SUCCESS;
}
Allocator_Result upload_queue_submit(Allocator *alloc) {
    // If the to upload count is zero on queue submission, just assume that everything queued was already cached,
    // and we need not do anything. This is most likely, as vertex data should just be able to live in memory
    // I am pretty certain. In case it can't (I would like to support even shit hardware, as everyone should)
    // the same range searching functionality is implemented here for disk->upload (in the same way as upload->device)
    Gpu *gpu = get_gpu_instance();
    Memory_Flags mem_flags = gpu->memory.flags;
    // Really this should never be used. Upload queue shouldnt be used at all if UMA
    if (alloc->to_upload_count == 0 || mem_flags & GPU_MEMORY_UMA) {
        alloc->to_upload_count == Max_u32;
        return SUCCESS;
    }

    u32 evict_idx;
    u32 count;
    Allocation *tmp;
    u32 *indices = alloc;
    u32 free_block = find_contiguous_free(alloc->upload_masks, alloc->upload_queue_byte_count);
    if (free_block == Max_u32) {
        count = find_flags(alloc->allocation_states, indices, UPLOADED, TO_DRAW);
        for(u32 i = count - 1; i != Max_u32; --i) {
            tmp = alloc->allocations[indices[i]];
            alloc->allocation_states[indices[i]] ^= UPLOADED; // mark as having been evicted from upload buffer
            make_free(alloc->upload_masks, tmp->upload_offset, tmp->size);
            free_block = find_contiguous_free(alloc->upload_masks, alloc->upload_queue_byte_count);
            if (free_block != Max_u32) {
                evict_idx = i;
                goto free_block_found;
            }
        }
        return STAGE_FULL; // Too many allocations waiting to draw
    }

    free_block_found: // goto

    u64 block_size = free_block * alloc->upload_bit_granularity;
    count = find_flags_any(alloc->allocation_states, indices, 0xff, UPLOADED);
    u32 indices_final = alloc;
    u64 size = 0;
    for(u32 i = 0; i < count; ++i) {
        size += align(alloc->allocations[indices[i]].size, alloc->upload_bit_granularity);
        if (block_size - size > alloc->upload_queue_byte_cap)
            indices_final[i] = indices[i];
        else
            break;
    }
    u32 tmp = find_flags_any(alloc->allocation_states, indices, TO_DRAW, UPLOADED);
    ASSERT(tmp == alloc->to_upload_count, "UUUGHGHGHHGGHHH!!!");
    memcpy(indices_final + count, indices, alloc->to_upload_count * sizeof(u32));
    count = eject_repeat_indices(count + tmp, indices_final);

    VkBufferCopy2 *regions = (VkBufferCopy2*)malloc_t(sizeof(VkBufferCopy2) * count, 8);
    u64 g = alloc->upload_bit_granularity;
    u64 upload_offset = free_block * g;
    for(u32 i = 0; i < count; ++i) {
        tmp = &alloc->allocations[indices[i]];
        regions[i] = {VK_STRUCTURE_TYPE_BUFFER_COPY_2};
        regions[i].srcOffset = tmp->stage_offset;
        regions[i].dstOffset = tmp->upload_offset;
        regions[i].size = tmp->size;
        upload_offset = align(tmp->size, g);
    }

    VkCopyBufferInfo2 copy_info = {VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2};
    copy_info.srcBuffer = alloc->stage;
    copy_info.dstBuffer = alloc->upload;
    copy_info.regionCount = region_count;
    copy_info.pRegions = regions;

    VkDevice device = gpu->device;

    VkCommandBufferAllocateInfo cmd_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc_info.commandPool = alloc->graphics_pool;
    cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    cmd_alloc_info.commandBufferCount = 1;
    auto check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->graphics_cmd);
    DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

    if (mem_flags & GPU_MEMORY_DISCRETE_TRANSFER_BIT == 0) {
        cmd_alloc_info.commandPool = alloc->transfer_pool;
        check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->transfer_cmd);
        DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);
    }

    VkCommandBufferInheritanceInfo inheritance = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    VkCommandBufferBeginInfo cmd_begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmd_begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    cmd_begin_info.pInheritanceInfo = &inheritance;

    // Submit copies later with other commands that use the graphics queue
    if (alloc->flags & (u8)Flags::UNIFIED_TRANSFER) {
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

    // Submit copies now since transfer queue is discrete
    } else {
        vkBeginCommandBuffer(alloc->transfer_cmd, &cmd_begin_info);

        vkCmdCopyBuffer2(alloc->transfer_cmd, &copy_info);

        VkBufferMemoryBarrier2 buf_barr = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        buf_barr.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        buf_barr.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        buf_barr.srcQueueFamilyIndex = gpu->transfer_queue_index;
        buf_barr.dstQueueFamilyIndex = gpu->graphics_queue_index;
        buf_barr.buffer = alloc->upload;
        buf_barr.offset = upload_begin;
        buf_barr.size = align(upload_end, gpu->info.props.limits.nonCoherentAtomSize);

        VkDependencyInfo dep_info = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep_info.bufferMemoryBarrierCount = 1;
        dep_info.pBufferMemoryBarriers = &buf_barr;

        vkCmdPipelineBarrier2(alloc->transfer_cmd, &dep_info);

        vkEndCommandBuffer(alloc->transfer_cmd);

        vkBeginCommandBuffer(alloc->graphics_cmd, &cmd_begin_info);

        buf_barr.srcStageMask = 0x0;
        buf_barr.srcAccessMask = 0x0;
        buf_barr.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR;
        buf_barr.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR;

        vkCmdPipelineBarrier2(alloc->graphics_cmd, &dep_info);

        vkEndCommandBuffer(alloc->graphics_cmd);
    }

    alloc->upload_queue = Max_u64;
    return true;

    return SUCCESS;
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
