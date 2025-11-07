#version 430 core

in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D uImage;
uniform bool uHorizontal;
uniform vec2 uTexel;

const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main()
{
    vec2 offset = uHorizontal ? vec2(uTexel.x, 0.0) : vec2(0.0, uTexel.y);
    vec3 result = texture(uImage, TexCoord).rgb * weights[0];
    for (int i = 1; i < 5; ++i) {
        result += texture(uImage, TexCoord + offset * float(i)).rgb * weights[i];
        result += texture(uImage, TexCoord - offset * float(i)).rgb * weights[i];
    }
    FragColor = vec4(result, 1.0);
}
