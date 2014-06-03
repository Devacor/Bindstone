#version 330 core

// Input vertex data, different for all executions of this shader.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

uniform mat4 transformation;

smooth out vec4 color;

void main(){
    //gl_Position = vec4(inPosition.x, inPosition.y, inPosition.z, 0);
    color = inColor;
    gl_Position.xyz = inPosition;
    gl_Position.w = 1.0;
}