#version 330 core

smooth in vec4 color;
smooth in vec2 uv;

uniform sampler2D texture;

out vec4 colorResult;
 
void main(){
	vec4 sampledColor = texture2D(texture, uv.st);
    colorResult = vec4(sampledColor.rgb * color.rgb, sampledColor.a);
}