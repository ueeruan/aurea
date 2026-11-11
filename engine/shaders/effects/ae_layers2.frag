#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_layers2.frag
//
//  Lote "AE 2026", segunda leva: os efeitos que leem até DUAS outras camadas
//  (u_tex1 e u_tex2, cada uma desenhada no quadro da composição). Operação em
//  u.p[11].w (contrato `LayerOp` em AeEffects2026Extra.cpp):
//    0 Aritmética composta · 1 Cálculos · 2 Óculos 3D · 3 Limpar com vidro ·
//    4 WarpoMatic · 5 Ambiente
//  u.p[10]: x = há camada 1, y = há camada 2, z = camada 1 esticada (0 =
//  centralizada 1:1), w = camada 2 esticada; u.p[9] = tamanho (px) da 1 (xy) e
//  da 2 (zw). Sem a camada, o slot recebe a própria entrada.
//  Especificados pelo comportamento público dos efeitos do After Effects; as
//  contas são do Aurea.
// =============================================================================
#include "ae_common.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_tex2;

const float PI = 3.14159265359;

vec2 placed_uv(vec2 px, vec2 size, bool stretch) {
    const vec2 ls = ae_layer_size();
    const vec2 os = max(size, vec2(1.0));
    return stretch ? px / ls : (px - ls * 0.5 + os * 0.5) / os;
}
vec4 layer1_at(vec2 px) {
    if (u.p[10].x < 0.5) return ae_at_px(px);
    return texture(u_tex1, placed_uv(px, u.p[9].xy, u.p[10].z > 0.5));
}
vec4 layer2_at(vec2 px) {
    if (u.p[10].y < 0.5) return ae_at_px(px);
    return texture(u_tex2, placed_uv(px, u.p[9].zw, u.p[10].w > 0.5));
}
float luma_of(vec4 premul) { return ae_luma(ae_decode(premul).rgb); }

// --- 0 Aritmética composta ----------------------------------------------------------
// p[0]: x = operador, y = canais (0 RGB, 1 ARGB, 2 alfa), z = excesso (0 recortar,
// 1 envolver, 2 escalar), w = misturar com o original
float arith(float a, float b, int op) {
    if (op == 0) return b;
    if (op == 1) return a + b;
    if (op == 2) return a - b;
    if (op == 3) return a * b;
    if (op == 4) return abs(a - b);
    if (op >= 5 && op <= 7) {
        const uint ia = uint(clamp(a, 0.0, 1.0) * 255.0 + 0.5), ib = uint(clamp(b, 0.0, 1.0) * 255.0 + 0.5);
        const uint r = op == 5 ? (ia & ib) : op == 6 ? (ia | ib) : (ia ^ ib);
        return float(r) / 255.0;
    }
    if (op == 8 || op == 11) return max(a, b);
    if (op == 9 || op == 10) return min(a, b);
    if (op == 12) return 1.0 - (1.0 - a) * (1.0 - b);
    if (op == 13) return a < 0.5 ? 2.0 * a * b : 1.0 - 2.0 * (1.0 - a) * (1.0 - b);
    return b < 0.5 ? 2.0 * a * b : 1.0 - 2.0 * (1.0 - a) * (1.0 - b);   // 14 luz direta
}
float overflow(float v, int mode) {
    if (mode == 1) return fract(v);   // envolver
    if (mode == 2) return clamp(v * 0.5 + (v < 0.0 ? 0.5 : 0.0), 0.0, 1.0);   // escalar: a faixa dupla volta a 0..1
    return clamp(v, 0.0, 1.0);
}
vec4 compound_arithmetic(vec4 src, vec2 px) {
    const vec4 a = ae_decode(src), b = ae_decode(layer1_at(px));
    const int op = int(u.p[0].x + 0.5), ch = int(u.p[0].y + 0.5), ov = int(u.p[0].z + 0.5);
    vec3 c = a.rgb;
    float al = a.a;
    if (ch != 2) for (int i = 0; i < 3; ++i) c[i] = overflow(arith(a[i], b[i], op), ov);
    if (ch != 0) al = overflow(arith(a.a, b.a, op), ov);
    return mix(ae_encode(c, al), src, clamp(u.p[0].w, 0.0, 1.0));
}

