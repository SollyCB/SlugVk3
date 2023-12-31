#include "image.hpp"
#include "allocator.hpp"
#include "print.h"


/*
   @Note I cannot use the temp allocator here (annoyingly): stbi_load(..) sometimes uses realloc even
   just for pngs and jpegs. The temp allocator does not support reallocation, as it does not track
   specific allocations. Without realloc, obvs everything just breaks. As such, we have to use the heap
   allocator. (Below are the previous temp STBI_<alloc_func> defines.)

    void dead_free() {}
    u8* dead_realloc() { return NULL; }

    #define STBI_MALLOC(sz) malloc_t(sz)
    #define STBI_FREE(p) dead_free()
    #define STBI_REALLOC(p, newsz) realloc_t(p, newsz)
*/

#define STBI_MALLOC(sz) malloc_h(sz, 16)
#define STBI_FREE(p) free_h(p)
#define STBI_REALLOC(p, newsz) realloc_h(p, newsz)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define IMG_LOADER_SHOW_INFO false

Image load_image(String *file_name) {
    int x,y,n;
    u8* data = stbi_load(file_name->str, &x, &y, &n, 4); // Vulkan does not like fewer than 4 channels

    char msg[127];
    string_format(msg, "Failed to load image %s", file_name->str);
    assert(data && (const char*)msg);

    #if IMG_LOADER_SHOW_INFO
    println("Info for Image: %s", file_name->str);
    println("    width: %i", x); // See if this ever crashes without being cast to s64 (print always va_args integers to 64 and I thought this should cause crashes but it never has...)
    println("    height: %i", y);
    #endif

    Image image;
    image.width = (u32)x;
    image.height = (u32)y;
    image.data = (u8*)data;

    return image;
}
