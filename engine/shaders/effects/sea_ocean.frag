#version 450
// =============================================================================
//  Aurea / shaders / effects / sea_ocean.frag
//
//  Oceano (Gerar, pacote "Mar"): o mar visto da praia, com horizonte. Cada
//  pixel abaixo do horizonte é um raio de uma câmera parada a `H` metros da
//  água, que bate no plano do mar; ali somam-se ondas de Gerstner (uma por
//  comprimento, todas com a direção do vento e um leque pela semente) e um
//  relevo fino de ruído. A normal analítica das ondas dá:
//    - o reflexo do céu (cor em degradê + sol, ou a imagem de uma camada,
//      espelhada pelo raio refletido) pesado por Fresnel (Schlick, água 0,02);
//    - a cor do corpo d'água entre a funda e a rasa (mais rasa nas cristas);
//    - o brilho do sol (especular fino + cintilação nas facetas);
//    - a espuma onde a onda se comprime (o termo Q·k·a·sen de Gerstner).
//  Ondas mais finas que um texel somem pela distância (sem chuvisco no
//  horizonte) e a névoa leva a água à cor do horizonte.
//
//  Acima do horizonte: o céu (degradê + sol), a própria camada ou
//  transparente (a camada escolhida aparece por baixo).
//
//  p0 = horizonte y (px), foco (px), centro x (px), altura da câmera (m)
//  p1 = amplitude da onda principal (m), comprimento (m), tempo × velocidade, agitação 0..1
//  p2 = direção do vento (x, z), nº de ondas, oitavas do relevo fino
//  p3 = direção PARA o sol (x, y, z; y para cima), brilho do sol 0..1
//  p4 = céu (0 cor, 1 a camada, 2 outra camada), recortar pelo alfa, névoa, estilizado
//  p5 = região da outra camada (px da composição)
//  color = cor do céu (linear)
//  q0 = água rasa (rgb), espuma 0..1;  q1 = água funda (rgb), semente
//  q2/q3 = linhas x/y de compFromLayer (a, b, c | d, e, f); q2.w = há outra camada
// =============================================================================
#include "sea_common.glsl"

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // a outra camada (céu)

const int kMaxWaves = 7;
const float kGravity = 9.81;

vec3 sky_gradient(vec3 dir) {
    const vec3 zenith = p.color.rgb;
    const vec3 horizon = mix(p.color.rgb, vec3(1.0), 0.5);
    const float e = max(dir.y, 0.0);
    vec3 sky = mix(horizon, zenith, smoothstep(0.0, 0.45, e));
    const float s = max(dot(dir, p.p3.xyz), 0.0);
    const vec3 sun = vec3(1.0, 0.92, 0.78);
    // Disco + halo do sol (o brilho do sol acende também o céu).
    sky += sun * (pow(s, 900.0) * 6.0 + pow(s, 40.0) * 0.18 + pow(s, 6.0) * 0.06) * (0.35 + p.p3.w);
    return sky;
}

/// Ponto da tela (px da camada) de uma direção acima do horizonte.
vec2 screen_of(vec3 dir) {
    const float z = max(dir.z, 0.05);
    return vec2(p.p0.z + dir.x / z * p.p0.y, p.p0.x - dir.y / z * p.p0.y);
}

/// A imagem do céu (a própria camada ou a escolhida) num ponto da tela.
vec4 sky_image(vec2 px) {
    if (p.p4.x > 1.5) {
        if (p.q2.w < 0.5) return vec4(p.color.rgb, 1.0);
        const vec2 comp = vec2(dot(p.q2.xyz, vec3(px, 1.0)), dot(p.q3.xyz, vec3(px, 1.0)));
        return texture(u_tex1, (comp - p.p5.xy) / max(p.p5.zw, vec2(1e-6)));
    }
    return sea_at(px);
}

