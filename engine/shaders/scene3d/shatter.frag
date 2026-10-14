#version 450
// =============================================================================
//  Aurea / shaders / scene3d / shatter.frag
//
//  Uma face de uma peça do 3D Shatter dentro da cena: o pedaço da camada (no
//  uv dele), com profundidade de verdade. Camada que aceita luzes: as luzes da
//  composição pela normal da peça (a mesma conta do plane.frag). Sem luz: a
//  "luz nas faces" do efeito, presa à orientação original da camada — peça
//  parada sai igual ao plano sem o efeito.
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;   // x = opacidade; y = 0 opacos com depth, 1 transparência
} pc;

#define PLANE_MAX_LIGHTS 8
layout(set = 0, binding = AUREA_PARAMS, std140) uniform ShatterBlock {
    mat4 worldFromLayer;
    vec4 cameraPos;
    vec4 ambient;
    vec4 lightCount;
    vec4 lightPos[PLANE_MAX_LIGHTS];
    vec4 lightColor[PLANE_MAX_LIGHTS];
    vec4 lightSpot[PLANE_MAX_LIGHTS];
    vec4 lightSpot2[PLANE_MAX_LIGHTS];
    vec4 src;
    vec4 dst;
    vec4 grid;
    vec4 motion;
    vec4 shape;
    vec4 view;     // w = luz nas faces (0..1)
    vec4 misc;
} u;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada (pré-multiplicada, linear)

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec3 v_world;
layout(location = 2) in vec3 v_normal;
layout(location = 3) flat in vec2 v_info;
layout(location = 4) in vec3 v_layerNormal;

layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_scene;

vec3 scene_light(vec3 P, vec3 N) {
    vec3 sum = u.ambient.rgb;
    int count = int(u.lightCount.x + 0.5);
    for (int i = 0; i < PLANE_MAX_LIGHTS; ++i) {
        if (i >= count) break;
        int type = int(u.lightPos[i].w + 0.5);
        vec3 L;
        float atten = 1.0;
        if (type == 0) {
            L = normalize(u.lightPos[i].xyz);
        } else {
            vec3 d = u.lightPos[i].xyz - P;
            float dist = max(length(d), 1e-3);
            L = d / dist;
            float range = u.lightColor[i].w;
            if (range > 0.0) {
                float r = dist / range;
                float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
                atten = w * w;
            }
            if (type == 2) {
                float cd = dot(-L, normalize(u.lightSpot[i].xyz));
                float outer = u.lightSpot[i].w, inner = u.lightSpot2[i].x;
                float t = clamp((cd - outer) / max(inner - outer, 1e-4), 0.0, 1.0);
                atten *= t * t;
            }
        }
        sum += u.lightColor[i].rgb * (max(dot(N, L), 0.0) * atten);
    }
    return sum;
}

void main() {
    vec4 c = texture(u_tex0, v_uv) * (pc.params.x * v_info.x);
    const int face = int(v_info.y + 0.5);
    if (u.cameraPos.w > 0.5) {
        vec3 N = normalize(v_normal);
        if (dot(N, u.cameraPos.xyz - v_world) < 0.0) N = -N;   // a face vista
        c.rgb *= scene_light(v_world, N);
    } else {
        // Luz de cima/esquerda/frente da camada (a do desenho 2D do efeito).
        const vec3 L = normalize(vec3(0.35, -0.45, 1.0));
        const float diffuse = abs(dot(normalize(v_layerNormal), L));
        float shade = mix(1.0, clamp(0.3 + 0.7 * diffuse / L.z, 0.0, 1.25), u.view.w);
        if (face == 1) shade *= mix(1.0, 0.7, u.view.w);
        if (face == 2) shade *= mix(1.0, 0.8, u.view.w);
        c.rgb *= shade;
    }
    if (pc.params.y < 0.5) {
        if (c.a < 0.999) discard;
    } else {
        if (c.a <= 0.000001 || c.a >= 0.999) discard;
    }
    o_color = c;
    o_scene = vec4(0.0, 0.0, 0.0, c.a);
}
