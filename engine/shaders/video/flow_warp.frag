#version 450
// =============================================================================
//  Aurea / shaders / video / flow_warp.frag
//
//  Quadro intermediário por movimento de pixels (Twixtor), nos DOIS sentidos.
//  No instante t entre o quadro atual A e o seguinte B:
//    * de A: o pixel que chega em p saiu de xa = p − t·F(xa)  (F = A → B);
//    * de B: o pixel que chega em p saiu de xb = p − (1−t)·G(xb)  (G = B → A).
//
//  1.0.3 (filmagem real: gente, jogo, cena de filme). O quadro antigo caía na
//  MISTURA dos dois quadros no lugar sempre que um lado falhava ou os dois
//  discordavam — em filmagem real isso é quase toda borda de braço, rosto e
//  roupa, e virava fantasma. Agora:
//
//  1. Várias hipóteses por lado, ordenadas por oclusão (o equivalente em
//     "gather" do softmax splatting, Niklaus & Liu 2020): além do ponto fixo
//     que parte do vetor do próprio pixel, um que parte do maior vetor da
//     vizinhança (o objeto rápido que chega). Cada hipótese só vale se o
//     pixel de origem cai mesmo em p (resíduo do ponto fixo) e pesa pela
//     importância exp(Z), Z = −(erro fotométrico na chegada)² − (erro de ida e
//     volta)²: o fundo que vai ser coberto não acha a si mesmo no outro
//     quadro e perde para o objeto que passa na frente. A primeira hipótese
//     tem prioridade (a segunda precisa ser claramente melhor): o HUD parado
//     de um jogo não é trocado pelo fundo que corre atrás dele.
//  2. Confiança por pixel e por lado — ida e volta, resíduo fotométrico
//     normalizado pela textura local (o equivalente da confiança do
//     Lucas-Kanade: num degrau forte, meio pixel de erro já muda a cor) e
//     origem dentro do quadro — e mistura PONDERADA pela confiança, não um
//     liga/desliga.
//  3. Confiança baixa vai para o quadro MAIS PRÓXIMO deformado (sem fantasma),
//     não para a mistura. O controle "Fantasma ↔ Deformação" (blend.w = β)
//     escolhe o lado: β = 0 aceita fantasma (mistura no lugar, como antes,
//     quando os vetores falham), β = 1 confia nos vetores até o fim.
//
//  image.w = 0 roda o algoritmo de antes (comparação nos testes).
// =============================================================================
#include "../common/bindings.glsl"
#include "flow_common.glsl"

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = AUREA_TEX0) uniform sampler2D u_a;
layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_b;
layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_flow;    // RG = A→B, BA = B→A (px do nível base)
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_tileF;   // RG = maior vetor A→B da vizinhança
layout(set = 0, binding = AUREA_TEX4) uniform sampler2D u_tileG;   // RG = maior vetor B→A da vizinhança

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 blend;   // x = t, yz = uv por pixel do nível base, w = β (0 fantasma … 1 deformação)
    vec4 image;   // xy = uv por pixel, z = hipóteses por lado, w = legado (0), local (1), compartilhado (2)
} p;

float inside(vec2 uv) {
    return (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0) ? 1.0 : 0.0;
}

float tone(vec3 c) { return sqrt(max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 0.0)); }

vec4 img(int side, vec2 uv) { return side == 0 ? textureLod(u_a, uv, 0.0) : textureLod(u_b, uv, 0.0); }
vec2 own_flow(int side, vec2 uv) { const vec4 f = textureLod(u_flow, uv, 0.0); return side == 0 ? f.rg : f.ba; }
vec2 back_flow(int side, vec2 uv) { const vec4 f = textureLod(u_flow, uv, 0.0); return side == 0 ? f.ba : f.rg; }

