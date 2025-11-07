#version 450 core

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

flat in int vFaceIndex[];
in vec3 vWorldPos[];

out gl_PerVertex {
    vec4 gl_Position;
};

out vec3 gWorldPos;

void main()
{
    const int layer = vFaceIndex[0];
    for (int i = 0; i < 3; ++i) {
        gl_Layer = layer;
        gl_Position = gl_in[i].gl_Position;
        gWorldPos = vWorldPos[i];
        EmitVertex();
    }
    EndPrimitive();
}
