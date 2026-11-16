// =============================================================================
//  Lote X do AE (2026-11-12): Create Shapes from Text / Create Masks from Text
//  (`Engine::text_to_vector`) e os contornos bézier do texto
//  (`text::outline_bezier`). O AE é só a especificação de comportamento.
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/text/TextOutline.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/LayerWorld.hpp"
#include "aurea/vector/VectorData.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace aurea;

namespace {

struct OutlineRig {
    Engine e;
    OutlineRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        AUREA_CHECK(e.initialize(ec).ok());
        AUREA_CHECK(e.new_project(640, 360, 30.0, nullptr).ok());
    }
    ~OutlineRig() { e.shutdown(); }
    Composition* comp() { return e.project()->timeline().composition(e.project()->timeline().current()); }
    Layer* L(u64 id) { return comp()->layer(LayerId::unpack(id)); }
    u64 text(const char* content) {
        const auto id = e.add_text(content);
        AUREA_CHECK(id.ok());
        return id.ok() ? *id : 0;
    }
    bool undo() {
        Command u;
        u.type = CommandType::Undo;
        return e.apply_command(u).ok();
    }
};

struct Box {
    f32 x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    void add(Vec2 p) {
        x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
};

Box box_of(const VectorGroup& g) {
    Box b;
    for (const VectorPath& p : g.paths) for (const BezierVertex& v : p.path.v) b.add(v.p);
    return b;
}

Box box_of(const VectorData& d) {
    Box b;
    for (const VectorGroup& g : d.groups) for (const VectorPath& p : g.paths) for (const BezierVertex& v : p.path.v) b.add(v.p);
    return b;
}

/// Ponto da cúbica entre dois vértices.
Vec2 cubic(const BezierVertex& a, const BezierVertex& b, f32 u) {
    const Vec2 p0 = a.p, p1{a.p.x + a.out.x, a.p.y + a.out.y}, p2{b.p.x + b.in.x, b.p.y + b.in.y}, p3 = b.p;
    const f32 w = 1.0f - u;
    return Vec2{w * w * w * p0.x + 3 * w * w * u * p1.x + 3 * w * u * u * p2.x + u * u * u * p3.x,
                w * w * w * p0.y + 3 * w * w * u * p1.y + 3 * w * u * u * p2.y + u * u * u * p3.y};
}

} // namespace

// -----------------------------------------------------------------------------
// Contornos bézier: a mesma forma dos contornos achatados do texto 3D
// -----------------------------------------------------------------------------
AUREA_TEST(AeTextOutline, BezierOutlinesMatchTheFlattenedOnesGlyphByGlyph) {
    TextData t;
    t.content = "Oi B";
    t.size = 96.0f;
    const auto font = text::FontManager::instance().font_for(t);
    AUREA_CHECK(font != nullptr);
    if (!font) return;
    std::vector<text::GlyphOutline> glyphs;
    AUREA_CHECK(text::outline_bezier(*font, t, glyphs));
    // O espaço não tem contorno: 3 glifos (O, i, B), em ordem, cada um do seu caractere.
    AUREA_CHECK_EQ(glyphs.size(), 3u);
    if (glyphs.size() != 3) return;
    AUREA_CHECK_EQ(glyphs[0].cluster, 0u);
    AUREA_CHECK_EQ(glyphs[1].cluster, 1u);
    AUREA_CHECK_EQ(glyphs[2].cluster, 3u);
    AUREA_CHECK_EQ(glyphs[0].contours.size(), 2u);   // O: fora e furo
    AUREA_CHECK_EQ(glyphs[1].contours.size(), 2u);   // i: haste e pingo
    AUREA_CHECK_EQ(glyphs[2].contours.size(), 3u);   // B: fora e dois furos
    AUREA_CHECK(glyphs[0].penX < glyphs[1].penX && glyphs[1].penX < glyphs[2].penX);
    // Cada contorno achatado (text::outline) tem todos os pontos SOBRE a bézier
    // do mesmo contorno: mesma geometria, sem achatar.
    std::vector<std::vector<Vec2>> flat;
    std::vector<u32> owner;
    AUREA_CHECK(text::outline(*font, t, flat, -1, &owner));
    usize total = 0;
    for (const text::GlyphOutline& g : glyphs) total += g.contours.size();
    AUREA_CHECK_EQ(flat.size(), total);
    if (flat.size() != total) return;
    usize ci = 0;
    f32 worst = 0.0f;
    for (const text::GlyphOutline& g : glyphs) {
        for (const BezierPath& c : g.contours) {
            AUREA_CHECK(c.closed);
            // Amostra densa da bézier.
            std::vector<Vec2> dense;
            for (usize i = 0; i < c.v.size(); ++i) {
                const BezierVertex& a = c.v[i];
                const BezierVertex& b = c.v[(i + 1) % c.v.size()];
                for (u32 s = 0; s < 64; ++s) dense.push_back(cubic(a, b, static_cast<f32>(s) / 64.0f));
            }
            for (const Vec2& p : flat[ci]) {
                f32 best = 1e30f;
                for (const Vec2& q : dense) best = std::min(best, (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y));
                worst = std::max(worst, std::sqrt(best));
            }
            ++ci;
        }
    }
    AUREA_CHECK(worst < 0.35f);
    std::printf("3 glifos, 7 contornos; maior distancia achatado -> bezier = %.3f px; ", worst);
}

