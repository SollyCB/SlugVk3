#version 450

layout(binding = 0, set = 0) uniform UBO_1 {
    vec3 a_vec;
} ubo_1[2];
layout(binding = 1, set = 0) uniform UBO_2 {
    vec3 b_vec;
} ubo_2[4];

layout(binding = 0, set = 1) uniform UBO_3 {
    vec3 a_vec;
} ubo_3[3];
layout(binding = 1, set = 1) uniform UBO_4 {
    vec3 b_vec;
} ubo_4;

layout(binding = 0, set = 2) uniform UBO_5 {
    vec3 a_vec;
} ubo_5[7];
layout(binding = 1, set = 2) uniform UBO_6 {
    vec3 b_vec;
} ubo_6;
layout(binding = 2, set = 2) uniform UBO_7 {
    vec3 b_vec;
} ubo_7[2];


layout(binding = 0, set = 3) uniform sampler2D tex_sampler_1[4];
layout(binding = 1, set = 3) uniform sampler2D tex_sampler_2[3];
layout(binding = 2, set = 3) uniform sampler2D tex_sampler_3[2];
layout(binding = 3, set = 3) uniform sampler2D tex_sampler_4[1];

void main() {
    gl_Position = vec4(1, 1, 1, 1);
}
