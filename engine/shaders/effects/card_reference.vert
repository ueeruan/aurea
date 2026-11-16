#version 450
// =============================================================================
//  Aurea / shaders / effects / card_reference.vert
//
//  Card Dance and Card Wipe (AE reference, batch B): the layer is cut into a
//  grid of cards; each card gets its own 4x4 matrix in the effect's card space
//  (origin at the layer centre, x right, y up, z toward the camera, layer px)
//  and the effect's camera projects it. Six vertices per card, no vertex
//  buffer; every vertex of a card rebuilds the same matrix.
//
//  Card Wipe: per-card timing, flip and jitter from the native LCG sequence
//  (x' = x * 1664525 + 1013904223, 21 draws per card, row-major from the
//  bottom row), reached here by jumping ahead instead of a data texture.
//  Card Dance: eight transform channels read from the gradient layers at the
//  card centre (8-bit, premultiplied, nearest texel).
//  Behaviour reference: build/reference/effects-20261115/batch-b/cards.md.
// =============================================================================
#include "../common/bindings.glsl"

layout(set = 0, binding = AUREA_TEX2) uniform sampler2D u_grad1;   // Card Wipe gradient / Card Dance gradient 1
layout(set = 0, binding = AUREA_TEX3) uniform sampler2D u_grad2;   // Card Dance gradient 2

layout(set = 0, binding = AUREA_PARAMS, std140) uniform Params {
    vec4 layer;        // W, H, columns, rows
    vec4 region;       // output region (layer px): x, y, w, h
    vec4 inputMap;     // input texture region (layer px)
    vec4 mode;         // 0 wipe / 1 dance, back source (0 none, 1 self, 2 layer), seed, z bias allowed
    vec4 camera[4];    // card space -> homogeneous (x, y, z, w), rows
    vec4 eye;          // camera position (card space), two-sided lighting
    vec4 light;        // point: position, distant: direction; w = 1 point
    vec4 ambient;      // rgb, w = specular on
    vec4 diffuse;      // rgb, w = specular strength (max component)
    vec4 specular;     // normalised rgb, w = exponent on cos^2
    vec4 wipe0;        // completion, window, step, 1 - window
    vec4 wipe1;        // timing randomness, card scale, flip axis (1..3), flip direction (1..3)
    vec4 wipe2;        // flip order (0..8), time (s), gradient present, unused
    vec4 jitterPos;    // amounts (px): x, y, z
    vec4 jitterPosSpeed;
    vec4 jitterRot;    // amounts (deg): x, y, z
    vec4 jitterRotSpeed;
    vec4 channel[8];   // dance: layer (0 none, 1, 2), code (0..8), multiplier, offset
    vec4 order;        // dance: transform ops (0 position, 1 rotation, 2 scale) x3, rotation axes packed
} p;

layout(location = 0) out vec2 v_uv;          // uv in the sampled texture
layout(location = 1) out vec3 v_world;       // card space position (lighting)
layout(location = 2) flat out vec4 v_normal; // card space normal, w = 0 front / 1 back

const float kPi = 3.14159265359;

// --- the native random sequence ------------------------------------------------
struct Lcg { uint a; uint c; };
Lcg lcg_jump(uint n) {
    // x -> a*x + c applied n times, by squaring the step.
    uint ra = 1u, rc = 0u;
    uint sa = 1664525u, sc = 1013904223u;
    while (n != 0u) {
        if ((n & 1u) != 0u) { rc = sa * rc + sc; ra = sa * ra; }
        sc = sa * sc + sc;
        sa = sa * sa;
        n >>= 1u;
    }
    return Lcg(ra, rc);
}
uint g_state;
float draw() {
    g_state = g_state * 1664525u + 1013904223u;
    return float(g_state) * 2.3283064e-10;
}

