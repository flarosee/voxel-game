#version 450
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
layout(location = 3) in uint material;
layout(location = 4) in uint sunlight;
layout(location = 5) in uint blockLight;
layout(location = 0) flat out vec3 faceNormal;
layout(location = 1) out vec2 faceUV;
layout(location = 2) flat out uint faceMaterial;
layout(location = 3) out vec3 localPosition;
layout(location = 4) flat out uint faceSunlight;
layout(location = 5) flat out uint faceBlockLight;
layout(push_constant) uniform Transform { mat4 mvp; uint debugView; vec4 eyeAndFog; } transform;
void main() {
    gl_Position = transform.mvp * vec4(position, 1.0);
    faceNormal = normal;
    faceUV = uv;
    faceMaterial = material;
    localPosition = position;
    faceSunlight = sunlight;
    faceBlockLight = blockLight;
}