// -----------------------------------------------------------------------------
// Create Shapes from Text
// -----------------------------------------------------------------------------
AUREA_TEST(AeTextOutline, ShapesFromTextMakesOneGroupPerGlyphAndHidesTheText) {
    OutlineRig r;
    const u64 id = r.text("Oi");
    if (!id) return;
    Layer* t = r.L(id);
    t->text.color = Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    t->text.strokeWidth = 3.0f;
    t->text.strokeColor = Vec4{0.0f, 0.0f, 1.0f, 1.0f};
    t->transform.rotation.z = 0.0f;
    const Transform before = t->transform;
    const std::string textName = t->name;
    const u32 depth = r.e.history().depth();
    const u32 layers = r.comp()->order().size();
    const i32 textIndex = r.comp()->order().index_of(LayerId::unpack(id));

    const auto made = r.e.text_to_vector(id, false);
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    const Layer* n = r.L(*made);
    t = r.L(id);
    AUREA_CHECK(n != nullptr && t != nullptr);
    if (!n || !t) return;
    // O texto fica, escondido; a camada nova entra logo acima, com um passo de desfazer.
    AUREA_CHECK(!t->visible);
    AUREA_CHECK(t->kind == LayerKind::Text);
    AUREA_CHECK(n->visible);
    AUREA_CHECK_EQ(r.comp()->order().size(), layers + 1);
    AUREA_CHECK_EQ(r.comp()->order().index_of(LayerId::unpack(*made)), textIndex + 1);
    AUREA_CHECK_EQ(r.e.history().depth(), depth + 1);
    AUREA_CHECK(n->kind == LayerKind::Shape);
    AUREA_CHECK_EQ(n->shape.shapeType, kShapeVector);
    AUREA_CHECK(n->name.find(textName) == 0 && n->name.size() > textName.size());
    AUREA_CHECK(n->text.content.empty() && n->text.animators.empty());
    // A mesma transformação e o mesmo tempo: o quadro fica igual.
    AUREA_CHECK_NEAR(n->transform.position.x, before.position.x, 1e-4f);
    AUREA_CHECK_NEAR(n->transform.position.y, before.position.y, 1e-4f);
    AUREA_CHECK_NEAR(n->transform.anchor.x, before.anchor.x, 1e-4f);
    AUREA_CHECK_NEAR(n->transform.anchor.y, before.anchor.y, 1e-4f);
    AUREA_CHECK_EQ(n->start.value, t->start.value);
    AUREA_CHECK_EQ(n->end.value, t->end.value);
    // Um grupo por glifo, com o nome da letra, os contornos e o estilo do texto.
    const VectorData& d = n->shape.vector;
    AUREA_CHECK_EQ(d.groups.size(), 2u);
    if (d.groups.size() != 2) return;
    AUREA_CHECK(d.groups[0].name == "O" && d.groups[1].name == "i");
    AUREA_CHECK_EQ(d.groups[0].paths.size(), 2u);
    AUREA_CHECK_EQ(d.groups[1].paths.size(), 2u);
    for (const VectorGroup& g : d.groups) {
        for (const VectorPath& p : g.paths) AUREA_CHECK(p.kind == VectorPathKind::Free && p.path.closed && p.path.v.size() >= 3);
        AUREA_CHECK(g.fill.enabled);
        AUREA_CHECK_NEAR(g.fill.paint.color.x, 1.0f, 1e-4f);
        AUREA_CHECK_NEAR(g.fill.paint.color.y, 0.0f, 1e-4f);
        AUREA_CHECK_NEAR(g.fill.paint.opacity, 100.0f, 1e-3f);
        // O traço do texto (3 px para fora) vira um traço centrado de 6 px.
        AUREA_CHECK(g.stroke.enabled);
        AUREA_CHECK_NEAR(g.stroke.width, 6.0f, 1e-3f);
        AUREA_CHECK_NEAR(g.stroke.paint.color.z, 1.0f, 1e-4f);
        AUREA_CHECK_NEAR(g.opacity, 100.0f, 1e-3f);
    }
    // Onde as letras estão: a caixa dos contornos é a caixa do layout do texto,
    // deslocada pela margem de base da camada de texto (traço + 2 px).
    const auto font = text::FontManager::instance().font_for(t->text);
    AUREA_CHECK(font != nullptr);
    if (!font) return;
    std::vector<text::GlyphOutline> glyphs;
    AUREA_CHECK(text::outline_bezier(*font, t->text, glyphs));
    Box want;
    for (const text::GlyphOutline& g : glyphs) for (const BezierPath& c : g.contours) for (const BezierVertex& v : c.v) want.add(v.p);
    const Box got = box_of(d);
    const f32 margin = 3.0f + 2.0f;
    AUREA_CHECK_NEAR(got.x0, want.x0 + margin, 0.01f);
    AUREA_CHECK_NEAR(got.y0, want.y0 + margin, 0.01f);
    AUREA_CHECK_NEAR(got.x1, want.x1 + margin, 0.01f);
    AUREA_CHECK_NEAR(got.y1, want.y1 + margin, 0.01f);
    // "O" fica à esquerda do "i".
    AUREA_CHECK(box_of(d.groups[0]).x1 < box_of(d.groups[1]).x1);
    // Desfazer: a camada nova sai e o texto volta a aparecer.
    AUREA_CHECK(r.undo());
    AUREA_CHECK(r.L(*made) == nullptr);
    AUREA_CHECK(r.L(id) != nullptr && r.L(id)->visible);
    AUREA_CHECK_EQ(r.comp()->order().size(), layers);
    std::printf("2 grupos (O, i), 2 caminhos cada; caixa %.1f..%.1f x %.1f..%.1f; ", got.x0, got.x1, got.y0, got.y1);
}

