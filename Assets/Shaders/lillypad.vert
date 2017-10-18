#version 330 core

// Input vertex data, different for all executions of this shader.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

uniform mat4 transformation;
uniform float time;

uniform vec2 uvMin;
uniform vec2 uvMax;

out vec4 color;
out vec2 uv;

void main(){
    color = inColor;
    uv = inUV;

    vec2 percentUv = (inUV - uvMin) / (uvMax - uvMin);

    vec4 v = vec4(inPosition, 1.0f);

    float cTime1 = cos(time * 1.15f);
    float cTime2 = cos(time * 2.85f);

    float sTime1 = sin(time * 0.275f);
    float sTime2 = sin(time * 1.25f);

    v.y += (sTime1 * percentUv.x * 8.5f * cTime2);
    v.y -= (sTime2 * (1.0f - percentUv.x) * 12.5f * cTime1);

    v.x += (sTime1 * fract(percentUv.y - 0.25f) * 9.5f * cTime1);
    v.x += (sTime2 * fract(percentUv.y - 0.575f) * 4.5f * cTime2);
    
    gl_Position = transformation * v;
}
