#version 450
// =============================================================================
//  Aurea / shaders / effects / color_stack.frag
//
//  PASS FUSION dos efeitos de cor: N efeitos por pixel consecutivos numa
//  leitura e numa escrita.
//
//  Exposure + Brightness/Contrast + Saturation + Tint + Levels + Curves numa
//  layer 4K, ingenuamente, são seis passes: seis leituras e seis escritas de
//  ~66 MB. Aqui é UMA. O EffectGraph agrupa os efeitos por pixel seguidos e
//  manda a lista de operações neste bloco de uniforms; o shader executa a
//  lista em ordem.
//
//  O `switch` NÃO diverge: a lista é a mesma para todos os pixels do passe
//  (controle de fluxo uniforme), então todos os núcleos tomam o mesmo ramo.
//  Um compilador de shader por combinação daria o mesmo resultado com mais
//  pipelines para aquecer — a arquitetura permite trocar por isso depois sem
//  mexer em nenhum efeito.
//
//  Os opcodes batem com `ColorOpCode` em effects/Effect.hpp.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

#define OP_EXPOSURE      1
#define OP_BRIGHT_CONT   2
#define OP_SATURATION    3
#define OP_TINT          4
#define OP_COLOR_MATRIX  5
#define OP_LEVELS        6
#define OP_CURVES        7
#define OP_LUMA_KEY      8
#define OP_CHROMA_KEY    9

#define MAX_OPS 12

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // entrada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // LUT de curvas (256x1)

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 header;              // x=quantidade de operações
    vec4 ops[MAX_OPS * 4];    // 4 vec4 por operação; ops[i*4].x = opcode
} p;

float levels1(float e, float inB, float inW, float gammaV, float outB, float outW) {
    float t = clamp((e - inB) / max(inW - inB, 1e-5), 0.0, 1.0);
    t = pow(t, 1.0 / max(gammaV, 1e-3));
    return mix(outB, outW, t);
}

vec3 curves_lut(vec3 e) {
    // 256 amostras; o centro do texel i representa o código i/255.
    vec3 t = clamp(e, 0.0, 1.0) * (255.0 / 256.0) + 0.5 / 256.0;
    return vec3(texture(u_tex1, vec2(t.r, 0.5)).r,
                texture(u_tex1, vec2(t.g, 0.5)).g,
                texture(u_tex1, vec2(t.b, 0.5)).b);
}

// Crominância BT.709 (Cb, Cr) de um valor codificado, e a volta com a luma.
vec2 chroma709(vec3 e, float y) {
    return vec2((e.b - y) / 1.8556, (e.r - y) / 1.5748);
}
vec3 from_ycc709(float y, vec2 cc) {
    float r = y + 1.5748 * cc.y;
    float b = y + 1.8556 * cc.x;
    float g = (y - 0.2126 * r - 0.0722 * b) / 0.7152;
    return vec3(r, g, b);
}

void main() {
    vec4 src = unpremultiply(texture(u_tex0, v_uv));
    vec3 c = src.rgb;   // linear, alfa reto
    float alpha = src.a;   // as chaves multiplicam o alfa

    int count = int(p.header.x + 0.5);
    for (int i = 0; i < MAX_OPS; ++i) {
        if (i >= count) break;
        vec4 a = p.ops[i * 4 + 0];
        vec4 b = p.ops[i * 4 + 1];
        vec4 d = p.ops[i * 4 + 2];
        vec4 e4 = p.ops[i * 4 + 3];
        int op = int(a.x + 0.5);

        if (op == OP_EXPOSURE) {
            // Linear: 1 stop = 2x a luz. Offset e gamma seguem a convenção de
            // um "exposure" de pós-produção (offset nas sombras, gamma no meio).
            c = c * exp2(a.y) + a.z;
            c = pow(max(c, vec3(0.0)), vec3(1.0 / max(a.w, 1e-3)));
        } else if (op == OP_BRIGHT_CONT) {
            // Brilho e contraste são definidos no valor CODIFICADO (é como o
            // olho julga "metade do brilho"); contraste gira em torno do cinza
            // médio perceptual 0.5.
            vec3 enc = linear_to_srgb(c);
            enc = enc + a.y;
            enc = (enc - 0.5) * a.z + 0.5;
            c = srgb_to_linear(max(enc, vec3(0.0)));
        } else if (op == OP_SATURATION) {
            float l = luminance709(c);
            c = max(mix(vec3(l), c, a.y), vec3(0.0));
        } else if (op == OP_TINT) {
            // Mapeia o preto para uma cor e o branco para outra pela
            // luminância; `amount` mistura com o original.
            float l = clamp(luminance709(c), 0.0, 1.0);
            vec3 tinted = mix(a.yzw, b.xyz, l);
            c = mix(c, tinted, b.w);
        } else if (op == OP_COLOR_MATRIX) {
            // 3x4 por linhas: [a.yzw b.x] [b.yzw d.x] [d.yzw e4.x]
            vec3 m0 = a.yzw; vec3 m1 = b.yzw; vec3 m2 = d.yzw;
            vec3 off = vec3(b.x, d.x, e4.x);
            c = vec3(dot(m0, c), dot(m1, c), dot(m2, c)) + off;
            c = max(c, vec3(0.0));
        } else if (op == OP_LEVELS) {
            vec3 enc = linear_to_srgb(c);
            enc = vec3(levels1(enc.r, a.y, a.z, a.w, b.x, b.y),
                       levels1(enc.g, a.y, a.z, a.w, b.x, b.y),
                       levels1(enc.b, a.y, a.z, a.w, b.x, b.y));
            c = srgb_to_linear(enc);
        } else if (op == OP_CURVES) {
            c = srgb_to_linear(curves_lut(linear_to_srgb(c)));
        } else if (op == OP_LUMA_KEY) {
            // a.y limiar, a.z suavidade, a.w: 0 tira escuros, 1 tira claros.
            float y = dot(linear_to_srgb(c), vec3(0.2126, 0.7152, 0.0722));
            float lo = a.y - a.z * 0.5;
            float hi = a.y + a.z * 0.5;
            float keep = hi - lo > 1e-5 ? smoothstep(lo, hi, y) : step(a.y, y);
            if (a.w > 0.5) keep = 1.0 - keep;
            alpha *= keep;
        } else if (op == OP_CHROMA_KEY) {
            // a.yzw cor-chave codificada, b.x tolerância, b.y suavidade, b.z derramamento.
            vec3 enc = linear_to_srgb(c);
            float y = dot(enc, vec3(0.2126, 0.7152, 0.0722));
            vec2 cc = chroma709(enc, y);
            vec2 kc = chroma709(a.yzw, dot(a.yzw, vec3(0.2126, 0.7152, 0.0722)));
            float dist = length(cc - kc);
            alpha *= smoothstep(b.x, b.x + max(b.y, 1e-4), dist);
            float kl = length(kc);
            if (b.z > 0.0 && kl > 1e-4) {
                vec2 dir = kc / kl;
                float along = dot(cc, dir);
                if (along > 0.0) {
                    cc -= dir * along * b.z;
                    c = srgb_to_linear(max(from_ycc709(y, cc), vec3(0.0)));
                }
            }
        }
    }

    o_color = premultiply(vec4(c, alpha));
}
