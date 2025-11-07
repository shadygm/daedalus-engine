#version 430 core
out vec4 FragColor;

in vec3 WorldDir;

layout(binding = 19) uniform samplerCube uEnvironmentMap;
uniform float uEnvIntensity;
uniform float uMipOverride;
uniform float uMaxMip;

vec3 toneMap(vec3 color)
{
    color = color / (color + vec3(1.0));
    return color;
}

void main()
{
    vec3 dir = normalize(WorldDir);
    vec3 sampleColor;
    if (uMipOverride >= 0.0) {
        float mip = clamp(uMipOverride, 0.0, uMaxMip);
        sampleColor = textureLod(uEnvironmentMap, dir, mip).rgb;
    } else {
        sampleColor = texture(uEnvironmentMap, dir).rgb;
    }
    vec3 color = sampleColor * uEnvIntensity;
    color = toneMap(color);
    FragColor = vec4(color, 1.0);
}