AUREA_TEST(AeTextOutline, ShapesFromTextFreezeTheAnimatorsAtTheCurrentTime) {
    OutlineRig r;
    const u64 id = r.text("AB");
    if (!id) return;
    // Sem animador: a referência.
    const auto plain = r.e.text_to_vector(id, false);
    AUREA_CHECK(plain.ok());
    if (!plain.ok()) return;
    const VectorData reference = r.L(*plain)->shape.vector;
    AUREA_CHECK(r.undo());
    // Animador de posição (todas as letras) + cor de preenchimento + opacidade.
    Layer* t = r.L(id);
    TextAnimator a;
    a.props = kTextPropPosition | kTextPropOpacity | kTextPropFill;
    a.position = Vec3{40.0f, -25.0f, 0.0f};
    a.opacity = 50.0f;
    a.fill = Vec4{0.0f, 1.0f, 0.0f, 1.0f};
    t->text.animators.push_back(a);
    const auto moved = r.e.text_to_vector(id, false);
    AUREA_CHECK(moved.ok());
    if (!moved.ok()) return;
    const Layer* n = r.L(*moved);
    const VectorData& d = n->shape.vector;
    AUREA_CHECK_EQ(d.groups.size(), reference.groups.size());
    if (d.groups.size() != reference.groups.size() || d.groups.empty()) return;
    for (usize g = 0; g < d.groups.size(); ++g) {
        const Box b0 = box_of(reference.groups[g]), b1 = box_of(d.groups[g]);
        AUREA_CHECK_NEAR(b1.x0 - b0.x0, 40.0f, 0.05f);
        AUREA_CHECK_NEAR(b1.y0 - b0.y0, -25.0f, 0.05f);
        AUREA_CHECK_NEAR(b1.x1 - b0.x1, 40.0f, 0.05f);
        AUREA_CHECK_NEAR(d.groups[g].opacity, 50.0f, 0.5f);
        AUREA_CHECK_NEAR(d.groups[g].fill.paint.color.y, 1.0f, 0.02f);
        AUREA_CHECK_NEAR(d.groups[g].fill.paint.color.x, 0.0f, 0.02f);
    }
    // O animador é do TEXTO: a camada de formas não leva as trilhas dele.
    AUREA_CHECK(n->tracks.find(TrackProperty::TextAnimParam, 0, text::kPosX) == nullptr);
    std::printf("animador de posicao (+40, -25), opacidade 50%% e cor verde congelados nos grupos; ");
}

