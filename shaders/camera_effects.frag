#version 430 core

in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D uSceneColor;
uniform sampler2D uSceneDepth;
uniform sampler2D uBloomTexture;
uniform sampler2D uLensDirtTexture;
uniform sampler2D uVelocityTexture;

layout(std140, binding = 5) uniform CameraEffectsSettings {
    vec4 togglesA;           // x: bloom, y: lens flare, z: chromatic aberration, w: vignette
    vec4 togglesB;           // x: depth of field, y: motion blur, z: color grading (lift/gamma/gain), w: film grain
    vec4 exposureParams;     // x: exposure, y: gamma, z: contrast, w: saturation
    vec4 bloomParams;        // x: enabled, y: strength, z: soft knee, w: radius (uv units, used on CPU)
    vec4 bloomAdvanced;      // x: threshold, y: use soft threshold (0/1), z: dirt intensity, w: legacy shaping (0/1)
    vec4 lensFlareParams;    // x: intensity, y: ghost count, z: halo radius, w: chroma boost
    vec4 lensFlareShape;     // x: ghost spacing, y: ghost threshold, z: halo thickness, w: starburst strength (reserved)
    vec4 chromaticParams;    // x: strength, y: radial strength, z: tangential strength, w: falloff
    vec4 vignetteParams;     // x: inner radius, y: outer radius, z: power, w: intensity
    vec4 dofParams;          // x: focus distance, y: focus range, z: max blur radius (in px), w: bokeh bias
    vec4 motionBlurParams;   // x: strength, y: max samples, z: shutter scale, w: padding
    vec4 colorGradeLift;     // rgb lift, w unused
    vec4 colorGradeGamma;    // rgb gamma, w unused
    vec4 colorGradeGain;     // rgb gain, w unused
    vec4 grainParams;        // x: amount, y: response, z: time, w: seed
    vec4 depthParams;        // x: near plane, y: far plane, z: invNear (unused), w: invFar (unused)
    vec4 resolutionParams;   // x: width, y: height, z: inv width, w: inv height
};

const int kMaxGhosts = 6;
const int kMaxMotionSamples = 8;
const vec2 kPoissonDisk[12] = vec2[](
    vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696,  0.457), vec2(-0.203,  0.621),
    vec2( 0.962, -0.194), vec2( 0.473, -0.480), vec2( 0.519,  0.767), vec2( 0.185, -0.893),
    vec2( 0.507,  0.064), vec2( 0.896,  0.412), vec2(-0.321, -0.932), vec2(-0.791, -0.597)
);

float saturate(float value)
{
    return clamp(value, 0.0, 1.0);
}

vec3 saturate(vec3 value)
{
    return clamp(value, vec3(0.0), vec3(1.0));
}

float linearizeDepth(float depth)
{
    float nearPlane = max(depthParams.x, 0.0001);
    float farPlane = max(depthParams.y, nearPlane + 0.0001);
    float z = depth * 2.0 - 1.0;
    return (2.0 * nearPlane * farPlane) / (farPlane + nearPlane - z * (farPlane - nearPlane));
}

float computeLuminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 applyBloom(vec3 baseColor, vec2 uv)
{
    if (togglesA.x < 0.5 || bloomParams.x < 0.5)
        return baseColor;

    vec3 bloomColor = texture(uBloomTexture, uv).rgb;
    float strength = bloomParams.y;
    float softKnee = bloomParams.z;
    float threshold = bloomAdvanced.x;
    bool useSoftThreshold = bloomAdvanced.y > 0.5;
    bool legacyMode = bloomAdvanced.w > 0.5;

    if (legacyMode) {
        float brightness = max(computeLuminance(bloomColor) - threshold, 0.0);
        float knee = softKnee + 1e-4;
        float softContribution = brightness * (1.0 / knee);
        float bloomFactor = saturate(brightness / (brightness + knee));
        bloomFactor = mix(softContribution, bloomFactor, saturate(brightness));
        bloomColor *= bloomFactor;
    } else if (useSoftThreshold) {
        float brightness = max(computeLuminance(bloomColor) - threshold, 0.0);
        float knee = softKnee + 1e-4;
        float softness = brightness / (brightness + knee);
        softness = saturate(softness);
        bloomColor *= softness;
    }

    vec3 bloomContribution = bloomColor * strength;
    vec3 result = baseColor + bloomContribution;

    float dirtIntensity = bloomAdvanced.z;
    if (dirtIntensity > 0.0) {
        vec3 dirtMask = texture(uLensDirtTexture, uv).rgb;
        result += bloomContribution * dirtMask * dirtIntensity;
    }

    return result;
}