// --- 1 Cálculos ---------------------------------------------------------------------
// p[0]: x = canal de entrada, y = inverter a entrada, z = canal da 2ª, w = inverter a 2ª
// p[1]: x = opacidade da 2ª, y = preservar transparência, z = modo (ae_blend_mode)
// Canais: 0 RGBA, 1 cinza, 2 vermelho, 3 verde, 4 azul, 5 alfa.
vec4 channel_view(vec4 d, int ch) {
    if (ch == 0) return d;
    float v = ch == 1 ? ae_luma(d.rgb) : ch == 2 ? d.r : ch == 3 ? d.g : ch == 4 ? d.b : d.a;
    return vec4(vec3(v), ch == 5 ? 1.0 : d.a);
}
vec4 calculations(vec4 src, vec2 px) {
    const vec4 a0 = ae_decode(src);
    vec4 a = channel_view(a0, int(u.p[0].x + 0.5));
    if (u.p[0].y > 0.5) a.rgb = 1.0 - a.rgb;
    vec3 c = a.rgb;
    float al = a.a;
    if (u.p[10].x > 0.5) {
        vec4 b = channel_view(ae_decode(layer1_at(px)), int(u.p[0].z + 0.5));
        if (u.p[0].w > 0.5) b.rgb = 1.0 - b.rgb;
        const float k = clamp(u.p[1].x, 0.0, 1.0) * b.a;
        c = mix(c, ae_blend_mode(c, b.rgb, int(u.p[1].z + 0.5)), k);
        if (u.p[1].y < 0.5) al = al + k * (1.0 - al);
    }
    return ae_encode(c, clamp(al, 0.0, 1.0));
}

// --- 2 Óculos 3D ----------------------------------------------------------------------
// p[0]: x = vista (0 par lado a lado, 1 entrelaçado, 2 vermelho/verde, 3 vermelho/azul,
//       4 vermelho/azul balanceado, 5 vermelho/azul colorido), y = convergência (px),
//       z = alinhamento vertical (px), w = equilíbrio (0..1)
// p[1]: x = trocar esquerda e direita
vec4 glasses(vec2 px) {
    const vec2 shift = vec2(u.p[0].y * 0.5, 0.0);
    const vec2 vshift = vec2(0.0, u.p[0].z);
    vec4 L = ae_at_px(px + shift);
    vec4 R = layer1_at(px - shift + vshift);
    if (u.p[1].x > 0.5) { const vec4 t = L; L = R; R = t; }
    const int view = int(u.p[0].x + 0.5);
    const vec2 ls = ae_layer_size();
    if (view == 0) {
        // Par estéreo: esquerda na metade esquerda, direita na direita (cada uma espremida).
        const bool right = px.x >= ls.x * 0.5;
        const vec2 p = vec2(fract(px.x / (ls.x * 0.5)) * ls.x, px.y);
        return right ? layer1_at(p - shift + vshift) : ae_at_px(p + shift);
    }
    if (view == 1) return mod(floor(px.y), 2.0) < 0.5 ? L : R;
    const vec4 l = ae_decode(L), r = ae_decode(R);
    const float a = max(l.a, r.a);
    const float bal = clamp(u.p[0].w, 0.0, 1.0);
    vec3 c;
    if (view == 2) c = vec3(ae_luma(l.rgb), ae_luma(r.rgb), 0.0);
    else if (view == 3) c = vec3(ae_luma(l.rgb), 0.0, ae_luma(r.rgb));
    else if (view == 4) {
        // Balanceado: parte da cor de cada olho volta para reduzir o fantasma.
        c = vec3(mix(ae_luma(l.rgb), l.r, bal), 0.0, mix(ae_luma(r.rgb), r.b, bal));
    } else c = vec3(l.r, mix(r.g, ae_luma(r.rgb), bal * 0.5), r.b);
    return ae_encode(c, a);
}

