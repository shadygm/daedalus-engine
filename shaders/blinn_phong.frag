#version 430 core

out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord0;
in vec2 TexCoord1;
in vec3 Tangent;
in vec3 Bitangent;
in vec3 TangentLightPos;
in vec3 TangentViewPos;
in vec3 TangentFragPos;

layout(std140, binding = 3) uniform PerFrameDataBlock {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 inverseView;
    vec4 cameraPos;
    vec4 lightPos;
    vec4 lightColor;
    vec4 ambientColorStrength;
    ivec4 frameFlags;
    vec4 envParams;
} uFrame;

uniform bool uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogGradient;

layout(std140, binding = 4) uniform ObjectDataBlock {
    mat4 model;
    mat4 normalMatrix;
    ivec4 materialFlags;
    ivec4 textureUsage;
    ivec4 textureUsage2;
    ivec4 uvSets0;
    ivec4 uvSets1;
} uObject;

struct MaterialData {
    vec4 baseColor;
    vec4 diffuseColor;
    vec4 specularColor;
    vec4 emissiveColorIntensity;
    vec4 pbrParams;
    vec4 extraParams;
    ivec4 textureUsage;
    ivec4 textureUsage2;
    ivec4 uvSets0;
    ivec4 uvSets1;
    vec4 uvTransformAlbedo;
    vec4 uvTransformMR;
    vec4 uvTransformNormal;
    vec4 uvTransformAO;
    vec4 uvTransformEmissive;
    vec4 uvRotations;
    vec4 uvRotations2;
};

layout(std430, binding = 2) readonly buffer MaterialBuffer {
    MaterialData uMaterials[];
};

layout(binding = 0) uniform sampler2D uAlbedoMap;
layout(binding = 2) uniform sampler2D uNormalMap;
layout(binding = 3) uniform sampler2D uAOMap;
layout(binding = 4) uniform sampler2D uEmissiveMap;
// Optional: user-provided height map (not bound by default)
layout(binding = 5) uniform sampler2D uHeightMap;

struct GpuLight {
    vec4 positionType;
    vec4 directionRange;
    vec4 colorIntensity;
    vec4 spotShadow;
    vec4 shadowParams;
    vec4 attenuation;
    vec4 extra;
};

layout(std430, binding = 0) buffer LightBuffer { GpuLight uLights[]; };

struct ShadowUniformData {
    mat4 lightMatrix;
    vec4 params; // x: near, y: far, z: invResolution, w: type (1 = spot)
};

layout(std140, binding = 1) uniform ShadowDataBlock {
    ShadowUniformData uShadowData[8];
};

layout(binding = 7) uniform sampler2DArrayShadow uShadowMapArray;
layout(binding = 13) uniform samplerCubeShadow uPointShadowMaps[8];

const int LIGHT_TYPE_POINT = 0;
const int LIGHT_TYPE_SPOT = 1;
const int MAX_SHADOW_SLOTS = 8;

vec3 decodeNormal(vec3 encoded)
{
    return normalize(encoded * 2.0 - 1.0);
}

// Parallax mapping uniforms (global)
uniform bool uParallaxEnabled;
uniform bool uParallaxUseNormalAlpha;
uniform bool uHasHeightMap;
uniform float uParallaxScale;
uniform float uParallaxBias;
uniform bool uParallaxInvertOffset;

vec2 applyUvTransform(vec2 uv, vec4 transform, float rotation)
{
    vec2 offset = transform.xy;
    vec2 scale = transform.zw;
    vec2 scaled = uv * scale;
    float s = sin(rotation);
    float c = cos(rotation);
    vec2 rotated = vec2(c * scaled.x - s * scaled.y, s * scaled.x + c * scaled.y);
    return rotated + offset;
}

vec3 visualizeUV(vec2 uv)
{
    vec2 wrapped = fract(uv);
    vec3 base = vec3(wrapped, 0.5);
    float edgeX = step(wrapped.x, 0.02) + step(1.0 - wrapped.x, 0.02);
    float edgeY = step(wrapped.y, 0.02) + step(1.0 - wrapped.y, 0.02);
    float grid = clamp(edgeX + edgeY, 0.0, 1.0);
    vec3 gridColor = vec3(1.0, 0.1, 0.1);
    return mix(base, gridColor, grid);
}