// --- matrices: column vectors, M = op * M (the native composition order) -------
mat4 translate(vec3 t) { mat4 m = mat4(1.0); m[3] = vec4(t, 1.0); return m; }
mat4 rot_x(float deg) {
    float r = deg * 0.017453292519944444, c = cos(r), s = sin(r);
    return mat4(1, 0, 0, 0,  0, c, s, 0,  0, -s, c, 0,  0, 0, 0, 1);
}
mat4 rot_y(float deg) {
    float r = deg * 0.017453292519944444, c = cos(r), s = sin(r);
    return mat4(c, 0, s, 0,  0, 1, 0, 0,  -s, 0, c, 0,  0, 0, 0, 1);
}
mat4 rot_z(float deg) {
    float r = deg * 0.017453292519944444, c = cos(r), s = sin(r);
    return mat4(c, s, 0, 0,  -s, c, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1);
}
mat4 rot_axis(int axis, float deg) {
    if (deg == 0.0) return mat4(1.0);
    return axis == 0 ? rot_x(deg) : axis == 1 ? rot_y(deg) : rot_z(deg);
}

// --- Card Dance gradient channels ----------------------------------------------
ivec4 texel8(int layerIndex, ivec2 at) {
    vec4 c = layerIndex == 1 ? texelFetch(u_grad1, at, 0) : texelFetch(u_grad2, at, 0);
    return ivec4(clamp(floor(c * 255.0 + 0.5), 0.0, 255.0));
}
float channel_value(int k, vec2 uvFromBottom) {
    const vec4 ch = p.channel[k];
    const int layerIndex = int(ch.x + 0.5);
    if (layerIndex == 0) return ch.w;
    const ivec2 size = layerIndex == 1 ? textureSize(u_grad1, 0) : textureSize(u_grad2, 0);
    ivec2 at = ivec2(int(float(size.x) * uvFromBottom.x), int(float(size.y) * (1.0 - uvFromBottom.y)));
    at = clamp(at, ivec2(0), size - 1);
    const int code = int(ch.y + 0.5);
    float value;
    if (code == 5 || code == 7) {
        // Horizontal slope: the texel to the right minus this one.
        ivec2 a = at;
        if (a.x == size.x - 1) a.x -= 1;
        const ivec4 c0 = texel8(layerIndex, a), c1 = texel8(layerIndex, ivec2(min(a.x + 1, size.x - 1), a.y));
        value = code == 5 ? float((c1.r + c1.g + c1.b) - (c0.r + c0.g + c0.b)) / 765.0 * 50.0
                          : float((c1.r - c0.r) / 255) * 50.0;
    } else if (code == 6 || code == 8) {
        // Vertical slope: the texel above (image row - 1) minus this one.
        ivec2 a = at;
        if (a.y == 0) a.y += 1;
        a.y = min(a.y, size.y - 1);
        const ivec4 c0 = texel8(layerIndex, a), c1 = texel8(layerIndex, ivec2(a.x, max(a.y - 1, 0)));
        value = code == 6 ? float((c1.r + c1.g + c1.b) - (c0.r + c0.g + c0.b)) / 765.0 * 50.0
                          : float((c1.r - c0.r) / 255) * 50.0;
    } else {
        const ivec4 c = texel8(layerIndex, at);
        if (code == 0) value = float(min(c.r + c.g + c.b, 762) - 381) / 381.0;
        else {
            int v = code == 1 ? c.r : code == 2 ? c.g : code == 3 ? c.b : c.a;
            value = float(min(v, 254) - 127) / 127.0;
        }
    }
    return value * ch.z + ch.w;
}

float gradient_intensity(vec2 uvFromBottom) {
    const ivec2 size = textureSize(u_grad1, 0);
    ivec2 at = ivec2(int(uvFromBottom.x * float(size.x)), int((1.0 - uvFromBottom.y) * float(size.y)));
    at = clamp(at, ivec2(0), size - 1);
    vec4 c = texelFetch(u_grad1, at, 0);
    // The native wipe reads straight 8-bit RGB.
    vec3 straight = c.a > 0.0 ? c.rgb / c.a : vec3(0.0);
    vec3 b = clamp(floor(straight * 255.0 + 0.5), 0.0, 255.0);
    return (b.r + b.g + b.b) / 765.0;
}

