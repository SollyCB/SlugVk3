#ifndef SOL_SHADER_HPP_INCLUDE_GUARD_
#define SOL_SHADER_HPP_INCLUDE_GUARD_

#include "typedef.h"

// @Note shader ids must appear in the enum in the same order that they appear in 'shader_file_names'
static const char *g_shader_file_names[] = {
    "shaders/basic.vert.spv",
    "shaders/basic.frag.spv",
};
static const u32 g_shader_file_count = sizeof(g_shader_file_names) / sizeof(g_shader_file_names[0]);
enum Shader_Id {
    SHADER_ID_BASIC_VERT,
    SHADER_ID_BASIC_FRAG,
    SHADER_ID_SHADOW_VERT,
    SHADER_ID_SHADOW_FRAG,
};

#endif
