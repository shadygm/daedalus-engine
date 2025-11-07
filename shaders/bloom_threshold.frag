#version 430 core

in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D uSceneColor;

layout(std140, binding = 5) uniform CameraEffectsSettings {
    vec4 togglesA;
    vec4 togglesB;
    vec4 exposureParams;
    vec4 bloomParams;
    vec4 lensFlareParams;
    vec4 chromaticParams;
    vec4 vignetteParams;
    vec4 dofParams;
    vec4 motionBlurParams;
    vec4 colorGradeLift;
    vec4 colorGradeGamma;
    vec4 colorGradeGain;
    vec4 grainParams;
    vec4 depthParams;
    vec4 resolutionParams;
};

float luminance(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

void main()
{
    vec3 hdr = texture(uSceneColor, TexCoord).rgb;
    float threshold = bloomParams.y;
    float softKnee = max(bloomParams.z, 1e-4);

    float l = luminance(hdr);
    float x = max(l - threshold, 0.0);
    float soft = x * (1.0 / softKnee);
    float weight = clamp(x / (x + softKnee), 0.0, 1.0);

    vec3 pass = hdr * max(soft, weight);
    FragColor = vec4(pass, 1.0);
}
