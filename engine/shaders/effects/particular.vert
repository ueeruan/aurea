#version 450
// =============================================================================
//  Aurea / shaders / effects / particular.vert
//
//  Partículas 3D AUREA: cada
//  partícula é um quadrado gerado aqui, sem vertex buffer (6 vértices por
//  slot). A simulação é FECHADA: posição, tamanho, cor e opacidade saem de
//  (slot, geração, semente, tempo) — nada de estado entre quadros. Por isso a
//  prévia e o export dão o mesmo quadro, pular no tempo é instantâneo e a
//  pré-rolagem é só somar tempo.
//
//  Espaço: px da camada, y para BAIXO, +z para LONGE (como a composição). A
//  câmera vem da composição. Sem câmera, o efeito 2D usa uma projeção local
//  de 40° na vertical; uma partícula em z > 0 encolhe em direção ao centro.
//
//  Slots: uma GRADE fixa de nascimentos (`clock.y` por segundo) em ciclos de
//  `clock.z` slots; a taxa pedida só decide QUANTOS slots disparam (sorteio
//  fixo por slot). Animar a taxa não re-sincroniza o jato inteiro.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;    // região de saída (px da camada)
    vec4 view;      // centro da projeção (px), distância focal (px), -
    vec4 clock;     // agora (s, com a pré-rolagem), grade (slots/s), slots, taxa (/s)
    vec4 frame;     // composição (px): largura, altura; origem do emissor (px da camada)
    vec4 emitPos;   // posição x, y, z (fração do quadro), emissor esférico (0/1)
    vec4 emitSize;  // tamanho x, y, z (fração do quadro), semente
    vec4 launch;    // velocidade (px/s), variação 0..1, inclinação (rad), giro (rad)
    vec4 cone;      // abertura (graus), para fora 0..1, esticar, suavidade da borda
    vec4 forces;    // gravidade (px/s², +y desce), vento x, y, z (px/s)
    vec4 motion;    // arrasto (1/s), turbulência (px), velocidade da turbulência,
                    // MEIA janela do obturador (s; 0 = sem desfoque de movimento)
    vec4 lifeSize;  // vida (ms), variação 0..1, tamanho (px), variação 0..1
    vec4 look;      // tamanho final (×), opacidade, surgir, sumir (frações da vida)
    vec4 color0;    // cor inicial (linear), cor aleatória 0..1
    vec4 color1;    // cor final (linear)
    // Camada do Particular: o espaço 3D da camada e a câmera da composição.
    mat4 worldFromLayer;   // px da camada (z para longe) -> mundo
    mat4 compFromWorld;    // mundo -> px da composição (homogêneo)
    vec4 camRight;         // xyz = eixo da câmera no mundo; w = 1 liga este modo
    vec4 camUp;
    mat4 previousParticleProjection;
    mat4 layerFromComp;    // composição -> plano original, antes dos efeitos seguintes
} p;

layout(location = 0) out vec2 v_corner;   // -1..1 no quadrado da partícula
layout(location = 1) out vec4 v_color;    // cor linear (reta) e alfa
layout(location = 2) flat out float v_feather;   // suavidade da borda 0..1

const float TWO_PI = 6.28318530718;

// Três valores 0..1 de um número, só com aritmética de float (sem sin): o
// mesmo resultado em todo driver.
vec3 rand3(float n) {
    vec3 q = fract(vec3(n) * vec3(0.1031, 0.1030, 0.0973));
    q += dot(q, q.yxz + 33.33);
    return fract((q.xxy + q.yzz) * q.zyx);
}

