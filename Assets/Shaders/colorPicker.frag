#version 330 core

in vec4 color;
in vec2 uv;

out vec4 colorResult;

void main(){
	vec4 colorCopy = color;
	colorCopy.a = 1.0;

    float uvX = 1.0 - uv.st.x;
    float uvY = uv.st.y;

    vec4 white = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 black = vec4(0.0, 0.0, 0.0, 1.0);

 	colorResult = mix(mix(colorCopy, white, uvX), black, uvY) * color.a;
}
