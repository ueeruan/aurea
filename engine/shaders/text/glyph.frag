#version 450
// =============================================================================
//  Aurea / shaders / text / glyph.frag
//
//  Preenchimento e contorno pela distância assinada do atlas: borda nítida em
//  qualquer escala (a largura da transição vem da derivada na tela) e
//  contorno de espessura uniforme sem rasterizar de novo. Saída
//  pré-multiplicada, linear.
//
//  0.5.2 TEXTURA NO TEXTO: o glifo com `extra.z` = 1 pinta o preenchimento (e,
//  se pedido, o contorno) com uma textura procedural (common/texture_lib.glsl)
//  ou com a imagem em u_texfill, em vez da cor lisa. Os parâmetros moram num
//  registro do mesmo buffer, `extra.y` posições adiante (Renderer.cpp,
//  TextTexture.hpp). A textura é medida no px da camada ANTES do transform de
//  cada letra: o animador move a letra e ela leva o pedaço de textura dela.
// =============================================================================
#include "../common/bindings.glsl"
#include "../common/texture_lib.glsl"

struct Glyph {
    vec4 rect;
    vec4 uv;
    vec4 fill;
    vec4 stroke;
    mat4 xform;
    vec4 misc;
    vec4 extra;
};

layout(set = 0, binding = AUREA_DATA, std430) readonly buffer Glyphs { Glyph g[]; } glyphs;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_atlas;
// Imagem da galeria do preenchimento com textura (sem imagem: o próprio atlas, nunca lido).
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_texfill;

layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in int v_index;
layout(location = 0) out vec4 o_color;

const float kDistScale = 8.0;   // valor 0..255 por px da base (Text.hpp)

// Cobertura do contorno. Com preenchimento opaco ele passa inteiro por baixo;
// com preenchimento translúcido (knockout = 1 - alfa) o miolo não pode vazar,
// e a divisão já desconta o que a instância do preenchimento cobre depois.
float strokeCoverage(float inner, float outer, float knockout) {
    if (knockout <= 0.0) return outer;
    return clamp(outer * (1.0 - inner) / max(1.0 - inner * (1.0 - knockout), 1e-4), 0.0, 1.0);
}

/// Cobertura do preenchimento (x) e a parte do contorno que aparece por baixo
/// dele (y). As cores entram depois: a soma do desfoque é linear nelas.
vec2 coverageAt(Glyph gl, vec2 uv, float aa, float fillAlpha) {
    if (any(lessThan(uv, gl.uv.xy)) || any(greaterThan(uv, gl.uv.zw))) return vec2(0.0);
    vec2 inset = 0.5 / vec2(textureSize(u_atlas, 0));
    float sd = (texture(u_atlas, clamp(uv, gl.uv.xy + inset, gl.uv.zw - inset)).r * 255.0 - 128.0) / kDistScale * gl.misc.z;
    float inner = smoothstep(-aa, aa, sd);
    float fillA = inner * fillAlpha;
    float strokeA = gl.misc.w > 0.0 ? strokeCoverage(inner, smoothstep(-aa, aa, sd + gl.misc.w), gl.misc.x) * gl.stroke.a : 0.0;
    return vec2(fillA, strokeA * (1.0 - fillA));
}

/// Distância assinada (px da camada) no ponto `uv` do atlas, presa à célula.
float sdfAt(Glyph gl, vec2 uv) {
    const vec2 inset = 0.5 / vec2(textureSize(u_atlas, 0));
    return (texture(u_atlas, clamp(uv, gl.uv.xy + inset, gl.uv.zw - inset)).r * 255.0 - 128.0) / kDistScale * gl.misc.z;
}

/// Normal do bisel: a altura sobe da borda (distância `sd`) até `bw` px para
/// dentro, com perfil arredondado; `g` = direção do gradiente da distância.
vec3 bevelNormal(vec2 g, float sd, float bw) {
    const float e = clamp(sd / bw, 0.0, 1.0);
    const float slope = 2.0 * (1.0 - e);
    return normalize(vec3(-g * slope, 1.0));
}

