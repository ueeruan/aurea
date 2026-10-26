// =============================================================================
//  Omega 0.3.2, área 05 (aparência): inclinação na matriz (P0-6), decorações de
//  Borda e sombra (P0-4), preenchimento em degradê e cor animável (P1-4), ímã
//  (P1-2), código de cor (P1-7) e os modos de mesclagem novos (P1-5).
//  As fórmulas dos modos são conferidas pixel a pixel em test_gpu.cpp
//  (EveryBlendModeMatchesTheFormulaPixelForPixel); aqui só o contrato.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/core/ColorCode.hpp"
#include "aurea/core/LayerSkew.hpp"
#include "aurea/core/SnapMath.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/render/Renderer.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/ShapeFill.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace aurea;

namespace {

struct AppearanceRig {
    Engine e;
    AppearanceRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(320, 180, 30.0, nullptr).ok());
    }
    ~AppearanceRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    /// Forma 100×100 com o pivô no canto superior esquerdo, em (px, py).
    u64 square(f32 px, f32 py) {
        const auto id = e.add_shape(0);
        AUREA_CHECK(id.ok());
        if (!id.ok()) return 0;
        Layer* l = L(*id);
        l->tracks = TrackSet{};
        l->shape.bounds = Rect{0.0f, 0.0f, 100.0f, 100.0f};
        l->transform.anchor = Vec3{0.0f, 0.0f, 0.0f};
        l->transform.position = Vec3{px, py, 0.0f};
        l->transform.scale = Vec3{1.0f, 1.0f, 1.0f};
        l->transform.rotation = Vec3{0.0f, 0.0f, 0.0f};
        return *id;
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
    std::string temp(const char* name) {
        const char* dir = std::getenv("TEMP");
        return std::string(dir ? dir : ".") + "/" + name;
    }
};

Vec4 apply(const Mat4& m, f32 x, f32 y) { return m * Vec4{x, y, 0.0f, 1.0f}; }

} // namespace

// -----------------------------------------------------------------------------
// P0-6 · inclinação (convenção em core/LayerSkew.hpp)
// -----------------------------------------------------------------------------
AUREA_TEST(OmegaAppearance, SkewXShearsVerticallyLikeTheReference) {
    AppearanceRig r;
    const u64 id = r.square(0.0f, 0.0f);
    if (!id) return;
    r.L(id)->transform.skewX = 45.0f;   // "Inclinação X" = skew.x → ky (vertical)
    const Mat4 m = layer_world_matrix(*r.comp(), *r.L(id), FrameIndex{0});
    const f32 k = 45.0f * kDeg2Rad;     // radianos direto, sem tan (≈ 0,785)
    const Vec4 tr = apply(m, 100.0f, 0.0f);
    AUREA_CHECK_NEAR(tr.x, 100.0f, 1e-3f);
    AUREA_CHECK_NEAR(tr.y, 100.0f * k, 1e-3f);   // o canto superior direito DESCE ≈ 78,5 px
    const Vec4 bl = apply(m, 0.0f, 100.0f);
    AUREA_CHECK_NEAR(bl.x, 0.0f, 1e-3f);         // a inclinação X não mexe na horizontal
    AUREA_CHECK_NEAR(bl.y, 100.0f, 1e-3f);
}

