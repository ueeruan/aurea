#version 450
// Ampliação com preservação de detalhes (referência do AE): reamostragem
// separável centrada, em 16.16 como o efeito nativo. Kernel 0 = cúbico
// a = -0.625 esticado 1,15x (cor); 1 = cúbico a = -0.75 (alfa bicúbico).
// Comportamento descrito em UpscaleReferenceEffect.cpp.
#include "../common/bindings.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_source;
layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    ivec4 sizes;   // origem: largura, altura; destino: largura, altura
    ivec4 axis;    // eixo (0 = x), posição inicial 16.16, passo 16.16, kernel
} p;

float cubic(float x, float a) {
    x = abs(x);
    if (x < 1.0) return ((a + 2.0) * x - (a + 3.0)) * x * x + 1.0;
    if (x < 2.0) return ((a * x - 5.0 * a) * x + 8.0 * a) * x - 4.0 * a;
    return 0.0;
}

void main() {
    ivec2 dst = clamp(ivec2(floor(v_uv * vec2(p.sizes.zw))), ivec2(0), p.sizes.zw - 1);
    bool horizontal = p.axis.x == 0;
    int extent = horizontal ? p.sizes.x : p.sizes.y;
    int d = horizontal ? dst.x : dst.y;
    int pos = p.axis.y + d * p.axis.z;
    bool detail = p.axis.w == 0;
    int halfWidth = detail ? 3 * 65536 : 2 * 65536;
    int first = (pos - halfWidth + 65535) >> 16;
    int last = (pos + halfWidth + 65535) >> 16;
    vec4 sum = vec4(0.0);
    float inside = 0.0;
    float total = 0.0;
    for (int k = first; k < last; ++k) {
        float t = float(k * 65536 - pos) / 65536.0;
        float w = detail ? cubic(t / 1.15, -0.625) : cubic(t, -0.75);
        total += w;
        if (k < 0 || k >= extent) continue;
        ivec2 at = horizontal ? ivec2(k, dst.y) : ivec2(dst.x, k);
        sum += w * texelFetch(u_source, at, 0);
        inside += w;
    }
    // Cor: renormaliza nas amostras dentro da imagem. Alfa bicúbico: o que
    // fica fora conta como transparente.
    vec4 color = inside != 0.0 ? sum / inside : vec4(0.0);
    if (!detail && total != 0.0) color.a = sum.a / total;
    o_color = color;
}
