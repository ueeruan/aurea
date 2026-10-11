// =============================================================================
//  Aurea / shaders / scene3d / common / shadow.glsl
//
//  Sombra suave da luz principal (direcional, mapa ortográfico) — PCSS:
//
//    1. normal offset: a consulta sai da superfície ao longo da normal
//       GEOMÉTRICA (mais em ângulo rasante) — some a acne sem empurrar a
//       sombra para longe do contato (o viés constante fica pequeno);
//    2. busca de bloqueadores (profundidade crua, disco de Vogel girado por
//       pixel com ruído de gradiente intercalado): profundidade média de
//       quem bloqueia;
//    3. PCF de raio variável com o sampler de COMPARAÇÃO do hardware (cada
//       amostra já é um PCF 2×2 bilinear): raio = k·(receptor − bloqueador) —
//       contato nítido, penumbra que abre com a distância.
//
//  Sem busca (params2.w = 0, níveis BAIXO/MÉDIO): PCF de raio fixo (1,5 texel).
//
//  Parâmetros (SceneRenderer.cpp preenche; mesmas unidades para qualquer
//  shader que queira receber a sombra — o chão do estúdio, por exemplo):
//    params  = (amostras do PCF, 0 = sem sombra; texel em uv; viés constante
//               em profundidade; —)
//    params2 = (k: raio uv por unidade de profundidade (tan do raio angular
//               da luz × R/S); normal offset no mundo (px); S/R: quanto a
//               profundidade muda por uv numa superfície de inclinação 1;
//               amostras da busca de bloqueadores, 0 = PCF fixo)
//  Profundidade do mapa: Z comum, 0 = perto da luz, limpa em 1.
// =============================================================================
#ifndef AUREA_SHADOW_GLSL
#define AUREA_SHADOW_GLSL

// Ruído de gradiente intercalado (Jimenez 2014): estável por pixel, sem textura.
float aurea_shadow_ign(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// Disco de Vogel (espiral de ouro): n amostras bem espalhadas em qualquer n.
vec2 aurea_shadow_vogel(int i, int n, float phi) {
    float r = sqrt((float(i) + 0.5) / float(n));
    float t = float(i) * 2.39996323 + phi;
    return r * vec2(cos(t), sin(t));
}

// Raio máximo da penumbra em uv: 2·tan do raio angular da luz — a penumbra de
// um receptor até ~2 larguras do mapa atrás do bloqueador. O ajuste do mapa
// (SceneRenderer.cpp) deixa essa folga em volta dos projetores, e o valor
// não depende do tamanho do mapa: preview e export mostram a MESMA sombra.
float aurea_shadow_max_radius(vec4 params2) {
    return min(2.0 * params2.x / max(params2.z, 1e-6), 0.12);
}

// 1 = iluminado, 0 = na sombra. `Ng` = normal geométrica (sem mapa de
// normal), `L` = direção PARA a luz, `pixel` = gl_FragCoord.xy.
float aurea_shadow(sampler2DShadow cmpMap, sampler2D depthMap, mat4 shadowMatrix, vec4 params, vec4 params2,
                   vec3 world, vec3 Ng, vec3 L, vec2 pixel) {
    int taps = int(params.x + 0.5);
    if (taps <= 0) return 1.0;
    float NdotL = clamp(dot(Ng, L), 0.0, 1.0);
    float sinT = sqrt(max(1.0 - NdotL * NdotL, 0.0));
    // Inclinação limitada: em ângulo rasante quem segura a acne é o normal
    // offset (e o viés de inclinação do raster no passe do mapa) — um viés
    // de receptor sem teto apagaria a sombra no terminador de uma curva.
    float tanT = min(sinT / max(NdotL, 0.05), 2.0);
    // Normal offset: ~1 texel no mundo, mais em ângulo rasante.
    vec3 wp = world + Ng * (params2.y * max(sinT, 0.25));
    vec4 sc = shadowMatrix * vec4(wp, 1.0);
    vec3 c = sc.xyz / sc.w;
    if (c.x <= 0.0 || c.x >= 1.0 || c.y <= 0.0 || c.y >= 1.0 || c.z <= 0.0) return 1.0;
    // Receptor atrás do último bloqueador (chão longe): a comparação usa 1
    // (continua recebendo); a distância da penumbra usa a profundidade real
    // (a ortográfica é linear além do plano distante).
    float zr = c.z;
    float z = min(zr, 1.0);
    float texel = params.y;
    float bias = params.z;
    // Quanto a profundidade do PRÓPRIO receptor muda a uma distância uv
    // (plano inclinado em relação à luz): sem isso o kernel largo se sombreia.
    float slope = params2.z * tanT;
    float phi = aurea_shadow_ign(pixel) * 6.2831853;
    float radius = 1.5 * texel;
    int blockers = int(params2.w + 0.5);
    float maxRadius = max(aurea_shadow_max_radius(params2), 2.0 * texel);
    float slopeCap = 1e9;
    if (blockers > 0) {
        float search = clamp(params2.x * zr, 2.0 * texel, maxRadius);
        float sum = 0.0, count = 0.0;
        for (int i = 0; i < 32; ++i) {
            if (i >= blockers) break;
            vec2 o = aurea_shadow_vogel(i, blockers, phi) * search;
            float d = textureLod(depthMap, c.xy + o, 0.0).r;
            // Viés de inclinação limitado a poucos texels: um receptor curvo
            // (esfera) não pode esconder o bloqueador de verdade.
            if (d < z - bias - slope * min(length(o) + texel, 3.0 * texel)) {
                sum += d;
                count += 1.0;
            }
        }
        if (count < 0.5) return 1.0;   // nenhum bloqueador por perto: luz cheia, sem PCF
        float zb = sum / count;
        radius = clamp(params2.x * (zr - zb), texel, maxRadius);
        // O viés do kernel largo nunca passa da metade da distância até o
        // bloqueador (senão a própria sombra some em superfície inclinada).
        slopeCap = max(0.5 * (zr - zb), slope * 2.0 * texel);
    }
    float lit = 0.0;
    for (int i = 0; i < 32; ++i) {
        if (i >= taps) break;
        vec2 o = aurea_shadow_vogel(i, taps, phi) * radius;
        // + texel: a pegada do filtro bilinear da comparação.
        float ref = z - bias - min(slope * (length(o) + texel), slopeCap);
        lit += textureLod(cmpMap, vec3(c.xy + o, ref), 0.0);
    }
    return lit / float(taps);
}

#endif
