#version 330 core

in vec4 color;
in vec2 uv;
in vec2 worldPosition;

uniform sampler2D texture0;
// A Sprite Lamp normal map: red is +x (image right), green +y (image up), blue toward the viewer.
uniform sampler2D texture1;
uniform float normalMapped;
uniform float alpha;

// Render::Lighting::LIGHTS_PER_DRAW
const int LIGHTS_PER_DRAW = 16;
uniform float lightCount;
uniform vec4 lightPositions[LIGHTS_PER_DRAW]; // x, y, height above the plane, radius
uniform vec4 lightColors[LIGHTS_PER_DRAW];    // colour times intensity, falloff exponent
uniform vec3 ambient;

out vec4 colorResult;

// The map's normal turned into the world. Where u and v run there comes from the screen derivatives, so a mirrored or
// turned sprite turns its normals with it; the explicit gradients let a pixel no light reaches skip the read.
vec3 surfaceNormal(vec2 a_uvAcross, vec2 a_uvDown, mat2 a_worldFromScreen) {
    mat2 uvFromScreen = mat2(a_uvAcross, a_uvDown);
    if (abs(determinant(uvFromScreen)) < 1e-12) {
        return vec3(0.0, 0.0, 1.0);
    }
    vec3 mapped = textureGrad(texture1, uv, a_uvAcross, a_uvDown).rgb * 2.0 - 1.0;
    mat2 worldFromUV = a_worldFromScreen * inverse(uvFromScreen);
    vec2 uDirection = normalize(worldFromUV[0]);
    vec2 vDirection = normalize(worldFromUV[1]);
    // v runs down the image, so the map's up is -v.
    return normalize(vec3(mapped.x * uDirection - mapped.y * vDirection, mapped.z));
}

void main(){
    vec2 uvAcross = dFdx(uv);
    vec2 uvDown = dFdy(uv);
    mat2 worldFromScreen = mat2(dFdx(worldPosition), dFdy(worldPosition));
    vec3 normal = vec3(0.0, 0.0, 1.0);
    bool normalKnown = normalMapped < 0.5;
    vec3 light = ambient;
    int count = int(lightCount + 0.5);
    for (int i = 0; i < LIGHTS_PER_DRAW; ++i) {
        if (i >= count) {
            break;
        }
        vec2 offset = lightPositions[i].xy - worldPosition;
        float reach = 1.0 - length(offset) / lightPositions[i].w;
        if (reach > 0.0) {
            if (!normalKnown) {
                normal = surfaceNormal(uvAcross, uvDown, worldFromScreen);
                normalKnown = true;
            }
            vec3 toLight = normalize(vec3(offset, lightPositions[i].z));
            light += lightColors[i].rgb * max(dot(normal, toLight), 0.0) * pow(reach, lightColors[i].a);
        }
    }

    vec4 texel = texture(texture0, uv) * color;
    colorResult = vec4(texel.rgb * light, texel.a);
    colorResult.rgb *= colorResult.a;
    colorResult *= alpha;
}
