#version 450
// =============================================================================
//  Aurea / shaders / effects / turbulence_types.frag
//
//  A Turbulência com tipo de deslocamento, ciclo de evolução ou fixação nova.
//  O caso de sempre (Turbulento, sem ciclo, fixações 0..5) continua no
//  turbulence_displace.frag, INTOCADO: o mesmo SPIR-V é o que garante que um
//  projeto antigo desenhe bit a bit igual em qualquer GPU — mexer naquele
//  arquivo muda o que o driver funde (fma) e o quadro muda no último bit.
//  Mesmos uniforms; a CPU escolhe o shader.
//
//  O deslocamento é medido em pixels da LAYER; `texel.zw` traz texels por
//  pixel de layer, então o mesmo número vale no preview reduzido e no 4K.
//
//  Tipos (p3.z): 0 Turbulento (o campo de sempre), 1 Protuberância (ao longo
//  do GRADIENTE do ruído), 2 Torção (o rotacional: gira sem acumular), 3..5 os
//  mesmos três com interpolação quíntica e oitavas mais fracas, 6 só vertical,
//  7 só horizontal, 8 cruzado (X vem de um campo que só varia em Y, e
//  vice-versa).
//
//  Ciclo de evolução (p3.w = revoluções, 0 = desligado): o tempo vira um eixo
//  de rede que se repete a cada N células; uma revolução = uma célula. O laço
//  fecha exato, sem misturar dois campos.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;   // x = intensidade (px da layer), y = tamanho do ruído (px), z = complexidade (oitavas), w = evolução (graus)
    vec4 p1;   // x = deslocamento X (px), y = deslocamento Y (px), z = semente, w = 1 só na horizontal
    vec4 p2;   // x = borda (0 repetir, 1 recortar, 2 esticar), y = girar o vetor (graus), z = mistura, w = fixar bordas
    vec4 p3;   // x = quadro local, y = complexidade, z = tipo, w = ciclo em revoluções (0 = sem ciclo)
    vec4 p4;   // x = tempo no ciclo, em células, já dobrado em [0, ciclo) na CPU
} p;

// Curva da rede e sua derivada, em (valor, derivada). A cúbica é a de sempre;
// a quíntica não deixa vinco na derivada nas paredes das células — e a
// Protuberância e a Torção SÃO a derivada.
vec2 turb_fade(float t, bool quintic) {
    if (quintic) return vec2(t * t * t * (t * (t * 6.0 - 15.0) + 10.0), 30.0 * t * t * (t * (t - 2.0) + 1.0));
    return vec2(t * t * (3.0 - 2.0 * t), 6.0 * t * (1.0 - t));
}

// Valor de um canto da rede. Sem ciclo é o mesmo hash do aurea_value_noise;
// com ciclo, a fatia do tempo (já dobrada no período) entra no hash.
float turb_corner(ivec2 c, float s0, float s1, float tz, bool timed) {
    const uint h = aurea_hash_u(uvec2(c));
    if (!timed) return float(h & 0xFFFFFFu) / 16777216.0;
    const float a = float(aurea_hash_u(uvec2(h, uint(s0))) & 0xFFFFFFu) / 16777216.0;
    const float b = float(aurea_hash_u(uvec2(h, uint(s1))) & 0xFFFFFFu) / 16777216.0;
    return mix(a, b, tz);
}

// Ruído de valor com o gradiente: (valor, d/dx, d/dy) em unidades de célula.
// period > 0 liga o eixo do tempo `z`, periódico em `period` células.
vec3 turb_noise(vec2 x, float z, float period, bool quintic) {
    const vec2 i = floor(x);
    const vec2 f = x - i;
    const vec2 fx = turb_fade(f.x, quintic);
    const vec2 fy = turb_fade(f.y, quintic);
    const bool timed = period > 0.0;
    float s0 = 0.0, s1 = 0.0, tz = 0.0;
    if (timed) {
        const float zi = floor(z);
        tz = turb_fade(z - zi, quintic).x;
        s0 = mod(zi, period);
        s1 = mod(zi + 1.0, period);
    }
    const ivec2 c = ivec2(i);
    const float a = turb_corner(c, s0, s1, tz, timed);
    const float b = turb_corner(c + ivec2(1, 0), s0, s1, tz, timed);
    const float e = turb_corner(c + ivec2(0, 1), s0, s1, tz, timed);
    const float g = turb_corner(c + ivec2(1, 1), s0, s1, tz, timed);
    return vec3(mix(mix(a, b, fx.x), mix(e, g, fx.x), fy.x),
                fx.y * mix(b - a, g - e, fy.x),
                fy.y * mix(e - a, g - b, fx.x));
}

