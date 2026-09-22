#version 450
// =============================================================================
//  Aurea / shaders / particles / particles.vert
//
//  Partículas ANALÍTICAS: cada instância é um "slot" que emite em sequência
//  (nascimento = slot/taxa + k·período). Posição, vida, tamanho e cor saem de
//  uma conta fechada sobre (semente, slot, geração k, tempo) — sem estado de
//  simulação. Consequência: determinístico (prévia = export), seek instantâneo
//  em qualquer ponto, nenhum checkpoint a guardar.
//
//  Espaço: pixels da camada, y para baixo; gravidade já convertida no C++.
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
} pc;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 emit;      // taxa (/s), vida (s), velocidade (px/s), espalhamento (rad)
    vec4 force;     // gravidade x, y (px/s², y para baixo), tamanho inicial, final
    vec4 look;      // opacidade inicial, final, direção (rad), semente
    vec4 area;      // emissor largura, altura, tempo (s), nº de slots
    vec4 colorA;    // cor inicial (linear, reta)
    vec4 colorB;    // cor final
    vec4 origin;    // centro do emissor (px da camada), _, _
} p;

layout(location = 0) out vec2 v_local;
layout(location = 1) out vec4 v_color;

// Quatro cantos + índices (0,1,2 / 1,3,2): com o desenho INDEXADO a GPU
// reaproveita os vértices repetidos da instância — 4 execuções deste shader
// por partícula em vez de 6 (8E: o vértice é o custo das partículas).
const vec2 kCorners[4] = vec2[4](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));

uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float rnd(inout uint s) { s = pcg(s); return float(s) * (1.0 / 4294967296.0); }

void main() {
    const vec2 c = kCorners[gl_VertexIndex];
    const float rate = max(p.emit.x, 1e-3);
    const float slots = max(p.area.w, 1.0);
    const float t = p.area.z;
    const float first = float(gl_InstanceIndex) / rate;
    const float period = slots / rate;
    // Longe da tela = não desenha (sem ramificar o rasterizador).
    gl_Position = vec4(4.0, 4.0, 0.0, 1.0);
    v_local = vec2(0.0);
    v_color = vec4(0.0);
    if (t < first) return;
    const float k = floor((t - first) / period);
    const float birth = first + k * period;
    const float age = t - birth;

    uint s = pcg(uint(gl_InstanceIndex) * 9781u ^ pcg(uint(k) * 6271u ^ uint(p.look.w)));
    const float life = p.emit.y * (0.75 + 0.5 * rnd(s));
    if (age >= life) return;
    const float ang = p.look.z + (rnd(s) - 0.5) * p.emit.w;
    const float spd = p.emit.z * (0.7 + 0.6 * rnd(s));
    const vec2 start = p.origin.xy + (vec2(rnd(s), rnd(s)) - 0.5) * p.area.xy;
    const vec2 vel = spd * vec2(cos(ang), sin(ang));
    const vec2 pos = start + vel * age + 0.5 * p.force.xy * age * age;

    const float u = age / life;
    const float size = max(mix(p.force.z, p.force.w, u), 0.0);
    const float op = clamp(mix(p.look.x, p.look.y, u), 0.0, 1.0);
    const vec4 col = mix(p.colorA, p.colorB, u);
    const float a = col.a * op;
    v_color = vec4(col.rgb * a, a);   // pré-multiplicado
    v_local = c * 2.0 - 1.0;
    gl_Position = pc.clipFromLayer * vec4(pos + (c - 0.5) * size, 0.0, 1.0);
}