vec2 selectUV(int set)
{
    bool hasPrimary = uObject.materialFlags.z != 0;
    bool hasSecondary = uObject.materialFlags.w != 0;

    if (set == 1)
        return hasSecondary ? TexCoord1 : TexCoord0;
    if (set == 0) {
        if (!hasPrimary && hasSecondary)
            return TexCoord1;
        return TexCoord0;
    }
    return hasSecondary ? TexCoord1 : TexCoord0;
}

vec2 transformedUV(int set, vec4 transform, float rotation)
{
    return applyUvTransform(selectUV(set), transform, rotation);
}

mat3 orthonormalizeTBN(vec3 T, vec3 B, vec3 N)
{
    vec3 n = normalize(N);
    vec3 t = normalize(T - n * dot(n, T));
    vec3 b = cross(n, t);
    float hand = sign(dot(b, B));
    b *= hand;
    return mat3(t, b, n);
}

// --- Basic parallax UV offset ---
float sampleHeightValue(vec2 uv, bool canUseNormalAlpha)
{
    // 1) Prefer a dedicated height map when provided
    if (uHasHeightMap)
        return texture(uHeightMap, uv).r;

    // 2) Optional: derive height from normal map
    if (canUseNormalAlpha) {
        vec4 n = texture(uNormalMap, uv);
        float a = n.a;

        // Heuristics: if alpha is effectively constant (near 0 or 1)
        // or varies negligibly across the surface, fall back to RGB luminance.
        float da = max(abs(dFdx(a)), abs(dFdy(a)));
        bool alphaLooksUniform = (a < 0.01) || (a > 0.99) || (da < 1e-3);
        if (alphaLooksUniform) {
            // Luminance of the normal RGB as a proxy height
            return dot(n.rgb, vec3(0.299, 0.587, 0.114));
        }
        return a;
    }

    // 3) No height information available
    return 0.0;
}

// LearnOpenGL tutorial parallax mapping
vec2 ParallaxMapping(vec2 texCoords, vec3 viewDir, bool canUseNormalAlpha)
{
    float height = sampleHeightValue(texCoords, canUseNormalAlpha);
    float sign = uParallaxInvertOffset ? -1.0 : 1.0;
    vec2 p = viewDir.xy / viewDir.z * (height * uParallaxScale);
    return texCoords - (sign * p);
}

vec3 computeNormal(vec2 normalUV, bool useNormalMap, float strength, int hasTangents)
{
    vec3 N = normalize(Normal);
    if (!gl_FrontFacing)
        N = -N;
    if (!useNormalMap || strength <= 0.0)
        return N;

    vec3 tangentNormal = decodeNormal(texture(uNormalMap, normalUV).xyz);
    tangentNormal.xy *= strength;
    tangentNormal = normalize(tangentNormal);

    if (hasTangents == 1) {
        mat3 TBN = orthonormalizeTBN(Tangent, Bitangent, N);
        return normalize(TBN * tangentNormal);
    }

    vec3 dp1 = dFdx(FragPos);
    vec3 dp2 = dFdy(FragPos);
    vec2 duv1 = dFdx(normalUV);
    vec2 duv2 = dFdy(normalUV);

    float det = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(det) < 1e-6)
        return N;

    vec3 T = normalize((dp1 * duv2.y - dp2 * duv1.y) * (1.0 / det));
    vec3 B = normalize((-dp1 * duv2.x + dp2 * duv1.x) * (1.0 / det));
    mat3 TBN = orthonormalizeTBN(T, B, N);
    return normalize(TBN * tangentNormal);
}

float computeRangeFalloff(float distance, float range)
{
    if (range <= 0.0)
        return 1.0;
    float n = clamp(distance / range, 0.0, 1.0);
    return 1.0 - n * n * n * n;
}

