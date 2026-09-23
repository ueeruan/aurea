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
// =============================================================================
#include "../common/bindings.glsl"

layout(push_constant) uniform Push {
    mat4 clipFromLayer;
} pc;

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

#include "particles_extras.glsl"

/// Ponto de nascimento dentro da forma escolhida. Fechado: uma amostra, sem
/// rejeição e sem laço — o pior caso de um emissor de grade 64×64 é uma
/// multiplicação.
vec3 shape_offset(float type, inout uint s) {
    const float w = p.area.x;
    const float h = p.area.y;
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
/// caminho e malha leem os pontos da fonte; as formas, a conta de cima.
vec3 emit_start(float type, inout uint s) {
    vec3 q;
    if (type > 5.5 && emit_from_source(s, q)) return q;
    return vec3(p.origin.xy, 0.0) + shape_offset(type, s);
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
void emit_vertex(vec2 pos, float z, float size, float rot, vec4 colPremul, vec2 c, inout uint s2) {
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
        gl_Position = pc.clipFromLayer * vec4(pos + local.xy, 0.0, 1.0);
        // Profundidade do passe de malha (Z reverso: perto = maior). Faixa de
        // ±8192 px em torno do plano da camada.
        gl_Position.z = clamp(0.5 - (z + local.z) / 16384.0, 0.0, 1.0) * gl_Position.w;
        return;
    }
    const vec2 local = c * 2.0 - 1.0;
    const float cs = cos(rot), sn = sin(rot);
    const vec2 rl = vec2(local.x * cs - local.y * sn, local.x * sn + local.y * cs);
    v_local = rl;
    v_misc.zw = c;
    gl_Position = pc.clipFromLayer * vec4(pos + rl * size * 0.5, 0.0, 1.0);
}

void main() {
    const bool meshPass = px_mesh();
    const vec2 c = meshPass ? vec2(0.5) : kCorners[gl_VertexIndex & 3];
    const float rate = max(p.emit.x, 1e-3);
    const float slots = max(p.area.w, 1.0);
    const float t = p.area.z;

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
    const float period = p.area.w / rate;
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
        const float at = clamp(p.trail.w, 0.0, 0.95) * life;
        if (ageAll < at) return;
        const float auxAge = ageAll - at;
        const float auxLife = max(p.aux.x, 1e-4);
        if (auxAge >= auxLife) return;

        // Posição da primária no instante do nascimento — recalculada aqui, e
        // não guardada: é o preço (baixo) de não ter estado.
        const float ang0 = p.look.z + (rnd(s) - 0.5) * p.emit.w;
        const float spd0 = p.emit.z * (1.0 + (rnd(s) - 0.5) * 2.0 * p.emission.y);
        const vec3 st = emit_start(p.emitShape.x, s);
        const vec2 start = st.xy;
        const vec2 v0 = spd0 * vec2(cos(ang0), sin(ang0)) + p.motion.xy * p.emission.z;
        const vec2 acc = vec2(p.force.x, p.force.y) + p.physics.yz;
        const vec2 pAt = start + v0 * at + 0.5 * acc * at * at;

        const float aAng = rnd(s) * 2.0 * kPi;
        const float aSpd = p.aux.y * (0.6 + 0.8 * rnd(s));
        const float aSpreadRad = p.aux.w;
        const float dir = aAng * (aSpreadRad / (2.0 * kPi));
        const vec2 aVel = aSpd * vec2(cos(dir), sin(dir));
        const vec2 aPos = pAt + aVel * auxAge + 0.5 * acc * auxAge * auxAge;

        const float au = auxAge / auxLife;
        const float aSize = max(p.aux.z * (1.0 - au), 0.0);
        if (aSize <= 0.01) return;
        const vec4 ac = p.auxTint;
        const float aOp = (1.0 - au) * ac.a;
        v_soft = 0.6;
        if (!meshPass) v_shape = 0.0;   // secundária: disco macio
        emit_vertex(aPos, st.z, aSize, 0.0, vec4(ac.rgb * aOp, aOp), c, s2);
        return;
    }

    if (ageAll >= life) return;

    // ---- Nascimento ----------------------------------------------------------
    const float ang = p.look.z + (rnd(s) - 0.5) * p.emit.w;
    const float spd = p.emit.z * (1.0 + (rnd(s) - 0.5) * 2.0 * p.emission.y);
    const vec3 st = emit_start(p.emitShape.x, s);
    Particle P;
    P.start = st.xy;
    P.z = st.z;
    P.ctr = p.origin.xy;
    P.v0 = spd * vec2(cos(ang), sin(ang)) + p.motion.xy * p.emission.z;
    P.acc = vec2(p.force.x, p.force.y) + p.physics.yz;
    P.k = k;

    const float age = ageAll;
    vec2 pos;
    if (col_volume()) {
        pos = collide_volume(P, age);
    } else {
        pos = particle_path(P, age);
        if (p.collide.x > 0.5 && p.collide.x < 1.5) pos = collide_plane(P, pos, age);
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
    if (p.trail.x > 1e-4 && !meshPass) {
        const vec2 vel = P.v0 + P.acc * age;
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
            pos += dir * (along * len) + perp * ((c.x * 2.0 - 1.0) * wNow);
            v_local = vec2(c.x * 2.0 - 1.0, along);
            v_color = vec4(rgb * a, a);
            // A cauda (along = −1) sai com a opacidade do rastro.
            v_misc = vec4(tl.y, 1.0, c);
            gl_Position = pc.clipFromLayer * vec4(pos, 0.0, 1.0);
            return;
        }
    }

    emit_vertex(pos, P.z, size, rot, vec4(rgb * a, a), c, s2);
}
