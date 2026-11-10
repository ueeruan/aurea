#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / ssr.frag
//
//  Reflexos em espaço de tela (0.4.6, "realismo nível Blender"), meia
//  resolução. Da profundidade do pré-passe (Z reverso infinito) saem a
//  posição de vista e a normal (como no GTAO); o raio refletido marcha em
//  vista com passos que crescem, projeta cada ponto na tela e compara com a
//  profundidade dali. No acerto, 4 passos de bisseção refinam o ponto e a cor
//  vem da cena HDR já iluminada.
//
//  Saída: rgb = cor refletida × peso (linear), a = peso (Fresnel × lisura × borda).
//  Superfície áspera, céu ou raio que sai da tela = peso 0 (fica a
//  reflexão do ambiente do pbr.frag).
// =============================================================================
#include "../../common/bindings.glsl"
#include "../common/hdr.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_ssr;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D t_depth;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D t_material;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D t_scene;
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D t_holdout;   // objetos com efeitos, iluminados

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    mat4 proj;      // clip <- vista
    mat4 invProj;   // vista <- clip
    vec4 texel;     // xy = 1/tamanho, zw = tamanho (meia resolução)
    vec4 cfg;       // x = passos, y = espessura (fração da profundidade), z = alcance (× profundidade), w = força
    vec4 holdout;   // x > 0,5 = t_holdout vale
} p;

vec3 view_pos(vec2 uv, float d) {
    vec4 v = p.invProj * vec4(uv * 2.0 - 1.0, d, 1.0);
    return v.xyz / v.w;
}

vec3 view_at(vec2 uv) {
    return view_pos(uv, max(textureLod(t_depth, uv, 0.0).r, 1e-7));
}

vec2 to_uv(vec3 v) {
    vec4 c = p.proj * vec4(v, 1.0);
    return c.xy / c.w * 0.5 + 0.5;
}

bool off_screen(vec2 uv) {
    return any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)));
}

void main() {
    o_ssr = vec4(0.0);
    float d = textureLod(t_depth, v_uv, 0.0).r;
    if (d <= 0.0) return;
    vec2 mat = textureLod(t_material, v_uv, 0.0).rg;
    float rough = mat.r, metal = mat.g;
    if (rough > 0.65) return;

    vec3 P = view_pos(v_uv, d);
    vec2 tx = vec2(p.texel.x, 0.0), ty = vec2(0.0, p.texel.y);
    vec3 pl = view_at(v_uv - tx), pr = view_at(v_uv + tx);
    vec3 pu = view_at(v_uv - ty), pd = view_at(v_uv + ty);
    vec3 dx = abs(pr.z - P.z) < abs(P.z - pl.z) ? pr - P : P - pl;
    vec3 dy = abs(pd.z - P.z) < abs(P.z - pu.z) ? pd - P : P - pu;
    vec3 N = normalize(cross(dy, dx));
    vec3 V = normalize(-P);
    if (dot(N, V) < 0.0) N = -N;
    // Só superfície quase plana: em malha detalhada (parafusos, grade) a
    // normal da profundidade de meia resolução é ruído — fica o ambiente.
    float bend = max(max(abs(dot(pl - P, N)), abs(dot(pr - P, N))), max(abs(dot(pu - P, N)), abs(dot(pd - P, N))));
    if (bend > 0.004 * abs(P.z)) return;
    vec3 R = reflect(-V, N);
    // Raio que volta para a câmera quase nunca acha nada na tela.
    float toward = clamp(R.z * 2.0 + 1.0, 0.0, 1.0);
    if (toward <= 0.0) return;

    float zP = abs(P.z);
    float reach = p.cfg.z * zP;
    int steps = int(p.cfg.x + 0.5);
    float thick = p.cfg.y * zP;
    float noise = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));

    float tPrev = 0.0, tHit = -1.0;
    for (int i = 1; i <= steps; ++i) {
        float t = (float(i) - noise) / float(steps);
        t = t * t;
        vec3 X = P + R * (reach * t);
        if (X.z <= 0.0) break;
        vec2 uv = to_uv(X);
        if (off_screen(uv)) break;
        float sd = textureLod(t_depth, uv, 0.0).r;
        if (sd > 0.0) {
            vec3 S = view_pos(uv, sd);
            float diff = X.z - S.z;
            // O próprio plano do ponto (piso visto rasante: o texel vizinho
            // da meia resolução fica "na frente" do raio) não conta.
            bool samePlane = abs(dot(S - P, N)) < 0.01 * S.z;
            if (diff > 0.0 && diff < thick + reach * (t - tPrev) && !samePlane) { tHit = t; break; }
        }
        tPrev = t;
    }
    if (tHit < 0.0) return;
    // Bisseção entre o último ponto livre e o acerto.
    float a = tPrev, b = tHit;
    for (int k = 0; k < 4; ++k) {
        float m = 0.5 * (a + b);
        vec3 X = P + R * (reach * m);
        vec2 uv = to_uv(X);
        float sd = textureLod(t_depth, uv, 0.0).r;
        if (sd > 0.0 && X.z - view_pos(uv, sd).z > 0.0) b = m; else a = m;
    }
    vec2 huv = to_uv(P + R * (reach * b));
    if (off_screen(huv)) return;
    vec3 encoded = textureLod(t_scene, huv, 0.0).rgb;
    // Recorte: na cena o pixel é zero exato; a cor dele vem do alvo à parte.
    if (p.holdout.x > 0.5 && dot(encoded, encoded) <= 0.0) encoded = textureLod(t_holdout, huv, 0.0).rgb;
    vec3 hit = aurea_hdr_decode(encoded);
    hit = min(hit, vec3(16.0));   // sem vagalumes no bloom

    float NdotV = clamp(dot(N, V), 0.0, 1.0);
    float F0 = mix(0.04, 0.9, metal);
    float F = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
    float smooth_ = 1.0 - smoothstep(0.15, 0.65, rough);
    vec2 e = min(huv, 1.0 - huv);
    float edge = smoothstep(0.0, 0.08, min(e.x, e.y));
    float dist = smoothstep(1.0, 0.7, b);   // o fim do alcance some suave
    float w = clamp(F * smooth_ * edge * dist * toward * p.cfg.w, 0.0, 1.0);
    o_ssr = vec4(hit * w, w);   // pré-multiplicado: o filtro linear da subida não faz halo
}