float computeDistanceAttenuation(GpuLight light, float distance)
{
    if (light.attenuation.w <= 0.5)
        return 1.0;

    float denom = light.attenuation.x + light.attenuation.y * distance + light.attenuation.z * distance * distance;
    float attenuation = 1.0 / max(denom, 1e-6);

    float range = max(light.extra.x, 0.0);
    if (range <= 0.0)
        range = light.directionRange.w;

    if (range > 0.0) {
        float n = clamp(distance / range, 0.0, 1.0);
        float soft = smoothstep(0.0, 1.0, 1.0 - n);
        attenuation *= computeRangeFalloff(distance, range) * soft;
    }

    return attenuation;
}

float sampleSpotShadow(GpuLight light, vec3 fragPos, vec3 N, vec3 L)
{
    float layerRaw = light.spotShadow.z;
    if (layerRaw < 0.0)
        return 1.0;

    int layer = int(layerRaw + 0.5);
    if (layer < 0 || layer >= MAX_SHADOW_SLOTS)
        return 1.0;

    ShadowUniformData shadowUniform = uShadowData[layer];
    vec4 lightClip = shadowUniform.lightMatrix * vec4(fragPos, 1.0);
    if (lightClip.w <= 0.0)
        return 1.0;

    vec3 projCoords = lightClip.xyz / lightClip.w;
    if (projCoords.z < -1.0 || projCoords.z > 1.0)
        return 1.0;

    vec2 uv = projCoords.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0)
        return 1.0;

    float depth = projCoords.z * 0.5 + 0.5;
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0)
        return 1.0;

    float bias = light.shadowParams.x;
    float slopeBias = (1.0 - NdotL) * 0.0025;
    float texelSize = shadowUniform.params.z;
    float referenceDepth = depth - (bias + slopeBias + texelSize * 1.5);

    vec2 texel = vec2(texelSize);
    float visibility = 0.0;
    int kernelRadius = 1;
    int taps = 0;
    for (int y = -kernelRadius; y <= kernelRadius; ++y) {
        for (int x = -kernelRadius; x <= kernelRadius; ++x) {
            vec2 offset = vec2(x, y) * texel;
            visibility += texture(uShadowMapArray, vec4(uv + offset, float(layer), referenceDepth));
            ++taps;
        }
    }

    if (taps > 0)
        visibility /= float(taps);

    return visibility;
}

float samplePointShadow(GpuLight light, vec3 fragPos, vec3 N, vec3 L)
{
    int slot = int(light.spotShadow.z + 0.5);
    if (slot < 0 || slot >= MAX_SHADOW_SLOTS)
        return 1.0;

    vec3 lightToFrag = fragPos - light.positionType.xyz;
    float distanceToLight = length(lightToFrag);
    if (distanceToLight <= 0.0)
        return 1.0;

    float nearPlane = max(light.shadowParams.y, 0.0001);
    float farPlane = max(light.shadowParams.z, nearPlane + 0.0001);
    float normalizedDepth = (distanceToLight - nearPlane) / (farPlane - nearPlane);
    if (normalizedDepth > 1.0)
        return 1.0;
    normalizedDepth = clamp(normalizedDepth, 0.0, 1.0);

    float NdotL = max(dot(N, L), 0.0);
    float bias = light.shadowParams.x + (1.0 - NdotL) * 0.0025;
    float referenceDepth = clamp(normalizedDepth - bias, 0.0, 1.0);

    vec3 direction = lightToFrag / distanceToLight;
    return texture(uPointShadowMaps[slot], vec4(direction, referenceDepth));
}

float sampleShadow(GpuLight light, vec3 fragPos, vec3 N, vec3 L)
{
    if (light.shadowParams.w <= 0.5)
        return 1.0;

    int type = clamp(int(light.positionType.w + 0.5), 0, 1);
    if (type == LIGHT_TYPE_POINT)
        return samplePointShadow(light, fragPos, N, L);
    return sampleSpotShadow(light, fragPos, N, L);
}

