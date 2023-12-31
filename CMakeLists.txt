cmake_minimum_required(VERSION 3.9)

# Setup
set(CMAKE_CXX_COMPILER "clang++-17")
set(CMAKE_C_COMPILER "clang-17")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED true)
set(CMAKE_BUILD_PARALLEL_LEVEL 4)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_RELEASE -O2)

option(EXTRA_WARNINGS OFF)
set(EXTRA_WARNINGS OFF)
if (EXTRA_WARNINGS)
    set(CMAKE_CXX_COMPILE_FLAGS
        -mlzcnt
        -msse4.1
        -mbmi
        -mavx2
        -ggdb
        -fstack-protector-all
        -Werror
        -Wall
        -Wextra
        -Wpedantic
    )
else()
    set(CMAKE_CXX_COMPILE_FLAGS
        -mlzcnt
        -msse4.1
        -mbmi
        -mavx2
        -ggdb
        -fstack-protector-all
    )
endif()

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
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
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
    glfw.cpp
    file.cpp
    spirv.cpp
    gpu.cpp
    gltf.cpp
    string.cpp
    image.cpp
    print.cpp
    asset.cpp

    external/tlsf.cpp

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
    # target_link_libraries(Slug PUBLIC  "C:/VulkanSDK/1.3.261.0/Lib/vulkan-1.lib")
    target_link_libraries(Slug PUBLIC  "C:/VulkanSDK/1.3.261.0/Lib/vulkan-1.dll") # dyn loader is better https://github.com/KhronosGroup/Vulkan-LoaderAndValidationLayers/issues/1707
else()
    # @Todo Make this same setup for Windows.
    target_include_directories(Slug PUBLIC  "${CMAKE_CURRENT_LIST_DIR}/external/vulkan-1.3.261.1-linux/include/")
    target_link_libraries(Slug      PUBLIC  "${CMAKE_CURRENT_LIST_DIR}/external/vulkan-1.3.261.1-linux/lib/libvulkan.so")
endif()

# GLFW
if(WIN32)
    # @Todo switch Windows to use dynamic lib as well
    # target_link_libraries(Slug      PUBLIC "${CMAKE_CURRENT_LIST_DIR}/external/glfw-win64-bin/lib-vc2022/glfw3.lib")
    target_include_directories(Slug PUBLIC "${CMAKE_CURRENT_LIST_DIR}/external/glfw-win64-bin/include")
else()
    target_link_libraries(Slug      PUBLIC "${CMAKE_CURRENT_LIST_DIR}/external/glfw-shared-linux/build/src/libglfw.so")
    target_include_directories(Slug PUBLIC "${CMAKE_CURRENT_LIST_DIR}/external/glfw-shared-linux/include/")
endif()