AUREA_TEST(OmegaAppearance, SkewYShearsHorizontallyAroundThePivot) {
    AppearanceRig r;
    const u64 id = r.square(160.0f, 90.0f);
    if (!id) return;
    Layer* l = r.L(id);
    l->transform.anchor = Vec3{50.0f, 50.0f, 0.0f};   // pivô no centro
    l->transform.skewY = -20.0f;
    const Mat4 m = layer_world_matrix(*r.comp(), *l, FrameIndex{0});
    const f32 k = -20.0f * kDeg2Rad;
    // O pivô fica parado; o canto de baixo anda kx·50 na horizontal.
    const Vec4 pivot = apply(m, 50.0f, 50.0f);
    AUREA_CHECK_NEAR(pivot.x, 160.0f, 1e-3f);
    AUREA_CHECK_NEAR(pivot.y, 90.0f, 1e-3f);
    const Vec4 bottom = apply(m, 50.0f, 100.0f);
    AUREA_CHECK_NEAR(bottom.x, 160.0f + k * 50.0f, 1e-3f);
    AUREA_CHECK_NEAR(bottom.y, 140.0f, 1e-3f);
    // Escala e rotação vêm DEPOIS da inclinação: T·R·S·K·T(−pivô).
    l->transform.scale = Vec3{2.0f, 1.0f, 1.0f};
    const Vec4 scaled = apply(layer_world_matrix(*r.comp(), *l, FrameIndex{0}), 50.0f, 100.0f);
    AUREA_CHECK_NEAR(scaled.x, 160.0f + 2.0f * k * 50.0f, 1e-3f);
}

AUREA_TEST(OmegaAppearance, AnimatedSkewTrackAndStageCornersFollowTheRender) {
    AppearanceRig r;
    const u64 id = r.square(100.0f, 40.0f);
    if (!id) return;
    // A trilha manda (keyframe), como na posição.
    r.L(id)->tracks.get_or_create(TrackProperty::SkewX).set(FrameIndex{0}, 30.0f);
    bridge::LayerDetailPOD d;
    AUREA_CHECK(r.e.query_layer_detail(id, d));
    AUREA_CHECK(d.geomFlags & bridge::kGeomCornersValid);
    AUREA_CHECK_NEAR(d.skew[0], 30.0f, 1e-4f);
    // Cantos TL, TR, BR, BL: o palco e o toque usam a MESMA matriz do render.
    const f32 k = 30.0f * kDeg2Rad;
    AUREA_CHECK_NEAR(d.corners[0], 100.0f, 1e-2f);
    AUREA_CHECK_NEAR(d.corners[1], 40.0f, 1e-2f);
    AUREA_CHECK_NEAR(d.corners[2], 200.0f, 1e-2f);
    AUREA_CHECK_NEAR(d.corners[3], 40.0f + 100.0f * k, 1e-2f);
}

AUREA_TEST(OmegaAppearance, SkewFactorIsTheRadianValue) {
    AUREA_CHECK_NEAR(skew_factor(0.0f), 0.0f, 1e-9f);
    AUREA_CHECK_NEAR(skew_factor(180.0f), kPi, 1e-5f);
    AUREA_CHECK(!has_skew(0.0f, 0.0f));
    AUREA_CHECK(has_skew(0.0f, 0.1f));
    AUREA_CHECK_NEAR(skew_factor(std::nanf("")), 0.0f, 1e-9f);
}

