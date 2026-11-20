#version 330 core

in vec4 color;
in vec2 uv;

uniform sampler2D texture0;
uniform float alpha;

out vec4 colorResult;
 
void main(){
    colorResult = texture(texture0, uv.st) * color * alpha;
}
