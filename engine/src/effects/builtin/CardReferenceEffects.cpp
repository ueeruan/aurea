// =============================================================================
//  Aurea / effects / builtin / CardReferenceEffects.cpp
//
//  AE reference batch B: Card Wipe (APC CardWipeCam) and Card Dance
//  (APC CardDanceCam). Both cut the layer into a grid of cards that the
//  vertex shader (effects/card_reference.vert) moves in the effect's own 3D
//  space and projects with the effect camera; the fragment shader lights each
//  face with the native ambient/diffuse/highlight model.
//
//  Behaviour observed with REA in the shipped plug-ins (no code taken):
//  build/reference/effects-20261115/batch-b/cards.md (camera, lighting, card
//  matrices, LCG sequence, gradient sampling). Independent implementation.
// =============================================================================
#include "CardReference.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>

namespace aurea::card_reference {
namespace {

constexpr u16 kAnim = kParamAnimatable;
constexpr u16 kAnimPct = kParamAnimatable | kParamPercent;
constexpr u16 kAnimRel = kParamAnimatable | kParamRelative;

struct Uniforms {
    Vec4 layer, region, inputMap, mode;
    Vec4 camera[4];
    Vec4 eye, light, ambient, diffuse, specular;
    Vec4 wipe0, wipe1, wipe2;
    Vec4 jitterPos, jitterPosSpeed, jitterRot, jitterRotSpeed;
    Vec4 channel[8];
    Vec4 order;
};
static_assert(sizeof(Uniforms) == 29 * 16);

f64 finite(f32 v, f64 fallback) noexcept { return std::isfinite(v) ? static_cast<f64>(v) : fallback; }

// --- 4x4 in doubles, row-major, column vectors (p' = M p) ----------------------
struct M4 {
    std::array<f64, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    f64& at(int r, int c) noexcept { return m[static_cast<usize>(r * 4 + c)]; }
    f64 at(int r, int c) const noexcept { return m[static_cast<usize>(r * 4 + c)]; }
};
M4 mul(const M4& a, const M4& b) noexcept {
    M4 out;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            f64 s = 0;
            for (int k = 0; k < 4; ++k) s += a.at(r, k) * b.at(k, c);
            out.at(r, c) = s;
        }
    return out;
}
M4 translate(f64 x, f64 y, f64 z) noexcept {
    M4 t;
    t.at(0, 3) = x; t.at(1, 3) = y; t.at(2, 3) = z;
    return t;
}
M4 rotation(int axis, f64 degrees) noexcept {
    M4 r;
    const f64 a = degrees * 0.017453292519944444, c = std::cos(a), s = std::sin(a);
    if (axis == 0) { r.at(1, 1) = c; r.at(1, 2) = -s; r.at(2, 1) = s; r.at(2, 2) = c; }
    else if (axis == 1) { r.at(0, 0) = c; r.at(0, 2) = -s; r.at(2, 0) = s; r.at(2, 2) = c; }
    else { r.at(0, 0) = c; r.at(0, 1) = -s; r.at(1, 0) = s; r.at(1, 1) = c; }
    return r;
}

/// Rotation order codes: two bits per step, the step applied first in the low bits
/// (0 X, 1 Y, 2 Z): XYZ, XZY, YXZ, YZX, ZXY, ZYX.
constexpr u32 kRotationCodes[6] = {36, 24, 33, 9, 18, 6};

Vec2 centered(Vec2 relative, Vec2 size) noexcept {
    // Relative layer point -> card space (origin at the centre, y up).
    return Vec2{static_cast<f32>((finite(relative.x, .5) - .5) * size.x),
                static_cast<f32>((.5 - finite(relative.y, .5)) * size.y)};
}

bool solve8(f64 a[8][9]) noexcept {
    for (int c = 0; c < 8; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 8; ++r) if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
        if (std::fabs(a[pivot][c]) < 1e-12) return false;
        for (int k = 0; k < 9; ++k) std::swap(a[c][k], a[pivot][k]);
        for (int r = 0; r < 8; ++r) {
            if (r == c) continue;
            const f64 f = a[r][c] / a[c][c];
            for (int k = c; k < 9; ++k) a[r][k] -= f * a[c][k];
        }
    }
    for (int r = 0; r < 8; ++r) a[r][8] /= a[r][r];
    return true;
}

f64 cross2(Vec2 o, Vec2 a, Vec2 b) noexcept {
    return (static_cast<f64>(a.x) - o.x) * (static_cast<f64>(b.y) - o.y) -
           (static_cast<f64>(a.y) - o.y) * (static_cast<f64>(b.x) - o.x);
}

