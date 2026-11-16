#version 450
// =============================================================================
//  Aurea / shaders / effects / headbop.vert
//
//  Headbop: a grade (até 32×56 pontos, sem vertex buffer — 6 vértices por
//  célula) é deformada AQUI pela pose de cada pessoa. Os pesos de cabeça e
//  tronco saem da geometria do rig (elipse da cabeça, pivô do pescoço, linha
//  dos ombros): nenhuma textura por quadro. Espelho exato de
//  `headbop::deform_point` (src/effects/HeadbopRig.cpp) — mudou um, muda o outro.
// =============================================================================
#include "../common/bindings.glsl"

struct Face {
    vec4 head;       // cx, cy, rx, ry
    vec4 axis;       // cos(roll), sin(roll), rz, queda
    vec4 neck;       // pivô xy, meio dos ombros xy
    vec4 body;       // meia largura dos ombros, comprimento do tronco
    vec4 torso;      // dx, dy, roll
    vec4 headT;      // dx, dy, roll, pitch
    vec4 headR;      // yaw
    vec4 prevTorso;
    vec4 prevHeadT;
    vec4 prevHeadR;
    vec4 hairT;      // 0.5.2: cabelo − cabeça (dx, dy, roll, pitch)
    vec4 clothT;     // roupa − tronco (dx, dy, roll), amplitude da onda
    vec4 flutter;    // tremulação xy (px), fase da onda
    vec4 prevHairT;
    vec4 prevClothT;
    vec4 prevFlutter;
    vec4 joints[22];
};

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 region;
    vec4 inputRegion;
    vec4 layer;      // largura, altura, pontos x, pontos y
    vec4 frame;      // zoom, dy, zoom anterior, dy anterior
    vec4 mode;       // pessoas, quadro inteiro, saída, bits
    vec4 color;
    vec4 blur;
    vec4 window;
    vec4 frameShift; // dx global, dx anterior
    vec4 gridX[18];
    vec4 gridY[18];
    Face face[4];
} p;

layout(set = 0, binding = AUREA_TEX1) uniform sampler2D u_tex1;   // máscara da pessoa (soltura do cabelo/roupa)

layout(location = 0) out vec2 v_uv;    // ponto de repouso (uv da entrada)
layout(location = 1) out vec2 v_bg;    // ponto da tela antes do zoom global (uv da entrada)
layout(location = 2) out vec2 v_vel;   // deslocamento no meio obturador (uv da entrada)
layout(location = 3) out vec2 v_rest;  // ponto de repouso (fração da camada: a máscara)

