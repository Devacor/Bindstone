#version 330 core

in vec4 color;
in vec2 uv;

uniform float time;

uniform sampler2D texture0;

out vec4 colorResult;

float pulse(float sec, float duration) {
    float pulsePercent = mod(sec, duration) / duration;
    float pulseInTermsOfPI = (pulsePercent * 2.0f * 3.1415f) - 3.1415f;
    return (sin(pulseInTermsOfPI) + 1.0f) / 2.0f;
}
 
void main(){
    float percent = fract(time * .5f);
    int offset1 = 0;
    if(percent < .25f) {
    	offset1 = 0;
    } else if (percent < .5f) {
    	offset1 = 1;
    	percent -= .25f;
    } else if (percent < .75f) {
    	offset1 = 2;
    	percent -= .5f;
    } else {
    	offset1 = 3;
    	percent -= .75f;
    }
    percent *= 4.0f;
    int offset2 = offset1 + 1;
    if(offset2 > 3){
    	offset2 = 0;
    }

    const int frames = 4;
    vec4 textureColors[frames];

    vec2 uvOffsets[frames];
    uvOffsets[0] = vec2(fract(uv.s + pulse(time, 1.56f) * .035f), uv.t);
    uvOffsets[1] = vec2(fract(uv.s - pulse(time, 1.56f) * .035f), uv.t);
    uvOffsets[2] = uvOffsets[0];
    uvOffsets[3] = uvOffsets[1];

    textureColors[0] = (texture(texture0, vec2((uvOffsets[0].s / 2.0f), uvOffsets[0].t / 2.0f)) * color);
    textureColors[0].rgb *= textureColors[0].a;
    textureColors[1] = (texture(texture0, vec2((uvOffsets[1].s / 2.0f) + .5f, (uvOffsets[1].t / 2.0f))) * color);
	textureColors[1].rgb *= textureColors[1].a;

	textureColors[2] = (texture(texture0, vec2((uvOffsets[2].s / 2.0f), (uvOffsets[2].t / 2.0f) + .5f)) * color);
    textureColors[2].rgb *= textureColors[2].a;
    textureColors[3] = (texture(texture0, vec2((uvOffsets[3].s / 2.0f) + .5f, (uvOffsets[3].t / 2.0f))) * color);
	textureColors[3].rgb *= textureColors[3].a;

    colorResult = (textureColors[offset2] * percent) + (textureColors[offset1] * (1.0f - percent));
}
