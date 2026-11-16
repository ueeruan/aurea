#version 450
// =============================================================================
//  Aurea / shaders / effects / card_reference.frag
//
//  One face of a Card Dance / Card Wipe card: the layer (front) or the back
//  layer, lit like the native CAMLIGHT model: ambient + diffuse (N.L) and a
//  reflection highlight (R.V)^2 ^ (sharpness / 2) that can raise alpha.
//  The native shading works on straight colour and clamps each channel to 1;
//  the output here is premultiplied, like every layer image.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_front;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_back;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 layer;
    vec4 region;
    vec4 inputMap;
    vec4 mode;
    vec4 camera[4];
    vec4 eye;
    vec4 light;
    vec4 ambient;
    vec4 diffuse;
    vec4 specular;
    vec4 wipe0;
    vec4 wipe1;
    vec4 wipe2;
    vec4 jitterPos;
    vec4 jitterPosSpeed;
    vec4 jitterRot;
    vec4 jitterRotSpeed;
    vec4 channel[8];
    vec4 order;
} p;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_world;
layout(location = 2) flat in vec4 v_normal;
layout(location = 0) out vec4 o_color;

void main() {
    const vec4 front = texture(u_front, v_uv);
    const vec4 other = texture(u_back, v_uv);
    const bool useOther = v_normal.w > 0.5 && p.mode.y > 1.5;
    const vec4 s = useOther ? other : front;
    float a = clamp(s.a, 0.0, 1.0);
    vec3 straight = a > 0.0 ? s.rgb / a : vec3(0.0);

    const vec3 N = normalize(v_normal.xyz);
    const vec3 L = p.light.w > 0.5 ? normalize(p.light.xyz - v_world) : p.light.xyz;
    const float ndl = dot(L, N);
    vec3 rgb;
    if (ndl <= 0.0) {
        rgb = straight * p.ambient.rgb;
    } else {
        rgb = (ndl * p.diffuse.rgb + p.ambient.rgb) * straight;
        if (p.ambient.w > 0.5) {
            const vec3 V = p.eye.xyz - v_world;
            const vec3 R = (ndl + ndl) * N - L;
            const float rv = dot(R, V);
            const float vv = dot(V, V);
            if (rv > 0.0 && vv > 0.0) {
                const float cos2 = clamp(rv * rv / vv, 0.0, 1.0);
                const float spec = pow(cos2, p.specular.w) * p.diffuse.w;
                if (spec <= 1.0) {
                    if (spec <= a) {
                        rgb += p.specular.rgb * (a > 0.0 ? spec / a : 0.0);
                    } else {
                        rgb = rgb * (a / spec) + p.specular.rgb;
                        a = spec;
                    }
                } else {
                    rgb = rgb * a + p.specular.rgb * spec;
                    a = 1.0;
                }
            }
        }
    }
    rgb = min(rgb, vec3(1.0));
    o_color = vec4(rgb * a, a);
}
