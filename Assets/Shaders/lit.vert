#version 330 core

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

uniform mat4 transformation;
// Clip space to the render world (x, y scale, then x, y offset): the space the lights' positions are in.
uniform vec4 worldFromClip;

out vec4 color;
out vec2 uv;
out vec2 worldPosition;

void main(){
    color = inColor;
    uv = inUV;

    gl_Position = transformation * vec4(inPosition, 1.0);
    worldPosition = worldFromClip.xy * (gl_Position.xy / gl_Position.w) + worldFromClip.zw;
    gl_Position.z = 0.0;
}
