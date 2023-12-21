#version 450

layout(set = 0, binding = 0) uniform sampler2D material_samplers[5];

layout(set = 1, binding = 0) uniform Material_UBO {
    vec4  base_color_factor;
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    vec3  emissive_factor;
    float alpha_cutoff;
} mat_ubo;

layout(location = 0) out vec4 frag_color;

void main() {
    frag_color = vec4(1, 0, 0, 1);
}
