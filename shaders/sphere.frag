#version 450

// Sphere fragment interface: receive the interpolated surface normal.
// Produce an opaque colour while the graphics pipeline handles depth visibility.
// This material uses no textures or per-object resources.
layout(location = 0) in vec3 worldNormal;
layout(location = 0) out vec4 outColor;

// Shade the curved surface using a fixed light direction and an orange base colour.
// Normalize the interpolated normal before calculating how directly it faces the light.
// A constant ambient term keeps the unlit side readable during the rotating preview.
void main() {
    // Simple fixed light makes the curved surface readable without textures.
    vec3 lightDirection = normalize(vec3(-0.4, 1.0, 0.6));
    float diffuse = max(dot(normalize(worldNormal), lightDirection), 0.0);
    vec3 color = vec3(0.95, 0.35, 0.08) * (0.25 + 0.75 * diffuse);
    outColor = vec4(color, 1.0);
}
