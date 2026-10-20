#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 uiColor;
} ubo;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(vec3(0.5, 1.0, 0.5));
    float diff = max(dot(N, L), 0.2);
    vec3 finalColor = fragColor * ubo.uiColor.rgb * diff;
    outColor = vec4(finalColor, 1.0);
}