#include "gpu.hpp"
#include "vulkan/vulkan_core.h"
#include "vulkan_errors.hpp"
#include "glfw.hpp"
#include "spirv.hpp"
#include "file.hpp"
#include "builtin_wrappers.h"

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
    u32 *host_mem_type)
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
    u32 *device_host_mem_type)
{
    for(int i = 0; i < props->memoryTypeCount; ++i)
        if (props->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
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
                break;
            } else {
                *device_host_mem_type = i;
            }
        }
}
void setup_memory_integrated(u32 device_mem_type, u32 host_mem_type) {
    Gpu *gpu = get_gpu_instance();
    VkDevice device = gpu->device;

    gpu->memory.vertex_mem_device = NULL;
    gpu->memory.index_mem_device = NULL;

    VkResult check;

    VkMemoryRequirements mem_req;

    VkMemoryDedicatedAllocateInfo dedicate_info = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};

    VkMemoryPriorityAllocateInfoEXT priority_info = {VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT};

    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.pNext = &priority_info;

                                    /* Attachments */

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

                                    /* Vertex Attributes */

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

    u32 host_mem_type;
    u32 device_mem_type;
    u32 both_mem_type;
    if (gpu->info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        integrated_gpu_get_memory_type(&mem_props,
            largest_heap_device,
            largest_heap_host,
            &device_mem_type,
            &host_mem_type);
        setup_memory_integrated(device_mem_type, host_mem_type);
    }
    else if (gpu->info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        discrete_gpu_get_memory_type(&mem_props,
            largest_heap_device,
            largest_heap_host,
            &device_mem_type,
            &host_mem_type,
            &both_mem_type);
        //setup_memory_discrete(device_mem_type, host_mem_type, both_mem_type);
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

    /*
    // Vertex attribute mem

    if (gpu->info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        vkFreeMemory(device, gpu->memory.vertex_mem_device, ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory.index_mem_device, ALLOCATION_CALLBACKS);
    }
    for(u32 i= 0; i < VERTEX_STAGE_COUNT; ++i)
        vkFreeMemory(device, gpu->memory.vertex_mem_stage[i], ALLOCATION_CALLBACKS);
    for(u32 i= 0; i < INDEX_STAGE_COUNT; ++i)
        vkFreeMemory(device, gpu->memory.index_mem_stage[i], ALLOCATION_CALLBACKS);

    // Texture mem
    for(u32 i= 0; i < TEXTURE_STAGE_COUNT; ++i)
        vkFreeMemory(device, gpu->memory.texture_mem_stage[i], ALLOCATION_CALLBACKS);
    vkFreeMemory(device, gpu->memory.texture_mem_device, ALLOCATION_CALLBACKS);

    // Uniform mem
    for(u32 i= 0; i < UNIFORM_BUFFER_COUNT; ++i)
        vkFreeMemory(device, gpu->memory.uniform_mem[i], ALLOCATION_CALLBACKS);
    */
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
Set_Allocate_Info insert_shader_set(const char *set_name, u32 count, String *files, Shader_Map *map) {
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
    map->map.insert_ptr(&hash, &set);

    Set_Allocate_Info ret;
    ret.count = layout_count;
    ret.infos = layout_infos;
    ret.layouts = layouts;
    ret.sets = set.sets;

    return ret;
}

Shader_Set* get_shader_set(const char *set_name, Shader_Map *map) {
    u64 hash = get_string_hash(set_name);
    return map->map.find_cpy(hash);
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

// `Model Loading
#if 1
struct Mesh;
struct Node {
    u32 child_count;
    Mesh *mesh;
    Node *children;
    float rotation[4];
    float scale[3];
    float translation[3];
};
struct Primitive {
    u32 mat_index;
    u32 draw_count;
    VkIndexType index_type;

    VkBuffer vertex_buffer;
    VkBuffer index_buffer;

    // Offsets
    u64 pos;
    u64 tangent;
    u64 normal;
    u64 tex_coords;
};
struct Pl_Prim_Info {
    VkPrimitiveTopology topology;

    u32 pos_stride;
    VkFormat pos_format;
    u32 norm_stride;
    VkFormat norm_format;
    u32 tang_stride;
    VkFormat tang_format;
    u32 tex_stride;
    VkFormat tex_format;
};
struct Mesh {
    u32 primitive_count;
    Primitive *primitives;
    Pl_Prim_Info *pl_infos;
};
struct Material {
    VkImage texture;
    VkBuffer tex_stage;
};
struct Model_Allocation;
// @Note Static vs animated models should be differentiated. This would reduce memory footprint, 
// as static models need less data (cache may also benefit).
// But for now I do not want to increase complexity too much.
struct Model {
    u32 mesh_count;
    u32 mat_count;
    u32 node_count;
    Mesh *meshes;
    Material *mats;
    Node *nodes; // @Unused Waiting for animation

    Model_Allocation *index_alloc;
    Model_Allocation *vert_alloc;
    Model_Allocation *tex_alloc;
    Model_Allocation *unif_alloc; // @Unused Waiting for animation
};
struct Model_Map {
    HashMap<u64, Model> map;
};

enum class Model_Allocation_State : u32 {
    NONE = 0,
    STAGED = 1,
    NEEDS_UPLOAD = 2,
    UPLOADED = 3,
    DRAWN = 4,
};
struct Model_Allocation {
    u64 vertex_offset;
    u64 vertex_size;
    Model_Allocation_State state;
};
struct Model_Allocator {
    u64 bytes_allocated;
    u64 byte_cap;
    u32 allocation_cap;
    u32 allocation_count;
    Model_Allocation *allocations;
    VkBuffer stage;
    VkBuffer upload;
};
struct Model_Allocator_Group {
    Model_Allocator *index;
    Model_Allocator *vert;
    Model_Allocator *unif;
};
namespace {
enum class Type : u8 {
    INDEX,
    VERT,
    UNIF,
};
struct Accessor {
    u64 offset;
};
struct Buffer_View {
    Type type;
};
}
u32 get_accessor_byte_stride(Gltf_Accessor_Format accessor_format);
Model* load_models(Model_Allocator_Group *allocators, u32 count, String *model_files) {
    Model *models = (Model*)malloc_h(sizeof(Model) * count, 8);
    for(u32 i = 0; i < count; ++i) {
        models[i].index = allocators->index;
        models[i].vert = allocators->vert;
        models[i].unif = allocators->unif;
    }
    Gltf gltf;
    Gltf_Mesh mesh;
    Gltf_Mesh_Primitive primitive;
    Gltf_Accessor *accessor;
    Accessor *accessors;
    u32 mesh_count;
    u32 material_count;
    u32 accessor_count;
    u32 buffer_view_count;
    u32 prim_track;
    for(u32 i = 0; i < count; ++i) {
        mark = get_mark_temp();

        gltf = parse_gltf(model_files[i].str);
        mesh_count = gltf_mesh_get_count(&gltf);
        material_count = gltf_material_get_count(&gltf);
        accessor_count = gltf_accessor_get_count(&gltf);
        buffer_view_count = gltf_buffer_view_get_count(&gltf);

        // Allocate CPU model memory
        models[i].meshes = (Mesh*)malloc_h(sizeof(Mesh) * mesh_count, 8);
        models[i].mats = (Material*)malloc_h(sizeof(Material) * material_count, 8);

        models[i].meshes[0].primitives = (Primitive*)malloc_h(sizeof(Primitive) * gltf.total_primitive_count, 8);
        models[i].meshes[0].pl_infos = (Pl_Prim_Info*)malloc_h(sizeof(Pl_Prim_Info) * gltf.total_primitive_count, 8);

        // For matching primitives to allocation offsets
        accessors = (Accessor*)malloc_t(sizeof(Accessors) * accessor_count, 8);
        buffer_views = (Buffer_View*)malloc_t(sizeof(Buffer_View) * buffer_view_count, 8);

        // Find buffer view types
        mesh = gltf.meshes;
        for(u32 j = 0; j < mesh_count; ++j) {

            models[i].meshes[j].primitive_count = mesh->primitive_count;
            models[i].meshes[j].primitives = models[i].meshes[0].primitives + prim_track;
            models[i].meshes[j].pl_infos = models[i].meshes[0].pl_infos + prim_track;

            prim_track += mesh->primitive_count;

            primitive = mesh->primitives;
            for(u32 k = 0; k < mesh->primitive_count; ++k) {
                
                accessor = gltf_accessor_by_index(&gltf, primitive->indices);

                // Draw Info
                models[i].meshes[j].primitives[k].mat_index = primitive->material;
                models[i].meshes[j].primitives[k].draw_count = accessor->count;

                switch(accessor->format) {
                case GLTF_ACCESSOR_FORMAT_SCALAR_U16:
                    models[i].meshes[j].primitives[k].index_type = VK_INDEX_TYPE_UINT16;
                    break;
                case GLTF_ACCESSOR_FORMAT_SCALAR_U32:
                    models[i].meshes[j].primitives[k].index_type = VK_INDEX_TYPE_UINT32;
                    break;
                default:
                    ASSERT(false, "Invalid Index Type");
                }
                
                // Pl Vertex Assembly State
                models[i].meshes[j].pl_infos[k].topology = (VkPrimitiveTopology)primitive->topology;

                // Setup allocation information return
                accessors[primitive->indices].offset = accessor->byte_offset;
                buffer_views[accessor->buffer_view].type = Type::INDEX;

                if (primitive->position != -1) {
                    // Pl Vertex Input State
                    accessor = gltf_accessor_by_index(&gltf, primitive->position);
                    models[i].meshes[j].pl_infos[k].pos_format = (VkFormat)accessor->format;
                    models[i].meshes[j].pl_infos[k].pos_stride = get_accessor_byte_stride(accessor->format);

                    // Setup allocation information return
                    accessors[primitive->position].offset =
                        gltf_accessor_by_index(&gltf, primitive->position)->byte_offset;
                    buffer_views[accessors[primitive->position].buffer_view].type = Type::VERTEX;
                }
                if (primitive->normal != -1) {
                    // Pl Vertex Input State
                    accessor = gltf_accessor_by_index(&gltf, primitive->normal);
                    models[i].meshes[j].pl_infos[k].norm_format = (VkFormat)accessor->format;
                    models[i].meshes[j].pl_infos[k].norm_stride = get_accessor_byte_stride(accessor->format);

                    accessors[primitive->normal].offset =
                        gltf_accessor_by_index(&gltf, primitive->normal)->byte_offset;
                    buffer_views[accessors[primitive->normal].buffer_view].type = Type::VERTEX;
                }
                if (primitive->tangent != -1) {
                    // Pl Vertex Input State
                    accessor = gltf_accessor_by_index(&gltf, primitive->tangent);
                    models[i].meshes[j].pl_infos[k].tang_format = (VkFormat)accessor->format;
                    models[i].meshes[j].pl_infos[k].tang_stride = get_accessor_byte_stride(accessor->format);

                    accessors[primitive->tangent].offset =
                        gltf_accessor_by_index(&gltf, primitive->tangent)->byte_offset;
                    buffer_views[accessors[primitive->tangent].buffer_view].type = Type::VERTEX;
                }
                if (primitive->tex_coord_0 != -1) {
                    // Pl Vertex Input State
                    accessor = gltf_accessor_by_index(&gltf, primitive->tex_coord_0);
                    models[i].meshes[j].pl_infos[k].tex_stride = get_accessor_byte_stride(accessor->format);
                    models[i].meshes[j].pl_infos[k].tex_format = (VkFormat)accessor->format;

                    accessors[primitive->tex_coord_0].offset =
                        gltf_accessor_by_index(&gltf, primitive->tex_coord_0)->byte_offset;
                    buffer_views[accessors[primitive->tex_coord_0].buffer_view].type = Type::VERTEX;
                }

                primitive = (Gltf_Mesh_Primitive*)((u8*)primitive + primitive->stride);
            }

            mesh = (Gltf_Mesh*)((u8*)mesh + mesh->stride);
        }

        reset_to_mark_temp(mark);
    }
}

#endif

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
} // namespace Gpu
#endif
