#version 330 core

// Input vertex data, different for all executions of this shader.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

uniform mat4 transformation;
uniform float time;

smooth out vec4 color;
smooth out vec2 uv;

float pulse(float sec) {
    float pulsePercent = mod(sec, 4.5f) / 4.5f;
    float pulseInTermsOfPI = (pulsePercent * 2.0f * 3.1415f) - 3.1415f;
    return (sin(pulseInTermsOfPI) + 1) / 3 + .25f;
}

void main(){
    color = inColor;
    color.a = fract(color.a * pulse(time));
    uv = inUV;

    vec4 v = vec4(inPosition, 1.0);
    v.x += sin(time) * inUV.y * 10.0f * cos(time * 0.25f) * (sin(time * 1.0f) * 0.5f);
    v.y += cos(time) * inUV.y * 10.0f * cos(time * 0.5f);
    gl_Position = transformation * v;
}