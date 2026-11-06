#version 450

// Floor vertex interface: read world-space mesh positions and the camera transform.
// Pass the floor's horizontal coordinates to the fragment stage for procedural colouring.
// The same camera push-constant layout is also used by the sphere pipeline.
layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec2 worldXZ;

layout(push_constant) uniform CameraPushConstants {
    mat4 viewProjection;
} camera;

// Transform the floor from world space into the camera's clip space.
// Keep world coordinates for the checker pattern so its cells stay one metre wide.
// Rasterization interpolates the outgoing coordinates across each floor triangle.
void main() {
    worldXZ = inPosition.xz;
    gl_Position = camera.viewProjection * vec4(inPosition, 1.0);
}
