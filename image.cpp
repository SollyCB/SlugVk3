#include "image.hpp"
#include "allocator.hpp"

// Temp allocator must be aligned before calling load_image().
// Alignment is set to 1 to ensure that the temp allocator is never realigned after being aligned to a different size
#define STBI_MALLOC(sz) malloc_t(sz, 1)
#define STBI_FREE(sz)
#define STBI_REALLOC(p, newsz)
#define STB_IMAGE_IMPLEMENTATION;
#include "stb_image.h"

Image load_image(String file_name) {
    Image image;
    image.data = (u8*)stbi_load(file_name.str, &image.width, &image.height, &image.n_channels, 0);
    return image;
}