// A crista do Turbulento (|2n - 1|), já centrada em [-1, 1].
float turb_ridge(vec2 q, float z, float period, bool quintic) {
    return abs(turb_noise(q, z, period, quintic).x * 2.0 - 1.0) * 2.0 - 1.0;
}

// O vetor de UMA oitava. `evo` é o círculo da evolução livre (zero em ciclo).
vec2 turb_octave(int type, vec2 q, vec2 evo, float z, float period, bool quintic) {
    if (type == 1 || type == 2 || type == 4 || type == 5) {
        const vec2 g = turb_noise(q + evo, z, period, quintic).yz;
        // Protuberância: os pixels descem a encosta do campo e incham nos
        // picos. Torção: o mesmo gradiente girado 90° — sem divergência, roda.
        return (type == 1 || type == 4) ? g : vec2(-g.y, g.x);
    }
    vec2 qx = q, qy = q + vec2(31.7, 17.3);
    if (type == 8) {
        // Cruzado: X lido numa coluna fixa (só varia com Y) e Y numa linha fixa.
        qx = vec2(0.37, q.y);
        qy = vec2(q.x, 17.91);
    }
    vec2 v = vec2(0.0);
    if (type != 6) v.x = turb_ridge(qx + evo, z, period, quintic);
    if (type != 7) v.y = turb_ridge(qy + evo, z, period, quintic);
    return v;
}

// Campo dos tipos novos (e do Turbulento em ciclo): média das oitavas como no
// Turbulento de sempre, com o comprimento preso em 1 — nunca passa da
// intensidade, que é a margem que o efeito declara.
vec2 turb_field(int type, vec2 q, float phase, float complexity, float cycle, float cycleZ) {
    const bool quintic = type >= 3 && type <= 5;
    const float gain = quintic ? 0.4 : 0.5;
    const bool timed = cycle > 0.0;
    const vec2 evo = timed ? vec2(0.0) : vec2(cos(phase), sin(phase)) * 1.7;
    float z = timed ? cycleZ : 0.0;
    float period = timed ? cycle : 0.0;
    vec2 sum = vec2(0.0);
    float weight = 1.0, total = 0.0;
    for (int i = 0; i < 6; ++i) {
        const float contribution = clamp(complexity - float(i), 0.0, 1.0) * weight;
        if (contribution > 0.0) {
            sum += turb_octave(type, q, evo, z, period, quintic) * contribution;
            total += contribution;
        }
        weight *= gain;
        q *= 2.0;
        // A oitava mais fina ferve duas vezes mais rápido; a rede dela repete
        // em duas vezes mais células, então o laço continua fechado.
        z *= 2.0;
        period *= 2.0;
    }
    const vec2 v = sum / max(total, 1e-6);
    const float len = length(v);
    return len > 1.0 ? v / len : v;
}

