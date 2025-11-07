#version 430 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec4 aTangent;
layout (location = 4) in vec2 aTexCoords1;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord0;
out vec2 TexCoord1;
out vec3 Tangent;
out vec3 Bitangent;
// Tangent-space vectors for parallax mapping (LearnOpenGL tutorial)
out vec3 TangentLightPos;
out vec3 TangentViewPos;
out vec3 TangentFragPos;

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

uniform bool uWorldCurvatureEnabled;
uniform float uWorldCurvatureStrength;

layout(std140, binding = 4) uniform ObjectDataBlock {
    mat4 model;
    mat4 normalMatrix;
    ivec4 materialFlags;
    ivec4 textureUsage;
    ivec4 textureUsage2;
    ivec4 uvSets0;
    ivec4 uvSets1;
} uObject;

void main()
{
    mat3 normalMatrix3 = mat3(uObject.normalMatrix);
    FragPos = vec3(uObject.model * vec4(aPos, 1.0));
    Normal = normalize(normalMatrix3 * aNormal);
    vec3 T = normalize(normalMatrix3 * aTangent.xyz);
    T = normalize(T - Normal * dot(Normal, T));
    vec3 B = normalize(cross(Normal, T)) * aTangent.w;
    Tangent = T;
    Bitangent = B;
    TexCoord0 = aTexCoords;
    TexCoord1 = aTexCoords1;
    
    // Construct TBN matrix for tangent-space transformation (LearnOpenGL tutorial)
    mat3 TBN = transpose(mat3(T, B, Normal));
    vec3 lightPos = uFrame.lightPos.xyz;
    vec3 viewPos = uFrame.cameraPos.xyz;
    TangentLightPos = TBN * lightPos;
    TangentViewPos = TBN * viewPos;
    TangentFragPos = TBN * FragPos;
    
    // Apply optional world curvature in view space before projection
    vec4 posView = uFrame.view * vec4(FragPos, 1.0);
    if (uWorldCurvatureEnabled) {
        float fragmentDist = length(posView.xyz);
        float curved = posView.y - uWorldCurvatureStrength * fragmentDist * fragmentDist;
        posView.y = curved;
    }
    gl_Position = uFrame.projection * posView;
}