vec3 applyLensFlare(vec3 baseColor, vec2 uv)
{
    if (togglesA.y < 0.5 || lensFlareParams.x <= 0.0)
        return baseColor;

    vec2 center = vec2(0.5);
    vec2 toCenter = center - uv;
    float dist = length(toCenter);
    if (dist < 1e-5)
        return baseColor;

    vec2 dir = toCenter / dist;

    float intensity = lensFlareParams.x;
    float ghostCount = clamp(lensFlareParams.y, 0.0, float(kMaxGhosts));
    float haloRadius = max(lensFlareParams.z, 0.0);
    float chroma = lensFlareParams.w;

    float ghostSpacing = lensFlareShape.x;
    float ghostThreshold = max(lensFlareShape.y, 0.0);
    float haloThickness = max(lensFlareShape.z, 1e-3);

    vec3 accumulatedFlare = vec3(0.0);

    for (int i = 0; i < kMaxGhosts; ++i) {
        if (float(i) >= ghostCount)
            break;

        float phase = float(i) + 1.0;
        float weight = 1.0 - (phase - 1.0) / max(ghostCount, 1.0);

        vec2 offset = dir * ghostSpacing * phase;
        // wrap back into 0..1
        vec2 ghostUv  = fract(uv + offset + vec2(1.0));
        vec2 mirrorUv = fract((2.0 * center) - (uv + offset) + vec2(1.0));


        vec3 ghostSample = texture(uBloomTexture, ghostUv).rgb;
        vec3 mirrorSample = texture(uBloomTexture, mirrorUv).rgb;

        ghostSample = max(ghostSample - vec3(ghostThreshold), vec3(0.0));
        mirrorSample = max(mirrorSample - vec3(ghostThreshold), vec3(0.0));

        float edgeFadeGhost = 1.0 - smoothstep(0.0, 0.75, distance(ghostUv, center));
        float edgeFadeMirror = 1.0 - smoothstep(0.0, 0.75, distance(mirrorUv, center));

        accumulatedFlare += ghostSample * edgeFadeGhost * weight;
        accumulatedFlare += mirrorSample * edgeFadeMirror * weight;
    }

    vec2 aspect = vec2(resolutionParams.y / max(resolutionParams.x, 1.0), 1.0);
    float haloDist = length((uv - center) * aspect);
    float haloNorm = abs(haloDist - haloRadius) / haloThickness;
    float haloWindow = 1.0 - saturate(haloNorm);
    haloWindow = haloWindow * haloWindow * (3.0 - 2.0 * haloWindow);

    vec3 haloSample = texture(uBloomTexture, center).rgb;
    haloSample = max(haloSample - vec3(ghostThreshold), vec3(0.0));
    vec3 haloContribution = haloSample * haloWindow;

    vec3 chromaMask = mix(vec3(1.0), vec3(1.5, 1.2, 1.0), clamp(chroma, 0.0, 1.0));
    accumulatedFlare += haloContribution;
    accumulatedFlare *= chromaMask;

    vec3 dirt = texture(uLensDirtTexture, uv).rgb;
    accumulatedFlare *= dirt;

    return baseColor + accumulatedFlare * intensity;
}

vec3 applyChromaticAberration(vec3 baseColor, vec2 uv)
{
    if (togglesA.z < 0.5 || chromaticParams.x <= 0.0)
        return baseColor;

    vec2 center = vec2(0.5);
    vec2 direction = uv - center;
    float radialFalloff = chromaticParams.w;
    float dist = length(direction);
    if (dist < 1e-5)
        return baseColor;

    vec2 dirNorm = direction / dist;
    vec2 tangent = vec2(-dirNorm.y, dirNorm.x);

    float strength = chromaticParams.x;
    float radialStrength = chromaticParams.y;
    float tangentialStrength = chromaticParams.z;

    float attenuation = pow(dist, radialFalloff);
    vec2 texelSize = resolutionParams.zw;

    vec2 radialOffset = dirNorm * strength * radialStrength * attenuation;
    vec2 tangentialOffset = tangent * strength * tangentialStrength * attenuation;

    vec2 offsetR = (radialOffset + tangentialOffset) * texelSize;
    vec2 offsetB = -(radialOffset + tangentialOffset) * texelSize;

    vec3 color;
    color.r = texture(uSceneColor, uv + offsetR).r;
    color.g = baseColor.g;
    color.b = texture(uSceneColor, uv + offsetB).b;

    return mix(baseColor, color, saturate(strength));
}

vec3 applyVignette(vec3 baseColor, vec2 uv)
{
    if (togglesA.w < 0.5 || vignetteParams.w <= 0.0)
        return baseColor;

    vec2 aspect = vec2(resolutionParams.y / max(resolutionParams.x, 1.0), 1.0);
    vec2 centered = (uv - 0.5) * aspect;
    float dist = length(centered);

    float inner = vignetteParams.x;
    float outer = max(vignetteParams.y, inner + 0.001);
    float power = max(vignetteParams.z, 0.1);
    float intensity = vignetteParams.w;

    float t = saturate((dist - inner) / (outer - inner));
    float fade = pow(1.0 - t, power);

    return mix(baseColor * fade, baseColor, 1.0 - intensity);
}

