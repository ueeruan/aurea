// =============================================================================
//  Aurea / engine / EngineTextExplode.cpp
//
//  0.4.4: Explodir texto — a camada de texto vira uma camada por letra, palavra
//  ou linha. Cada pedaço é um texto LIMPO (só os caracteres dele, sem
//  animador escondendo o resto) e fica exatamente onde estava: o primeiro
//  glifo do pedaço cai no mesmo ponto da tela. Referência de comportamento:
//  TextExploder (aescripts) — implementação própria.
//
//  Camada 2D parada: a âncora vai para o centro do pedaço (girar/escalar cada
//  letra em volta dela) e a posição compensa. Camada 3D ou com transform
//  animado: posição/rotação/escala ficam iguais às da original e a âncora
//  compensa — vale em qualquer instante. Um único desfazer.
// =============================================================================
#include "aurea/Engine.hpp"
#include "aurea/core/LayerSkew.hpp"
#include "aurea/text/FontManager.hpp"
#include "aurea/text/Text.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace aurea {

namespace {

/// Início (byte) de cada code point de `s`, mais o fim.
std::vector<usize> codepoint_offsets(const std::string& s) {
    std::vector<usize> at;
    for (usize i = 0; i < s.size();) {
        at.push_back(i);
        const u8 c = static_cast<u8>(s[i]);
        usize n = 1;
        if ((c >> 5) == 0x6) n = 2;
        else if ((c >> 4) == 0xE) n = 3;
        else if ((c >> 3) == 0x1E) n = 4;
        i = std::min(s.size(), i + n);
    }
    at.push_back(s.size());
    return at;
}

bool is_space(const std::string& s, usize byte) {
    const char c = s[byte];
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

struct Piece {
    u32 begin = 0, end = 0;   ///< code points [begin, end) no texto original
    Vec2 pen{0, 0};           ///< origem do primeiro glifo (px da layer original)
};

f32 static_value(const Layer& l, TrackProperty p, f32 fallback) {
    const Track* tr = l.tracks.find(p);
    return tr ? tr->value_or(FrameIndex{0}, fallback) : fallback;
}

void set_static(Layer& l, TrackProperty p, f32 v) {
    if (Track* tr = l.tracks.find(p); tr && tr->keys.empty()) tr->staticValue = v;
}

/// Soma `d` no valor parado e em todos os keyframes da trilha.
void shift_track(Layer& l, TrackProperty p, f32 d) {
    if (Track* tr = l.tracks.find(p)) {
        tr->staticValue += d;
        for (Keyframe& k : tr->keys) k.value += d;
    }
}

bool transform_driven(const Layer& l) {
    for (TrackProperty p : {TrackProperty::PositionX, TrackProperty::PositionY, TrackProperty::PositionZ, TrackProperty::ScaleX,
                            TrackProperty::ScaleY, TrackProperty::RotationX, TrackProperty::RotationY, TrackProperty::RotationZ,
                            TrackProperty::AnchorX, TrackProperty::AnchorY, TrackProperty::SkewX, TrackProperty::SkewY}) {
        const Track* tr = l.tracks.find(p);
        if (tr && tr->driven()) return true;
    }
    return false;
}

} // namespace

Result<u32> Engine::explode_text(u64 layerId, u32 mode, bool keepOriginal) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    const LayerId sourceId = LayerId::unpack(layerId);
    const Layer* src = comp->layer(sourceId);
    if (!src || src->kind != LayerKind::Text) return Status{Errc::InvalidArgument, "camada nao e texto"};
    if (src->locked) return Status{Errc::InvalidState, "camada travada"};
    if (mode > 2) return Status{Errc::InvalidArgument, "modo invalido"};
    if (!src->captions.empty()) return Status{Errc::NotSupported, "legendas nao se dividem"};
    if (src->text.pathLayer != 0 || src->text.pathMask != kInvalidIndex) return Status{Errc::NotSupported, "texto no caminho"};
    const auto font = text::FontManager::instance().font_for(src->text);
    if (!font) return Status{Errc::NotSupported, "fonte indisponivel"};

