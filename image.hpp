#ifndef SOL_IMAGE_HPP_INCLUDE_GUARD_
#define SOL_IMAGE_HPP_INCLUDE_GUARD_

#include "typedef.h"
#include "string.hpp"
#include "stb_image.h"
#include "print.hpp"

struct Image {
    u32 width;
    u32 height;
    u8 *data;
};
Image load_image(String *file_name);
inline static void free_image(Image *image) { stbi_image_free(image->data); }

#endif // include guard
