#version 330 core

smooth in vec4 color;
smooth in vec2 uv;

uniform float time;

uniform sampler2D texture;

uniform float duration;

out vec4 colorResult;
 
void main(){
    vec4 premultipliedColor = color;
    premultipliedColor *= color.a;

    colorResult = texture2D(texture, 
		vec2(uv.s, 
			mod(uv.t + mod(time, 1.0f / .25f) * .25f, 1.0f)
			)
		) * premultipliedColor;
}