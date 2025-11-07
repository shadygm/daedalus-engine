#version 450 core

layout(triangles) in;
layout(triangle_strip, max_vertices = 24) out;

const int MAX_SHADOW_LIGHTS = 8;

struct ShadowUniform {
    mat4 matrix;
    vec4 params;
};

layout(std140, binding = 0) uniform ShadowMatrices {
    ShadowUniform uShadowData[MAX_SHADOW_LIGHTS];
};

in vec3 vsWorldPos[];
out vec3 vWorldPos;

uniform int uLayeredPass;
uniform int uShadowLayerCount;
uniform int uIsPointLight;
uniform mat4 uPointLightViewProj;

void emitTri(mat4 mvp, int layer, bool writeLayer)
{
    for (int i = 0; i < 3; ++i) {
        vec4 world = vec4(vsWorldPos[i], 1.0);
        vWorldPos = vsWorldPos[i];
        gl_Position = mvp * world;
        if (writeLayer)
            gl_Layer = layer;
        EmitVertex();
    }
    EndPrimitive();
}

void main()
{
    if (uIsPointLight != 0) {
        emitTri(uPointLightViewProj, 0, false);
        return;
    }

    int layerCount = min(uShadowLayerCount, MAX_SHADOW_LIGHTS);
    if (layerCount <= 0)
        return;

    if (uLayeredPass == 0) {
        emitTri(uShadowData[0].matrix, 0, false);
        return;
    }

    for (int layer = 0; layer < layerCount; ++layer)
        emitTri(uShadowData[layer].matrix, layer, true);
}