/// PREENCHIMENTO COM TEXTURA. Registro `R` (TextTexture.hpp, text_texture_record):
///   rect   = id da textura (0 = imagem), escala, rotação (rad), mapeamento (0 bloco, 1 letra)
///   uv     = deslocamento (unidades), contraste, brilho (-1..1)
///   fill   = tinta (cor de tela, alfa = quanto entra)
///   stroke = fase da animação, relevo (0..1), ângulo da luz (rad), semente
///   xform0 = centro do bloco (px da camada), tamanho da fonte (px), 1 = há imagem
///   xform1 = contorno também (0/1), proporção da imagem, largura e altura do bloco
void textureColors(Glyph gl, vec2 atlasUV, float sd, inout vec3 fillRGB, inout vec3 strokeRGB, inout float texA) {
    const Glyph R = glyphs.g[v_index + int(round(gl.extra.y))];
    const vec2 pLayer = mix(gl.rect.xy, gl.rect.zw, v_uv);
    const float em = max(R.xform[0].z, 1e-3);
    const bool perLetter = R.rect.w > 0.5;
    const vec2 origin = perLetter ? 0.5 * (gl.rect.xy + gl.rect.zw) : R.xform[0].xy;
    const float scale = max(R.rect.y, 1e-3);
    const float c = cos(R.rect.z), s = sin(R.rect.z);
    // camada -> textura: centro, em, escala, rotação (inversa) e deslocamento.
    const vec2 d = (pLayer - origin) / (em * scale);
    const vec2 q = vec2(c * d.x + s * d.y, -s * d.x + c * d.y) - R.uv.xy;
    const float relief = clamp(R.stroke.y, 0.0, 1.0);

    // Direção da distância (px da camada), pela diferença central no atlas,
    // levada à orientação da letra na tela (o animador pode girá-la).
    vec2 g = vec2(0.0);
    vec2 lit2 = vec2(1.0, 0.0);
    mat2 orient = mat2(1.0);
    if (relief > 0.0) {
        const vec2 ts = vec2(textureSize(u_atlas, 0));
        const vec2 cell = max(gl.uv.zw - gl.uv.xy, vec2(1e-6));
        const vec2 pxPerTexel = (gl.rect.zw - gl.rect.xy) / (cell * ts);
        const float gx = (sdfAt(gl, atlasUV + vec2(1.0 / ts.x, 0.0)) - sdfAt(gl, atlasUV - vec2(1.0 / ts.x, 0.0))) / max(2.0 * pxPerTexel.x, 1e-4);
        const float gy = (sdfAt(gl, atlasUV + vec2(0.0, 1.0 / ts.y)) - sdfAt(gl, atlasUV - vec2(0.0, 1.0 / ts.y))) / max(2.0 * pxPerTexel.y, 1e-4);
        const mat2 m = mat2(gl.xform[0].xy, gl.xform[1].xy);
        const float k = max(length(m[0]), 1e-4);
        orient = m / k;
        g = orient * vec2(gx, gy);
        lit2 = vec2(cos(R.stroke.z), -sin(R.stroke.z));
    }
    const vec3 L = normalize(vec3(lit2, 1.0));
    // Bisel: até ~9% do em com 100% de relevo, dentro do alcance do SDF.
    const float bw = clamp(relief * 0.09 * em, 0.5, max(0.9 * 16.0 * gl.misc.z, 0.5));
    vec3 nFill = relief > 0.0 ? bevelNormal(g, sd, bw) : vec3(0.0, 0.0, 1.0);
    vec3 nStroke = relief > 0.0 ? bevelNormal(g, sd + gl.misc.w, bw) : vec3(0.0, 0.0, 1.0);
    nFill = normalize(vec3(nFill.xy * relief, nFill.z));
    nStroke = normalize(vec3(nStroke.xy * relief, nStroke.z));

    const int id = int(R.rect.x + 0.5);
    AtxTexel tx;
    vec3 albedo;
    if (R.xform[0].w > 0.5) {
        // Imagem da galeria: cobre o bloco (ou cada letra) sem deformar; repete além.
        const vec2 box = perLetter ? vec2(em) : max(R.xform[1].zw, vec2(1.0));
        const float aspect = max(R.xform[1].y, 1e-3);
        const vec2 cover = box.x / box.y > aspect ? vec2(box.x, box.x / aspect) : vec2(box.y * aspect, box.y);
        const vec2 uv = q * em * scale / cover + 0.5;
        const vec4 img = texture(u_texfill, uv);
        albedo = atx_to_display(img.rgb);
        texA = img.a;
        tx = atx_make(albedo, atx_luma(albedo), 0.0, 0.0);
    } else {
        tx = atx_texel(id, q, R.stroke.x, nFill, R.stroke.w);
        albedo = tx.albedo;
    }
    albedo = atx_grade(albedo, R.uv.z, R.uv.w, R.fill);
    if (relief > 0.0) {
        // Relevo próprio da textura: a variação da altura na tela volta para
        // unidades de textura (inversa do jacobiano) e depois à orientação da letra.
        const vec2 dqx = dFdx(q), dqy = dFdy(q);
        const vec2 dhs = vec2(dFdx(tx.height), dFdy(tx.height));
        const mat2 jt = mat2(dqx.x, dqy.x, dqx.y, dqy.y);
        const float det = determinant(jt);
        vec2 dhq = abs(det) > 1e-12 ? inverse(jt) * dhs : vec2(0.0);
        dhq = clamp(dhq, vec2(-40.0), vec2(40.0));
        const vec2 bump = orient * vec2(c * dhq.x - s * dhq.y, s * dhq.x + c * dhq.y) * 0.06 * relief;
        nFill = normalize(vec3(nFill.xy - bump * nFill.z, nFill.z));
        nStroke = normalize(vec3(nStroke.xy - bump * nStroke.z, nStroke.z));
        fillRGB = atx_to_linear(mix(albedo, atx_light(tx, albedo, nFill, L), relief));
        if (R.xform[1].x > 0.5) strokeRGB = atx_to_linear(mix(albedo, atx_light(tx, albedo, nStroke, L), relief));
    } else {
        fillRGB = atx_to_linear(albedo);
        if (R.xform[1].x > 0.5) strokeRGB = fillRGB;
    }
}

