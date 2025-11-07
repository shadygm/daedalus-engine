#version 430 core

layout(location = 0) in vec2 aUV;               // grid coordinates in [0,1]
layout(location = 1) in vec3 aChunkData;         // x: origin.x, y: origin.z, z: texture layer index

uniform mat4 view;
uniform mat4 projection;
uniform float uChunkSize;
uniform float uInvResolution;
uniform sampler2DArray uHeightTex;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} vs_out;
uniform bool uWorldCurvatureEnabled;
uniform float uWorldCurvatureStrength;

float sampleHeight(vec2 uv)
{
    uv = clamp(uv, vec2(0.0), vec2(1.0));
    return texture(uHeightTex, vec3(uv, aChunkData.z)).r;
}

void main()
{
    vec2 uv = aUV;
    float height = sampleHeight(uv);

    float offset = uInvResolution;
    float hL = sampleHeight(uv - vec2(offset, 0.0));
    float hR = sampleHeight(uv + vec2(offset, 0.0));
    float hD = sampleHeight(uv - vec2(0.0, offset));
    float hU = sampleHeight(uv + vec2(0.0, offset));

    float stepWorld = uChunkSize * uInvResolution;
    float dhdx = (hR - hL) / (2.0 * stepWorld);
    float dhdz = (hU - hD) / (2.0 * stepWorld);
    vec3 normal = normalize(vec3(-dhdx, 1.0, -dhdz));

    vec3 worldPos = vec3(aChunkData.x + uv.x * uChunkSize, height, aChunkData.y + uv.y * uChunkSize);

    vs_out.worldPos = worldPos;
    vs_out.normal = normal;
    vs_out.uv = uv;

    vec4 posView = view * vec4(worldPos, 1.0);
    if (uWorldCurvatureEnabled) {
        float fragmentDist = length(posView.xyz);
        float curved = posView.y - uWorldCurvatureStrength * fragmentDist * fragmentDist;
        posView.y = curved;
    }
    gl_Position = projection * posView;
}
