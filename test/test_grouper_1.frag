#version 450

layout(set = 0, binding = 0) uniform sampler2D tex_sampler_0;
layout(set = 0, binding = 1) uniform sampler2D tex_sampler_1[2];
layout(set = 0, binding = 2) uniform sampler2D tex_sampler_2[3];
layout(set = 3, binding = 0) uniform sampler2D tex_sampler_3[4];

layout(set = 1, binding = 0) uniform UBO1 {
    vec3 a_vec;
} ubo1;

layout(set = 1, binding = 1) buffer SBO1 {
    vec3 a_vec;
} sbo1;

layout(location = 0) out vec4 color;

void main() {
    color = vec4(1, 1, 1, 1);
}