/// Corner pins: the plane maps exactly onto the four corners; off the plane the
/// camera is the pinhole K [r1 r2 r3 t] that produces that homography.
bool corner_camera(const EffectEval& e, const CameraParams& c, Vec2 size, M4& out) noexcept {
    const Vec2 dst[4] = {centered(e.p2(c.upperLeft), size), centered(e.p2(c.upperRight), size),
                         centered(e.p2(c.lowerRight), size), centered(e.p2(c.lowerLeft), size)};
    // The native solver refuses a crossed or concave quadrilateral.
    f64 sign = 0;
    for (int i = 0; i < 4; ++i) {
        const f64 z = cross2(dst[i], dst[(i + 1) % 4], dst[(i + 2) % 4]);
        if (std::fabs(z) < 1e-9) return false;
        if (sign == 0) sign = z;
        else if ((z > 0) != (sign > 0)) return false;
    }
    const f64 hw = size.x * .5, hh = size.y * .5;
    const f64 src[4][2] = {{-hw, hh}, {hw, hh}, {hw, -hh}, {-hw, -hh}};
    f64 a[8][9];
    for (int i = 0; i < 4; ++i) {
        const f64 x = src[i][0], y = src[i][1], X = dst[i].x, Y = dst[i].y;
        f64* r0 = a[i * 2];
        f64* r1 = a[i * 2 + 1];
        r0[0] = x; r0[1] = y; r0[2] = 1; r0[3] = 0; r0[4] = 0; r0[5] = 0; r0[6] = -X * x; r0[7] = -X * y; r0[8] = X;
        r1[0] = 0; r1[1] = 0; r1[2] = 0; r1[3] = x; r1[4] = y; r1[5] = 1; r1[6] = -Y * x; r1[7] = -Y * y; r1[8] = Y;
    }
    if (!solve8(a)) return false;
    const f64 h[9] = {a[0][8], a[1][8], a[2][8], a[3][8], a[4][8], a[5][8], a[6][8], a[7][8], 1.0};
    // Columns of the homography: (X, Y, w) per plane axis.
    const f64 c1[3] = {h[0], h[3], h[6]}, c2[3] = {h[1], h[4], h[7]}, c3[3] = {h[2], h[5], h[8]};
    f64 f = size.x * std::clamp(finite(e.f(c.cornerFocal), 70), 10., 1000.) / 35.;
    if (e.b(c.autoFocal)) {
        const f64 w12 = c1[2] * c2[2];
        const f64 f2 = std::fabs(w12) > 1e-15 ? -(c1[0] * c2[0] + c1[1] * c2[1]) / w12 : -1;
        const f64 d = c2[2] * c2[2] - c1[2] * c1[2];
        const f64 g2 = std::fabs(d) > 1e-15
            ? ((c1[0] * c1[0] + c1[1] * c1[1]) - (c2[0] * c2[0] + c2[1] * c2[1])) / d : -1;
        if (std::isfinite(f2) && f2 > 1e-9) f = std::sqrt(f2);
        else if (std::isfinite(g2) && g2 > 1e-9) f = std::sqrt(g2);
    }
    f64 a1[3] = {c1[0] / f, c1[1] / f, c1[2]}, a2[3] = {c2[0] / f, c2[1] / f, c2[2]};
    f64 a3[3] = {c3[0] / f, c3[1] / f, c3[2]};
    const f64 n1 = std::sqrt(a1[0] * a1[0] + a1[1] * a1[1] + a1[2] * a1[2]);
    const f64 n2 = std::sqrt(a2[0] * a2[0] + a2[1] * a2[1] + a2[2] * a2[2]);
    if (!(n1 > 0) || !(n2 > 0)) return false;
    f64 lambda = 2 / (n1 + n2);
    if (a3[2] * lambda < 0) lambda = -lambda;
    f64 r1[3], r2[3], t[3];
    for (int i = 0; i < 3; ++i) { r1[i] = a1[i] * lambda; r2[i] = a2[i] * lambda; t[i] = a3[i] * lambda; }
    // Card space is right-handed with z toward the camera; the camera looks down -z.
    const f64 r3[3] = {-(r1[1] * r2[2] - r1[2] * r2[1]), -(r1[2] * r2[0] - r1[0] * r2[2]),
                       -(r1[0] * r2[1] - r1[1] * r2[0])};
    if (!(t[2] > 1e-12)) return false;
    const f64 k = 1 / t[2];
    out = M4{};
    for (int col = 0; col < 3; ++col) {
        const f64* r = col == 0 ? r1 : col == 1 ? r2 : r3;
        out.at(0, col) = f * r[0] * k;
        out.at(1, col) = f * r[1] * k;
        out.at(2, col) = r[2] * k;
        out.at(3, col) = r[2] * k;
    }
    out.at(0, 3) = f * t[0] * k;
    out.at(1, 3) = f * t[1] * k;
    out.at(2, 3) = 1;
    out.at(3, 3) = 1;
    return true;
}

/// Camera Position (and the Comp Camera fallback with the default values).
M4 position_camera(f64 rx, f64 ry, f64 rz, Vec2 xy, f64 zDistance, f64 focal, u32 order, f64 width) noexcept {
    M4 m;
    const u32 index = std::min(order, 11u);
    if (index >= 6) m = mul(translate(-xy.x, -xy.y, -zDistance * width), m);
    const u32 code = kRotationCodes[index % 6];
    const f64 angles[3] = {rx, ry, rz};
    for (int step = 0; step < 3; ++step) {
        const int axis = static_cast<int>((code >> (2 * step)) & 3u);
        if (angles[axis] != 0) m = mul(rotation(axis, angles[axis]), m);
    }
    if (index < 6) m = mul(translate(-xy.x, -xy.y, -zDistance * width), m);
    const f64 d = -(width * focal / 35.);
    M4 projection;
    projection.at(2, 3) = 1;
    projection.at(3, 2) = 1 / d;
    projection.at(3, 3) = 0;
    return mul(projection, m);
}

