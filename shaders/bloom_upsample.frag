#version 430 core

in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D uSource;
uniform vec2 uTexelSize;
uniform float uFilterRadius;

vec3 sampleSource(vec2 texelOffset)
{
    return texture(uSource, TexCoord + texelOffset * uTexelSize * uFilterRadius).rgb;
}

void main()
{
    vec3 result = vec3(0.0);

    result += sampleSource(vec2( 0.0,  0.0)) * 4.0;

    result += sampleSource(vec2( 1.0,  0.0)) * 2.0;
    result += sampleSource(vec2(-1.0,  0.0)) * 2.0;
    result += sampleSource(vec2( 0.0,  1.0)) * 2.0;
    result += sampleSource(vec2( 0.0, -1.0)) * 2.0;

    result += sampleSource(vec2( 1.0,  1.0)) * 1.0;
    result += sampleSource(vec2(-1.0,  1.0)) * 1.0;
    result += sampleSource(vec2( 1.0, -1.0)) * 1.0;
    result += sampleSource(vec2(-1.0, -1.0)) * 1.0;

    FragColor = vec4(result * (1.0 / 16.0), 1.0);
}
