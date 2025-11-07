#version 430 core

in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D uSource;
uniform vec2 uTexelSize;
uniform int uApplyKarisAverage;
uniform int uClampToMinimum;
uniform float uMinimumValue;

const vec3 kLuminanceWeights = vec3(0.2126, 0.7152, 0.0722);

vec3 sampleSource(vec2 texelOffset)
{
    vec3 color = texture(uSource, TexCoord + texelOffset * uTexelSize).rgb;
    if (uApplyKarisAverage != 0) {
        float luminance = max(dot(color, kLuminanceWeights), 0.0);
        color /= (1.0 + luminance);
    }
    return color;
}

void main()
{
    vec3 result = vec3(0.0);

    result += sampleSource(vec2( 0.0,  0.0)) * 1.0000;

    result += sampleSource(vec2( 1.0,  0.0)) * 0.5000;
    result += sampleSource(vec2(-1.0,  0.0)) * 0.5000;
    result += sampleSource(vec2( 0.0,  1.0)) * 0.5000;
    result += sampleSource(vec2( 0.0, -1.0)) * 0.5000;

    result += sampleSource(vec2( 1.0,  1.0)) * 0.2500;
    result += sampleSource(vec2(-1.0,  1.0)) * 0.2500;
    result += sampleSource(vec2( 1.0, -1.0)) * 0.2500;
    result += sampleSource(vec2(-1.0, -1.0)) * 0.2500;

    result += sampleSource(vec2( 2.0,  0.0)) * 0.1250;
    result += sampleSource(vec2(-2.0,  0.0)) * 0.1250;
    result += sampleSource(vec2( 0.0,  2.0)) * 0.1250;
    result += sampleSource(vec2( 0.0, -2.0)) * 0.1250;

    const float weightSum = 4.5;
    vec3 downsample = result / weightSum;

    if (uClampToMinimum != 0) {
        downsample = max(downsample, vec3(uMinimumValue));
    }

    FragColor = vec4(downsample, 1.0);
}
