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
#include "shader.hpp"
#include "assert.h"

#define SHADER_INIT_INFO 0

#define ALLOCATOR_INFO 0
#if ALLOCATOR_INFO

    #define ALLOCATOR_MEMORY_FOOTPRINT 0
    #define TEX_ALLOCATOR_INFO 1

    #if TEX_ALLOCATOR_INFO
        #define TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO 1
    #endif

#endif

// Some arbitrarily large number for allocating temp storage for descriptor sets.
static const u32 DESCRIPTOR_SET_COUNT = 128;

#if DEBUG
static VkDebugUtilsMessengerEXT s_debug_messenger;
VkDebugUtilsMessengerEXT* get_debug_messenger_instance() { return &s_debug_messenger; }
#endif

static Gpu s_Gpu;
Gpu* get_gpu_instance() { return &s_Gpu; }

static VkFormat COLOR_ATTACHMENT_FORMAT;

// @Todo Move the other gpu initialization functions out of the header and into static functions.
static void gpu_create_image_views();
static void gpu_destroy_image_views();
static void gpu_init_shaders();
static void gpu_shutdown_shaders();

void init_gpu() {
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
    gpu_create_image_views();

    pl_load_cache();
    gpu_init_shaders();
}
void kill_gpu(Gpu *gpu) {
    pl_store_cache();
    gpu_shutdown_shaders();

    gpu_destroy_image_views();

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
        "VK_EXT_memory_priority",
        "VK_EXT_descriptor_buffer",
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
    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_FEATURES_EXT,
        .pNext = &mem_priority,
        .descriptorBuffer = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features_full = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &descriptor_buffer_features,
        .features = vk1_features,
    };

    VkPhysicalDeviceFeatures vk1_features_unfilled = vk1_features;
    VkPhysicalDeviceVulkan12Features vk12_features_unfilled = vk12_features;

    VkPhysicalDeviceVulkan13Features vk13_features_unfilled = vk13_features;
    vk13_features_unfilled.pNext = &vk12_features_unfilled;

    VkPhysicalDeviceMemoryPriorityFeaturesEXT mem_priority_empty =  mem_priority;
    mem_priority_empty.pNext = &vk13_features_unfilled;

    VkPhysicalDeviceDescriptorBufferFeaturesEXT descriptor_buffer_empty = descriptor_buffer_features;
    descriptor_buffer_empty.pNext = &mem_priority_empty;

    VkPhysicalDeviceFeatures2 features_full_unfilled = features_full;
    features_full_unfilled.pNext = &descriptor_buffer_empty;

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
        if (descriptor_buffer_features.descriptorBuffer == VK_FALSE) {
            println("Device Index %u does not support Descriptor Buffer", i);
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
        assert(backup_physical_device_index != -1 && "Failed to choose suitable device\n");
        if (backup_physical_device_index == -1) {
            println("Failed to choose suitable device\n");
            return NULL;
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
        gpu->memory.flags |= GPU_MEMORY_DISCRETE_TRANSFER_BIT;
        println("Selected Device (Primary Choice) %s", props.deviceName);

        queue_info_count++;
        transfer_queue_create_info = graphics_queue_create_info;
        transfer_queue_create_info.queueFamilyIndex = transfer_queue_index;
    } else {
        println("Selected Device (Backup) %s", props.deviceName);
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

static void gpu_create_image_views() {
    Gpu *gpu = get_gpu_instance();

    VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.viewType         = VK_IMAGE_VIEW_TYPE_2D;
    info.format           = VK_FORMAT_D16_UNORM;
    info.components       = {}; // zero == component identity swizzle
    info.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};

    VkResult check;
    for(u32 i = 0; i < DEPTH_ATTACHMENT_COUNT; ++i) {
        info.image = gpu->memory.depth_attachments[i];

        check = vkCreateImageView(gpu->device, &info, ALLOCATION_CALLBACKS, &gpu->memory.depth_views[i]);
        DEBUG_OBJ_CREATION(vkCreateImageView, check);
    }
    for(u32 i = 0; i < SHADOW_ATTACHMENT_COUNT; ++i) {
        info.image = gpu->memory.shadow_attachments[i];

        check = vkCreateImageView(gpu->device, &info, ALLOCATION_CALLBACKS, &gpu->memory.shadow_views[i]);
        DEBUG_OBJ_CREATION(vkCreateImageView, check);
    }
}
static void gpu_destroy_image_views() {
    Gpu *gpu = get_gpu_instance();
    for(u32 i = 0; i < DEPTH_ATTACHMENT_COUNT; ++i)
        vkDestroyImageView(gpu->device, gpu->memory.depth_views[i], ALLOCATION_CALLBACKS);
    for(u32 i = 0; i < SHADOW_ATTACHMENT_COUNT; ++i)
        vkDestroyImageView(gpu->device, gpu->memory.shadow_views[i], ALLOCATION_CALLBACKS);
}

// `Surface and `Swapchain
static Window s_Window;
Window* get_window_instance() { return &s_Window; }

void init_window(Gpu *gpu, Glfw *glfw) {
    Window *window = get_window_instance();
    *window = {};

    VkSurfaceKHR surface = create_surface(gpu->instance, glfw);
    VkSwapchainKHR swapchain = create_swapchain(gpu, surface);
    window->swapchain = swapchain;

    reset_viewport_and_scissor_to_window_extent();
}
void kill_window(Gpu *gpu, Window *window) {
    destroy_swapchain(gpu->device, window);
    destroy_surface(gpu->instance, window->info.surface);
    free_h(window->images);
}

VkSurfaceKHR create_surface(VkInstance vk_instance, Glfw *glfw) {
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

    window->info.imageExtent  = surface_capabilities.currentExtent;
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

    reset_viewport_and_scissor_to_window_extent();

    return window->swapchain;
}

VkSwapchainKHR create_swapchain(Gpu *gpu, VkSurfaceKHR surface) {
    Window *window = get_window_instance();
    VkSurfaceCapabilitiesKHR surface_capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu->phys_device, surface, &surface_capabilities);

    VkSwapchainCreateInfoKHR swapchain_info = {};
    swapchain_info.surface      = surface;
    swapchain_info.imageExtent  = surface_capabilities.currentExtent;
    swapchain_info.preTransform = surface_capabilities.currentTransform;

    u32 format_count;
    VkSurfaceFormatKHR *formats;
    u32 present_mode_count;
    VkPresentModeKHR *present_modes;

    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu->phys_device, swapchain_info.surface, &format_count, NULL);

    formats = (VkSurfaceFormatKHR*)malloc_t(sizeof(VkSurfaceFormatKHR) * format_count, 8);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu->phys_device, swapchain_info.surface, &format_count, formats);

    swapchain_info.imageFormat     = formats[0].format;
    COLOR_ATTACHMENT_FORMAT        = swapchain_info.imageFormat;
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

    swapchain_info.minImageCount    = window->image_count;
    swapchain_info.imageArrayLayers = 1;
    swapchain_info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    swapchain_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchain_info.clipped        = VK_TRUE;

    swapchain_info.queueFamilyIndexCount = 1;
    swapchain_info.pQueueFamilyIndices   = &gpu->present_queue_index;

    VkSwapchainKHR swapchain;
    auto check = vkCreateSwapchainKHR(gpu->device, &swapchain_info, ALLOCATION_CALLBACKS, &swapchain);
    DEBUG_OBJ_CREATION(vkCreateSwapchainKHR, check);

    // Image setup
    u32 image_count = surface_capabilities.minImageCount < 2 ? 2 : surface_capabilities.minImageCount;

    // Is this better than just continuing to use s_Window? who cares...
    window->swapchain         = swapchain;
    window->info              = swapchain_info;
    window->info.oldSwapchain = swapchain;

    window->images = (VkImage*)malloc_h(sizeof(VkImage) * window->image_count * 2, 8);
    window->views  = (VkImageView*)(window->images + window->image_count);

    u32 image_count_check;
    vkGetSwapchainImagesKHR(gpu->device, window->swapchain, &image_count_check, NULL);
    assert(image_count_check == image_count && "Incorrect return value from GetSwapchainImages");

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
    VkMemoryDedicatedAllocateInfo dedicate_info   = {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    VkMemoryPriorityAllocateInfoEXT priority_info = {VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT};

    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate_info.pNext = &priority_info;

    VkImageCreateInfo attachment_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    attachment_info.imageType     = VK_IMAGE_TYPE_2D;
    attachment_info.extent        = {.width = 1920, .height = 1080, .depth = 1};
    attachment_info.mipLevels     = 1;
    attachment_info.arrayLayers   = 1;
    attachment_info.samples       = VK_SAMPLE_COUNT_1_BIT;
    attachment_info.tiling        = VK_IMAGE_TILING_OPTIMAL;
    attachment_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    for(u32 i = 0; i < COLOR_ATTACHMENT_COUNT; ++i) {
        attachment_info.format = VK_FORMAT_R8G8B8A8_SRGB;
        attachment_info.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        check = vkCreateImage(device, &attachment_info, ALLOCATION_CALLBACKS, &gpu->memory.color_attachments[i]);
        DEBUG_OBJ_CREATION(vkCreateImage, check);

        vkGetImageMemoryRequirements(device, gpu->memory.color_attachments[i], &mem_req);

        dedicate_info.image = gpu->memory.color_attachments[i];

        priority_info.priority = 1.0;
        priority_info.pNext    = &dedicate_info;

        allocate_info.allocationSize  = mem_req.size;
        allocate_info.memoryTypeIndex = device_mem_type;

        check = vkAllocateMemory(device, &allocate_info, ALLOCATION_CALLBACKS, &gpu->memory.color_mem[i]);
        DEBUG_OBJ_CREATION(vkAllocateMemory, check);

        vkBindImageMemory(device, gpu->memory.color_attachments[i], gpu->memory.color_mem[i], 0);
    }

    for(u32 i = 0; i < DEPTH_ATTACHMENT_COUNT; ++i) {
        attachment_info.format = VK_FORMAT_D16_UNORM;
        attachment_info.usage  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

        check = vkCreateImage(device, &attachment_info, ALLOCATION_CALLBACKS, &gpu->memory.depth_attachments[i]);
        DEBUG_OBJ_CREATION(vkCreateImage, check);

        vkGetImageMemoryRequirements(device, gpu->memory.depth_attachments[i], &mem_req);

        dedicate_info.image = gpu->memory.depth_attachments[i];

        priority_info.priority = 1.0;
        priority_info.pNext    = &dedicate_info;

        allocate_info.allocationSize  = mem_req.size;
        allocate_info.memoryTypeIndex = device_mem_type;

        check = vkAllocateMemory(device, &allocate_info, ALLOCATION_CALLBACKS, &gpu->memory.depth_mem[i]);
        DEBUG_OBJ_CREATION(vkAllocateMemory, check);

        vkBindImageMemory(device, gpu->memory.depth_attachments[i], gpu->memory.depth_mem[i], 0);
    }

    for(u32 i = 0; i < SHADOW_ATTACHMENT_COUNT; ++i) {
        attachment_info.format = VK_FORMAT_D16_UNORM;
        attachment_info.usage  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;

        check = vkCreateImage(device, &attachment_info, ALLOCATION_CALLBACKS, &gpu->memory.shadow_attachments[i]);
        DEBUG_OBJ_CREATION(vkCreateImage, check);

        vkGetImageMemoryRequirements(device, gpu->memory.shadow_attachments[i], &mem_req);

        dedicate_info.image = gpu->memory.shadow_attachments[i];

        priority_info.priority = 1.0;
        priority_info.pNext    = &dedicate_info;

        allocate_info.allocationSize  = mem_req.size;
        allocate_info.memoryTypeIndex = device_mem_type;

        check = vkAllocateMemory(device, &allocate_info, ALLOCATION_CALLBACKS, &gpu->memory.shadow_mem[i]);
        DEBUG_OBJ_CREATION(vkAllocateMemory, check);

        vkBindImageMemory(device, gpu->memory.shadow_attachments[i], gpu->memory.shadow_mem[i], 0);
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
        gpu->memory.vertex_ptrs,
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
        gpu->memory.texture_ptrs,
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
        gpu->memory.vertex_ptrs,
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
        gpu->memory.texture_ptrs,
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

//
// @Todo Update the way I find the memory type indices. What I implemented was a super naive implementation
// before I knew how the stuff worked as well as I do now (which is still not great). The indices should be
// found by first creating the buffers, then selecting from the memory types which are suitable for the buffer,
// as opposed to finding the types, then applying the buffers to them.
//
void allocate_memory() {
    Gpu *gpu                     = get_gpu_instance();
    VkPhysicalDevice phys_device = gpu->phys_device;
    VkDevice device              = gpu->device;

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(phys_device, &mem_props);

    // Select largest heaps for device and host
    u32 largest_heap_device;
    u32 largest_heap_host;
    u64 heap_size_device = 0;
    u64 heap_size_host = 0;
    for(u32 i = 0; i < mem_props.memoryHeapCount; ++i)
        if (mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            if (mem_props.memoryHeaps[i].size > heap_size_device) {
                heap_size_device = mem_props.memoryHeaps[i].size;
                largest_heap_device = i;
            }
        } else {
            if (mem_props.memoryHeaps[i].size > heap_size_host) {
                heap_size_host = mem_props.memoryHeaps[i].size;
                largest_heap_host = i;
            }
        }

    //
    // @Unused these final heap indices were intended for allocating proportions of device memory
    // rather than fixed sizes because it is sooo variable and can be adapted to by the allocator
    // implmentations, but I am not going bother with that yet. (The allocators already totally work
    // with this, but I am not implementing actually allocating the proportion way yet. These values
    // are set, but do nothing.)
    //
    u32 final_heap_device;
    u32 final_heap_host;
    u32 final_heap_both;

    u32 host_mem_type;
    u32 device_mem_type;
    u32 both_mem_type;

    if (gpu->info.props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {

        gpu->memory.flags |= GPU_MEMORY_UMA_BIT;

        integrated_gpu_get_memory_type(
            &mem_props,
            largest_heap_device,
            largest_heap_host,
            &device_mem_type,
            &host_mem_type,
            &final_heap_device,
            &final_heap_host);

        setup_memory_integrated(device_mem_type, host_mem_type);

        gpu->memory.attachment_mem_index = device_mem_type;
        gpu->memory.vertex_mem_index     = device_mem_type;
        gpu->memory.uniform_mem_index    = device_mem_type;

        assert(mem_props.memoryTypes[device_mem_type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        assert(mem_props.memoryTypes[device_mem_type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    } else if (gpu->info.props.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        discrete_gpu_get_memory_type( // @Note Assumes there is some shared heap (for uniform buffers)
            &mem_props,
            largest_heap_device,
            largest_heap_host,
            &device_mem_type,
            &host_mem_type,
            &both_mem_type,
            &final_heap_device,
            &final_heap_host,
            &final_heap_both); // Both is device local + host visible

        setup_memory_discrete(device_mem_type, host_mem_type, both_mem_type);

        gpu->memory.attachment_mem_index = device_mem_type;
        gpu->memory.vertex_mem_index     = device_mem_type;
        gpu->memory.uniform_mem_index    = both_mem_type;
    }

                        /*Create Descriptor Buffers*/

    VkBuffer       sampler_descriptor_buffer;
    VkBuffer       resource_descriptor_buffer;
    VkDeviceMemory descriptor_buffer_memory;

    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};

    buffer_info.size  = DESCRIPTOR_BUFFER_SIZE;
    buffer_info.usage = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    auto check = vkCreateBuffer(device, &buffer_info, ALLOCATION_CALLBACKS, &sampler_descriptor_buffer);
    DEBUG_OBJ_CREATION(vkCreateBuffer, check);

    buffer_info.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    check = vkCreateBuffer(device, &buffer_info, ALLOCATION_CALLBACKS, &resource_descriptor_buffer);
    DEBUG_OBJ_CREATION(vkCreateBuffer, check);

    VkPhysicalDeviceDescriptorBufferPropertiesEXT buf_props;
    buf_props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};

    VkPhysicalDeviceProperties2 props2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &buf_props;

    vkGetPhysicalDeviceProperties2(gpu->phys_device, &props2);

    VkMemoryRequirements mem_req[2];
    vkGetBufferMemoryRequirements(device, sampler_descriptor_buffer,  &mem_req[0]);
    vkGetBufferMemoryRequirements(device, resource_descriptor_buffer, &mem_req[1]);

    u64 sampler_size = mem_req[0].size;
    sampler_size     = align(sampler_size, mem_req[1].alignment);

    u32 descriptor_mem_type_index = Max_u32;
    for(u32 i = 0; i < mem_props.memoryTypeCount; ++i) {
        if (!((1 << i) & mem_req[0].memoryTypeBits & mem_req[1].memoryTypeBits))
            continue;

        if ((mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)     &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
        {
            descriptor_mem_type_index = i;
            break;
        }
    }

    if (descriptor_mem_type_index == Max_u32) {
        println("Unable to find memory type for descriptor buffers");
        assert(false && "See Above Me");
        return;
    }

    VkMemoryAllocateFlagsInfo allocate_flags = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    allocate_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

    VkMemoryAllocateInfo alloc_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc_info.pNext           = &allocate_flags;
    alloc_info.allocationSize  = sampler_size + mem_req[1].size;
    alloc_info.memoryTypeIndex = descriptor_mem_type_index; // This is device local + host visible memory

    check = vkAllocateMemory(device, &alloc_info, ALLOCATION_CALLBACKS, &descriptor_buffer_memory);
    DEBUG_OBJ_CREATION(vkAllocateMemory, check);

    check = vkBindBufferMemory(device, sampler_descriptor_buffer, descriptor_buffer_memory, 0);
    DEBUG_OBJ_CREATION(vkBindBufferMemory, check);

    check = vkBindBufferMemory(device, resource_descriptor_buffer, descriptor_buffer_memory, sampler_size);
    DEBUG_OBJ_CREATION(vkBindBufferMemory, check);

    assert(DESCRIPTOR_BUFFER_SIZE < buf_props.maxSamplerDescriptorBufferRange);
    assert(DESCRIPTOR_BUFFER_SIZE < buf_props.maxResourceDescriptorBufferRange);

    if (DESCRIPTOR_BUFFER_SIZE > buf_props.maxSamplerDescriptorBufferRange ||
        DESCRIPTOR_BUFFER_SIZE > buf_props.maxResourceDescriptorBufferRange)
    {
        println("Requested descriptor buffer size + buffer device address would overflow max buffer range");
        return;
    }

    u8 *sampler_ptr;

    void *tmp = (void*)sampler_ptr; // Such a dumb work around. This or cast in create_allocator call...
    vkMapMemory(device, descriptor_buffer_memory, 0, VK_WHOLE_SIZE, 0x0, &tmp);

    u8 *resource_ptr = sampler_ptr + sampler_size;

    gpu->memory.sampler_descriptor_buffer  = sampler_descriptor_buffer;
    gpu->memory.resource_descriptor_buffer = resource_descriptor_buffer;
    gpu->memory.descriptor_buffer_memory   = descriptor_buffer_memory;
    gpu->memory.sampler_descriptor_ptr     = sampler_ptr;
    gpu->memory.resource_descriptor_ptr    = resource_ptr;
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
    for(u32 i = 0; i < SHADOW_ATTACHMENT_COUNT; ++i) {
        vkDestroyImage(device, gpu->memory.shadow_attachments[i], ALLOCATION_CALLBACKS);
        vkFreeMemory(device, gpu->memory.shadow_mem[i], ALLOCATION_CALLBACKS);
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

    // Descriptor
    vkDestroyBuffer(device, gpu->memory.sampler_descriptor_buffer, ALLOCATION_CALLBACKS);
    vkDestroyBuffer(device, gpu->memory.resource_descriptor_buffer, ALLOCATION_CALLBACKS);
    vkFreeMemory(device, gpu->memory.descriptor_buffer_memory, ALLOCATION_CALLBACKS);
}

// `Shaders + Descriptors
Descriptor_Allocator get_descriptor_allocator(u64 size, void *mem, VkBuffer buf) {
    Gpu *gpu        = get_gpu_instance();
    VkDevice device = gpu->device;

    VkBufferDeviceAddressInfo address_info = {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    address_info.buffer = buf;

    Descriptor_Allocator ret;
    ret.cap  = size;
    ret.used = 0;
    ret.mem  = (u8*)mem;
    ret.buf  = buf;
    ret.buffer_address = vkGetBufferDeviceAddress(device, &address_info);

    VkPhysicalDeviceDescriptorBufferPropertiesEXT buf_props;
    buf_props = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT};

    VkPhysicalDeviceProperties2 props2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    props2.pNext = &buf_props;

    vkGetPhysicalDeviceProperties2(gpu->phys_device, &props2);

    ret.info = buf_props;

    return ret;
}

u8* descriptor_allocate_layout(Descriptor_Allocator *alloc, u64 size, u64 *offset) {
    u8 *ret      = (u8*)alloc->mem + alloc->used;
    *offset      = alloc->used;
    size         = align(size, alloc->info.descriptorBufferOffsetAlignment);
    alloc->used += size;

    assert(alloc->used <= alloc->cap && "Descriptor Allocator Overflow");
    return ret;
}

void descriptor_write_combined_image_sampler(Descriptor_Allocator *alloc, u32 count,
                                             VkDescriptorDataEXT *datas, u8 *mem)
{
    /* From the Vulkan Spec for vkGetDescriptorEXT:

            "If the VkPhysicalDeviceDescriptorBufferPropertiesEXT::combinedImageSamplerDescriptorSingleArray
            property is VK_FALSE the implementation requires an array of
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER descriptors to be written into a descriptor buffer as an
            array of image descriptors, immediately followed by an array of sampler descriptors. Applications
            must write the first VkPhysicalDeviceDescriptorBufferPropertiesEXT::sampledImageDescriptorSize
            bytes of the data returned through pDescriptor to the first array, and the remaining
            VkPhysicalDeviceDescriptorBufferPropertiesEXT::samplerDescriptorSize bytes of the data to the
            second array."

        @Note It is unclear what exactly you do if it is not an array...
    */

    VkDescriptorGetInfoEXT get_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    get_info.type                   =  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;

    u64 combined_size = alloc->info.combinedImageSamplerDescriptorSize;
    u64 image_size;
    u64 sampler_size;

    u8 *tmp;
    u64 sampler_offset;
    if (!alloc->info.combinedImageSamplerDescriptorSingleArray) {
        tmp            = malloc_t(combined_size, alloc->info.descriptorBufferOffsetAlignment);
        sampler_offset = image_size * count;
        image_size     = alloc->info.sampledImageDescriptorSize;
        sampler_size   = alloc->info.samplerDescriptorSize;
    }

    VkDevice device = get_gpu_instance()->device;
    for(u32 i = 0; i < count; ++i) {
        get_info.data = datas[i];

        // These branches will always be predicted.
        if (alloc->info.combinedImageSamplerDescriptorSingleArray) {
            vkGetDescriptor(device, &get_info, combined_size, mem + (i * combined_size));
        } else {
            //
            // This is awkward, but I guess it is the only way...
            //
            vkGetDescriptor(device, &get_info, combined_size, tmp);
            memcpy(mem + (i * image_size), tmp, image_size);
            memcpy(mem + sampler_offset + (i * sampler_size), tmp + image_size, sampler_size);
        }
    }
}

void descriptor_write_uniform_buffer(Descriptor_Allocator *alloc, u32 count, VkDescriptorDataEXT *datas, u8 *mem) {
    VkDevice device = get_gpu_instance()->device;

    VkDescriptorGetInfoEXT get_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    get_info.type                   =  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    u64 ubo_size = alloc->info.uniformBufferDescriptorSize;
    for(u32 i = 0; i < count; ++i) {
        get_info.data = datas[i];
        vkGetDescriptor(device, &get_info, ubo_size, mem + (i * ubo_size));
    }
}

void descriptor_write_input_attachment(Descriptor_Allocator *alloc, u32 count, VkDescriptorDataEXT *datas, u8 *mem) {
    VkDevice device = get_gpu_instance()->device;

    VkDescriptorGetInfoEXT get_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT};
    get_info.type                   =  VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;

    u64 ia_size = alloc->info.inputAttachmentDescriptorSize;
    for(u32 i = 0; i < count; ++i) {
        get_info.data = datas[i];
        vkGetDescriptor(device, &get_info, ia_size, mem + (i * ia_size));
    }
}

struct Set_Layout_Info {
    u32 count;
    VkDescriptorSetLayoutBinding *bindings;
};
static void gpu_init_shaders() {
    Shader_Memory ret = {};

    ret.shaders =                (Shader*) malloc_h(sizeof(Shader)                * ret.shader_cap,         8);
    ret.layouts = (VkDescriptorSetLayout*) malloc_h(sizeof(VkDescriptorSetLayout) * ret.descriptor_set_cap, 8);

    u32 shader_count = g_shader_count;

    #if SHADER_INIT_INFO
    println("Loading %u shaders...", shader_count);
    #endif

    const u32 *pcode;
    u64 size;

    Gpu *gpu = get_gpu_instance();
    VkDevice device = gpu->device;

    u32 max_sets = DESCRIPTOR_SET_COUNT;

    Parsed_Spirv spirv;
    u32 j;
    u32 layout_set;
    u32 binding_count;
    u32 set_array_index = 0;
    u32 descriptor_counts[11];
    memset(descriptor_counts, 0, sizeof(u32) * 11);

    VkDescriptorSetLayoutBinding *bindings = (VkDescriptorSetLayoutBinding*)malloc_t(sizeof(VkDescriptorSetLayoutBinding) * DESCRIPTOR_SET_COUNT, 8);
    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};

    //
    // @Todo @Test Check the values for the set indices into the descriptor set array in the shaders.
    // Just have to make sure that they are always being set properly (I made an update while tired)
    //

    #if DEBUG
    u32 *tmp_set_indices = (u32*)malloc_t(sizeof(u32) * max_sets, 8);
    #endif

    VkResult check;
    bool allocate_set = false;

    Shader *pshader;
    VkShaderModuleCreateInfo module_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};

    u32 total_code_size = 0;
    for(u32 i = 0; i < shader_count; ++i) {

        pcode = (const u32*)file_read_bin_temp(g_shader_file_names[i], &size);
        spirv = parse_spirv(size, pcode);


        #if SHADER_INIT_INFO
        println("    %s, size: %u", g_shader_file_names[i], size);
        #endif

        total_code_size += size;

        pshader            = &ret.shaders[ret.shader_count];
       *pshader            = {};
        pshader->id        = (Shader_Id)i;
        pshader->stage     = spirv.stage;

        module_info.codeSize = size;
        module_info.pCode    = pcode;
        check = vkCreateShaderModule(device, &module_info, ALLOCATION_CALLBACKS, &pshader->module);
        DEBUG_OBJ_CREATION(vkCreateShaderModule, check);

        layout_set    = spirv.bindings[0].set;
        binding_count = 0;
        allocate_set  = false;

        for(j = 0; j < spirv.binding_count; ++j) {
            allocate_set = true; // Indicate that when the loop exits, there was an active set being parsed.

            assert(spirv.bindings[j].set == layout_set || spirv.bindings[j].set == layout_set + 1 &&
                  "Sets must be in increasing sequential order (sorted at the end of parse_spirv(..))");

            // If the  set index changes, the parse of the old set is complete and the layout can be created.
            // @Note This works because sets are in order, see above assert.
            if (layout_set < spirv.bindings[j].set) {

                descriptor_set_layout_create_info.bindingCount = binding_count;
                descriptor_set_layout_create_info.pBindings     = bindings;

                check = vkCreateDescriptorSetLayout(
                            device,
                           &descriptor_set_layout_create_info,
                            ALLOCATION_CALLBACKS,
                           &ret.layouts[ret.layout_count]);
                DEBUG_OBJ_CREATION(vkCreateDescriptorSetLayout, check);

                #if DEBUG
                tmp_set_indices[pshader->layout_count] = layout_set;
                #endif

                pshader->layout_index = set_array_index;
                pshader->layout_count++;
                ret.layout_count++;

                assert(ret.layout_count <= ret.descriptor_set_cap);
                if (ret.layout_count > ret.descriptor_set_cap)
                    return;

                layout_set    = spirv.bindings[j].set;
                binding_count = 0;
            }

            bindings[binding_count].stageFlags      = spirv.stage;
            bindings[binding_count].binding         = spirv.bindings[j].binding;
            bindings[binding_count].descriptorType  = spirv.bindings[j].type;
            bindings[binding_count].descriptorCount = spirv.bindings[j].count;

            binding_count++;
            descriptor_counts[(u32)spirv.bindings[j].type] += spirv.bindings[j].count;
        }

        if (allocate_set) { // There was a set being parsed when loop exited, so wrap up the parse.

            descriptor_set_layout_create_info.bindingCount = binding_count;
            descriptor_set_layout_create_info.pBindings     = bindings;

            check = vkCreateDescriptorSetLayout(
                        device,
                       &descriptor_set_layout_create_info,
                        ALLOCATION_CALLBACKS,
                       &ret.layouts[ret.layout_count]);

            DEBUG_OBJ_CREATION(vkCreateDescriptorSetLayout, check);

            #if DEBUG
            tmp_set_indices[pshader->layout_count] = layout_set;
            #endif

            pshader->layout_index = set_array_index;
            pshader->layout_count++;
            ret.layout_count++;

            assert(ret.layout_count <= ret.descriptor_set_cap);
            if (ret.layout_count > ret.descriptor_set_cap)
                return;
        }

        #if DEBUG
        pshader->layout_indices = (u32*)malloc_h(sizeof(u32) * pshader->layout_count, 8);
        memcpy(pshader->layout_indices, tmp_set_indices, sizeof(u32) * pshader->layout_count);
        #endif

        set_array_index += pshader->layout_count;
        ret.shader_count++;

        assert(ret.shader_count <= ret.shader_cap);
        if (ret.shader_count > ret.shader_cap)
            return;
    }

    gpu->shader_memory = ret;

    #if SHADER_INIT_INFO
    println("Total shader code size (spirv): %u", total_code_size);
    #endif

    return;

    //
    // Below is old code for using descriptor pools; keeping it around in case I find that descriptor buffer is not
    // working out as intended. This function does not do anything more beyond this point.
    //

    /*
    VkDescriptorPoolSize *pool_sizes = (VkDescriptorPoolSize*)malloc_t(sizeof(VkDescriptorPoolSize*) * 11, 8);
    u32 pool_size_count = 0;
    for(u32 i = 0; i < 11; ++i) {
        if (descriptor_counts[i] == 0)
            continue;

        pool_sizes[pool_size_count].type = (VkDescriptorType)i;
        pool_sizes[pool_size_count].descriptorCount = descriptor_counts[i];
        pool_size_count++;
    }

    VkDescriptorPoolCreateInfo pool_create_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_create_info.maxSets       = ret.descriptor_set_count;
    pool_create_info.poolSizeCount = pool_size_count;
    pool_create_info.pPoolSizes    = pool_sizes;

    check = vkCreateDescriptorPool(device, &pool_create_info, ALLOCATION_CALLBACKS, &ret.descriptor_pools[0]);
    DEBUG_OBJ_CREATION(vkCreateDescriptorPool, check);

    if (check != VK_SUCCESS)
        return {};
    else
        ret.descriptor_pool_count++;

    VkDescriptorSetAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate_info.descriptorPool     = ret.descriptor_pools[0];
    allocate_info.descriptorSetCount = ret.descriptor_set_count;
    allocate_info.pSetLayouts        = ret.descriptor_set_layouts;

    check = vkAllocateDescriptorSets(device, &allocate_info, ret.descriptor_sets);
    DEBUG_OBJ_CREATION(vkAllocateDescriptorSets, check);

    if (check != VK_SUCCESS)
        return {};
    */
}
static void gpu_shutdown_shaders() {
    Gpu *gpu           = get_gpu_instance();
    VkDevice device    = gpu->device;
    Shader_Memory *mem = &gpu->shader_memory;

    for(u32 i = 0; i < mem->shader_count; ++i)
        vkDestroyShaderModule(device, mem->shaders[i].module, ALLOCATION_CALLBACKS);
    for(u32 i = 0; i < mem->layout_count; ++i)
        vkDestroyDescriptorSetLayout(device, mem->layouts[i], ALLOCATION_CALLBACKS);

    #if DEBUG
    for(u32 i = 0; i < mem->shader_count; ++i)
        free_h(mem->shaders[i].layout_indices);
    #endif

    free_h(mem->shaders);
    free_h(mem->layouts);
    *mem = {};

    /*
    for(u32 i = 0; i < mem->descriptor_pool_count; ++i)
        vkDestroyDescriptorPool(device, mem->descriptor_pools[i], ALLOCATION_CALLBACKS);

    free_h(mem->descriptor_pools);
    free_h(mem->descriptor_sets);
    */
}


u32 get_accessor_byte_stride(Gltf_Accessor_Format accessor_format);

        /* Begin Allocator Helper Algorithms */
// @Note Many of these algorithm implementations are not heavily documented, the principle reason being
// that they cannot be explained in english much better than the code explains itself. These algorithms
// do not have any dependencies that need explaining or documenting. They work entirely on fundamental
// types.
struct Weight_Args {
    Gpu_Allocation *allocations;
    u8  *weights;
    u8  *states;
    u32 *indices;
    u32  count;
    u32  idx;
    u8   inc;
    u8   dec;
};
struct Tex_Weight_Args {
    Gpu_Tex_Allocation *allocations;
    u8  *weights;
    u8  *states;
    u32 *indices;
    u32  count;
    u32  idx;
    u8   inc;
    u8   dec;
};
static u32 adjust_allocation_weights(Tex_Weight_Args *args) {
    u32 idx = args->indices[args->idx];

    // Ensure inc and dec are in range
    args->inc |= max8_if_true(args->inc > 127);
    args->dec |= max8_if_true(args->dec > 127);
    args->inc &= 127;
    args->dec &= 127;

    // Find incremented weight
    u8 w   = args->weights[idx];
    u8 tmp = max8_if_true(w + args->inc >= w || w + args->inc > 127); // stay in signed range
    w      = (w + args->inc) | tmp;
    w     &= 0b0111'1111; // Ensure weight is not negative

    __m128i a;
    __m128i b = _mm_set1_epi8(args->dec);
    __m128i c = _mm_set1_epi8(w);
    __m128i d;
    __m128i e;
    u32 inc = 0;
    u32 tmp32;
    u16 mask;

    u32 count = args->count;

    // if weights didnt change, dont loop
    count &= max32_if_true(args->inc || args->dec);

    u32 pos = idx;
    bool new_pos_found = false;

    u8 *weights = args->weights;
    for(inc = 0; inc < count; inc += 16) {
        // Decrement values
        a = _mm_load_si128((__m128i*)(weights + inc));
        d = _mm_cmpgt_epi8(a, b);
        e = _mm_and_si128(b, d);
        a = _mm_sub_epi8(a, e);
        a = _mm_and_si128(a, d);
        _mm_store_si128((__m128i*)(weights + inc), a);

        // Find new pos (elements are stored high to low by weight,
        // so looping from 0..count we set pos to the index of the
        // first weight which is lower).
        d     = _mm_cmplt_epi8(a, c);
        mask  = _mm_movemask_epi8(d);

        tmp32 = max32_if_true(pop_count16(mask) > 0 && !new_pos_found);
        new_pos_found = tmp32 || new_pos_found;
        pos  -= pos & tmp32;
        pos  += (count_trailing_zeros_u16(mask) + inc) & tmp32;
    }

    Gpu_Tex_Allocation *allocations = args->allocations;
    Gpu_Tex_Allocation allocation   = allocations[idx];
    u8 *states = args->states;
    u8  state  = states[idx];

    // If no new pos was found in range, pos = idx;
    pos -= pos & max32_if_true(pos >= count);
    pos += idx & max32_if_true(pos >= count);

    memmove(weights     + pos + 1, weights     + pos, idx - pos);
    memmove(states      + pos + 1, states      + pos, idx - pos);
    memmove(allocations + pos + 1, allocations + pos, (idx - pos) * sizeof(Gpu_Tex_Allocation));

    weights    [pos] = w;
    states     [pos] = state;
    allocations[pos] = allocation;

    b = _mm_set1_epi32(pos - 1);
    c = _mm_set1_epi32(idx);
    inc = 0;

    u32 *indices = args->indices;
    while(inc < count) {
        a = _mm_load_si128((__m128i*)(indices + inc));
        d = _mm_cmpgt_epi32(a, b);
        e = _mm_cmplt_epi32(a, c);
        d = _mm_and_si128(d, e);
        e = _mm_set1_epi32(0x01);
        d = _mm_and_si128(d, e);
        a = _mm_add_epi32(a, d);
        _mm_store_si128((__m128i*)(indices + inc), a);
        inc += 4;
    }
    indices[args->idx] = pos;
    return pos;
}
static u32 adjust_allocation_weights(Weight_Args *args) {
    u32 idx = args->indices[args->idx];

    // Ensure inc and dec are in range
    args->inc |= max8_if_true(args->inc > 127);
    args->dec |= max8_if_true(args->dec > 127);
    args->inc &= 127;
    args->dec &= 127;

    // Find incremented weight
    u8 w   = args->weights[idx];
    u8 tmp = max8_if_true(w + args->inc >= w || w + args->inc > 127); // stay in signed range
    w      = (w + args->inc) | tmp;
    w     &= 0b0111'1111; // Ensure weight is not negative

    __m128i a;
    __m128i b = _mm_set1_epi8(args->dec);
    __m128i c = _mm_set1_epi8(w);
    __m128i d;
    __m128i e;
    u32 inc = 0;
    u32 tmp32;
    u16 mask;

    u32 count = args->count;

    // if weights didnt change, dont loop
    count &= max32_if_true(args->inc || args->dec);

    u32 pos = idx;
    bool new_pos_found = false;

    u8 *weights = args->weights;
    for(inc = 0; inc < count; inc += 16) {
        // Decrement values
        a = _mm_load_si128((__m128i*)(weights + inc));
        d = _mm_cmpgt_epi8(a, b);
        e = _mm_and_si128(b, d);
        a = _mm_sub_epi8(a, e);
        a = _mm_and_si128(a, d);
        _mm_store_si128((__m128i*)(weights + inc), a);

        // Find new pos (elements are stored high to low by weight,
        // so looping from 0..count we set pos to the index of the
        // first weight which is lower).
        d     = _mm_cmplt_epi8(a, c);
        mask  = _mm_movemask_epi8(d);

        tmp32 = max32_if_true(pop_count16(mask) > 0 && !new_pos_found);
        new_pos_found = tmp32 || new_pos_found;
        pos  -= pos & tmp32;
        pos  += (count_trailing_zeros_u16(mask) + inc) & tmp32;
    }

    Gpu_Allocation *allocations = args->allocations;
    Gpu_Allocation allocation   = allocations[idx];
    u8 *states = args->states;
    u8  state  = states[idx];

    // If no new pos was found in range, pos = idx;
    pos -= pos & max32_if_true(pos >= count);
    pos += idx & max32_if_true(pos >= count);

    memmove(weights     + pos + 1, weights     + pos, idx - pos);
    memmove(states      + pos + 1, states      + pos, idx - pos);
    memmove(allocations + pos + 1, allocations + pos, (idx - pos) * sizeof(Gpu_Allocation));

    weights    [pos] = w;
    states     [pos] = state;
    allocations[pos] = allocation;

    b = _mm_set1_epi32(pos - 1);
    c = _mm_set1_epi32(idx);
    inc = 0;

    u32 *indices = args->indices;
    while(inc < count) {
        a = _mm_load_si128((__m128i*)(indices + inc));
        d = _mm_cmpgt_epi32(a, b);
        e = _mm_cmplt_epi32(a, c);
        d = _mm_and_si128(d, e);
        e = _mm_set1_epi32(0x01);
        d = _mm_and_si128(d, e);
        a = _mm_add_epi32(a, d);
        _mm_store_si128((__m128i*)(indices + inc), a);
        inc += 4;
    }
    indices[args->idx] = pos;
    return pos;
}

void adjust_weights(u32 count, u8 *weights, u32 idx, s8 inc, s8 dec) {
    // Ensure inc and dec are in range
    inc |= max8_if_true(inc > 127);
    dec |= max8_if_true(dec > 127);
    inc &= 127;
    dec &= 127;

    // Find incremented weight
    u8 w   = weights[idx];
    u8 tmp = max8_if_true(w + inc >= w || w + inc > 127); // stay in signed range
    w      = (w + inc) | tmp;
    w     &= 0b0111'1111; // Ensure weight is not negative

    // Ensure weight is not negative
    w &= 0b0111'1111;

    __m128i a;
    __m128i b = _mm_set1_epi8(dec);
    __m128i c;

    // dont loop if nothing will change
    count &= max32_if_true(inc || dec);

    for(u32 i = 0; i < count; i += 16) {
        a = _mm_load_si128((__m128i*)(weights + i));
        c = _mm_cmpgt_epi8(a, b);
        a = _mm_and_si128(a, c);
        c = _mm_and_si128(b, c);
        a = _mm_sub_epi8(a, c);
        _mm_store_si128((__m128i*)(weights + i), a);
    }
    weights[idx] = w; // revert loop decrement
}

u32 find_lowest_weight_with_without_flags(u32 count, u8 *weights, u8 *flags, u8 with_flags, u8 without_flags) {
    // Use signed bytes as cmp ops use sign
    s8 *s_weights = (s8*)weights;

    __m128i a;
    __m128i b = _mm_set1_epi8(with_flags);
    __m128i c = _mm_set1_epi8(with_flags | without_flags);
    __m128i d;
    __m128i e;

    s8 lowest = 0b0111'1111; // lol I had this set to Max_u8, comparing negatives
    u32 ret   = Max_u32;
    u16 mask;
    u32 tz;
    u32 pc;
    for(u32 i = 0; i < count; i += 16) {
        a = _mm_load_si128((__m128i*)(s_weights + i));
        d = _mm_load_si128((__m128i*)(flags     + i));
        d = _mm_and_si128(d, c);
        d = _mm_cmpeq_epi8(d, b);

        mask = 1; // ensure we loop
        while(mask) {
            e = _mm_set1_epi8(lowest);
            e = _mm_cmplt_epi8(a, e);
            e = _mm_and_si128(e, d);
            mask = _mm_movemask_epi8(e);

            tz = count_trailing_zeros_u16(mask);
            pc = max32_if_true(pop_count16(mask));

            ret    -= (ret & pc);
            ret    += (i + tz) & pc;
            lowest  = s_weights[ret];
        }
    }
    return ret;
}
/*
   eject_repeat_indices(..)
   Branchless function to remove duplicate indices from an array, with specific caveats:
       1. There can only be ONE duplicate for EACH entry within the count.
       2. It must be safe to deref array[align(len, 4) - 1] (sse simd, 4 * sizeof(u32))
          (it is safe to have extra duplicates inside the deref range, but beyond the given count)
       Example:
          array len = 9, array data = malloc(sizeof(u32) * 12) // size allocated aligned to 16 bytes
              OK:   1, 1, 2, 2, 3, 3, 4, 4, 5
          NOT OK:   1, 1, 1, 2, 2, 3, 3, 4, 4    (the one has three entries)
              OK:   1, 1, 5, 2, 2, 3, 3, 4, 4, 1 (the third one is beyond the count)

    (The clamp functions compile to branchless assembly.)

    @Todo It would cool to also try to trim down on multiplies, but I mean then you are really
    getting negligible lol.
*/
static u32 eject_repeat_indices(u32 count, u32 *indices) {
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

        mask  = _mm_movemask_epi8(a);
        mask &= mask_mask;
        mask ^= 1 << (4 * (i & 3)); // clear self match
        mask &= 0xffff >> (((4 - ((count - dec) & 3)) * ((u32)(count - dec < inc + 4))) << 2); // clear overflow matches beyond count

        tmp  = (u32)(pop_count16(mask) >= 1);
        dec += tmp;
        idx  = inc + (count_trailing_zeros_u16(mask) >> 2);
        idx *= tmp;

        // shuffle everything backwards if a dupe was found
        move  = (u32)(inc >= idx) & tmp;
        mov_t = inc * move;
        mov_f = max_clamp32((count - dec) - 1, inc + 1) * move;
        indices[mov_t] = indices[mov_f];

        move  = (u32)(inc + 1 >= idx) & tmp;
        mov_t = (inc + 1) * move;
        mov_f = max_clamp32(count - 1, inc + 2) * move;
        indices[mov_t] = indices[mov_f];

        move  = (u32)(inc + 2 >= idx) & tmp;
        mov_t = (inc + 2) * move;
        mov_f = max_clamp32(count - 1, inc + 3) * move;
        indices[mov_t] = indices[mov_f];

        move  = (u32)(inc + 3 >= idx) & tmp;
        mov_t = (inc + 3) * move;
        mov_f = max_clamp32(count - 1, inc + 4) * move;
        indices[mov_t] = indices[mov_f];

        inc += 4;
        while(inc + 4 < count - dec) { // do not check into potential overflow range in loop
            a = _mm_load_si128((__m128i*)(indices + inc));
            a = _mm_cmpeq_epi32(a, b);

            mask = _mm_movemask_epi8(a);
            mask &= mask_mask;

            tmp  = (u32)(pop_count16(mask) >= 1);
            dec += tmp;
            idx  = inc + (count_trailing_zeros_u16(mask) >> 2);
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

        mask  = _mm_movemask_epi8(a);
        mask &= mask_mask * ((u32)(inc < count - dec));
        mask &= 0xffff >> (((4 - ((count - dec) & 3)) << 2) * ((count - dec) & 3));

        tmp  = (u32)(pop_count16(mask) >= 1);
        dec += tmp;
        idx  = inc + (count_trailing_zeros_u16(mask) >> 2);
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
        inc   = (i << 6) + shift;

        tz     = count_trailing_zeros_u64(~mask);
        mask >>= tz;
        shift += tz;
        inc   += tz;
        mask  |= 0x8000000000000000 >> (shift - 1);

        while(shift < 64 - tail) {
            tz = count_trailing_zeros_u64(mask);
            if (tz >= req_count)
                return inc;

            mask >>= tz;
            shift += tz;
            inc   += tz;

            tz     = count_trailing_zeros_u64(~mask);
            mask >>= tz;
            shift += tz;
            inc   += tz;
        }
    }
    return Max_u32;
}

static u32 get_block_size(u32 count, u64 *masks, u32 offset) {
    u32 mask_idx = offset >> 6;
    u32 bit_idx  = offset & 63;
    u64 restore  = masks[mask_idx];

    u64 mask = masks[mask_idx];
    mask   >>= bit_idx;
    mask   <<= bit_idx;

    u32 pc   = pop_count64(mask);
    u32 tc   = 64 & (Max_u32 + pc);
    tc      += count_trailing_zeros_u64(mask) & ~(Max_u32 + pc);
    tc      -= bit_idx;

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

static bool is_range_free(u32 count, u64 *bits, u32 offset, u32 range) {
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

static u32 find_hash_idx(u32 count, u64 *hashes, u64 hash) {
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

static void recreate_images(Gpu_Tex_Allocator *alloc, u32 count, u32 *indices) {
    VkDevice device = get_gpu_instance()->device;

    Settings *settings                 = get_global_settings();
    u32 mip_levels                     = settings->mip_levels;
    VkSampleCountFlagBits sample_count = settings->sample_count;

    VkResult check;
    Gpu_Tex_Allocation *tex;
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    for(u32 i = 0; i < count; ++i) {
        tex                = &alloc->allocations[indices[i]];

        info.imageType     = VK_IMAGE_TYPE_2D;
        info.extent        = {.width = tex->width, .height = tex->height, .depth = 1};
        info.mipLevels     = mip_levels;
        info.arrayLayers   = 1;
        info.samples       = sample_count;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        info.usage         = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        info.format        = VK_FORMAT_R8G8B8A8_SRGB;

        vkDestroyImage(device, tex->image, ALLOCATION_CALLBACKS);
        check = vkCreateImage(device, &info, ALLOCATION_CALLBACKS, &tex->image);
        DEBUG_OBJ_CREATION(vkCreateImage, check);
    }
}
    /* End Allocator Helper Algorithms */

    /* Model Texture and Vertex/Index Attribute Allocators */
Gpu_Allocator_Result create_allocator(Gpu_Allocator_Config *config, Gpu_Allocator *allocator) {
    if ((config->stage_bit_granularity  & (config->stage_bit_granularity  - 1)) != 0) {
        println("Misaligned Bit Granularity (Stage):");
        println("    config->stage_bit_granularity = %u - this is not a power of 2");

        assert(false && "Bit Granularity Must Be A Power Of 2");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }
    if ((config->upload_bit_granularity & (config->upload_bit_granularity - 1)) != 0) {
        println("Misaligned Bit Granularity (Upload):");
        println("    config->upload_bit_granularity = %u - this is not a power of 2");

        assert(false && "Bit Granularity Must Be A Power Of 2");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }

    if ((config->stage_cap & (config->stage_bit_granularity  - 1)) != 0) {
        println("Misaligned Bit Granularity (Stage):");
        println("    config->stage_bit_granularity = %u, config->stage_cap = %u - cap must be a multiple of bit granularity", config->stage_bit_granularity, config->stage_cap);

        assert(false && "Bit Granularity Must Be A Power Of 2");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }
    if ((config->upload_cap & (config->upload_bit_granularity - 1)) != 0) {
        println("Misaligned Bit Granularity (Upload):");
        println("    config->upload_bit_granularity = %u, config->upload_cap = %u - cap must be a multiple of bit granularity", config->upload_bit_granularity, config->upload_cap);

        assert(false && "Bit Granularity Must Be A Power Of 2");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }

    Gpu *gpu = get_gpu_instance();

    // Have to do mod here, as this optimal alignment can be 1
    if (config->stage_bit_granularity % gpu->info.props.limits.optimalBufferCopyOffsetAlignment != 0) {
        println("Misaligned Bit Granularity (Stage):");
        println("    config->stage_bit_granularity  = %u, optimal buffer copy alignment = %u - bit granularity must be aligned to optimal buffer copy offset alignment", config->stage_bit_granularity, gpu->info.props.limits.optimalBufferCopyOffsetAlignment);

        assert(false && "Bit granularity must be aligned to the optimal buffer copy offset alignment");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }
    if (config->upload_bit_granularity % gpu->info.props.limits.optimalBufferCopyOffsetAlignment != 0) {
        println("Misaligned Bit Granularity (Upload):");
        println("    config->upload_bit_granularity = %u, optimal buffer copy alignment = %u - bit granularity must be aligned to optimal buffer copy offset alignment", config->upload_bit_granularity, gpu->info.props.limits.optimalBufferCopyOffsetAlignment);

        assert(false && "Bit granularity must be aligned to the optimal buffer copy offset alignment");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }

    Gpu_Allocator ret = {};
    ret.allocation_cap         = config->allocation_cap;
    ret.to_stage_cap           = config->to_stage_cap;
    ret.to_upload_cap          = config->to_upload_cap;
    ret.staging_queue_byte_cap = config->staging_queue_byte_cap;
    ret.upload_queue_byte_cap  = config->upload_queue_byte_cap;
    ret.allocation_cap         = config->allocation_cap;
    ret.stage_cap              = config->stage_cap;
    ret.upload_cap             = config->upload_cap;
    ret.stage_bit_granularity  = config->stage_bit_granularity;
    ret.upload_bit_granularity = config->upload_bit_granularity;
    ret.stage_ptr              = config->stage_ptr;
    ret.stage                  = config->stage;
    ret.upload                 = config->upload;

    // Its so fucking funny that this is how EVERY std::string exists lol
    char *string = (char*)malloc_h(config->disk_storage.len + 1, 1);
    memcpy(string, config->disk_storage.str, config->disk_storage.len + 1);
    ret.disk_storage = {.len = config->disk_storage.len, .str = string};

    u32 allocation_cap = ret.allocation_cap;
    ret.allocations        =             (Gpu_Allocation*) malloc_h(sizeof(Gpu_Allocation)             * allocation_cap, 16);
    ret.allocation_states  = (Gpu_Allocation_State_Flags*) malloc_h(sizeof(Gpu_Allocation_State_Flags) * allocation_cap, 16);
    ret.allocation_indices =                        (u32*) malloc_h(sizeof(u32)                        * allocation_cap, 16);
    ret.allocation_weights =                         (u8*) malloc_h(sizeof(u8)                         * allocation_cap, 16);

    memset(ret.allocation_states,   0, sizeof(u8) * allocation_cap);
    memset(ret.allocation_weights,  0, sizeof(u8) * allocation_cap);

    ret.stage_mask_count  = config->stage_cap  / (64 * ret.stage_bit_granularity);
    ret.upload_mask_count = config->upload_cap / (64 * ret.upload_bit_granularity);

    ret.stage_masks       = (u64*)malloc_h(sizeof(u64) * ret.stage_mask_count,  16);
    ret.upload_masks      = (u64*)malloc_h(sizeof(u64) * ret.upload_mask_count, 16);

    memset(ret.stage_masks,  0, sizeof(u64) * ret.stage_mask_count);
    memset(ret.upload_masks, 0, sizeof(u64) * ret.upload_mask_count);

    VkDevice device = gpu->device;

    VkCommandPoolCreateInfo cmd_pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // buffers will be recorded right before drawing
    cmd_pool_info.queueFamilyIndex = gpu->graphics_queue_index;

    u32 frame_index = g_frame_index;

    auto check = vkCreateCommandPool(device, &cmd_pool_info, ALLOCATION_CALLBACKS, &ret.graphics_cmd_pools[frame_index]);
    DEBUG_OBJ_CREATION(vkCreateCommandPool, check);

    if (gpu->transfer_queue_index != gpu->graphics_queue_index) {
        cmd_pool_info.queueFamilyIndex = gpu->transfer_queue_index;

        auto check = vkCreateCommandPool(device, &cmd_pool_info, ALLOCATION_CALLBACKS, &ret.transfer_cmd_pools[frame_index]);
        DEBUG_OBJ_CREATION(vkCreateCommandPool, check);
    }

    // Indicate that queues are safe to use
    ret.staging_queue_byte_count = Max_u64;
    ret.to_stage_count           = Max_u32;
    ret.upload_queue_byte_count  = Max_u64;
    ret.to_upload_count          = Max_u32;

    // Fill return value
    *allocator = ret;

    #if ALLOCATOR_MEMORY_FOOTPRINT
    u64 total_memory_footprint = 0;
    total_memory_footprint += align(sizeof(u64)                    * ret.stage_mask_count,  16);
    total_memory_footprint += align(sizeof(u64)                    * ret.upload_mask_count, 16);
    total_memory_footprint += align(sizeof(Allocation)             * ret.allocation_cap,    16);
    total_memory_footprint += align(sizeof(Allocation_State_Flags) * ret.allocation_cap,    16);
    total_memory_footprint += align(sizeof(u32)                    * ret.allocation_cap,    16);
    total_memory_footprint += align(sizeof(u32)                    * ret.allocation_cap,    16);

    println("Allocator Memory Footprint (file name: %s):", ret.disk_storage.str);
    println("        %u stage_masks: %u",ret.stage_mask_count , align(sizeof(u64)                    * ret.stage_mask_count,  16));
    println("       %u upload_masks: %u",ret.upload_mask_count, align(sizeof(u64)                    * ret.upload_mask_count, 16));
    println("        %u allocations: %u",ret.allocation_cap   , align(sizeof(Allocation)             * ret.allocation_cap,    16));
    println("  %u allocation_states: %u",ret.allocation_cap   , align(sizeof(Allocation_State_Flags) * ret.allocation_cap,    16));
    println(" %u allocation_indices: %u",ret.allocation_cap   , align(sizeof(u32)                    * ret.allocation_cap,    16));
    println(" %u allocation_weights: %u",ret.allocation_cap   , align(sizeof(u8)                     * ret.allocation_cap,    16));
    println(" total footprint = %u", total_memory_footprint);
    #endif

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}
void destroy_allocator(Gpu_Allocator *alloc) {
    Gpu *gpu        = get_gpu_instance();
    VkDevice device = gpu->device;

    vkDestroyCommandPool(device, alloc->graphics_cmd_pools[g_frame_index], ALLOCATION_CALLBACKS);
    if (gpu->transfer_queue_index != gpu->graphics_queue_index)
        vkDestroyCommandPool(device, alloc->transfer_cmd_pools[g_frame_index], ALLOCATION_CALLBACKS);

    free_h((void*)alloc->disk_storage.str);
    free_h(alloc->allocations);
    free_h(alloc->allocation_states);
    free_h(alloc->allocation_indices);
    free_h(alloc->allocation_weights);
    free_h(alloc->stage_masks);
    free_h(alloc->upload_masks);

    *alloc = {};
}

Gpu_Allocator_Result create_tex_allocator(Gpu_Tex_Allocator_Config *config, Gpu_Tex_Allocator *allocator) {
    if ((config->stage_bit_granularity  & (config->stage_bit_granularity  - 1)) != 0) {
        println("Misaligned Bit Granularity (Stage):");
        println("    config->stage_bit_granularity = %u - this is not a power of 2");

        assert(false && "Bit Granularity Must Be A Power Of 2");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }
    if ((config->upload_bit_granularity & (config->upload_bit_granularity - 1)) != 0) {
        println("Misaligned Bit Granularity (Upload):");
        println("    config->upload_bit_granularity = %u - this is not a power of 2");

        assert(false && "Bit Granularity Must Be A Power Of 2");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }

    if ((config->stage_cap & (config->stage_bit_granularity  - 1)) != 0) {
        println("Misaligned Bit Granularity (Stage):");
        println("    config->stage_bit_granularity = %u, config->stage_cap = %u - cap must be a multiple of bit granularity", config->stage_bit_granularity, config->stage_cap);

        assert(false && "Bit Granularity Must Be A Power Of 2");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }
    if ((config->upload_cap & (config->upload_bit_granularity - 1)) != 0) {
        println("Misaligned Bit Granularity (Upload):");
        println("    config->upload_bit_granularity = %u, config->upload_cap = %u - cap must be a multiple of bit granularity", config->upload_bit_granularity, config->upload_cap);

        assert(false && "Bit Granularity Must Be A Power Of 2");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }

    Gpu *gpu = get_gpu_instance();

    // Have to do mod here, as this optimal alignment can be 1
    if (config->stage_bit_granularity % gpu->info.props.limits.optimalBufferCopyOffsetAlignment != 0) {
        println("Misaligned Bit Granularity (Stage):");
        println("    config->stage_bit_granularity  = %u, optimal buffer copy alignment = %u - bit granularity must be aligned to optimal buffer copy offset alignment", config->stage_bit_granularity, gpu->info.props.limits.optimalBufferCopyOffsetAlignment);

        assert(false && "Bit granularity must be aligned to the optimal buffer copy offset alignment");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }
    if (config->upload_bit_granularity % gpu->info.props.limits.optimalBufferCopyOffsetAlignment != 0) {
        println("Misaligned Bit Granularity (Upload):");
        println("    config->upload_bit_granularity = %u, optimal buffer copy alignment = %u - bit granularity must be aligned to optimal buffer copy offset alignment", config->upload_bit_granularity, gpu->info.props.limits.optimalBufferCopyOffsetAlignment);

        assert(false && "Bit granularity must be aligned to the optimal buffer copy offset alignment");
        return GPU_ALLOCATOR_RESULT_MISALIGNED_BIT_GRANULARITY;
    }

    Gpu_Tex_Allocator ret = {};
    ret.allocation_cap         = config->allocation_cap;
    ret.to_stage_cap           = config->to_stage_cap;
    ret.to_upload_cap          = config->to_upload_cap;
    ret.staging_queue_byte_cap = config->staging_queue_byte_cap;
    ret.upload_queue_byte_cap  = config->upload_queue_byte_cap;
    ret.allocation_cap         = config->allocation_cap;
    ret.stage_cap              = config->stage_cap;
    ret.upload_cap             = config->upload_cap;
    ret.stage_bit_granularity  = config->stage_bit_granularity;
    ret.upload_bit_granularity = config->upload_bit_granularity;
    ret.string_buffer          = create_string_buffer(config->string_buffer_size);
    ret.stage_ptr              = config->stage_ptr;
    ret.stage                  = config->stage;
    ret.upload                 = config->upload;

    ret.allocations        =             (Gpu_Tex_Allocation*)malloc_h(sizeof(Gpu_Allocation)             * ret.allocation_cap, 16);
    ret.allocation_states  =     (Gpu_Allocation_State_Flags*)malloc_h(sizeof(Gpu_Allocation_State_Flags) * ret.allocation_cap, 16);
    ret.allocation_indices =                            (u32*)malloc_h(sizeof(u32)                        * ret.allocation_cap, 16);
    ret.allocation_weights =                             (u8*)malloc_h(sizeof(u8)                         * ret.allocation_cap, 16);
    ret.hashes             =                            (u64*)malloc_h(sizeof(u64)                        * ret.allocation_cap, 16);

    memset(ret.allocation_states,   0, sizeof(u8)  * ret.allocation_cap);
    memset(ret.allocation_weights,  0, sizeof(u8)  * ret.allocation_cap);
    memset(ret.hashes,              0, sizeof(u64) * ret.allocation_cap);

    ret.stage_mask_count  = config->stage_cap  / (64 * ret.stage_bit_granularity);
    ret.upload_mask_count = config->upload_cap / (64 * ret.upload_bit_granularity);

    ret.stage_masks       = (u64*)malloc_h(sizeof(u64) * ret.stage_mask_count,  16);
    ret.upload_masks      = (u64*)malloc_h(sizeof(u64) * ret.upload_mask_count, 16);

    memset(ret.stage_masks,  0, sizeof(u64) * ret.stage_mask_count);
    memset(ret.upload_masks, 0, sizeof(u64) * ret.upload_mask_count);

    VkDevice device = gpu->device;

    VkCommandPoolCreateInfo cmd_pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; // buffers will be recorded right before drawing
    cmd_pool_info.queueFamilyIndex = gpu->graphics_queue_index;

    u32 frame_index = g_frame_index;

    auto check =
        vkCreateCommandPool(device, &cmd_pool_info, ALLOCATION_CALLBACKS, &ret.graphics_cmd_pools[frame_index]);
    DEBUG_OBJ_CREATION(vkCreateCommandPool, check);

    if (gpu->transfer_queue_index != gpu->graphics_queue_index) {
        cmd_pool_info.queueFamilyIndex = gpu->transfer_queue_index;

        auto check =
            vkCreateCommandPool(device, &cmd_pool_info, ALLOCATION_CALLBACKS, &ret.transfer_cmd_pools[frame_index]);
        DEBUG_OBJ_CREATION(vkCreateCommandPool, check);
    }

    // Indicate that queues are safe to use
    ret.staging_queue_byte_count = Max_u64;
    ret.to_stage_count           = Max_u32;
    ret.upload_queue_byte_count  = Max_u64;
    ret.to_upload_count          = Max_u32;

    // Fill return value
    *allocator = ret;

    #if ALLOCATOR_MEMORY_FOOTPRINT
    u64 total_memory_footprint = 0;
    total_memory_footprint += align(sizeof(u64)                    * ret.stage_mask_count,  16);
    total_memory_footprint += align(sizeof(u64)                    * ret.upload_mask_count, 16);
    total_memory_footprint += align(sizeof(Tex_Allocation)         * ret.allocation_cap,    16);
    total_memory_footprint += align(sizeof(Allocation_State_Flags) * ret.allocation_cap,    16);
    total_memory_footprint += align(sizeof(u32)                    * ret.allocation_cap,    16);
    total_memory_footprint += align(sizeof(u32)                    * ret.allocation_cap,    16);
    total_memory_footprint += align(sizeof(u64)                    * ret.allocation_cap,    16);

    println("Tex_Allocator Memory Footprint:");
    println("        %u stage_masks: %u",ret.stage_mask_count , align(sizeof(u64)                    * ret.stage_mask_count,  16));
    println("       %u upload_masks: %u",ret.upload_mask_count, align(sizeof(u64)                    * ret.upload_mask_count, 16));
    println("        %u allocations: %u",ret.allocation_cap   , align(sizeof(Allocation)             * ret.allocation_cap,    16));
    println("  %u allocation_states: %u",ret.allocation_cap   , align(sizeof(Allocation_State_Flags) * ret.allocation_cap,    16));
    println(" %u allocation_indices: %u",ret.allocation_cap   , align(sizeof(u32)                    * ret.allocation_cap,    16));
    println(" %u allocation_weights: %u",ret.allocation_cap   , align(sizeof(u8)                     * ret.allocation_cap,    16));
    println("             %u hashes: %u",ret.allocation_cap   , align(sizeof(u64)                    * ret.allocation_cap,    16));
    println("    total footprint = %u", total_memory_footprint);
    #endif

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}
void destroy_tex_allocator(Gpu_Tex_Allocator *alloc) {
    Gpu *gpu        = get_gpu_instance();
    VkDevice device = gpu->device;

    for(u32 i = 0; i < alloc->allocation_count; ++i)
        vkDestroyImage(device, alloc->allocations[i].image, ALLOCATION_CALLBACKS);

    vkDestroyCommandPool(device, alloc->graphics_cmd_pools[0], ALLOCATION_CALLBACKS);
    vkDestroyCommandPool(device, alloc->graphics_cmd_pools[1], ALLOCATION_CALLBACKS);

    if (gpu->transfer_queue_index != gpu->graphics_queue_index) {
        vkDestroyCommandPool(device, alloc->transfer_cmd_pools[0], ALLOCATION_CALLBACKS);
        vkDestroyCommandPool(device, alloc->transfer_cmd_pools[1], ALLOCATION_CALLBACKS);
    }

    destroy_string_buffer(&alloc->string_buffer);

    free_h(alloc->allocations);
    free_h(alloc->allocation_states);
    free_h(alloc->allocation_indices);
    free_h(alloc->allocation_weights);
    free_h(alloc->hashes);
    free_h(alloc->stage_masks);
    free_h(alloc->upload_masks);

    *alloc = {};
}

//
//  ** Model Allocation implementation details (grep marker: ~MAID) **
//                  @Todo Explain the implementation.
//
// @Todo Setting the flags to 'TO_STAGE' on allocations that are automatically added to the staging queue
// by their weight and their lack of being staged should be done with simd find and update flags: that loop
// should only check sizes to find the last allocation to add to the queue, then we can run to that index
// with the find and update.

// begin/continue/submit_allocation(..) explanation:
//
// The way that data is described in a gltf file makes it easier to incrementally
// add an allocation to an allocator, as then an allocation can easily be built
// up from the numerous accessors and buffer views described in the gltf buffer.
Gpu_Allocator_Result begin_allocation(Gpu_Allocator *alloc) {
    assert(alloc->staging_queue_byte_count == Max_u64);

    // Byte count set to max on queue submission, indicating queue is available again
    if (alloc->staging_queue_byte_count != Max_u64)
        return GPU_ALLOCATOR_RESULT_QUEUE_IN_USE;

    // '.allocations' is not a dynamic array.
    if (alloc->allocation_count >= alloc->allocation_cap)
        return GPU_ALLOCATOR_RESULT_ALLOCATOR_FULL;

    // Begin the new allocation. The allocation count is only incremented once submit has been
    // called.  Therefore, if this allocation is cancelled, (which really it never should be) the
    // left over data will just get overwritten next time.
    Gpu_Allocation *p_allocation = &alloc->allocations[alloc->allocation_count];
    *p_allocation = {};

    // File is closed the first time staging_queue_begin() is called
    if (!alloc->disk)
        alloc->disk = fopen(alloc->disk_storage.str, "wb");

    // staging queue must not be used during the allocation phase of the program. So it is safe to
    // reuse it here for tracking in-progress allocations.
    alloc->staging_queue_byte_count = 0;
    alloc->to_stage_count           = 0;

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result continue_allocation(Gpu_Allocator *alloc, u64 size, void *ptr) {

    alloc->allocations[alloc->allocation_count].size += size;

    if (align(alloc->allocations[alloc->allocation_count].size, alloc->stage_bit_granularity) > alloc->staging_queue_byte_cap) {
        println("Allocation Size (Aligned to Stage Bit Granularity) Would Overflow The Staging Queue: allocation_size = %u, staging_queue_byte_cap = %u", alloc->allocations[alloc->allocation_count].size, alloc->staging_queue_byte_cap);
        return GPU_ALLOCATOR_RESULT_ALLOCATION_TOO_LARGE;
    }
    if (align(alloc->allocations[alloc->allocation_count].size, alloc->upload_bit_granularity) > alloc->upload_queue_byte_cap) {
        println("Allocation Size (Aligned to Upload Bit Granularity) Would Overflow The Upload Queue: allocation_size = %u, upload_queue_byte_cap = %u", alloc->allocations[alloc->allocation_count].size, alloc->upload_queue_byte_cap);
        return GPU_ALLOCATOR_RESULT_ALLOCATION_TOO_LARGE;
    }

    // Use the staging queue as temp storage while adding allocations to the allocator
    memcpy((u8*)alloc->stage_ptr + alloc->staging_queue_byte_count, ptr, size);
    alloc->staging_queue_byte_count += size;

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result submit_allocation(Gpu_Allocator *alloc, u32 *key) {
    u32  allocation_count              = alloc->allocation_count;
    Gpu_Allocation *allocations        = alloc->allocations;
    Gpu_Allocation_State_Flags *states = alloc->allocation_states;

    // Write total allocation to disk for faster reloading.
    fwrite(alloc->stage_ptr, 1, allocations[allocation_count].size, alloc->disk);

    allocations[allocation_count].disk_offset  = alloc->disk_size;
    alloc->disk_size                          += allocations[allocation_count].size;

    states[allocation_count] = 0x0;

    // '*key' is an index which corresponds to this allocation's index in the
    // '.indices' field of the allocator. As the allocator is used, allocations
    // will be shuffled around; this key is maintained to point to the correct
    // allocation. (Read the implementation details above for details - grep for '~MAID'.)
    *key = allocation_count;
    alloc->allocation_indices[allocation_count] = allocation_count;
    alloc->allocation_count++;

    // Max_u64 indicates that the queue is safe to use again. This is used by 'begin_allocation(..)'.
    alloc->staging_queue_byte_count = Max_u64;
    alloc->to_stage_count           = Max_u32;

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result staging_queue_begin(Gpu_Allocator *alloc) {
    if (alloc->disk) { // Close the file: using the queue indicates allocation adding phase is complete.
        fclose(alloc->disk);
        alloc->disk = NULL;
    }

    // '.to_stage_count' set to max by queue submission, indicating it
    // is safe to use again.
    if (alloc->to_stage_count != Max_u32)
        return GPU_ALLOCATOR_RESULT_QUEUE_IN_USE;

    alloc->to_stage_count = 0;
    alloc->staging_queue_byte_count = 0;
    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result staging_queue_add(Gpu_Allocator *alloc, u32 key, bool adjust_weights) {
    u8 inc = 10 & ~(Max_u8 + (u8)(adjust_weights));
    u8 dec =  1 & ~(Max_u8 + (u8)(adjust_weights));

    Weight_Args w_args = {
        .allocations = alloc->allocations,
        .weights     = alloc->allocation_weights,
        .states      = alloc->allocation_states,
        .indices     = alloc->allocation_indices,
        .count       = alloc->allocation_count,
        .idx = key,
        .inc = inc,
        .dec = dec, // @Test Find effective inc and dec values
    };
    Gpu_Allocation *allocations        = alloc->allocations;
    Gpu_Allocation_State_Flags *states = alloc->allocation_states;

    u32 idx = alloc->allocation_indices[key];

    // If the allocation is already staged or marked to be staged, early return.
    if (states[idx] & ALLOCATION_STATE_STAGED_BIT || states[idx] & ALLOCATION_STATE_TO_STAGE_BIT) {

        // Indicate that the allocation has been called up.
        states[idx] |= ALLOCATION_STATE_TO_DRAW_BIT;

        adjust_allocation_weights(&w_args);
        return GPU_ALLOCATOR_RESULT_SUCCESS;
    }

    if (alloc->to_stage_count >= alloc->to_stage_cap)
        return GPU_ALLOCATOR_RESULT_QUEUE_FULL;

    // Allocations' offsets must be aligned to their representation in the allocator bit masks.
    // See the implementation info for details (grep '~MAID').
    u64 bit_aligned_size = align(allocations[idx].size, alloc->stage_bit_granularity);

    if (bit_aligned_size + alloc->staging_queue_byte_count > alloc->staging_queue_byte_cap) {
        // If the queue add fails, we do not want stuff marked as to draw that is not also part of a queue.
        return GPU_ALLOCATOR_RESULT_QUEUE_FULL;
    }

    states[idx] |= ALLOCATION_STATE_TO_STAGE_BIT | ALLOCATION_STATE_TO_DRAW_BIT;

    alloc->staging_queue_byte_count += bit_aligned_size;
    alloc->to_stage_count++;

    // 'idx' becomes invalid after this call
    adjust_allocation_weights(&w_args);

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result staging_queue_submit(Gpu_Allocator *alloc) {
    // If the 'to_stage' count is zero on queue submission, just assume that everything queued was
    // already cached, and we need not do anything. This is most likely, as vertex data should just
    // be able to live in memory (I am pretty sure). However, if vertex data is ever too large for
    // the staging buffer, allocations can be evicted and reloaded from the allocator's disk storage
    // (see implementation above - grep '~MAID').
    if (alloc->to_stage_count == 0) {
        // println("All Data Cached On Queue Submission");
        alloc->to_stage_count = Max_u32;
        return GPU_ALLOCATOR_RESULT_SUCCESS;
    }

    Gpu_Allocation_State_Flags *states      = alloc->allocation_states;
    Gpu_Allocation             *allocations = alloc->allocations;

    u32 allocation_count = alloc->allocation_count;
    u64 queue_size       = alloc->staging_queue_byte_count;

    u32 mask_count = alloc->stage_mask_count;
    u64 *masks     = alloc->stage_masks;

    u32 g        = alloc->stage_bit_granularity;
    u32 req_bits = alloc->staging_queue_byte_count / g; // This size is aligned to g (being the sum of aligned sizes), so need to worry about remainder

    u32 indices_count;
    u32 *indices = (u32*)malloc_t(sizeof(u32) * allocation_count, 16); // Align 16 for SIMD

    // @Note Although I would like to, this section cannot really be moved into its own function
    // cleanly, as the internal logic has to be so slightly different each time (such as which size
    // to use, or how to calculate the size). So it is easier to just inline it and not fuss...
    //
    // Section Explanation: If no contiguous block of free memory sufficient to hold the size of the
    // stage queue is available in the stage buffer (as represented by the bit masks), find the
    // allocations flagged as staged, but which are not flagged for staging, uploading or drawing;
    // loop these allocations, starting at the allocation with the lowest weight; mark the
    // allocation's range as free in the bit mask, check if there is now a large enough size, and if
    // so, break; if we have otherwise looped all allocations and there is no room, restore the bit
    // masks (as nothing will actually be overwritten) and return error code. (See implementation
    // details if unsure - grep '~MAID'.)

    u32 free_block = find_contiguous_free(mask_count, masks, req_bits);
    if (free_block == Max_u32) {
        // In case of failure, we need to restore the masks initial states. (Failure should be
        // incredibly unlikely, if it ever happens at all. The code using the allocators should use
        // them efficiently.)
        u64 *mask_copies = (u64*)malloc_t(sizeof(u64) * mask_count, 16);
        memcpy(mask_copies, masks, sizeof(u64) * mask_count);

        u32 bit_size;
        u32 bit_offset;
        indices_count = simd_find_flags_u8(allocation_count, states, ALLOCATION_STATE_STAGED_BIT, ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_TO_DRAW_BIT | ALLOCATION_STATE_TO_STAGE_BIT, indices);
        for(u32 i = indices_count - 1; i != Max_u32; --i) {
            u32 idx = indices[i];

            // @Todo Really g should always be power of 2, so these should be a bit shifts, not divides.
            bit_size   = align(allocations[idx].size, g) / g;
            bit_offset = allocations[idx].stage_offset / g;

            // Clear the allocation's range in the bit masks, and check if there is now a large
            // enough free block.
            make_free(mask_count, masks, bit_offset, bit_size);
            free_block = find_contiguous_free(mask_count, masks, req_bits);

            if (free_block != Max_u32) {
                // Only mark allocations as having been evicted from staging buffer if they are
                // actually going to be evicted (i.e. only if a sufficient free block is actually
                // available).
                //
                // @Todo This should be implemented as simd_update_flags_u8(..) but with the ability
                // to start from an offset. Doing this as a loop over individual u8s is very very
                // lame.
                for(u32 j = i; j < indices_count; ++j)
                    states[indices[j]] &= ~ALLOCATION_STATE_STAGED_BIT;

                goto free_block_found; // jump over early return
            }
        }
        // Restore the masks to before allocations were marked as free.
        memcpy(masks, mask_copies, sizeof(u64) * mask_count);
        return GPU_ALLOCATOR_RESULT_STAGE_FULL; // Too many allocations waiting to draw
    }

    free_block_found: // goto

    // Find how large free_block is.
    u64 block_size = get_block_size(mask_count, masks, free_block) * g;

    // Find all allocations which are not flagged as ALLOCATION_STATE_STAGED_BIT or ALLOCATION_STATE_TO_STAGE_BIT.
    indices_count = simd_find_flags_u8(allocation_count, states, 0x00, ALLOCATION_STATE_STAGED_BIT | ALLOCATION_STATE_TO_STAGE_BIT, indices);

    // Loop the above allocations, starting at the highest weight (lowest index). If there is size
    // available in the free block for the queued allocations and the current allocation in loop,
    // add the allocation to the list of allocations that we want to stage.
    u64 size = 0;
    u32 idx;
    for(u32 i = 0; i < indices_count; ++i) {
        idx = indices[i];
        size += align(allocations[idx].size, g);

        // @Test This static predicts that there will be room for at least one allocation, it might be
        // best to switch it around.
        if (size > block_size - queue_size)
            break;
        else
            states[idx] |= ALLOCATION_STATE_TO_STAGE_BIT;
    }
    // Get the final list of allocations to stage (comprised of those called up by 'queue_add()', and those
    // grabbed from the above loop').
    indices_count = simd_find_flags_u8(allocation_count, states, ALLOCATION_STATE_TO_STAGE_BIT, 0x00, indices);

    // Loop vars
    u64   stage_offset = free_block * g;
    FILE *disk       = fopen(alloc->disk_storage.str, "rb");
    void *stage_ptr  = alloc->stage_ptr;

    // Read the data from the allocator's buffer file into the staging buffer.
    for(u32 i = 0; i < indices_count; ++i) {
        idx = indices[i];

        fseek(disk, allocations[idx].disk_offset, 0);
        fread((u8*)stage_ptr + stage_offset, 1, allocations[idx].size, disk);

        allocations[idx].stage_offset = stage_offset;
        stage_offset += align(allocations[idx].size, g);

        assert(stage_offset + allocations[idx].size <= alloc->stage_cap && "Allocator Stage Overflow");
    }
    fclose(disk);

    // Mark all TO_STAGE allocations as STAGED, and clear TO_STAGE bit.
    simd_update_flags_u8(allocation_count, states, ALLOCATION_STATE_TO_STAGE_BIT, 0x0, ALLOCATION_STATE_STAGED_BIT, ALLOCATION_STATE_TO_STAGE_BIT);

    // Fill the bit masks where the data was copied to.
    make_full(alloc->stage_mask_count, alloc->stage_masks, free_block, stage_offset / g);

    alloc->to_stage_count = Max_u32; // Indicate that it is safe to begin a new queue.
    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result upload_queue_begin(Gpu_Allocator *alloc) {
    // .to_upload_count is set to max upon successful queue submission,
    // indicating that the queue is safe to use again.
    if (alloc->to_upload_count != Max_u32)
        return GPU_ALLOCATOR_RESULT_QUEUE_IN_USE;

    alloc->to_upload_count = 0;
    alloc->upload_queue_byte_count = 0;

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result upload_queue_add(Gpu_Allocator *alloc, u32 key, bool adjust_weights) {
    u8 inc = 10 & ~(Max_u32 + (u32)(adjust_weights));
    u8 dec =  1 & ~(Max_u32 + (u32)(adjust_weights));

    Weight_Args w_args = {
        .allocations = alloc->allocations,
        .weights     = alloc->allocation_weights,
        .states      = alloc->allocation_states,
        .indices     = alloc->allocation_indices,
        .count       = alloc->allocation_count,
        .idx = key,
        .inc = inc,
        .dec = dec, // @Test Find effective inc and dec values
    };

    u32 idx = alloc->allocation_indices[key];

    // If the allocation is already uploaded or marked to be uploaded, early return.
    if (alloc->allocation_states[idx] & ALLOCATION_STATE_UPLOADED_BIT ||
        alloc->allocation_states[idx] & ALLOCATION_STATE_TO_UPLOAD_BIT)
    {
        // Indicate that the allocation has been called up.
        alloc->allocation_states[idx] |= ALLOCATION_STATE_TO_DRAW_BIT;

        adjust_allocation_weights(&w_args);
        return GPU_ALLOCATOR_RESULT_SUCCESS;
    }

    if (alloc->to_upload_count >= alloc->to_upload_cap)
        return GPU_ALLOCATOR_RESULT_QUEUE_FULL;

    // Allocations' offsets must be aligned to their representation in the allocator bit masks.
    // See the implementation info for details (grep '~MAID').
    u64 bit_align_size = align(alloc->allocations[idx].size, alloc->upload_bit_granularity);
    if (bit_align_size + alloc->upload_queue_byte_count > alloc->upload_queue_byte_cap) {
        // If the queue add fails, we do not want stuff marked as to draw that is not also part of a
        // queue.
        return GPU_ALLOCATOR_RESULT_QUEUE_FULL;
    }

    alloc->allocation_states[idx]  |= ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_TO_DRAW_BIT;
    alloc->upload_queue_byte_count += bit_align_size;
    alloc->to_upload_count++;

    // 'idx' becomes invalid after this call.
    adjust_allocation_weights(&w_args);

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result upload_queue_submit(Gpu_Allocator *alloc) {
    // If the to upload count is zero on queue submission, just assume that everything queued was
    // already cached, and we need not do anything.
    if (alloc->to_upload_count == 0) {
        alloc->to_upload_count = Max_u32;
        return GPU_ALLOCATOR_RESULT_SUCCESS;
    }

    Gpu_Allocation_State_Flags *states      = alloc->allocation_states;
    Gpu_Allocation             *allocations = alloc->allocations;

    u32 allocation_count  = alloc->allocation_count;
    u64 queue_size        = alloc->upload_queue_byte_count;

    u32 mask_count = alloc->upload_mask_count;
    u64 *masks     = alloc->upload_masks;

    u32 g          = alloc->upload_bit_granularity;
    u32 req_bits   = alloc->upload_queue_byte_count / g; // This size is aligned to g (being the sum of aligned sizes), so need to worry about remainder

    u32 idx;
    u32 indices_count;
    u32 *indices   = (u32*)malloc_t(sizeof(u32) * alloc->allocation_count, 16); // Align 16 for SIMD

    // @Note Although I would like to, this section cannot really be moved into its own function
    // cleanly, as the internal logic has to be so slightly different each time (such as which size
    // to use, or how to calculate the size). So it is easier to just inline it and not fuss...
    //
    // Section Explanation: If no contiguous block of free memory sufficient to hold the size of the
    // upload queue is available in the upload buffer (as represented by the bit masks), find the
    // allocations flagged as uploaded, but which are not flagged for uploading or drawing; loop
    // these allocations, starting at the allocation with the lowest weight (see implementation
    // details above for what 'weight' means); mark the allocation's range as free in the bit mask,
    // check if there is now a large enough size, and if so, break; if we have otherwise looped all
    // allocations and there is no room, so return error code.
    u32 free_block = find_contiguous_free(mask_count, masks, req_bits);
    if (free_block == Max_u32) {
        // In case of failure, we need to restore the masks initial states. (Failure should be
        // incredibly unlikely, if it ever happens at all. The code using the allocators should use
        // them efficiently.)
        u64 *mask_copies = (u64*)malloc_t(sizeof(u64) * mask_count, 16);
        memcpy(mask_copies, masks, sizeof(u64) * mask_count);

        u32 bit_size;
        u32 bit_offset;
        indices_count = simd_find_flags_u8(allocation_count, states, ALLOCATION_STATE_UPLOADED_BIT, ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_TO_DRAW_BIT, indices);
        for(u32 i = indices_count - 1; i != Max_u32; --i) {
            idx = indices[i];

            // @Note Really g should always be power of 2, so these should be bit shifts, not
            // divides. I really do not like these divides...
            //
            // Find the allocation's range (adjusted to the range in bits).
            bit_size   = align(allocations[idx].size, g) / g;
            bit_offset = allocations[idx].upload_offset / g;

            // Clear the allocation's range in the bit masks, and check if there is now a large
            // enough free block.
            make_free(mask_count, masks, bit_offset, bit_size);
            free_block = find_contiguous_free(mask_count, masks, req_bits);

            if (free_block != Max_u32) {
                // Only mark allocations as having been evicted from upload buffer if they are
                // actually going to be evicted (i.e. only if a sufficient free block is actually
                // available).
                //
                // @Todo This should be implemented as simd_update_flags_u8(..) but with the ability
                // to start from an offset. Doing this as a loop over individual u8s is very very
                // lame.
                for(u32 j = i; j < indices_count; ++j)
                    states[indices[j]] &= ~ALLOCATION_STATE_UPLOADED_BIT;

                goto free_block_found; // jump over early return
            }
        }
        // Restore the masks to before allocations were marked as free.
        memcpy(masks, mask_copies, sizeof(u64) * mask_count);
        return GPU_ALLOCATOR_RESULT_UPLOAD_FULL; // Too many allocations waiting to draw
    }

    free_block_found: // goto label

    // Find how large free_block is.
    u64 block_size = get_block_size(mask_count, masks, free_block) * g;

    // Find the indices of all allocations which are not uploaded and not marked as to upload.
    indices_count = simd_find_flags_u8(allocation_count, states, 0x00, ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_UPLOADED_BIT, indices);

    // Loop the above allocations, starting at the highest weight. If there is size available in
    // the free block for the queued allocations and the current allocation in loop, add the
    // allocation to the list of allocations that we want to upload.
    u64 size = 0;
    for(u32 i = 0; i < indices_count; ++i) {
        idx = indices[i];

        // Memory offsets must be aligned to match their bit representation.
        size += align(allocations[idx].size, g);

        // @Test This static predicts that there will be room for at least one allocation; it might be
        // best to switch it around.
        if (size > block_size - queue_size)
            break;
        else
            states[idx] |= ALLOCATION_STATE_TO_UPLOAD_BIT;
    }
    // Get the final list of allocations to upload.
    indices_count = simd_find_flags_u8(allocation_count, states, ALLOCATION_STATE_TO_UPLOAD_BIT, 0x00, indices);

    // Create the upload regions information
    u64 upload_offset      = free_block * g;
    VkBufferCopy2 *regions = (VkBufferCopy2*)malloc_t(sizeof(VkBufferCopy2) * indices_count, 8);

    Gpu_Allocation *p_allocation;
    for(u32 i = 0; i < indices_count; ++i) {
        p_allocation = &alloc->allocations[indices[i]];
        p_allocation->upload_offset = upload_offset;

        regions[i]           = {VK_STRUCTURE_TYPE_BUFFER_COPY_2};
        regions[i].srcOffset = p_allocation->stage_offset;  // @Note I would like to break up the 'Allocation' struct even further, separating out stage
        regions[i].dstOffset = p_allocation->upload_offset; // and upload offsets. But this loop deters me. Specifically these two lines...
        regions[i].size      = p_allocation->size;

        upload_offset += align(p_allocation->size, g);
        assert(upload_offset <= alloc->upload_cap && "Gpu Allocator Upload Overflow");
    }

    // Fill the bit masks where the data was copied to.
    make_full(alloc->upload_mask_count, alloc->upload_masks, free_block, upload_offset / g);

    Gpu *gpu        = get_gpu_instance();
    VkDevice device = gpu->device;

    //
    // Record copy commands and the pipeline barriers into secondary command buffers stored in the
    // allocator.
    //

    // Copy info
    VkCopyBufferInfo2 copy_info = {VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2};
    copy_info.srcBuffer         = alloc->stage;
    copy_info.dstBuffer         = alloc->upload;
    copy_info.regionCount       = indices_count;
    copy_info.pRegions          = regions;

    u32 frame_index = g_frame_index;

    // Allocate graphics command buffers
    VkCommandBufferAllocateInfo cmd_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc_info.commandPool                 = alloc->graphics_cmd_pools[frame_index];
    cmd_alloc_info.level                       = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    cmd_alloc_info.commandBufferCount          = 1;

    auto check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->graphics_cmds[frame_index]);
    DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

    VkCommandBufferInheritanceInfo inheritance = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    VkCommandBufferBeginInfo cmd_begin_info    = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cmd_begin_info.flags                       = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    cmd_begin_info.pInheritanceInfo            = &inheritance;

    // Local copies
    VkCommandBuffer graphics_cmd = alloc->graphics_cmds[frame_index];
    u32 graphics_queue_index     = gpu->graphics_queue_index;
    u32 transfer_queue_index     = gpu->transfer_queue_index;

    if (graphics_queue_index == transfer_queue_index) { // static predict discrete transfer
        vkBeginCommandBuffer(graphics_cmd, &cmd_begin_info);

        //
        // Only a memory barrier is required without queue ownership transfer
        //

        // Copy memory barrier
        VkMemoryBarrier2 mem_barr = {VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mem_barr.srcStageMask     = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        mem_barr.srcAccessMask    = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        mem_barr.dstStageMask     = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR;
        mem_barr.dstAccessMask    = VK_ACCESS_2_MEMORY_READ_BIT_KHR;

        VkDependencyInfo dep   = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers    = &mem_barr;
        vkCmdPipelineBarrier2(graphics_cmd, &dep);

        vkEndCommandBuffer(graphics_cmd);
    } else {
        cmd_alloc_info.commandPool = alloc->transfer_cmd_pools[frame_index];

        // Allocate transfer command buffers
        check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->transfer_cmds[frame_index]);
        DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

        VkCommandBuffer transfer_cmd = alloc->transfer_cmds[frame_index];
        vkBeginCommandBuffer(transfer_cmd, &cmd_begin_info);

        vkCmdCopyBuffer2(transfer_cmd, &copy_info);

        //
        // Buffer barrier required for queue ownership transfer
        //

        // Transfer queue release buffer barrier
        VkBufferMemoryBarrier2 buf_barr = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
        buf_barr.srcStageMask           = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        buf_barr.srcAccessMask          = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        buf_barr.dstStageMask           = 0x0; // Ignore dst on release operation
        buf_barr.dstAccessMask          = 0x0;
        buf_barr.srcQueueFamilyIndex    = transfer_queue_index;
        buf_barr.dstQueueFamilyIndex    = graphics_queue_index;
        buf_barr.buffer                 = alloc->upload;
        buf_barr.offset                 = free_block * g;
        buf_barr.size                   = upload_offset; // This does point to the end of the last allocation

        VkDependencyInfo dep_info         = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep_info.bufferMemoryBarrierCount = 1;
        dep_info.pBufferMemoryBarriers    = &buf_barr;
        vkCmdPipelineBarrier2(transfer_cmd, &dep_info);

        vkEndCommandBuffer(transfer_cmd);

        vkBeginCommandBuffer(graphics_cmd, &cmd_begin_info);

        // Graphics queue acquire barrier
        buf_barr.srcStageMask  = 0x0; // Ignore src on acquire operation
        buf_barr.srcAccessMask = 0x0;
        buf_barr.dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT_KHR;
        buf_barr.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT_KHR;
        vkCmdPipelineBarrier2(graphics_cmd, &dep_info);

        vkEndCommandBuffer(graphics_cmd);
    }

    //
    // @Note This could be perceived as a little premature, as the commands have not actually been
    // submitted.  However, the commands having been recorded constitutes upload complete from the
    // ALLOCATOR's point of view: the draw commands will happen later in the same queue submission
    // section later down the pipeline, which the allocator is not a part of. Furthermore, from the
    // allocation's perspective, as they will be truly uploaded in the same point of the pipeline as
    // the draw commands (the upload cmds will even be in the same batch as the draw commands if
    // unified transfer), they cannot react to a ALLOCATION_STATE_TO_UPLOAD_BIT state. I cannot, call queue submit, wait
    // for the semaphore, then set ALLOCATION_STATE_UPLOADED_BIT state, then call draw, as the draws are already
    // submitted with the submission that the semaphore wait was signalling.
    //
    simd_update_flags_u8(allocation_count, states, ALLOCATION_STATE_TO_UPLOAD_BIT, 0x0, ALLOCATION_STATE_UPLOADED_BIT, ALLOCATION_STATE_TO_UPLOAD_BIT);

    alloc->to_upload_count = Max_u32;
    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result tex_add_texture(Gpu_Tex_Allocator *alloc, String *file_name, u32 *key) {
    // Check if the texture has already been seen. If so, early return.
    u64 hash = get_string_hash(file_name);

    for(u32 i = 0; i < alloc->allocation_count; ++i)
        if (hash == alloc->hashes[i]) {
            // If a texture is added to an allocator multiple times, it is probably going to be used
            // often, so increase its weight.
            Tex_Weight_Args w_args = {
                .allocations = alloc->allocations,
                .weights     = alloc->allocation_weights,
                .states      = alloc->allocation_states,
                .indices     = alloc->allocation_indices,
                .count       = alloc->allocation_count,
                .idx = i, // The current index is the key to the allocation attributes.
                .inc = 1,
                .dec = 0 // @Test Find effective inc and dec values
            };
            adjust_allocation_weights(&w_args);
            *key = i;
            return GPU_ALLOCATOR_RESULT_SUCCESS;
        }

    // .allocations is not a dynamic array, so check capacity if this is a new allocation.
    assert(alloc->allocation_count < alloc->allocation_cap);
    if (alloc->allocation_count >= alloc->allocation_cap)
        return GPU_ALLOCATOR_RESULT_ALLOCATOR_FULL;

    // Add the new hash. It is fine to do this here even though we may early return, as the
    // allocation count is only incremented at the end of the function. Before this happens, this
    // hash is not visible outside of this function, as it is not accounted for in the count. Doing
    // this later would be sub optimal, as right now, the end of the hashes array is hot in the
    // cache.
    alloc->hashes[alloc->allocation_count] = hash;

    // Images are forced to have four channels, as Vulkan can reject a format with fewer.
    Image image      = load_image(file_name);
    u64 image_size   = image.width * image.height * 4;

    if (align(image_size, alloc->stage_bit_granularity) > alloc->staging_queue_byte_cap) {
        free_image(&image);
        println("Image %s aligned to stage_bit_granularity (%u) would overflow staging queue: image_size = %u, staging_queue_byte_cap = %u",
                 file_name->str, alloc->stage_bit_granularity, align(image_size, alloc->stage_bit_granularity), alloc->staging_queue_byte_cap);

        // Ensure that the image can actually be staged.
        assert(false && "See above");

        return GPU_ALLOCATOR_RESULT_ALLOCATION_TOO_LARGE;
    }

    // VkImage info
    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType         = VK_IMAGE_TYPE_2D;
    image_info.format            = VK_FORMAT_R8G8B8A8_SRGB;
    image_info.extent            = {.width = image.width, .height = image.height, .depth = 1};
    image_info.mipLevels         = 1;
    image_info.arrayLayers       = 1;
    image_info.samples           = get_global_settings()->sample_count;
    image_info.tiling            = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage             = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.initialLayout     = VK_IMAGE_LAYOUT_UNDEFINED;

    // Create VkImage
    VkImage vk_image;
    VkDevice device = get_gpu_instance()->device;
    auto check      = vkCreateImage(device, &image_info, ALLOCATION_CALLBACKS, &vk_image);
    DEBUG_OBJ_CREATION(vkCreateImage, check);

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device, vk_image, &req);

    u64 aligned_size_upload = align(req.size, req.alignment);

    if (aligned_size_upload > alloc->upload_queue_byte_cap) {
        println("Image %s aligned to upload_bit_granularity (%u) would overflow upload queue: image_size = %u, upload_queue_byte_cap = %u",
                 file_name->str, alloc->upload_bit_granularity, align(image_size, alloc->upload_bit_granularity), alloc->upload_queue_byte_cap);

        assert(false && "See Above");

        vkDestroyImage(device, vk_image, ALLOCATION_CALLBACKS);
        free_image(&image);
        return GPU_ALLOCATOR_RESULT_ALLOCATION_TOO_LARGE;
    }

    // Fill in allocation fields
    Gpu_Tex_Allocation *p_allocation = &alloc->allocations[alloc->allocation_count];
    *p_allocation = {};

    p_allocation->file_name        = string_buffer_get_string(&alloc->string_buffer, file_name);
    p_allocation->width            = image.width;
    p_allocation->height           = image.height;
    p_allocation->image            = vk_image;
    p_allocation->size             = aligned_size_upload;
    p_allocation->upload_alignment = req.alignment;

    alloc->allocation_states[alloc->allocation_count] = 0x0;

    free_image(&image);

    *key = alloc->allocation_count;
    alloc->allocation_indices[alloc->allocation_count] = alloc->allocation_count;
    alloc->allocation_count++;

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result tex_staging_queue_begin(Gpu_Tex_Allocator *alloc) {
    // .to_stage_count is set to Max_u32 on queue submission, indicating it is safe to use again.
    if (alloc->to_stage_count != Max_u32)
        return GPU_ALLOCATOR_RESULT_QUEUE_IN_USE;

    alloc->to_stage_count = 0;
    alloc->staging_queue_byte_count = 0;

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result tex_staging_queue_add(Gpu_Tex_Allocator *alloc, u32 key, bool adjust_weights) {
    u8 inc = 10 & ~(Max_u8 + (u8)(adjust_weights));
    u8 dec =  1 & ~(Max_u8 + (u8)(adjust_weights));

    Tex_Weight_Args w_args = {
        .allocations = alloc->allocations,
        .weights     = alloc->allocation_weights,
        .states      = alloc->allocation_states,
        .indices     = alloc->allocation_indices,
        .count       = alloc->allocation_count,
        .idx = key,
        .inc = inc,
        .dec = dec, // @Test Find effective inc and dec values
    };

    u32 allocation_count               = alloc->allocation_count;
    Gpu_Tex_Allocation *allocations    = alloc->allocations;
    Gpu_Allocation_State_Flags *states = alloc->allocation_states;

    u32 idx = alloc->allocation_indices[key];

    // If the allocation is already uploaded or marked to be uploaded, early return.
    if (states[idx] & ALLOCATION_STATE_STAGED_BIT || states[idx] & ALLOCATION_STATE_TO_STAGE_BIT) {

        // Flag the allocation as having been called up.
        states[idx] |= ALLOCATION_STATE_TO_DRAW_BIT;

        #if TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO
        if (states[idx] & ALLOCATION_STATE_STAGED_BIT) {
            println("Tex Allocator Staging Queue: Queued cached allocation %s", allocations[idx].file_name.str);
        } else {
            println("Tex Allocator Staging Queue: Uncached allocation %s is already queued", allocations[idx].file_name.str);
        }
        #endif

        adjust_allocation_weights(&w_args);
        return GPU_ALLOCATOR_RESULT_SUCCESS;
    }

    // Allocations' offsets must be aligned to their bit representation. See implementation
    // information (grep '~MAID').
    u64 img_size         = allocations[idx].width * allocations[idx].height * 4;
    u64 bit_aligned_size = align(img_size, alloc->stage_bit_granularity);

    if (bit_aligned_size + alloc->staging_queue_byte_count > alloc->staging_queue_byte_cap) {

        #if TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO
        println("Tex Allocator Staging Queue: Failed to queue uncached allocation %s, queued bytes: %u, queue cap: %u, allocation aligned size: %u",
                allocations[idx].file_name.str, alloc->staging_queue_byte_count, alloc->staging_queue_byte_cap, bit_aligned_size);
        #endif

        return GPU_ALLOCATOR_RESULT_QUEUE_FULL;
    }

    states[idx]                     |= ALLOCATION_STATE_TO_STAGE_BIT | ALLOCATION_STATE_TO_DRAW_BIT;
    alloc->staging_queue_byte_count += bit_aligned_size;
    alloc->to_stage_count++;

    #if TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO
    println("Tex Allocator Staging Queue: Queued uncached allocation %s, queued bytes: %u, allocation aligned size: %u", allocations[idx].file_name.str, alloc->staging_queue_byte_count, bit_aligned_size);
    #endif

    // Only adjust weights if the queue was successful, otherwise retries to add an allocation to the
    // queue will make the weights inaccurate.
    adjust_allocation_weights(&w_args);

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result tex_staging_queue_submit(Gpu_Tex_Allocator *alloc) {
    // If the to stage count is zero on queue submission, just assume that everything queued was
    // already cached, and we need not do anything.
    if (alloc->to_stage_count == 0) {
        alloc->to_stage_count = Max_u32;

        #if TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO
        println("Tex Allocator Staging Queue: All allocations cached on queue submission");
        #endif

        return GPU_ALLOCATOR_RESULT_SUCCESS;
    }

    #if TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO
    println("Tex Allocator Staging Queue: Beginning queue submission, %u uncached allocations in queue", alloc->to_stage_count);
    #endif

    Gpu_Allocation_State_Flags *states = alloc->allocation_states;
    Gpu_Tex_Allocation  *allocations   = alloc->allocations;
    Gpu_Tex_Allocation   allocation;

    u32 allocation_count  = alloc->allocation_count;
    u64 queue_size        = alloc->staging_queue_byte_count;

    u32 mask_count = alloc->stage_mask_count;
    u64 *masks     = alloc->stage_masks;

    u32 g          = alloc->stage_bit_granularity;
    u32 req_bits   = alloc->staging_queue_byte_count / g; // This size is aligned to g (being the sum of aligned sizes), so need to worry about remainder

    u32 idx;
    u32 indices_count;
    u32 *indices   = (u32*)malloc_t(sizeof(u32) * allocation_count, 16); // Align 16 for SIMD

    // @Note Although I would like to, this section cannot really be moved into its own function
    // cleanly, as the internal logic has to be so slightly different each time (such as which size
    // to use, or how to calculate the size). So it is easier to just inline it and not fuss...
    //
    // Section Explanation: If no contiguous block of free memory sufficient to hold the size of the
    // stage queue is available in the stage buffer (as represented by the bit masks), find the
    // allocations flagged as stageed, but which are not flagged for staging, uploading or drawing;
    // loop these allocations, starting at the allocation with the lowest weight (see implementation
    // details above for what 'weight' means); mark the allocation's range as free in the bit mask,
    // check if there is now a large enough size, and if so, break; if we have otherwise looped all
    // allocations and there is no room, restore the bit masks (as nothing will actually be
    // overwritten) and return error code.

    u32 free_block = find_contiguous_free(mask_count, masks, req_bits);
    if (free_block == Max_u32) {
        // In case of failure, we need to restore the masks initial states. (Failure should be
        // incredibly unlikely, if it ever happens at all. The code using the allocators should use
        // them efficiently.)
        u64 *mask_copies = (u64*)malloc_t(sizeof(u64) * mask_count, 16);
        memcpy(mask_copies, masks, sizeof(u64) * mask_count);

        u32 bit_size;
        u32 bit_offset;
        indices_count = simd_find_flags_u8(allocation_count, states, ALLOCATION_STATE_STAGED_BIT, ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_TO_DRAW_BIT | ALLOCATION_STATE_TO_STAGE_BIT, indices);
        for(u32 i = indices_count - 1; i != Max_u32; --i) {
            u32 idx = indices[i];
            allocation = allocations[idx];

            // @Note Really g should always be power of 2, so these should be bit shifts, not divides.
            bit_size   = align(allocation.width * allocation.height * 4, g) / g;
            bit_offset = allocation.stage_offset / g;

            // Clear the allocation's range in the bit masks, and check if there is now a large
            // enough free block.
            make_free(mask_count, masks, bit_offset, bit_size);
            free_block = find_contiguous_free(mask_count, masks, req_bits);

            if (free_block != Max_u32) {
                // Only mark allocations as having been evicted from staging buffer if they are
                // actually going to be evicted (i.e. only if a sufficient free block is actually
                // available).
                //
                // @Todo This should be implemented as simd_update_flags_u8(..) but with the ability
                // to start from an offset. Doing this as a loop over individual u8s is very very
                // lame.
                u32 j;
                for(j = i; j < indices_count; ++j) {
                    states[indices[j]] &= ~ALLOCATION_STATE_STAGED_BIT;
                }

                #if TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO
                println("Tex Allocator Staging Queue: Evicted %u allocations from staging buffer", j);
                #endif

                goto free_block_found; // jump over early return
            }
        }

        #if TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO
        // @Todo Should add a thing to fetch the actual free space in the free block to judge fragmentation
        println("Tex Allocator Staging Queue: Insufficient space in staging buffer for queue submission");
        #endif

        // Restore the masks to before allocations were marked as free.
        memcpy(masks, mask_copies, sizeof(u64) * mask_count);
        return GPU_ALLOCATOR_RESULT_STAGE_FULL; // Too many allocations waiting to draw
    }

    free_block_found: // goto label

    // Find how large free_block is.
    u64 block_size = get_block_size(mask_count, masks, free_block) * g;

    // Find all allocations which are not flagged as ALLOCATION_STATE_STAGED_BIT or ALLOCATION_STATE_TO_STAGE_BIT
    indices_count = simd_find_flags_u8(allocation_count, states, 0x00, ALLOCATION_STATE_STAGED_BIT | ALLOCATION_STATE_TO_STAGE_BIT, indices);

    // Loop the above allocations, starting at the highest weight (lowest index). If there is size
    // available in the free block for the queued allocations and the current allocation in loop,
    // add the allocation to the list of allocations that we want to stage.
    u64 size = 0;
    for(u32 i = 0; i < indices_count; ++i) {
        idx = indices[i];

        size += align(allocations[idx].width * allocations[idx].height * 4, g);

        // @Test This static predicts that there will be room for at least one allocation, it might be
        // best to switch it around.
        if (size > block_size - queue_size) {
            break;
        } else {
            states[idx] |= ALLOCATION_STATE_TO_STAGE_BIT;
        }
    }
    // Get the final list of allocations to stage.
    indices_count = simd_find_flags_u8(allocation_count, states, ALLOCATION_STATE_TO_STAGE_BIT, 0x00, indices);

    #if TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO
    println("Tex Allocator Staging Queue: Added %u allocations to submission queue (pre-emptive cache)", indices_count - alloc->to_stage_count);
    #endif

    // Loop vars
    u64   image_size;
    Image image;
    Gpu_Allocation *p_allocation;

    u64 stage_offset = free_block * g;
    u8 *stage_ptr    = (u8*)alloc->stage_ptr;

    for(u32 i = 0; i < indices_count; ++i) {
        idx = indices[i];

        image      = load_image(&allocations[idx].file_name);
        image_size = image.width * image.height * 4;

        memcpy(stage_ptr + stage_offset, image.data, image_size);

        #if TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO
        println("Tex Allocator Staging Queue: Staged image %s, offset: %u", allocations[idx].file_name.str, stage_offset);
        #endif

        allocations[idx].stage_offset = stage_offset;
        stage_offset                 += align(image_size, g);

        assert(stage_offset <= alloc->stage_cap && "Allocator Stage Overflow");

        // @Todo I so despise that I did not get the temp allocator working with this. Take another look.
        free_image(&image);
    }

    #if TEX_ALLOCATOR_STAGING_QUEUE_PROGRESS_INFO
    println("Tex Allocator Staging Queue: %u allocations uploaded to staging buffer", indices_count);
    #endif

    // Set all TO_STAGE flagged allocations to STAGED and clear TO_STAGE bit.
    simd_update_flags_u8(allocation_count, states, ALLOCATION_STATE_TO_STAGE_BIT, 0x0, ALLOCATION_STATE_STAGED_BIT, ALLOCATION_STATE_TO_STAGE_BIT);

    // Fill the bit masks where the data was copied to.
    make_full(alloc->stage_mask_count, alloc->stage_masks, free_block, stage_offset / g);

    alloc->to_stage_count = Max_u32;
    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result tex_upload_queue_begin(Gpu_Tex_Allocator *alloc) {
    // .to_upload_count is set to max upon successful queue submission, indicating the queue
    // is safe to use again.
    if (alloc->to_upload_count != Max_u32)
        return GPU_ALLOCATOR_RESULT_QUEUE_IN_USE;

    alloc->to_upload_count = 0;
    alloc->upload_queue_byte_count = 0;

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result tex_upload_queue_add(Gpu_Tex_Allocator *alloc, u32 key, bool adjust_weights) {
    // Increment the allocation's weight since it has been referenced.
    // See implementation info (grep '~MAID').

    u8 inc = 10 & ~(Max_u8 + (u8)(adjust_weights));
    u8 dec =  1 & ~(Max_u8 + (u8)(adjust_weights));

    Tex_Weight_Args w_args = {
        .allocations = alloc->allocations,
        .weights     = alloc->allocation_weights,
        .states      = alloc->allocation_states,
        .indices     = alloc->allocation_indices,
        .count       = alloc->allocation_count,
        .idx = key,
        .inc = inc,
        .dec = dec, // @Test Find effective inc and dec values
    };

    Gpu_Tex_Allocation *allocations    = alloc->allocations;
    Gpu_Allocation_State_Flags *states = alloc->allocation_states;

    u32 idx = alloc->allocation_indices[key];

    // If the allocation is already uploaded or marked to be uploaded, early return.
    if (states[idx] & ALLOCATION_STATE_UPLOADED_BIT || states[idx] & ALLOCATION_STATE_TO_UPLOAD_BIT) {
        states[idx] |= ALLOCATION_STATE_TO_DRAW_BIT;

        adjust_allocation_weights(&w_args);
        return GPU_ALLOCATOR_RESULT_SUCCESS;
    }

    if (alloc->to_upload_count >= alloc->to_upload_cap)
        return GPU_ALLOCATOR_RESULT_QUEUE_FULL;

    u64 size                  = allocations[idx].size;
    u64 aligned_upload_offset = align(alloc->upload_queue_byte_count, allocations[idx].upload_alignment);

    if (size + aligned_upload_offset > alloc->upload_queue_byte_cap) {
        // If the queue add fails, we do not want stuff marked as to draw that is not also part of a
        // queue.
        return GPU_ALLOCATOR_RESULT_QUEUE_FULL;
    }

    states[idx]                    |= ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_TO_DRAW_BIT;
    alloc->upload_queue_byte_count  = aligned_upload_offset + size;
    alloc->to_upload_count++;

    // 'idx' becomes invalid after this call
    adjust_allocation_weights(&w_args);

    return GPU_ALLOCATOR_RESULT_SUCCESS;
}

Gpu_Allocator_Result tex_upload_queue_submit(Gpu_Tex_Allocator *alloc) {
    // If the to upload count is zero on queue submission, just assume that everything queued was
    // already cached, and we need not do anything.
    if (alloc->to_upload_count == 0) {
        alloc->to_upload_count = Max_u32;
        return GPU_ALLOCATOR_RESULT_SUCCESS;
    }

    Gpu_Allocation_State_Flags *states = alloc->allocation_states;
    Gpu_Tex_Allocation  *allocations   = alloc->allocations;
    Gpu_Tex_Allocation   allocation;

    u32 allocation_count  = alloc->allocation_count;
    u64 queue_size        = alloc->upload_queue_byte_count;

    u32 mask_count = alloc->upload_mask_count;
    u64 *masks     = alloc->upload_masks;
    u32 g          = alloc->upload_bit_granularity;
    u32 req_bits   = queue_size / g; // This size is aligned to g (being the sum of aligned sizes), so need to worry about remainder

    u32 idx;
    u32 indices_count;
    u32 *indices = (u32*)malloc_t(sizeof(u32) * allocation_count, 16); // Align 16 for SIMD

    // @Note Although I would like to, this section cannot really be moved into its own function
    // cleanly, as the internal logic has to be so slightly different each time (such as which size
    // to use, or how to calculate the size). So it is easier to just inline it and not fuss...
    //
    // Section Explanation: If no contiguous block of free memory sufficient to hold the size of the
    // upload queue is available in the upload buffer (as represented by the bit masks), find the
    // allocations flagged as uploaded, but which are not flagged for uploading or drawing; loop
    // these allocations, starting at the allocation with the lowest weight (see implementation
    // details above for what 'weight' means); mark the allocation's range as free in the bit mask,
    // check if there is now a large enough size, and if so, break; if we have otherwise looped all
    // allocations and there is no room, restore the masks (as nothing will actually be overwritten)
    // and return error code.

    u32 free_block = find_contiguous_free(alloc->upload_mask_count, alloc->upload_masks, req_bits);
    if (free_block == Max_u32) {
        // In case of failure, we need to restore the masks initial states. (Failure should be
        // incredibly unlikely, if it ever happens at all. The code using the allocators should use
        // them efficiently.)
        u64 *mask_copies = (u64*)malloc_t(sizeof(u64) * mask_count, 16);
        memcpy(mask_copies, masks, sizeof(u64) * mask_count);

        u32 bit_size;
        u32 bit_offset;
        indices_count = simd_find_flags_u8(allocation_count, states, ALLOCATION_STATE_UPLOADED_BIT, ALLOCATION_STATE_TO_DRAW_BIT | ALLOCATION_STATE_TO_UPLOAD_BIT, indices);
        for(u32 i = indices_count - 1; i != Max_u32; --i) {
            idx = indices[i];
            allocation = allocations[idx];

            // @Note Really g is always a power of 2, so these should be bit shifts, not divides.
            bit_size   = allocation.size / g; // No need to align size, as image upload sizes already are.
            bit_offset = allocation.upload_offset / g;

            // Clear the allocation's range in the bit masks, and check if there is now a large
            // enough free block.
            make_free(mask_count, masks, bit_offset, bit_size);
            free_block = find_contiguous_free(mask_count, masks, req_bits);

            if (free_block != Max_u32) {
                // Only mark allocations as having been evicted from upload buffer if they are
                // actually going to be evicted (i.e. only if a sufficient free block is actually
                // available).
                //
                // @Todo This should be implemented as simd_update_flags_u8(..) but with the ability
                // to start from an offset. Doing this as a loop over individual u8s is very very
                // lame.
                for(u32 j = i; j < indices_count; ++j)
                    states[indices[j]] &= ~ALLOCATION_STATE_UPLOADED_BIT;

                goto free_block_found; // jump over early return
            }
        }
        return GPU_ALLOCATOR_RESULT_STAGE_FULL; // Too many allocations waiting to draw
    }

    free_block_found: // goto label

    // Find how large free_block is.
    u64 block_size = get_block_size(mask_count, masks, free_block) * g;

    // Find the indices of all allocations which are not flagged ALLOCATION_STATE_UPLOADED_BIT or
    // ALLOCATION_STATE_TO_UPLOAD_BIT.
    indices_count = simd_find_flags_u8(allocation_count, states, 0x00, ALLOCATION_STATE_TO_UPLOAD_BIT | ALLOCATION_STATE_UPLOADED_BIT, indices);

    // Loop the above allocations. If there is size available in the free block for the queued
    // allocations and the current allocation in loop, add the allocation to the list of allocations
    // that we want to upload.
    u64 size = 0;
    for(u32 i = 0; i < indices_count; ++i) {
        idx = indices[i];

        size  = align(size, allocations[idx].upload_alignment);
        size += allocations[idx].size;

        // @Test This static predicts that there will be room for at least one allocation, it might be
        // best to switch it around.
        if (size > block_size - queue_size) {
            break;
        } else {
            states[idx] |= ALLOCATION_STATE_TO_UPLOAD_BIT;
        }
    }
    // Get the final list of allocations to upload.
    indices_count = simd_find_flags_u8(allocation_count, states, ALLOCATION_STATE_TO_UPLOAD_BIT, 0x00, indices);

    // Create bind infos
    u64 upload_offset = free_block * g;
    VkBindImageMemoryInfo *bind_infos =
        (VkBindImageMemoryInfo*)malloc_t(sizeof(VkBindImageMemoryInfo) * indices_count, 8);

    Gpu_Tex_Allocation *p_allocation;
    VkDeviceMemory upload = alloc->upload;
    for(u32 i = 0; i < indices_count; ++i) {
        idx = indices[i];

        p_allocation  = &allocations[indices[i]];
        upload_offset = align(upload_offset, p_allocation->upload_alignment);

        p_allocation->upload_offset = upload_offset;

        bind_infos[i]              = {VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO};
        bind_infos[i].image        = p_allocation->image;
        bind_infos[i].memory       = upload;
        bind_infos[i].memoryOffset = upload_offset;

        upload_offset += p_allocation->size;
        assert(upload_offset <= alloc->upload_cap && "Tex Allocator Upload Overflow");
    }

    // Fill the bit masks where the data was copied to.
    make_full(alloc->upload_mask_count, alloc->upload_masks, free_block, upload_offset / g);

    Gpu *gpu        = get_gpu_instance();
    VkDevice device = gpu->device;

    // Bind images to memory - from the spec:-
    //      "If vkBindImageMemory2 fails, and bindInfoCount was greater than one, then the
    //       images referenced by pBindInfos will be in an indeterminate state, and must not be
    //       used. Applications should destroy these images."
    // ...hence recreate_images(..) and the note on what to do about failure.
    if (vkBindImageMemory2(device, indices_count, bind_infos) != VK_SUCCESS) {
        // @Note Not quite sure what to do with failure here. Returning like this leaves the queue
        // still full, which means the client can call retry and this point should just be reached
        // instantly again.  And then if the bind fails again they can reset the queue themselves.
        // You could argue that the allocator should empty the queue and reset itself. But I will
        // stick with this for now. Idk how serious a bind failure is really. In my case it should
        // never happen as all my memory is allocated once.
        recreate_images(alloc, indices_count, indices);
        return GPU_ALLOCATOR_RESULT_BIND_IMAGE_FAIL;
    } else {
        simd_update_flags_u8(allocation_count, states, ALLOCATION_STATE_TO_UPLOAD_BIT, 0x0, ALLOCATION_STATE_UPLOADED_BIT, ALLOCATION_STATE_TO_UPLOAD_BIT);
    }

    //
    // Record the copy commands and the pipeline barriers into secondary command buffers stored in
    // the allocator.
    //

    u32 frame_index = g_frame_index;

    // Allocate graphics command buffers
    vkResetCommandPool(device, alloc->transfer_cmd_pools[frame_index], 0x0);

    VkCommandBufferAllocateInfo cmd_alloc_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc_info.commandPool                 = alloc->graphics_cmd_pools[frame_index];
    cmd_alloc_info.level                       = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    cmd_alloc_info.commandBufferCount          = 1;

    auto check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->graphics_cmds[frame_index]);
    DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

    VkCommandBufferInheritanceInfo inheritance = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    VkCommandBufferBeginInfo begin_info        = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags                           = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo                = &inheritance;

    // Allocate image barriers
    VkImageMemoryBarrier2 *barrs =
        (VkImageMemoryBarrier2*)malloc_t(sizeof(VkImageMemoryBarrier2) * indices_count, 8);

    // Setup image layout transition for optimal transfer
    for(u32 i = 0; i < indices_count; ++i) {
        barrs[i] = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};

        // Ignore src, previous layout does not matter. Same for queue families.
        barrs[i].dstStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR;
        barrs[i].dstAccessMask       = VK_ACCESS_2_MEMORY_WRITE_BIT_KHR;
        barrs[i].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
        barrs[i].newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrs[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrs[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrs[i].image               = allocations[indices[i]].image;

        // @Todo Switch to an image format which actually has mipmapping (ktx2)
        barrs[i].subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        };
    }

    // This info is persistent across all transitions, so fill in once here
    VkDependencyInfo dep        = {VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = indices_count;
    dep.pImageMemoryBarriers    = barrs;

    // This info is persistent across all copy commands, so fill in once here
    VkBufferImageCopy2 region          = {VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
    VkCopyBufferToImageInfo2 copy_info = {VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2};
    copy_info.srcBuffer                = alloc->stage;
    copy_info.regionCount              = 1; // @Note Can I store multiple textures in one image??
    copy_info.dstImageLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    copy_info.pRegions                 = &region;

    // Create local copies
    VkCommandBuffer graphics_cmd = alloc->graphics_cmds[frame_index];
    u32 transfer_queue_index     = gpu->transfer_queue_index;
    u32 graphics_queue_index     = gpu->graphics_queue_index;

    // if NOT discrete transfer queue (static predict discrete transfer)
    if (transfer_queue_index == graphics_queue_index) {
        vkBeginCommandBuffer(graphics_cmd, &begin_info);

        // Barrier to transition image to optimal transfer layout
        vkCmdPipelineBarrier2(graphics_cmd, &dep);

        // Fill in copy regions
        for(u32 i = 0; i < indices_count; ++i) {
            allocation = allocations[indices[i]];

            region.bufferOffset      = allocation.stage_offset;
            region.bufferRowLength   = allocation.width;
            region.bufferImageHeight = allocation.height;
            region.imageOffset = {.x = 0, .y = 0, .z = 0};
            region.imageExtent = {.width = allocation.width, .height = allocation.height, .depth = 1};

            region.imageSubresource  = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            };

            // Copy image data
            copy_info.dstImage = allocation.image;
            vkCmdCopyBufferToImage2(graphics_cmd, &copy_info);
        }

        // Barrier to transition image to optimal shader read layout
        for(u32 i = 0; i < indices_count; ++i) {
            barrs[i].srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
            barrs[i].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
            barrs[i].dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
            barrs[i].dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT_KHR;
            barrs[i].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs[i].newLayout     = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        }
        vkCmdPipelineBarrier2(graphics_cmd, &dep);

        vkEndCommandBuffer(graphics_cmd);
    } else {

        vkResetCommandPool(device, alloc->transfer_cmd_pools[frame_index], 0x0);

        cmd_alloc_info.commandPool = alloc->transfer_cmd_pools[frame_index];

        // Allocate transfer command buffer
        check = vkAllocateCommandBuffers(device, &cmd_alloc_info, &alloc->transfer_cmds[frame_index]);
        DEBUG_OBJ_CREATION(vkAllocateCommandBuffers, check);

        VkCommandBuffer transfer_cmd = alloc->transfer_cmds[frame_index];
        vkBeginCommandBuffer(transfer_cmd, &begin_info);

        // Barrier to transition image to optimal transfer layout
        vkCmdPipelineBarrier2(transfer_cmd, &dep);

        // Fill in copy regions
        for(u32 i = 0; i < indices_count; ++i) {
            allocation = alloc->allocations[indices[i]];

            region.bufferOffset      = allocation.stage_offset;
            region.bufferRowLength   = allocation.width;
            region.bufferImageHeight = allocation.height;
            region.imageOffset       = {.x = 0, .y = 0, .z = 0};
            region.imageExtent       = {.width = allocation.width, .height = allocation.height, .depth = 1};

            region.imageSubresource  = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel       = 0,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            };

            // Copy image data
            copy_info.dstImage = allocation.image;
            vkCmdCopyBufferToImage2(transfer_cmd, &copy_info);
        }

        // Queue ownership release + transition image to shader read optimal layout
        for(u32 i = 0; i < indices_count; ++i) {
            barrs[i].srcStageMask        = VK_PIPELINE_STAGE_2_TRANSFER_BIT_KHR,
            barrs[i].srcAccessMask       = VK_ACCESS_2_TRANSFER_WRITE_BIT_KHR,
            barrs[i].oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs[i].newLayout           = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            barrs[i].srcQueueFamilyIndex = transfer_queue_index;
            barrs[i].dstQueueFamilyIndex = graphics_queue_index;
        }
        vkCmdPipelineBarrier2(transfer_cmd, &dep);

        vkEndCommandBuffer(transfer_cmd);

        vkBeginCommandBuffer(graphics_cmd, &begin_info);

        // Queue ownership acquire + transition image to shader read optimal layout
        for(u32 i = 0; i < indices_count; ++i) {
            barrs[i].dstStageMask        = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT_KHR;
            barrs[i].dstAccessMask       = VK_ACCESS_2_SHADER_READ_BIT_KHR;
            barrs[i].oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrs[i].newLayout           = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            barrs[i].srcQueueFamilyIndex = transfer_queue_index;
            barrs[i].dstQueueFamilyIndex = graphics_queue_index;
        }
        vkCmdPipelineBarrier2(graphics_cmd, &dep);

        vkEndCommandBuffer(graphics_cmd);
    }

    // @Note This could be perceived as a little premature, as the commands have not actually been
    // submitted.  However, the commands having been recorded constitutes upload complete from the
    // ALLOCATOR's point of view: the draw commands will happen later in the same queue submission
    // section later down the pipeline, which the allocator is not a part of. Furthermore, from the
    // allocation's perspective, as they will be truly uploaded in the same point of the pipeline as
    // the draw commands (the upload cmds will even be in the same batch as the draw commands if
    // unified transfer), they cannot react to a ALLOCATION_STATE_TO_UPLOAD_BIT state. I cannot,
    // call queue submit, wait for the semaphore, then set ALLOCATION_STATE_UPLOADED_BIT state, then
    // call draw, as the draws are already submitted with the submission that the semaphore wait was
    // signalling.
    simd_update_flags_u8(allocation_count, states, ALLOCATION_STATE_TO_UPLOAD_BIT, 0x0, ALLOCATION_STATE_UPLOADED_BIT, ALLOCATION_STATE_TO_UPLOAD_BIT);

    alloc->to_upload_count = Max_u32;
    return GPU_ALLOCATOR_RESULT_SUCCESS;
}
    /* End Model Texture and Vertex/Index Attribute Allocators */

    /* Begin Sampler Allocator */
Sampler_Allocator create_sampler_allocator(u32 cap) {
    Sampler_Allocator ret = {};

    ret.device = get_gpu_instance()->device;

    u64 device_cap = get_gpu_instance()->info.props.limits.maxSamplerAllocationCount;
    if (cap)
        ret.cap = align(cap, 16);
    else
        ret.cap = align(device_cap, 16); // malloc'd size must be aligned to 16 for correct_weights() simd

    ret.device_cap = device_cap;
    ret.samplers   = (Sampler_Info*)malloc_h(sizeof(Sampler_Info) * ret.cap, 8);

    // Align 16 for SIMD
    u64 aligned_cap = align(ret.cap, 16);
    u64 block_size  = align(ret.cap * 8, 16);
    block_size     += aligned_cap * 2;
    u64 *block      = (u64*)malloc_h(block_size, 16);

    memset(block, 0, block_size);

    ret.hashes  = block;
    ret.weights = (u8*)(block + aligned_cap);
    ret.flags   = ret.weights + aligned_cap;

    return ret;
}

void destroy_sampler_allocator(Sampler_Allocator *alloc) {
    u32 *indices = (u32*)malloc_t(sizeof(u32) * alloc->active, 16);
    u32 active_count = simd_find_flags_u8(alloc->count, alloc->flags, SAMPLER_ACTIVE_BIT, 0x0, indices);

    for(u32 i = 0; i < active_count; ++i)
        vkDestroySampler(alloc->device, alloc->samplers[indices[i]].sampler, ALLOCATION_CALLBACKS);

    free_h(alloc->samplers);
    free_h(alloc->hashes);
}

Sampler_Allocator_Result add_sampler(Sampler_Allocator *alloc, Get_Sampler_Info *sampler_info, u32 *key) {
    //
    // @Note Ik that the hash will change when the sampler handle in the 'Sampler' type
    // changes, but calling 'insert_hash()' doesnt actually do a rehash, so the hash that the
    // sampler is inserted with will always be its key.
    //

    u64 hash  = hash_bytes(sampler_info, sizeof(Get_Sampler_Info));
    u32 h_idx = find_hash_idx(alloc->count, alloc->hashes, hash); // have we already seen this sampler type

    if (h_idx < alloc->count) {
        adjust_weights(alloc->count, alloc->weights, h_idx, 1, 0);
        *key = h_idx;
        return SAMPLER_ALLOCATOR_RESULT_SUCCESS;
    }

    assert(alloc->count <= alloc->cap);
    if (alloc->count >= alloc->cap) {
        *key = Max_u32;
        return SAMPLER_ALLOCATOR_RESULT_ALLOCATOR_FULL;
    }

    Sampler_Info info = {
        .wrap_s      = sampler_info->wrap_s,
        .wrap_t      = sampler_info->wrap_t,
        .mipmap_mode = sampler_info->mipmap_mode,
        .mag_filter  = sampler_info->mag_filter,
        .min_filter  = sampler_info->min_filter,
    };
    alloc->samplers[alloc->count] = info;
    alloc->hashes  [alloc->count] = hash;
    alloc->count++;

    *key = h_idx;
    return SAMPLER_ALLOCATOR_RESULT_SUCCESS;
}

Sampler_Allocator_Result get_sampler(Sampler_Allocator *alloc, u32 key, VkSampler *ret_sampler,
                                     bool do_weight_adjustment)
{
    assert(key < alloc->count && "Invalid Sampler Key");
    if (key >= alloc->count)
        return SAMPLER_ALLOCATOR_RESULT_INVALID_KEY;

    Sampler_Info *info = &alloc->samplers[key];

    if (!info->sampler) {

        if (alloc->in_use == alloc->device_cap) {
            return SAMPLER_ALLOCATOR_RESULT_ALL_IN_USE;
        } else {
            alloc->in_use++;
        }

        float anisotropy = get_global_settings()->anisotropy;

        VkSamplerCreateInfo create_info = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        create_info.magFilter           = info->mag_filter;
        create_info.minFilter           = info->min_filter;
        create_info.addressModeU        = info->wrap_s;
        create_info.addressModeV        = info->wrap_t;
        create_info.anisotropyEnable    = (VkBool32)(anisotropy > 0);
        create_info.maxAnisotropy       = anisotropy;

        if (alloc->active == alloc->device_cap) {

            u32 evict_idx = find_lowest_weight_with_without_flags(alloc->count, alloc->weights, alloc->flags,
                                                                  SAMPLER_ACTIVE_BIT, SAMPLER_IN_USE_BIT);

            Sampler_Info *to_evict = &alloc->samplers[evict_idx];

            // Clear 'active' flag
            alloc->flags[evict_idx] &= ~(u8)SAMPLER_ACTIVE_BIT;
            vkDestroySampler(alloc->device, to_evict->sampler, ALLOCATION_CALLBACKS);

            to_evict->sampler = NULL;
            alloc->active--;
        }
        auto check = vkCreateSampler(alloc->device, &create_info, ALLOCATION_CALLBACKS, &info->sampler);
        DEBUG_OBJ_CREATION(vkCreateSampler, check);

        alloc->flags[key] |= (u8)SAMPLER_ACTIVE_BIT;
        alloc->active++;
    }

    alloc->flags[key] |= (u8)SAMPLER_IN_USE_BIT;

    u8 inc = 10 & max8_if_true(do_weight_adjustment);
    u8 dec =  1 & max8_if_true(do_weight_adjustment);

    adjust_weights(alloc->count, alloc->weights, key, inc, dec);

   *ret_sampler = info->sampler;
    info->user_count++;
    return SAMPLER_ALLOCATOR_RESULT_SUCCESS;
}

void done_with_sampler(Sampler_Allocator *alloc, u32 key) {
    assert(key < alloc->count && "This is not a valid sampler hash");

    Sampler_Info *sampler  = &alloc->samplers[key];
    sampler->user_count   -= (sampler->user_count > 0);

    u32 user_count = sampler->user_count;
    u8 rm_flag     = ~((u8)SAMPLER_IN_USE_BIT);

    alloc->flags[key] &= rm_flag | max8_if_true(user_count > 0); // if user_count > 0 then flags &= 0xff
    alloc->in_use     -= (user_count == 0);

    return;
}
        /* End Sampler Allocation */

        /* Begin Image View Allocator */
Image_View_Allocator create_image_view_allocator(u32 cap) {
    Image_View_Allocator ret = {};

    ret.device = get_gpu_instance()->device;

    ret.cap = align(cap, 16); // malloc'd size must be aligned to 16 for correct_weights() simd
    ret.map = HashMap<u64, Image_View>::get(ret.cap);

    // Align 16 for SIMD
    u64 aligned_cap = align(ret.cap, 16);
    u64 block_size  = align(ret.cap * 8, 16);
    block_size     += aligned_cap * 2;
    u64 *block      = (u64*)malloc_h(block_size, 16);

    memset(block, 0, block_size);

    ret.hashes  = block;
    ret.weights = (u8*)(block + aligned_cap);
    ret.flags   = ret.weights + aligned_cap;

    return ret;
}
void destroy_image_view_allocator(Image_View_Allocator *alloc) {
    for(u32 i = 0; i < alloc->count; ++i)
        vkDestroyImageView(alloc->device, alloc->map.find_hash(alloc->hashes[i])->view, ALLOCATION_CALLBACKS);
    alloc->map.kill();
    free_h(alloc->hashes);
}
/*
   Image View Allocator Implementation is basically identical sampler allocator: a hashmap and some weights
*/
Image_View_Allocator_Result get_image_view(Image_View_Allocator *alloc, VkImageViewCreateInfo *image_view_info,
                                           VkImageView *ret_view, u64 *ret_hash, bool do_weight_adjustment)
{
    u64 hash               = hash_bytes(image_view_info, sizeof(VkImageViewCreateInfo));
    Image_View *image_view = alloc->map.find_hash(hash);

    VkImageView ret;

    u32 h_idx;
    if (!image_view) {
        if (alloc->count == alloc->cap) {

            if (alloc->in_use == alloc->cap)
                return IMAGE_VIEW_ALLOCATOR_RESULT_ALL_IN_USE;

            u8 with_flags    = 0x0; // all weights present in the array represent active image views
            u8 without_flags = IMAGE_VIEW_IN_USE_BIT;
            h_idx            = find_lowest_weight_with_without_flags(alloc->count, alloc->weights, alloc->flags,
                                                                     with_flags, without_flags);
            u64 evict_hash = alloc->hashes[h_idx];

            image_view = alloc->map.find_hash(evict_hash);
            assert(ret && "Arrgh broken hash map? Broken alloc->hashes??");

            vkDestroyImageView(alloc->device, image_view->view, ALLOCATION_CALLBACKS);

            bool del_result = alloc->map.delete_hash(evict_hash);
            assert(del_result && "Arrgh broken hash map? Broken alloc->hashes??");

            alloc->in_use--;
            alloc->count--;
        }

        alloc->flags  [h_idx] = IMAGE_VIEW_IN_USE_BIT;
        alloc->weights[h_idx] = 0;
        alloc->hashes [h_idx] = hash;

        auto check = vkCreateImageView(alloc->device, image_view_info, ALLOCATION_CALLBACKS, &ret);
        DEBUG_OBJ_CREATION(vkCreateImageView, check);

        Image_View new_view = {.view = ret, .user_count = 1};
        alloc->map.insert_hash(hash, &new_view);
        alloc->in_use++;
        alloc->count++;
    } else {
        h_idx = find_hash_idx(alloc->count, alloc->hashes, hash);
        ret   = image_view->view;
        image_view->user_count++;
    }

    u8 inc = 10 & max32_if_true(do_weight_adjustment);
    u8 dec =  1 & max32_if_true(do_weight_adjustment);

    adjust_weights(alloc->count, alloc->weights, h_idx, inc, dec);

   *ret_view = ret;
   *ret_hash = hash;
    return IMAGE_VIEW_ALLOCATOR_RESULT_SUCCESS;
}

void done_with_image_view(Image_View_Allocator *alloc, u64 hash) {
    Image_View *view = alloc->map.find_hash(hash);

    assert(view && "This is not a valid view hash");
    if (!view) {
        // do something dramatic to cause a crash because this should never happen
        *alloc = {};
        return;
    }

    view->user_count -= (view->user_count > 0);

    u32 user_count = view->user_count;
    u8  rm_flag    = ~((u8)IMAGE_VIEW_IN_USE_BIT);

    u32 h_idx            = find_hash_idx(alloc->count, alloc->hashes, hash);
    alloc->flags[h_idx] &= rm_flag | max8_if_true(user_count > 0); // if user_count > 0 then flags &= 0xff
    alloc->in_use       -= (user_count == 0);

    return;
}
        /* End Image View Allocator */

    /* Renderpass Framebuffer Pipeline */
void rp_forward_shadow(VkImageView present_attachment, VkImageView depth_attachment, VkImageView shadow_attachment,
                       VkRenderPass *renderpass, VkFramebuffer *framebuffer)
{
    VkAttachmentDescription depth_description = {};
    depth_description.format         = VK_FORMAT_D16_UNORM;
    depth_description.samples        = VK_SAMPLE_COUNT_1_BIT;
    depth_description.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_description.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_description.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depth_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth_description.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depth_description.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription present_description = {};
    present_description.format         = get_window_instance()->info.imageFormat;
    present_description.samples        = VK_SAMPLE_COUNT_1_BIT;
    present_description.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    present_description.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    present_description.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    present_description.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    u32 attachment_count = 3;
    VkAttachmentDescription attachment_descriptions[3] = {};
    attachment_descriptions[0] = present_description;
    attachment_descriptions[1] = depth_description;
    attachment_descriptions[2] = depth_description;

    VkAttachmentReference present_reference = {.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_reference   = {.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkAttachmentReference shadow_reference_0 = {.attachment = 2, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkAttachmentReference shadow_reference_1 = {.attachment = 2, .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    u32 subpass_count = 2;
    VkSubpassDescription subpass_descriptions[2] = {};
    subpass_descriptions[0].pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_descriptions[0].pDepthStencilAttachment = &shadow_reference_0;

    subpass_descriptions[1].inputAttachmentCount    = 1;
    subpass_descriptions[1].colorAttachmentCount    = 1;
    subpass_descriptions[1].pInputAttachments       = &shadow_reference_1;
    subpass_descriptions[1].pColorAttachments       = &present_reference;
    subpass_descriptions[1].pDepthStencilAttachment = &depth_reference;

    u32 dependency_count = 3;
    VkSubpassDependency dependencies[3] = {};

    //
    // @Note I am not sure if these dependencies are correct. I used Willems deferred render as an example,
    // but his setup is for something a bit different. I will see what the validation layers say...
    //

    // Ensure the shadow depth attachment is available
	dependencies[0].srcSubpass      = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass      = 0;
	dependencies[0].srcStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].dstStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].srcAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	dependencies[0].dstAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Ensure the previous subpass has finished with the depth
	dependencies[1].srcSubpass      = 0;
	dependencies[1].dstSubpass      = 1;
	dependencies[1].srcStageMask    = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[1].dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].srcAccessMask   = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstAccessMask   = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    // Ensure we are ready to present
	dependencies[2].srcSubpass      = 1;
	dependencies[2].dstSubpass      = VK_SUBPASS_EXTERNAL;
	dependencies[2].srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[2].dstStageMask    = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	dependencies[2].srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[2].dstAccessMask   = VK_ACCESS_MEMORY_READ_BIT;
	dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo rp_info = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp_info.attachmentCount = attachment_count;
    rp_info.pAttachments    = attachment_descriptions;
    rp_info.subpassCount    = subpass_count;
    rp_info.pSubpasses      = subpass_descriptions;
    rp_info.dependencyCount = dependency_count;
    rp_info.pDependencies   = dependencies;

    VkDevice device = get_gpu_instance()->device;
    auto check = vkCreateRenderPass(device, &rp_info, ALLOCATION_CALLBACKS, renderpass);
    DEBUG_OBJ_CREATION(vkCreateRenderPass, check);

    VkImageView attachments[3] = {present_attachment, depth_attachment, shadow_attachment};

    VkViewport viewport = get_global_settings()->viewport;

    VkFramebufferCreateInfo fb_info = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb_info.renderPass      = *renderpass;
    fb_info.attachmentCount = 3;
    fb_info.pAttachments    = attachments;
    fb_info.width           = viewport.width;
    fb_info.height          = viewport.height;
    fb_info.layers          = 1;

    check = vkCreateFramebuffer(device, &fb_info, ALLOCATION_CALLBACKS, framebuffer);
    DEBUG_OBJ_CREATION(vkCreateFramebuffer, check);
}

#ifdef _WIN32 // Different drivers for different OSs, so I assume cache impl would be different
#define PL_CACHE_FILE_NAME "pl-caches/window.bin"
#else
#define PL_CACHE_FILE_NAME "pl-caches/ubuntu.bin"
#endif

// Pipeline Cache
VkPipelineCache pl_load_cache() {
    u64 size;
    const u8 *cache_data = file_read_bin_temp(PL_CACHE_FILE_NAME, &size);

    if (size) {
        VkPipelineCacheHeaderVersionOne *header = (VkPipelineCacheHeaderVersionOne*)cache_data;

        assert(header->headerVersion == 1 && "Failed to read pipeline cache header file");
        if (header->headerVersion != 1)
            return NULL;

        VkPhysicalDeviceProperties props = get_gpu_instance()->info.props;

        if (header->vendorID != props.vendorID) {
            println("Pipeline Cache vendor id does not match");
            return NULL;
        }

        if (header->deviceID != props.deviceID) {
            println("Pipeline Cache device id does not match");
            return NULL;
        }

        int result = memcmp(header->pipelineCacheUUID, props.pipelineCacheUUID, VK_UUID_SIZE);
        if (result != 0) {
            println("Pipeline Cache UUID does not match");
            return NULL;
        }
    }

    //
    // @Todo Consider setting externally syncd flag.
    //
    VkPipelineCacheCreateInfo create_info = {VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    create_info.initialDataSize = size;
    create_info.pInitialData    = cache_data;

    VkDevice device = get_gpu_instance()->device;

    VkPipelineCache pl_cache;
    auto check = vkCreatePipelineCache(device, &create_info, ALLOCATION_CALLBACKS, &pl_cache);
    DEBUG_OBJ_CREATION(vkCreatePipelineCache, check);

    // Store pipeline cache in global gpu
    get_gpu_instance()->pl_cache = pl_cache;

    return pl_cache; // return it cos why not (because it is confusing cos its unused, but here is a comment telling you so, so dont fret)
}
void pl_store_cache() {
    Gpu *gpu                 = get_gpu_instance();
    VkDevice device          = gpu->device;
    VkPipelineCache pl_cache = gpu->pl_cache;

    u64 size;
    auto check = vkGetPipelineCacheData(device, pl_cache, &size, NULL);

    void *cache_data = (void*)malloc_t(size, 8);
    vkGetPipelineCacheData(device, pl_cache, &size, cache_data);

    file_write_bin(PL_CACHE_FILE_NAME, size, cache_data);

    vkDestroyPipelineCache(device, pl_cache, ALLOCATION_CALLBACKS);
}

constexpr u32 PL_SHADER_STAGE_BUFFER_SIZE = 4;

struct Pl_Shader_Stage_Buffer { // Idk exactly how many stages there can be, but 4 is fine for now.
    VkPipelineShaderStageCreateInfo stages[PL_SHADER_STAGE_BUFFER_SIZE];
};

// Begin graphics pipeline
static void pl_get_shader_stages(u32 count, Pl_Shader_Info *infos,  Pl_Shader_Stage_Buffer *stages) {
    assert(PL_SHADER_STAGE_BUFFER_SIZE >= count);

    for(u32 i = 0; i < count; ++i) {
        stages->stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stages->stages[i].stage  = infos[i].stage;
        stages->stages[i].module = infos[i].module;
        stages->stages[i].pName  = "main";
    }
}

void pl_get_stages_and_layout(
    u32                  count,
    u32                 *shader_indices,
    u32                  push_constant_count,
    VkPushConstantRange *push_constants,
    Pl_Layout           *layout)
{
    // @Speed Fetching gpu on every call might be bad, might want to pass it in as an argument. Idk what the
    // call site for this function will look like yet.
    Gpu *gpu        =  get_gpu_instance();
    VkDevice device =  gpu->device;

    Shader_Memory         *shader_info      = &gpu->shader_memory;
    VkDescriptorSetLayout *set_layouts      = shader_info->layouts;
    u64                   *set_layout_sizes = shader_info->layout_sizes;
    Shader                *shaders          = shader_info->shaders;

    *layout = {};

    layout->stage_count = count;
    layout->stages = (Pl_Shader_Info*)malloc_t(sizeof(Pl_Shader_Info) * count, 8);

    // Count layouts
    for(u32 i = 0; i < count; ++i)
        layout->set_count += shaders[i].layout_count;

    layout->set_layout_sizes =                   (u64*)malloc_t(sizeof(u64)                   * layout->set_count);
    layout->set_layouts      = (VkDescriptorSetLayout*)malloc_t(sizeof(VkDescriptorSetLayout) * layout->set_count);

    //
    // @Note I am pretty sure that the order of the set layouts in the pipeline layout does not need to be the
    // same as their set numbers, that is only for bind order.
    //

    #if DEBUG
    bool zero_index = false;
    u32 last_set_index = 0;
    #endif

    u32 tmp_set_count = 0;
    Shader shader;
    const char *shader_entry_point = shader_info->entry_point;
    for(u32 i = 0; i < count; ++i) {
        shader = shaders[shader_indices[i]];

        //
        // @Note Shaders must be submitted to this function with increasing seqeuential set order. This means
        // that they do not need to be sorted in order to be bound, but instead can just be memcpyd into the
        // 'to bind' descriptor set array. This is perfectly reasonable: it would be very error prone to allow
        // programming shaders without strict rules on set sequencing.
        //
        #if DEBUG
        for(u32 j = 0; j < shader.layout_count; ++j) {
            if (zero_index == false) {
                assert(shader.layout_indices[j] == 0);
                zero_index = true;
            }

            assert(shader.layout_indices[j] == last_set_index     ||
                   shader.layout_indices[j] == last_set_index + 1 &&
                  "Shaders MUST appear with sequential set layout indices starting from 0");

           last_set_index = shader.layout_indices[j];
        }
        #endif

        layout->stages[i].stage  = shader.stage;
        layout->stages[i].module = shader.module;

        memcpy(layout->set_layouts + tmp_set_count, set_layouts + shader.layout_index,
               sizeof(VkDescriptorSetLayout) * shader.layout_count);
        memcpy(layout->set_layout_sizes + tmp_set_count, set_layout_sizes + shader.layout_index,
               sizeof(u64) * shader.layout_count);

        tmp_set_count += shader.layout_count;
    }

    VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount         = layout->set_count;
    layout_info.pSetLayouts            = layout->set_layouts;
    layout_info.pushConstantRangeCount = push_constant_count;
    layout_info.pPushConstantRanges    = push_constants;

    auto check = vkCreatePipelineLayout(device, &layout_info, ALLOCATION_CALLBACKS, &layout->pl_layout);
    DEBUG_OBJ_CREATION(vkCreatePipelineLayout, check);

    if (check != VK_SUCCESS) {
        *layout = {};
        return;
    }
}

static void pl_get_vertex_input_and_assembly(
    const Pl_Primitive_Info                *info,
    VkPipelineVertexInputStateCreateInfo   *ret_input_info,
    VkPipelineInputAssemblyStateCreateInfo *ret_assembly_info)
{
    VkVertexInputBindingDescription *bindings =
        (VkVertexInputBindingDescription*)malloc_t(sizeof(VkVertexInputBindingDescription) * info->count, 8);
    VkVertexInputAttributeDescription *attributes =
        (VkVertexInputAttributeDescription*)malloc_t(sizeof(VkVertexInputAttributeDescription) * info->count, 8);

    // @Note Offsets are set at bind time, as the allocation may not be uploaded yet.
    // Idk if this is less efficient than enforcing pipeline creation after allocation upload,
    // But that seems much worse, as then the upload cannot be part of the draw command.
    for(u32 i = 0; i < info->count; ++i) {
        bindings[i].binding = i;
        bindings[i].stride  = info->strides[i];

        attributes[i].format   = info->formats[i];
        attributes[i].location = i;
        attributes[i].binding  = i;
    }

   *ret_input_info = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    ret_input_info->vertexBindingDescriptionCount   = info->count;
    ret_input_info->pVertexBindingDescriptions      = bindings;
    ret_input_info->vertexAttributeDescriptionCount = info->count;
    ret_input_info->pVertexAttributeDescriptions    = attributes;

   *ret_assembly_info = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ret_assembly_info->topology = info->topology;
}

static void pl_get_viewport_and_scissor(VkPipelineViewportStateCreateInfo *ret_info) {
    Settings *settings = get_global_settings();
   *ret_info = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    ret_info->viewportCount = 1;
    ret_info->pViewports    = &settings->viewport;
    ret_info->scissorCount  = 1;
    ret_info->pScissors     = &settings->scissor;
}

static void pl_get_rasterization(Pl_Config_Flags flags, VkPipelineRasterizationStateCreateInfo *ret_info) {
   *ret_info = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    ret_info->lineWidth = 1.0f;

    if (flags & PL_CONFIG_WIRE_FRAME_BIT)
        ret_info->polygonMode = VK_POLYGON_MODE_LINE;
    else
        ret_info->polygonMode = VK_POLYGON_MODE_FILL;

    ret_info->frontFace = (VkFrontFace)(flags & (Pl_Config_Flags)PL_CONFIG_CLOCKWISE_FRONT_FACE_BIT);

    //
    // @Note The below is not robust to changes to Pl_Config_Flag_Bits, hence asserts
    //
    ret_info->cullMode = (flags & (Pl_Config_Flags)PL_CONFIG_CULL_FRONT_BIT) |
                         (flags & (Pl_Config_Flags)PL_CONFIG_CULL_BACK_BIT);

    assert((int)PL_CONFIG_CULL_FRONT_BIT == (int)VK_CULL_MODE_FRONT_BIT);
    assert((int)PL_CONFIG_CULL_BACK_BIT  == (int)VK_CULL_MODE_BACK_BIT);
}

static void pl_get_multisample(VkPipelineMultisampleStateCreateInfo *ret_info) {
    *ret_info = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ret_info->rasterizationSamples = get_global_settings()->sample_count;
}

static void pl_get_depth_stencil(
    Pl_Config_Flags                        flags,
    Pl_Stencil_Ops                        *stencil_ops,
    VkPipelineDepthStencilStateCreateInfo *ret_info)
{
    *ret_info = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

    // Clear all flags except those related to depth stencil settings
    flags &= PL_CONFIG_DEPTH_STENCIL_BITS;

    ret_info->depthTestEnable   = flags & PL_CONFIG_DEPTH_TEST_ENABLE_BIT;
    ret_info->depthWriteEnable  = flags & PL_CONFIG_DEPTH_WRITE_ENABLE_BIT;
    ret_info->stencilTestEnable = flags & PL_CONFIG_STENCIL_TEST_ENABLE_BIT;

    if (flags & PL_CONFIG_STENCIL_TEST_ENABLE_BIT) {
        ret_info->front = stencil_ops->front;
        ret_info->back  = stencil_ops->back;
    }

    // Clear all flags except those related to compare operations
    flags &= ~(PL_CONFIG_DEPTH_TEST_ENABLE_BIT | PL_CONFIG_DEPTH_WRITE_ENABLE_BIT | PL_CONFIG_STENCIL_TEST_ENABLE_BIT);

    // @Todo Make this branchless
    switch(flags) {
    case PL_CONFIG_DEPTH_COMPARE_EQUAL_BIT:
    {
         ret_info->depthCompareOp = VK_COMPARE_OP_EQUAL;
         break;
    }
    case PL_CONFIG_DEPTH_COMPARE_LESS_BIT:
    {
         ret_info->depthCompareOp = VK_COMPARE_OP_LESS;
         break;
    }
    case PL_CONFIG_DEPTH_COMPARE_GREATER_BIT:
    {
        ret_info->depthCompareOp = VK_COMPARE_OP_GREATER;
        break;
    }
    case PL_CONFIG_DEPTH_COMPARE_LESS_BIT | PL_CONFIG_DEPTH_COMPARE_EQUAL_BIT:
    {
        ret_info->depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        break;
    }
    case PL_CONFIG_DEPTH_COMPARE_GREATER_BIT | PL_CONFIG_DEPTH_COMPARE_EQUAL_BIT:
    {
        ret_info->depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
        break;
    }
    case PL_CONFIG_DEPTH_COMPARE_GREATER_BIT | PL_CONFIG_DEPTH_COMPARE_LESS_BIT:
    {
        ret_info->depthCompareOp = VK_COMPARE_OP_NOT_EQUAL;
        break;
    }
    case PL_CONFIG_DEPTH_COMPARE_GREATER_BIT | PL_CONFIG_DEPTH_COMPARE_LESS_BIT | PL_CONFIG_DEPTH_COMPARE_EQUAL_BIT:
    {
        ret_info->depthCompareOp = VK_COMPARE_OP_ALWAYS;
        break;
    }
    default:
        break;
    }
}

static void pl_get_color_blend(
    u32                                  count,
    Pl_Blend_Info                       *blends,
    VkPipelineColorBlendStateCreateInfo *ret_info)
{
   *ret_info = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    ret_info->attachmentCount = count;
    ret_info->pAttachments    = blends;
}

static void pl_get_dynamic(VkPipelineDynamicStateCreateInfo *ret_info) {
    Settings *settings = get_global_settings();
   *ret_info = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ret_info->dynamicStateCount = settings->pl_dynamic_state_count;
    ret_info->pDynamicStates    = settings->pl_dynamic_states;
}

void pl_create_pipelines(
    u32                      count,
    const Pl_Primitive_Info *primitive_infos,
    const Pl_Config         *configs,
    VkPipeline              *ret_pipelines)
{
    //
    // Could collapse all of the below into a single allocation, and then fill that, but I doubt that
    // there is any difference.
    //

    Pl_Shader_Stage_Buffer *shader_stages =
        (Pl_Shader_Stage_Buffer*)malloc_t(sizeof(Pl_Shader_Stage_Buffer) * count);

    VkPipelineVertexInputStateCreateInfo *vertex_input_states =
        (VkPipelineVertexInputStateCreateInfo*)malloc_t(sizeof(VkPipelineVertexInputStateCreateInfo) * count);

    VkPipelineInputAssemblyStateCreateInfo *input_assembly_states =
        (VkPipelineInputAssemblyStateCreateInfo*)malloc_t(sizeof(VkPipelineInputAssemblyStateCreateInfo) * count);

    VkPipelineRasterizationStateCreateInfo *rasterization_states =
        (VkPipelineRasterizationStateCreateInfo*)malloc_t(sizeof(VkPipelineRasterizationStateCreateInfo) * count);

    VkPipelineDepthStencilStateCreateInfo *depth_stencil_states =
        (VkPipelineDepthStencilStateCreateInfo*)malloc_t(sizeof(VkPipelineDepthStencilStateCreateInfo) * count);

    VkPipelineColorBlendStateCreateInfo *color_blend_states =
        (VkPipelineColorBlendStateCreateInfo*)malloc_t(sizeof(VkPipelineColorBlendStateCreateInfo) * count);

    VkPipelineViewportStateCreateInfo viewport_state;
    pl_get_viewport_and_scissor(&viewport_state);

    // @Todo Incoporate multisampling.
    VkPipelineMultisampleStateCreateInfo multisample_state;
    pl_get_multisample(&multisample_state);

    VkPipelineDynamicStateCreateInfo dyn_state;
    pl_get_dynamic(&dyn_state); // Takes its value from global settings struct (top of gpu.hpp)

    VkGraphicsPipelineCreateInfo *create_infos =
        (VkGraphicsPipelineCreateInfo*)malloc_t(sizeof(VkGraphicsPipelineCreateInfo) * count);

    for(u32 i = 0; i < count; ++i) {
        create_infos[i] = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        create_infos[i].flags             = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        create_infos[i].pViewportState    = &viewport_state;
        create_infos[i].pMultisampleState = &multisample_state;
        create_infos[i].pDynamicState     = &dyn_state;

        create_infos[i].renderPass = configs[i].renderpass;
        create_infos[i].layout     = configs[i].layout;
        create_infos[i].subpass    = configs[i].subpass;

        pl_get_shader_stages(configs[i].shader_count, configs[i].shader_infos, shader_stages);

        create_infos[i].stageCount = configs[i].shader_count;
        create_infos[i].pStages    = shader_stages[i].stages;

        pl_get_vertex_input_and_assembly(&primitive_infos[i], &vertex_input_states[i],
                                         &input_assembly_states[i]);

        create_infos[i].pVertexInputState   = &vertex_input_states[i];
        create_infos[i].pInputAssemblyState = &input_assembly_states[i];

        pl_get_rasterization(configs[i].flags, &rasterization_states[i]);
        create_infos[i].pRasterizationState = &rasterization_states[i];

        pl_get_depth_stencil(configs[i].flags, configs[i].stencil_ops, &depth_stencil_states[i]);
        create_infos[i].pDepthStencilState = &depth_stencil_states[i];

        pl_get_color_blend(configs[i].blend_count, configs[i].blend_infos, &color_blend_states[i]);
        create_infos[i].pColorBlendState = &color_blend_states[i];
    }

    Gpu *gpu              = get_gpu_instance();
    VkDevice device       = gpu->device;
    VkPipelineCache cache = gpu->pl_cache; // @Todo Check if this is thread safe.

    auto check = vkCreateGraphicsPipelines(device, cache, count, create_infos, ALLOCATION_CALLBACKS, ret_pipelines);
    DEBUG_OBJ_CREATION(vkCreateGraphicsPipelines, check);
}

/*
   Below are the big lumpy functions that sometimes arise that I never want to
               see and hardly have to use
*/

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
                assert(false && "Invalid Accessor Format");
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
#endif

//
// Below is unimplemented code/old code that I want to keep around for examples later if I need it
// (this code often does too much/is too general for what I currently want, so it is not useful for now,
// but it may be useful later when I want to do different things).
//

#if 0
// Pipeline creation minor helpers
inline static u32 pl_count_primitives(Static_Model *model) {
    u32 primitive_count = 0;
    for(u32 i = 0; i < model->mesh_count; ++i)
        primitive_count += model->meshes[i].count;
    return primitive_count;
}
inline static void pl_loop_model_primitives(Static_Model *model, VkPipelineVertexInputStateCreateInfo *ret_input_info, VkPipelineInputAssemblyStateCreateInfo *ret_assembly_info, u32 *count) {
    u32 idx = *count;
    for(u32 i = 0; i < model->mesh_count; ++i)
        for(u32 j = 0; j < model->meshes[i].count; ++j) {
            pl_get_vertex_input_and_assembly_static(&model->meshes[i].pl_infos[j], &ret_input_info[idx], &ret_assembly_info[idx]);
            idx++;
        }
    *count = idx;
}
inline static void pl_loop_model_primitives_shadow(Static_Model *model, VkPipelineVertexInputStateCreateInfo *ret_input_info, VkPipelineInputAssemblyStateCreateInfo *ret_assembly_info, u32 *count) {
    u32 idx = *count;
    for(u32 i = 0; i < model->mesh_count; ++i)
        for(u32 j = 0; j < model->meshes[i].count; ++j) {
            pl_get_vertex_input_and_assembly_static_shadow(&model->meshes[i].pl_infos[j], &ret_input_info[idx], &ret_assembly_info[idx]);
            idx++;
        }
    *count = idx;
}

//
// @Todo @Note I should a 'pl_set_default()' or something like that to set all the things I will basically ever
// want, and that can return a create info. And then I can call functions to just update the little things I want.
// although to be fair that is basically what the below 'create_basic()' is doing...
//

// Alpha blend, single sample, basic shaders
Pl_Final pl_create_basic(VkRenderPass renderpass, u32 count, Static_Model *models) {
    u32 primitive_count = 0;
    for(u32 i = 0; i < count; ++i)
        primitive_count += pl_count_primitives(&models[i]);

    //
    // @Note I can now see the usefulness of a cache and pipeline libraries. A LOT of these pipelines are almost
    // identical...
    //

    Pl_Final ret = {};
    ret.count     = primitive_count;
    ret.pipelines = (VkPipeline*)malloc_t(sizeof(VkPipeline) * primitive_count, 8);

    u32 shaders[2] = {(u32)SHADER_ID_BASIC_VERT, (u32)SHADER_ID_BASIC_FRAG};
    pl_get_stages_and_layout(2, shaders, 0, NULL, &ret.layout);

    VkPipelineVertexInputStateCreateInfo   *input_states    =   (VkPipelineVertexInputStateCreateInfo*)malloc_t(sizeof(VkPipelineVertexInputStateCreateInfo) * primitive_count, 8);
    VkPipelineInputAssemblyStateCreateInfo *assembly_states = (VkPipelineInputAssemblyStateCreateInfo*)malloc_t(sizeof(VkPipelineInputAssemblyStateCreateInfo) * primitive_count, 8);

    // Loop models, loop meshes, loop primitives.
    u32 tmp = 0;
    for(u32 i = 0; i < count; ++i)
        pl_loop_model_primitives(&models[i], input_states + tmp, assembly_states + tmp, &tmp);

    VkPipelineViewportStateCreateInfo viewport_state;
    pl_get_viewport_and_scissor(&viewport_state);

    VkPipelineRasterizationStateCreateInfo rasterization_state;
    pl_get_rasterization({false, false, true, false}, &rasterization_state);

    VkPipelineMultisampleStateCreateInfo multisample_state;
    pl_get_multisample(&multisample_state);

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
    pl_get_depth_stencil({true, true, false, VK_COMPARE_OP_LESS}, &depth_stencil_state);

    VkPipelineColorBlendAttachmentState blend_attachment;
    pl_attachment_get_alpha_blend(&blend_attachment);
    VkPipelineColorBlendStateCreateInfo blend_state;
    pl_get_color_blend(1, &blend_attachment, &blend_state);

    VkPipelineDynamicStateCreateInfo dyn_state;
    pl_get_dynamic(&dyn_state);

    //
    // @Note I feel like here I can enable rasterizer discard?
    //

    VkGraphicsPipelineCreateInfo *pl_infos = (VkGraphicsPipelineCreateInfo*)malloc_t(sizeof(VkGraphicsPipelineCreateInfo) * primitive_count, 8);

    for(u32 i = 0; i < primitive_count; ++i) {
        pl_infos[i] = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pl_infos[i].stageCount          = 2;
        pl_infos[i].pStages             = ret.layout.stages;
        pl_infos[i].pVertexInputState   = &input_states[i];
        pl_infos[i].pInputAssemblyState = &assembly_states[i];
        pl_infos[i].pViewportState      = &viewport_state;
        pl_infos[i].pRasterizationState = &rasterization_state;
        pl_infos[i].pMultisampleState   = &multisample_state;
        pl_infos[i].pDepthStencilState  = &depth_stencil_state;
        pl_infos[i].pColorBlendState    = &blend_state;
        pl_infos[i].pDynamicState       = &dyn_state;
        pl_infos[i].layout              = ret.layout.pl_layout;
        pl_infos[i].renderPass          = renderpass;
        pl_infos[i].subpass             = 1; // @Note After shadow pass
    }

    Gpu *gpu                 = get_gpu_instance();
    VkDevice device          = gpu->device;
    VkPipelineCache pl_cache = gpu->pl_cache;

    auto check = vkCreateGraphicsPipelines(
                    device,
                    pl_cache,
                    primitive_count,
                    pl_infos,
                    ALLOCATION_CALLBACKS,
                    ret.pipelines);
    DEBUG_OBJ_CREATION(vkCreateGraphicsPipelines, check);

    return ret;
}
Pl_Final pl_create_shadow(VkRenderPass renderpass, u32 count, Static_Model *models) {
    u32 primitive_count = 0;
    for(u32 i = 0; i < count; ++i)
        primitive_count += pl_count_primitives(&models[i]);

    //
    // @Note I can now see the usefulness of a cache and pipeline libraries. A LOT of these pipelines are almost
    // identical...
    //

    Pl_Final ret = {};
    ret.count     = primitive_count;
    ret.pipelines = (VkPipeline*)malloc_t(sizeof(VkPipeline) * primitive_count, 8);

    u32 shaders[2] = {(u32)SHADER_ID_SHADOW_VERT, (u32)SHADER_ID_SHADOW_FRAG};
    pl_get_stages_and_layout(2, shaders, 0, NULL, &ret.layout);

    VkPipelineVertexInputStateCreateInfo   *input_states    =   (VkPipelineVertexInputStateCreateInfo*)malloc_t(sizeof(VkPipelineVertexInputStateCreateInfo) * primitive_count, 8);
    VkPipelineInputAssemblyStateCreateInfo *assembly_states = (VkPipelineInputAssemblyStateCreateInfo*)malloc_t(sizeof(VkPipelineInputAssemblyStateCreateInfo) * primitive_count, 8);

    // Loop models, loop meshes, loop primitives.
    u32 tmp = 0;
    for(u32 i = 0; i < count; ++i)
        pl_loop_model_primitives_shadow(&models[i], input_states + tmp, assembly_states + tmp, &tmp);

    VkPipelineViewportStateCreateInfo viewport_state;
    pl_get_viewport_and_scissor(&viewport_state);

    VkPipelineRasterizationStateCreateInfo rasterization_state;
    pl_get_rasterization({false, true, false, false}, &rasterization_state);

    VkPipelineMultisampleStateCreateInfo multisample_state;
    pl_get_multisample(&multisample_state);

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state;
    pl_get_depth_stencil({true, true, false, VK_COMPARE_OP_LESS}, &depth_stencil_state);

    VkPipelineColorBlendStateCreateInfo blend_state;
    pl_get_color_blend(0, NULL, &blend_state);

    VkPipelineDynamicStateCreateInfo dyn_state;
    pl_get_dynamic(&dyn_state);

    //
    // @Note I feel like here I can enable rasterizer discard?
    //

    VkGraphicsPipelineCreateInfo *pl_infos = (VkGraphicsPipelineCreateInfo*)malloc_t(sizeof(VkGraphicsPipelineCreateInfo) * primitive_count, 8);

    for(u32 i = 0; i < primitive_count; ++i) {
        pl_infos[i] = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pl_infos[i].stageCount          = 2;
        pl_infos[i].pStages             = ret.layout.stages;
        pl_infos[i].pVertexInputState   = &input_states[i];
        pl_infos[i].pInputAssemblyState = &assembly_states[i];
        pl_infos[i].pViewportState      = &viewport_state;
        pl_infos[i].pRasterizationState = &rasterization_state;
        pl_infos[i].pMultisampleState   = &multisample_state;
        pl_infos[i].pDepthStencilState  = &depth_stencil_state;
        pl_infos[i].pColorBlendState    = &blend_state;
        pl_infos[i].pDynamicState       = &dyn_state;
        pl_infos[i].layout              = ret.layout.pl_layout;
        pl_infos[i].renderPass          = renderpass;
        pl_infos[i].subpass             = 0; // @Note Shadow pass must come first
    }

    Gpu *gpu                 = get_gpu_instance();
    VkDevice device          = gpu->device;
    VkPipelineCache pl_cache = gpu->pl_cache;

    auto check = vkCreateGraphicsPipelines(
                    device,
                    pl_cache,
                    primitive_count,
                    pl_infos,
                    ALLOCATION_CALLBACKS,
                    ret.pipelines);
    DEBUG_OBJ_CREATION(vkCreateGraphicsPipelines, check);

    return ret;
}

Draw_Final_Basic draw_create_basic(Draw_Final_Basic_Config *config) {
    VkRenderPass renderpass;
    VkFramebuffer framebuffer;
    rp_forward_shadow_basic(&config->rp_config, &renderpass, &framebuffer);

    Pl_Final pl_shadow = pl_create_shadow(renderpass, config->count, config->models);
    Pl_Final pl_basic  = pl_create_basic(renderpass, config->count, config->models);

    Draw_Final_Basic ret;
    ret.pl_basic    = pl_basic;
    ret.pl_shadow   = pl_shadow;
    ret.renderpass  = renderpass;
    ret.framebuffer = framebuffer;

    return ret;
}
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
    u32 view_count     = gltf_buffer_view_get_count(&gltf);
    u32 node_count     = gltf_node_get_count(&gltf);
    u32 mat_count      = gltf_material_get_count(&gltf);
    u32 mesh_count     = gltf_mesh_get_count(&gltf);
    u32 prim_count     = gltf.total_primitive_count;

    Static_Model ret = {};

    ret.node_count = node_count;
    ret.mesh_count = mesh_count;
    ret.mat_count  = mat_count;

    //ret.nodes = (Node*)malloc_h(sizeof(Node) * node_count, 8); @Unused
    ret.meshes = (Mesh*)malloc_h(sizeof(Mesh) * mesh_count, 8);
    ret.mats   = (Material*)malloc_h(sizeof(Material) * mat_count, 8);

    ret.meshes[0].primitives = (Primitive*)malloc_h(sizeof(Primitive) * prim_count, 8);
    ret.meshes[0].pl_infos   = (Pl_Primitive_Info*)malloc_h(sizeof(Pl_Primitive_Info) * prim_count, 8);

    // Allow summing offset
    memset(ret.meshes[0].primitives, 0, sizeof(Primitive) * prim_count);
    memset(ret.meshes[0].pl_infos, 0, sizeof(Pl_Primitive_Info) * prim_count);

    /*
        // @Todo properly document this function...
        Vertex Attribute Load method:
            1. Loop primitives   - mark data
            2. Loop buffer views - load data
            3. Loop primitives   - set offsets
    */

    Buffer_View *views = (Buffer_View*)malloc_t(sizeof(Buffer_View) * view_count, 8);
    memset(views, 0, sizeof(Buffer_View) * view_count); // ensure type is not wrongly matched
    u32 *view_indices = (u32*)malloc_t(sizeof(u32) * accessor_count, 8); // no more deref accessors

    Primitive *prim;
    Pl_Primitive_Info *pl_info;

    // 1. Loop primitives - mark data
    gltf_mesh = gltf.meshes;
    u32 prim_track = 0;
    for(u32 i = 0; i < mesh_count; ++i) {
        gltf_prim = gltf_mesh->primitives;

        ret.meshes[i].count      = gltf_mesh->primitive_count;
        ret.meshes[i].primitives = ret.meshes[0].primitives + prim_track;
        ret.meshes[i].pl_infos   = ret.meshes[0].pl_infos + prim_track;

        prim_track += ret.meshes[i].count;
        for(u32 j = 0; j < gltf_mesh->primitive_count; ++j) {
            prim    = &ret.meshes[i].primitives[j];
            pl_info = &ret.meshes[i].pl_infos[j];

            pl_info->topology = (VkPrimitiveTopology)gltf_prim->topology;

            // Set Index
            accessor = gltf_accessor_by_index(&gltf, gltf_prim->indices);

            view_indices[gltf_prim->indices]  = accessor->buffer_view;
            views[accessor->buffer_view].type = Data_Type::INDEX;

            prim->offset_index = accessor->byte_offset;
            prim->count        = accessor->count;
            prim->material     = &ret.mats[gltf_prim->material];

            switch(accessor->format) {
            case GLTF_ACCESSOR_FORMAT_SCALAR_U16:
            {
                prim->index_type = VK_INDEX_TYPE_UINT16;
                break;
            }
            case GLTF_ACCESSOR_FORMAT_SCALAR_U32:
            {
                prim->index_type = VK_INDEX_TYPE_UINT32;
                break;
            }
            default:
                assert(false && "Invalid Index Type");
            }

            if (gltf_prim->position != -1) {
                accessor = gltf_accessor_by_index(&gltf, gltf_prim->position);

                prim->offset_position = accessor->byte_offset;

                pl_info->stride_position = get_accessor_byte_stride(accessor->format);
                pl_info->fmt_position    = (VkFormat)accessor->format;

                view_indices[gltf_prim->position] = accessor->buffer_view;
                views[accessor->buffer_view].type = Data_Type::VERTEX;
            }
            if (gltf_prim->normal != -1) {
                accessor = gltf_accessor_by_index(&gltf, gltf_prim->normal);

                prim->offset_normal = accessor->byte_offset;

                pl_info->stride_normal = get_accessor_byte_stride(accessor->format);
                pl_info->fmt_normal    = (VkFormat)accessor->format;

                view_indices[gltf_prim->normal]   = accessor->buffer_view;
                views[accessor->buffer_view].type = Data_Type::VERTEX;
            }
            if (gltf_prim->tangent != -1) {
                accessor = gltf_accessor_by_index(&gltf, gltf_prim->tangent);

                prim->offset_tangent = accessor->byte_offset;

                pl_info->stride_tangent = get_accessor_byte_stride(accessor->format);
                pl_info->fmt_tangent    = (VkFormat)accessor->format;

                view_indices[gltf_prim->tangent]  = accessor->buffer_view;
                views[accessor->buffer_view].type = Data_Type::VERTEX;
            }
            if (gltf_prim->tex_coord_0 != -1) {
                accessor = gltf_accessor_by_index(&gltf, gltf_prim->tex_coord_0);

                prim->offset_tex_coords = accessor->byte_offset;

                pl_info->stride_tex_coords = get_accessor_byte_stride(accessor->format);
                pl_info->fmt_tex_coords    = (VkFormat)accessor->format;

                view_indices[gltf_prim->tex_coord_0] = accessor->buffer_view;
                views[accessor->buffer_view].type    = Data_Type::VERTEX;
            }

            gltf_prim = (Gltf_Mesh_Primitive*)((u8*)gltf_prim + gltf_prim->stride);
        }
        gltf_mesh = (Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }

    // 2. Loop buffer views - load data
    assert(gltf_buffer_get_count(&gltf) == 1 && "Too Many Buffers");
    Gltf_Buffer *gltf_buf = gltf.buffers;

    memcpy(uri_buffer, dir->str, dir->len);
    strcpy(&uri_buffer[0] + dir->len, gltf_buf->uri);

    u8 *buf = (u8*)file_read_bin_temp_large(uri_buffer, gltf_buf->byte_length);

    Gltf_Buffer_View *gltf_view = gltf.buffer_views;
    void *ptr;
    Gpu_Allocator_Result allocation_result;

    // These should never fail. If they do, adjust memory layout.
    allocation_result = begin_allocation(&allocs->vertex);
    assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
    if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
        return {};

    allocation_result = begin_allocation(&allocs->index);
    assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
    if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
        return {};

    u64 vertex_allocation_size = 0;
    u64 index_allocation_size  = 0;
    for(u32 i = 0; i < view_count; ++i) {
        // This switch seems lame, but in reality gltf views are likely packed by type, so it should
        // be well predicted.
        switch(views[i].type) {
        case Data_Type::VERTEX:
        {
            allocation_result = continue_allocation(&allocs->vertex, gltf_view->byte_length,
                                                    buf + gltf_view->byte_offset);

            assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
            if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
                return {};

            views[i].offset         = vertex_allocation_size;
            vertex_allocation_size += gltf_view->byte_length;

            break;
        }
        case Data_Type::INDEX:
        {
            allocation_result = continue_allocation(&allocs->index, gltf_view->byte_length,
                                                    buf + gltf_view->byte_offset);

            assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
            if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
                return {};

            views[i].offset         = index_allocation_size;
            index_allocation_size  += gltf_view->byte_length;

            break;
        }
        case Data_Type::NONE:
        {
            break;
        }
        case Data_Type::UNIFORM:
            assert(false && "No Uniform Data Allowed In Static Model");
            break;
        default:
            assert(false && "Invalid Buffer View Type");
        }

        gltf_view = (Gltf_Buffer_View*)((u8*)gltf_view + gltf_view->stride);
    }

    // Submit index allocation
    allocation_result = submit_allocation(&allocs->index, &ret.index_allocation_key);
    assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
    if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
        return {};

    // Submit vertex allocation
    allocation_result = submit_allocation(&allocs->vertex, &ret.vertex_allocation_key);
    assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
    if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
        return {};

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
                ret.meshes[i].primitives[j].offset_position +=
                    views[view_indices[gltf_prim->position]].offset;

            if (gltf_prim->normal != -1)
                ret.meshes[i].primitives[j].offset_normal +=
                    views[view_indices[gltf_prim->normal]].offset;

            if (gltf_prim->tangent != -1)
                ret.meshes[i].primitives[j].offset_tangent +=
                    views[view_indices[gltf_prim->tangent]].offset;

            if (gltf_prim->tex_coord_0 != -1)
                ret.meshes[i].primitives[j].offset_tex_coords +=
                    views[view_indices[gltf_prim->tex_coord_0]].offset;

            gltf_prim = (Gltf_Mesh_Primitive*)((u8*)gltf_prim + gltf_prim->stride);
        }

        gltf_mesh = (Gltf_Mesh*)((u8*)gltf_mesh + gltf_mesh->stride);
    }

    // Load Material Data
    Sampler_Info  sampler_info;
    Gltf_Material *gltf_mat = gltf.materials;
    Gltf_Texture  *gltf_tex;
    Gltf_Sampler  *gltf_sampler;
    Gltf_Image    *gltf_image;
    for(u32 i = 0; i < mat_count; ++i) {

        ret.mats[i].base_factors[0] = gltf_mat->base_color_factor[0];
        ret.mats[i].base_factors[1] = gltf_mat->base_color_factor[1];
        ret.mats[i].base_factors[2] = gltf_mat->base_color_factor[2];
        ret.mats[i].base_factors[3] = gltf_mat->base_color_factor[3];

        ret.mats[i].emissive_factors[0] = gltf_mat->emissive_factor[0];
        ret.mats[i].emissive_factors[1] = gltf_mat->emissive_factor[1];
        ret.mats[i].emissive_factors[2] = gltf_mat->emissive_factor[2];

        ret.mats[i].metal_factor       = gltf_mat->metallic_factor;
        ret.mats[i].rough_factor       = gltf_mat->roughness_factor;
        ret.mats[i].norm_scale         = gltf_mat->normal_scale;
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

        //
        // @Todo Think about converting some of the below stuff to functions. Maybe this would be easier
        // to understand, but then again maybe not.
        //

        // base
        if (gltf_mat->base_color_texture_index != -1) {
            gltf_tex     = gltf_texture_by_index(&gltf, gltf_mat->base_color_texture_index);
            gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
            gltf_image   = gltf_image_by_index  (&gltf, gltf_tex->source_image);

            strcpy(&uri_buffer[0] + dir->len, gltf_image->uri); // @Todo update the gltf uris to use String type
            tmp_uri = cstr_to_string((const char*)uri_buffer);

            sampler_info.wrap_s     = (VkSamplerAddressMode)gltf_sampler->wrap_u;
            sampler_info.wrap_t     = (VkSamplerAddressMode)gltf_sampler->wrap_v;
            sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
            sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;

            ret.mats[i].tex_base.sampler_key = add_sampler(&allocs->sampler, &sampler_info);
            allocation_result = tex_add_texture(&allocs->tex, &tmp_uri, &ret.mats[i].tex_base.allocation_key);

            assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
            if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
                return {};
        }


        // metallic roughness
        if (gltf_mat->metallic_roughness_texture_index != -1) {
            gltf_tex     = gltf_texture_by_index(&gltf, gltf_mat->metallic_roughness_texture_index);
            gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
            gltf_image   = gltf_image_by_index  (&gltf, gltf_tex->source_image);

            strcpy(&uri_buffer[0] + dir->len, gltf_image->uri);
            tmp_uri = cstr_to_string((const char*)uri_buffer);

            sampler_info.wrap_s     = (VkSamplerAddressMode)gltf_sampler->wrap_u;
            sampler_info.wrap_t     = (VkSamplerAddressMode)gltf_sampler->wrap_v;
            sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
            sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;

            ret.mats[i].tex_pbr.sampler_key = add_sampler(&allocs->sampler, &sampler_info);
            allocation_result = tex_add_texture(&allocs->tex, &tmp_uri, &ret.mats[i].tex_pbr.allocation_key);

            assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
            if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
                return {};
        }


        // normal
        if (gltf_mat->normal_texture_index != -1) {
            gltf_tex     = gltf_texture_by_index(&gltf, gltf_mat->normal_texture_index);
            gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
            gltf_image   = gltf_image_by_index  (&gltf, gltf_tex->source_image);

            strcpy(&uri_buffer[0] + dir->len, gltf_image->uri);
            tmp_uri = cstr_to_string((const char*)uri_buffer);

            sampler_info.wrap_s     = (VkSamplerAddressMode)gltf_sampler->wrap_u;
            sampler_info.wrap_t     = (VkSamplerAddressMode)gltf_sampler->wrap_v;
            sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
            sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;

            ret.mats[i].tex_norm.sampler_key = add_sampler(&allocs->sampler, &sampler_info);
            allocation_result = tex_add_texture(&allocs->tex, &tmp_uri, &ret.mats[i].tex_norm.allocation_key);

            assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
            if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
                return {};
        }


        // occlusion
        if (gltf_mat->occlusion_texture_index != -1) {
            gltf_tex     = gltf_texture_by_index(&gltf, gltf_mat->occlusion_texture_index);
            gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
            gltf_image   = gltf_image_by_index  (&gltf, gltf_tex->source_image);

            strcpy(&uri_buffer[0] + dir->len, gltf_image->uri);
            tmp_uri = cstr_to_string((const char*)uri_buffer);

            sampler_info.wrap_s     = (VkSamplerAddressMode)gltf_sampler->wrap_u;
            sampler_info.wrap_t     = (VkSamplerAddressMode)gltf_sampler->wrap_v;
            sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
            sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;

            ret.mats[i].tex_occlusion.sampler_key = add_sampler(&allocs->sampler, &sampler_info);
            allocation_result = tex_add_texture(&allocs->tex, &tmp_uri, &ret.mats[i].tex_occlusion.allocation_key);

            assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
            if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
                return {};
        }


        // emissive
        if (gltf_mat->emissive_texture_index != -1) {
            gltf_tex     = gltf_texture_by_index(&gltf, gltf_mat->emissive_texture_index);
            gltf_sampler = gltf_sampler_by_index(&gltf, gltf_tex->sampler);
            gltf_image   = gltf_image_by_index(&gltf, gltf_tex->source_image);

            strcpy(&uri_buffer[0] + dir->len, gltf_image->uri);
            tmp_uri = cstr_to_string((const char*)uri_buffer);

            sampler_info.wrap_s     = (VkSamplerAddressMode)gltf_sampler->wrap_u;
            sampler_info.wrap_t     = (VkSamplerAddressMode)gltf_sampler->wrap_v;
            sampler_info.mag_filter = (VkFilter)gltf_sampler->mag_filter;
            sampler_info.min_filter = (VkFilter)gltf_sampler->min_filter;

            ret.mats[i].tex_emissive.sampler_key = add_sampler(&allocs->sampler, &sampler_info);
            allocation_result = tex_add_texture(&allocs->tex, &tmp_uri, &ret.mats[i].tex_emissive.allocation_key);

            assert(allocation_result == GPU_ALLOCATOR_RESULT_SUCCESS);
            if (allocation_result != GPU_ALLOCATOR_RESULT_SUCCESS)
                return {};
        }

        gltf_mat = (Gltf_Material*)((u8*)gltf_mat + gltf_mat->stride);
    }

    reset_to_mark_temp(mark);
    return ret;
}
#endif

#if 0 // I will leave this unimplemented for now. I am not so interested in deferred rendering for the minute.
//
// Deferred renderer with forward transparency
//
// @Note Gltf pbr metallic roughness textures only use two channels, while the occlusion textures only use one.
// So in future I should pack these together. Also, emissive only uses three, so occlusion could also go in there.
//
//
// Includes forward rendering subpasses for translucency and @Unimplemented shadow mapping
//
// @Note This implementation a little crazy, as I do not want lots of long name functions, but I also want
// branchless...
//
struct Rp_Config {
    VkImageView present;
    VkImageView color;
    VkImageView depth;
    VkImageView position;
    VkImageView normal;
    VKImageView pbr; // metallic roughness
    VkImageView occlusion;
    VkImageView emissive;
};
void rp_deferred(Rp_Config *config, VkRenderpass *renderpass, VkFramebuffer *framebuffer) {
    VkSampleCount sample_count = get_global_settings()->sample_count;

    VkAttachmentDescription description_gbuffer = {};
    description_gbuffer.format        = VK_FORMAT_R8G8B8A8_SRGB;
    description_gbuffer.samples       = sample_count;
    description_gbuffer.loadOp        = VK_LOAD_OP_CLEAR;
    description_gbuffer.storeOp       = VK_STORE_OP_DONT_CARE; // I think this is right, as I will not need it after the final subpass
    description_gbuffer.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    description_gbuffer.finalLayout   = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription description_depth = {};
    description_depth.format         = VK_FORMAT_R8G8B8A8_SRGB;
    description_depth.samples        = sample_count;
    description_depth.loadOp         = VK_LOAD_OP_CLEAR;
    description_depth.storeOp        = VK_STORE_OP_DONT_CARE;
    description_depth.stencilLoadOp  = VK_LOAD_OP_DONT_CARE;
    description_depth.stencilStoreOp = VK_STORE_OP_DONT_CARE;
    description_depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    description_depth.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentDescription description_present = {};
    description_present.format        = VK_FORMAT_R8G8B8A8_SRGB;
    description_present.samples       = sample_count;
    description_present.loadOp        = VK_LOAD_OP_CLEAR;
    description_present.storeOp       = VK_STORE_OP_STORE;
    description_present.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    description_present.finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription *descriptions[8] = {};
    u32 description_count = 0;

    VkAttachmentReference *references_subpass_0[8] = {};
    VkAttachmentReference *references_subpass_1[8] = {};
    VkAttachmentReference *references_subpass_2[8] = {};
    u32 reference_count_subpass_0 = 0;
    u32 reference_count_subpass_1 = 0;
    u32 reference_count_subpass_2 = 0;

    u32 present_index;
    u32 color_index;
    u32 depth_index;
    u32 position_index;
    u32 normal_index;
    u32 pbr_index;
    u32 occlusion_index;
    u32 emissive_index;

    u32 tmp;
    u32 subpass_color_attachment_counts[3] = {};
    u32 subpass_input_attachment_counts[3] = {};
    u32 subpass_depth_attachment_counts[3] = {};

    present_index = description_count;
    descriptions[description_count] = &description_present;
    tmp = (int)(config->present != NULL);

    description_count += tmp;
    subpass_color_attachment_counts[1] += tmp;
    subpass_color_attachment_counts[2] += tmp;

    color_index = description_count;
    descriptions[description_count] = &description_gbuffer;
    tmp = (int)(config->color != NULL);

    description_count += tmp;
    subpass_color_attachment_counts[0] += tmp;
    subpass_input_attachment_counts[1] += tmp;

    depth_index = description_count;
    descriptions[description_count] = &description_depth;
    tmp = (int)(config->depth != NULL);

    description_count += tmp;
    subpass_depth_attachment_counts[0] += tmp;
    subpass_depth_attachment_counts[2] += tmp;

    position_index = description_count;
    descriptions[description_count] = &description_gbuffer;
    tmp = (int)(config->position != NULL);

    description_count += tmp;
    subpass_color_attachment_counts[0] += tmp;
    subpass_input_attachment_counts[1] += tmp;

    normal_index = description_count;
    descriptions[description_count] = &description_gbuffer;
    tmp = (int)(config->normal != NULL);

    description_count += tmp;
    subpass_color_attachment_counts[0] += tmp;
    subpass_input_attachment_counts[1] += tmp;

    pbr_index = description_count;
    descriptions[description_count] = &description_gbuffer;
    tmp = (int)(config->pbr != NULL);

    description_count += tmp;
    subpass_color_attachment_counts[0] += tmp;
    subpass_input_attachment_counts[1] += tmp;

    occlusion_index = description_count;
    descriptions[description_count] = &description_gbuffer;
    tmp = (int)(config->occlusion != NULL);

    description_count += tmp;
    subpass_color_attachment_counts[0] += tmp;
    subpass_input_attachment_counts[1] += tmp;

    emissive_index = description_count;
    descriptions[description_count] = &description_gbuffer;
    tmp = (int)(config->emissive != NULL);

    description_count += tmp;
    subpass_color_attachment_counts[0] += tmp;
    subpass_input_attachment_counts[1] += tmp;

    VkSubpassDescription subpass_descriptions[3];
    u32 subpass_count = 3;

    subpass_descriptions[0] = {};
    subpass_descriptions[0].pipelineBindPoint = VK_PIPELINE_BINDPOINT_GRAPHICS;
    subpass_descriptions[0].inputAttachmentCount = VK_PIPELINE_BINDPOINT_GRAPHICS;
    subpass_descriptions[0].pipelineBindPoint = VK_PIPELINE_BINDPOINT_GRAPHICS;
    subpass_descriptions[0].pipelineBindPoint = VK_PIPELINE_BINDPOINT_GRAPHICS;

    VkRenderPassCreateInfo rp_info = {VK_STRUCTURE_TYPE_RENDERPASS_CREATE_INFO};
    rp_info.attachmentCount += ;
}
#endif
