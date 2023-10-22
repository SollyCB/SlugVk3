#include "image.hpp"
#include "allocator.hpp"

#define STBI_MALLOC(sz) malloc_t(sz, 8)
#define STBI_FREE(sz)
#define STBI_REALLOC(p, newsz) realloc_h(p, newsz)
#include "stb_image.h"

Image load_image(String file_name) {
    Image image;
    image.data = (u8*)stbi_load(file_name.str, &image.width, &image.height, &image.n_channels, 0);
    return image;
}
