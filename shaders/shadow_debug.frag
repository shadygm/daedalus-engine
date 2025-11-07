#version 430 core

layout (location = 0) out vec4 FragColor;

in vec2 vUV;

uniform sampler2DArray uShadowMap;
uniform int uLayer;
uniform float uNearPlane;
uniform float uFarPlane;
uniform int uLinearize;
uniform float uContrast;

float linearizeDepth(float depth, float nearPlane, float farPlane)
{
    float z = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));
}

void main()
{
    float depth = texture(uShadowMap, vec3(vUV, float(uLayer))).r;
    float value = depth;

    if (uLinearize == 1) {
        float linearDepth = linearizeDepth(depth, uNearPlane, uFarPlane);
        float normalized = (linearDepth - uNearPlane) / max(uFarPlane - uNearPlane, 1e-5);
        value = clamp(normalized, 0.0, 1.0);
    }

    value = clamp(value, 0.0, 1.0);
    value = pow(value, uContrast);
    FragColor = vec4(vec3(value), 1.0);
}