/// The eye: the card-space point that the camera maps to x = y = w = 0.
Vec3 eye_of(const M4& m) noexcept {
    const int rows[3] = {0, 1, 3};
    f64 a[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int c = 0; c < 3; ++c) a[i][c] = m.at(rows[i], c);
        a[i][3] = -m.at(rows[i], 3);
    }
    for (int c = 0; c < 3; ++c) {
        int pivot = c;
        for (int r = c + 1; r < 3; ++r) if (std::fabs(a[r][c]) > std::fabs(a[pivot][c])) pivot = r;
        if (std::fabs(a[pivot][c]) < 1e-15) return Vec3{0, 0, 1e6f};
        for (int k = 0; k < 4; ++k) std::swap(a[c][k], a[pivot][k]);
        for (int r = 0; r < 3; ++r) {
            if (r == c) continue;
            const f64 f = a[r][c] / a[c][c];
            for (int k = c; k < 4; ++k) a[r][k] -= f * a[c][k];
        }
    }
    return Vec3{static_cast<f32>(a[0][3] / a[0][0]), static_cast<f32>(a[1][3] / a[1][1]),
                static_cast<f32>(a[2][3] / a[2][2])};
}

} // namespace

// --- shared declarations --------------------------------------------------------
u32 declare_camera(ParameterRegistry& p) {
    CameraParams c{};
    static const char* const systems[] = {"Posição da câmera", "Posicionar bordas", "Câmera da composição"};
    c.system = p.add_enum("camera_system", "Sistema da câmera", systems, 3, 0);
    c.rotationX = p.add_angle("camera_rotation_x", "Rotação X", 0);
    c.rotationY = p.add_angle("camera_rotation_y", "Rotação Y", 0);
    c.rotationZ = p.add_angle("camera_rotation_z", "Rotação Z", 0);
    c.position = p.add_point2("camera_position", "Posição X,Y", {.5f, .5f}, -10, 10, kAnimRel);
    c.zPosition = p.add_float("camera_z", "Posição Z", 2, -10, 10, kAnim);
    p.typed_range(-1000, 1000);
    c.focal = p.add_float("focal_length", "Distância focal", 70, 10, 300, kAnim, "mm");
    p.typed_range(10, 1000);
    static const char* const orders[] = {
        "Girar XYZ, posição", "Girar XZY, posição", "Girar YXZ, posição", "Girar YZX, posição",
        "Girar ZXY, posição", "Girar ZYX, posição", "Posição, girar XYZ", "Posição, girar XZY",
        "Posição, girar YXZ", "Posição, girar YZX", "Posição, girar ZXY", "Posição, girar ZYX"};
    c.order = p.add_enum("camera_order", "Ordem de transformação", orders, 12, 0);
    c.upperLeft = p.add_point2("corner_upper_left", "Canto superior esquerdo", {0, 0}, -10, 10, kAnimRel);
    c.upperRight = p.add_point2("corner_upper_right", "Canto superior direito", {1, 0}, -10, 10, kAnimRel);
    c.lowerLeft = p.add_point2("corner_lower_left", "Canto inferior esquerdo", {0, 1}, -10, 10, kAnimRel);
    c.lowerRight = p.add_point2("corner_lower_right", "Canto inferior direito", {1, 1}, -10, 10, kAnimRel);
    c.autoFocal = p.add_bool("auto_focal_length", "Distância focal automática", false);
    c.cornerFocal = p.add_float("corner_focal_length", "Distância focal das bordas", 70, 10, 300, kAnim, "mm");
    p.typed_range(10, 1000);
    for (u32 i = c.rotationX; i <= c.order; ++i) p.set_enabled_by(i, c.system, 0b101);
    for (u32 i = c.upperLeft; i <= c.cornerFocal; ++i) p.set_enabled_by(i, c.system, 0b010);
    return c.system;
}

u32 declare_light(ParameterRegistry& p) {
    LightParams l{};
    static const char* const types[] = {"Fonte pontual", "Fonte distante", "Primeira luz da composição"};
    l.type = p.add_enum("light_type", "Tipo de luz", types, 3, 1);
    l.intensity = p.add_float("light_intensity", "Intensidade da luz", 1, 0, 5, kAnim);
    p.typed_range(0, 50);
    l.color = p.add_color("light_color", "Cor da luz", {1, 1, 1, 1});
    l.position = p.add_point2("light_position", "Posição da luz", {.5f, .5f}, -10, 10, kAnimRel);
    l.depth = p.add_float("light_depth", "Profundidade da luz", 1, -10, 10, kAnim);
    p.typed_range(-100, 100);
    l.ambient = p.add_float("ambient_light", "Luz ambiente", .25f, 0, 2, kAnim);
    l.diffuse = p.add_float("diffuse_reflection", "Reflexo difuso", .75f, 0, 2, kAnim);
    l.specular = p.add_float("specular_reflection", "Reflexo especular", 0, 0, 2, kAnim);
    l.sharpness = p.add_float("highlight_sharpness", "Nitidez do realce", 10, 0, 100, kAnim);
    return l.type;
}

