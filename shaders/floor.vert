#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec2 worldXZ;

layout(push_constant) uniform CameraPushConstants {
    mat4 viewProjection;
} camera;

void main() {
    worldXZ = inPosition.xz;
    gl_Position = camera.viewProjection * vec4(inPosition, 1.0);
}
