#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / volume.frag
//
//  Neblina volumétrica com raios de luz (0.4.6, "realismo nível Blender").
//  Meia resolução. Para cada pixel, percorre o raio da câmera até o objeto
//  (profundidade do pré-passe, Z reverso) em `cfg.w` passos com deslocamento
//  por pixel (IGN). Em cada passo a neblina absorve (Beer-Lambert) e espalha:
//    - a luz do sol, SÓ onde o mapa de sombra diz que o sol chega — entre
//      galhos e frestas isso vira os feixes ("god rays") — com a fase de
//      Henyey-Greenstein (forte olhando para o sol);
//    - a luz ambiente (cor da neblina), uniforme.
//  Saída: RGB = luz espalhada (linear, mesma escala da cena), A = transmissão.
// =============================================================================
#include "../../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_vol;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_depth;    // pré-passe (Z reverso), meia res
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D t_shadow;   // mapa de sombra cru (Z comum)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    mat4 worldFromClip;   // inversa de viewProj (com a transformação da imagem)
    mat4 shadowMatrix;    // (uv, profundidade) do mapa ← mundo
    vec4 camera;          // xyz posição; w = distância máxima da marcha (céu)
    vec4 sun;             // xyz direção PARA o sol; w = 1 com mapa de sombra
    vec4 sunColor;        // rgb cor × intensidade do sol; w = força dos raios
    vec4 fog;             // rgb cor da neblina (luz ambiente espalhada); w = densidade (por px do mundo)
    vec4 cfg;             // x = anisotropia g, y = viés da sombra, z = 1/tamanho do mapa, w = passos
} p;

const float PI = 3.14159265359;

float phase_hg(float cosTheta, float g) {
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * PI * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

float sun_visible(vec3 x) {
    if (p.sun.w < 0.5) return 1.0;
    vec4 s = p.shadowMatrix * vec4(x, 1.0);
    vec3 uvz = s.xyz / s.w;
    if (any(lessThan(uvz.xy, vec2(0.0))) || any(greaterThan(uvz.xy, vec2(1.0))) || uvz.z >= 1.0) return 1.0;
    float occ = textureLod(t_shadow, uvz.xy, 0.0).r;
    return uvz.z <= occ + p.cfg.y ? 1.0 : 0.0;
}

void main() {
    float d = textureLod(t_depth, v_uv, 0.0).r;
    // Céu (profundidade 0): marcha até a distância máxima na mesma direção.
    vec4 w = p.worldFromClip * vec4(v_uv * 2.0 - 1.0, max(d, 1e-6), 1.0);
    vec3 P = w.xyz / w.w;
    vec3 C = p.camera.xyz;
    vec3 ray = P - C;
    float dist = length(ray);
    vec3 dir = ray / max(dist, 1e-6);
    float L = d <= 0.0 ? p.camera.w : min(dist, p.camera.w);

    int steps = int(p.cfg.w + 0.5);
    float dt = L / float(steps);
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float density = p.fog.w;
    float ph = phase_hg(dot(dir, p.sun.xyz), p.cfg.x);
    vec3 sunLight = p.sunColor.rgb * ph * 4.0 * PI * p.sunColor.w;   // 4π: a fase integra a 1
    vec3 ambient = p.fog.rgb;

    float T = 1.0;
    vec3 inscatter = vec3(0.0);
    float stepT = exp(-density * dt);
    for (int i = 0; i < steps; ++i) {
        float t = (float(i) + noise) * dt;
        vec3 x = C + dir * t;
        vec3 lum = ambient + sunLight * sun_visible(x);
        // Espalhamento integrado no passo (Hillaire 2015): estável com passo grande.
        inscatter += T * lum * (1.0 - stepT);
        T *= stepT;
    }
    o_vol = vec4(inscatter, T);
}
