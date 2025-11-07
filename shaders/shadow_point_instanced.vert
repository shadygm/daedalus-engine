#version 450 core

layout(location = 0) in vec3 aPos;

layout(std140, binding = 4) uniform PointShadowVPs {
    mat4 uViewProj[6];
};

uniform mat4 uModel;

out gl_PerVertex {
    vec4 gl_Position;
};

flat out int vFaceIndex;
out vec3 vWorldPos;

void main()
{
    vFaceIndex = gl_InstanceID;
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    gl_Position = uViewProj[vFaceIndex] * world;
}
