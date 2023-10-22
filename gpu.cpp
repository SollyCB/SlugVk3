#include "gpu.hpp"
#include "vulkan/vulkan_core.h"
#include "vulkan_errors.hpp"
#include "glfw.hpp"
#include "spirv.hpp"
#include "file.hpp"
#include "builtin_wrappers.h"
#include "gltf.hpp"
#include "image.hpp"

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

void begin(Allocator *alloc) {}
void* queue(Allocator *alloc, u64 size, u64 *offset) { *offset = 1; return (void*)malloc_t(size, 1); }
Allocation* stage(Allocator *alloc) {}
VkBuffer upload(Allocator *alloc, Allocation *allocation) {}

Texture* tex_add(Tex_Allocator *alloc, String uri) { return (Texture*)malloc_t(sizeof(Texture), 8); }
void tex_upload(Allocator *alloc) {}

/*

    Model Allocation System:

    Loading Models:
        Call 'begin()' on an allocator to prepare the allocator for queued allocations.
        Call 'queue()' to increase the size of the current allocation and get a ptr to
        write to the queued size.
        Call 'stage()' to get a pointer to the final total allocation.

    Uploading Models:
        When a model wants to be drawn, its checks the state of its allocations. If they
        are not uploaded, it calls 'upload()' with its allocation. The allocation's state
        is updated indicated it needs to be uploaded. 
        When the allocation is uploaded, its state is updated to reflect this. It also updates
        its 'upload_offset' field with the offset of the allocation in the device buffer.
        When an allocation's state demonstrates that it has been uploaded, the model can loop
        its respective members in order to update their offsets relative to the allocation's
        place in the device buffer.

    Evicting Models:
        When memory is running low in the device buffer, allocations can be evicted. This
        can only happen to allocations whose state == DRAWN. When an allocation is evicted,
        its state will return to STAGED, and its 'prev_offset' field will be set to its
        'upload_offset' field. This allocation can then be overwritten in the device buffer.
        Eviction begins at the end of the buffer, so models which are expected to be persistent
        should be allocated first.

    Updating Offsets:
        When a model is first loaded, its members' offsets should be relative to the beginning
        of the allocation, as if the allocation's offset into the device buffer were zero.
        When the allocation is later upload, the members' offsets will += the offset of the
        allocation into the device buffer.
        When a model is uploaded a subsequent time, its offset must be adjusted according the
        'prev_offset' field of the allocation.

        Example:

        1. model loaded
            position offset = 100 (100 bytes from beginning of allocation)
        2. model uploaded at offset 200
            position offset = 300 (beginning of allocation + beginning of buffer)
        3. model evicted, prev offset = 200
            position offset not updated
        4. model reuploaded at offset 100:
            position offset += (s64)upload - prev == 300 + (100 - 200) = 200
        5. model evicted, reuploaded at offset 600:
            position offset += (s64)upload - prev == 200 + (600 - 100) = 700

*/
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

    char tmp_uri[127];
    memcpy(tmp_uri, dir->str, dir->len);
    memcpy(&tmp_uri[0] + dir->len, model_name->str, model_name->len);
    tmp_uri[dir->len + model_name->len] = '\0';
    
    Gltf gltf = parse_gltf(tmp_uri);

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

    memcpy(tmp_uri, dir->str, dir->len);
    strcpy(&tmp_uri[0] + dir->len, gltf_buf->uri);

    const u8 *buf = file_read_bin_temp_large(tmp_uri, gltf_buf->byte_length);
   
    Gltf_Buffer_View *gltf_view = gltf.buffer_views;
    void *ptr;
    begin(allocs->index);
    begin(allocs->vert);
    for(u32 i = 0; i < view_count; ++i) {
    // This switch seems lame, but in reality gltf views are likely packed by type, so it will be predicted.
        switch(views[i].type) {
        case Data_Type::VERTEX:
            ptr = queue(allocs->vert, gltf_view->byte_length, &views[i].offset);
            memcpy(ptr, buf + gltf_view->byte_offset, gltf_view->byte_length);
            break;
        case Data_Type::INDEX:
            ptr = queue(allocs->index, gltf_view->byte_length, &views[i].offset);
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
    ret.index_alloc = stage(allocs->index);
    ret.vert_alloc = stage(allocs->vert);

    // 3. Loop primitives - set offsets
    gltf_mesh = gltf.meshes;
    for(u32 i = 0; i < mesh_count; ++i) {
        gltf_prim = gltf_mesh->primitives;
        for(u32 j = 0; j < gltf_mesh->primitive_count; ++j) {
            ret.meshes[i].primitives[j].offset_index += views[view_indices[gltf_prim->indices]].offset;
            if (gltf_prim->position != -1)
                ret.meshes[i].primitives[j].offset_pos += views[view_indices[gltf_prim->position]].offset;
            if (gltf_prim->normal != -1)
                ret.meshes[i].primitives[j].offset_norm += views[view_indices[gltf_prim->normal]].offset;
            if (gltf_prim->tangent != -1)
                ret.meshes[i].primitives[j].offset_tang += views[view_indices[gltf_prim->tangent]].offset;
            if (gltf_prim->tex_coord_0 != -1)
                ret.meshes[i].primitives[j].offset_tex += views[view_indices[gltf_prim->tex_coord_0]].offset;

            gltf_prim = (Gltf_Mesh_Primitive*)((u8*)gltf_prim + gltf_prim->stride);
        }
        
        gltf_mesh = (Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }

    // Load Material Data
    Gltf_Material *gltf_mat = gltf.materials;
    Gltf_Texture *gltf_tex;
    Gltf_Sampler *gltf_sampler;
    Gltf_Image *gltf_image;
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
        memcpy(tmp_uri, dir->str, dir->len);


        // base
        gltf_tex = gltf_texture_by_index(&gltf, gltf_mat->base_color_texture_index);
        gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
        gltf_image = gltf_image_by_index(&gltf, gltf_tex->source_image);
        strcpy(&tmp_uri[0] + dir->len, gltf_image->uri); // @Todo update the gltf uris to use String type

        ret.mats[i].tex_base = tex_add(allocs->tex, get_string(tmp_uri));
        ret.mats[i].tex_base->wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
        ret.mats[i].tex_base->wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
        ret.mats[i].tex_base->mag_filter = (VkFilter)gltf_sampler->mag_filter;
        ret.mats[i].tex_base->min_filter = (VkFilter)gltf_sampler->min_filter;


        // metallic roughness
        gltf_tex = gltf_texture_by_index(&gltf, gltf_mat->metallic_roughness_texture_index);
        gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
        gltf_image = gltf_image_by_index(&gltf, gltf_tex->source_image);
        strcpy(&tmp_uri[0] + dir->len, gltf_image->uri);

        ret.mats[i].tex_pbr = tex_add(allocs->tex, get_string(tmp_uri));
        ret.mats[i].tex_pbr->wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
        ret.mats[i].tex_pbr->wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
        ret.mats[i].tex_pbr->mag_filter = (VkFilter)gltf_sampler->mag_filter;
        ret.mats[i].tex_pbr->min_filter = (VkFilter)gltf_sampler->min_filter;


        // normal
        gltf_tex = gltf_texture_by_index(&gltf, gltf_mat->normal_texture_index);
        gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
        gltf_image = gltf_image_by_index(&gltf, gltf_tex->source_image);
        strcpy(&tmp_uri[0] + dir->len, gltf_image->uri);

        ret.mats[i].tex_norm = tex_add(allocs->tex, get_string(tmp_uri));
        ret.mats[i].tex_norm->wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
        ret.mats[i].tex_norm->wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
        ret.mats[i].tex_norm->mag_filter = (VkFilter)gltf_sampler->mag_filter;
        ret.mats[i].tex_norm->min_filter = (VkFilter)gltf_sampler->min_filter;


        // occlusion
        gltf_tex = gltf_texture_by_index(&gltf, gltf_mat->occlusion_texture_index);
        gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
        gltf_image = gltf_image_by_index(&gltf, gltf_tex->source_image);
        strcpy(&tmp_uri[0] + dir->len, gltf_image->uri);

        ret.mats[i].tex_occlusion = tex_add(allocs->tex, get_string(tmp_uri));
        ret.mats[i].tex_occlusion->wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
        ret.mats[i].tex_occlusion->wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
        ret.mats[i].tex_occlusion->mag_filter = (VkFilter)gltf_sampler->mag_filter;
        ret.mats[i].tex_occlusion->min_filter = (VkFilter)gltf_sampler->min_filter;


        // emissive
        gltf_tex = gltf_texture_by_index(&gltf, gltf_mat->emissive_texture_index);
        gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
        gltf_image = gltf_image_by_index(&gltf, gltf_tex->source_image);
        strcpy(&tmp_uri[0] + dir->len, gltf_image->uri);

        ret.mats[i].tex_emissive = tex_add(allocs->tex, get_string(tmp_uri));
        ret.mats[i].tex_emissive->wrap_s = (VkSamplerAddressMode)gltf_sampler->wrap_u;
        ret.mats[i].tex_emissive->wrap_t = (VkSamplerAddressMode)gltf_sampler->wrap_v;
        ret.mats[i].tex_emissive->mag_filter = (VkFilter)gltf_sampler->mag_filter;
        ret.mats[i].tex_emissive->min_filter = (VkFilter)gltf_sampler->min_filter;


        gltf_mat = (Gltf_Material*)((u8*)gltf_mat + gltf_mat->stride);
    }

    reset_to_mark_temp(mark);
    return ret;
}
void free_static_model(Static_Model *model) {
    free_h(model->meshes[0].primitives);
    free_h(model->meshes[0].pl_infos);
    //free_h(model->nodes);
    free_h(model->meshes);
    free_h(model->mats);
}
#endif

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
} // namespace Gpu
#endif
