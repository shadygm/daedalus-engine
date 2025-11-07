#version 410 core
layout (location = 0) in vec3 aPos;

out vec3 WorldDir;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    // Pass cube direction to the fragment shader
    WorldDir = aPos;

    // Remove translation from the view matrix so the cube stays centered on the camera
    mat4 rotView = mat4(mat3(view));

    // Project to clip space as an infinite background (xyww trick)
    vec4 clipPos = projection * rotView * vec4(aPos, 1.0);
    gl_Position = clipPos.xyww;
}