vec3 evaluateBlinnLight(GpuLight light, vec3 fragPos, vec3 N, vec3 V, vec3 diffuseColor,
    vec3 specularColor, float shininess)
{
    int type = clamp(int(light.positionType.w + 0.5), 0, 1);

    vec3 baseColor = max(light.colorIntensity.rgb, vec3(0.0));
    float intensity = max(light.colorIntensity.a, 0.0);

    vec3 toL = light.positionType.xyz - fragPos;
    float dist = length(toL);
    if (dist <= 0.0)
        return vec3(0.0);

    vec3 L = toL / dist;
    float attenuation = computeDistanceAttenuation(light, dist);

    float spot = 1.0;
    if (type == LIGHT_TYPE_SPOT) {
        vec3 dir = normalize(light.directionRange.xyz);
        float innerCos = light.spotShadow.x;
        float outerCos = light.spotShadow.y;
        if (innerCos > 1.0 || outerCos > 1.0) {
            innerCos = cos(radians(innerCos));
            outerCos = cos(radians(outerCos));
        }
        if (innerCos < outerCos) {
            float t = innerCos;
            innerCos = outerCos;
            outerCos = t;
        }
        float c = dot(dir, -L);
        float w = clamp((c - outerCos) / max(innerCos - outerCos, 1e-4), 0.0, 1.0);
        spot = w * w * (3.0 - 2.0 * w);
    }

    float shadow = sampleShadow(light, fragPos, N, L);

    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = NdotL * diffuseColor;

    vec3 specular = vec3(0.0);
    if (NdotL > 0.0) {
        vec3 H = normalize(V + L);
        float specTerm = pow(max(dot(N, H), 0.0), shininess);
        specular = specTerm * specularColor;
    }

    return (diffuse + specular) * baseColor * intensity * attenuation * spot * shadow;
}

