#version 450
// =============================================================================
//  Aurea / shaders / scene3d / plane.frag
//
//  Camada 2D no espaço 3D desenhada DENTRO da cena: a imagem da camada (com
//  efeitos, pré-multiplicada, linear) num plano com teste e escrita de
//  profundidade — ela passa por trás e pela frente dos modelos e das outras
//  camadas 3D de verdade, e se cruza com elas. Transparente não escreve
//  profundidade (não tapa o que está atrás).
//
//  LUZES ("aceita luzes" do After Effects): com luz na composição, a cor da
//  camada é multiplicada pelo Lambert da normal do plano (N·L) de cada luz —
//  cor × intensidade, janela do alcance, cone do spot — mais as luzes
//  ambiente. A face iluminada é a que a câmera vê (luz por trás = escuro).
//  Sem luz nenhuma (cameraPos.w = 0) a camada sai como no 2D.
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;   // x = opacidade; y = 0 opacos com depth, 1 transparência sem escrita de depth
} pc;

layout(location = 0) in vec2 v_uv;
// MRT do passe 3D: a camada é cor de EXIBIÇÃO (alvo 0) — não passa pelo tone
// map nem pelo bloom da cena, sai exatamente como no 2D; no alvo HDR (1) ela
// só tapa, pelo alfa, a cena que está atrás.
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_scene;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

#define PLANE_MAX_LIGHTS 8
// Espelho de scene3d::PlaneLightBlock (SceneRenderer.hpp).
layout(set = 0, binding = AUREA_PARAMS, std140) uniform PlaneLights {
    mat4 worldFromLayer;                 // mundo (px) ← px da camada
    vec4 cameraPos;                      // xyz = câmera; w = 1 iluminado, 0 sem luz
    vec4 ambient;                        // rgb = luzes ambiente somadas
    vec4 lightCount;                     // x = luzes ativas
    vec4 lightPos[PLANE_MAX_LIGHTS];     // xyz = posição ou direção PARA a luz; w = tipo (0 dir, 1 ponto, 2 spot)
    vec4 lightColor[PLANE_MAX_LIGHTS];   // rgb × intensidade; w = alcance (0 = infinito)
    vec4 lightSpot[PLANE_MAX_LIGHTS];    // xyz = direção do feixe; w = cos do cone externo
    vec4 lightSpot2[PLANE_MAX_LIGHTS];   // x = cos do cone interno
} u;

vec3 plane_light() {
    // Posição no mundo: o mesmo ponto que o layer.vert projeta (uvRect = 0..1).
    vec2 layerPos = pc.region.xy + v_uv * pc.region.zw;
    vec3 P = (u.worldFromLayer * vec4(layerPos, 0.0, 1.0)).xyz;
    vec3 n = cross(u.worldFromLayer[0].xyz, u.worldFromLayer[1].xyz);
    vec3 toCam = u.cameraPos.xyz - P;
    vec3 N = dot(n, n) > 1e-20 ? normalize(n) : normalize(toCam + vec3(0.0, 0.0, -1e-6));
    if (dot(N, toCam) < 0.0) N = -N;   // a face vista
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
                // A janela suave do alcance (a mesma dos modelos), sem 1/d².
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
    vec4 c = texture(u_tex0, v_uv) * pc.params.x;
    // Pré-multiplicada: a luz escala só a cor (o alfa é a cobertura).
    if (u.cameraPos.w > 0.5) c.rgb *= plane_light();
    // Alpha clip only for the depth-writing pass. Keep low-alpha fades in the
    // blended pass instead of cutting them off at a fixed visible threshold.
    if (pc.params.y < 0.5) {
        if (c.a < 0.999) discard;
    } else {
        if (c.a <= 0.000001 || c.a >= 0.999) discard;
    }
    o_color = c;
    o_scene = vec4(0.0, 0.0, 0.0, c.a);
}