bool fill_camera(const EffectEval& e, const CameraParams& c, Vec2 size, Vec4 rows[4], Vec4& eye) noexcept {
    M4 m;
    const u32 system = std::min(e.e(c.system), 2u);
    if (system == 1) {
        if (!corner_camera(e, c, size, m)) return false;
    } else if (system == 0) {
        m = position_camera(finite(e.f(c.rotationX), 0), finite(e.f(c.rotationY), 0), finite(e.f(c.rotationZ), 0),
                            centered(e.p2(c.position), size), finite(e.f(c.zPosition), 2),
                            std::clamp(finite(e.f(c.focal), 70), 10., 1000.), e.e(c.order), size.x);
    } else {
        // Comp Camera: the layer is drawn by the composition camera afterwards;
        // inside the effect it is the neutral camera (2 widths, 70 mm).
        m = position_camera(0, 0, 0, Vec2{}, 2, 70, 0, size.x);
    }
    for (int r = 0; r < 4; ++r)
        rows[r] = Vec4{static_cast<f32>(m.at(r, 0)), static_cast<f32>(m.at(r, 1)),
                       static_cast<f32>(m.at(r, 2)), static_cast<f32>(m.at(r, 3))};
    const Vec3 at = eye_of(m);
    eye = Vec4{at.x, at.y, at.z, 1};
    return true;
}

void fill_light(const EffectEval& e, const LightParams& l, Vec2 size, Vec4& light, Vec4& ambient, Vec4& diffuse,
                Vec4& specular) noexcept {
    const Vec4 color = e.color(l.color);
    const f64 intensity = std::max(0., finite(e.f(l.intensity), 1));
    const f64 lc[3] = {Color::srgb_to_linear(std::clamp(finite(color.x, 1), 0., 1.)) * intensity,
                       Color::srgb_to_linear(std::clamp(finite(color.y, 1), 0., 1.)) * intensity,
                       Color::srgb_to_linear(std::clamp(finite(color.z, 1), 0., 1.)) * intensity};
    const Vec2 xy = centered(e.p2(l.position), size);
    const f64 z = size.x * finite(e.f(l.depth), 1);
    // "First Comp Light" uses the manual light (no composition light reaches the effect).
    const bool point = e.e(l.type) != 1;
    if (point) {
        light = Vec4{xy.x, xy.y, static_cast<f32>(z), 1};
    } else {
        f64 v[3] = {xy.x, xy.y, z};
        if (std::hypot(v[0], v[1]) <= 1e-9) {
            v[0] = v[1] = 0;
            v[2] = std::fabs(z) <= 1e-9 ? 1 : (z > 0 ? 1 : -1);
        } else {
            const f64 n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            for (f64& x : v) x /= n;
        }
        light = Vec4{static_cast<f32>(v[0]), static_cast<f32>(v[1]), static_cast<f32>(v[2]), 0};
    }
    const f32 amb = static_cast<f32>(std::max(0., finite(e.f(l.ambient), .25)));
    const f64 dif = std::max(0., finite(e.f(l.diffuse), .75));
    const f64 sharp = std::clamp(finite(e.f(l.sharpness), 10), 0., 100.);
    const f64 k = (sharp / 50 + 1) * std::max(0., finite(e.f(l.specular), 0));
    const f64 sc[3] = {lc[0] * k, lc[1] * k, lc[2] * k};
    const f64 top = std::max({sc[0], sc[1], sc[2]});
    const bool on = top > 0;
    ambient = Vec4{amb, amb, amb, on ? 1.f : 0.f};
    diffuse = Vec4{static_cast<f32>(lc[0] * dif), static_cast<f32>(lc[1] * dif), static_cast<f32>(lc[2] * dif),
                   static_cast<f32>(top)};
    specular = on ? Vec4{static_cast<f32>(sc[0] / top), static_cast<f32>(sc[1] / top),
                         static_cast<f32>(sc[2] / top), static_cast<f32>(sharp * .5)}
                  : Vec4{0, 0, 0, static_cast<f32>(sharp * .5)};
}

namespace {

void card_pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) {
    PipelineKey k = PipelineKey::graphics(ShaderId::effects_card_reference_vert, ShaderId::effects_card_reference_frag, format);
    k.hasDepth = true;
    k.depthTest = true;
    k.depthWrite = true;
    k.depthCompare = CompareOp::GreaterOrEqual;
    k.depthFormat = SurfaceFormat::Depth32F;
    out.push_back(k);
}

Vec2 layer_size(const EffectEval& e, const LayerImage& input) noexcept {
    if (e.placement && e.placement->layerWidth && e.placement->layerHeight)
        return Vec2{static_cast<f32>(e.placement->layerWidth), static_cast<f32>(e.placement->layerHeight)};
    return Vec2{std::max(input.region.w, 1.f), std::max(input.region.h, 1.f)};
}

/// The grid: "Columns Follows Rows" copies the rows into the columns.
void grid(const EffectEval& e, u32 modeIndex, u32 rowsIndex, u32 colsIndex, i32& rows, i32& cols) noexcept {
    rows = std::clamp(e.value(rowsIndex).as_int(), 1, 1000);
    cols = e.e(modeIndex) == 1 ? rows : std::clamp(e.value(colsIndex).as_int(), 1, 1000);
}