// O algoritmo de antes (0.1.8), para comparação.
vec4 legacy() {
    const float t = p.blend.x;
    const vec2 scale = p.blend.yz;
    vec2 fa = textureLod(u_flow, v_uv, 0.0).rg;
    for (int i = 0; i < 3; ++i) fa = textureLod(u_flow, v_uv - t * fa * scale, 0.0).rg;
    const vec2 xa = v_uv - t * fa * scale;
    const vec2 fxa = textureLod(u_flow, xa, 0.0).rg;
    const float ra = t * length(fxa - fa);
    const float ca = length(fxa + textureLod(u_flow, xa + fxa * scale, 0.0).ba);
    vec2 fb = textureLod(u_flow, v_uv, 0.0).ba;
    for (int i = 0; i < 3; ++i) fb = textureLod(u_flow, v_uv - (1.0 - t) * fb * scale, 0.0).ba;
    const vec2 xb = v_uv - (1.0 - t) * fb * scale;
    const vec2 gxb = textureLod(u_flow, xb, 0.0).ba;
    const float rb = (1.0 - t) * length(gxb - fb);
    const float cb = length(gxb + textureLod(u_flow, xb + gxb * scale, 0.0).rg);
    const vec4 a = textureLod(u_a, xa, 0.0);
    const vec4 b = textureLod(u_b, xb, 0.0);
    const float va = inside(xa) * exp(-ra * ra * 1.4) * exp(-ca * ca * 0.3);
    const float vb = inside(xb) * exp(-rb * rb * 1.4) * exp(-cb * cb * 0.3);
    const float wa = (1.0 - t) * va + 1e-4;
    const float wb = t * vb + 1e-4;
    const vec4 moved = (a * wa + b * wb) / (wa + wb);
    const vec4 still = mix(textureLod(u_a, v_uv, 0.0), textureLod(u_b, v_uv, 0.0), t);
    const float disagree = min(va, vb) * smoothstep(0.10, 0.28, abs(tone(a.rgb) - tone(b.rgb)));
    const float lost = 1.0 - smoothstep(0.02, 0.15, max(va, vb));
    return mix(moved, still, max(lost, disagree));
}

// Uma hipótese de um lado: acumula a cor (peso softmax) e devolve a confiança.
float hypothesis(int side, vec2 seed, float s, float prior, inout vec4 acc, inout float wsum) {
    const vec2 scale = p.blend.yz;
    vec2 trial = seed;
    vec2 x = v_uv;
    vec2 fx = seed;
    float residual = 1e8;
    // At a motion boundary the inverse search can jump back and forth from
    // foreground to background. Keep the in-bounds iterate that actually
    // projects closest to this pixel, using the same four flow samples.
    for (int i = 0; i < 4; ++i) {
        const vec2 origin = v_uv - s * trial * scale;
        const vec2 field = own_flow(side, origin);
        const vec2 error = s * (field - trial);
        const float cost = dot(error, error) + (1.0 - inside(origin)) * 1e7;
        if (cost <= residual) {
            x = origin;
            fx = field;
            residual = cost;
        }
        trial = field;
    }
    const vec2 destination = x + fx * scale;
    const vec2 roundTrip = fx + back_flow(side, destination);
    const vec4 c = img(side, x);
    const vec3 here = flow_ycc(c.rgb);
    const vec3 there = flow_ycc(img(1 - side, destination).rgb);
    const vec3 d = here - there;
    // Textura local (gradiente da luminância em x, por pixel da imagem): num
    // degrau forte, o erro de meio pixel do vetor já muda a cor — o resíduo
    // é normalizado por isso (a confiança do rastreio, na deformação).
    const float gx = flow_ycc(img(side, x + vec2(p.image.x, 0.0)).rgb).x - here.x;
    const float gy = flow_ycc(img(side, x + vec2(0.0, p.image.y)).rgb).x - here.x;
    const float sigma2 = 0.0025 + 0.5 * (gx * gx + gy * gy);
    const float photo = flow_color_dist2(d.x, d.yz, 1.0) / sigma2;
    const float geo = inside(x) * exp(-residual * 1.4);
    const float cycle = mix(0.12, 1.0, inside(destination)) * exp(-dot(roundTrip, roundTrip) * 0.3);
    const float weight = geo * cycle * exp(-photo);
    const float w = weight * prior;
    acc += c * w;
    wsum += w;
    return geo * cycle * exp(-0.25 * photo);
}

