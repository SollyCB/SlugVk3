#ifndef SOL_SHADER_HPP_INCLUDE_GUARD_
#define SOL_SHADER_HPP_INCLUDE_GUARD_

#include "typedef.h"

// @Note shader ids must appear in the enum in the same order that they appear in 'shader_file_names'
enum Shader_Id {
    SHADER_ID_BASIC_VERT  = 0,
    SHADER_ID_BASIC_FRAG  = 1,
    SHADER_ID_SHADOW_VERT = 2,
    SHADER_ID_SHADOW_FRAG = 3,
};

static const char *g_shader_file_names[] = {
    "shaders/basic.vert.spv",
    "shaders/basic.frag.spv",
    "shaders/shadow.vert.spv",
    "shaders/shadow.frag.spv",
};

static const u32 g_shader_file_count = sizeof(g_shader_file_names) / sizeof(g_shader_file_names[0]);

#endif