AUREA_TEST(AeTextOutline, ShapesFromTextKeepTheLayerAnimationAndRefuseOtherLayers) {
    OutlineRig r;
    const u64 id = r.text("Ok");
    if (!id) return;
    Layer* t = r.L(id);
    (void)t->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{0}, 100.0f);
    (void)t->tracks.get_or_create(TrackProperty::PositionX).set(FrameIndex{30}, 400.0f);
    const auto made = r.e.text_to_vector(id, false);
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    const Track* x = r.L(*made)->tracks.find(TrackProperty::PositionX);
    AUREA_CHECK(x != nullptr && x->keys.size() == 2);
    // Uma forma, uma camada que não existe: recusa sem mexer no histórico.
    const auto shape = r.e.add_shape(0);
    AUREA_CHECK(shape.ok());
    const u32 depth = r.e.history().depth();
    AUREA_CHECK(!r.e.text_to_vector(shape.ok() ? *shape : 0, false).ok());
    AUREA_CHECK(!r.e.text_to_vector(0xdeadbeefull, true).ok());
    AUREA_CHECK(!r.e.text_to_vector(*made, false).ok());   // a camada de formas já não é texto
    AUREA_CHECK_EQ(r.e.history().depth(), depth);
}

// -----------------------------------------------------------------------------
// Create Masks from Text
// -----------------------------------------------------------------------------
AUREA_TEST(AeTextOutline, MasksFromTextMakesASolidWithOneMaskPerContourInCompSpace) {
    OutlineRig r;
    const u64 id = r.text("Oi");
    if (!id) return;
    Layer* t = r.L(id);
    t->text.color = Vec4{0.2f, 0.4f, 0.6f, 1.0f};
    t->transform.position = Vec3{300.0f, 200.0f, 0.0f};
    t->transform.scale = Vec3{2.0f, 2.0f, 1.0f};
    const u32 depth = r.e.history().depth();
    // As formas dão os contornos no espaço da camada (referência).
    const auto shapes = r.e.text_to_vector(id, false);
    AUREA_CHECK(shapes.ok());
    if (!shapes.ok()) return;
    const VectorData reference = r.L(*shapes)->shape.vector;
    AUREA_CHECK(r.undo());
    AUREA_CHECK_EQ(r.e.history().depth() >= depth, true);
    const i32 textIndex = r.comp()->order().index_of(LayerId::unpack(id));

    const auto made = r.e.text_to_vector(id, true);
    AUREA_CHECK(made.ok());
    if (!made.ok()) return;
    const Layer* n = r.L(*made);
    t = r.L(id);
    AUREA_CHECK(n != nullptr && t != nullptr);
    if (!n || !t) return;
    AUREA_CHECK(!t->visible);
    AUREA_CHECK_EQ(r.comp()->order().index_of(LayerId::unpack(*made)), textIndex + 1);
    // Sólido do tamanho da composição, da cor do texto.
    AUREA_CHECK(n->kind == LayerKind::Shape);
    AUREA_CHECK_EQ(n->shape.shapeType, 0u);
    AUREA_CHECK_NEAR(n->shape.bounds.w, 640.0f, 1e-3f);
    AUREA_CHECK_NEAR(n->shape.bounds.h, 360.0f, 1e-3f);
    AUREA_CHECK_NEAR(n->shape.fillColor.z, 0.6f, 1e-4f);
    AUREA_CHECK_NEAR(n->transform.position.x, 320.0f, 1e-3f);
    AUREA_CHECK_NEAR(n->transform.anchor.x, 320.0f, 1e-3f);
    // O: fora (somar) e furo (subtrair); i: haste e pingo (somar os dois).
    AUREA_CHECK_EQ(n->masks.size(), 4u);
    if (n->masks.size() != 4) return;
    AUREA_CHECK(n->masks[0].name == "O" && n->masks[1].name == "O" && n->masks[2].name == "i" && n->masks[3].name == "i");
    AUREA_CHECK(n->masks[0].operation == MaskOperation::Add);
    AUREA_CHECK(n->masks[1].operation == MaskOperation::Subtract);
    AUREA_CHECK(n->masks[2].operation == MaskOperation::Add);
    AUREA_CHECK(n->masks[3].operation == MaskOperation::Add);
    for (const Mask& m : n->masks) AUREA_CHECK(m.closed && m.points.size() >= 3);
    // Ids estáveis e distintos.
    AUREA_CHECK(n->masks[0].id != n->masks[1].id && n->masks[2].id != n->masks[3].id);
    // No espaço da composição: a caixa das máscaras é a caixa dos contornos da
    // camada levada pela transformação do texto (posição 300,200, escala 2).
    const Mat4 world = world::world_2d_frac(*r.comp(), *t, 0.0);
    Box want, got;
    for (const VectorGroup& g : reference.groups) for (const VectorPath& p : g.paths) for (const BezierVertex& v : p.path.v)
        want.add(Vec2{world.col[0].x * v.p.x + world.col[1].x * v.p.y + world.col[3].x, world.col[0].y * v.p.x + world.col[1].y * v.p.y + world.col[3].y});
    for (const Mask& m : n->masks) for (const MaskPoint& p : m.points) got.add(p.position);
    AUREA_CHECK_NEAR(got.x0, want.x0, 0.05f);
    AUREA_CHECK_NEAR(got.y0, want.y0, 0.05f);
    AUREA_CHECK_NEAR(got.x1, want.x1, 0.05f);
    AUREA_CHECK_NEAR(got.y1, want.y1, 0.05f);
    // Escala 2: a caixa das máscaras tem o dobro da largura da dos contornos.
    const Box local = box_of(reference);
    AUREA_CHECK_NEAR(got.x1 - got.x0, (local.x1 - local.x0) * 2.0f, 0.1f);
    AUREA_CHECK(r.undo());
    AUREA_CHECK(r.L(*made) == nullptr && r.L(id)->visible);
    std::printf("solido 640x360 com 4 mascaras (O+, O-, i+, i+), caixa %.1f..%.1f; ", got.x0, got.x1);
}

AUREA_TEST(AeTextOutline, MasksFromTextRefuseMoreContoursThanALayerHolds) {
    OutlineRig r;
    const u64 id = r.text("OOOOOOOOOOOOOOOOO");   // 17 letras × 2 contornos = 34 > 32
    if (!id) return;
    const u32 depth = r.e.history().depth();
    const u32 layers = r.comp()->order().size();
    const auto refused = r.e.text_to_vector(id, true);
    AUREA_CHECK(!refused.ok() && refused.status().code() == Errc::OutOfRange);
    AUREA_CHECK_EQ(r.e.history().depth(), depth);
    AUREA_CHECK_EQ(r.comp()->order().size(), layers);
    AUREA_CHECK(r.L(id)->visible);
    // As formas não têm esse limite.
    const auto shapes = r.e.text_to_vector(id, false);
    AUREA_CHECK(shapes.ok());
    if (shapes.ok()) AUREA_CHECK_EQ(r.L(*shapes)->shape.vector.groups.size(), 17u);
}
