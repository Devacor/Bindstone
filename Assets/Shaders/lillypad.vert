#version 330 core

// Input vertex data, different for all executions of this shader.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

uniform mat4 transformation;
uniform float time;

smooth out vec4 color;
smooth out vec2 uv;

void main(){
    color = inColor;
    uv = inUV;

    vec4 v = vec4(inPosition, 1.0f);
    v.y += (sin(time * 0.25f) * inUV.x * 12.5f * cos(time * 3.0f));
    v.y -= (sin(time * 1.25f) * (1.0f - inUV.x) * 12.5f * cos(time * 1.0f));

    v.x += (sin(time * 0.3f) * fract(inUV.y - .5f) * 12.5f * sin(time * 2.0f));
    //v.x -= (sin(time * 0.8f) * (1.0f - inUV.y) * 6.5f * cos(time * 1.5f));
    //v.y += (sin(time) * (1.0f - inUV.x) * 12.5f * cos(time * 2.0f);
    //v.y += (cos(time * 2.5) * inUV.x * 6.5f * sin(time * 2.25));
    //v.y += (sin(time * 2.0) * (1.0f - inUV.x) * 8.0f * cos(time * 2.25));
    //v.y += (sin(time * 0.25f) * 0.6f);
    gl_Position = transformation * v;
}