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
 
void main(){
	vec2 scrolledUV = vec2(uv.s, fract(uv.t - time * speed1));
	//scrolledUV = (scrolledUV * uvPercentSize) + uvOffset;

    colorResult = texture2D(texture, scrolledUV) * color;
    colorResult.rgb *= colorResult.a;

    scrolledUV = vec2(uv.s, fract(uv.t - time * speed2));
	//scrolledUV = (scrolledUV * uvPercentSize) + uvOffset;

    vec4 colorResult2 = texture2D(texture, scrolledUV) * color;
    colorResult2.rgb *= colorResult2.a;

    colorResult *= .75f;
    colorResult2 *= .25f;

    colorResult += colorResult2;
}