vec2 rot(vec2 v, float a) {
    float c = cos(a), s = sin(a);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

// Pesos (cabeça, tronco) da pessoa f no ponto de repouso q.
vec2 weights(int f, vec2 q) {
    float W = p.layer.x, H = p.layer.y;
    float m = p.window.w;   // margem (com o recorte a borda presa fica fora da caixa)
    float edge = min(min(q.x + m, W + m - q.x), min(q.y + m, H + m - q.y));
    float win = p.window.x > 0.0 ? smoothstep(0.0, p.window.x, edge) : 1.0;
    if (p.mode.y > 0.5) return vec2(0.0, win);
    Face F = p.face[f];
    vec2 c = F.head.xy;
    float rx = max(1.0, F.head.z), ry = max(1.0, F.head.w);
    vec2 d = q - c;
    vec2 l = vec2(F.axis.x * d.x + F.axis.y * d.y, -F.axis.y * d.x + F.axis.x * d.y);
    float e = length(l / vec2(rx, ry));
    float fall = F.axis.w;
    float head = 1.0 - smoothstep(1.0, 1.0 + fall, e);
    float headExt = 1.0 - smoothstep(1.0, 1.0 + fall * 1.6, e);
    float half_ = max(1.0, F.body.x * 1.15), tl = max(1.0, F.body.y);
    float hor = 1.0 - smoothstep(0.7, 1.7, abs(q.x - F.neck.z) / half_);
    float vert = 1.0 - smoothstep(0.15, 1.0, (q.y - F.neck.w) / tl);
    float top = smoothstep(c.y - ry * 3.4, c.y - ry * 1.2, q.y);
    return vec2(head * win, max(hor * vert * top, headExt) * win);
}

// Soltura (cabelo preso, cabelo com pontas, roupa) — espelho de headbop::looseness.
vec3 looseness(int f, vec2 q, float mask) {
    bool hairOn = p.window.y > 0.5, clothOn = p.window.z > 0.5;
    if (p.mode.y > 0.5 || (!hairOn && !clothOn) || !(mask > 0.0)) return vec3(0.0);
    Face F = p.face[f];
    float W = p.layer.x, H = p.layer.y;
    float m = p.window.w;   // margem (com o recorte a borda presa fica fora da caixa)
    float edge = min(min(q.x + m, W + m - q.x), min(q.y + m, H + m - q.y));
    float win = p.window.x > 0.0 ? smoothstep(0.0, p.window.x, edge) : 1.0;
    vec2 c = F.head.xy;
    float rx = max(1.0, F.head.z), ry = max(1.0, F.head.w);
    vec2 d = q - c;
    vec2 l = vec2(F.axis.x * d.x + F.axis.y * d.y, -F.axis.y * d.x + F.axis.x * d.y);
    float e = length(l / vec2(rx, ry));
    vec3 r = vec3(0.0);
    if (hairOn) {
        float up = smoothstep(0.5 * ry, -0.5 * ry, l.y);
        float side = smoothstep(0.62, 1.0, abs(l.x) / rx) * (1.0 - smoothstep(1.2, 2.8, abs(l.x) / rx)) *
                     smoothstep(1.4 * ry, 0.35 * ry, l.y);
        float region = max(up, side);
        float core = 1.0 - smoothstep(0.62, 0.9, e);
        float eh = length(l / vec2(1.15 * rx, ry));
        float ext = 1.0 - smoothstep(1.5, 3.2, eh);
        r.x = region * (1.0 - core) * ext * mask * win;
        r.y = r.x * smoothstep(0.75, 1.7, e);
    }
    if (clothOn) {
        float half_ = max(1.0, F.body.x), tl = max(1.0, F.body.y);
        float v = (q.y - F.neck.w) / tl;
        float below = smoothstep(-0.02, 0.22, v);
        float hem = smoothstep(0.05, 0.85, v);
        float sideX = smoothstep(0.35, 1.15, abs(q.x - F.neck.z) / half_);
        float headFree = smoothstep(1.0, 1.6, e);
        r.z = below * clamp(0.2 + 0.8 * max(hem, sideX), 0.0, 1.0) * headFree * mask * win;
    }
    return r;
}

vec2 torsoOf(vec2 x, vec2 hip, vec4 t) { return hip + rot(x - hip, t.z) + t.xy; }

void boneWarp(Face F, vec2 q, int first, int second, vec2 targetFirst, vec2 targetSecond, float radius, inout vec3 accumulated) {
    float confidence = min(F.joints[first].w, F.joints[second].w);
    if (confidence < .35) return;
    vec2 start = F.joints[first].xy, v = F.joints[second].xy - start;
    float squared = dot(v, v);
    if (!(squared > 1.0)) return;
    float along = clamp(dot(q - start, v) / squared, 0.0, 1.0);
    float distance = length(q - (start + v * along));
    float weight = (1.0 - smoothstep(radius * .6, radius * 1.4, distance)) * confidence;
    if (!(weight > 0.0)) return;
    vec2 targetVector = targetSecond - targetFirst;
    float angle = atan(targetVector.y, targetVector.x) - atan(v.y, v.x);
    accumulated.xy += (targetFirst + rot(q - start, angle) - q) * weight;
    accumulated.z += weight;
}

vec3 bodyWarp(Face F, vec2 q, bool previous, float mask) {
    if (!(mask > 0.0) || max(max(F.joints[2].w, F.joints[3].w), max(F.joints[14].w, F.joints[15].w)) < .35) return vec3(0.0);
    vec4 T = previous ? F.prevTorso : F.torso;
    if (any(isnan(T.xy)) || any(isinf(T.xy)) || max(abs(T.x), abs(T.y)) > 2.0 * max(p.layer.x, p.layer.y)) return vec3(0.0);
    vec2 hip = F.body.w > 0.0 ? F.body.zw : vec2(F.neck.z, F.neck.w + F.body.y);
    float armRadius = max(2.0, F.head.z * .4), legRadius = max(2.0, F.head.z * .6);
    float armAngle = T.z * .35 + (previous ? F.prevClothT.z : F.clothT.z) * .3;
    vec3 accumulated = vec3(0.0);
    for (int side = 0; side < 2; ++side) {
        int shoulder = side, elbow = 2 + side, wrist = 4 + side;
        if (F.joints[shoulder].w >= .35 && F.joints[elbow].w >= .35) {
            vec2 root = torsoOf(F.joints[shoulder].xy, hip, T);
            vec2 middle = root + rot(F.joints[elbow].xy - F.joints[shoulder].xy, armAngle);
            vec2 end = middle + rot(F.joints[wrist].xy - F.joints[elbow].xy, armAngle * .6);
            boneWarp(F, q, shoulder, elbow, root, middle, armRadius, accumulated);
            boneWarp(F, q, elbow, wrist, middle, end, armRadius * .8, accumulated);
            for (int index = 0; index < 3; ++index) {
                int finger = 6 + index * 2 + side;
                boneWarp(F, q, wrist, finger, end, end + rot(F.joints[finger].xy - F.joints[wrist].xy, armAngle * .6), armRadius * .7, accumulated);
            }
        }
        int pelvis = 12 + side, knee = 14 + side, ankle = 16 + side;
        if (F.joints[pelvis].w < .35 || F.joints[knee].w < .35) continue;
        vec2 root = torsoOf(F.joints[pelvis].xy, hip, T);
        vec2 middle = torsoOf(F.joints[knee].xy, hip, T), end = torsoOf(F.joints[ankle].xy, hip, T);
        if (F.joints[ankle].w >= .35) {
            float upper = length(F.joints[knee].xy - F.joints[pelvis].xy);
            float lower = length(F.joints[ankle].xy - F.joints[knee].xy);
            vec2 direction = F.joints[ankle].xy - root;
            float distance = length(direction);
            if (upper > 1.0 && lower > 1.0 && distance > .0001) {
                vec2 axis = direction / distance;
                float reach = clamp(distance, abs(upper - lower) + .0001, upper + lower);
                float along = (upper * upper - lower * lower + reach * reach) / (2.0 * reach);
                float bend = sqrt(max(0.0, upper * upper - along * along));
                vec2 original = F.joints[ankle].xy - F.joints[pelvis].xy;
                vec2 kneeOffset = F.joints[knee].xy - F.joints[pelvis].xy;
                float sign = original.x * kneeOffset.y - original.y * kneeOffset.x >= 0.0 ? 1.0 : -1.0;
                middle = root + axis * along + vec2(-axis.y, axis.x) * (bend * sign);
                end = root + axis * reach;
            }
        }
        boneWarp(F, q, pelvis, knee, root, middle, legRadius, accumulated);
        boneWarp(F, q, knee, ankle, middle, end, legRadius * .7, accumulated);
        for (int index = 0; index < 2; ++index) {
            int foot = 18 + index * 2 + side;
            boneWarp(F, q, ankle, foot, end, end + F.joints[foot].xy - F.joints[ankle].xy, legRadius * .55, accumulated);
        }
    }
    if (!(accumulated.z > 0.0)) return vec3(0.0);
    float m = p.window.w;
    float edge = min(min(q.x + m, p.layer.x + m - q.x), min(q.y + m, p.layer.y + m - q.y));
    float window = p.window.x > 0.0 ? smoothstep(0.0, p.window.x, edge) : 1.0;
    return vec3(accumulated.xy / accumulated.z, min(accumulated.z, 1.0) * clamp(mask, 0.0, 1.0) * window);
}

vec2 deformPoint(vec2 q, bool previous, float mask) {
    vec2 sum = q;
    bool whole = p.mode.y > 0.5;
    int n = whole ? 1 : min(4, int(p.mode.x + 0.5));
    for (int f = 0; f < 4; ++f) {
        if (f >= n) break;
        Face F = p.face[f];
        vec2 w = weights(f, q);
        vec3 lo = looseness(f, q, mask);
        float wh = max(w.x, lo.x);
        vec3 body = whole ? vec3(0.0) : bodyWarp(F, q, previous, mask);
        if (w.y <= 0.0 && wh <= 0.0 && lo.z <= 0.0 && body.z <= 0.0) continue;
        vec4 T = previous ? F.prevTorso : F.torso;
        vec2 hip = F.body.w > 0.0 ? F.body.zw : vec2(F.neck.z, F.neck.w + F.body.y);
        vec2 tp = torsoOf(q, hip, T);
        vec2 q1 = q + (tp - q) * w.y;
        if (lo.z > 0.0) {
            vec4 C = previous ? F.prevClothT : F.clothT;
            vec4 FL = previous ? F.prevFlutter : F.flutter;
            vec2 cp = torsoOf(q, hip, vec4(T.x + C.x, T.y + C.y, T.z + C.z, 0.0));
            q1 += (cp - tp) * lo.z;
            float v = (q.y - F.neck.w) / max(1.0, F.body.y);
            q1.x += lo.z * C.w * sin(15.7 * v - FL.z);
        }
        vec2 q2 = q1;
        if (!whole && wh > 0.0) {
            vec4 HT = previous ? F.prevHeadT : F.headT;
            float baseRoll = HT.z;
            vec4 HR = previous ? F.prevHeadR : F.headR;
            vec4 HD = previous ? F.prevHairT : F.hairT;
            HT = vec4(HT.x + lo.y * HD.x, HT.y + lo.y * HD.y, HT.z + lo.y * HD.z, HT.w + lo.y * HD.w);
            vec2 c = F.head.xy;
            float rx = max(1.0, F.head.z), ry = max(1.0, F.head.w);
            vec2 d = q - c;
            vec2 l = vec2(F.axis.x * d.x + F.axis.y * d.y, -F.axis.y * d.x + F.axis.x * d.y);
            float e2 = dot(l / vec2(rx, ry), l / vec2(rx, ry));
            float z = F.axis.z * max(0.0, 1.0 - e2);   // paraboloide: sem dobra na borda
            vec2 n1 = torsoOf(F.neck.xy, hip, T);
            vec2 c1 = torsoOf(c, hip, T);
            vec2 ax = rot(F.axis.xy, T.z);
            vec2 ay = vec2(-ax.y, ax.x);
            float xh = dot(q1 - c1, ax);
            float dx = xh * cos(HR.x) + z * sin(HR.x) - xh;
            float yh = dot(q1 - n1, ay);
            float dy = yh * cos(HT.w) + z * sin(HT.w) - yh;
            vec2 local_ = (q1 - n1) + ax * dx + ay * dy;
            vec2 h = n1 + rot(local_, HT.z) + HT.xy;
            if (p.window.y > 0.5 && lo.x > 0.0 && e2 > 1.0) {
                // Fora do crânio o cabelo pende da raiz (espelho de deform_point).
                float e = sqrt(e2);
                vec2 root = c + d / e;
                vec2 r1 = torsoOf(root, hip, T);
                float xr = dot(r1 - c1, ax), yr = dot(r1 - n1, ay);
                vec2 rl = (r1 - n1) + ax * (xr * cos(HR.x) - xr) + ay * (yr * cos(HT.w) - yr);
                vec2 hr = n1 + rot(rl, HT.z) + HT.xy;
                vec2 hh = hr + rot(q1 - r1, 0.5 * baseRoll + lo.y * HD.z);
                h = h + (hh - h) * smoothstep(1.0, 1.35, e);
            }
            if (lo.y > 0.0) {
                vec4 FL = previous ? F.prevFlutter : F.flutter;
                float wob = 0.6 + 0.4 * sin(1.1 * l.x / rx + 0.7 * l.y / ry + FL.z);
                h += FL.xy * (lo.y * wob);
            }
            q2 = q1 + (h - q1) * wh;
        }
        q2 = mix(q2, q + body.xy, body.z);
        sum += q2 - q;
    }
    return sum;
}

vec2 globalOf(vec2 q, bool previous) {
    float z = previous ? p.frame.z : p.frame.x;
    float dy = previous ? p.frame.w : p.frame.y;
    float dx = previous ? p.frameShift.y : p.frameShift.x;
    vec2 c = p.layer.xy * 0.5;
    return c + (q - c) * z + vec2(dx, dy);
}

float lineAt(int axis, int i) {
    vec4 v = axis == 0 ? p.gridX[i / 4] : p.gridY[i / 4];
    int k = i - (i / 4) * 4;
    return k == 0 ? v.x : k == 1 ? v.y : k == 2 ? v.z : v.w;
}

void main() {
    const ivec2 corners[6] = ivec2[](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(0, 1), ivec2(1, 0), ivec2(1, 1));
    int gx = int(p.layer.z + 0.5);
    int gy = int(p.layer.w + 0.5);
    int cell = gl_VertexIndex / 6;
    ivec2 g = ivec2(cell % (gx - 1), cell / (gx - 1)) + corners[gl_VertexIndex % 6];
    vec2 q = vec2(lineAt(0, g.x), lineAt(1, g.y));
    // Cabelo/roupa só com o recorte. A soltura segue a pessoa (máscara média de 5
    // amostras num raio de ~0,4 cabeça): fora da silhueta a malha solta MENOS que
    // dentro, então a borda do cabelo comprime em vez de esticar em fio.
    float mask = 0.0;
    if ((int(p.mode.w + 0.5) % 2) == 1 && (p.window.y > 0.5 || p.window.z > 0.5)) {
        vec2 fr = clamp(q / max(p.layer.xy, vec2(1.0)), vec2(0.0), vec2(1.0));
        vec2 r = vec2(0.4 * p.face[0].head.z) / max(p.layer.xy, vec2(1.0));
        float m = textureLod(u_tex1, fr, 0.0).r;
        m += textureLod(u_tex1, clamp(fr + vec2(r.x, 0.0), vec2(0.0), vec2(1.0)), 0.0).r;
        m += textureLod(u_tex1, clamp(fr - vec2(r.x, 0.0), vec2(0.0), vec2(1.0)), 0.0).r;
        m += textureLod(u_tex1, clamp(fr + vec2(0.0, r.y), vec2(0.0), vec2(1.0)), 0.0).r;
        m += textureLod(u_tex1, clamp(fr - vec2(0.0, r.y), vec2(0.0), vec2(1.0)), 0.0).r;
        mask = smoothstep(0.15, 0.75, m * 0.2);
    }
    vec2 now = deformPoint(q, false, mask);
    vec2 pos = globalOf(now, false);
    v_uv = (q - p.inputRegion.xy) / p.inputRegion.zw;
    v_bg = (now - p.inputRegion.xy) / p.inputRegion.zw;
    v_rest = q / max(p.layer.xy, vec2(1.0));
    if (p.blur.x > 0.5) {
        vec2 before = globalOf(deformPoint(q, true, mask), true);
        v_vel = (pos - before) / p.inputRegion.zw;
    } else {
        v_vel = vec2(0.0);
    }
    vec2 clip = (pos - p.region.xy) / p.region.zw * 2.0 - 1.0;
    gl_Position = vec4(clip, 0.0, 1.0);
}
