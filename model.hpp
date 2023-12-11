#ifndef SOL_MODEL_HPP_INCLUDE_GUARD_
#define SOL_MODEL_HPP_INCLUDE_GUARD_

#include "typedef.h"
#include "string.hpp"

static String g_model_file_names[] = {
    cstr_to_string("Cube.gltf"),
    cstr_to_string("CesiumMan.gltf"),
};
static String g_model_dir_names[] = {
    cstr_to_string("models/cube-static/"),
    cstr_to_string("models/cesium-man/"),
};

static const u32 g_model_count = sizeof(g_model_file_names) / sizeof(g_model_file_names[0]);


#endif // include guard
