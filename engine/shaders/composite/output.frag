#version 450
// =============================================================================
//  Aurea / shaders / composite / output.frag
//
//  Composição (linear, pré-multiplicada) → display.
//
//  É o ÚNICO lugar onde a cor sai do espaço de trabalho. O fundo da área de
//  preview é composto AQUI, em linear, e só depois codificado — misturar no
//  espaço codificado (blend de hardware sobre o swapchain UNORM) escurece as
//  bordas semitransparentes.
//
//  Dither de 1/2 LSB antes de quantizar para 8 bits: sem ele, um degradê
//  suave de céu vira faixas no preview, e o usuário acha que o vídeo está
//  estragado. Desligado nos testes visuais (resultado determinístico).
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/color.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
    vec4 region;
    vec4 uvRect;
    vec4 params;    // x=dither (0/1), y=texels da composição por pixel do display,
                    // z=lado da casa do xadrez em px (0 = fundo opaco, sem xadrez)
} pc;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 background;   // cor linear atrás de áreas transparentes
    // AE layers (B10): guias do visor, só na tela. x = bits (1 grade, 2 margens
    // de ação/título, 4 réguas), y = passo da grade (px da composição),
    // zw = tamanho da composição (px).
    vec4 guides;
} p;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_tex0;

float interleaved_gradient_noise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Minificação: com a composição encolhida no display (zoom < 0,75), uma única
// amostra bilinear pula texels — a imagem cintila quando algo se move (grade,
// texto fino, arestas do 3D). Quatro amostras bilineares a ±1/4 da pegada do
// pixel cobrem uma caixa de ~2×2 pegadas: média de área quase de graça.
vec4 sample_footprint(vec2 uv) {
    const float k = pc.params.y;
    if (k < 1.33) return texture(u_tex0, uv);
    const vec2 o = (0.25 * min(k, 4.0)) / vec2(textureSize(u_tex0, 0));
    return 0.25 * (texture(u_tex0, uv + vec2(-o.x, -o.y)) + texture(u_tex0, uv + vec2(o.x, -o.y))
                 + texture(u_tex0, uv + vec2(-o.x, o.y)) + texture(u_tex0, uv + vec2(o.x, o.y)));
}

// Linha de ~1 px da tela em `d` (distância em px da composição; `px` = px da
// composição por px da tela).
float guide_line(float d, float px) {
    return 1.0 - smoothstep(0.5 * px, 1.25 * px, abs(d));
}

// Contorno de um retângulo centrado (fração `k` da composição).
float guide_box(vec2 q, vec2 size, float k, vec2 px) {
    const vec2 lo = size * (0.5 - 0.5 * k), hi = size * (0.5 + 0.5 * k);
    const bool insideX = q.x >= lo.x - px.x && q.x <= hi.x + px.x;
    const bool insideY = q.y >= lo.y - px.y && q.y <= hi.y + px.y;
    float a = 0.0;
    if (insideY) a = max(a, max(guide_line(q.x - lo.x, px.x), guide_line(q.x - hi.x, px.x)));
    if (insideX) a = max(a, max(guide_line(q.y - lo.y, px.y), guide_line(q.y - hi.y, px.y)));
    return a;
}

