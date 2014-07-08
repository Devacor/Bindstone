#version 330 core

smooth in vec4 color;
smooth in vec2 uv;

uniform sampler2D texture;

out vec4 colorResult;
 
void main(){
	vec4 premultipliedColor = color;
	premultipliedColor *= color.a;
    colorResult = texture2D(texture, uv.st) * premultipliedColor;
}