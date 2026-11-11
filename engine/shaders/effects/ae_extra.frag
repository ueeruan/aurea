#version 450
// =============================================================================
//  Aurea / shaders / effects / ae_extra.frag
//
//  Lote "AE 2026", segunda leva (AeEffects2026Extra.cpp): uma operação por
//  efeito, escolhida por u.p[11].w (contrato `ExtraOp` no C++):
//    0 Combinador de canais · 1 Cores de transmissão · 2 Limitador de vídeo ·
//    3 Neutralizador de cor · 4 Excessos de brilho · 5 Fios (trama)
//  Especificados pelo comportamento descrito na documentação pública do After
//  Effects; as contas são do Aurea. Entrada e saída no espaço de trabalho
//  (linear, pré-multiplicado); quem julga cor decodifica para sRGB e volta.
// =============================================================================
#include "ae_common.glsl"

const float PI = 3.14159265359;

// BT.709 no valor codificado (Y'CbCr de faixa cheia).
vec3 rgb2ycc(vec3 c) {
    const float y = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return vec3(y, (c.b - y) / 1.8556, (c.r - y) / 1.5748);
}
vec3 ycc2rgb(vec3 y) {
    const float r = y.x + 1.5748 * y.z;
    const float b = y.x + 1.8556 * y.y;
    const float g = (y.x - 0.2126 * r - 0.0722 * b) / 0.7152;
    return vec3(r, g, b);
}

// --- 0 Combinador de canais ---------------------------------------------------------
// p[0]: x = de (0..13), y = para (0..6), z = inverter, w = alfa sólido
float from_scalar(vec4 d, int from) {
    const vec3 hsl = ae_rgb2hsl(d.rgb);
    if (from == 0) return hsl.x;
    if (from == 1) return hsl.z;
    if (from == 2) return hsl.y;
    if (from == 3) return ae_luma(d.rgb);
    if (from == 4) return d.r;
    if (from == 5) return d.g;
    if (from == 6) return d.b;
    if (from == 7) return d.a;
    if (from == 8) return min(d.r, min(d.g, d.b));
    return max(d.r, max(d.g, d.b));
}
vec4 op_channel_combiner(vec4 src) {
    const vec4 d = ae_decode(src);
    const int from = int(u.p[0].x + 0.5), to = int(u.p[0].y + 0.5);
    const bool invert = u.p[0].z > 0.5;
    vec3 c = d.rgb;
    float a = d.a;
    if (from >= 10) {
        // Conversões: o trio inteiro passa para o outro espaço.
        if (from == 10) c = ae_rgb2hsl(d.rgb);
        else if (from == 11) c = ae_hsl2rgb(d.rgb);
        else if (from == 12) c = rgb2ycc(d.rgb) + vec3(0.0, 0.5, 0.5);
        else c = ycc2rgb(d.rgb - vec3(0.0, 0.5, 0.5));
        c = clamp(c, 0.0, 1.0);
        if (invert) c = 1.0 - c;
    } else {
        float v = from_scalar(d, from);
        if (invert) v = 1.0 - v;
        if (to == 0) c.r = v;
        else if (to == 1) c.g = v;
        else if (to == 2) c.b = v;
        else if (to == 3) a = v;
        else {
            vec3 hsl = ae_rgb2hsl(d.rgb);
            if (to == 4) hsl.x = v;
            else if (to == 5) hsl.z = v;
            else hsl.y = v;
            c = ae_hsl2rgb(hsl);
        }
    }
    if (u.p[0].w > 0.5) a = 1.0;
    return ae_encode(c, clamp(a, 0.0, 1.0));
}

// --- 1 Cores de transmissão ---------------------------------------------------------
// p[0]: x = padrão (0 NTSC YIQ, 1 PAL YUV), y = método (0 reduzir luminância,
// 1 reduzir saturação, 2 recortar o inseguro, 3 recortar o seguro), z = limite (IRE/100)
vec4 op_broadcast(vec4 src) {
    const vec4 d = ae_decode(src);
    const vec3 c = d.rgb;
    const float y = dot(c, vec3(0.299, 0.587, 0.114));
    float chroma;
    if (u.p[0].x < 0.5) {
        const float i = dot(c, vec3(0.596, -0.274, -0.322));
        const float q = dot(c, vec3(0.211, -0.523, 0.312));
        chroma = length(vec2(i, q));
    } else {
        const float uu = dot(c, vec3(-0.147, -0.289, 0.436));
        const float vv = dot(c, vec3(0.615, -0.515, -0.100));
        chroma = length(vec2(uu, vv));
    }
    const float limit = max(u.p[0].z, 0.05);
    const float peak = y + chroma;
    const bool unsafe = peak > limit + 1e-4;
    const int method = int(u.p[0].y + 0.5);
    if (method == 2) return unsafe ? vec4(0.0) : src;
    if (method == 3) return unsafe ? src : vec4(0.0);
    if (!unsafe) return src;
    vec3 o;
    if (method == 0) {
        o = c * (limit / max(peak, 1e-4));
    } else {
        const float room = max(limit - y, 0.0);
        o = mix(vec3(y), c, clamp(room / max(chroma, 1e-4), 0.0, 1.0));
    }
    return ae_encode(o, d.a);
}