// --- 3 Limpar com vidro --------------------------------------------------------------
// p[0]: x = conclusão (0..1), y = suavidade (0..1), z = deslocamento (px)
// camada 1 = a revelar, camada 2 = gradiente (sem ela, a luminância da própria)
float grad_at(vec2 px) { return luma_of(u.p[10].y > 0.5 ? layer2_at(px) : ae_at_px(px)); }
vec4 glass_wipe(vec4 src, vec2 px) {
    const float c = clamp(u.p[0].x, 0.0, 1.0);
    const float soft = max(u.p[0].y, 1e-3);
    const float g = grad_at(px);
    // Limiar que anda de −suavidade a 1: em 0 nada some, em 1 tudo.
    const float t = c * (1.0 + soft) - soft;
    const float m = smoothstep(t, t + soft, g);              // 1 = ainda a camada
    const float edge = 1.0 - abs(m * 2.0 - 1.0);             // o "vidro" na borda
    const float e = 2.0;
    const vec2 n = vec2(grad_at(px + vec2(e, 0.0)) - grad_at(px - vec2(e, 0.0)),
                        grad_at(px + vec2(0.0, e)) - grad_at(px - vec2(0.0, e)));
    const vec2 off = n * u.p[0].z * edge * 4.0;
    const vec4 a = ae_at_px(px + off);
    const vec4 b = u.p[10].x > 0.5 ? layer1_at(px - off) : vec4(0.0);
    return mix(b, a, m);
}

// --- 4 WarpoMatic ----------------------------------------------------------------------
// p[0]: x = conclusão, y = reator (0 brilho, 1 dif. de contraste, 2 dif. de brilho,
//       3 dif. locais), z = suavidade (0..1), w = deformação (px)
// p[1]: x = direção (0 junta, 1 oposta, 2 torcendo), y = faixa de mistura (0..1)
float reactor(vec2 px) {
    const int mode = int(u.p[0].y + 0.5);
    const float a = luma_of(ae_at_px(px));
    if (mode == 0) return a;
    const float b = luma_of(layer1_at(px));
    if (mode == 2) return clamp(abs(a - b), 0.0, 1.0);
    if (mode == 1) return clamp(abs((a - 0.5) - (b - 0.5)) * 2.0, 0.0, 1.0);
    // Diferenças locais: a da vizinhança (média em cruz).
    const float r = 4.0;
    float s = 0.0;
    for (int k = 0; k < 4; ++k) {
        const vec2 o = vec2(k == 0 ? r : k == 1 ? -r : 0.0, k == 2 ? r : k == 3 ? -r : 0.0);
        s += abs(luma_of(ae_at_px(px + o)) - luma_of(layer1_at(px + o)));
    }
    return clamp(s * 0.25 * 2.0, 0.0, 1.0);
}
float reactor_soft(vec2 px) {
    const float r = 1.0 + u.p[0].z * 12.0;
    return (reactor(px) * 2.0 + reactor(px + vec2(r, 0.0)) + reactor(px - vec2(r, 0.0))
            + reactor(px + vec2(0.0, r)) + reactor(px - vec2(0.0, r))) / 6.0;
}
vec4 warpomatic(vec2 px) {
    const float c = clamp(u.p[0].x, 0.0, 1.0);
    const float e = 2.0 + u.p[0].z * 6.0;
    const vec2 grad = vec2(reactor_soft(px + vec2(e, 0.0)) - reactor_soft(px - vec2(e, 0.0)),
                           reactor_soft(px + vec2(0.0, e)) - reactor_soft(px - vec2(0.0, e)));
    const float amount = u.p[0].w * sin(c * PI);             // deforma no meio da transição
    vec2 offA = grad * amount * 4.0;
    vec2 offB = offA;
    const int dir = int(u.p[1].x + 0.5);
    if (dir == 1) offB = -offA;
    else if (dir == 2) offB = vec2(-offA.y, offA.x);
    const float r = reactor_soft(px);
    const float span = max(u.p[1].y, 1e-3);
    const float t = c * (1.0 + span) - span;
    const float m = smoothstep(t, t + span, r);              // 1 = ainda a camada
    const vec4 a = ae_at_px(px + offA);
    const vec4 b = u.p[10].x > 0.5 ? layer1_at(px + offB) : vec4(0.0);
    return mix(b, a, m);
}

