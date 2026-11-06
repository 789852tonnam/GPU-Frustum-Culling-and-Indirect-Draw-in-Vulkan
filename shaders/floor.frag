#version 450

// Floor fragment interface: receive interpolated horizontal world coordinates.
// Each fragment produces one opaque colour for the colour attachment.
// The pipeline's depth test determines whether that colour is visible in the final image.
layout(location = 0) in vec2 worldXZ;
layout(location = 0) out vec4 outColor;

// Build the checkerboard procedurally from world-space cells instead of a texture.
// Alternating cell parity selects a light or dark shade at each fragment.
// Using world coordinates keeps the pattern fixed while the camera rotates.
void main() {
    // One world unit is one metre, so each cell is 1 x 1 metre.
    vec2 cell = floor(worldXZ);
    float checker = mod(cell.x + cell.y, 2.0);
    float shade = mix(0.15, 0.75, checker);
    outColor = vec4(vec3(shade), 1.0);
}
