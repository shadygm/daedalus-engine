const float PI = 3.14159265359;

vec3 srgbToLinear(vec3 c)
{
    return pow(c, vec3(2.2));
}


vec3 decodeNormal(vec3 encoded)
{
    return normalize(encoded * 2.0 - 1.0);
}

vec2 applyUvTransform(vec2 uv, vec2 offset, vec2 scale, float rotation)
{
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

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / max(denom, 0.0001);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float denom = NdotV * (1.0 - k) + k;
    return NdotV / max(denom, 0.0001);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