// -----------------------------------------------------------------------------
// P1-5 · modos novos: contrato de arquivo
// -----------------------------------------------------------------------------
AUREA_TEST(OmegaAppearance, NewBlendModesAppendAndSurviveSaving) {
    static_assert(static_cast<u16>(BlendMode::DarkerColor) == 24);
    static_assert(static_cast<u16>(BlendMode::SoftOverlay) == 28);
    AUREA_CHECK(kLastBlendMode == BlendMode::SoftOverlay);
    AppearanceRig r;
    const u64 id = r.square(10.0f, 10.0f);
    if (!id) return;
    Command c;
    c.type = CommandType::LayerSetBlendMode;
    c.layer_blend.layer = LayerId::unpack(id);
    c.layer_blend.mode = BlendMode::PinLight;
    AUREA_CHECK(r.e.apply_command(c).ok());
    AUREA_CHECK(r.L(id)->blendMode == BlendMode::PinLight);
    const std::string path = r.temp("aurea_omega_blend.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    Composition* comp = r.comp();
    bool found = false;
    comp->layers().for_each([&](LayerId, const Layer& l) { found |= l.blendMode == BlendMode::PinLight; });
    AUREA_CHECK(found);
    std::remove(path.c_str());
}

// -----------------------------------------------------------------------------
// P0-4 · decorações (Borda e sombra)
// -----------------------------------------------------------------------------
AUREA_TEST(OmegaAppearance, DecorationsStayAfterCommonEffectsAndKeepTheirOrder) {
    AppearanceRig r;
    const u64 id = r.square(20.0f, 20.0f);
    if (!id) return;
    const u32 b1 = r.e.decoration_add(id, kEffectRoleBorder);
    const u32 shadow = r.e.decoration_add(id, kEffectRoleShadow);
    const u32 b2 = r.e.decoration_add(id, kEffectRoleBorder);
    AUREA_CHECK(b1 != kInvalidIndex && b2 != kInvalidIndex && shadow != kInvalidIndex);
    // Uma sombra só: pedir de novo devolve a mesma.
    AUREA_CHECK_EQ(r.e.decoration_add(id, kEffectRoleShadow), shadow);
    const Layer* l = r.L(id);
    AUREA_CHECK_EQ(l->effects.size(), static_cast<usize>(3));
    if (l->effects.size() != 3) return;
    // Ordem: borda 1, borda 2, sombra.
    AUREA_CHECK_EQ(l->effects[0].id, b1);
    AUREA_CHECK_EQ(l->effects[1].id, b2);
    AUREA_CHECK_EQ(l->effects[2].id, shadow);
    // Borda nova da referência: Dentro, branca, 6 px.
    AUREA_CHECK_NEAR(l->effects[0].params[0].constant.v[0], 6.0f, 1e-6f);
    AUREA_CHECK_NEAR(l->effects[0].params[3].constant.v[0], 2.0f, 1e-6f);
    AUREA_CHECK_NEAR(l->effects[0].params[1].constant.v[0], 1.0f, 1e-6f);
    // Um efeito comum entra ANTES das decorações, mesmo pedido no fim.
    Command add;
    add.type = CommandType::EffectAdd;
    add.effect_add.layer = LayerId::unpack(id);
    add.effect_add.effectType = r.e.effects().find_key("aurea.color.tint");
    add.effect_add.index = kInvalidIndex;
    AUREA_CHECK(r.e.apply_command(add).ok());
    l = r.L(id);
    AUREA_CHECK_EQ(l->effects.size(), static_cast<usize>(4));
    if (l->effects.size() != 4) return;
    AUREA_CHECK_EQ(l->effects[0].role, kEffectRoleNone);
    AUREA_CHECK_EQ(l->effects[3].id, shadow);
    // Reordenar o comum para o fim não passa das decorações.
    Command move;
    move.type = CommandType::EffectReorder;
    move.effect_reorder.layer = LayerId::unpack(id);
    move.effect_reorder.effect = EffectId{l->effects[0].id, 0};
    move.effect_reorder.newIndex = 3;
    (void)r.e.apply_command(move);
    l = r.L(id);
    AUREA_CHECK_EQ(l->effects[0].role, kEffectRoleNone);
    // Reordenar as bordas entre si (um passo de desfazer).
    AUREA_CHECK(r.e.decoration_move(id, b2, 0));
    l = r.L(id);
    AUREA_CHECK_EQ(l->effects[1].id, b2);
    AUREA_CHECK_EQ(l->effects[2].id, b1);
    r.undo();
    l = r.L(id);
    AUREA_CHECK_EQ(l->effects[1].id, b1);
    // A ponte vê o papel (a lista de Efeitos esconde role ≠ 0).
    bridge::LayerEffectRow rows[8]{};
    char names[4096]{};
    const u32 n = r.e.query_layer_effects(id, rows, 8, names, static_cast<u32>(sizeof(names)));
    AUREA_CHECK_EQ(n, 4u);
    AUREA_CHECK_EQ(rows[0].role, 0u);
    AUREA_CHECK_EQ(rows[1].role, static_cast<u32>(kEffectRoleBorder));
    AUREA_CHECK_EQ(rows[3].role, static_cast<u32>(kEffectRoleShadow));
    // Salvar e abrir: papéis e ordem voltam iguais (timeline v51).
    const std::string path = r.temp("aurea_omega_decor.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    bool checked = false;
    r.comp()->layers().for_each([&](LayerId, const Layer& item) {
        if (item.effects.size() != 4) return;
        checked = true;
        AUREA_CHECK_EQ(item.effects[0].role, kEffectRoleNone);
        AUREA_CHECK_EQ(item.effects[1].role, kEffectRoleBorder);
        AUREA_CHECK_EQ(item.effects[2].role, kEffectRoleBorder);
        AUREA_CHECK_EQ(item.effects[3].role, kEffectRoleShadow);
    });
    AUREA_CHECK(checked);
    std::remove(path.c_str());
}

AUREA_TEST(OmegaAppearance, ShadowOffsetBecomesDirectionAndDistance) {
    AppearanceRig r;
    const u64 id = r.square(20.0f, 20.0f);
    if (!id) return;
    const u32 shadow = r.e.decoration_add(id, kEffectRoleShadow);
    f32 dx = 0.0f, dy = 0.0f;
    AUREA_CHECK(r.e.decoration_shadow_offset(id, shadow, dx, dy));
    AUREA_CHECK_NEAR(dx, 5.0f, 1e-3f);   // o padrão da referência: (5, 5)
    AUREA_CHECK_NEAR(dy, 5.0f, 1e-3f);
    AUREA_CHECK(r.e.decoration_set_shadow_offset(id, shadow, -6.0f, 8.0f));
    const EffectInstance& e = r.L(id)->effects.back();
    AUREA_CHECK_NEAR(e.params[3].constant.v[0], 10.0f, 1e-4f);                              // distância
    AUREA_CHECK_NEAR(e.params[2].constant.v[0], std::atan2(8.0f, -6.0f) * kRad2Deg, 1e-3f); // direção
    AUREA_CHECK(r.e.decoration_shadow_offset(id, shadow, dx, dy));
    AUREA_CHECK_NEAR(dx, -6.0f, 1e-3f);
    AUREA_CHECK_NEAR(dy, 8.0f, 1e-3f);
}

// -----------------------------------------------------------------------------
// P1-4 · preenchimento
// -----------------------------------------------------------------------------
AUREA_TEST(OmegaAppearance, GradientFillAndAnimatedColorRoundTrip) {
    AppearanceRig r;
    const u64 id = r.square(20.0f, 20.0f);
    if (!id) return;
    f32 out[Engine::kShapeFillFloats]{};
    AUREA_CHECK_EQ(r.e.query_shape_fill(id, out, Engine::kShapeFillFloats), Engine::kShapeFillFloats);
    AUREA_CHECK_NEAR(out[21], 1.0f, 1e-6f);   // forma SDF aceita preenchimento
    AUREA_CHECK_NEAR(out[0], 0.0f, 1e-6f);    // cor sólida
    // Cor animada: ◆ no quadro 0, outra cor no 30.
    const f32 red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    AUREA_CHECK(r.e.set_shape_fill_value(id, shape_fill::kGroupColor, red, 4));
    AUREA_CHECK(r.e.toggle_shape_fill_key(id, shape_fill::kGroupColor));
    Layer* l = r.L(id);
    for (u32 p = shape_fill::kFillR; p <= shape_fill::kFillA; ++p) {
        Track& t = l->tracks.get_or_create(TrackProperty::ShapeParam, 0, p);
        t.set(FrameIndex{30}, p == shape_fill::kFillB || p == shape_fill::kFillA ? 1.0f : 0.0f, Interpolation::Linear);
        AUREA_CHECK_EQ(t.keys.size(), static_cast<usize>(2));
    }
    const shape_fill::Eval mid = shape_fill::evaluate(*l, 15.0);
    AUREA_CHECK_NEAR(mid.color.x, 0.5f, 0.05f);
    AUREA_CHECK_NEAR(mid.color.z, 0.5f, 0.05f);
    AUREA_CHECK(mid.animated & 1u);
    // Degradê radial a partir da cor de agora.
    AUREA_CHECK(r.e.set_shape_fill_mode(id, 2));
    AUREA_CHECK(r.e.set_shape_gradient_type(id, 1));
    AUREA_CHECK_EQ(r.L(id)->shape.fillKind, static_cast<u8>(1));
    AUREA_CHECK_NEAR(r.L(id)->shape.gradStart.x, 0.5f, 1e-6f);   // radial parte do centro
    const f32 end[2] = {0.9f, 0.2f};
    AUREA_CHECK(r.e.set_shape_fill_value(id, shape_fill::kGroupEnd, end, 2));
    // Salvar e abrir.
    const std::string path = r.temp("aurea_omega_fill.aurea");
    AUREA_CHECK(r.e.save_project(path.c_str()).ok());
    AUREA_CHECK(r.e.load_project(path.c_str()).ok());
    bool checked = false;
    r.comp()->layers().for_each([&](LayerId, const Layer& item) {
        if (item.kind != LayerKind::Shape) return;
        checked = true;
        AUREA_CHECK_EQ(item.shape.fillKind, static_cast<u8>(1));
        AUREA_CHECK_EQ(item.shape.gradType, static_cast<u8>(1));
        AUREA_CHECK_NEAR(item.shape.gradEnd.x, 0.9f, 1e-6f);
        AUREA_CHECK_NEAR(item.shape.gradEnd.y, 0.2f, 1e-6f);
        const Track* t = item.tracks.find(TrackProperty::ShapeParam, 0, shape_fill::kFillR);
        AUREA_CHECK(t != nullptr && t->keys.size() == 2);
    });
    AUREA_CHECK(checked);
    std::remove(path.c_str());
    // Nenhum preenchimento.
    r.comp()->layers().for_each([&](LayerId lid, const Layer&) {
        AUREA_CHECK(r.e.set_shape_fill_mode(lid.pack(), 0));
        AUREA_CHECK(!r.L(lid.pack())->shape.filled);
    });
}

// -----------------------------------------------------------------------------
// P1-2 · ímã
// -----------------------------------------------------------------------------
AUREA_TEST(OmegaAppearance, SnapPlateauHoldsTheValueAndResumesWithoutJump) {
    const f32 targets[2] = {10.0f, 50.0f};
    const f32 plateau = 4.0f;
    i32 active = -2;
    AUREA_CHECK_NEAR(snap::value_from_finger(targets, 2, plateau, 5.0f, &active), 5.0f, 1e-6f);
    AUREA_CHECK_EQ(active, -1);
    // Entrou no patamar do 10: o valor fica parado por 4 unidades de dedo.
    AUREA_CHECK_NEAR(snap::value_from_finger(targets, 2, plateau, 10.0f, &active), 10.0f, 1e-6f);
    AUREA_CHECK_EQ(active, 0);
    AUREA_CHECK_NEAR(snap::value_from_finger(targets, 2, plateau, 13.9f, &active), 10.0f, 1e-6f);
    // Saiu: volta a andar do próprio alvo, sem salto.
    AUREA_CHECK_NEAR(snap::value_from_finger(targets, 2, plateau, 14.5f, &active), 10.5f, 1e-5f);
    AUREA_CHECK_EQ(active, -1);
    // O segundo patamar começa 4 unidades depois (o primeiro já consumiu dedo).
    AUREA_CHECK_NEAR(snap::value_from_finger(targets, 2, plateau, 54.0f, &active), 50.0f, 1e-6f);
    AUREA_CHECK_EQ(active, 1);
    // Começar em cima de um alvo = meio do patamar (sai para os dois lados).
    const f32 start = snap::finger_from_value(targets, 2, plateau, 10.0f);
    AUREA_CHECK_NEAR(start, 12.0f, 1e-6f);
    AUREA_CHECK_NEAR(snap::value_from_finger(targets, 2, plateau, start), 10.0f, 1e-6f);
    // Ida e volta fora dos alvos.
    for (f32 v : {-3.0f, 0.0f, 25.0f, 49.0f, 70.0f})
        AUREA_CHECK_NEAR(snap::value_from_finger(targets, 2, plateau, snap::finger_from_value(targets, 2, plateau, v)), v, 1e-4f);
    // Sem alvos: identidade.
    AUREA_CHECK_NEAR(snap::value_from_finger(nullptr, 0, plateau, 7.0f), 7.0f, 1e-6f);
    // Ordenar com a linha-guia junto.
    f32 vals[4] = {5.0f, 1.0f, 5.0005f, 3.0f};
    f32 lines[4] = {50.0f, 10.0f, 51.0f, 30.0f};
    const u32 n = snap::sort_unique(vals, lines, 4);
    AUREA_CHECK_EQ(n, 3u);
    AUREA_CHECK_NEAR(vals[0], 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(lines[0], 10.0f, 1e-6f);
    AUREA_CHECK_NEAR(vals[2], 5.0f, 1e-3f);
}

AUREA_TEST(OmegaAppearance, DialAndHueSnap) {
    bool snapped = false;
    AUREA_CHECK_NEAR(snap::dial_snap(44.0f, &snapped), 45.0f, 1e-6f);
    AUREA_CHECK(snapped);
    AUREA_CHECK_NEAR(snap::dial_snap(42.5f, &snapped), 42.5f, 1e-6f);
    AUREA_CHECK(!snapped);
    AUREA_CHECK_NEAR(snap::dial_snap(-361.5f, &snapped), -360.0f, 1e-6f);
    i32 turns = 0;
    f32 within = 0.0f;
    snap::dial_turns(800.0f, turns, within);
    AUREA_CHECK_EQ(turns, 2);
    AUREA_CHECK_NEAR(within, 80.0f, 1e-4f);
    snap::dial_turns(-370.0f, turns, within);
    AUREA_CHECK_EQ(turns, -1);
    AUREA_CHECK_NEAR(within, -10.0f, 1e-4f);
    AUREA_CHECK_NEAR(snap::hue_snap(125.0f, &snapped), 120.0f, 1e-6f);
    AUREA_CHECK(snapped);
    AUREA_CHECK_NEAR(snap::hue_snap(100.0f, &snapped), 100.0f, 1e-6f);
    AUREA_CHECK_NEAR(snap::hue_snap(358.0f), 0.0f, 1e-6f);
    // Toque perto continua da matiz atual; longe salta para o múltiplo de 60°.
    AUREA_CHECK_NEAR(snap::hue_touch_start(100.0f, 130.0f), 100.0f, 1e-6f);
    AUREA_CHECK_NEAR(snap::hue_touch_start(100.0f, 250.0f), 240.0f, 1e-6f);
    AUREA_CHECK_EQ(snap::nudge_side(10.0f, 50.0f, 100.0f, 100.0f), 1u);
    AUREA_CHECK_EQ(snap::nudge_side(50.0f, 90.0f, 100.0f, 100.0f), 4u);
    AUREA_CHECK_EQ(snap::nudge_side(5.0f, 5.0f, 100.0f, 100.0f), 0u);    // canto
    AUREA_CHECK_EQ(snap::nudge_side(50.0f, 50.0f, 100.0f, 100.0f), 0u);  // centro
    AUREA_CHECK_NEAR(snap::pad_acceleration(600.0f), 1.0f, 1e-5f);
    AUREA_CHECK_NEAR(snap::pad_acceleration(10.0f), 0.5f, 1e-6f);
    AUREA_CHECK_NEAR(snap::pad_acceleration(100000.0f), 2.5f, 1e-6f);
}

AUREA_TEST(OmegaAppearance, SnapTargetsAlignEdgesAndCentersOfOtherLayers) {
    AppearanceRig r;
    const u64 other = r.square(200.0f, 60.0f);   // caixa x 200..300
    const u64 me = r.square(20.0f, 20.0f);       // caixa x 20..120, pivô no canto
    if (!other || !me) return;
    f32 values[256]{}, guides[256]{};
    const u32 n = r.e.query_snap_targets(me, 0, values, guides, 256);
    AUREA_CHECK(n > 0);
    auto has = [&](f32 v, f32 g) {
        for (u32 i = 0; i < n; ++i) if (std::fabs(values[i] - v) < 1e-3f && std::fabs(guides[i] - g) < 1e-3f) return true;
        return false;
    };
    AUREA_CHECK(has(200.0f, 200.0f));   // borda esquerda com a esquerda da outra
    AUREA_CHECK(has(200.0f, 300.0f) || has(200.0f, 200.0f));
    AUREA_CHECK(has(110.0f, 160.0f) || has(160.0f, 160.0f));   // centros (60 + ... ) ou esquerda no centro dela
    AUREA_CHECK(has(0.0f, 0.0f));       // borda esquerda da composição
    AUREA_CHECK(has(110.0f, 160.0f));   // centro da camada (x+50) no centro da composição (160)
    for (u32 i = 1; i < n; ++i) AUREA_CHECK(values[i] > values[i - 1]);   // ordenados
    // Larguras para as marcas da régua de Escala: a da composição e a da outra.
    const u32 w = r.e.query_snap_targets(me, 2, values, nullptr, 256);
    bool comp = false, sq = false;
    for (u32 i = 0; i < w; ++i) { comp |= std::fabs(values[i] - 320.0f) < 1e-3f; sq |= std::fabs(values[i] - 100.0f) < 1e-3f; }
    AUREA_CHECK(comp && sq);
}

// -----------------------------------------------------------------------------
// P1-7 · código da cor
// -----------------------------------------------------------------------------
AUREA_TEST(OmegaAppearance, ColorCodeParsesTheAcceptedFormats) {
    f32 c[4]{};
    AUREA_CHECK(color_code::parse("#FF8000", c));
    AUREA_CHECK_NEAR(c[0], 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(c[1], 128.0f / 255.0f, 1e-6f);
    AUREA_CHECK_NEAR(c[3], 1.0f, 1e-6f);
    AUREA_CHECK(color_code::parse("0x00ff00", c));
    AUREA_CHECK_NEAR(c[1], 1.0f, 1e-6f);
    AUREA_CHECK(color_code::parse("  0000FF80 ", c));
    AUREA_CHECK_NEAR(c[2], 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(c[3], 128.0f / 255.0f, 1e-6f);
    AUREA_CHECK(color_code::parse("FFFFFF 50%", c));
    AUREA_CHECK_NEAR(c[3], 0.5f, 1e-6f);
    AUREA_CHECK(color_code::parse("#123456 12,5 %", c));
    AUREA_CHECK_NEAR(c[3], 0.125f, 1e-6f);
    AUREA_CHECK(color_code::parse("#abc", c));
    AUREA_CHECK_NEAR(c[0], 0xAA / 255.0f, 1e-6f);
    AUREA_CHECK(!color_code::parse("", c));
    AUREA_CHECK(!color_code::parse("#GG0000", c));
    AUREA_CHECK(!color_code::parse("FF000050%", c));      // falta o espaço
    AUREA_CHECK(!color_code::parse("FF0000FF 50%", c));   // alfa duas vezes
    AUREA_CHECK(!color_code::parse("FF0000 150%", c));
    AUREA_CHECK(!color_code::parse("vermelho", c));
    char text[16]{};
    const f32 opaque[4] = {1.0f, 0.5f, 0.0f, 1.0f};
    AUREA_CHECK_EQ(color_code::format(opaque, true, text, static_cast<u32>(sizeof(text))), 7u);
    AUREA_CHECK(std::string(text) == "#FF8000");
    const f32 half[4] = {0.0f, 0.0f, 0.0f, 0.5f};
    AUREA_CHECK_EQ(color_code::format(half, true, text, static_cast<u32>(sizeof(text))), 9u);
    AUREA_CHECK(std::string(text) == "#00000080");
    AUREA_CHECK(color_code::wants_dark_text(opaque) == true);
    const f32 navy[4] = {0.1f, 0.1f, 0.3f, 1.0f};
    AUREA_CHECK(!color_code::wants_dark_text(navy));
    AUREA_CHECK(color_code::wants_dark_text(half));
}
