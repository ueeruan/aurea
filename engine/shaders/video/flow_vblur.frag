#version 450
// =============================================================================
//  Aurea / shaders / video / flow_vblur.frag
//
//  Desfoque pelo movimento do próprio vídeo (RSMB), no trecho que o obturador
//  cobre, centrado no quadro. Cada pixel recolhe as amostras cujo rastro passa
//  por ele, em vez de borrar cegamente ao longo do próprio vetor: o contorno de
//  um objeto rápido fica translúcido por cima do fundo, e o fundo parado não é
//  arrastado para dentro do objeto. Vetor abaixo da zona morta é ruído do
//  fluxo: a imagem parada continua nítida.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_img;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_flow;    // RG = px do nível base, por quadro da fonte
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_tiles;   // RG = maior vetor da vizinhança, já no obturador

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 blur;     // x = fração do quadro no obturador, yz = uv por pixel do nível base, w = amostras
    vec4 limits;   // x = comprimento máximo, y = zona morta (px do nível base), z = t do retime (0 = o próprio quadro)
} p;

vec2 motion(vec2 uv) {
    vec2 v = textureLod(u_flow, uv, 0.0).xy;
    if (v.x > 5000.0) return vec2(0.0);   // corte de cena (flow_pack.frag)
    // Quadro interpolado: o campo descreve o quadro A, então o vetor de quem
    // está aqui no instante t mora em p − t·v (uma iteração basta para o desfoque).
    if (p.limits.z > 0.0) v = textureLod(u_flow, uv - p.limits.z * v * p.blur.yz, 0.0).xy;
    v *= p.blur.x;
    const float len = length(v);
    if (len > p.limits.x) v *= p.limits.x / len;
    return v * smoothstep(p.limits.y, p.limits.y * 2.0, len);
}

// Até onde um rastro de comprimento `len` chega, com meia unidade de transição.
float reaches(float dist, float len) { return clamp(len * 0.5 - dist + 0.5, 0.0, 1.0); }

void main() {
    const vec2 far = textureLod(u_tiles, v_uv, 0.0).xy;
    const float farLen = length(far);
    const vec4 here = textureLod(u_img, v_uv, 0.0);
    if (farLen <= p.limits.y) {
        o_color = here;
        return;
    }
    const vec2 own = motion(v_uv);
    const float ownLen = length(own);
    const bool hasOwn = ownLen > p.limits.y;
    // Ruído entrelaçado: troca as faixas das poucas amostras do preview por grão fino.
    const float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715)))) - 0.5;
    const int n = clamp(int(p.blur.w), 1, 64);
    vec4 acc = here / max(ownLen, 1.0);
    float total = 1.0 / max(ownLen, 1.0);
    for (int i = 0; i < 64; ++i) {
        if (i >= n) break;
        const vec2 dir = (hasOwn && (i & 1) == 1) ? own : far;
        const float s = (float(i) + 0.5 + jitter) / float(n) - 0.5;
        const float dist = abs(s) * length(dir);
        const vec2 q = v_uv + s * dir * p.blur.yz;
        const float qLen = length(motion(q));
        const float w = reaches(dist, qLen) / max(qLen, 1.0) + reaches(dist, ownLen) / max(ownLen, 1.0);
        acc += textureLod(u_img, q, 0.0) * w;
        total += w;
    }
    o_color = acc / total;
}