// --- 2 Limitador de vídeo -------------------------------------------------------------
// p[0]: x = eixo (0 luma, 1 croma, 2 luma e croma, 3 limite inteligente),
//       y = sinal mínimo, z = sinal máximo (0..1,1), w = croma máxima (0..1)
vec4 op_video_limiter(vec4 src) {
    const vec4 d = ae_decode(src);
    const int axis = int(u.p[0].x + 0.5);
    const float lo = u.p[0].y, hi = max(u.p[0].z, lo + 1e-3);
    vec3 ycc = rgb2ycc(d.rgb);
    if (axis == 1 || axis == 2) {
        const float cmax = u.p[0].w * 0.5;
        const float r = length(ycc.yz);
        if (r > cmax) ycc.yz *= cmax / max(r, 1e-5);
    }
    if (axis == 0 || axis == 2) ycc.x = clamp(ycc.x, lo, hi);
    vec3 c = ycc2rgb(ycc);
    if (axis == 3) {
        // Limite inteligente: a luma presa e o croma reduzido só o bastante
        // para nenhum canal sair da faixa (a cor some por último).
        const float y = clamp(ycc.x, lo, hi);
        const vec3 off = ycc2rgb(vec3(y, ycc.yz)) - vec3(y);
        float k = 1.0;
        for (int i = 0; i < 3; ++i) {
            if (off[i] > 1e-5) k = min(k, (hi - y) / off[i]);
            if (off[i] < -1e-5) k = min(k, (lo - y) / off[i]);
        }
        c = vec3(y) + off * clamp(k, 0.0, 1.0);
    }
    return ae_encode(clamp(c, 0.0, 1.0), d.a);
}

// --- 3 Neutralizador de cor -------------------------------------------------------------
// c[0..2]: o desequilíbrio (cor amostrada) das sombras, tons médios e realces;
// p[0]: x = fixação (0..1), y = misturar com o original (0..1)
vec3 cast_of(vec4 c) {
    const vec3 k = clamp(aurea_linear_to_srgb(max(c.rgb, vec3(0.0))), 0.0, 1.0);
    return vec3(ae_luma(k)) - k;   // o que leva a cor amostrada ao cinza dela
}
vec4 op_neutralizer(vec4 src) {
    const vec4 d = ae_decode(src);
    const float l = clamp(ae_luma(d.rgb), 0.0, 1.0);
    const float ws = (1.0 - l) * (1.0 - l), wh = l * l, wm = 2.0 * l * (1.0 - l);
    vec3 fix = cast_of(u.c[0]) * ws + cast_of(u.c[1]) * wm + cast_of(u.c[2]) * wh;
    // Fixação: o preto e o branco puros ficam onde estão.
    fix *= mix(1.0, clamp(4.0 * l * (1.0 - l) * 1.5, 0.0, 1.0), clamp(u.p[0].x, 0.0, 1.0));
    const vec3 c = clamp(d.rgb + fix, 0.0, 1.0);
    return mix(ae_encode(c, d.a), src, clamp(u.p[0].y, 0.0, 1.0));
}

