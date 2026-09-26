#version 450
// =============================================================================
//  Aurea / shaders / particles / particles.vert  —  AUREA PARTICULAR
//
//  Partículas ANALÍTICAS: cada instância é um "slot" que emite em sequência
//  (nascimento = slot/taxa + k·período). Posição, vida, tamanho, cor, rotação e
//  colisão saem de uma conta FECHADA sobre (semente, slot, geração k, tempo) —
//  sem estado de simulação em lugar nenhum.
//
//  O que isso compra, e por que vale a restrição:
//    - determinístico por construção: prévia e export dão o MESMO quadro;
//    - seek instantâneo em qualquer ponto: não há estado para reconstruir, e
//      por isso não há checkpoint para guardar nem simulação desde o quadro 0;
//    - nenhum buffer de partículas na GPU: 100 mil partículas custam o mesmo
//      que 100 — só vértices.
//
//  A RESTRIÇÃO É REAL e está paga: uma força que só existe integrada passo a
//  passo NÃO entra aqui. Todas as que entraram têm forma fechada:
//    gravidade/vento  → ½·a·t²                       (integração direta)
//    arrasto          → v·(1−e^(−k·t))/k             (exponencial)
//    vórtice          → rotação de ω·t em torno do emissor
//    atrator/repulsor → mola radial: (cos(√k·t)−1)·r
//    turbulência      → SOMA DE SENOIDES de três eixos, com amplitude que
//                       cresce com a idade. É fechada, é contínua e dá o visual
//                       de campo de fluido sem integrar ruído nenhum.
//
//  COMO AS FORÇAS SE COMBINAM: por SUPERPOSIÇÃO dos deslocamentos, cada um
//  calculado como se fosse a única força. Não é a integração exata do sistema
//  acoplado (arrasto agindo sobre a gravidade, por exemplo) — é a aproximação
//  que mantém tudo fechado, e é o que um sistema analítico pode prometer. O
//  resultado é estável, contínuo e igual na prévia e no export.
//
//  8.2: emissão de outra camada (pontos no storage buffer), curvas ao longo da
//  vida, aleatórios, colisão esfera/caixa, partícula de textura e de MALHA (o
//  mesmo shader: no passe de malha, gl_VertexIndex é o vértice da malha e a
//  instância continua sendo a partícula). Contas novas em particles_extras.glsl.
//
//  Espaço: pixels da camada, y para baixo; gravidade já convertida no C++.
//
//  CENA 3D, ESPAÇO MUNDO E DESFOQUE (8.2): a conta acima dá a posição no
//  espaço da CAMADA. Para onde ela vai depois — clip direto (o 2D de sempre),
//  composição (2D no espaço mundo / com desfoque) ou mundo 3D com billboard
//  voltado para a câmera — é decidido no fim, em `ps_place`, pelos bits de
//  `pc.mode.x` e pelo histórico (binding AUREA_DATA1). Sem bits, nada disso
//  roda e o caminho é o mesmo de antes.
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    /// 2D: clip ← camada (como sempre). Espaço da composição: clip ←
    /// composição. 3D: clip ← mundo (a MESMA viewProj da cena).
    mat4 clipFromLayer;
    vec4 mode;       // bits PS_*, deslocamento de tempo (s), início do bloco no histórico (vec4), peso da amostra
    vec4 camRight;   // 3D: eixo X da câmera no mundo
    vec4 camDown;    // 3D: eixo Y da câmera (para baixo) no mundo
} pc;

// Histórico da camada no quadro (render/ParticleScene.cpp). A partir de `pc.mode.z`:
//   +0..3  saída ← camada AGORA (mat4, colunas)
//   +4     t0 (s, tempo local), passo (s), nº de amostras, taxa máxima (a agenda)
//   +5     gravidade Z, vento Z (px/s², + = para dentro), _, _
//   +6+5i  amostra i: 3 linhas da matriz saída ← camada (afim), emissão A
//          (taxa, velocidade, direção, espalhamento), emissão B (origem x, y,
//          velocidade da camada x, y)
layout(set = 0, binding = AUREA_DATA1, std430) readonly buffer ParticleHistory { vec4 ph[]; };

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 emit;      // taxa (/s), vida (s), velocidade (px/s), espalhamento (rad)
    vec4 force;     // gravidade x, y (px/s², y para baixo), tamanho inicial, final
    vec4 look;      // opacidade inicial, final, direção (rad), semente
    vec4 area;      // emissor largura, altura, tempo (s), nº de slots
    vec4 colorA;    // cor inicial (linear, reta)
    vec4 colorB;    // cor final
    vec4 origin;    // centro do emissor (px da camada), _, _
    // --- Aurea Particular ---------------------------------------------------
    vec4 emitShape; // tipo de emissor, raio, rotação (rad), profundidade (Z)
    vec4 grid;      // colunas, linhas, esfera cheia, estouro (burst)
    vec4 emission;  // variação da vida, variação da velocidade, herdar movimento, forma
    vec4 partLook;  // maciez, rotação (rad), variação da rotação (rad), giro (rad/s)
    vec4 physics;   // arrasto, vento x, vento y, turbulência
    vec4 physics2;  // escala da turbulência, velocidade da turbulência, vórtice (rad/s), atrator
    vec4 trail;     // segundos de rastro, afinamento, nº de aux, momento do aux
    vec4 aux;       // vida do aux, velocidade do aux, tamanho do aux, abertura do aux (rad)
    vec4 collide;   // modo, plano em Y, elasticidade, _
    vec4 auxTint;   // cor do aux
    vec4 motion;    // velocidade da camada (px/s) x, y, _, _
    vec4 shapeAux;  // proporção da camada (l/a), _, _, _
    vec4 extras;    // (8.2) slots do fluxo, 1º vec4 do cabeçalho, dados prontos, passe de malha
} p;