/// A layer parameter's image: the other layer, the layer itself when the
/// reference points back at it (or is the native default), or nothing.
const LayerImage* layer_param(EffectBuildContext& ctx, const EffectEval& e, u32 index, bool selfWhenEmpty,
                              const LayerImage& input, bool& self) noexcept {
    self = false;
    const u64 ref = e.value(index).ref;
    if (ref) {
        if (const LayerImage* image = ctx.layer_input(ref)) return image;
        self = true;
        return &input;
    }
    if (selfWhenEmpty) { self = true; return &input; }
    return nullptr;
}

Status draw_cards(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, Uniforms& u,
                  const LayerImage* back, bool backSelf, const LayerImage* g1, const LayerImage* g2,
                  i32 rows, i32 cols, const char* name, LayerImage& out) {
    const Vec2 size{u.layer.x, u.layer.y};
    const Rect region{0, 0, size.x, size.y};
    u32 w = 0, h = 0;
    ctx.region_size(region, input.texel_scale_x(), w, h);
    if (!w || !h) { out = input; return OkStatus; }
    u.region = Vec4{region.x, region.y, region.w, region.h};
    u.inputMap = Vec4{input.region.x, input.region.y, input.region.w, input.region.h};
    u.mode.y = back ? (backSelf ? 1.f : 2.f) : 0.f;
    out = LayerImage{ctx.texture(name, w, h), region, w, h};
    const u32 vertices = static_cast<u32>(rows) * static_cast<u32>(cols) * 6u;
    const FGTexture backTexture = back && !backSelf ? back->texture : input.texture;
    const FGTexture grad1 = g1 ? g1->texture : input.texture;
    const FGTexture grad2 = g2 ? g2->texture : input.texture;
    if (ctx.geometry_pass(name, PassStage::Effects, out.texture, ShaderId::effects_card_reference_vert,
                          ShaderId::effects_card_reference_frag,
                          {PassTexture{input.texture, {}, CommonSampler::LinearBorder},
                           PassTexture{backTexture, {}, CommonSampler::LinearBorder},
                           PassTexture{grad1, {}, CommonSampler::NearestClamp},
                           PassTexture{grad2, {}, CommonSampler::NearestClamp}},
                          &u, sizeof(u), vertices, true) == kInvalidIndex)
        return Errc::PipelineCompileFailed;
    (void)e;
    return OkStatus;
}

Status clear_output(EffectBuildContext& ctx, const LayerImage& input, Vec2 size, const char* name, LayerImage& out) {
    const Rect region{0, 0, size.x, size.y};
    u32 w = 0, h = 0;
    ctx.region_size(region, input.texel_scale_x(), w, h);
    if (!w || !h) { out = input; return OkStatus; }
    out = LayerImage{ctx.texture(name, w, h), region, w, h};
    (void)ctx.graph().add_raster_pass(name, PassStage::Effects, out.texture, LoadOp::Clear, Vec4{},
                                      [](PassContext&) {});
    return OkStatus;
}