vec3 hue_color(float h) {
    const vec3 k = abs(fract(vec3(h) + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    const vec3 srgb = mix(vec3(1.0), clamp(k - 1.0, 0.0, 1.0), 0.85);
    return pow(srgb, vec3(2.2));   // o trabalho é linear
}

// Deslocamento desde o lançamento após `t` segundos: lançamento amortecido
// pelo arrasto (integral de e^(-kt), em série perto de zero para não dividir
// 0 por 0), vento constante e gravidade.
vec3 flight(float t, vec3 dir, float speed) {
    const float k = max(p.motion.x, 0.0);
    const float damp = (k * t < 1e-3) ? t * (1.0 - 0.5 * k * t) : (1.0 - exp(-k * t)) / k;
    return dir * speed * damp + p.forces.yzw * t + vec3(0.0, p.forces.x, 0.0) * (0.5 * t * t);
}

// Turbulência: soma de senos com fase própria por partícula (não repete e
// não balança o jato como um bloco), entrando na primeira metade da vida.
vec3 wander(float clockNow, float lifeFrac, vec3 phase, float generation) {
    if (p.motion.y <= 0.0) return vec3(0.0);
    const float w = p.motion.z * (clockNow + generation * 3.7);
    const vec3 s = vec3(sin(w + phase.x) + 0.5 * sin(2.13 * w + phase.y),
                        sin(1.17 * w + phase.y) + 0.5 * sin(2.31 * w + phase.z),
                        sin(0.93 * w + phase.z) + 0.5 * sin(1.87 * w + phase.x));
    return p.motion.y * s * min(lifeFrac * 2.0, 1.0);
}

void cull() {
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // fora do recorte: nenhum fragmento
    v_color = vec4(0.0);
    v_corner = vec2(0.0);
    v_feather = 0.0;
}

void main() {
    const vec2 corners[6] = vec2[6](vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
                                    vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0));
    const int vid = gl_VertexIndex;
    const float slot = float(vid / 6);
    const vec2 corner = corners[vid % 6];

    // Em que geração este slot está e há quanto tempo ela nasceu.
    const float now = p.clock.x;
    const float grid = max(p.clock.y, 1e-4);
    const float cycle = max(p.clock.z / grid, 1e-4);
    const float since = now - slot / grid;
    const float generation = floor(since / cycle);
    const float age = since - generation * cycle;

    // Sorteios por (slot, geração): renascer é outra partícula.
    const float id = slot * 1.7 + generation * 91.7 + p.emitSize.w * 13.31;
    const vec3 rA = rand3(id);            // vida, cone
    const vec3 rB = rand3(id + 37.13);    // ponto no emissor
    const vec3 rC = rand3(id + 71.77);    // esfera, fase da turbulência
    const vec3 rD = rand3(id + 113.7);    // velocidade, tamanho, matiz
    const vec3 rE = rand3(id + 191.3);    // disparou?

    const float life = max(max(p.lifeSize.x, 1.0) * 0.001 * (1.0 + (rA.x - 0.5) * 2.0 * p.lifeSize.y), 1e-3);
    const float k = age / life;
    if (rE.x >= p.clock.w / grid || since < 0.0 || k > 1.0) {
        cull();
        return;
    }

    // Ponto de partida no emissor (tamanho em fração do quadro; a
    // profundidade usa a altura).
    const vec3 halfSize = vec3(p.emitSize.x * p.frame.x, p.emitSize.y * p.frame.y, p.emitSize.z * p.frame.y) * 0.5;
    vec3 unit = rB * 2.0 - 1.0;
    if (p.emitPos.w > 0.5) {
        // Uniforme na esfera (cos z / ângulo) e uniforme no volume (raiz cúbica).
        const float cz = rC.x * 2.0 - 1.0;
        const float sr = sqrt(max(0.0, 1.0 - cz * cz));
        const float ph = TWO_PI * rC.y;
        unit = vec3(sr * cos(ph), sr * sin(ph), cz) * pow(max(rB.x, 1e-4), 1.0 / 3.0);
    }
    const vec3 offset = unit * halfSize;

    // Direção: cone de meia-abertura `spread` em volta da mira. Inclinação 0 =
    // para cima (y da tela desce). Cosseno uniforme na calota.
    const float tilt = p.launch.z, spin = p.launch.w;
    const vec3 aim = vec3(sin(tilt) * sin(spin), -cos(tilt), sin(tilt) * cos(spin));
    const float cosMax = cos(radians(clamp(p.cone.x, 0.0, 180.0)));
    const float cz2 = mix(1.0, cosMax, rA.y);
    const float sz2 = sqrt(max(0.0, 1.0 - cz2 * cz2));
    const float ph2 = TWO_PI * rA.z;
    const vec3 pole = abs(aim.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
    const vec3 ta = normalize(cross(pole, aim));
    const vec3 tb = cross(aim, ta);
    vec3 dir = normalize(ta * (sz2 * cos(ph2)) + tb * (sz2 * sin(ph2)) + aim * cz2);
    const float offLen = length(offset);
    if (p.cone.y > 0.001 && offLen > 0.001) dir = normalize(mix(dir, offset / offLen, clamp(p.cone.y, 0.0, 1.0)));

    const float speed = p.launch.x * max(0.0, 1.0 + (rD.x - 0.5) * 2.0 * p.launch.y);
    const vec3 start = vec3(p.emitPos.x * p.frame.x, p.emitPos.y * p.frame.y, p.emitPos.z * p.frame.y) + offset;
    const vec3 pos = vec3(p.frame.zw, 0.0) + start + flight(age, dir, speed) + wander(now, k, rC * TWO_PI, generation);
    // Desfoque de movimento POR PARTÍCULA: a folha não se
    // move, o movimento está nas partículas. Onde ela estava meia janela do
    // obturador atrás é só avaliar a mesma fórmula fechada de novo — a
    // turbulência também, no instante anterior, para que só a VARIAÇÃO dela
    // conte como trajeto (reusar a de agora viraria ruído de dezenas de px).
    const float shutter = p.motion.w;
    const float agePrev = max(age - shutter, 0.0);
    const vec3 posPrev = vec3(p.frame.zw, 0.0) + start + flight(agePrev, dir, speed)
                       + wander(now - shutter, agePrev / life, rC * TWO_PI, generation);

    float size = p.lifeSize.z * max(0.05, 1.0 + (rD.y - 0.5) * 2.0 * p.lifeSize.w) * mix(1.0, p.look.x, k);
    const float fin = p.look.z <= 0.0 ? 1.0 : smoothstep(0.0, p.look.z, k);
    const float fout = p.look.w <= 0.0 ? 1.0 : 1.0 - smoothstep(1.0 - p.look.w, 1.0, k);
    vec3 color = mix(p.color0.rgb, p.color1.rgb, k);
    if (p.color0.w > 0.0) color = mix(color, hue_color(rD.z), clamp(p.color0.w, 0.0, 1.0));
    v_color = vec4(color, p.look.y * fin * fout);
    v_corner = corner;
    v_feather = p.cone.w;

    // Eixos do quadrado: os da tela; com Esticar, deitado na direção do
    // movimento visível e alongado pela velocidade (a derivada da posição).
    vec2 axisU = vec2(1.0, 0.0), axisV = vec2(0.0, 1.0);
    float sizeV = size;
    if (p.cone.z > 0.0) {
        const vec3 vel = dir * speed * exp(-max(p.motion.x, 0.0) * age) + p.forces.yzw + vec3(0.0, p.forces.x, 0.0) * age;
        const float vlen = length(vel.xy);
        if (vlen > 1e-3) {
            axisV = vel.xy / vlen;
            axisU = vec2(-axisV.y, axisV.x);
            sizeV = size * clamp(1.0 + p.cone.z * vlen / max(p.launch.x, 1.0), 1.0, 1.0 + p.cone.z * 6.0);
        }
    }

    if (p.camRight.w > 0.5) {
        // Espaço 3D da camada: a rotação/orientação da camada gira o emissor e
        // a física; os sprites continuam de frente para a câmera (eixos dela
        // no mundo) e a perspectiva é a da câmera da composição.
        const vec3 R = p.camRight.xyz, U = p.camUp.xyz;
        const vec3 center = (p.worldFromLayer * vec4(pos, 1.0)).xyz;
        const vec4 cc = p.compFromWorld * vec4(center, 1.0);
        if (cc.w < max(p.frame.y, 1.0) * 1e-3) {
            cull();
            return;
        }
        vec3 wu = R, wv = U;
        float sv = size;
        if (p.cone.z > 0.0) {
            const vec3 vel = dir * speed * exp(-max(p.motion.x, 0.0) * age) + p.forces.yzw + vec3(0.0, p.forces.x, 0.0) * age;
            const vec3 vw = mat3(p.worldFromLayer) * vel;
            const vec2 vs = vec2(dot(vw, R), dot(vw, U));
            const float vlen = length(vs);
            if (vlen > 1e-3) {
                const vec2 d = vs / vlen;
                wv = R * d.x + U * d.y;
                wu = R * -d.y + U * d.x;
                sv = size * clamp(1.0 + p.cone.z * vlen / max(p.launch.x, 1.0), 1.0, 1.0 + p.cone.z * 6.0);
            }
        }
        if (shutter > 0.0) {
            // O trajeto medido NA TELA (entre a posição de meia janela atrás
            // e a de agora, pela câmera) e trazido de volta a unidades do
            // mundo nesta profundidade pelos eixos da câmera: um passo em R
            // só mexe o x da tela e um passo em U só o y.
            const vec4 cp = p.compFromWorld * vec4((p.worldFromLayer * vec4(posPrev, 1.0)).xyz, 1.0);
            if (cp.w >= max(p.frame.y, 1.0) * 1e-3) {
                const vec2 nNow = cc.xy / cc.w;
                vec2 previousScreen = cp.xy / cp.w;
                // Layer/parent/camera movement is measured too, even for a stationary particle.
                // The one-frame screen velocity is scaled to the half shutter interval.
                const vec4 pp = p.previousParticleProjection * vec4(posPrev, 1.0);
                if (pp.w >= max(p.frame.y, 1.0) * 1e-3)
                    previousScreen += (pp.xy / pp.w - previousScreen) * shutter * p.camUp.w;
                const vec2 dScr = (nNow - previousScreen) * 2.0;
                const vec4 cu = p.compFromWorld * vec4(center + R, 1.0);
                const vec4 cv = p.compFromWorld * vec4(center + U, 1.0);
                const float perU = cu.x / max(cu.w, 1e-6) - nNow.x;
                const float perV = cv.y / max(cv.w, 1e-6) - nNow.y;
                const vec2 travel = vec2(dScr.x / (abs(perU) < 1e-9 ? 1e-9 : perU),
                                         dScr.y / (abs(perV) < 1e-9 ? 1e-9 : perV));
                // Teto de uma altura de quadro: além disso não é rastro, é um
                // quadrado do tamanho da tela por partícula.
                const float mbLen = min(length(travel), max(p.frame.y, 1.0));
                if (mbLen > max(size, 1.0) * 0.05) {
                    const vec2 a = normalize(travel);
                    wv = R * a.x + U * a.y;
                    wu = R * -a.y + U * a.x;
                    sv += mbLen;
                    // A mesma luz espalhada no rastro inteiro: mais longo = mais fraco.
                    v_color.a *= size / (size + mbLen);
                }
            }
        }
        const vec3 world = center + wu * (corner.x * size * 0.5) + wv * (corner.y * sv * 0.5);
        const vec4 c = p.compFromWorld * vec4(world, 1.0);
        const vec2 at3 = c.xy / max(c.w, 1e-6);
        const vec4 plane = p.layerFromComp * vec4(at3, 0.0, 1.0);
        if (plane.w <= 1e-6) { cull(); return; }
        // Retain homogeneous W so perspective interpolation remains correct
        // when the compositor projects this source plane again.
        gl_Position = vec4((plane.xy - p.region.xy * plane.w)
            / max(p.region.zw, vec2(1e-4)) * 2.0 - plane.w, 0.5 * plane.w, plane.w);
        return;
    }

    // Perspectiva da câmera padrão: distância focal F diante do plano z = 0.
    const float F = max(p.view.z, 1.0);
    const float depth = F + pos.z;
    if (depth < 0.01 * F) {
        cull();
        return;
    }
    const float scale = F / depth;
    const float depthPrev = F + posPrev.z;
    if (shutter > 0.0 && depthPrev >= 0.01 * F) {
        // O trajeto na tela (perspectiva nos dois instantes), de volta ao
        // tamanho no plano da partícula (o quadrado é escalado por `scale`).
        const vec2 scrNow = p.view.xy + (pos.xy - p.view.xy) * scale;
        const vec2 scrPrev = p.view.xy + (posPrev.xy - p.view.xy) * (F / depthPrev);
        const vec2 travel = (scrNow - scrPrev) * 2.0 / scale;
        const float mbLen = min(length(travel), max(p.frame.y, 1.0));
        if (mbLen > max(size, 1.0) * 0.05) {
            axisV = normalize(travel);
            axisU = vec2(-axisV.y, axisV.x);
            sizeV += mbLen;
            v_color.a *= size / (size + mbLen);
        }
    }
    const vec2 at = p.view.xy + (pos.xy - p.view.xy) * scale
                  + (axisU * (corner.x * size * 0.5) + axisV * (corner.y * sizeV * 0.5)) * scale;
    const vec2 ndc = (at - p.region.xy) / max(p.region.zw, vec2(1e-4)) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.5, 1.0);
}
