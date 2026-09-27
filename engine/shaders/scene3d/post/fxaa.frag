#version 450
// =============================================================================
//  Aurea / shaders / scene3d / post / fxaa.frag
//
//  FXAA 3.11 "quality" (Timothy Lottes), preset 12 — o AA do 3D quando não
//  há MSAA: o backend GLES (sem MSAA no motor) e o nível BAIXO do preview.
//  Roda DEPOIS do tone map (a detecção de borda precisa de luma perceptual,
//  não de HDR).
//
//  A imagem é pré-multiplicada sobre fundo transparente: a luma de detecção é
//  a da cor composta sobre cinza médio. Assim um objeto escuro sobre fundo
//  transparente também tem borda (preto contra "nada" seria luma 0 dos dois
//  lados). A mistura final usa RGBA, então a cobertura (alfa) também é
//  suavizada.
// =============================================================================
#include "../../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_src;

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 texel;   // xy = 1/tamanho, z = subpixel (0..1), w = limiar de borda
} p;

const float kEdgeThresholdMin = 0.0625;

vec4 fetch(vec2 uv) { return textureLod(u_src, uv, 0.0); }
float luma_of(vec4 c) {
    const vec3 over = c.rgb + vec3(0.5) * (1.0 - clamp(c.a, 0.0, 1.0));
    return sqrt(max(dot(over, vec3(0.2126, 0.7152, 0.0722)), 0.0));
}
float luma_at(vec2 uv) { return luma_of(fetch(uv)); }