layout(location = 0) out vec2 v_local;
layout(location = 1) out vec4 v_color;
layout(location = 2) out float v_soft;
layout(location = 3) out float v_shape;   // 0/3 = redondo, 1/2 = quadrado, 4 textura, 5 malha
layout(location = 4) out vec4 v_misc;     // rastro: opacidade da cauda, é rastro; uv da textura
layout(location = 5) out vec4 v_normal;   // malha: normal (px da camada), iluminada

// Quatro cantos + índices (0,1,2 / 1,3,2): com o desenho INDEXADO a GPU
// reaproveita os vértices repetidos da instância — 4 execuções deste shader
// por partícula em vez de 6 (8E: o vértice é o custo das partículas).
const vec2 kCorners[4] = vec2[4](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));

const float kPi = 3.14159265;

uint pcg(uint v) {
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
float rnd(inout uint s) { s = pcg(s); return float(s) * (1.0 / 4294967296.0); }

#include "particle_world.glsl"

#include "particles_extras.glsl"
// =============================================================================
//  ESPAÇO DA CENA (8.2) — cena 3D, espaço mundo, histórico e desfoque.
//  Chamado de pontos únicos do main; com `pc.mode.x == 0` nenhum destes
//  caminhos lê o histórico.
// =============================================================================
const uint PS_BUFFER = 1u;    // há bloco no histórico (cabeçalho válido)
const uint PS_3D     = 2u;    // billboard no mundo 3D, projetado pela câmera da cena
const uint PS_WORLD  = 4u;    // espaço mundo: a matriz do NASCIMENTO de cada partícula
const uint PS_MOVING = 8u;    // matriz de saída no instante do desenho (subamostra do desfoque)
const uint PS_EMIT   = 16u;   // emissão (taxa, velocidade, direção...) no instante do nascimento

uint ps_flags() { return uint(pc.mode.x + 0.5); }
bool ps_has(uint f) { return (ps_flags() & f) != 0u; }
/// O tempo do desenho: o do quadro + o deslocamento da subamostra do obturador.
float ps_time() { return p.area.z + pc.mode.y; }
int ps_base() { return int(pc.mode.z + 0.5); }

/// Parâmetros de emissão que um keyframe NÃO pode mudar retroativamente.
struct PEmit { float rate; float speed; float dir; float spread; vec2 origin; vec2 motion; };

/// Vec4 `field` (0..4) da amostra do histórico, interpolada no tempo `tt` (s).
vec4 ps_hist(float tt, int field) {
    const vec4 info = ph[ps_base() + 4];
    const float u = clamp((tt - info.x) / max(info.y, 1e-6), 0.0, max(info.z - 1.0, 0.0));
    const int i0 = int(floor(u));
    const int i1 = min(i0 + 1, int(info.z + 0.5) - 1);
    const int b = ps_base() + 6;
    return mix(ph[b + i0 * 5 + field], ph[b + i1 * 5 + field], u - float(i0));
}

PEmit ps_emit(float birth) {
    if (!ps_has(PS_EMIT)) return PEmit(p.emit.x, p.emit.z, p.look.z, p.emit.w, p.origin.xy, p.motion.xy);
    const vec4 a = ps_hist(birth, 3);
    const vec4 b = ps_hist(birth, 4);
    return PEmit(a.x, a.y, a.z, a.w, b.xy, b.zw);
}

/// Taxa animada sem "pular" partículas: a agenda roda na taxa MÁXIMA da
/// camada (`p.emit.x`) e cada nascimento é aceito com probabilidade
/// taxa(nascimento)/taxa máxima — sorteio próprio por (slot, geração,
/// semente), determinístico e fora do sorteio da aparência.
bool ps_accept(float birthRate, float slot, float gen) {
    if (!ps_has(PS_EMIT)) return true;
    const float ratio = birthRate / max(p.emit.x, 1e-3);
    if (ratio >= 1.0) return true;
    const uint h = pcg(uint(slot) * 2654435761u ^ pcg(uint(gen) * 40503u ^ uint(p.look.w) * 97u ^ 0x68E31DA4u));
    return float(h) * (1.0 / 4294967296.0) < ratio;
}

/// Cone 3D: no 3D o espalhamento abre também para fora do plano XY (faixa
/// uniforme na esfera, meia abertura = metade do espalhamento, até ±90°).
/// Devolve (cos, sen) da elevação; no 2D é (1, 0) e nada muda.
vec2 ps_cone(float spreadRad, float slot, float gen) {
    if (!ps_has(PS_3D)) return vec2(1.0, 0.0);
    const uint h = pcg(uint(slot) * 7919u ^ pcg(uint(gen) * 104729u ^ uint(p.look.w) * 31u ^ 0x2545F491u));
    const float halfE = min(spreadRad * 0.5, 1.5707963);
    const float se = (float(h) * (1.0 / 4294967296.0) * 2.0 - 1.0) * sin(halfE);
    return vec2(sqrt(max(0.0, 1.0 - se * se)), se);
}

/// Gravidade + vento em Z (px/s², + = para dentro). Só no 3D.
float ps_accel_z() {
    if (!ps_has(PS_3D)) return 0.0;
    const vec4 z = ph[ps_base() + 5];
    return z.x + z.y;
}

/// Z da partícula (px da camada, + = para dentro), SEM a gravidade (ela vem
/// de `ps_gravity`). As mesmas forças fechadas do XY: arrasto no vz,
/// turbulência com um terceiro eixo e o atrator puxando também em Z.
float ps_depth(float z0, float vz0, float age, float gen) {
    if (!ps_has(PS_3D)) return 0.0;
    const float dragK = max(p.physics.x, 0.0);
    float z = z0 + ((dragK > 1e-4) ? vz0 * (1.0 - exp(-dragK * age)) / dragK : vz0 * age);
    const float turb = p.physics.w;
    if (turb > 1e-3) {
        const float sc = max(p.physics2.x, 0.05);
        const float tt = age * max(p.physics2.y, 0.0);
        const float base = gen * 0.37 + p.look.w * 0.13;
        const float ramp = sqrt(max(age, 0.0)) * turb * sc * 0.5;
        z += ramp * (cos(base * 2.3 + 0.9) * (1.0 - cos(tt * 1.3 + 0.7))
                   + sin(base * 1.1 + 3.1) * (1.0 - cos(tt * 0.8 + 2.2)) * 0.6);
    }
    const float spring = p.physics2.w;
    if (abs(spring) > 1e-4) z += z0 * (cos(sqrt(abs(spring)) * age) - 1.0) * sign(spring);
    return z;
}

/// Deslocamento da gravidade + vento (XY já está na conta do main; aqui ele é
/// SEPARADO para o espaço mundo agir nos eixos do mundo) e Z.
vec3 ps_gravity(vec2 acc, float age) {
    return 0.5 * vec3(acc, ps_accel_z()) * age * age;
}

/// As três linhas da matriz saída ← camada que valem para esta partícula:
/// espaço mundo = a do nascimento; desfoque = a do instante da subamostra;
/// senão, a de agora (cabeçalho).
void ps_rows(float birth, out vec4 r0, out vec4 r1, out vec4 r2) {
    if (ps_has(PS_WORLD) || ps_has(PS_MOVING)) {
        const float tt = ps_has(PS_WORLD) ? birth : ps_time();
        r0 = ps_hist(tt, 0);
        r1 = ps_hist(tt, 1);
        r2 = ps_hist(tt, 2);
        return;
    }
    const int b = ps_base();
    r0 = vec4(ph[b].x, ph[b + 1].x, ph[b + 2].x, ph[b + 3].x);
    r1 = vec4(ph[b].y, ph[b + 1].y, ph[b + 2].y, ph[b + 3].y);
    r2 = vec4(ph[b].z, ph[b + 1].z, ph[b + 2].z, ph[b + 3].z);
}

vec3 ps_xform(vec4 r0, vec4 r1, vec4 r2, vec3 q) {
    const vec4 h = vec4(q, 1.0);
    return vec3(dot(r0, h), dot(r1, h), dot(r2, h));
}

/// Ponto da partícula na SAÍDA (camada, composição ou mundo). No espaço mundo
/// sem colisão, a gravidade sai do quadro do nascimento e entra nos eixos da
/// saída (cai para baixo mesmo com o emissor girado); com colisão (o quique
/// usa a gravidade) tudo fica no quadro do nascimento.
vec3 ps_out_point(vec3 q, float birth, vec3 grav, out vec4 r0, out vec4 r1, out vec4 r2) {
    ps_rows(birth, r0, r1, r2);
    if (ps_has(PS_WORLD) && p.collide.x < 0.5) return ps_xform(r0, r1, r2, q - grav) + grav;
    return ps_xform(r0, r1, r2, q);
}

/// Posição final: `pos` (px da camada, com Z) + `corner` (canto do quadrado,
/// px da camada, já girado). 2D sem histórico: exatamente a conta de antes.
void ps_place(vec3 pos, vec2 corner, float birth, vec3 grav) {
    v_color *= pc.mode.w;
    if (!ps_has(PS_BUFFER)) {
        gl_Position = pc.clipFromLayer * vec4(pos.xy + corner, 0.0, 1.0);
        return;
    }
    vec4 r0, r1, r2;
    if (!ps_has(PS_3D)) {
        const vec3 o = ps_out_point(vec3(pos.xy + corner, 0.0), birth, vec3(grav.xy, 0.0), r0, r1, r2);
        gl_Position = pc.clipFromLayer * vec4(o.xy, 0.0, 1.0);
        return;
    }
    // Billboard: o centro vai ao mundo; o canto anda nos eixos da CÂMERA, na
    // escala da camada (nulo pai com escala 2 = partícula 2× maior). A
    // perspectiva faz o resto: longe = menor.
    const vec3 cw = ps_out_point(pos, birth, grav, r0, r1, r2);
    const float sc = length(vec3(r0.x, r1.x, r2.x));
    gl_Position = pc.clipFromLayer * vec4(cw + (pc.camRight.xyz * corner.x + pc.camDown.xyz * corner.y) * sc, 1.0);
}

/// Rastro: no 2D, a cunha no plano da camada (como antes); no 3D, uma fita
/// voltada para a câmera ao longo da velocidade no mundo (`axis`, px da
/// camada, já com o comprimento e o lado da ponta).
void ps_place_streak(vec3 pos, vec2 offset2d, vec3 axis, float halfW, float birth, vec3 grav) {
    if (!ps_has(PS_3D)) {
        ps_place(pos, offset2d, birth, grav);
        return;
    }
    v_color *= pc.mode.w;
    vec4 r0, r1, r2;
    const vec3 cw = ps_out_point(pos, birth, grav, r0, r1, r2);
    const vec3 aw = vec3(dot(r0.xyz, axis), dot(r1.xyz, axis), dot(r2.xyz, axis));
    const float sc = length(vec3(r0.x, r1.x, r2.x));
    vec3 perp = cross(aw, cross(pc.camRight.xyz, pc.camDown.xyz));
    perp = (length(perp) > 1e-5) ? normalize(perp) : pc.camRight.xyz;
    gl_Position = pc.clipFromLayer * vec4(cw + aw + perp * (halfW * sc), 1.0);
}

/// Ponto de nascimento dentro da forma escolhida. Fechado: uma amostra, sem
/// rejeição e sem laço — o pior caso de um emissor de grade 64×64 é uma
/// multiplicação.
vec3 shape_offset(float type, inout uint s) {
    const float w = p.area.x;
    const float h = p.area.y;
    if (world_particle()) {
        if (p.emitShape.x > 12.5) return vec3((rnd(s)-.5)*w, (rnd(s)-.5)*h, (rnd(s)-.5)*p.emitShape.w);
        vec3 direction = world_direction(s);
        return direction * (p.emitShape.y * pow(rnd(s), 1.0/3.0));
    }
    if (type < 0.5) return vec3(0.0);                       // ponto
    if (type < 1.5) {                                        // caixa
        return vec3((vec2(rnd(s), rnd(s)) - 0.5) * vec2(w, h),
                    (rnd(s) - 0.5) * p.emitShape.w);
    }
    if (type < 2.5) {                                        // esfera
        const float u = rnd(s) * 2.0 - 1.0;
        const float phi = rnd(s) * 2.0 * kPi;
        const float r = (p.grid.z > 0.5) ? pow(rnd(s), 1.0 / 3.0) : 1.0;
        const float rho = sqrt(max(0.0, 1.0 - u * u));
        return vec3(rho * cos(phi), rho * sin(phi), u) * (p.emitShape.y * r);
    }
    if (type < 3.5) {                                        // disco
        const float a = rnd(s) * 2.0 * kPi;
        const float r = sqrt(rnd(s)) * p.emitShape.y;
        return vec3(cos(a + p.emitShape.z), sin(a + p.emitShape.z), 0.0) * r;
    }
    if (type < 4.5) {                                        // linha
        const float t = rnd(s) - 0.5;
        const vec2 dir = vec2(cos(p.emitShape.z), sin(p.emitShape.z));
        return vec3(dir * t * w, 0.0);
    }
    if (type < 5.5) {                                        // grade
        const vec2 cells = max(p.grid.xy, vec2(1.0));
        const vec2 a = floor(vec2(rnd(s), rnd(s)) * cells);
        const vec2 uv = (a + 0.5) / cells - 0.5;
        return vec3(uv * vec2(w, h), 0.0);
    }
    return vec3((vec2(rnd(s), rnd(s)) - 0.5) * vec2(w, h), 0.0);   // fonte ainda sem pontos: caixa
}

/// Onde a partícula nasce (px da camada; z = profundidade). Camada, texto,
/// caminho e malha leem os pontos da fonte (já absolutos); as formas, a conta
/// de cima em volta de `origin` — o centro do emissor NO NASCIMENTO (ps_emit),
/// somado uma vez só.
vec3 emit_start(float type, vec2 origin, inout uint s) {
    vec3 q;
    if (type > 5.5 && type < 9.5 && emit_from_source(s, q)) return q;
    return vec3(origin, 0.0) + shape_offset(type, s);
}

/// O que é FIXO numa partícula (sai do nascimento) — a trajetória inteira é
/// uma função disto e da idade.
struct Particle {
    vec2 start;
    float z;
    vec2 ctr;
    vec2 v0;
    vec2 acc;
    float k;
};

/// Posição na idade `age`: deslocamento linear com arrasto + gravidade/vento
/// + turbulência + atrator, girado pelo vórtice. É a trajetória do sistema —
/// a colisão procura o toque NELA, e não numa aproximação à parte.
vec2 particle_path(Particle P, float age) {
    // v(t) = v0·e^(−k·t)  →  d(t) = v0·(1−e^(−k·t))/k.  k→0 cai em v0·t.
    const float dragK = max(p.physics.x, 0.0);
    const vec2 linear = dragK > 1e-4 ? P.v0 * (1.0 - exp(-dragK * age)) / dragK : P.v0 * age;
    vec2 disp = 0.5 * P.acc * age * age;

    // Turbulência: soma de três senoides em eixos diferentes. A amplitude
    // cresce com a raiz da idade — começa reta e vai abrindo, como campo de
    // fluido. Tudo fechado, tudo igual na prévia e no export.
    const float turb = p.physics.w;
    if (turb > 1e-3) {
        const float sc = max(p.physics2.x, 0.05);
        const float sp = max(p.physics2.y, 0.0);
        const float base = P.k * 0.37 + p.look.w * 0.13;
        const float tt = age * sp;
        const float ramp = sqrt(max(age, 0.0)) * turb * sc * 0.5;
        const vec2 dir0 = vec2(cos(base), sin(base));
        const vec2 dir1 = vec2(cos(base * 1.7 + 2.1), sin(base * 1.7 + 2.1));
        const vec2 dir2 = vec2(cos(base * 2.9 + 4.2), sin(base * 2.9 + 4.2));
        disp += ramp * (dir0 * (1.0 - cos(tt * 1.0))
                      + dir1 * (1.0 - cos(tt * 1.7 + 1.3)) * 0.7
                      + dir2 * (1.0 - cos(tt * 0.6 + 2.7)) * 0.5);
    }

    // Atrator / repulsor (mola radial).
    const float spring = p.physics2.w;
    if (abs(spring) > 1e-4) {
        const float w = sqrt(abs(spring));
        disp += (P.start - P.ctr) * (cos(w * age) - 1.0) * sign(spring);
    }

    vec2 pos = P.start + linear + disp;

    // Vórtice (rotação em torno do emissor).
    const float vort = p.physics2.z;
    if (abs(vort) > 1e-4) {
        const float a = vort * age;
        const vec2 rel = pos - P.ctr;
        const float ca = cos(a), sa = sin(a);
        pos = P.ctr + vec2(rel.x * ca - rel.y * sa, rel.x * sa + rel.y * ca);
    }
    return pos;
}

/// Plano horizontal: acha o instante do toque pela quadrática, reflete a
/// velocidade normal com a elasticidade e segue fechado depois disso.
vec2 collide_plane(Particle P, vec2 pos, float age) {
    const float floorY = p.collide.y;
    const float a2 = 0.5 * P.acc.y;
    const float b2 = P.v0.y;
    const float c2 = P.start.y - floorY;
    const float disc = b2 * b2 - 4.0 * a2 * c2;
    if (disc > 0.0 && abs(a2) > 1e-6) {
        const float sq = sqrt(disc);
        const float t1 = (-b2 + sq) / (2.0 * a2);
        const float t2 = (-b2 - sq) / (2.0 * a2);
        const float th = min(t1 > 0.0 ? t1 : 1e9, t2 > 0.0 ? t2 : 1e9);
        if (th < age) {
            const float vyHit = P.v0.y + P.acc.y * th;
            const float rest = age - th;
            const float bounce = -vyHit * clamp(p.collide.z, 0.0, 1.0);
            pos.y = floorY + bounce * rest + 0.5 * P.acc.y * rest * rest;
        }
    }
    return pos;
}

/// Esfera/caixa, quicando POR FORA. APROXIMAÇÃO FECHADA, documentada:
///   1. o 1º toque é procurado na trajetória verdadeira (`particle_path`) por
///      amostragem fixa (24 passos) + bisseção (8) — sem estado, igual em
///      qualquer quadro e em qualquer ordem de seek;
///   2. no toque, a velocidade (diferença finita da trajetória) é refletida
///      pela normal com a elasticidade; dali em diante o voo é balístico
///      (v' + gravidade/vento), como no plano — até 2 quiques novos, achados
///      do mesmo jeito;
///   3. o que ainda ficou dentro do volume (deslizando, bounce 0) é empurrado
///      para a superfície pela normal: nenhuma partícula termina DENTRO.
/// Nascer dentro do volume = nascer na superfície dele.
vec2 collide_volume(inout Particle P, float age) {
    vec3 s0 = vec3(P.start, P.z);
    const float d0 = col_sdf(s0);
    if (d0 < 0.0) P.start -= col_normal(s0).xy * d0;

    const int kSteps = 24;
    float ta = 0.0;
    float fa = col_sdf(vec3(particle_path(P, 0.0), P.z));
    float th = -1.0;
    for (int i = 1; i <= kSteps; ++i) {
        const float tb = age * float(i) / float(kSteps);
        const float fb = col_sdf(vec3(particle_path(P, tb), P.z));
        if (fa > 0.0 && fb <= 0.0) {
            float lo = ta, hi = tb;
            for (int j = 0; j < 8; ++j) {
                const float mid = 0.5 * (lo + hi);
                if (col_sdf(vec3(particle_path(P, mid), P.z)) > 0.0) lo = mid; else hi = mid;
            }
            th = lo;
            break;
        }
        ta = tb;
        fa = fb;
    }
    vec2 pos = particle_path(P, age);
    if (th >= 0.0) {
        const float e = col_bounce();
        const float dt = max(1e-3, th * 1e-3);
        vec2 hit = particle_path(P, th);
        vec2 vel = (hit - particle_path(P, max(th - dt, 0.0))) / dt;
        float t0 = th;
        for (int b = 0; b < 3; ++b) {
            const vec3 n = col_normal(vec3(hit, P.z));
            const float vn = dot(vel, n.xy);
            if (vn < 0.0) vel -= (1.0 + e) * vn * n.xy;
            // Voo balístico do quique até agora (ou até o próximo toque).
            const float rest = age - t0;
            pos = hit + vel * rest + 0.5 * P.acc * rest * rest;
            if (b == 2 || col_sdf(vec3(pos, P.z)) > 0.0) break;
            // Voltou para dentro: acha o novo toque nesse voo (fechado: a
            // parábola é conhecida) e quica de novo.
            float lo = 0.0, hi = rest;
            for (int j = 0; j < 12; ++j) {
                const float mid = 0.5 * (lo + hi);
                const vec2 q = hit + vel * mid + 0.5 * P.acc * mid * mid;
                if (col_sdf(vec3(q, P.z)) > 0.0) lo = mid; else hi = mid;
            }
            hit = hit + vel * lo + 0.5 * P.acc * lo * lo;
            vel = vel + P.acc * lo;
            t0 += lo;
            if (lo < 1e-4) break;   // encostado: o empurrão abaixo resolve
        }
    }
    const float df = col_sdf(vec3(pos, P.z));
    if (df < 0.0) pos -= col_normal(vec3(pos, P.z)).xy * df;
    return pos;
}

/// Vértice final da partícula: quad (canto `c`) ou vértice da malha.
void emit_vertex(vec2 pos, float z, float size, float rot, vec4 colPremul, vec2 c, inout uint s2,
                 float birth, vec3 grav) {
    v_color = colPremul;
    if (px_mesh()) {
        vec3 mp, mn;
        vec4 mc;
        mesh_vertex(gl_VertexIndex, mp, mn, mc);
        // Eixo de giro por partícula (determinístico): malha tomba em 3D, não
        // só gira no plano.
        vec3 axis = vec3(rnd(s2), rnd(s2), rnd(s2)) - 0.5;
        axis = length(axis) > 1e-3 ? normalize(axis) : vec3(0.0, 0.0, 1.0);
        const float k = size * mesh_scale();
        const vec3 local = rotate_axis(mp, axis, rot) * k;
        v_normal = vec4(rotate_axis(mn, axis, rot), mesh_lit() ? 1.0 : 0.0);
        v_color = vec4(colPremul.rgb * mc.rgb, colPremul.a) * mc.a;
        v_local = vec2(0.0);
        v_shape = 5.0;
        if (ps_has(PS_3D)) {
            // Cena 3D: o centro vai ao mundo pelo histórico (como o billboard)
            // e a malha fica de frente para a câmera, com o Z dela no eixo de
            // visão — volume de verdade contra os modelos (o passe escreve depth).
            v_color *= pc.mode.w;
            vec4 r0, r1, r2;
            const vec3 cw = ps_out_point(vec3(pos, z), birth, grav, r0, r1, r2);
            const float sc = length(vec3(r0.x, r1.x, r2.x));
            const vec3 fwd = normalize(cross(pc.camRight.xyz, pc.camDown.xyz));
            gl_Position = pc.clipFromLayer
                        * vec4(cw + (pc.camRight.xyz * local.x + pc.camDown.xyz * local.y + fwd * local.z) * sc, 1.0);
            return;
        }
        // A saída passa pelo MESMO ps_place do quad (o vértice da malha é o
        // "canto"): 2D de sempre, espaço mundo e desfoque valem para a malha.
        ps_place(vec3(pos, z), local.xy, birth, grav);
        // Profundidade do passe de malha (Z reverso: perto = maior). Faixa de
        // ±8192 px em torno do plano da camada. No 3D quem manda é a câmera.
        if (!ps_has(PS_3D)) gl_Position.z = clamp(0.5 - (z + local.z) / 16384.0, 0.0, 1.0) * gl_Position.w;
        return;
    }
    const vec2 local = c * 2.0 - 1.0;
    const float cs = cos(rot), sn = sin(rot);
    const vec2 rl = vec2(local.x * cs - local.y * sn, local.x * sn + local.y * cs);
    v_local = rl;
    v_misc.zw = c;
    // Textura: a imagem CABE no quadrado com a proporção dela (H1.w = l/a); o
    // que sobra do quadrado sai do uv [0,1] e o fragmento descarta.
    if (v_shape > 3.5 && v_shape < 4.5) {
        const float asp = max(px_h(1).w, 1e-3);
        v_misc.zw = 0.5 + (c - 0.5) * (asp >= 1.0 ? vec2(1.0, asp) : vec2(1.0 / asp, 1.0));
    }
    // Saída: no 2D sem histórico é a MESMA conta de antes (posição + canto
    // girado); no 3D o billboard vai ao mundo pelos eixos da câmera da cena.
    ps_place(vec3(pos, z), rl * size * 0.5, birth, grav);
}

void main() {
    const bool meshPass = px_mesh();
    const vec2 c = meshPass ? vec2(0.5) : kCorners[gl_VertexIndex & 3];
    const float rate = max(p.emit.x, 1e-3);
    const float slots = max(p.area.w, 1.0);
    const float t = ps_time();

    // Aux: as instâncias de índice >= slots são as secundárias. Cada primária
    // gera `auxCount`, então o total é slots·(1+auxCount) e o par
    // (primária, sub) sai de uma divisão inteira.
    const int auxPer = int(p.trail.z);
    const float primarySlots = slots;
    const float total = slots * (1.0 + float(auxPer));
    const float idx = float(gl_InstanceIndex);
    const bool isAux = idx >= primarySlots;
    const float primaryIdx = isAux ? mod(floor(idx - primarySlots), primarySlots)
                                   : idx;

    // As instancias ACIMA do fluxo continuo sao o ESTOURO: nascem todas no
    // instante zero, e nao na vez delas na fila. Sem isto um preset so de
    // estouro (taxa 0) nunca acenderia nada — foi o que o teste dos dez
    // presets pegou.
    const float flowSlots = p.shapeAux.y;
    const bool isBurst = primaryIdx >= flowSlots;
    const float first = isBurst ? 0.0 : primaryIdx / rate;
    // Burst slots fire once at t=0; they are not part of the repeating flow
    // schedule. Including them creates long emission gaps after the burst.
    const float period = max(flowSlots, 1.0) / rate;
    // Longe da tela = não desenha (sem ramificar o rasterizador).
    gl_Position = vec4(4.0, 4.0, 0.0, 1.0);
    v_local = vec2(0.0);
    v_color = vec4(0.0);
    v_misc = vec4(1.0, 0.0, 0.0, 0.0);
    v_normal = vec4(0.0, 0.0, -1.0, 0.0);
    // Maciez efetiva: a forma "Soft" e macia por definicao, independente
    // do controle — pedir poeira de luz com borda dura nao faz sentido.
    v_soft = (p.emission.w > 2.5) ? max(p.partLook.x, 0.85) : p.partLook.x;
    // Textura sem imagem pronta desenha o disco (nunca um quadrado vazio).
    v_shape = (p.emission.w > 3.5 && p.emission.w < 4.5 && !px_texture()) ? 0.0 : p.emission.w;
    if (t < first) return;
    if (total > 4000000.0) return;

    const float k = isBurst ? 0.0 : floor((t - first) / period);
    const float birth = isBurst ? 0.0 : first + k * period;
    const float ageAll = t - birth;

    // Emissão no instante do NASCIMENTO (keyframe depois não muda quem já
    // nasceu) e a aceitação da taxa animada.
    const PEmit em = ps_emit(birth);
    if (!isBurst && !ps_accept(em.rate, primaryIdx, k)) return;

    uint s = pcg(uint(primaryIdx) * 9781u ^ pcg(uint(k) * 6271u ^ uint(p.look.w)));
    // Fluxo SEPARADO para aparência e aux (8.2): ligar um aleatório não muda
    // a trajetória de ninguém — e projeto antigo continua igual.
    uint s2 = pcg(s ^ 0x68E31DA4u);
    const float auxRoll = rnd(s2);
    const float lifeRand = 1.0 + (rnd(s) - 0.5) * 2.0 * clamp(p.emission.x, 0.0, 1.0);
    const float life = max(p.emit.y * lifeRand, 1e-4);

    // ---- Partícula secundária -------------------------------------------------
    if (isAux) {
        // Probabilidade: a primária inteira gera (ou não) as secundárias.
        if (auxRoll >= aux_probability()) return;
        // Ela nasce quando a primária chega em `auxAt` da vida dela; o que
        // sobra da vida da primária é o tempo que a aux tem para aparecer.
        float at = clamp(p.trail.w, 0.0, 0.95) * life;
        if (world_particle()) {
            float sub = floor((idx - primarySlots) / primarySlots);
            at = life * (sub + .5) / max(1.0, float(auxPer));
        }
        if (ageAll < at) return;
        const float auxAge = ageAll - at;
        const float auxLife = max(p.aux.x, 1e-4);
        if (auxAge >= auxLife) return;

        // Posição da primária no instante do nascimento — recalculada aqui, e
        // não guardada: é o preço (baixo) de não ter estado.
        const float ang0 = em.dir + (rnd(s) - 0.5) * em.spread;
        const float spd0 = em.speed * (1.0 + (rnd(s) - 0.5) * 2.0 * p.emission.y);
        // O emissor estendido manda sobre a forma simples: e dele que saem os
        // pontos de nascimento (imagem, texto, forma, mascara, modelo).
        const vec3 st = emit_start(p.emitShape.x, em.origin, s);
        const vec2 start = st.xy;
        const vec2 cone0 = ps_cone(em.spread, primaryIdx, k);
        const vec2 v0 = spd0 * cone0.x * vec2(cos(ang0), sin(ang0)) + em.motion * p.emission.z;
        const vec2 acc = vec2(p.force.x, p.force.y) + p.physics.yz;
        vec2 pAt = start + v0 * at + 0.5 * acc * at * at;
        float zAt = ps_depth(st.z, spd0 * cone0.y, at, k);
        if (world_particle()) {
            vec3 velocity = world_velocity(spd0, em.dir, em.spread, s);
            velocity.xy += em.motion * p.emission.z;
            vec3 ignoredVelocity, ignoredGravity;
            vec3 point = world_path(st, vec3(em.origin,0), velocity,
                vec3(acc, ps_accel_z()), at, ignoredVelocity, ignoredGravity);
            pAt = point.xy; zAt = point.z;
        }

        const float aAng = rnd(s) * 2.0 * kPi;
        const float aSpd = p.aux.y * (0.6 + 0.8 * rnd(s));
        const float aSpreadRad = p.aux.w;
        const float dir = aAng * (aSpreadRad / (2.0 * kPi));
        const vec2 aVel = aSpd * vec2(cos(dir), sin(dir));
        const vec2 aPos = pAt + aVel * auxAge + 0.5 * acc * auxAge * auxAge;

        const float au = auxAge / auxLife;
        const float aSize = max(p.aux.z * (1.0 - au), 0.0);
        if (aSize <= 0.01) return;
        const vec4 ac = world_particle() ? vec4(life_color(clamp(at/life,0.0,1.0),p.colorA.rgb,p.colorB.rgb),p.auxTint.a) : p.auxTint;
        const float aOp = (1.0 - au) * ac.a;
        v_soft = 0.6;
        if (!meshPass) v_shape = 0.0;   // secundária: disco macio
        // Gravidade da primária até o nascimento do aux + a do aux depois.
        const vec3 aGrav = ps_gravity(acc, at) + ps_gravity(acc, auxAge);
        float extraZ = world_particle() ? ps_gravity(acc, auxAge).z : aGrav.z;
        emit_vertex(aPos, zAt + extraZ, aSize, 0.0, vec4(ac.rgb * aOp, aOp), c, s2, birth, aGrav);
        return;
    }

    if (ageAll >= life) return;

    // ---- Nascimento ----------------------------------------------------------
    const float ang = em.dir + (rnd(s) - 0.5) * em.spread;
    const float spd = em.speed * (1.0 + (rnd(s) - 0.5) * 2.0 * p.emission.y);
    const vec3 st = emit_start(p.emitShape.x, em.origin, s);
    const vec2 cone = ps_cone(em.spread, primaryIdx, k);
    Particle P;
    P.start = st.xy;
    P.z = st.z;
    P.ctr = em.origin;
    P.v0 = spd * cone.x * vec2(cos(ang), sin(ang)) + em.motion * p.emission.z;
    P.acc = vec2(p.force.x, p.force.y) + p.physics.yz;
    P.k = k;

    const float age = ageAll;
    vec2 pos;
    vec3 grav;
    float posZ;
    vec3 currentVelocity;
    if (world_particle()) {
        vec3 velocity = world_velocity(spd, em.dir, em.spread, s);
        velocity.xy += em.motion * p.emission.z;
        vec3 result = world_path(st, vec3(em.origin, 0), velocity, vec3(P.acc, ps_accel_z()),
                                 age, currentVelocity, grav);
        pos = result.xy; posZ = result.z;
    } else {
        if (col_volume()) pos = collide_volume(P, age);
        else {
            pos = particle_path(P, age);
            if (p.collide.x > 0.5 && p.collide.x < 1.5) pos = collide_plane(P, pos, age);
        }
        grav = ps_gravity(P.acc, age);
        posZ = ps_depth(P.z, spd * cone.y, age, k) + grav.z;
        currentVelocity = vec3(P.v0 + P.acc * age, spd * cone.y + ps_accel_z() * age);
    }

    // ---- Ao longo da vida ---------------------------------------------------
    const float u = age / life;
    float sizeMul, opMul;
    vec3 tint;
    random_look(s2, sizeMul, opMul, tint);
    const vec3 rgb = life_color(u, p.colorA.rgb, p.colorB.rgb) * tint;
    const float alphaA = mix(p.colorA.a, p.colorB.a, u);
    const float op = clamp(life_opacity(u, p.look.x, p.look.y) * opMul, 0.0, 1.0);
    const float a = alphaA * op;
    if (a <= 0.001) return;

    // Rotação: base + variação + giro acumulado.
    const float rot = p.partLook.y + (rnd(s) - 0.5) * 2.0 * p.partLook.z + p.partLook.w * age;

    const float size = max(life_size(u, p.force.z, p.force.w), 0.0) * sizeMul;

    // Rastro: estica no sentido do movimento, e afina com a idade.
    if (p.trail.x > 1e-4 && !meshPass && (!world_particle() || p.emission.w == 2.0)) {
        const vec2 vel = currentVelocity.xy;
        const float vlen = length(vel);
        if (vlen > 1e-3) {
            const vec2 tl = trail_look();
            const vec2 dir = vel / vlen;
            const vec2 perp = vec2(-dir.y, dir.x);
            const float minHalf = max(size, 0.5) * 0.5;
            const float taper = mix(1.0, max(1.0 - u, 0.05), clamp(p.trail.y, 0.0, 1.0));
            const float len = max(vlen * p.trail.x * 0.5 * taper, minHalf);
            // O quad vira uma cunha: a ponta fica no lado de onde veio.
            const float along = c.y * 2.0 - 1.0;
            const float wNow = max(size * 0.5 * mix(taper, 1.0, 0.35) * tl.x, 0.35);
            v_local = vec2(c.x * 2.0 - 1.0, along);
            v_color = vec4(rgb * a, a);
            // A cauda (along = −1) sai com a opacidade do rastro.
            v_misc = vec4(tl.y, 1.0, c);
            // 3D: a fita segue a velocidade com Z (velocidade + gravidade Z).
            const vec3 vel3 = currentVelocity;
            ps_place_streak(vec3(pos, posZ), dir * (along * len) + perp * ((c.x * 2.0 - 1.0) * wNow),
                            vel3 * (along * len / vlen),
                            (c.x * 2.0 - 1.0) * wNow, birth, grav);
            return;
        }
    }

    emit_vertex(pos, posZ, size, rot, vec4(rgb * a, a), c, s2, birth, grav);
}
