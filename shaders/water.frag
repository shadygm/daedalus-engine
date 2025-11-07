#version 450 core

in VS_OUT {
    vec3 worldPos;
    vec3 worldNormal;
} v_in;

layout(location = 0) out vec4 FragColor;

uniform vec3 u_lightPos;
uniform vec3 u_lightColor;
uniform vec3 u_ambientColor;
uniform float u_ambientStrength;
uniform vec3 u_cameraPos;

uniform vec3 u_waterColor;
uniform float u_opacity;
uniform float u_specStrength;
uniform float u_shininess;

// Fresnel & tint
uniform float u_fresnelStrength; // scales Schlick fresnel
uniform vec3  u_shallowColor;
uniform vec3  u_deepColor;
uniform float u_depthRange;     // meters

// Detail normal maps
uniform bool u_detailEnabled;
uniform sampler2D u_detailNormal1;
uniform sampler2D u_detailNormal2;
uniform float u_tile1;
uniform float u_tile2;
uniform vec2 u_dir1;
uniform vec2 u_dir2;
uniform float u_speed1;
uniform float u_speed2;
uniform float u_strength1;
uniform float u_strength2;
uniform float u_detailBlend;
uniform float u_time;

void main() {
    vec3 N = normalize(v_in.worldNormal);
    
    // Add detail normal maps if enabled
    if (u_detailEnabled) {
        // Scrolling UVs based on world position
        vec2 baseUV = v_in.worldPos.xz;
        vec2 uv1 = baseUV * u_tile1 + u_time * u_dir1 * u_speed1;
        vec2 uv2 = baseUV * u_tile2 + u_time * u_dir2 * u_speed2;
        
        // Sample and decode normal maps (tangent space: Z-up)
        vec3 detail1 = texture(u_detailNormal1, uv1).xyz * 2.0 - 1.0;
        vec3 detail2 = texture(u_detailNormal2, uv2).xyz * 2.0 - 1.0;
        
        // Apply individual strengths and combine (creates interference as they scroll opposite directions)
        vec3 detailCombined = detail1 * u_strength1 + detail2 * u_strength2;
        
        // Create tangent-space to world-space basis aligned with Gerstner normal
        vec3 up = vec3(0.0, 1.0, 0.0);
        vec3 T = normalize(cross(up, N));
        if (length(T) < 0.001) T = vec3(1.0, 0.0, 0.0);
        vec3 B = normalize(cross(N, T));
        
        // Transform detail perturbation from tangent space to world space
        // We want the XY components to perturb the normal, Z reinforces it
        vec3 detailPerturbation = T * detailCombined.x + B * detailCombined.y;
        
        // Apply detail blend to add perturbation to the Gerstner normal
        N = normalize(N + detailPerturbation * u_detailBlend);
    }
    vec3 V = normalize(u_cameraPos - v_in.worldPos);
    vec3 L = normalize(u_lightPos - v_in.worldPos);
    vec3 H = normalize(L + V);

    float NdotL = max(dot(N, L), 0.0);
    float NdotH = max(dot(N, H), 0.0);

    vec3 ambient = u_ambientColor * u_ambientStrength;

    // Depth tint: shallow to deep
    float depthT = clamp((u_cameraPos.y - v_in.worldPos.y) / max(0.001, u_depthRange), 0.0, 1.0);
    vec3 tint = mix(u_shallowColor, u_deepColor, depthT);

    vec3 diffuse = tint * NdotL * u_lightColor;

    // Schlick Fresnel with base F0 = 0.04
    float NdotV = max(dot(N, V), 0.0);
    float schlick = pow(1.0 - NdotV, 5.0);
    float Fscalar = clamp(schlick * u_fresnelStrength, 0.0, 1.0);
    vec3  F = mix(vec3(0.04), vec3(1.0), Fscalar);

    // Specular term (white) scaled by Fresnel
    float specBRDF = (NdotL > 0.0) ? pow(NdotH, u_shininess) : 0.0;
    vec3 spec = specBRDF * u_specStrength * F;

    // Fake reflection lift at glancing angles for stronger visual response
    vec3 base = ambient + diffuse;
    vec3 fakeReflection = mix(tint, vec3(1.0), 0.7);
    vec3 color = mix(base, fakeReflection, Fscalar) + spec;

    FragColor = vec4(color, clamp(u_opacity, 0.0, 1.0));
}
