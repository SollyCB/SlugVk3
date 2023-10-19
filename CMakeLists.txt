cmake_minimum_required(VERSION 3.9)

# Setup
set(CMAKE_CXX_STANDARD 20) 
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_BUILD_PARALLEL_LEVEL 4)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_COMPILE_FLAGS -mlzcnt -msse4.1 -mbmi -mavx2 -ggdb)

#-mavx512f <- this option causes me to crash with illegal instruction when casting int to float. I was using it for avx instructions... I assume this laptop doesnt support 512 registers?

project(Slug)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE) 
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE) 
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Build Options
option(BUILD_TESTS OFF)
option(BUILD_DEBUG ON)

set(BUILD_DEBUG ON CACHE BOOL "Enable DEBUG during development...")

if (BUILD_DEBUG)
    message("Building DEBUG...\n")
    set(CMAKE_BUILD_TYPE Debug)
    add_compile_definitions(DEBUG=true)
else()
    message("Building RELEASE...\n")
    set(CMAKE_BUILD_TYPE Release)
    add_compile_definitions(DEBUG=false)
endif()

if (BUILD_TESTS)
    add_compile_definitions(TEST=true)
else()
    add_compile_definitions(TEST=false)
endif()


# Source
add_executable(Slug 
    main.cpp
    allocator.cpp
    vulkan_errors.cpp
    gpu.cpp
    glfw.cpp
    file.cpp
    spirv.cpp
    gltf.cpp
    string.cpp

    external/tlsf.cpp
    external/stb_image.cpp

    test/test.cpp
)
target_include_directories(Slug PUBLIC external)
target_include_directories(Slug PUBLIC test)
target_compile_options(Slug PUBLIC ${CMAKE_CXX_COMPILE_FLAGS})


                            ## External libs ##
# Vulkan
if (WIN32)
    find_package(Vulkan REQUIRED)
    target_include_directories(Slug PUBLIC "C:/VulkanSDK/1.3.261.0/Include")
    target_link_libraries(Slug PUBLIC  "C:/VulkanSDK/1.3.261.0/Lib/vulkan-1.lib")
else()
    #find_package(Vulkan REQUIRED)
    target_include_directories(Slug PUBLIC  "/home/solly/vulkan/1.3.261.1/x86_64/include/")
    target_link_libraries(Slug PUBLIC  "/home/solly/vulkan/1.3.261.1/x86_64/lib/libvulkan.so")
endif()

# GLFW
if(WIN32)
    target_link_libraries(Slug PUBLIC "${CMAKE_CURRENT_LIST_DIR}/external/glfw-win64-bin/lib-vc2022/glfw3.lib")
    target_include_directories(Slug PUBLIC "${CMAKE_CURRENT_LIST_DIR}/external/glfw-win64-bin/include")
else()
    target_link_libraries(Slug PUBLIC "${CMAKE_CURRENT_LIST_DIR}/external/glfw-shared-linux/build/src/libglfw.so")
    target_include_directories(Slug PUBLIC "${CMAKE_CURRENT_LIST_DIR}/external/glfw-shared-linux/include/")
endif()
