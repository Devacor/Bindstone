#version 330 core

in vec4 color;
in vec2 uv;

uniform float time;

uniform sampler2D texture0;

out vec4 colorResult;
 
void main(){
    const float speed1x = 0.111f;
    const float speed1 = 0.17257f;
    
    const float speed2x = 0.111f;
    const float speed2 = 0.17f;
    
    const float speed3 = 0.35f;
    
	vec2 scrolledUV = vec2(fract(uv.s - time * speed1x), fract(uv.t - time * speed1));

    colorResult = texture(texture0, scrolledUV) * color;
    colorResult.rgb *= colorResult.a;

    scrolledUV = vec2(fract(uv.s + time * speed2x), fract(uv.t - time * speed2));

    vec4 colorResult2 = texture(texture0, scrolledUV) * color;
    colorResult2.rgb *= colorResult2.a;

    scrolledUV = vec2(uv.s, fract(uv.t - time * speed3));

    vec4 colorResult3 = texture(texture0, scrolledUV) * color;
    colorResult3.rgb *= colorResult3.a;

    colorResult *= .25f;
    colorResult2 *= .25f;
    colorResult3 *= .5f;

    colorResult += colorResult2;
    colorResult += colorResult3;
}
