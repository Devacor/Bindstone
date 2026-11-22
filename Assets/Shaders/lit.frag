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
uniform float lightInnerRadii[LIGHTS_PER_DRAW];
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
    vec4 albedo = texture(texture0, uv);
    vec4 texel = albedo * color;
    // What is painted near white is a painted glow already: light fades out on it rather than flattening it.
    float lightable = texel.a > 0.0 ? 1.0 - smoothstep(0.8, 1.0, max(max(albedo.r, albedo.g), albedo.b)) : 0.0;
    vec3 normal = vec3(0.0, 0.0, 1.0);
    bool normalKnown = normalMapped < 0.5;
    vec3 added = vec3(0.0);
    int count = lightable > 0.0 ? int(lightCount + 0.5) : 0;
    for (int i = 0; i < LIGHTS_PER_DRAW; ++i) {
        if (i >= count) {
            break;
        }
        vec2 offset = lightPositions[i].xy - worldPosition;
        float away = length(offset);
        float inner = lightInnerRadii[i];
        // Nothing inside the inner radius, easing up to full a quarter of it further out, then the fade to the radius.
        float reach = 1.0 - max(away - inner, 0.0) / max(lightPositions[i].w - inner, 0.001);
        if (reach > 0.0 && away > inner) {
            if (!normalKnown) {
                normal = surfaceNormal(uvAcross, uvDown, worldFromScreen);
                normalKnown = true;
            }
            float edge = inner > 0.0 ? smoothstep(inner, inner * 1.25, away) : 1.0;
            vec3 toLight = normalize(vec3(offset, lightPositions[i].z));
            added += lightColors[i].rgb * max(dot(normal, toLight), 0.0) * pow(reach, lightColors[i].a) * edge;
        }
    }

    vec3 base = texel.rgb * ambient;
    // A soft knee: the added light fills what is left below white and never reaches it, and adds nothing where none falls.
    vec3 headroom = max(1.0 - base, 0.0);
    vec3 lit = base + headroom * (1.0 - exp(-texel.rgb * added * lightable / max(headroom, 0.0001)));
    colorResult = vec4(lit, texel.a);
    colorResult.rgb *= colorResult.a;
    colorResult *= alpha;
}
