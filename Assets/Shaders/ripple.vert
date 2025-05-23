#version 330 core

// Input vertex data, different for all executions of this shader.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

uniform mat4 transformation;
uniform float time;

out vec4 color;
out vec2 uv;

float pulse(float sec) {
    float pulsePercent = mod(sec, 5.0f) / 5.0f;
    float pulseInTermsOfPI = (pulsePercent * 2.0f * 3.1415f) - 3.1415f;
    return (sin(pulseInTermsOfPI) + 1) / 2;
}

void main(){
    color = inColor;
    color.a = fract(color.a * pulse(time));
    uv = inUV;

    vec4 v = vec4(inPosition, 1.0);
    v.x += sin(time) * inUV.y * 10.0f * cos(time * 0.25f) * (sin(time * 1.0f) * 0.5f);
    v.y += cos(time) * inUV.y * 10.0f * cos(time * 0.5f);
    gl_Position = transformation * v;
    gl_Position.z = 0.0;
}
