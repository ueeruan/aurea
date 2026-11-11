#version 450
// =============================================================================
//  Aurea / shaders / effects / headbop.frag
//
//  Headbop: a pessoa (camada no ponto de repouso × máscara) por cima do fundo
//  preenchido, amostrado no ponto da tela. Desfoque de movimento opcional:
//  poucas amostras ao longo do deslocamento de meio obturador.
//  Saídas: normal, só a pessoa (fundo transparente), só a pessoa em cor sólida.
// =============================================================================
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec2 v_bg;
layout(location = 2) in vec2 v_vel;
layout(location = 3) in vec2 v_rest;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;   // a camada
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // máscara da pessoa (R)
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_tex2;   // fundo preenchido (rgb·peso, peso)
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_tex3;   // alfa do fundo preenchido (alfa·peso, -, -, peso)

struct Face { vec4 a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, q; };
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;
    vec4 inputRegion;
    vec4 layer;
    vec4 frame;
    vec4 mode;       // pessoas, quadro inteiro, saída, bits (1 máscara, 2 fundo)
    vec4 color;
    vec4 blur;       // amostras
    vec4 window;
    vec4 frameShift; // dx global, dx anterior
    vec4 gridX[18];
    vec4 gridY[18];
    Face face[4];
} p;

bool hasBit(float bits, int b) { return (int(bits + 0.5) / b) % 2 == 1; }

float personMask(vec2 f) {
    if (any(lessThan(f, vec2(0.0))) || any(greaterThan(f, vec2(1.0)))) return 0.0;
    return smoothstep(0.35, 0.65, texture(u_tex1, f).r);
}

vec4 personAt(vec2 uv, vec2 rest, bool masked) {
    vec4 c = texture(u_tex0, clamp(uv, vec2(0.0), vec2(1.0)));
    return masked ? c * personMask(rest) : c;
}

void main() {
    bool masked = hasBit(p.mode.w, 1);
    bool plate = hasBit(p.mode.w, 2);
    vec4 person;
    int samples = int(p.blur.x + 0.5);
    if (samples > 1 && dot(v_vel, v_vel) > 1e-10) {
        vec2 restScale = p.inputRegion.zw / max(p.layer.xy, vec2(1.0));
        vec4 acc = vec4(0.0);
        for (int k = 0; k < 8; ++k) {
            if (k >= samples) break;
            float t = float(k) / float(samples - 1) - 0.5;
            acc += personAt(v_uv - v_vel * t, v_rest - v_vel * restScale * t, masked);
        }
        person = acc / float(samples);
    } else {
        person = personAt(v_uv, v_rest, masked);
    }
    int outMode = int(p.mode.z + 0.5);
    if (outMode == 2) { o_color = p.color * person.a; return; }
    if (outMode == 1 || !masked) { o_color = person; return; }
    vec4 bg = texture(u_tex0, v_bg);
    if (plate) {
        vec2 f = (v_bg * p.inputRegion.zw + p.inputRegion.xy) / max(p.layer.xy, vec2(1.0));
        bool inside = all(greaterThanEqual(f, vec2(0.0))) && all(lessThanEqual(f, vec2(1.0)));
        // Fora da caixa da camada (a parede do Motion Tile) não há pessoa: a imagem fica.
        float m = inside ? smoothstep(0.12, 0.4, texture(u_tex1, f).r) : 0.0;
        vec4 pl = texture(u_tex2, clamp(f, vec2(0.0), vec2(1.0)));
        vec3 fill = pl.a > 1e-4 ? pl.rgb / pl.a : bg.rgb / max(bg.a, 1e-4);
        // O buraco herda também a TRANSPARÊNCIA em volta (recorte sobre fundo
        // transparente não deixa uma silhueta opaca para trás).
        vec4 pa = texture(u_tex3, clamp(f, vec2(0.0), vec2(1.0)));
        float fillA = pa.a > 1e-4 ? clamp(pa.r / pa.a, 0.0, 1.0) : bg.a;
        bg = mix(bg, vec4(fill * fillA, fillA), m);
    }
    o_color = person + bg * (1.0 - person.a);
}