// Fixações novas (6..10), a 2x a intensidade da borda da camada. As sem trava
// só seguram o movimento QUE ATRAVESSA a borda (o pixel ainda desliza ao longo
// dela); as travadas seguram as duas direções.
vec2 turb_pin(int pin, vec2 d, vec2 uv, vec2 uvPerLayer, float amount) {
    const vec2 edge = min(uv, 1.0 - uv) / uvPerLayer;   // px da camada até a borda
    const float fade = max(2.0 * amount, 1e-3);
    const float wx = smoothstep(0.0, fade, edge.x);
    const float wy = smoothstep(0.0, fade, edge.y);
    if (pin == 6) d.y *= wy;             // bordas horizontais (em cima e embaixo)
    else if (pin == 7) d.x *= wx;        // bordas verticais (esquerda e direita)
    else if (pin == 8) d *= min(wx, wy); // todas travadas
    else if (pin == 9) d *= wy;          // horizontais travadas
    else if (pin == 10) d *= wx;         // verticais travadas
    return d;
}

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    // Unidades de uv por PIXEL DA LAYER: com isto, um deslocamento em pixels
    // da layer vira o mesmo número de uv no preview e no export.
    const vec2 uvPerLayer = max(vec2(p.texel.x * p.texel.z, p.texel.y * p.texel.w), vec2(1e-9));

    // Mesmo espaço de ruído do turbulence_displace.frag: célula de `tamanho`
    // px da camada, deslocamento e semente somados antes.
    const float sizePx = max(p.p0.y, 1.0);
    const float phase = radians(p.p0.w);
    const vec2 layerPx = inUv / uvPerLayer + p.p1.xy;
    const vec2 seed = vec2(p.p1.z * 13.7, p.p1.z * 7.3);
    const vec2 q = (layerPx + seed) / sizePx;
    const float complexity = clamp(p.p0.z, 1.0, 6.0);
    const int type = int(p.p3.z + 0.5);
    const float cycle = p.p3.w;
    vec2 d;
    if (type == 0 && cycle <= 0.0) {
        // Turbulento sem ciclo (aqui só com fixação nova): o campo de sempre.
        vec2 qo = q;
        const vec2 evolution = vec2(cos(phase), sin(phase)) * 1.7;
        vec2 noise = vec2(0.0);
        float weight = 1.0, total = 0.0;
        for (int i = 0; i < 6; ++i) {
            const float contribution = clamp(complexity - float(i), 0.0, 1.0) * weight;
            noise += vec2(aurea_turbulence(qo + evolution, 1, .5),
                          aurea_turbulence(qo + evolution + vec2(31.7, 17.3), 1, .5)) * contribution;
            total += contribution;
            weight *= .5;
            qo *= 2.0;
        }
        d = (noise / max(total, 1e-6) - .5) * (2.0 * p.p0.x) * uvPerLayer;
    } else {
        d = turb_field(type, q, phase, complexity, cycle, p.p4.x) * p.p0.x * uvPerLayer;
    }
    // Fixações de sempre (1..5): o vetor inteiro some a 12% da borda.
    const int pin = int(p.p2.w + .5);
    float edge = 1.0;
    if (pin == 1) edge = min(min(inUv.x, 1.0 - inUv.x), min(inUv.y, 1.0 - inUv.y));
    if (pin == 2) edge = inUv.x;
    if (pin == 3) edge = 1.0 - inUv.x;
    if (pin == 4) edge = inUv.y;
    if (pin == 5) edge = 1.0 - inUv.y;
    if (pin >= 1 && pin <= 5) d *= smoothstep(0.0, .12, edge);
    if (p.p1.w > 0.5) d.y = 0.0;
    if (abs(p.p2.y) > 1e-4) d = aurea_rot2(radians(p.p2.y)) * d;
    // As fixações novas vêm depois do giro: a borda segura o vetor FINAL.
    if (pin >= 6) d = turb_pin(pin, d, inUv, uvPerLayer, p.p0.x);

    const vec2 uv = inUv + d;
    const int mode = int(p.p2.x + 0.5);
    // O tratamento de borda é sobre a COORDENADA DA ENTRADA: fora do [0,1] da
    // textura é que não há pixel para ler.
    vec2 sampleUv = uv;
    vec4 outc;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        if (mode == 1) {
            outc = vec4(0.0);
        } else if (mode == 2) {
            sampleUv = clamp(uv, 0.0, 1.0);
            outc = unpremultiply(texture(u_tex0, sampleUv));
        } else {
            outc = unpremultiply(texture(u_tex0, fract(uv)));
        }
    } else {
        outc = unpremultiply(texture(u_tex0, uv));
    }

    // A borda transparente do modo recortar vale para o alfa também: a camada
    // não pode "crescer" só na cor.
    o_color = mix(texture(u_tex0,inUv),premultiply(vec4(max(outc.rgb, vec3(0.0)), outc.a)),clamp(p.p2.z,0.0,1.0));
}
