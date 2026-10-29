#version 450

layout(location = 0) in vec2 worldXZ;
layout(location = 0) out vec4 outColor;

void main() {
    // One world unit is one metre, so each cell is 1 x 1 metre.
    vec2 cell = floor(worldXZ);
    float checker = mod(cell.x + cell.y, 2.0);
    float shade = mix(0.15, 0.75, checker);
    outColor = vec4(vec3(shade), 1.0);
}