vec4 coherent_warp(vec4 localFlow, float t, vec4 fallback) {
    const vec2 scale = p.blend.yz;
    vec2 motion = mix(localFlow.rg, -localFlow.ba, t);
    // Share one displacement between both frames, and damp the inverse search
    // at motion boundaries. Hard switches between independently fitted warps
    // can cut pieces out of a foreground object in consecutive frames.
    for (int i = 0; i < 2; ++i) {
        const vec2 forward = own_flow(0, v_uv - t * motion * scale);
        const vec2 backward = own_flow(1, v_uv + (1.0 - t) * motion * scale);
        motion = mix(motion, mix(forward, -backward, t), 0.5);
    }
    const vec2 a = v_uv - t * motion * scale;
    const vec2 b = v_uv + (1.0 - t) * motion * scale;
    const float wa = (1.0 - t) * inside(a);
    const float wb = t * inside(b);
    if (wa + wb < 1e-5) return fallback;
    return (img(0, a) * wa + img(1, b) * wb) / (wa + wb);
}

void main() {
    const float t = p.blend.x;
    const vec4 localFlow = textureLod(u_flow, v_uv, 0.0);
    // Corte de cena (flow_pack.frag): não existe quadro intermediário.
    if (localFlow.r > 5000.0) {
        o_color = t < 0.5 ? textureLod(u_a, v_uv, 0.0) : textureLod(u_b, v_uv, 0.0);
        return;
    }
    if (p.image.w < 0.5) {
        o_color = legacy();
        return;
    }
    const float beta = clamp(p.blend.w, 0.0, 1.0);
    const bool twoHypotheses = p.image.z > 1.5;
    const vec4 source[2] = vec4[2](img(0, v_uv), img(1, v_uv));
    const vec4 still = mix(source[0], source[1], t);
    if (p.image.w > 1.5) {
        o_color = mix(still, coherent_warp(localFlow, t, still), smoothstep(0.0, 0.45, beta));
        return;
    }
    vec4 col[2];
    float conf[2];
    for (int side = 0; side < 2; ++side) {
        const float s = side == 0 ? t : 1.0 - t;
        // Fade a discarded correspondence into the source, with an anchor
        // small enough to leave genuinely matched motion alone.
        float wsum = 1e-7;
        vec4 acc = source[side] * wsum;
        const vec2 seed = side == 0 ? localFlow.rg : localFlow.ba;
        float c = hypothesis(side, seed, s, 1.0, acc, wsum);
        if (twoHypotheses) {
            // O maior vetor da vizinhança: o objeto que chega a p vindo de longe.
            const vec2 far = side == 0 ? textureLod(u_tileF, v_uv, 0.0).rg : textureLod(u_tileG, v_uv, 0.0).rg;
            const float separation = length(far - seed) * s;
            if (separation > 0.45) {
                const float secondWeight = smoothstep(0.45, 1.1, separation);
                c = max(c, secondWeight * hypothesis(side, far, s, 0.3 * secondWeight, acc, wsum));
            }
        }
        col[side] = acc / wsum;
        conf[side] = c;
    }
    // Mistura pela confiança e pela distância no tempo.
    const float wa = (1.0 - t) * conf[0] + 1e-5;
    const float wb = t * conf[1] + 1e-5;
    // Os dois lados afirmam ter o pixel mas trazem cores diferentes: um deles
    // está errado. Em vez de somar (fantasma), o mais confiável domina — tanto
    // mais quanto mais perto de "Deformação".
    const vec3 dd = flow_ycc(col[0].rgb) - flow_ycc(col[1].rgb);
    const float disagree = smoothstep(0.03, 0.12, sqrt(flow_color_dist2(dd.x, dd.yz, 1.0)));
    const float trust = max(conf[0], conf[1]);
    const float k = 1.0 + 7.0 * beta * disagree * smoothstep(0.04, 0.25, trust);
    const float ra = wa / (wa + wb);
    const float pa = pow(ra, k) / (pow(ra, k) + pow(1.0 - ra, k));
    const vec4 moved = mix(col[1], col[0], pa);
    // Nenhum lado confiável: o quadro mais próximo, deformado (sem fantasma);
    // perto de "Fantasma", a mistura no lugar, como no algoritmo de antes.
    // In an occluded or rotating region both correspondences may be poor.
    // Keep that transition coherent in time instead of promoting small
    // confidence variations into a pixel-by-pixel hard switch.
    const float near0 = (1.0 - t) * (1.0 - t) * (conf[0] + 0.14);
    const float near1 = t * t * (conf[1] + 0.14);
    const float rn = near0 / (near0 + near1);
    const float pn = pow(rn, 4.0) / (pow(rn, 4.0) + pow(1.0 - rn, 4.0));
    const float lost = 1.0 - smoothstep(mix(0.12, 0.0, beta), mix(0.40, 0.09, beta), trust);
    vec4 result = moved;
    if (lost > 0.0) {
        vec4 nearest = mix(col[1], col[0], pn);
        // When neither inverse projection is reliable, use one shared
        // displacement for both samples. This keeps a weakly tracked patch
        // from alternating between two unrelated trajectories.
        if (beta > 0.0) {
            const vec2 shift = mix(localFlow.rg, -localFlow.ba, t) * p.blend.yz;
            const vec2 a = v_uv - t * shift;
            const vec2 b = v_uv + (1.0 - t) * shift;
            const float usable = inside(a) * inside(b);
            const float paired = (1.0 - smoothstep(0.01, 0.09, trust)) * usable;
            if (paired > 0.0) nearest = mix(nearest, mix(img(1, b), img(0, a), pn), paired);
        }
        const vec4 fallback = mix(still, nearest, smoothstep(0.0, 0.5, beta));
        result = mix(moved, fallback, lost);
    }
    const float speed = max(length(localFlow.rg), length(localFlow.ba));
    const float matched = smoothstep(0.05, 0.28, min(conf[0], conf[1]));
    const float unstable = smoothstep(2.0, 6.0, speed) * (1.0 - matched) * smoothstep(0.0, 0.45, beta);
    if (unstable > 0.0) result = mix(result, coherent_warp(localFlow, t, result), unstable);
    // Parado de verdade: o pixel e os vizinhos têm a MESMA cor nos dois
    // quadros (HUD de jogo, legenda queimada, logo, cenário fixo). Um campo de
    // vetores medido com janela arrasta a linha fina do HUD junto com o fundo
    // que corre atrás; aqui o próprio pixel fica.
    float d2 = 0.0;
    float stationaryContrast = 0.0;
    const vec3 center = flow_ycc(source[0].rgb);
    for (int k2 = 0; k2 < 5; ++k2) {
        const vec2 o = k2 == 0 ? vec2(0.0) : vec2(k2 == 1 ? 1.5 : (k2 == 2 ? -1.5 : 0.0), k2 == 3 ? 1.5 : (k2 == 4 ? -1.5 : 0.0));
        const vec2 uv = v_uv + o * p.image.xy;
        const vec3 a = k2 == 0 ? center : flow_ycc(img(0, uv).rgb);
        const vec3 b = flow_ycc((k2 == 0 ? source[1] : img(1, uv)).rgb);
        const vec3 sd = a - b;
        d2 += flow_color_dist2(sd.x, sd.yz, 1.0);
        const vec3 edge = a - center;
        stationaryContrast = max(stationaryContrast, dot(edge, edge));
    }
    // Só quando o vetor do próprio pixel NÃO explica o pixel (o HUD que o
    // fluxo do fundo atravessa); numa textura que se repete, a cor igual no
    // mesmo lugar é coincidência e o vetor explica o pixel.
    const vec2 scale = p.blend.yz;
    const vec3 ea = center - flow_ycc(img(1, v_uv + localFlow.rg * scale).rgb);
    const vec3 eb = flow_ycc(source[1].rgb) - flow_ycc(img(0, v_uv + localFlow.ba * scale).rgb);
    const float unexplained = min(flow_color_dist2(ea.x, ea.yz, 1.0), flow_color_dist2(eb.x, eb.yz, 1.0));
    // Matching source pixels can still be covered only in the middle of
    // the interval. Avoid pinning a smooth background through a moving
    // foreground; retain the static override on genuinely crisp overlays.
    const vec3 novel = flow_ycc(result.rgb) - flow_ycc(still.rgb);
    const float entering = smoothstep(0.012, 0.16, trust) * smoothstep(0.0016, 0.0144, dot(novel, novel));
    const float overlayEdge = smoothstep(0.0016, 0.0144, stationaryContrast);
    const float staticness = exp(-(d2 / 5.0) / (2.0 * 0.02 * 0.02)) *
        (1.0 - exp(-unexplained / (2.0 * 0.04 * 0.04))) * (1.0 - entering * (1.0 - overlayEdge));
    o_color = mix(result, still, staticness);
}
