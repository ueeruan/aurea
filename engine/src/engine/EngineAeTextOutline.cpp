// =============================================================================
//  Aurea / engine / EngineAeTextOutline.cpp
//
//  Lote X do AE (2026-11-12, gap_system item 15): Create Shapes from Text e
//  Create Masks from Text (`Engine::text_to_vector`). O AE é só a
//  especificação de comportamento.
//
//  Os contornos bézier dos glifos (text::outline_bezier), no lugar em que cada
//  letra aparece no quadro do cabeçote (texto-fonte com key ou expressão,
//  texto dinâmico, animadores de conteúdo, layout, animadores de texto), viram
//  uma camada vetorial com um grupo por glifo, ou um sólido do tamanho da
//  composição com uma máscara por contorno. O texto fica escondido, não sai.
//
//  Diferenças do AE, de propósito ou por limite:
//  - a camada de formas é uma CÓPIA do texto convertida: mantém transformação
//    (com keyframes), pai, tempo, efeitos, máscaras e modo de mesclagem, para
//    o quadro ficar igual. O AE copia só a transformação.
//  - o traço do texto do Aurea fica por FORA da letra e por baixo do
//    preenchimento; no vetor ele vira um traço centrado com o dobro da
//    largura, desenhado ANTES do preenchimento do mesmo grupo.
//  - rotação 3D por letra é projetada sem perspectiva; desfoque por letra,
//    sombra e fundo do texto não têm equivalente vetorial e ficam de fora.
//  - texto no caminho usa o layout reto nesta conversão.
//  - legendas (texto que troca no tempo) são recusadas.
//  - máscaras: no máximo kMaxMaskCount (32) contornos por camada; texto com
//    mais contornos é recusado. As máscaras nascem no espaço da composição,
//    no instante do cabeçote (a animação do texto não vai junto, como no AE).
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/DefaultNames.hpp"
#include "aurea/expr/Expression.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/text/TextOutline.hpp"
#include "aurea/text/TextLayerOutline.hpp"
#include "aurea/text/TextTransform.hpp"
#include "aurea/timeline/LayerAnimator.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/timeline/Layer.hpp"
#include "aurea/timeline/LayerWorld.hpp"
#include "aurea/vector/VectorData.hpp"
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace aurea {
namespace {

using OutlinedGlyph = text::OutlinedLayerGlyph;

std::string outline_layer_name(const Composition& comp, const std::string& base) {
    for (u32 n = 1; n < 100000; ++n) {
        const std::string candidate = n == 1 ? base : base + " " + std::to_string(n);
        bool used = false;
        for (u32 i = 0; i < comp.order().size() && !used; ++i) {
            const Layer* l = comp.layer(comp.order().at(i));
            used = l && l->name == candidate;
        }
        if (!used) return candidate;
    }
    return base;
}

std::vector<u32> utf8_codepoints(const std::string& s) {
    std::vector<u32> out;
    for (usize i = 0; i < s.size();) {
        const u8 c = static_cast<u8>(s[i]);
        u32 cp = c;
        usize n = 1;
        if (c >= 0xF0 && i + 3 < s.size()) { cp = c & 0x07u; n = 4; }
        else if (c >= 0xE0 && i + 2 < s.size()) { cp = c & 0x0Fu; n = 3; }
        else if (c >= 0xC0 && i + 1 < s.size()) { cp = c & 0x1Fu; n = 2; }
        for (usize k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<u8>(s[i + k]) & 0x3Fu);
        out.push_back(cp);
        i += n;
    }
    return out;
}

std::string utf8_of(u32 cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

/// Mistura em luz linear (como o shader do texto) e volta para sRGB.
Vec4 mix_srgb(Vec4 base, Vec4 over, f32 weight) noexcept {
    if (weight <= 0.0f) return base;
    const f32 w = std::min(weight, 1.0f);
    const auto ch = [w](f32 a, f32 b) {
        const f32 la = Color::srgb_to_linear(std::clamp(a, 0.0f, 1.0f)), lb = Color::srgb_to_linear(std::clamp(b, 0.0f, 1.0f));
        return Color::linear_to_srgb(std::clamp(la + (lb - la) * w, 0.0f, 1.0f));
    };
    return Vec4{ch(base.x, over.x), ch(base.y, over.y), ch(base.z, over.z), base.w};
}

/// Área com sinal do polígono dos vértices (só o sentido e a ordem importam).
f32 contour_area(const BezierPath& c) noexcept {
    f64 a = 0.0;
    for (usize i = 0; i < c.v.size(); ++i) {
        const Vec2 p = c.v[i].p, q = c.v[(i + 1) % c.v.size()].p;
        a += static_cast<f64>(p.x) * q.y - static_cast<f64>(q.x) * p.y;
    }
    return static_cast<f32>(a * 0.5);
}

/// As letras do texto no instante, como a prévia as põe (Renderer.cpp, caso
/// Text): texto-fonte, expressão, tracking animado, texto dinâmico, animadores
/// de conteúdo, layout e a matriz de cada glifo. Espaço de saída: o da camada.
} // namespace

bool text::outline_layer(const Layer& l, f64 localFrame, f64 compFrame, f64 fps,
                         std::vector<OutlinedLayerGlyph>& out, const vector::Contour* path) {
    out.clear();
    if (l.kind != LayerKind::Text || !l.captions.empty()) return false;
    fps = fps > 0.0 ? fps : 30.0;
    const TextData* textData = &l.text;
    TextData sourceText;
    const bool sourceKeyed = text::apply_source_text(*textData, static_cast<i64>(std::floor(localFrame)), sourceText);
    if (sourceKeyed) textData = &sourceText;
    TextData expressionText;
    if (l.text.sourceExpression && l.text.sourceExpressionEnabled) {
        expressionText = *textData;
        if (expr::apply_source_expression(l, static_cast<f64>(localFrame), expressionText)) textData = &expressionText;
    }
    TextData evaluatedTracking;
    if (const Track* tracking = l.tracks.find(TrackProperty::TextTracking); tracking && tracking->driven()) {
        evaluatedTracking = *textData;
        evaluatedTracking.tracking = std::clamp(tracking->value_or_f(localFrame, textData->tracking), -1000.f, 1000.f);
        textData = &evaluatedTracking;
    }
    TextData dynamicText;
    if (textData->dynamic) {
        std::string expanded = text::expand_dynamic(*textData, static_cast<f64>(localFrame),
            static_cast<f64>(l.end.value - l.start.value), compFrame, fps);
        if (expanded != textData->content) {
            dynamicText = *textData;
            dynamicText.content = std::move(expanded);
            textData = &dynamicText;
        }
    }
    const auto font = text::FontManager::instance().font_for(*textData);
    if (!font || textData->content.empty()) return false;
    const TextData& T = *textData;
    const bool unitAnimated = layeranim::has_units(l);
    const bool rigAnimated = text::has_rig_animators(T);
    const bool animated = text::has_animators(T) || unitAnimated || rigAnimated;
    TextData shaped;
    const TextData* src = &T;
    if (animated && text::has_content_animators(T)) {
        shaped = T;
        text::apply_text_content(T, l.tracks, static_cast<f64>(localFrame), fps, shaped);
        src = &shaped;
    }
    // A margem de base do texto (a mesma de recenter_text): com ela como `pad`
    // os quads saem direto no espaço da camada.
    const f32 margin = T.strokeWidth > 0.0f ? T.strokeWidth + 2.0f : 2.0f;
    Vec2 shift{0.0f, 0.0f};
    if (sourceKeyed) {
        // Texto-fonte com key: a âncora é a do texto de base (ver o Renderer).
        const TextData& base = l.text;
        const f32 strokeDelta = margin - (base.strokeWidth > 0.0f ? base.strokeWidth + 2.0f : 2.0f);
        shift = Vec2{strokeDelta, strokeDelta};
        if (T.boxMode == 0) {
            const auto baseFont = text::FontManager::instance().font_for(base);
            const f32 wb = baseFont ? text::measure(*baseFont, base).width : 0.0f;
            const f32 wk = text::measure(*font, T).width;
            const auto at = [](u32 align, f32 w) { return align == 1 ? w * 0.5f : align == 2 ? w : 0.0f; };
            shift.x -= at(base.alignment, wb) - at(T.alignment, wk);
        }
    }
    text::TextLayout L;
    if (!text::layout_quads(*font, *src, margin, L)) return false;
    std::vector<text::GlyphOutline> glyphs;
    if (!text::outline_bezier(*font, *src, glyphs)) return false;

    std::vector<text::GlyphAnim> anim(L.quads.size());
    if (animated) {
        std::vector<text::GlyphUnits> units(L.quads.size());
        for (usize g = 0; g < L.quads.size(); ++g) units[g] = text::GlyphUnits{L.quads[g].charIndex, L.quads[g].wordIndex, L.quads[g].lineIndex};
        text::evaluate_text_animators(T, l.tracks, static_cast<f64>(localFrame), fps, units, L.chars, L.words, L.lines, anim);
        anim.resize(L.quads.size());
        if (unitAnimated) {
            const f32 alignment = T.alignment == 1 ? .5f : T.alignment == 2 ? 1.f : 0.f;
            layeranim::apply_to_glyphs(l, localFrame, fps, units, L.chars, L.words, L.lines, alignment, anim);
        }
    }
    std::vector<Mat4> rigTransforms;
    if (rigAnimated) text::evaluate_text_rigs(l, localFrame, L, anim, rigTransforms);
    std::vector<Mat4> pathTransforms;
    if (path && path->pts.size() >= 2) {
        const auto pathMargin = [&](TrackProperty property, f32 fallback) {
            const Track* track = l.tracks.find(property);
            return track && track->driven() ? track->value_or_f(localFrame, fallback) : fallback;
        };
        const f32 first = pathMargin(TrackProperty::TextPathFirstMargin, T.pathOffset);
        const f32 last = pathMargin(TrackProperty::TextPathLastMargin, T.pathLastMargin);
        const u32 lineCount = std::max(1u, L.lines);
        const f64 length = vector::length_of(*path);
        std::vector<f64> distances(L.quads.size());
        for (u32 line = 0; line < lineCount; ++line) {
            std::vector<usize> members;
            std::vector<f32> centers;
            f32 left = 1e30f, right = -1e30f;
            for (usize index = 0; index < L.quads.size(); ++index) {
                const auto& quad = L.quads[index];
                if (quad.lineIndex != line) continue;
                members.push_back(index);
                left = std::min(left, quad.penX);
                right = std::max(right, quad.penX + quad.advance);
            }
            if (members.empty()) continue;
            for (usize member : members) {
                const auto& quad = L.quads[member];
                centers.push_back(quad.penX + quad.advance * .5f - left);
            }
            std::vector<f64> positions(members.size());
            vector::path_line_positions(length, first, last, T.alignment, T.pathForceAlign,
                centers.data(), centers.size(), right - left, positions.data());
            for (usize index = 0; index < members.size(); ++index) distances[members[index]] = positions[index];
        }
        const f32 baseline = L.quads.empty() ? 0 : L.quads.front().baseline;
        for (usize index = 0; index < L.quads.size(); ++index) {
            const auto& quad = L.quads[index];
            if (T.pathLegacyLayout) distances[index] = first + quad.penX + quad.advance * .5f - L.pad;
            Vec2 point;
            f32 angle = 0;
            if (!vector::place_on_path_at(*path, distances[index], T.pathReverse, T.pathPerpendicular,
                quad.baseline - baseline, point, angle)) return false;
            pathTransforms.push_back(Mat4::translation({point.x, point.y, 0}) *
                Mat4::from_quat(Quat::from_axis_angle({0, 0, 1}, angle * kDeg2Rad)) *
                Mat4::translation({-quad.penX - quad.advance * .5f, -quad.baseline, 0}));
        }
    }
    // Caixas de palavra, linha e do texto todo (pivôs do agrupamento).
    const Vec4 emptyBounds{1e30f, 1e30f, -1e30f, -1e30f};
    std::vector<Vec4> wordBounds(std::max(1u, L.words), emptyBounds), lineBounds(std::max(1u, L.lines), emptyBounds);
    Vec4 allBounds = emptyBounds;
    for (const text::GlyphQuad& q : L.quads) {
        const auto extend = [&](Vec4& b) {
            b.x = std::min(b.x, q.x0);
            b.y = std::min(b.y, q.y0);
            b.z = std::max(b.z, q.x1);
            b.w = std::max(b.w, q.y1);
        };
        if (q.wordIndex < wordBounds.size()) extend(wordBounds[q.wordIndex]);
        if (q.lineIndex < lineBounds.size()) extend(lineBounds[q.lineIndex]);
        extend(allBounds);
    }
    // A matriz do glifo: a MESMA conta do Renderer (glyphMatrix).
    const auto glyphMatrix = [&](const text::GlyphQuad& q, const text::GlyphAnim& a) {
        f32 x0 = q.x0, x1 = q.x1, y0 = q.y0, y1 = q.y1;
        const Vec4* group = a.anchorGrouping == 1 && q.wordIndex < wordBounds.size() ? &wordBounds[q.wordIndex]
                      : a.anchorGrouping == 2 && q.lineIndex < lineBounds.size() ? &lineBounds[q.lineIndex]
                      : a.anchorGrouping == 3 ? &allBounds : nullptr;
        if (group) {
            x0 = group->x; y0 = group->y; x1 = group->z; y1 = group->w;
        }
        Vec3 pivot{(x0 + x1) * 0.5f + a.groupAlign.x * (x1 - x0) * 0.5f, (y0 + y1) * 0.5f + a.groupAlign.y * (y1 - y0) * 0.5f, 0};
        if (a.aeModel) {
            const f32 own = std::max(0.0f, q.advance - T.tracking * T.size / 1000.0f);
            f32 cx = q.penX + own * 0.5f, half = own * 0.5f;
            if (group) {
                cx = (x0 + x1) * 0.5f;
                half = std::max(0.0f, (x1 - x0) * 0.5f - text::kGlyphSpread * q.k);
            }
            const f32 cy = a.anchorGrouping == 3 ? (y0 + y1) * 0.5f : q.baseline;
            pivot = Vec3{cx + a.groupAlign.x * half, cy + a.groupAlign.y * L.capHeight * 1.2f, 0};
        }
        const Vec3 anchor{a.anchor.x, a.anchor.y, 0};
        Mat4 m = Mat4::translation(pivot + a.translate + Vec3{a.trackingShift, 0, 0});
        if (a.rotation.x != 0 || a.rotation.y != 0 || a.rotation.z != 0)
            m = m * Mat4::from_quat(Quat::from_euler_zyx(a.rotation.x * kDeg2Rad, a.rotation.y * kDeg2Rad, a.rotation.z * kDeg2Rad));
        m = m * a.skewTransform;
        return m * Mat4::scale(Vec3{a.scale.x, a.scale.y, 1}) * Mat4::translation(-pivot - anchor);
    };
    const f32 strokeMax = text::kGlyphSpread * std::max(1.0f, T.size) / text::kGlyphBasePx - 1.0f;
    const f32 stroke = std::clamp(T.strokeWidth, 0.0f, std::max(0.0f, strokeMax));
    const std::vector<u32> cps = utf8_codepoints(src->content);

    usize qi = 0;
    for (text::GlyphOutline& go : glyphs) {
        // O quad do mesmo glifo (as duas listas saem do mesmo layout, na mesma ordem).
        usize found = L.quads.size();
        for (usize j = qi; j < L.quads.size(); ++j) {
            const text::GlyphQuad& q = L.quads[j];
            if (q.charIndex == go.cluster && std::fabs(q.penX - (go.penX + margin)) < 0.01f
                && std::fabs(q.baseline - (go.baseline + margin)) < 0.01f) {
                found = j;
                break;
            }
        }
        OutlinedGlyph g;
        g.fill = T.color;
        g.stroke = T.strokeColor;
        g.strokeWidth = stroke;
        Mat4 m = Mat4::identity();
        if (found < L.quads.size()) {
            qi = found + 1;
            const text::GlyphQuad& q = L.quads[found];
            const text::GlyphAnim& a = anim[found];
            m = glyphMatrix(q, a);
            if (found < rigTransforms.size()) m = rigTransforms[found] * m;
            if (found < pathTransforms.size()) m = pathTransforms[found] * m;
            g.fill = mix_srgb(q.color, Vec4{a.fill.x, a.fill.y, a.fill.z, 1.0f}, a.fill.w);
            g.fill.w = std::clamp(q.color.w * a.fillOpacity, 0.0f, 1.0f);
            g.stroke = mix_srgb(T.strokeColor, Vec4{a.stroke.x, a.stroke.y, a.stroke.z, 1.0f}, a.stroke.w);
            g.stroke.w = std::clamp(T.strokeColor.w * a.strokeOpacity, 0.0f, 1.0f);
            g.strokeWidth = std::clamp(stroke + a.strokeAdd, 0.0f, std::max(0.0f, text::kGlyphSpread * q.k - 1.0f));
            g.opacity = std::clamp(a.opacity, 0.0f, 1.0f);
        }
        g.label = go.cluster < cps.size() ? utf8_of(cps[go.cluster]) : std::string("?");
        // Ponto do layout (sem margem) → espaço da camada, pela matriz do glifo
        // (a parte XY: rotação 3D por letra sai sem perspectiva).
        const auto point = [&](Vec2 p) {
            const f32 x = p.x + margin, y = p.y + margin;
            return Vec2{m.col[0].x * x + m.col[1].x * y + m.col[3].x - shift.x, m.col[0].y * x + m.col[1].y * y + m.col[3].y - shift.y};
        };
        const auto dir = [&](Vec2 d) { return Vec2{m.col[0].x * d.x + m.col[1].x * d.y, m.col[0].y * d.x + m.col[1].y * d.y}; };
        for (BezierPath& c : go.contours) {
            for (BezierVertex& v : c.v) {
                v.in = dir(v.in);
                v.out = dir(v.out);
                v.p = point(v.p);
            }
        }
        g.contours = std::move(go.contours);
        out.push_back(std::move(g));
    }
    return !out.empty();
}


Result<u64> Engine::text_to_vector(u64 layerId, bool asMasks) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    InlineEditScope inlineScope(*this, layerId, true);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    const LayerId source = LayerId::unpack(layerId);
    Layer* l = comp->layer(source);
    if (!l || l->kind != LayerKind::Text) return Status{Errc::InvalidArgument, "a camada nao e um texto"};
    if (!l->captions.empty()) return Status{Errc::NotSupported, "legendas nao viram contornos"};
    const FrameIndex time = editing_frame_locked();
    std::vector<OutlinedGlyph> glyphs;
    if (!text::outline_layer(*l, static_cast<f64>(l->local_time(time).value), static_cast<f64>(time.value), comp->fps(), glyphs)) return Status{Errc::InvalidArgument, "texto sem contornos"};
    usize contours = 0;
    for (const OutlinedGlyph& g : glyphs) contours += g.contours.size();
    if (asMasks && contours > kMaxMaskCount)
        return Status{Errc::OutOfRange, "texto com contornos demais para virar mascaras (maximo de 32 por camada)"};

    const std::string name = outline_layer_name(*comp, l->name + " " + default_name(DefaultName::Outline));
    const f32 cw = static_cast<f32>(comp->width()), ch = static_cast<f32>(comp->height());
    history_.before_mutation(*comp, project_->timeline().current(), asMasks ? "criar máscaras do texto" : "criar formas do texto");
    bump_model_revision_locked();
    LayerId made{};
    if (!asMasks) {
        // Uma cópia do texto, logo acima dele, que troca o conteúdo: tudo o que
        // é da CAMADA (transformação e keyframes, pai, tempo, efeitos) fica.
        made = comp->duplicate_layer(source, l->start);
        Layer* n = comp->layer(made);
        l = comp->layer(source);
        if (!n || !l) return Status{Errc::OutOfMemory, "camada nao criada"};
        const u32 pathMask = n->text.pathMask;
        n->name = name;
        n->kind = LayerKind::Shape;
        n->text = TextData{};
        n->text.content.clear();   // a camada de formas não guarda texto ("Texto" é só o padrão de camada nova)
        n->visible = true;
        n->tracks.remove_if([](const Track& t) {
            return t.property == TrackProperty::TextTracking || t.property == TrackProperty::TextAnimParam
                || t.property == TrackProperty::TextPathFirstMargin || t.property == TrackProperty::TextPathLastMargin
                || t.property == TrackProperty::TextSource;
        });
        // A máscara que servia de caminho do texto não recortava: aqui recortaria.
        if (pathMask != kInvalidIndex) {
            n->masks.erase(std::remove_if(n->masks.begin(), n->masks.end(), [pathMask](const Mask& m) { return m.id == pathMask; }),
                           n->masks.end());
        }
        ShapeData sh;
        sh.shapeType = kShapeVector;
        sh.bounds = Rect{0.0f, 0.0f, cw, ch};
        sh.vector.groups.reserve(glyphs.size());
        for (OutlinedGlyph& g : glyphs) {
            VectorGroup vg;
            vg.name = g.label;
            vg.opacity = g.opacity * 100.0f;
            for (BezierPath& c : g.contours) {
                VectorPath p;
                p.kind = VectorPathKind::Free;
                p.path = std::move(c);
                vg.paths.push_back(std::move(p));
            }
            vg.fill.enabled = g.fill.w > 0.0f;
            vg.fill.rule = 0;   // não-zero: o sentido dos contornos da fonte abre os furos
            vg.fill.paint.type = 0;
            vg.fill.paint.color = Vec4{g.fill.x, g.fill.y, g.fill.z, 1.0f};
            vg.fill.paint.opacity = g.fill.w * 100.0f;
            vg.stroke.enabled = g.strokeWidth > 0.0f && g.stroke.w > 0.0f;
            vg.stroke.width = g.strokeWidth > 0.0f ? g.strokeWidth * 2.0f : 6.0f;   // centrado: metade para fora
            vg.stroke.paint.type = 0;
            vg.stroke.paint.color = Vec4{g.stroke.x, g.stroke.y, g.stroke.z, 1.0f};
            vg.stroke.paint.opacity = g.stroke.w * 100.0f;
            vg.stroke.join = 1;
            vg.stroke.cap = 1;
            sh.vector.groups.push_back(std::move(vg));
        }
        n->shape = std::move(sh);
    } else {
        // Sólido da cor do texto, do tamanho da composição, com uma máscara por
        // contorno no espaço da composição (a transformação do texto no instante).
        const Mat4 world = world::world_2d_frac(*comp, *l, static_cast<f64>(time.value));
        const Vec4 color = l->text.color;
        const FrameIndex start = l->start, end = l->end;
        made = comp->add_layer(LayerKind::Shape, name);
        Layer* n = comp->layer(made);
        l = comp->layer(source);
        if (!n || !l) return Status{Errc::OutOfMemory, "camada nao criada"};
        n->shape.shapeType = 0;   // retângulo
        n->shape.bounds = Rect{0.0f, 0.0f, cw, ch};
        n->shape.cornerRadius = 0.0f;
        n->shape.fillColor = Vec4{color.x, color.y, color.z, 1.0f};
        n->shape.filled = true;
        n->start = start;
        n->end = end;
        n->transform.anchor = Vec3{cw * 0.5f, ch * 0.5f, 0.0f};
        n->transform.position = Vec3{cw * 0.5f, ch * 0.5f, 0.0f};
        const auto point = [&](Vec2 p) {
            return Vec2{world.col[0].x * p.x + world.col[1].x * p.y + world.col[3].x, world.col[0].y * p.x + world.col[1].y * p.y + world.col[3].y};
        };
        const auto dir = [&](Vec2 d) { return Vec2{world.col[0].x * d.x + world.col[1].x * d.y, world.col[0].y * d.x + world.col[1].y * d.y}; };
        for (OutlinedGlyph& g : glyphs) {
            // O maior contorno dá o sentido "cheio" do glifo; o sentido oposto é furo.
            std::vector<std::pair<f32, usize>> order;
            for (usize i = 0; i < g.contours.size(); ++i) order.emplace_back(contour_area(g.contours[i]), i);
            std::sort(order.begin(), order.end(), [](const std::pair<f32, usize>& a, const std::pair<f32, usize>& b) {
                return std::fabs(a.first) > std::fabs(b.first);
            });
            const bool positive = !order.empty() && order.front().first >= 0.0f;
            for (const std::pair<f32, usize>& entry : order) {
                Mask m;
                m.id = n->alloc_mask_id();
                m.name = g.label;
                m.operation = (entry.first >= 0.0f) == positive ? MaskOperation::Add : MaskOperation::Subtract;
                m.closed = true;
                m.points.reserve(g.contours[entry.second].v.size());
                for (const BezierVertex& v : g.contours[entry.second].v) m.points.push_back(MaskPoint{point(v.p), dir(v.in), dir(v.out)});
                n->masks.push_back(std::move(m));
            }
        }
        const i32 at = comp->order().index_of(source);
        if (at >= 0) (void)comp->reorder_layer(made, static_cast<u32>(at) + 1);
    }
    l->visible = false;   // como no AE: o texto fica, escondido
    comp->rebuild_draw_order();
    comp->touch();
    project_->mark_dirty();
    request_render();
    return made.pack();
}

} // namespace aurea