void main() {
    v_uv = vec2(0.0);
    v_world = vec3(0.0);
    v_normal = vec4(0.0);
    gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // discarded vertex

    const uint vid = uint(gl_VertexIndex);
    const uint card = vid / 6u;
    const uint k = vid % 6u;
    const int cols = max(int(p.layer.z + 0.5), 1);
    const int rows = max(int(p.layer.w + 0.5), 1);
    const int col = int(card) % cols;
    const int row = int(card) / cols;
    if (row >= rows) return;

    const float W = p.layer.x, H = p.layer.y;
    const float fc = 1.0 / float(cols), fr = 1.0 / float(rows);
    const float left = (float(col) * fc - 0.5) * W;
    const float right = left + fc * W;
    const float bottom = (float(row) * fr - 0.5) * H;
    const float top = bottom + fr * H;
    const vec2 c = vec2(left + fc * W * 0.5, bottom + fr * H * 0.5);
    const bool dance = p.mode.x > 0.5;

    mat4 M = translate(vec3(-c, 0.0));
    bool zBias = false;
    bool flipX = true;   // wipe: the axis decides how the back layer is mapped
    if (!dance) {
        Lcg jump = lcg_jump(uint(card) * 21u);
        g_state = jump.a * uint(p.mode.z + 0.5) + jump.c;
        float start;
        const int orderIndex = int(p.wipe2.x + 0.5);
        const float step = p.wipe0.z, oneMinus = p.wipe0.w;
        if (orderIndex == 0) start = float(col) * step;
        else if (orderIndex == 1) start = oneMinus - float(col) * step;
        else if (orderIndex == 2) start = oneMinus - float(row) * step;
        else if (orderIndex == 3) start = float(row) * step;
        else if (orderIndex == 4) start = float(rows - row - 1 + col) * step;
        else if (orderIndex == 5) start = oneMinus - float(row + col) * step;
        else if (orderIndex == 6) start = float(row + col) * step;
        else if (orderIndex == 7) start = float(cols - col - 1 + row) * step;
        else start = p.wipe2.z > 0.5 ? gradient_intensity(c / vec2(W, H) + 0.5) * oneMinus : 0.0;
        const float T = p.wipe1.x;
        const float span = T + 1.0;
        const float startN = (start + T * draw()) / span;
        const float rAxis = draw();
        const float rDir = draw();
        const float window = p.wipe0.y / span;
        const float completion = p.wipe0.x;
        float angle = 180.0;
        if (completion < startN + window) {
            if (completion <= startN) angle = 0.0;
            else {
                angle = (completion - startN) * 180.0 / window;
                const int dir = int(p.wipe1.w + 0.5);
                if (dir == 1 || (dir == 3 && rDir < 0.5)) angle = -angle;
            }
        }
        const float s = p.wipe1.y;
        M = mat4(s, 0, 0, 0,  0, s, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1) * M;
        const int axis = int(p.wipe1.z + 0.5);
        flipX = axis == 1 || (axis != 2 && rAxis < 0.5);
        M = (flipX ? rot_x(angle) : rot_y(angle)) * M;
        const float t = p.wipe2.y;
        for (int a = 0; a < 3; ++a) {
            const float amount = draw() * p.jitterRot[a];
            const float phase = draw() * 6.28318530718;
            const float speed = draw() * p.jitterRotSpeed[a] * t;
            M = rot_axis(a, cos((speed + speed) * kPi + phase) * amount) * M;
        }
        M = translate(vec3(c, 0.0)) * M;
        vec3 jitter;
        for (int a = 0; a < 3; ++a) {
            const float amount = draw() * p.jitterPos[a];
            const float phase = draw() * 6.2831855;
            const float speed = draw() * p.jitterPosSpeed[a] * t;
            jitter[a] = cos((speed + speed) * kPi + phase) * amount;
        }
        M = translate(jitter) * M;
        zBias = p.mode.w > 0.5;
    } else {
        const vec2 at = c / vec2(W, H) + 0.5;
        const float positionUnit = W / 5.0;
        for (int i = 0; i < 3; ++i) {
            const int op = int(p.order[i] + 0.5);
            if (op == 0) {
                const vec3 t = vec3(channel_value(0, at), channel_value(1, at), channel_value(2, at)) * positionUnit;
                if (t.x != 0.0 || t.y != 0.0) zBias = true;
                if (t != vec3(0.0)) M = translate(t) * M;
            } else if (op == 1) {
                const int packed = int(p.order.w + 0.5);
                for (int j = 0; j < 3; ++j) {
                    const int axis = (packed >> (2 * j)) & 3;
                    const float deg = channel_value(3 + axis, at);
                    if (axis == 2 && deg != 0.0) zBias = true;
                    M = rot_axis(axis, deg) * M;
                }
            } else {
                const float sx = channel_value(6, at), sy = channel_value(7, at);
                if (abs(sx) > 1.0 || abs(sy) > 1.0) zBias = true;
                M = mat4(sx, 0, 0, 0,  0, sy, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1) * M;
            }
        }
        M = translate(vec3(c, 0.0)) * M;
    }

    // Corners: (left, bottom), (right, bottom), (right, top), (left, top).
    const vec3 P0 = (M * vec4(left, bottom, 0.0, 1.0)).xyz;
    const vec3 P1 = (M * vec4(right, bottom, 0.0, 1.0)).xyz;
    const vec3 P2 = (M * vec4(right, top, 0.0, 1.0)).xyz;
    const vec3 P3 = (M * vec4(left, top, 0.0, 1.0)).xyz;
    vec3 n = cross(P1 - P0, P3 - P0);
    const float nl = length(n);
    if (!(nl > 0.0)) return;
    n /= nl;
    const bool back = dot(n, p.eye.xyz - P0) <= 0.0;
    const int backSource = int(p.mode.y + 0.5);
    if (back && backSource == 0) return;

    const int cornerOf[6] = int[6](0, 1, 2, 0, 2, 3);
    const int corner = cornerOf[k];
    const vec3 Pw = corner == 0 ? P0 : corner == 1 ? P1 : corner == 2 ? P2 : P3;
    vec2 uv = vec2(corner == 0 || corner == 3 ? float(col) * fc : float(col + 1) * fc,
                   corner < 2 ? float(row) * fr : float(row + 1) * fr);
    if (back && !dance) {
        // The wipe maps the back layer so that it reads upright after the flip.
        if (flipX) uv.y = (corner < 2 ? float(row + 1) : float(row)) * fr;
        else uv.x = (corner == 0 || corner == 3 ? float(col + 1) : float(col)) * fc;
    }
    // uv runs from the bottom-left of the layer; the sampled texture is y-down.
    const vec2 layerPx = vec2(uv.x * W, (1.0 - uv.y) * H);
    v_uv = back && backSource == 2 ? layerPx / vec2(W, H)
                                   : (layerPx - p.inputMap.xy) / max(p.inputMap.zw, vec2(1e-4));

    const vec4 h = vec4(dot(p.camera[0], vec4(Pw, 1.0)), dot(p.camera[1], vec4(Pw, 1.0)),
                        dot(p.camera[2], vec4(Pw, 1.0)), dot(p.camera[3], vec4(Pw, 1.0)));
    // Screen (layer px, y down) times w.
    const vec2 screen = vec2(W * 0.5 * h.w + h.x, H * 0.5 * h.w - h.y);
    const vec2 clip = (screen - p.region.xy * h.w) / max(p.region.zw, vec2(1e-4)) * 2.0 - h.w;
    // Reverse depth from 1/w (screen-linear); the native z bias becomes a
    // tie-break between coplanar overlapping cards.
    float depth = 0.05;
    if (zBias) depth *= 1.0 - 5e-7 * float(col * 2 + row);
    gl_Position = vec4(clip, depth, h.w);

    vec3 shadeNormal = n;
    if (back && p.eye.w > 0.5) shadeNormal = -n;
    v_world = Pw;
    v_normal = vec4(shadeNormal, back ? 1.0 : 0.0);
}