vec3 applyDepthOfField(vec3 baseColor, vec2 uv)
{
    if (togglesB.x < 0.5 || dofParams.z <= 0.0)
        return baseColor;

    float depthSample = texture(uSceneDepth, uv).r;
    float linearDepth = linearizeDepth(depthSample);

    float focusDistance = dofParams.x;
    float focusRange = max(dofParams.y, 0.0001);
    float maxBlurRadius = dofParams.z;

    float coc = (linearDepth - focusDistance) / focusRange;
    coc = clamp(coc, -1.0, 1.0);
    float blurRadius = abs(coc) * maxBlurRadius;
    if (blurRadius < 0.5)
        return baseColor;

    vec2 texelSize = resolutionParams.zw;
    float accumWeight = 1.0;
    vec3 accumColor = baseColor;

    for (int i = 0; i < 12; ++i) {
        vec2 offset = kPoissonDisk[i] * blurRadius * texelSize;
        vec3 sampleColor = texture(uSceneColor, uv + offset).rgb;
        float weight = 1.0 - float(i) / 12.0;
        accumColor += sampleColor * weight;
        accumWeight += weight;
    }

    return accumColor / accumWeight;
}

vec3 applyMotionBlur(vec3 baseColor, vec2 uv)
{
    if (togglesB.y < 0.5 || motionBlurParams.x <= 0.0)
        return baseColor;

    vec3 velocitySample = texture(uVelocityTexture, uv).xyz;
    vec2 velocity = velocitySample.xy * motionBlurParams.x * motionBlurParams.z;

    float lengthSq = dot(velocity, velocity);
    if (lengthSq < 1e-6)
        return baseColor;

    float sampleCountF = clamp(motionBlurParams.y, 1.0, float(kMaxMotionSamples));
    int sampleCount = int(sampleCountF);

    vec3 accum = baseColor;
    float weightSum = 1.0;

    for (int i = 1; i < kMaxMotionSamples; ++i) {
        if (i >= sampleCount)
            break;

        float t = float(i) / sampleCountF;
        vec2 sampleUv = uv + velocity * t;
        vec3 sampleColor = texture(uSceneColor, sampleUv).rgb;
        float weight = 1.0 - t;
        accum += sampleColor * weight;
        weightSum += weight;
    }

    return accum / weightSum;
}

vec3 applyColorGrading(vec3 baseColor)
{
    if (togglesB.z < 0.5)
        return baseColor;

    vec3 lifted = baseColor + colorGradeLift.rgb;
    vec3 graded = pow(max(lifted, vec3(0.0)), max(colorGradeGamma.rgb, vec3(0.001)));
    graded *= colorGradeGain.rgb;

    float contrast = exposureParams.z;
    float saturation = exposureParams.w;
    vec3 contrasted = mix(vec3(0.5), graded, contrast);

    float luminance = computeLuminance(contrasted);
    vec3 saturated = mix(vec3(luminance), contrasted, saturation);
    return saturate(saturated);
}

vec3 applyFilmGrain(vec3 baseColor, vec2 uv)
{
    if (togglesB.w < 0.5 || grainParams.x <= 0.0)
        return baseColor;

    vec2 seed = uv * resolutionParams.xy + vec2(grainParams.w);
    float noise = fract(sin(dot(seed + vec2(grainParams.z), vec2(12.9898, 78.233))) * 43758.5453);
    float intensity = grainParams.x;
    float response = grainParams.y;
    float luminance = computeLuminance(baseColor);
    float grain = (noise - 0.5) * intensity * (1.0 + (1.0 - luminance) * response);
    return saturate(baseColor + grain);
}

vec3 applyExposure(vec3 baseColor)
{
    float exposure = exposureParams.x;
    float gamma = max(exposureParams.y, 0.001);
    vec3 mapped = baseColor * pow(2.0, exposure);
    mapped = max(mapped, vec3(0.0));
    mapped = pow(mapped, vec3(1.0 / gamma));
    return mapped;
}

void main()
{
    vec2 uv = TexCoord;

    vec3 sceneColor = texture(uSceneColor, uv).rgb;

    sceneColor = applyBloom(sceneColor, uv);
    sceneColor = applyLensFlare(sceneColor, uv);
    sceneColor = applyChromaticAberration(sceneColor, uv);
    sceneColor = applyDepthOfField(sceneColor, uv);
    sceneColor = applyMotionBlur(sceneColor, uv);
    sceneColor = applyVignette(sceneColor, uv);
    sceneColor = applyColorGrading(sceneColor);
    sceneColor = applyFilmGrain(sceneColor, uv);

    sceneColor = applyExposure(sceneColor);
    sceneColor = saturate(sceneColor);

    FragColor = vec4(sceneColor, 1.0);
}
