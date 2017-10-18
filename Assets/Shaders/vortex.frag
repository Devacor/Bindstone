#version 330 core

in vec4 color;
in vec2 uv;

uniform float time;

uniform sampler2D texture0;
uniform sampler2D texture1;

out vec4 colorResult;
 
float pulse(float sec, float duration) {
    float pulsePercent = mod(sec, duration) / duration;
    float pulseInTermsOfPI = (pulsePercent * 2.0f * 3.1415f) - 3.1415f;
    return (sin(pulseInTermsOfPI) + 1.0f) / 2.0f;
}

void main(){
	vec2 rotatedUv = uv;
	float totalDistance = distance(vec2(.5f, .5f), uv) * 2.0f;
	float amount = 65.0f + (1.0f - (totalDistance * 2.0f));
    float rot = radians((mod(time, 100.0f) + 150.0f) * amount);
    float rot2 = rot * 1.5f;
    rotatedUv-=.5f;
    
    mat2 m = mat2(cos(rot), -sin(rot), sin(rot), cos(rot));
    vec2 rotatedUv2 = m * m * rotatedUv;
    mat2 m2 = mat2(cos(rot2), -sin(rot2), sin(rot2), cos(rot2));
    vec2 rotatedUv3 = m2 * rotatedUv;
   	rotatedUv  = m * rotatedUv;
    
    rotatedUv+=.5f;
    rotatedUv2+=.5f;
    rotatedUv3+=.5f;

    colorResult = texture(texture0, rotatedUv3);
	colorResult.rgb *= colorResult.a;

	vec4 colorResult1 = texture(texture0, rotatedUv);
	colorResult1.rgb *= colorResult1.a;

	vec4 colorResult2 = texture(texture1, rotatedUv2);
	colorResult2.rgb *= colorResult2.a;

	colorResult.rgb = ((abs((colorResult2).rgb - (colorResult).rgb) * .25f) + (colorResult2.rgb * .35f) + (colorResult.rgb * .25f)) - colorResult1.rgb * .35f;
	colorResult.a = (colorResult.a * .5f) + (colorResult2.a * .5f) - totalDistance;
}
