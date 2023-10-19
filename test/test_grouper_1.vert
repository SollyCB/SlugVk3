#version 450

layout(set = 0, binding = 0) uniform sampler2D tex_sampler_0;
layout(set = 0, binding = 1) uniform sampler2D tex_sampler_1[2];
layout(set = 0, binding = 2) uniform sampler2D tex_sampler_2[3];

layout(set = 1, binding = 0) uniform UBO1 {
    vec3 a_vec;
} ubo1;

layout(set = 2, binding = 0) uniform UBO2 {
    vec3 a_vec;
} ubo2;

layout(set = 1, binding = 1) buffer SBO1 {
    vec3 a_vec;
} sbo1;

layout(set = 2, binding = 1) buffer SBO2 {
    vec3 a_vec;
} sbo2;

void main() {
    gl_Position = vec4(1, 1, 1, 1);
}
