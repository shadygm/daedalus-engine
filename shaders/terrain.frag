#version 430 core

in VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
} fs_in;

out vec4 FragColor;

uniform vec3 lightPos;
uniform vec3 lightColor;
uniform vec3 ambientColor;
uniform float ambientStrength;
uniform vec3 cameraPos;

uniform bool uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform float uFogGradient;

const vec3 kBaseColor = vec3(0.35, 0.8, 0.4);

void main()
{
    vec3 N = normalize(fs_in.normal);
    vec3 L = normalize(lightPos - fs_in.worldPos);
    vec3 V = normalize(cameraPos - fs_in.worldPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 32.0);

    vec3 ambient = ambientStrength * ambientColor;
    vec3 diffuse = diff * lightColor;
    vec3 specular = 0.1 * spec * lightColor;

    vec3 color = kBaseColor * (ambient + diffuse) + specular;
    vec4 outCol = vec4(color, 1.0);
    if (uFogEnabled) {
        float dist = length(cameraPos - fs_in.worldPos);
        float vis = exp(-pow(dist * uFogDensity, uFogGradient));
        vis = clamp(vis, 0.0, 1.0);
        outCol.rgb = mix(uFogColor, outCol.rgb, vis);
    }
    FragColor = outCol;
}
