#version 330 core

in vec4 color;
in vec2 uv;

uniform float time;

uniform sampler2D texture0;

out vec4 colorResult;
 
void main(){
    const float speed1 = 0.75f;
    const float speed2 = 0.25f;
    
	vec2 scrolledUV = vec2(uv.s, fract(uv.t - time * speed1));

    colorResult = texture(texture0, scrolledUV) * color;
    colorResult.rgb *= colorResult.a;

    scrolledUV = vec2(uv.s, fract(uv.t - time * speed2));

    vec4 colorResult2 = texture(texture0, scrolledUV) * color;
    colorResult2.rgb *= colorResult2.a;

    colorResult *= .75f;
    colorResult2 *= .25f;

    colorResult += colorResult2;
}