// =============================================================================
// Card Wipe
// =============================================================================
class CardWipe final : public Effect {
public:
    enum : u32 {
        kCompletion = 0, kWidth, kBack, kRowsColumns, kRows, kColumns, kCardScale, kFlipAxis, kFlipDirection,
        kFlipOrder, kGradient, kTiming, kSeed, kCamera
    };
    static constexpr CameraParams camera = camera_params(kCamera);
    static constexpr LightParams light = light_params(kCamera + kCameraParamCount);
    static constexpr u32 jitter = kCamera + kCameraParamCount + kLightParamCount;
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kCardWipeKey, "Card Wipe", "Transição", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        p.add_float("completion", "Conclusão da transição", 25, 0, 100, kAnimPct, "%");
        p.add_float("width", "Largura da transição", 50, 0, 100, kAnimPct, "%");
        p.add_layer_ref("back_layer", "Camada traseira");
        static const char* const rc[] = {"Independente", "Colunas seguem linhas"};
        p.add_enum("rows_columns", "Linhas e colunas", rc, 2, 0);
        p.add_int("rows", "Linhas", 9, 1, 1000);
        p.add_int("columns", "Colunas", 12, 1, 1000);
        p.add_float("card_scale", "Escala do cartão", 1, 0, 10, kAnim);
        static const char* const axes[] = {"X", "Y", "Aleatório"};
        p.add_enum("flip_axis", "Inverter eixo", axes, 3, 0);
        static const char* const dirs[] = {"Positivo", "Negativo", "Aleatório"};
        p.add_enum("flip_direction", "Inverter direção", dirs, 3, 0);
        static const char* const orders[] = {
            "Esquerda para direita", "Direita para esquerda", "De cima para baixo", "De baixo para cima",
            "Superior esquerdo para inferior direito", "Superior direito para inferior esquerdo",
            "Inferior esquerdo para superior direito", "Inferior direito para superior esquerdo", "Gradiente"};
        p.add_enum("flip_order", "Inverter ordem", orders, 9, 0);
        p.add_layer_ref("gradient_layer", "Camada de gradiente");
        p.add_float("timing_randomness", "Aleatoriedade de programação", 0, 0, 1, kAnim);
        p.add_int("random_seed", "Distribuição aleatória", 1, 1, 1000);
        (void)declare_camera(p);
        (void)declare_light(p);
        p.add_float("x_jitter", "Valor da tremulação X", 0, 0, 5, kAnim);
        p.add_float("x_jitter_speed", "Velocidade da tremulação X", 1, 0, 100, kAnim);
        p.typed_range(0, 1000);
        p.add_float("y_jitter", "Valor da tremulação Y", 0, 0, 5, kAnim);
        p.add_float("y_jitter_speed", "Velocidade da tremulação Y", 1, 0, 100, kAnim);
        p.typed_range(0, 1000);
        p.add_float("z_jitter", "Valor da tremulação Z", 0, 0, 25, kAnim);
        p.add_float("z_jitter_speed", "Velocidade da tremulação Z", 1, 0, 100, kAnim);
        p.typed_range(0, 1000);
        p.add_float("x_rotation_jitter", "Valor da tremulação de rot. X", 0, 0, 360, kAnim);
        p.add_float("x_rotation_jitter_speed", "Veloc. tremulação de rot. X", 1, 0, 100, kAnim);
        p.typed_range(0, 1000);
        p.add_float("y_rotation_jitter", "Valor da tremulação de rot. Y", 0, 0, 360, kAnim);
        p.add_float("y_rotation_jitter_speed", "Veloc. tremulação de rot. Y", 1, 0, 100, kAnim);
        p.typed_range(0, 1000);
        p.add_float("z_rotation_jitter", "Valor da tremulação de rot. Z", 0, 0, 360, kAnim);
        p.add_float("z_rotation_jitter_speed", "Veloc. tremulação de rot. Z", 1, 0, 100, kAnim);
        p.typed_range(0, 1000);
        p.add_section_range("cards", "Cartões", kRowsColumns, kCardScale);
        p.add_section_range("flip", "Inversão", kFlipAxis, kSeed);
        p.add_section_range("camera", "Posição da câmera", camera.system, camera.cornerFocal);
        p.add_section_range("lighting", "Iluminação", light.type, light.ambient, kInvalidIndex, false);
        p.add_section_range("material", "Material", light.diffuse, light.sharpness, kInvalidIndex, false);
        p.add_section_range("position_jitter", "Tremulação da posição", jitter, jitter + 5, kInvalidIndex, false);
        p.add_section_range("rotation_jitter", "Tremulação da rotação", jitter + 6, jitter + 11, kInvalidIndex, false);
        p.set_enabled_by(kColumns, kRowsColumns, 0b01);
        p.set_enabled_by(kGradient, kFlipOrder, 1u << 8);
    }
    bool needs_full_input() const noexcept override { return true; }
    i32 input_layer_param() const noexcept override { return static_cast<i32>(kBack); }
    u32 input_layer_count() const noexcept override { return 2; }
    i32 input_layer_param_at(u32 slot) const noexcept override {
        return slot == 0 ? static_cast<i32>(kBack) : slot == 1 ? static_cast<i32>(kGradient) : -1;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        v[kCompletion] = ParamValue::scalar(50);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override { card_pipelines(out, format); }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 size = layer_size(e, input);
        const f64 scale = finite(e.f(kCardScale), 1);
        if (std::fabs(scale) < 1e-9) return clear_output(ctx, input, size, "card-wipe-empty", out);
        i32 rows = 1, cols = 1;
        grid(e, kRowsColumns, kRows, kColumns, rows, cols);
        Uniforms u{};
        u.layer = Vec4{size.x, size.y, static_cast<f32>(cols), static_cast<f32>(rows)};
        if (!fill_camera(e, camera, size, u.camera, u.eye))
            return clear_output(ctx, input, size, "card-wipe-empty", out);
        u.eye.w = 1;
        fill_light(e, light, size, u.light, u.ambient, u.diffuse, u.specular);

        const f64 completion = std::clamp(finite(e.f(kCompletion), 25), 0., 100.) / 100.;
        const f64 width = std::max(.001, std::clamp(finite(e.f(kWidth), 50), 0., 100.) / 100.);
        const u32 order = std::min(e.e(kFlipOrder), 8u);
        f64 window = 1, denominator = 1;
        const i32 cards = rows * cols;
        if (order <= 1) { if (cols != 1) { window = width; denominator = cols - 1; } }
        else if (order <= 3) { if (rows != 1) { window = width; denominator = rows - 1; } }
        else if (order <= 7) { if (cards != 1) { window = width; denominator = rows + cols - 2; } }
        else { window = cards == 1 ? 1. : width; }
        const f64 oneMinus = 1 - window;
        u.wipe0 = Vec4{static_cast<f32>(completion), static_cast<f32>(window),
                       static_cast<f32>(oneMinus / denominator), static_cast<f32>(oneMinus)};
        const f32 cardScale = static_cast<f32>(std::max(.001, scale));
        u.wipe1 = Vec4{static_cast<f32>(std::clamp(finite(e.f(kTiming), 0), 0., 1.)), cardScale,
                       static_cast<f32>(std::min(e.e(kFlipAxis), 2u) + 1), static_cast<f32>(std::min(e.e(kFlipDirection), 2u) + 1)};
        const f64 seconds = e.framesPerSecond > 0 ? e.time_frames() / e.framesPerSecond : 0;
        bool gradientSelf = false;
        const LayerImage* gradient = order == 8 ? layer_param(ctx, e, kGradient, false, input, gradientSelf) : nullptr;
        u.wipe2 = Vec4{static_cast<f32>(order), static_cast<f32>(seconds), gradient ? 1.f : 0.f, 0};
        const auto j = [&](u32 k) { return std::max(0., finite(e.f(jitter + k), 0)); };
        const f64 halfW = size.x * .5 / cols, halfH = size.y * .5 / rows;
        const f64 zUnit = (size.x + size.y) * .5 * .25 * (1. / cols + 1. / rows);
        u.jitterPos = Vec4{static_cast<f32>(halfW * j(0)), static_cast<f32>(halfH * j(2)), static_cast<f32>(zUnit * j(4)), 0};
        u.jitterPosSpeed = Vec4{static_cast<f32>(j(1)), static_cast<f32>(j(3)), static_cast<f32>(j(5)), 0};
        u.jitterRot = Vec4{static_cast<f32>(j(6)), static_cast<f32>(j(8)), static_cast<f32>(j(10)), 0};
        u.jitterRotSpeed = Vec4{static_cast<f32>(j(7)), static_cast<f32>(j(9)), static_cast<f32>(j(11)), 0};
        // The native coplanar tie-break: x/y position jitter, z rotation jitter or cards larger than their cell.
        const bool bias = j(0) != 0 || j(2) != 0 || j(10) != 0 || cardScale > 1;
        u.mode = Vec4{0, 0, static_cast<f32>(std::clamp(e.value(kSeed).as_int(), 1, 1000)), bias ? 1.f : 0.f};
        bool backSelf = false;
        const LayerImage* back = layer_param(ctx, e, kBack, true, input, backSelf);
        if (gradientSelf) gradient = &input;
        return draw_cards(ctx, e, input, u, back, backSelf, gradient, nullptr, rows, cols, "card-wipe", out);
    }
};

