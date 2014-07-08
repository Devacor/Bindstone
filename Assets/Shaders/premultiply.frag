#version 330 core

smooth in vec4 color;
smooth in vec2 uv;

uniform sampler2D texture;

out vec4 colorResult;
 
void main(){
    colorResult = texture2D(texture, uv.st) * color;
    colorResult.rgb *= colorResult.a;
}