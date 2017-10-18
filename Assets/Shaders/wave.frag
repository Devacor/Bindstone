#version 330 core

in vec4 color;
in vec2 uv;

uniform float time;

uniform sampler2D texture0;

out vec4 colorResult;
 
float pulse(float sec) {
    float pulsePercent = mod(sec, 4.5f) / 4.5f;
    float pulseInTermsOfPI = (pulsePercent * 2.0f * 3.1415f) - 3.1415f;
    return (sin(pulseInTermsOfPI) + 1.0f) / 2.0f;
}

void main(){
    vec2 scrolledUV = vec2(uv.s, fract(uv.t - pulse(time)));

    colorResult = texture(texture0, scrolledUV) * color;
    colorResult.a = (colorResult.a * (1.0f - uv.y));
    colorResult.rgb *= colorResult.a;
}