vec3 sky_color(vec3 dir) {
    if (p.p4.x < 0.5) return sky_gradient(dir);
    const vec4 c = sky_image(screen_of(dir));
    // Pré-multiplicada: onde a imagem é transparente, o degradê completa.
    return c.rgb + sky_gradient(dir) * (1.0 - c.a) * 0.6;
}

void main() {
    const vec2 px = sea_px();
    const float texel = sea_texel();
    const vec4 base = sea_at(px);
    const float yh = p.p0.x, focal = max(p.p0.y, 1.0), H = max(p.p0.w, 0.1);
    const vec3 ray = normalize(vec3((px.x - p.p0.z) / focal, -(px.y - yh) / focal, 1.0));

    // Céu (sempre: é barato e suaviza a linha do horizonte).
    vec4 sky;
    if (p.p4.x < 0.5) sky = vec4(sky_gradient(ray), 1.0);
    else if (p.p4.x < 1.5) sky = base;
    else sky = vec4(0.0);
    const vec3 horizonColor = p.p4.x < 0.5 ? mix(p.color.rgb, vec3(1.0), 0.5)
                                           : sky_color(vec3(0.0, 0.002, 1.0));

    const float below = (px.y - yh) / texel;   // texels abaixo do horizonte
    if (below <= -1.0) {
        o_color = p.p4.y > 0.5 ? sky * base.a : sky;
        return;
    }

    // --- O ponto do mar ------------------------------------------------------
    const float sy = max((px.y - yh) / focal, 1e-5);
    const float L = max(p.p1.y, 0.05);
    const float zMax = L * 600.0;
    const float z = min(H / sy, zMax);
    const vec2 xz = vec2((px.x - p.p0.z) / focal * z, z);
    // Tamanho de um texel no mar (m): o que é menor que isso vira média.
    const float footprint = texel * z * max(1.0, z / H) / focal;

    const vec2 wind = p.p2.xy;
    const float tau = p.p1.z;
    const float chop = clamp(p.p1.w, 0.0, 1.0);
    const uint seed = uint(max(p.q1.w, 0.0));
    const int waves = clamp(int(p.p2.z + 0.5), 1, kMaxWaves);
    float h = 0.0, compress = 0.0;
    vec3 n = vec3(0.0, 1.0, 0.0);
    float lambda = L, amp = max(p.p1.x, 0.0);
    const float a0 = max(amp, 1e-4);
    for (int i = 0; i < kMaxWaves; ++i) {
        if (i >= waves) break;
        // Leque em volta do vento (a principal segue o vento exato).
        const float spread = i == 0 ? 0.0 : (aurea_hash3(vec2(float(i), 3.0), seed) - 0.5) * 1.3;
        const float cs = cos(spread), sn = sin(spread);
        const vec2 D = vec2(wind.x * cs - wind.y * sn, wind.x * sn + wind.y * cs);
        const float k = AUREA_TAU / lambda;
        const float w = sqrt(kGravity * k);
        const float phase = k * dot(D, xz) - w * tau + AUREA_TAU * aurea_hash3(vec2(float(i), 7.0), seed);
        // Antisserrilhado: a onda some quando cabe em menos de ~3 texels.
        const float aa = clamp(lambda / (footprint * 3.0) - 0.5, 0.0, 1.0);
        const float ka = k * amp * aa;
        const float Q = chop / float(waves) * aa;   // Q·k·a de cada onda: a soma não passa da agitação
        const float s = sin(phase), c = cos(phase);
        h += amp * aa * s;
        n.x -= D.x * ka * c;
        n.z -= D.y * ka * c;
        n.y -= Q * s;
        compress += Q * s;
        lambda *= 0.61;
        amp *= 0.57;
    }
    // Relevo fino (ruído com derivada), rolando com o vento.
    const int octaves = clamp(int(p.p2.w + 0.5), 0, 2);
    float scale = 1.0 / (L * 0.11);
    float gain = 0.10 + 0.25 * chop;
    for (int o = 0; o < 2; ++o) {
        if (o >= octaves) break;
        const float aa = clamp(1.0 / (scale * footprint * 4.0) - 0.5, 0.0, 1.0);
        const vec2 q = xz * scale + wind * (tau * 0.35 * float(o + 1)) + vec2(float(seed % 997u) * 1.37);
        const vec3 nd = sea_noised(q, seed + uint(o) * 13u);
        n.x -= nd.y * gain * aa;
        n.z -= nd.z * gain * aa;
        scale *= 2.3;
        gain *= 0.55;
    }
    n = normalize(n);

    const vec3 V = -ray;
    vec3 R = reflect(ray, n);
    R.y = abs(R.y) + 0.002;
    R = normalize(R);
    const float cosv = clamp(dot(n, V), 0.0, 1.0);
    const float fresnel = 0.02 + 0.98 * pow(1.0 - cosv, 5.0);

    // Corpo d'água: funda → rasa nas cristas (a luz atravessa a onda fina).
    const float crest = clamp(0.5 + 0.5 * h / a0, 0.0, 1.0);
    const vec3 sunDir = p.p3.xyz;
    const float sunlit = max(dot(n, sunDir), 0.0);
    vec3 body = mix(p.q1.rgb, p.q0.rgb, clamp(0.15 + 0.7 * crest * crest, 0.0, 1.0));
    body *= 0.6 + 0.4 * sunlit;
    vec3 col = mix(body, sky_color(R), fresnel);

    // Brilho do sol: especular estreito + facetas que cintilam.
    const vec3 Hh = normalize(sunDir + V);
    const float nh = max(dot(n, Hh), 0.0);
    const float glitter = p.p3.w;
    float spec = pow(nh, 260.0) * 3.0 + pow(nh, 40.0) * 0.12;
    if (glitter > 0.0) {
        const float sparkle = sea_noise(xz * (6.0 / L) + vec2(tau * 1.7, -tau * 1.3), seed + 77u);
        spec *= 1.0 + 5.0 * smoothstep(0.72, 0.95, sparkle);
    }
    col += vec3(1.0, 0.94, 0.82) * spec * glitter * clamp(sunDir.y * 4.0 + 0.2, 0.0, 1.0);

    // Espuma onde a onda se comprime, quebrada por ruído (some ao longe).
    const float foam = p.q0.w;
    if (foam > 0.0) {
        const float breakup = sea_noise(xz * (14.0 / L) + wind * tau * 0.6, seed + 91u);
        const float fade = clamp(L * 0.6 / max(footprint * 8.0, 1e-4), 0.0, 1.0);
        float f = smoothstep(1.0 - foam * 0.85, 1.05, compress / max(chop, 0.05) * 0.85 + breakup * 0.45);
        f *= fade * clamp(foam * 1.6, 0.0, 1.0);
        col = mix(col, vec3(0.92, 0.95, 0.97) * (0.75 + 0.25 * sunlit), clamp(f, 0.0, 1.0));
    }

    // Névoa: ao longe a água vira a cor do horizonte.
    const float haze = p.p4.z;
    const float fog = clamp(1.0 - exp(-z / (L * mix(400.0, 12.0, haze))), 0.0, 1.0);
    col = mix(col, horizonColor, max(fog, smoothstep(zMax * 0.5, zMax, z)));

    // Estilizado: faixas de cor chapadas (desenho), em vez do degradê real.
    const float stylize = p.p4.w;
    if (stylize > 0.0) {
        const float levels = 5.0;
        const vec3 toon = floor(aurea_linear_to_srgb(max(col, vec3(0.0))) * levels + 0.5) / levels;
        col = mix(col, aurea_srgb_to_linear(toon), stylize);
    }

    vec4 sea = vec4(max(col, vec3(0.0)), 1.0);
    // A linha do horizonte em um texel (sem degrau).
    const float k = clamp(below + 0.5, 0.0, 1.0);
    vec4 outc = mix(sky, sea, k);
    if (p.p4.y > 0.5) outc *= base.a;
    o_color = outc;
}