void main() {
    const vec2 rcp = p.texel.xy;
    vec2 posM = v_uv;
    const vec4 rgbyM = fetch(posM);
    const float lumaM = luma_of(rgbyM);
    float lumaS = luma_at(posM + vec2(0.0, rcp.y));
    float lumaE = luma_at(posM + vec2(rcp.x, 0.0));
    float lumaN = luma_at(posM + vec2(0.0, -rcp.y));
    float lumaW = luma_at(posM + vec2(-rcp.x, 0.0));

    const float rangeMax = max(max(lumaN, lumaW), max(lumaE, max(lumaS, lumaM)));
    const float rangeMin = min(min(lumaN, lumaW), min(lumaE, min(lumaS, lumaM)));
    const float range = rangeMax - rangeMin;
    if (range < max(kEdgeThresholdMin, rangeMax * p.texel.w)) {
        o_color = rgbyM;
        return;
    }

    const float lumaNW = luma_at(posM + vec2(-rcp.x, -rcp.y));
    const float lumaSE = luma_at(posM + vec2(rcp.x, rcp.y));
    const float lumaNE = luma_at(posM + vec2(rcp.x, -rcp.y));
    const float lumaSW = luma_at(posM + vec2(-rcp.x, rcp.y));

    const float lumaNS = lumaN + lumaS;
    const float lumaWE = lumaW + lumaE;
    const float subpixRcpRange = 1.0 / range;
    const float subpixNSWE = lumaNS + lumaWE;
    const float edgeHorz1 = -2.0 * lumaM + lumaNS;
    const float edgeVert1 = -2.0 * lumaM + lumaWE;
    const float lumaNESE = lumaNE + lumaSE;
    const float lumaNWNE = lumaNW + lumaNE;
    const float edgeHorz2 = -2.0 * lumaE + lumaNESE;
    const float edgeVert2 = -2.0 * lumaN + lumaNWNE;
    const float lumaNWSW = lumaNW + lumaSW;
    const float lumaSWSE = lumaSW + lumaSE;
    const float edgeHorz4 = abs(edgeHorz1) * 2.0 + abs(edgeHorz2);
    const float edgeVert4 = abs(edgeVert1) * 2.0 + abs(edgeVert2);
    const float edgeHorz3 = -2.0 * lumaW + lumaNWSW;
    const float edgeVert3 = -2.0 * lumaS + lumaSWSE;
    const float edgeHorz = abs(edgeHorz3) + edgeHorz4;
    const float edgeVert = abs(edgeVert3) + edgeVert4;
    const float subpixNWSWNESE = lumaNWSW + lumaNESE;
    float lengthSign = rcp.x;
    const bool horzSpan = edgeHorz >= edgeVert;
    const float subpixA = subpixNSWE * 2.0 + subpixNWSWNESE;
    if (!horzSpan) { lumaN = lumaW; lumaS = lumaE; }
    if (horzSpan) lengthSign = rcp.y;
    const float subpixB = subpixA * (1.0 / 12.0) - lumaM;

    const float gradientN = lumaN - lumaM;
    const float gradientS = lumaS - lumaM;
    float lumaNN = lumaN + lumaM;
    const float lumaSS = lumaS + lumaM;
    const bool pairN = abs(gradientN) >= abs(gradientS);
    const float gradient = max(abs(gradientN), abs(gradientS));
    if (pairN) lengthSign = -lengthSign;
    const float subpixC = clamp(abs(subpixB) * subpixRcpRange, 0.0, 1.0);

    vec2 posB = posM;
    const vec2 offNP = horzSpan ? vec2(rcp.x, 0.0) : vec2(0.0, rcp.y);
    if (!horzSpan) posB.x += lengthSign * 0.5;
    if (horzSpan) posB.y += lengthSign * 0.5;

    // Preset 12: passos de busca 1; 1,5; 2; 4; 12.
    const float steps[5] = float[5](1.0, 1.5, 2.0, 4.0, 12.0);
    vec2 posN = posB - offNP * steps[0];
    vec2 posP = posB + offNP * steps[0];
    const float subpixD = -2.0 * subpixC + 3.0;
    float lumaEndN = luma_at(posN);
    const float subpixE = subpixC * subpixC;
    float lumaEndP = luma_at(posP);
    if (!pairN) lumaNN = lumaSS;
    const float gradientScaled = gradient * 0.25;
    const float lumaMM = lumaM - lumaNN * 0.5;
    const float subpixF = subpixD * subpixE;
    const bool lumaMLTZero = lumaMM < 0.0;
    lumaEndN -= lumaNN * 0.5;
    lumaEndP -= lumaNN * 0.5;
    bool doneN = abs(lumaEndN) >= gradientScaled;
    bool doneP = abs(lumaEndP) >= gradientScaled;
    if (!doneN) posN -= offNP * steps[1];
    if (!doneP) posP += offNP * steps[1];
    for (int s = 2; s < 5 && !(doneN && doneP); ++s) {
        if (!doneN) lumaEndN = luma_at(posN) - lumaNN * 0.5;
        if (!doneP) lumaEndP = luma_at(posP) - lumaNN * 0.5;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN -= offNP * steps[s];
        if (!doneP) posP += offNP * steps[s];
    }

    float dstN = posM.x - posN.x;
    float dstP = posP.x - posM.x;
    if (!horzSpan) { dstN = posM.y - posN.y; dstP = posP.y - posM.y; }
    const bool goodSpanN = (lumaEndN < 0.0) != lumaMLTZero;
    const bool goodSpanP = (lumaEndP < 0.0) != lumaMLTZero;
    const float spanLength = dstP + dstN;
    const bool directionN = dstN < dstP;
    const float dst = min(dstN, dstP);
    const bool goodSpan = directionN ? goodSpanN : goodSpanP;
    const float subpixG = subpixF * subpixF;
    const float pixelOffset = dst * (-1.0 / max(spanLength, 1e-6)) + 0.5;
    const float subpixH = subpixG * p.texel.z;
    const float pixelOffsetGood = goodSpan ? pixelOffset : 0.0;
    const float pixelOffsetSubpix = max(pixelOffsetGood, subpixH);
    if (!horzSpan) posM.x += pixelOffsetSubpix * lengthSign;
    if (horzSpan) posM.y += pixelOffsetSubpix * lengthSign;
    o_color = fetch(posM);
}
