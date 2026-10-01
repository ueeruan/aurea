#version 450
// =============================================================================
//  Aurea / shaders / scene3d / ground.frag
//
//  O chão do grupo 3D: dielétrico PBR (difusa de Lambert + GGX das luzes +
//  IBL split-sum), com
//    - a sombra da luz principal (common/shadow.glsl, o MESMO PCSS dos modelos);
//    - a sombra de CONTATO (mapa visto de baixo, desfocado — Ground.cpp):
//      o "assentado" do carro mesmo sem luz que projete sombra;
//    - o reflexo PLANAR (a cena espelhada no plano, desfocada pela
//      rugosidade), com Fresnel; onde não há objeto refletido, o reflexo do
//      ambiente (especular pré-filtrado);
//    - desbotamento radial até o horizonte: o chão some no fundo sem borda.
//
//  Modo 1 (visível): a superfície inteira, alfa = desbotamento.
//  Modo 2 (shadow catcher): transparente — só a sombra (preto com alfa) e o
//  reflexo planar (luz pré-multiplicada) — compõe sobre um fundo 2D.
//
//  Saída no MRT do passe 3D (como o pbr.frag): 0 = só o alfa, 1 = a cena
//  linear HDR codificada para o resolve (common/hdr.glsl), pré-multiplicada.
// =============================================================================
#include "common/ground.glsl"
#include "common/hdr.glsl"

layout(location = 0) in vec3 v_world;

layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_scene;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_reflection;   // reflexo planar (pré-mult., codificado)
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D t_contact;      // oclusão de contato desfocada (r)
layout(set = 0, binding = AUREA_TEX5) uniform samplerCube t_irradiance;
layout(set = 0, binding = AUREA_TEX6) uniform samplerCube t_prefilter;
layout(set = 0, binding = AUREA_TEX7) uniform sampler2D t_brdf;
layout(set = 0, binding = AUREA_TEX8) uniform sampler2DShadow t_shadow;
layout(set = 0, binding = AUREA_TEX11) uniform sampler2D t_shadowDepth;

#include "common/shadow.glsl"

const float PI = 3.14159265359;

vec3 env_dir(vec3 worldDir) {
    vec3 d = vec3(worldDir.x, -worldDir.y, -worldDir.z);
    float r = g.envParams.w;
    if (r == 0.0) return d;
    float c = cos(r), s = sin(r);
    return vec3(c * d.x + s * d.z, d.y, -s * d.x + c * d.z);
}

vec3 analytic_env(vec3 dir) {
    float up = -dir.y;
    vec3 horizon = mix(g.groundColor.rgb, g.skyColor.rgb, 0.5);
    return up >= 0.0 ? mix(horizon, g.skyColor.rgb, sqrt(clamp(up, 0.0, 1.0)))
                     : mix(horizon, g.groundColor.rgb, sqrt(clamp(-up, 0.0, 1.0)));
}

float D_GGX(float NdotH, float a) {
    float a2 = a * a;
    float f = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / (PI * f * f);
}

float V_SmithGGXCorrelated(float NdotV, float NdotL, float a) {
    float a2 = a * a;
    float gv = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float gl = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1e-5);
}

// 1 = livre, 0 = ocluído pelo que está logo acima do chão.
float contact_visibility() {
    if (g.contactParams.x < 0.5) return 1.0;
    vec4 c = g.contactMatrix * vec4(v_world, 1.0);
    vec2 uv = c.xy / c.w;
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0) return 1.0;
    float occ = textureLod(t_contact, uv, 0.0).r;
    return 1.0 - g.params.z * clamp(occ * 1.8, 0.0, 1.0);
}

