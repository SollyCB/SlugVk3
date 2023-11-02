#include "image.hpp"
#include "allocator.hpp"

// Temp allocator must be aligned before calling load_image().
// Alignment is set to 1 to ensure that the temp allocator is never realigned after being aligned to a different size
#define STBI_MALLOC(sz) malloc_t(sz, 1)
#define STBI_FREE(p) free_h(p)
#define STBI_REALLOC(p, newsz) (realloc_h((u8*)p, newsz))
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Image load_image(String *file_name) {
    int x,y,n;
    u8* data = stbi_load(file_name->str, &x, &y, &n, 0);

    ASSERT(x >= 0, "");
    ASSERT(y >= 0, "");
    ASSERT(n >= 0, "");

    Image image;
    image.width = (u32)x;
    image.height = (u32)y;
    image.n_channels = (u32)n;
    image.data = (u8*)data;

    return image;
}