void main() {
    const Glyph gl = glyphs.g[v_index];
    if (gl.uv.x < 0.0) {
        // Fundo: caixa arredondada (raio em uv.y), borda suave de 1 px.
        const vec2 size = gl.rect.zw - gl.rect.xy;
        const float r = min(gl.uv.y, 0.5 * min(size.x, size.y));
        const vec2 q = abs(v_uv - 0.5 * size) - (0.5 * size - vec2(r));
        const float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - r;
        // The analytic distance gradient is anchored at this pixel as well.
        const vec2 outside = max(q, vec2(0.0));
        const vec2 gradient = (length(outside) > 1e-6 ? normalize(outside)
            : (q.x > q.y ? vec2(1.0, 0.0) : vec2(0.0, 1.0))) * sign(v_uv - 0.5 * size);
        const float aw = max(0.5 * (abs(dot(gradient, dFdx(v_uv))) + abs(dot(gradient, dFdy(v_uv)))), 1e-4);
        const float a = (1.0 - smoothstep(-aw, aw, d)) * gl.fill.a;
        o_color = vec4(gl.fill.rgb * a, a);
        return;
    }
    // The varying is local to this glyph, with perspective interpolation.
    // Add the exact cell origin only at texture access, after local derivatives.
    const vec2 uvExtent = gl.uv.zw - gl.uv.xy;
    const vec2 atlasUV = gl.uv.xy + v_uv * uvExtent;
    // Blur coverage rather than widening the distance threshold: widening
    // left a visible rectangle wherever the finite atlas SDF saturated.
    // Distância assinada em px da layer (positiva dentro do glifo).
    const float sd = (texture(u_atlas, atlasUV).r * 255.0 - 128.0) / kDistScale * gl.misc.z;
    vec3 fillRGB = gl.fill.rgb;
    vec3 strokeRGB = gl.stroke.rgb;
    float texA = 1.0;
    if (gl.extra.z > 0.5) textureColors(gl, atlasUV, sd, fillRGB, strokeRGB, texA);
    const float fillAlpha = gl.fill.a * texA;
    if (gl.extra.x > 0.01) {
        vec2 uvPerPixel = (gl.uv.zw - gl.uv.xy) / max(gl.rect.zw - gl.rect.xy, vec2(0.001));
        float aa = max(0.25, length(fwidth(v_uv) * uvExtent / uvPerPixel) * 0.5);
        float sigma = gl.extra.x * 0.5;
        // Each kernel sample covers a cell of this size. Prefilter its SDF
        // coverage so thin strokes do not turn into repeated sharp copies.
        aa = max(aa, sigma);
        vec2 sum = vec2(0.0); float total = 0.0;
        for (int y = -3; y <= 3; ++y) for (int x = -3; x <= 3; ++x) {
            vec2 d = vec2(x, y);
            float weight = exp(-0.5 * dot(d, d));
            sum += coverageAt(gl, atlasUV + d * sigma * uvPerPixel, aa, fillAlpha) * weight;
            total += weight;
        }
        sum /= total;
        o_color = vec4(fillRGB * sum.x + strokeRGB * sum.y, sum.x + sum.y);
        return;
    }
    const float blur = gl.extra.x;
    // Derivatives of a sampled distance use a fixed 2x2 GPU quad. Moving a
    // glyph by an odd whole pixel changes that quad and its coverage, even
    // when the layer's opposite motion cancels the move. Measure the local
    // distance gradient around this pixel instead; UV derivatives retain the
    // projected footprint without depending on the quad's parity.
    const vec2 dx = dFdx(v_uv) * 0.5;
    const vec2 dy = dFdy(v_uv) * 0.5;
    const float distanceScale = 255.0 / kDistScale * gl.misc.z;
    const vec2 inset = 0.5 / vec2(textureSize(u_atlas, 0));
    const vec2 lo = gl.uv.xy + inset, hi = gl.uv.zw - inset;
    const float gx = abs(texture(u_atlas, clamp(gl.uv.xy + (v_uv + dx) * uvExtent, lo, hi)).r
                       - texture(u_atlas, clamp(gl.uv.xy + (v_uv - dx) * uvExtent, lo, hi)).r) * distanceScale;
    const float gy = abs(texture(u_atlas, clamp(gl.uv.xy + (v_uv + dy) * uvExtent, lo, hi)).r
                       - texture(u_atlas, clamp(gl.uv.xy + (v_uv - dy) * uvExtent, lo, hi)).r) * distanceScale;
    const float w = max((gx + gy) * 0.5, 1e-4) + blur;
    const float inner = smoothstep(-w, w, sd);
    const float fillA = inner * fillAlpha;
    float strokeA = 0.0;
    if (gl.misc.w > 0.0) strokeA = strokeCoverage(inner, smoothstep(-w, w, sd + gl.misc.w), gl.misc.x) * gl.stroke.a;
    // Preenchimento por cima do contorno.
    const vec3 rgb = fillRGB * fillA + strokeRGB * strokeA * (1.0 - fillA);
    const float a = fillA + strokeA * (1.0 - fillA);
    o_color = vec4(rgb, a);
}