// --- 5 Ambiente -------------------------------------------------------------------------
// p[0]: x = mapeamento (0 esférico, 1 sonda, 2 cruz vertical), y = filtrar o ambiente,
//       z = relevo (altura da superfície pela luminância), w = quantidade (0..1)
vec4 env_sample(vec2 uv) {
    const vec2 ls = ae_layer_size();
    vec4 s = layer1_at(uv * ls);
    if (u.p[0].y > 0.5) {
        const float r = 0.01 * max(ls.x, ls.y);
        s = s * 0.4 + 0.15 * (layer1_at(uv * ls + vec2(r, 0.0)) + layer1_at(uv * ls - vec2(r, 0.0))
                            + layer1_at(uv * ls + vec2(0.0, r)) + layer1_at(uv * ls - vec2(0.0, r)));
    }
    return s;
}
vec4 environment(vec4 src, vec2 px) {
    if (src.a <= 1e-4) return src;
    const float e = 1.5;
    const float h = u.p[0].z;
    const vec3 nrm = normalize(vec3(-(luma_of(ae_at_px(px + vec2(e, 0.0))) - luma_of(ae_at_px(px - vec2(e, 0.0)))) * h,
                                    -(luma_of(ae_at_px(px + vec2(0.0, e))) - luma_of(ae_at_px(px - vec2(0.0, e)))) * h,
                                    1.0));
    // Câmera ortográfica olhando a camada: a reflexão de (0,0,−1) pela normal,
    // inclinada pela posição na camada (a superfície "vê" ao redor).
    const vec2 ls = ae_layer_size();
    const vec2 q = (px / ls) * 2.0 - 1.0;
    const vec3 view = normalize(vec3(q * 0.35, -1.0));
    const vec3 r = reflect(view, nrm);
    vec2 uv;
    const int mapping = int(u.p[0].x + 0.5);
    if (mapping == 0) {
        // Esférico (latitude/longitude).
        uv = vec2(atan(r.x, -r.z) / (2.0 * PI) + 0.5, 0.5 - asin(clamp(-r.y, -1.0, 1.0)) / PI);
    } else if (mapping == 1) {
        // Sonda de luz (bola espelhada): o círculo inscrito.
        const float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
        uv = vec2(r.x, -r.y) / max(m, 1e-4) + 0.5;
    } else {
        // Cruz vertical: a face da frente no meio de uma cruz 3x4.
        const vec3 a = abs(r);
        vec2 f;
        vec2 cell;
        if (a.z >= a.x && a.z >= a.y) { f = vec2(r.x, -r.y) / a.z; cell = r.z < 0.0 ? vec2(1.0, 1.0) : vec2(1.0, 3.0); }
        else if (a.x >= a.y) { f = vec2(-r.z * sign(r.x), -r.y) / a.x; cell = r.x > 0.0 ? vec2(2.0, 1.0) : vec2(0.0, 1.0); }
        else { f = vec2(r.x, r.z * sign(r.y)) / a.y; cell = r.y > 0.0 ? vec2(1.0, 0.0) : vec2(1.0, 2.0); }
        uv = (cell + clamp(f * 0.5 + 0.5, vec2(0.001), vec2(0.999))) / vec2(3.0, 4.0);
    }
    const vec4 env = ae_decode(env_sample(uv));
    const vec4 d = ae_decode(src);
    const vec3 c = mix(d.rgb, env.rgb, clamp(u.p[0].w, 0.0, 1.0));
    return ae_encode(c, d.a);
}

void main() {
    const vec4 src = texture(u_tex0, ae_in_uv());
    const vec2 px = ae_px();
    const int op = int(u.p[11].w + 0.5);
    if (op == 0) o_color = compound_arithmetic(src, px);
    else if (op == 1) o_color = calculations(src, px);
    else if (op == 2) o_color = glasses(px);
    else if (op == 3) o_color = glass_wipe(src, px);
    else if (op == 4) o_color = warpomatic(px);
    else o_color = environment(src, px);
}
