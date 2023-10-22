#ifndef SOL_IMAGE_HPP_INCLUDE_GUARD_
#define SOL_IMAGE_HPP_INCLUDE_GUARD_

#include "typedef.h"

struct Image {
    u32 width;
    u32 height;
    u32 n_channels;
    u8 *data;
};
Image load_image(String file_name);

#endif // include guard
