#version 450 core
layout (location = 0) in vec3 aPos;

out vec3 LocalPos;

uniform mat4 projection;
uniform mat4 view;

void main()
{
    LocalPos = aPos;
    gl_Position = projection * view * vec4(aPos, 1.0);
}
