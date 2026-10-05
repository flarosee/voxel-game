#version 450
layout(location = 0) flat in vec3 faceNormal;
layout(location = 1) in vec2 faceUV;
layout(location = 2) flat in uint faceMaterial;
layout(location = 0) out vec4 outColor;
layout(location = 3) in vec3 localPosition;
layout(location = 4) flat in uint faceSunlight;
layout(location = 5) flat in uint faceBlockLight;
layout(push_constant) uniform Transform { mat4 mvp; uint debugView; vec4 eyeAndFog; } transform;
void main() {
    // Repeating cutout mask: holes discard before writing depth. Integer UV phase
    // survives greedy merging, chunk seams, and BSP splits.
    if (faceMaterial == 9u) {
        ivec2 cell = ivec2(floor(fract(faceUV) * 8.0));
        if ((cell.x * 3 + cell.y * 5) % 7 < 2) discard;
    }
    float alpha = faceMaterial == 7u ? 0.45 : faceMaterial == 8u ? 0.22 : 1.0;
    vec3 color = vec3(1.0, 0.0, 1.0);
    if (faceMaterial == 1u) color = vec3(0.24, 0.60, 0.26);
    if (faceMaterial == 2u) color = vec3(0.43, 0.26, 0.14);
    if (faceMaterial == 3u) color = vec3(0.48, 0.51, 0.55);
    if (faceMaterial == 4u) color = vec3(0.86, 0.76, 0.46);
    if (faceMaterial == 5u) color = vec3(0.65, 0.49, 0.29);
    if (faceMaterial == 6u) color = vec3(1.0, 0.85, 0.5);
    if (faceMaterial == 7u) color = vec3(0.10, 0.38, 0.78);
    if (faceMaterial == 8u) color = vec3(0.65, 0.87, 0.95);
    if (faceMaterial == 9u) color = vec3(0.16, 0.48, 0.12);
    // Independent light channels: brightest wins, without additive overexposure.
    float skylight = float(min(faceSunlight, 15u)) / 15.0;
    float blocklight = float(min(faceBlockLight, 15u)) / 15.0;
    color *= max(vec3(skylight), vec3(1.0, 0.72, 0.38) * blocklight);
    if (transform.debugView == 1u) {
        vec2 tile = fract(faceUV);
        float checker = mod(floor(faceUV.x * 2.0) + floor(faceUV.y * 2.0), 2.0);
        color = mix(vec3(0.18), vec3(0.8), checker);
        if (tile.x < 0.06) color = vec3(1.0, 0.2, 0.2);
        if (tile.y < 0.06) color = vec3(0.2, 0.8, 1.0);
    }
    if (transform.debugView == 2u) color = faceNormal * 0.5 + 0.5;
    if (transform.debugView == 3u) color = vec3(skylight);
    if (transform.debugView == 4u) color = vec3(blocklight);
    // Fade the finite draw frontier into the clear color. This is distance fog, not lighting.
    if (transform.debugView < 3u && transform.eyeAndFog.w > 0.0) {
        float distanceToEye = length(localPosition - transform.eyeAndFog.xyz);
        float fog = smoothstep(transform.eyeAndFog.w * 0.65, transform.eyeAndFog.w, distanceToEye);
        color = mix(color, vec3(0.018, 0.027, 0.045), fog);
    }
    outColor = vec4(color, alpha);
}