// --- 4 Excessos de brilho ---------------------------------------------------------------
// p[0]: x = canal (0 RGB, 1 luminância, 2 vermelho, 3 verde, 4 azul, 5 alfa); c[0] = cor do recorte
vec4 op_overbrights(vec4 src) {
    const vec4 s = unpremultiply(src);
    const int ch = int(u.p[0].x + 0.5);
    const float a = clamp(s.a, 0.0, 1.0);
    // Valores LINEARES (os do espaço de trabalho, que podem passar de 1).
    float v = 0.0;
    bool over = false, under = false;
    if (ch == 0) {
        over = max(s.r, max(s.g, s.b)) > 1.0 + 1e-4;
        under = min(s.r, min(s.g, s.b)) < -1e-4;
        if (!over && !under) return src;
    } else {
        if (ch == 1) v = luminance709(s.rgb);
        else if (ch == 2) v = s.r;
        else if (ch == 3) v = s.g;
        else if (ch == 4) v = s.b;
        else v = s.a;
        over = v > 1.0 + 1e-4;
        under = v < -1e-4;
        if (!over && !under) {
            // A vista do canal: ele em cinza (o alfa fica opaco onde há camada).
            const float g = clamp(v, 0.0, 1.0);
            return ch == 5 ? vec4(vec3(g), 1.0) : vec4(vec3(g) * a, a);
        }
    }
    const vec3 k = max(u.c[0].rgb, vec3(0.0));
    const vec3 col = under ? vec3(1.0) - clamp(aurea_linear_to_srgb(k), 0.0, 1.0) : clamp(aurea_linear_to_srgb(k), 0.0, 1.0);
    const float outA = ch == 5 ? 1.0 : a;
    return vec4(aurea_srgb_to_linear(col) * outA, outA);
}

// --- 5 Fios (trama) ---------------------------------------------------------------------
// p[0]: xy = célula (px), z = fios por célula, w = direção (rad)
// p[1]: xy = centro (px da camada), z = cobertura (0..1), w = sombreamento (0..1)
// p[2]: x = textura (0..1)
vec4 op_threads(vec4 src) {
    const vec2 px = ae_px();
    const vec2 ctr = u.p[1].xy;
    const float ang = u.p[0].w;
    const mat2 toWeave = mat2(cos(ang), -sin(ang), sin(ang), cos(ang));   // gira o tecido
    const mat2 toLayer = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
    const vec2 q = toWeave * (px - ctr);
    const float n = max(u.p[0].z, 1.0);
    const vec2 slot = max(u.p[0].xy, vec2(1.0)) / n;        // largura de um fio (x: verticais, y: horizontais)
    const vec2 idx = floor(q / slot);
    const vec2 t = fract(q / slot);                           // posição dentro do fio, 0..1
    const float cover = clamp(u.p[1].z, 0.05, 1.0);
    const float half_ = cover * 0.5;
    const bool inH = abs(t.y - 0.5) <= half_;                 // dentro de um fio horizontal
    const bool inV = abs(t.x - 0.5) <= half_;                 // dentro de um fio vertical
    if (!inH && !inV) return vec4(0.0);
    // Sobre / sob: alterna como num tafetá.
    const bool hOnTop = mod(idx.x + idx.y, 2.0) < 0.5;
    const bool useH = inH && (!inV || hOnTop);
    // A cor do fio é a da imagem no EIXO do fio (o fio "carrega" a cor ao longo dele).
    vec2 axisQ = useH ? vec2(q.x, (idx.y + 0.5) * slot.y) : vec2((idx.x + 0.5) * slot.x, q.y);
    vec4 col = ae_at_px(toLayer * axisQ + ctr);
    // Sombreamento: fio cilíndrico, mais escuro onde passa por baixo do outro.
    const float across = useH ? (t.y - 0.5) / max(half_, 1e-3) : (t.x - 0.5) / max(half_, 1e-3);
    float shade = 1.0 - u.p[1].w * (1.0 - sqrt(max(1.0 - across * across, 0.0)));
    const float along = useH ? t.x : t.y;
    const bool otherOnTop = useH ? !hOnTop : hOnTop;
    if (otherOnTop || (inH && inV)) shade *= 1.0 - 0.35 * u.p[1].w * (1.0 - abs(along - 0.5) * 2.0);
    // Textura: fibras ao longo do fio.
    const float fiber = ae_hash(vec2(useH ? idx.y : idx.x, floor((useH ? q.x : q.y) * 0.75)), useH ? 17.0 : 29.0);
    shade *= 1.0 - u.p[2].x * 0.5 * fiber;
    col.rgb *= max(shade, 0.0);
    return col;
}

void main() {
    const int op = int(u.p[11].w + 0.5);
    if (op == 5) { o_color = op_threads(vec4(0.0)); return; }
    const vec4 src = texture(u_tex0, ae_in_uv());
    if (op == 0) o_color = op_channel_combiner(src);
    else if (op == 1) o_color = op_broadcast(src);
    else if (op == 2) o_color = op_video_limiter(src);
    else if (op == 3) o_color = op_neutralizer(src);
    else o_color = op_overbrights(src);
}
