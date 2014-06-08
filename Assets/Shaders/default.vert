#version 330 core

// Input vertex data, different for all executions of this shader.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

uniform mat4 transformation;

smooth out vec4 color;
smooth out vec2 uv;

void main(){
    color = inColor;
    uv = inUV;

    gl_Position = transformation * vec4(inPosition, 1.0);
}