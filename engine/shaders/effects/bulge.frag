#version 450
// =============================================================================
//  Aurea / shaders / effects / bulge.frag
//
//  Bojo / puxada: um raio é remapeado em volta de um centro. `height` > 0
//  ESTUFA o meio para fora (lente de aumento, o centro da imagem cresce);
//  `height` < 0 CHUPA para dentro (pinça, o centro encolhe). Fora do raio a
//  imagem fica intacta, e a emenda é invisível porque a curva é contínua na
//  borda: em r = R, rp = R para qualquer `height`.
//
//  A conta é a do editor antigo, e ela carrega duas armadilhas que valem
//  lembrar, porque o erro era um texel solto no meio do quadro:
//
//   - `pow(0.0, e)` é INDEFINIDO em GLSL ES. O NaN que ele pode devolver
//     sobrevive ao teste do `dir` (0.0 * NaN ainda é NaN) e nenhuma comparação
//     pega NaN (todas dão falso), então ele chega no texture() e sorteia um
//     texel qualquer. O piso de 1e-4 na base é o que impede isso — e não
//     custa nada, porque rp ali deveria ser um fio de cabelo acima de zero.
//   - Em mediump, `r` colapsa: um fragmento a um texel do centro eleva ao
//     quadrado ~1e-8, abaixo do menor normal do formato, e um punhado de
//     pixels arredonda para r = 0 e lê o centro exato — um borrão chapado.
//     O highp (onde o aparelho tem) mantém a conta viva até ela parar de
//     importar.
// =============================================================================
#include "../common/bindings.glsl"
#include "common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;   // x = 1/largura da região, y = 1/altura — a razão y/x é w/h
    vec4 p0;      // x,y = centro em uv, z = raio (fração da altura), w = reservado
    vec4 p1;      // x = altura (-0.95 pinça .. 0.95 bojo), y = mistura
    vec4 p2;
    vec4 p3;
    vec4 color;
} p;

void main() {
    const vec2 inUv = v_uv * p.uvMap.xy + p.uvMap.zw;
    const vec2 c = p.p0.xy;
    // A razão de aspecto entra para o raio ser um CÍRCULO no quadro, não uma
    // elipse esticada com a largura: a distância é medida em unidades de
    // ALTURA (1 uv de altura = h pixels; 1 uv de largura = w pixels).
    const float aspect = max(p.texel.y / max(p.texel.x, 1e-9), 1e-9);

    vec2 d = inUv - c;
    d.x *= aspect;
    const float r = length(d);
    const float R = max(p.p0.z, 1e-4);

    vec2 uv = inUv;
    if (r < R) {
        const float pct = max(r / R, 1e-4);                 // 0 no centro, 1 na borda
        const float e = 1.0 + clamp(p.p1.x, -0.95, 0.95);   // >1 amplia, <1 pinça
        const float rp = R * pow(pct, e);                   // raio de ORIGEM
        const vec2 dir = (r > 1e-5) ? d / r : vec2(0.0);
        vec2 src = dir * rp;
        src.x /= aspect;                                    // volta para unidades de uv
        uv = c + src;
    }

    // O sampler é de borda transparente: o que sai do quadro some, como o
    // recorte do editor antigo — sem um `if` por amostra.
    const vec4 warped = texture(u_tex0, uv);
    const vec4 src = texture(u_tex0, inUv);
    o_color = mix(src, warped, clamp(p.p1.y, 0.0, 1.0));
}
