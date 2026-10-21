// =============================================================================
//  Aurea / shaders / effects / aurora_common.glsl
//
//  O que os dois passes da Aurora (AuroraEffect.cpp) dividem: o bloco de
//  parâmetros, a cortina e o mapa da CÚPULA (azimute × elevação).
//
//  A cúpula guarda a aurora (a parte cara) por DIREÇÃO, só na faixa que a
//  câmera vê: aurora_dome.frag escreve, aurora.frag lê para o céu e para o
//  reflexo. A elevação vai ao quadrado no v: mais texels perto do horizonte,
//  onde as cortinas se espremem.
// =============================================================================
#ifndef AUREA_AURORA_COMMON_GLSL
#define AUREA_AURORA_COMMON_GLSL

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 uvMap;
    vec4 texel;
    vec4 p0;      // x = distorção, y = quantidade, z = escala, w = comprimento
    vec4 p1;      // x = exposição, y = detalhe 0..1, z = curvatura, w = brilho do céu
    vec4 p2;      // região da imagem (px da camada)
    vec4 p3;      // x = relógio, y = horizonte (−1..1), z = reflexo 0..1, w = ondulação 0..1
    vec4 color;   // cor principal (linear)
    vec4 q0;      // rgb = secundária, w = estrelas 0..1
    vec4 q1;      // rgb = céu, w = foco da câmera (1/tan(fov/2))
    vec4 q2;      // x = lâminas, zw = tamanho da camada
    vec4 q3;      // x = opacidade, y = altura das cortinas 0..1, z = cúpula ligada
    vec4 q4;      // cúpula: x = azimute do centro, y = meia faixa, z = elevação máxima (rad)
    vec4 right;   // eixos da câmera no espaço da aurora (y para cima)
    vec4 up;
    vec4 forward;
} p;

const int kMaxSheets = 40;

/// Um arco da aurora (faixa sinuosa) em `u`, a `depth` do observador.
float aurora_arc(vec2 u, float t, float sheet, float depth, float seed) {
    // Distorção: o arco serpenteia e, com mais distorção, enrola em
    // redemoinhos (o sinal escolhe o sentido do giro). Sem distorção (o
    // padrão) o laço inteiro some: ele é 2/3 do custo do arco.
    const float warp = p.p0.x;
    vec2 w = u;
    if (abs(warp) > 1e-4) {
        for (int i = 0; i < 2; ++i) {
            const vec2 n = vec2(aurea_fbm(w * 0.45 + vec2(t * 0.04, seed), 3, 0.5),
                                aurea_fbm(w * 0.45 + vec2(seed * 1.7, -t * 0.03), 3, 0.5)) - 0.5;
            w += vec2(-n.y, n.x) * warp * 2.4 + n * abs(warp) * 0.8;
        }
    }
    const float meander = aurea_fbm(vec2(w.x * 0.22 + t * 0.035, 7.3 + seed), 3, 0.5) * 2.0 - 1.0;
    float center = meander * 1.3;
    // Curvatura: o arco dobra sobre o observador (parábola em profundidade):
    // nas laterais ele desce em direção ao horizonte.
    center += p.p1.z * w.x * w.x * 3.0;
    const float dist = w.y - depth - center;
    // Faixa: borda de baixo (perto) definida, a de cima mais difusa.
    const float width = 0.30 + 0.20 * sheet;
    const float band = exp(-dist * dist / (width * width));
    if (band < 1e-3) return 0.0;   // longe da faixa: os raios não aparecem
    // Raios verticais: ruído fino ao longo do arco, mais fino com o detalhe.
    const float detail = p.p1.y;
    const float rayFreq = 3.0 + detail * 22.0;
    float rays = aurea_fbm(vec2(w.x * rayFreq + center * 4.0 + t * 0.25, t * 0.11 + sheet * 0.15 + seed),
                           2 + int(detail * 3.0), 0.55);
    rays = smoothstep(0.2, 0.9, rays);
    // Pulsação lenta do brilho ao longo do arco.
    const float pulse = 0.55 + 0.45 * aurea_value_noise(vec2(w.x * 0.5 - t * 0.12, 3.7 + seed));
    return band * (0.25 + 2.2 * rays * rays) * pulse;
}

/// A cortina numa lâmina: `q` no plano da lâmina (unidades da cena). Dois
/// arcos: um perto, alto no céu, e outro longe, que chega ao horizonte.
float aurora_curtain(vec2 q, float t, float sheet) {
    const float scale = max(p.p0.z, 0.05) * 0.06;
    const vec2 u = q * scale;
    return aurora_arc(u, t, sheet, 2.5, 0.0) + 0.75 * aurora_arc(u, t * 1.07, sheet, 6.5, 13.0);
}

/// A aurora vista na direção `d` (só acima do horizonte). `jitter` (0..1,
/// parado por texel) desloca as lâminas: poucas lâminas sem faixas visíveis.
vec3 aurora_light(vec3 d, float t, float jitter) {
    if (d.y <= 0.002) return vec3(0.0);
    const float length01 = clamp(p.p0.w, 0.0, 10.0);
    const int sheets = clamp(int(p.q2.x + 0.5), 4, kMaxSheets);
    const float base = mix(1.6, 4.0, clamp(p.q3.y, 0.0, 1.0));   // altura da borda de baixo
    const float span = 0.1 + length01 * 2.4;                       // altura da cortina
    vec3 acc = vec3(0.0);
    for (int i = 0; i < kMaxSheets; ++i) {
        if (i >= sheets) break;
        const float k = (float(i) + jitter) / float(sheets);
        const float height = base + k * span;
        const float dist = height / d.y;
        const vec2 q = d.xz * dist;
        const float c = aurora_curtain(q, t, k);
        // De baixo para cima: forte na base, some no topo (a cauda da aurora).
        const float fade = pow(1.0 - k, 1.6) * (0.35 + 0.65 * smoothstep(0.0, 0.12, k + 0.05));
        const vec3 tint = mix(p.color.rgb, p.q0.rgb, smoothstep(0.15, 0.9, k));
        acc += tint * c * fade;
    }
    acc *= 2.2 / float(sheets);
    // Núcleo: onde a cortina é mais densa ela estoura para um ciano quase branco.
    acc += vec3(0.55, 1.0, 0.95) * pow(max(max(acc.r, acc.g), acc.b), 2.0) * 0.45;
    // Longe, perto do horizonte, a aurora some na névoa.
    acc *= smoothstep(0.0, 0.10, d.y);
    return acc * p.p0.y;
}

/// Direção (y para cima) do texel `uv` da cúpula.
vec3 aurora_dome_dir(vec2 uv) {
    const float az = p.q4.x + (uv.x * 2.0 - 1.0) * p.q4.y;
    const float el = uv.y * uv.y * p.q4.z;
    return vec3(cos(el) * sin(az), sin(el), cos(el) * cos(az));
}

/// Onde a direção `d` (acima do horizonte) cai na cúpula.
vec2 aurora_dome_uv(vec3 d) {
    const float el = asin(clamp(d.y, 0.0, 1.0));
    float az = atan(d.x, d.z) - p.q4.x;
    az -= 6.28318530718 * floor((az + 3.14159265359) / 6.28318530718);
    return vec2(0.5 + 0.5 * az / max(p.q4.y, 1e-4), sqrt(clamp(el / max(p.q4.z, 1e-4), 0.0, 1.0)));
}

#endif