void main() {
    const vec3 N = vec3(0.0, -1.0, 0.0);   // "para cima"
    vec3 V = normalize(g.cameraPos.xyz - v_world);
    // Visto de baixo (câmera sob o chão): nada.
    if (dot(N, V) <= 0.0) discard;
    float NdotV = clamp(dot(N, V), 1e-4, 1.0);
    float roughness = clamp(g.params.x, 0.02, 1.0);
    float a = roughness * roughness;
    float reflectivity = clamp(g.params.y, 0.0, 1.0);
    int mode = int(g.color.w + 0.5);

    // Desbotamento radial (até o horizonte): alfa 1 perto dos modelos, 0 no raio final.
    float dist = length(v_world.xz - g.center.xz);
    float fade = 1.0 - smoothstep(g.viewport.w, g.params.w, dist);
    if (fade <= 0.0) discard;

    // --- Oclusões ---------------------------------------------------------------
    float contact = contact_visibility();
    int shadowLight = int(g.shadowParams.w + 0.5);

    // --- Luz direta ---------------------------------------------------------------
    vec3 albedo = g.color.rgb;
    // F0 do dielétrico: 4% (verniz/piso polido) — a "refletividade" escala o
    // quanto do lobo especular existe (0 = chão fosco, sem reflexo).
    const float F0 = 0.04;
    vec3 direct = vec3(0.0);
    vec3 directSpec = vec3(0.0);
    float keyLit = 1.0;
    int lights = int(g.lightCount.x + 0.5);
    for (int i = 0; i < GROUND_MAX_LIGHTS; ++i) {
        if (i >= lights) break;
        vec4 lp = g.lightPos[i];
        int type = int(lp.w + 0.5);
        vec3 L;
        float atten = 1.0;
        if (type == 0) {
            L = normalize(lp.xyz);
        } else {
            vec3 toL = lp.xyz - v_world;
            float d2 = max(dot(toL, toL), 1e-4);
            L = toL * inversesqrt(d2);
            atten = 1.0 / d2;
            float range = g.lightColor[i].w;
            if (range > 0.0) {
                float r = sqrt(d2) / range;
                float w = clamp(1.0 - r * r * r * r, 0.0, 1.0);
                atten *= w * w;
            }
            if (type == 2) {
                float cd = dot(-L, normalize(g.lightSpot[i].xyz));
                float outer = g.lightSpot[i].w, inner = g.lightSpot2[i].x;
                float t = clamp((cd - outer) / max(inner - outer, 1e-4), 0.0, 1.0);
                atten *= t * t;
            }
        }
        float NdotL = clamp(dot(N, L), 0.0, 1.0);
        if (NdotL <= 0.0 || atten <= 0.0) continue;
        float sh = 1.0;
        if (i == shadowLight) {
            sh = aurea_shadow(t_shadow, t_shadowDepth, g.shadowMatrix, g.shadowParams, g.shadowParams2,
                              v_world, N, L, gl_FragCoord.xy);
            sh = mix(1.0, sh, g.lightSpot2[i].y);
            keyLit = sh;
        }
        vec3 radiance = g.lightColor[i].rgb * (NdotL * atten * sh);
        vec3 H = normalize(L + V);
        float NdotH = clamp(dot(N, H), 0.0, 1.0);
        float VdotH = clamp(dot(V, H), 0.0, 1.0);
        float F = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
        directSpec += radiance * (F * D_GGX(NdotH, a) * V_SmithGGXCorrelated(NdotV, NdotL, a)) * reflectivity;
        direct += radiance * (1.0 - F * reflectivity) * albedo / PI;
    }

    // --- Ambiente e reflexo ---------------------------------------------------------
    vec3 R = reflect(-V, N);
    vec2 dfg = texture(t_brdf, vec2(NdotV, roughness)).rg;
    float specW = (F0 * dfg.x + dfg.y) * reflectivity;
    vec3 irradiance, envSpec;
    if (g.envParams.y > 0.5) {
        irradiance = texture(t_irradiance, env_dir(N)).rgb;
        float r = roughness;
        float lod = max(g.envParams.z - 1.0, 0.0) * r * (1.7 - 0.7 * r);
        envSpec = textureLod(t_prefilter, env_dir(R), lod).rgb;
    } else {
        irradiance = analytic_env(N);
        envSpec = analytic_env(R);
        specW = (F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0)) * reflectivity;
    }
    irradiance *= g.envParams.x;
    envSpec *= g.envParams.x;
    // Reflexo planar: a cena espelhada na MESMA projeção — o texel é o do pixel.
    vec4 planar = vec4(0.0);
    if (g.viewport.z > 0.5) {
        planar = textureLod(t_reflection, gl_FragCoord.xy * g.viewport.xy, 0.0);
        planar.rgb = aurea_hdr_decode(planar.rgb);
        planar.a = clamp(planar.a, 0.0, 1.0);
    }

    // Oclusão de contato: no ambiente (difuso e reflexo do céu) inteira; na
    // luz direta pela metade (a sombra do mapa já cuida da direta).
    float shadowVis = min(keyLit, contact);

    if (mode == 2) {
        // Shadow catcher: preto com alfa = sombra; reflexo planar somado como luz.
        float shadowA = clamp(1.0 - shadowVis, 0.0, 1.0) * max(g.params.z, 0.6);
        vec3 refl = planar.rgb * specW;
        float reflA = clamp(planar.a * specW, 0.0, 1.0);
        float alpha = 1.0 - (1.0 - shadowA) * (1.0 - reflA);
        alpha *= fade;
        refl *= fade;
        vec3 color = refl * g.cameraPos.w;
        o_color = vec4(0.0, 0.0, 0.0, alpha);
        o_scene = vec4(aurea_hdr_encode(color), alpha);
        return;
    }

    vec3 reflection = planar.rgb + envSpec * contact * (1.0 - planar.a);
    vec3 diffuse = (irradiance * albedo * contact) * (1.0 - specW) + direct * mix(1.0, contact, 0.5);
    vec3 color = diffuse + reflection * specW + directSpec;
    color *= g.cameraPos.w * fade;   // pré-multiplicado pelo desbotamento
    o_color = vec4(0.0, 0.0, 0.0, fade);
    o_scene = vec4(aurea_hdr_encode(color), fade);
}