void main()
{
    MaterialData material = uMaterials[uObject.materialFlags.x];

    bool unlit = material.textureUsage2.z != 0;

    bool useAlbedoMap = (uObject.textureUsage.x != 0) && (material.textureUsage.x != 0);
    bool useNormalMap = (uObject.textureUsage.z != 0) && (material.textureUsage.z != 0);
    bool useAOMap = (uObject.textureUsage.w != 0) && (material.textureUsage.w != 0);
    bool useEmissiveMap = (uObject.textureUsage2.x != 0) && (material.textureUsage2.x != 0);

    int alphaMode = uObject.textureUsage2.y;
    int hasTangents = uObject.materialFlags.y;

    vec2 albedoUV = transformedUV(uObject.uvSets0.x, material.uvTransformAlbedo, material.uvRotations.x);
    vec2 normalUV = transformedUV(uObject.uvSets0.z, material.uvTransformNormal, material.uvRotations.z);
    vec2 aoUV = transformedUV(uObject.uvSets0.w, material.uvTransformAO, material.uvRotations.w);
    vec2 emissiveUV = transformedUV(uObject.uvSets1.x, material.uvTransformEmissive, material.uvRotations2.x);

    // Optional parallax mapping (LearnOpenGL tutorial approach)
    if (uParallaxEnabled) {
        bool canUseNormalAlpha = uParallaxUseNormalAlpha && (useNormalMap);
        vec3 viewDir = normalize(TangentViewPos - TangentFragPos);
        albedoUV = ParallaxMapping(albedoUV, viewDir, canUseNormalAlpha);
        normalUV = ParallaxMapping(normalUV, viewDir, canUseNormalAlpha);
        aoUV = ParallaxMapping(aoUV, viewDir, canUseNormalAlpha);
        emissiveUV = ParallaxMapping(emissiveUV, viewDir, canUseNormalAlpha);
    }

    vec3 diffuseColor = clamp(material.diffuseColor.rgb, vec3(0.0), vec3(1.0));
    float alpha = clamp(material.baseColor.a, 0.0, 1.0);
    if (useAlbedoMap) {
        vec4 tex = texture(uAlbedoMap, albedoUV);
        diffuseColor *= tex.rgb;
        alpha *= tex.a;
    }

    float alphaCutoff = clamp(material.extraParams.z, 0.0, 1.0);
    if (alphaMode == 1) {
        if (alpha < alphaCutoff)
            discard;
        alpha = 1.0;
    } else if (alphaMode == 0) {
        alpha = 1.0;
    } else {
        alpha = clamp(alpha, 0.0, 1.0);
    }

    vec3 emissiveColor = material.emissiveColorIntensity.rgb;
    float emissiveIntensity = max(material.emissiveColorIntensity.a, 0.0);
    vec3 emissive = emissiveColor;
    if (useEmissiveMap)
        emissive += texture(uEmissiveMap, emissiveUV).rgb;
    emissive *= emissiveIntensity;

    if (unlit) {
        vec3 color = diffuseColor + emissive;
        FragColor = vec4(color, alpha);
        return;
    }

    float normalScale = max(material.extraParams.x, 0.0);
    float normalStrength = max(material.extraParams.y, 0.0);
    float normalCombinedStrength = normalScale * normalStrength;

    vec3 N = computeNormal(normalUV, useNormalMap, normalCombinedStrength, hasTangents);
    vec3 V = normalize(uFrame.cameraPos.xyz - FragPos);

    vec3 specularColor = clamp(material.specularColor.rgb, vec3(0.0), vec3(1.0));
    float shininess = max(material.extraParams.w, 1.0);

    vec3 directLighting = vec3(0.0);
    int lightCount = max(uFrame.frameFlags.x, 0);
    if (lightCount > 0) {
        for (int i = 0; i < lightCount; ++i) {
            directLighting += evaluateBlinnLight(uLights[i], FragPos, N, V, diffuseColor, specularColor, shininess);
        }
    } else {
        GpuLight fallback;
        fallback.positionType = vec4(uFrame.lightPos.xyz, float(LIGHT_TYPE_POINT));
        fallback.directionRange = vec4(0.0, 0.0, 0.0, -1.0);
        fallback.colorIntensity = vec4(uFrame.lightColor.rgb, 1.0);
        fallback.spotShadow = vec4(0.0);
        fallback.shadowParams = vec4(0.0);
    fallback.attenuation = vec4(1.0, 0.0, 0.0, 0.0);
    fallback.extra = vec4(-1.0, 0.0, 0.0, 0.0);
        directLighting = evaluateBlinnLight(fallback, FragPos, N, V, diffuseColor, specularColor, shininess);
    }

    float aoBase = clamp(material.pbrParams.z, 0.0, 1.0);
    float aoIntensity = max(material.pbrParams.w, 0.0);
    float aoSample = 1.0;
    if (useAOMap)
        aoSample = clamp(texture(uAOMap, aoUV).r, 0.0, 1.0);
    float ao = clamp(aoBase * aoSample * aoIntensity, 0.0, 1.0);

    vec3 ambientColor = uFrame.ambientColorStrength.rgb;
    float ambientStrength = uFrame.ambientColorStrength.a;
    vec3 ambient = ambientColor * ambientStrength * diffuseColor * ao;

    vec3 color = ambient + directLighting + emissive;

    int debugFlag = uFrame.frameFlags.y;
    int debugTarget = uFrame.frameFlags.z;
    if (debugFlag != 0) {
        if (debugTarget == 0) color = diffuseColor;
        else if (debugTarget == 1) color = specularColor;
        else if (debugTarget == 2) color = vec3(shininess / 256.0);
        else if (debugTarget == 3) color = emissive;
        else if (debugTarget == 6) {
            // Visualize height (prefer dedicated height map). Use normal UV space for alignment.
            vec2 debugUV = transformedUV(uObject.uvSets0.z, material.uvTransformNormal, material.uvRotations.z);
            debugUV = clamp(debugUV, 0.0, 1.0);
            float h = sampleHeightValue(debugUV, uParallaxUseNormalAlpha);
            color = vec3(h);
        }
    }

    vec4 outCol = vec4(color, alpha);
    if (uFogEnabled) {
        float dist = length(uFrame.cameraPos.xyz - FragPos);
        float vis = exp(-pow(dist * uFogDensity, uFogGradient));
        vis = clamp(vis, 0.0, 1.0);
        outCol.rgb = mix(uFogColor, outCol.rgb, vis);
    }
    FragColor = outCol;
}
