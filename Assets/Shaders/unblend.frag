#version 330 core

smooth in vec4 color;
smooth in vec2 uv;

uniform sampler2D texture;

out vec4 colorResult;
 
void main(){
	vec4 textureColor = texture2D(texture, uv.st);

	textureColor/=sqrt(textureColor.a);
	textureColor.rgb*=color.a;

    colorResult = textureColor * color;
}