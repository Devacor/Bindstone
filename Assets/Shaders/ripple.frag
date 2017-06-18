#version 330 core

smooth in vec4 color;
smooth in vec2 uv;

uniform float time;

uniform sampler2D texture;

uniform float speed1 = 0.75f;
uniform float speed2 = 0.25f;

//uniform vec2 uvPercentSize = vec2(1.0f, 1.0f);
//uniform vec2 uvOffset;

out vec4 colorResult;
 
float pulse(float sec) {
    float pulsePercent = mod(sec, 5.0f) / 5.0f;
    float pulseInTermsOfPI = (pulsePercent * 2.0f * 3.1415f) - 3.1415f;
    return (sin(pulseInTermsOfPI) + 1) / 2;
}

void main(){
    vec2 scrolledUV = vec2(uv.s, fract(uv.t - pulse(time)));
    //scrolledUV = (scrolledUV * uvPercentSize) + uvOffset;

    colorResult = texture2D(texture, scrolledUV) * color;
    colorResult.a = (colorResult.a * (1.0f - uv.y));
    colorResult.rgb *= colorResult.a;
}
