#version 450

// Sphere vertex interface: read shared mesh vertices and one translation per instance.
// The camera matrix is shared with the floor through compatible push-constant layouts.
// Send a world-space normal to the fragment stage for simple directional lighting.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 instancePosition;
layout(location = 0) out vec3 worldNormal;

layout(push_constant) uniform CameraPushConstants {
    mat4 viewProjection;
} camera;

// Place this copy of the sphere in the world, then project it through the camera.
// Positions lie on a sphere around the local origin, so their directions provide surface normals.
// Instances use translation only; those normals therefore keep their world-space directions.
void main() {
    // The mesh is centered at the origin; each instance adds its own translation.
    vec3 worldPosition = inPosition + instancePosition;
    worldNormal = normalize(inPosition);
    gl_Position = camera.viewProjection * vec4(worldPosition, 1.0);
}
