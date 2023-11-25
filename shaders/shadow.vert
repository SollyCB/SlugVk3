#version 450

layout(location = 0) in vec3 position;

layout(set = 0, binding = 0) uniform Pc {
    mat4 world;
} pc;

void main() {
    gl_Position = vec4(position, 1) * pc.world;
} 
