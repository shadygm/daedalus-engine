#version 450 core

layout(location = 0) in vec3 aPos;

uniform mat4 modelMatrix;

out vec3 vsWorldPos;

void main()
{
    vec4 worldPos = modelMatrix * vec4(aPos, 1.0);
    vsWorldPos = worldPos.xyz;
    gl_Position = worldPos;
}