// =============================================================================
// Card Dance
// =============================================================================
class CardDance final : public Effect {
public:
    enum : u32 {
        kRowsColumns = 0, kRows, kColumns, kBack, kGradient1, kGradient2, kRotationOrder, kTransformOrder,
        kChannels
    };
    static constexpr u32 kCamera = kChannels + 24;
    static constexpr CameraParams camera = camera_params(kCamera);
    static constexpr LightParams light = light_params(kCamera + kCameraParamCount);
    const EffectInfo& info() const noexcept override {
        static const EffectInfo i{kCardDanceKey, "Card Dance", "3D", EffectClass::Domain};
        return i;
    }
    void declare_parameters(ParameterRegistry& p) const override {
        static const char* const rc[] = {"Independente", "Colunas seguem linhas"};
        p.add_enum("rows_columns", "Linhas e colunas", rc, 2, 0);
        p.add_int("rows", "Linhas", 9, 1, 1000);
        p.add_int("columns", "Colunas", 12, 1, 1000);
        p.add_layer_ref("back_layer", "Camada traseira");
        p.add_layer_ref("gradient_layer_1", "Camada de gradiente 1");
        p.add_layer_ref("gradient_layer_2", "Camada de gradiente 2");
        static const char* const rotations[] = {"XYZ", "XZY", "YXZ", "YZX", "ZXY", "ZYX"};
        p.add_enum("rotation_order", "Ordem de rotação", rotations, 6, 0);
        static const char* const transforms[] = {
            "Posição, girar, escala", "Posição, escala, girar", "Girar, posição, escala",
            "Girar, escala, posição", "Escala, posição, girar", "Escala, girar, posição"};
        p.add_enum("transformation_order", "Ordem de transformação", transforms, 6, 5);
        static const char* const sources[] = {
            "Nenhum", "Intensidade 1", "Vermelho 1", "Verde 1", "Azul 1", "Alfa 1", "Inclinação H da intensidade 1",
            "Inclinação V da intensidade 1", "Inclinação H do vermelho 1", "Inclinação V do vermelho 1",
            "Intensidade 2", "Vermelho 2", "Verde 2", "Azul 2", "Alfa 2", "Inclinação H da intensidade 2",
            "Inclinação V da intensidade 2", "Inclinação H do vermelho 2", "Inclinação V do vermelho 2"};
        static const char* const ids[8] = {"x_position", "y_position", "z_position", "x_rotation", "y_rotation",
                                           "z_rotation", "x_scale", "y_scale"};
        static const char* const sourceLabels[8] = {
            "Origem da posição X", "Origem da posição Y", "Origem da posição Z", "Origem da rotação X",
            "Origem da rotação Y", "Origem da rotação Z", "Origem da escala X", "Origem da escala Y"};
        static const char* const multiplierLabels[8] = {
            "Multiplicador da posição X", "Multiplicador da posição Y", "Multiplicador da posição Z",
            "Multiplicador da rotação X", "Multiplicador da rotação Y", "Multiplicador da rotação Z",
            "Multiplicador da escala X", "Multiplicador da escala Y"};
        static const char* const offsetLabels[8] = {
            "Deslocamento da posição X", "Deslocamento da posição Y", "Deslocamento da posição Z",
            "Deslocamento da rotação X", "Deslocamento da rotação Y", "Deslocamento da rotação Z",
            "Deslocamento da escala X", "Deslocamento da escala Y"};
        static char names[8][3][40];
        for (u32 i = 0; i < 8; ++i) {
            std::snprintf(names[i][0], sizeof(names[i][0]), "%s_source", ids[i]);
            std::snprintf(names[i][1], sizeof(names[i][1]), "%s_multiplier", ids[i]);
            std::snprintf(names[i][2], sizeof(names[i][2]), "%s_offset", ids[i]);
            const bool rotation = i >= 3 && i < 6, scale = i >= 6;
            p.add_enum(names[i][0], sourceLabels[i], sources, 19, 0);
            const f32 range = rotation ? 1800.f : 1000.f;
            p.add_float(names[i][1], multiplierLabels[i], rotation ? 90.f : 1.f, rotation ? -360.f : -10.f,
                        rotation ? 360.f : 10.f, kAnim);
            p.typed_range(-range, range);
            p.add_float(names[i][2], offsetLabels[i], scale ? 1.f : 0.f, rotation ? -360.f : -10.f,
                        rotation ? 360.f : 10.f, kAnim);
            p.typed_range(-range, range);
        }
        (void)declare_camera(p);
        (void)declare_light(p);
        p.add_section_range("cards", "Cartões", kRowsColumns, kTransformOrder);
        p.add_section_range("position", "Posição", kChannels, kChannels + 8);
        p.add_section_range("rotation", "Rotação", kChannels + 9, kChannels + 17);
        p.add_section_range("scale", "Escala", kChannels + 18, kChannels + 23, kInvalidIndex, false);
        p.add_section_range("camera", "Posição da câmera", camera.system, camera.cornerFocal, kInvalidIndex, false);
        p.add_section_range("lighting", "Iluminação", light.type, light.ambient, kInvalidIndex, false);
        p.add_section_range("material", "Material", light.diffuse, light.sharpness, kInvalidIndex, false);
        p.set_enabled_by(kColumns, kRowsColumns, 0b01);
    }
    bool needs_full_input() const noexcept override { return true; }
    i32 input_layer_param() const noexcept override { return static_cast<i32>(kBack); }
    u32 input_layer_count() const noexcept override { return 3; }
    i32 input_layer_param_at(u32 slot) const noexcept override {
        return slot < 3 ? static_cast<i32>(kBack + slot) : -1;
    }
    bool demo_values(EffectInstance&, std::vector<ParamValue>& v) const noexcept override {
        // Without a gradient layer every channel is its offset: tilt the cards.
        v[kChannels + 3 * 4 + 2] = ParamValue::scalar(35);
        v[kChannels + 3 * 5 + 2] = ParamValue::scalar(8);
        return true;
    }
    void pipelines(std::vector<PipelineKey>& out, SurfaceFormat format) const override { card_pipelines(out, format); }

