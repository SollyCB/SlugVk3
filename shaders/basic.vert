#version 450

layout (location = 0) in vec3 pos;
layout (location = 1) in vec3 norm;
layout (location = 2) in vec4 tang;
layout (location = 3) in vec2 tex_coords;

layout (location = 0) out vec2 out_tex_coords;

layout(set = 0, binding = 0) uniform UBO {
    mat4 mvp;
} ubo;

void main() {
    gl_Position = vec4(pos, 1) * ubo.mvp; 
    out_tex_coords = tex_coords;
}
