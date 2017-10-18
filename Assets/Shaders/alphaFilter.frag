#version 330 core

in vec4 color;
in vec2 uv;

uniform sampler2D texture0;

out vec4 colorResult;

uniform float alphaFilter;
 
void main(){
    vec4 premultipliedColor = color;
    premultipliedColor *= color.a;

    colorResult = texture(texture0, uv.st) * premultipliedColor;

    if ( colorResult.a <= alphaFilter )
        discard;
}