// As guias do AE por cima da imagem já codificada: grade, margens de ação
// (90 %) e título (80 %) com a cruz do centro, e réguas no topo e à esquerda.
vec3 draw_guides(vec3 enc) {
    const uint bits = uint(p.guides.x + 0.5);
    if (bits == 0u) return enc;
    const vec2 size = max(p.guides.zw, vec2(1.0));
    const vec2 q = v_uv * size;
    const vec2 px = max(fwidth(q), vec2(1e-4));
    if ((bits & 1u) != 0u) {
        // A partir do CENTRO da composição (a grade da coluna de modos de sempre).
        const float gstep = max(p.guides.y, 4.0);
        const vec2 g = q - 0.5 * size;
        const vec2 m = mod(g + 0.5 * gstep, gstep) - 0.5 * gstep;
        const vec2 major = mod(g + 2.5 * gstep, 5.0 * gstep) - 2.5 * gstep;
        const float minorA = max(guide_line(m.x, px.x), guide_line(m.y, px.y));
        const float majorA = max(guide_line(major.x, px.x), guide_line(major.y, px.y));
        // Passo menor que ~4 px da tela: só as linhas maiores (sem virar um borrão).
        const float minorK = gstep / max(px.x, px.y) >= 4.0 ? 0.28 : 0.0;
        enc = mix(enc, vec3(0.62, 0.66, 0.72), max(minorA * minorK, majorA * 0.5));
    }
    if ((bits & 3u) != 0u) {
        // Margens de ação (90 %) e de título (80 %) só com o bit 1; a cruz do
        // centro vale para a grade também.
        const float a = (bits & 2u) != 0u ? max(guide_box(q, size, 0.9, px), guide_box(q, size, 0.8, px)) : 0.0;
        const vec2 c = q - 0.5 * size;
        const float arm = 10.0 * max(px.x, px.y);
        float crossA = 0.0;
        if (abs(c.y) <= arm) crossA = max(crossA, guide_line(c.x, px.x));
        if (abs(c.x) <= arm) crossA = max(crossA, guide_line(c.y, px.y));
        enc = mix(enc, vec3(0.92), max(a, crossA) * 0.75);
    }
    if ((bits & 4u) != 0u) {
        // Faixa de 16 px da tela; marcas a cada passo redondo de ≥ 6 px da tela.
        const float band = 16.0;
        const float pxs = max(px.x, px.y);
        float tick = 1.0;
        for (int i = 0; i < 12 && tick / pxs < 6.0; ++i) tick *= (i % 3 == 1) ? 2.5 : 2.0;
        const vec2 sp = q / px;   // px da tela desde o canto da composição
        float a = 0.0;
        float bg = 0.0;
        if (sp.y < band) {
            bg = 1.0;
            const float d = mod(q.x + 0.5 * tick, tick) - 0.5 * tick;
            const float idx = floor((q.x + 0.5 * tick) / tick);
            const float len = mod(idx, 10.0) < 0.5 ? band : mod(idx, 5.0) < 0.5 ? 0.6 * band : 0.3 * band;
            if (sp.y < len) a = max(a, guide_line(d, px.x));
        }
        if (sp.x < band) {
            bg = 1.0;
            const float d = mod(q.y + 0.5 * tick, tick) - 0.5 * tick;
            const float idx = floor((q.y + 0.5 * tick) / tick);
            const float len = mod(idx, 10.0) < 0.5 ? band : mod(idx, 5.0) < 0.5 ? 0.6 * band : 0.3 * band;
            if (sp.x < len) a = max(a, guide_line(d, px.y));
        }
        enc = mix(enc, vec3(0.08, 0.09, 0.1), bg * 0.7);
        enc = mix(enc, vec3(0.85), a);
    }
    return enc;
}

void main() {
    vec4 c = sample_footprint(v_uv);
    // Composição de fundo transparente: xadrez cinza claro/médio atrás dela
    // (só na tela — o PNG/GIF exportado guarda o alfa de verdade).
    vec3 under = p.background.rgb;
    if (pc.params.z > 0.5) {
        vec2 cell = floor(gl_FragCoord.xy / pc.params.z);
        under = mod(cell.x + cell.y, 2.0) < 0.5 ? vec3(0.58) : vec3(0.36);
    }
    vec3 lin = c.rgb + under * (1.0 - c.a);
    vec3 enc = linear_to_srgb(lin);
    if (pc.params.x > 0.5) {
        enc += (interleaved_gradient_noise(gl_FragCoord.xy) - 0.5) / 255.0;
    }
    enc = draw_guides(enc);
    o_color = vec4(clamp(enc, 0.0, 1.0), 1.0);
}