    Status build(EffectBuildContext& ctx, const EffectEval& e, const LayerImage& input, f32,
                 LayerImage& out) const override {
        const Vec2 size = layer_size(e, input);
        i32 rows = 1, cols = 1;
        grid(e, kRowsColumns, kRows, kColumns, rows, cols);
        Uniforms u{};
        u.layer = Vec4{size.x, size.y, static_cast<f32>(cols), static_cast<f32>(rows)};
        if (!fill_camera(e, camera, size, u.camera, u.eye))
            return clear_output(ctx, input, size, "card-dance-empty", out);
        u.eye.w = 1;
        fill_light(e, light, size, u.light, u.ambient, u.diffuse, u.specular);
        bool self1 = false, self2 = false;
        const LayerImage* g1 = layer_param(ctx, e, kGradient1, false, input, self1);
        const LayerImage* g2 = layer_param(ctx, e, kGradient2, false, input, self2);
        for (u32 i = 0; i < 8; ++i) {
            const u32 source = std::min(e.e(kChannels + 3 * i), 18u);
            const f32 multiplier = static_cast<f32>(finite(e.f(kChannels + 3 * i + 1), 0));
            const f32 offset = static_cast<f32>(finite(e.f(kChannels + 3 * i + 2), i >= 6 ? 1 : 0));
            f32 layerIndex = 0, code = 0;
            if (source >= 1 && source <= 9 && g1) { layerIndex = 1; code = static_cast<f32>(source - 1); }
            if (source >= 10 && g2) { layerIndex = 2; code = static_cast<f32>(source - 10); }
            u.channel[i] = Vec4{layerIndex, code, multiplier, offset};
        }
        // Transformation order: three steps (0 position, 1 rotation, 2 scale), first applied first.
        static constexpr f32 kSteps[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
        static constexpr f32 kAxes[6] = {0 | 1 << 2 | 2 << 4, 0 | 2 << 2 | 1 << 4, 1 | 0 << 2 | 2 << 4,
                                         1 | 2 << 2 | 0 << 4, 2 | 0 << 2 | 1 << 4, 2 | 1 << 2 | 0 << 4};
        const u32 t = std::min(e.e(kTransformOrder), 5u);
        u.order = Vec4{kSteps[t][0], kSteps[t][1], kSteps[t][2], kAxes[std::min(e.e(kRotationOrder), 5u)]};
        u.mode = Vec4{1, 0, 0, 0};
        bool backSelf = false;
        const LayerImage* back = layer_param(ctx, e, kBack, true, input, backSelf);
        return draw_cards(ctx, e, input, u, back, backSelf, g1, g2, rows, cols, "card-dance", out);
    }
};

} // namespace

void register_effects(EffectRegistry& registry) {
    (void)registry.add(std::make_unique<CardWipe>());
    (void)registry.add(std::make_unique<CardDance>());
}

} // namespace aurea::card_reference