    // Os pedaços, pelo layout de verdade (quebras de linha da caixa incluídas).
    const f32 pad = src->text.strokeWidth > 0.0f ? src->text.strokeWidth + 2.0f : 2.0f;
    text::TextLayout L;
    if (!text::layout_quads(*font, src->text, pad, L) || L.quads.empty()) return Status{Errc::InvalidState, "texto vazio"};
    const std::string& content = src->text.content;
    const std::vector<usize> at = codepoint_offsets(content);
    const u32 cpCount = static_cast<u32>(at.size() - 1);
    std::vector<u32> clusters;
    for (const text::GlyphQuad& q : L.quads) clusters.push_back(q.charIndex);
    std::sort(clusters.begin(), clusters.end());
    clusters.erase(std::unique(clusters.begin(), clusters.end()), clusters.end());
    auto cluster_end = [&](u32 c) {
        const auto it = std::upper_bound(clusters.begin(), clusters.end(), c);
        u32 e = it == clusters.end() ? cpCount : *it;
        while (e > c + 1 && is_space(content, at[e - 1])) --e;   // espaço depois do glifo não entra
        return std::min(e, cpCount);
    };
    std::vector<Piece> pieces;
    auto group_of = [&](const text::GlyphQuad& q) { return mode == 0 ? q.charIndex : mode == 1 ? q.wordIndex : q.lineIndex; };
    std::vector<u32> keys;
    for (const text::GlyphQuad& q : L.quads) keys.push_back(group_of(q));
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    for (u32 key : keys) {
        Piece p;
        p.begin = cpCount;
        for (const text::GlyphQuad& q : L.quads) {
            if (group_of(q) != key) continue;
            if (q.charIndex < p.begin) { p.begin = q.charIndex; p.pen = Vec2{q.penX, q.baseline}; }
            p.end = std::max(p.end, cluster_end(q.charIndex));
        }
        if (p.begin < p.end) pieces.push_back(p);
    }
    if (pieces.size() < 2 && keepOriginal) return Status{Errc::InvalidState, "nada para dividir"};
    if (pieces.size() + comp->layers().count() > kMaxLayerCount) return Status{Errc::BudgetExceeded, "camadas demais"};

    history_.before_mutation(*comp, project_->timeline().current(), "explodir texto");
    bump_model_revision_locked();
    const Layer original = *src;
    const bool keepTransform = original.threeD || transform_driven(original);
    const Vec2 a0{static_value(original, TrackProperty::AnchorX, original.transform.anchor.x),
                  static_value(original, TrackProperty::AnchorY, original.transform.anchor.y)};
    // Parte linear do transform 2D (rotação · escala · inclinação), como no renderer.
    const f32 rot = static_value(original, TrackProperty::RotationZ, original.transform.rotation.z) * kDeg2Rad;
    const Mat4 linear = Mat4::from_quat(Quat::from_axis_angle(Vec3{0, 0, 1}, rot))
        * Mat4::scale(Vec3{static_value(original, TrackProperty::ScaleX, original.transform.scale.x),
                           static_value(original, TrackProperty::ScaleY, original.transform.scale.y), 1.0f})
        * skew_matrix(static_value(original, TrackProperty::SkewX, original.transform.skewX),
                      static_value(original, TrackProperty::SkewY, original.transform.skewY));
    const Vec2 p0{static_value(original, TrackProperty::PositionX, original.transform.position.x),
                  static_value(original, TrackProperty::PositionY, original.transform.position.y)};

    u32 made = 0;
    for (const Piece& piece : pieces) {
        const LayerId id = comp->duplicate_layer(sourceId, original.start);
        Layer* n = comp->layer(id);
        if (!n) break;
        TextData& t = n->text;
        t.content = content.substr(at[piece.begin], at[piece.end] - at[piece.begin]);
        t.spans.clear();
        for (const TextSpan& sp : original.text.spans) {
            const u32 b = std::max(sp.start, piece.begin), e = std::min(sp.end, piece.end);
            if (b >= e) continue;
            TextSpan s = sp;
            s.start = b - piece.begin;
            s.end = e - piece.begin;
            t.spans.push_back(s);
        }
        t.boxMode = 0;
        t.autoSize = true;
        n->name = t.content.size() > 24 ? t.content.substr(0, at[std::min<u32>(piece.begin + 20, piece.end)] - at[piece.begin]) + "…" : t.content;
        n->end = original.end;
        n->offset = original.offset;
        // Onde o primeiro glifo cai na camada nova.
        text::TextLayout N;
        Vec2 pen = piece.pen;
        if (text::layout_quads(*font, t, pad, N) && !N.quads.empty()) {
            u32 first = ~0u;
            for (const text::GlyphQuad& q : N.quads)
                if (q.charIndex < first) { first = q.charIndex; pen = Vec2{q.penX, q.baseline}; }
        }
        if (keepTransform) {
            // Mesmo transform da original; a âncora leva o pedaço ao lugar.
            const Vec2 d{pen.x - piece.pen.x, pen.y - piece.pen.y};
            n->transform.anchor.x += d.x;
            n->transform.anchor.y += d.y;
            shift_track(*n, TrackProperty::AnchorX, d.x);
            shift_track(*n, TrackProperty::AnchorY, d.y);
        } else {
            recenter_text(*n);
            const Vec2 an{n->transform.anchor.x, n->transform.anchor.y};
            const Vec4 v = linear * Vec4{piece.pen.x - pen.x + an.x - a0.x, piece.pen.y - pen.y + an.y - a0.y, 0, 0};
            n->transform.position.x = p0.x + v.x;
            n->transform.position.y = p0.y + v.y;
            set_static(*n, TrackProperty::PositionX, n->transform.position.x);
            set_static(*n, TrackProperty::PositionY, n->transform.position.y);
        }
        ++made;
    }
    if (!keepOriginal && made > 0) {
        Command del{};
        del.type = CommandType::LayerDelete;
        del.layer_ref.layer = sourceId;
        (void)apply_command_internal(del, nullptr, false);
    }
    project_->mark_dirty();
    request_render();
    return made;
}

} // namespace aurea
