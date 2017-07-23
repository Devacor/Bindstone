#version 330 core

smooth in vec4 color;
smooth in vec2 uv;

uniform float time;

uniform sampler2D texture;
uniform sampler2D texture0;

uniform float speed1 = 0.75f;
uniform float speed2 = 0.25f;

//uniform vec2 uvPercentSize = vec2(1.0f, 1.0f);
//uniform vec2 uvOffset;

out vec4 colorResult;
 
float pulse(float sec, float duration) {
    float pulsePercent = mod(sec, duration) / duration;
    float pulseInTermsOfPI = (pulsePercent * 2.0f * 3.1415f) - 3.1415f;
    return (sin(pulseInTermsOfPI) + 1) / 2;
}

void main(){
	vec2 rotatedUv = uv;
	float totalDistance = distance(vec2(.5f, .5f), uv) * 2.0f;
	float amount = 65.0f + (1.0f - (totalDistance * 2.0f));
    float rot = radians((mod(time, 100.0f) + 150.0f) * amount);
    float rot2 = rot * 1.5f;
    rotatedUv-=.5;
    
    mat2 m = mat2(cos(rot), -sin(rot), sin(rot), cos(rot));
    vec2 rotatedUv2 = m * m * rotatedUv;
    mat2 m2 = mat2(cos(rot2), -sin(rot2), sin(rot2), cos(rot2));
    vec2 rotatedUv3 = m2 * rotatedUv;
   	rotatedUv  = m * rotatedUv;
    
    rotatedUv+=.5;
    rotatedUv2+=.5;
    rotatedUv3+=.5;

    colorResult = texture2D(texture, rotatedUv3);
	colorResult.rgb *= colorResult.a;

	vec4 colorResult1 = texture2D(texture, rotatedUv);
	colorResult1.rgb *= colorResult1.a;

	vec4 colorResult2 = texture2D(texture0, rotatedUv2);
	colorResult2.rgb *= colorResult2.a;

	colorResult.rgb = ((abs((colorResult2).rgb - (colorResult).rgb) * .25f) + (colorResult2.rgb * .35f) + (colorResult.rgb * .25f)) - colorResult1.rgb * .35f;
	colorResult.a = (colorResult.a * .5f) + (colorResult2.a * .5f) - totalDistance;
}
