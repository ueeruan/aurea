// =============================================================================
//  Aurea / engine / EngineOmegaContent.cpp
//
//  Omega 0.3.2 · área 06 (editores de conteúdo). As regras de edição que as
//  duas interfaces (Android e iOS) só controlam:
//   - camada de DESENHO e os traços de caneta, pincel, preencher e borracha
//     (P0-4);
//   - operações de ponto do "Editar pontos" (P1-8; as contas em
//     vector/PathEdit.cpp);
//   - largura da caixa de texto pela alça do palco (P1-10);
//   - alça de tamanho da forma a partir do centro ou da borda (P1-9);
//   - apagar uma fonte importada (P1-12).
//  Mesmo padrão de toda mutação do motor: modelMutex_, history_.before_mutation
//  (menos quando `continuing`), bump_model_revision_locked, mark_dirty e
//  request_render.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/text/FontManager.hpp"
#include "aurea/timeline/ShapeGeometry.hpp"
#include "aurea/vector/PathEdit.hpp"
#include "aurea/vector/Vector.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace aurea {
namespace {

Layer* content_vector_layer(Composition* comp, u64 id) noexcept {
    Layer* l = comp ? comp->layer(LayerId::unpack(id)) : nullptr;
    return l && l->kind == LayerKind::Shape && l->shape.shapeType == kShapeVector ? l : nullptr;
}

vector::Affine2 content_affine(const Mat4& m) noexcept {
    return vector::Affine2{m.col[0].x, m.col[0].y, m.col[1].x, m.col[1].y, m.col[3].x, m.col[3].y};
}

/// A camada de desenho nova: vetorial do tamanho da composição, com a âncora e
/// a posição no centro (espaço da camada = espaço da composição).
Layer* create_drawing_layer(Composition& comp, LayerId& lid) {
    lid = comp.add_layer(LayerKind::Shape, "Desenho");
    Layer* l = comp.layer(lid);
    if (!l) return nullptr;
    const f32 w = static_cast<f32>(comp.width()), h = static_cast<f32>(comp.height());
    l->shape.shapeType = kShapeVector;
    l->shape.bounds = Rect{0.0f, 0.0f, w, h};
    l->shape.vector.drawing = true;
    l->transform.anchor = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    l->transform.position = Vec3{w * 0.5f, h * 0.5f, 0.0f};
    return l;
}

/// Pressão → perfil de largura do pincel: o fator ao longo do comprimento do
/// traço, normalizado pela maior pressão (dedo sem pressão = caneta). Vazio
/// quando a pressão quase não varia.
std::vector<f32> pressure_profile(const std::vector<Vec2>& pts, const std::vector<f32>& press) {
    std::vector<f32> out;
    if (pts.size() < 2 || pts.size() != press.size()) return out;
    const f32 hi = *std::max_element(press.begin(), press.end());
    const f32 lo = *std::min_element(press.begin(), press.end());
    if (!(hi > 1e-4f) || hi - lo < 0.02f) return out;
    std::vector<f32> s(pts.size(), 0.0f);
    for (usize i = 1; i < pts.size(); ++i) s[i] = s[i - 1] + (pts[i] - pts[i - 1]).length();
    const f32 total = s.back();
    if (!(total > 1e-3f)) return out;
    const usize samples = std::clamp<usize>(pts.size(), 2, 64);
    out.resize(samples);
    usize j = 0;
    for (usize k = 0; k < samples; ++k) {
        const f32 at = total * static_cast<f32>(k) / static_cast<f32>(samples - 1);
        while (j + 1 < s.size() && s[j + 1] < at) ++j;
        const usize n = std::min(j + 1, s.size() - 1);
        const f32 span = s[n] - s[j];
        const f32 t = span > 1e-6f ? std::clamp((at - s[j]) / span, 0.0f, 1.0f) : 0.0f;
        const f32 p = press[j] + (press[n] - press[j]) * t;
        out[k] = std::clamp(p / hi, 0.15f, 1.0f);
    }
    return out;
}

} // namespace

// =============================================================================
// Desenho (P0-4)
// =============================================================================
Result<u64> Engine::add_drawing_layer() noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (!project_) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Composition* comp = current_composition();
    if (!comp) return Status{Errc::InvalidState, "projeto sem composicao"};
    history_.before_mutation(*comp, project_->timeline().current(), "camada de desenho");
    bump_model_revision_locked();
    LayerId lid{};
    Layer* l = create_drawing_layer(*comp, lid);
    if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
    const i64 t = std::clamp<i64>(playback_.current().value, 0, std::max<i64>(0, comp->duration().value - 1));
    l->start = FrameIndex{t};
    l->end = FrameIndex{new_layer_end_locked(comp, l, t)};
    project_->mark_dirty();
    request_render();
    return lid.pack();
}

Result<u64> Engine::add_drawing_stroke(u64 layerId, u32 tool, const f32* rgba, f32 width, const f32* xyp, usize count) noexcept {
    if (!xyp || count < 3 || count % 3 != 0) return Status{Errc::InvalidArgument, "traço vazio"};
    if (tool > kDrawEraser) return Status{Errc::InvalidArgument, "ferramenta desconhecida"};
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) return Status{Errc::InvalidState, "nenhum projeto aberto"};
    Layer* l = layerId ? content_vector_layer(comp, layerId) : nullptr;
    if (layerId && !l) return Status{Errc::NotFound, "camada vetorial nao encontrada"};
    if (l && l->locked) return Status{Errc::InvalidState, "camada travada"};
    // Composição → camada. A camada nova nasce com âncora = posição = centro:
    // a conta é a identidade.
    vector::Affine2 world{};
    if (l) world = content_affine(layer_world_matrix(*comp, *l, playback_.current()));
    const vector::Affine2 inv = world.inverse();
    std::vector<Vec2> pts;
    std::vector<f32> press;
    pts.reserve(count / 3);
    press.reserve(count / 3);
    for (usize i = 0; i + 2 < count; i += 3) {
        if (!std::isfinite(xyp[i]) || !std::isfinite(xyp[i + 1])) continue;
        pts.push_back(inv.apply(Vec2{xyp[i], xyp[i + 1]}));
        press.push_back(std::isfinite(xyp[i + 2]) ? std::clamp(xyp[i + 2], 0.0f, 1.0f) : 1.0f);
    }
    if (pts.empty()) return Status{Errc::InvalidArgument, "traço vazio"};
    if (tool == kDrawFill) {
        // Preencher pede uma área: três pontos que não estão no mesmo lugar.
        usize distinct = 1;
        for (usize i = 1; i < pts.size(); ++i) if ((pts[i] - pts[0]).length_sq() > 1.0f) ++distinct;
        if (distinct < 3) return Status{Errc::InvalidArgument, "preencher pede uma area"};
    }
    const f32 layerScale = std::max(1e-4f, world.mean_scale());
    const f32 strokeWidth = std::clamp(std::isfinite(width) ? width : 8.0f, 0.5f, 2000.0f) / layerScale;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    if (rgba) {
        color = Vec4{std::clamp(rgba[0], 0.0f, 1.0f), std::clamp(rgba[1], 0.0f, 1.0f), std::clamp(rgba[2], 0.0f, 1.0f),
                     std::clamp(rgba[3], 0.0f, 1.0f)};
        if (!std::isfinite(color.x + color.y + color.z + color.w)) color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    }

    VectorGroup g;
    VectorPath p;
    p.kind = VectorPathKind::Free;
    p.path = vector::fit_curve(pts, std::max(0.35f, 1.0f / layerScale), tool != kDrawFill);
    g.fill.paint.color = color;
    g.stroke.paint.color = color;
    g.stroke.cap = 1;
    g.stroke.join = 1;
    g.stroke.width = strokeWidth;
    switch (tool) {
        case kDrawFill:
            p.path.closed = true;
            g.fill.enabled = true;
            g.stroke.enabled = false;
            break;
        case kDrawEraser:
            g.fill.enabled = false;
            g.stroke.enabled = true;
            g.stroke.paint.color = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
            g.composite = kVectorCompositeErase;
            break;
        case kDrawBrush:
            g.fill.enabled = false;
            g.stroke.enabled = true;
            g.stroke.widthProfile = pressure_profile(pts, press);
            break;
        default:   // caneta
            g.fill.enabled = false;
            g.stroke.enabled = true;
            break;
    }
    g.paths.push_back(std::move(p));

    history_.before_mutation(*comp, project_->timeline().current(), "traço do desenho");
    bump_model_revision_locked();
    u64 result = layerId;
    if (!l) {
        LayerId lid{};
        l = create_drawing_layer(*comp, lid);
        if (!l) return Status{Errc::OutOfMemory, "camada nao criada"};
        result = lid.pack();
        const i64 t = std::clamp<i64>(playback_.current().value, 0, std::max<i64>(0, comp->duration().value - 1));
        l->start = FrameIndex{t};
        l->end = FrameIndex{new_layer_end_locked(comp, l, t)};
    }
    const usize index = l->shape.vector.groups.size() + 1;
    const char* base = tool == kDrawFill ? "Preenchimento " : tool == kDrawEraser ? "Borracha " : "Traço ";
    g.name = std::string(base) + std::to_string(index);
    l->shape.vector.groups.push_back(std::move(g));
    project_->mark_dirty();
    request_render();
    return result;
}

u32 Engine::vector_layer_flags(u64 layerId) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    const Layer* l = content_vector_layer(project_ ? current_composition() : nullptr, layerId);
    if (!l) return 0;
    return 1u | (l->shape.vector.drawing ? 2u : 0u);
}

// =============================================================================
// Editar pontos (P1-8)
// =============================================================================
i32 Engine::edit_vector_path(u64 layerId, u32 group, u32 path, PathOp op, const f32* args, u32 argc, bool continuing) noexcept {
    std::lock_guard<std::mutex> lock(modelMutex_);
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = content_vector_layer(comp, layerId);
    if (!l || l->locked || group >= l->shape.vector.groups.size() || path >= l->shape.vector.groups[group].paths.size()) return -1;
    if (argc > 0 && !args) return -1;
    for (u32 i = 0; i < argc; ++i) if (!std::isfinite(args[i])) return -1;
    auto index = [&](u32 k) { return static_cast<u32>(std::max(0.0f, std::round(args[k]))); };
    VectorPath& vp = l->shape.vector.groups[group].paths[path];

    if (op == PathOp::SetExclude) {
        if (argc < 1) return -1;
        const bool exclude = args[0] > 0.5f;
        if (vp.exclude == exclude) return 0;
        if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "furo do contorno");
        bump_model_revision_locked();
        vp.exclude = exclude;
        project_->mark_dirty();
        request_render();
        return 0;
    }

    // A forma no cabeçote (paramétrico vira caminho livre com a mesma forma).
    const i64 local = l->local_time(playback_.current()).value;
    VectorPath work = vp;
    if (work.kind != VectorPathKind::Free) vector::make_editable(work);
    BezierPath b = work.keys.empty() ? work.path : vector::path_at(work, static_cast<f64>(local));
    i32 result = 0;
    bool changed = false;
    switch (op) {
        case PathOp::MovePoints: {
            if (argc < 3) return -1;
            const Vec2 d{args[0], args[1]};
            std::vector<u32> idx;
            for (u32 k = 2; k < argc; ++k) idx.push_back(index(k));
            if (d.length_sq() <= 0.0f || idx.empty()) return idx.empty() ? -1 : static_cast<i32>(idx.front());
            vector::pathedit::move_points(b, idx.data(), idx.size(), d);
            result = static_cast<i32>(idx.front());
            changed = true;
            break;
        }
        case PathOp::MoveHandle: {
            if (argc < 6) return -1;
            const u32 i = index(0);
            if (!vector::pathedit::move_handle(b, i, args[1] > 0.5f, Vec2{args[2], args[3]}, args[4] > 0.5f, args[5] > 0.5f)) return -1;
            result = static_cast<i32>(i);
            changed = true;
            break;
        }
        case PathOp::Insert: {
            if (argc < 2) return -1;
            result = vector::pathedit::insert_at(b, index(0), args[1]);
            if (result < 0) return -1;
            changed = true;
            break;
        }
        case PathOp::Remove: {
            if (argc < 1) return -1;
            const u32 i = index(0);
            if (!vector::pathedit::remove_point(b, i)) return -1;
            result = b.v.empty() ? 0 : static_cast<i32>(std::min<usize>(i, b.v.size() - 1));
            changed = true;
            break;
        }
        case PathOp::SetType: {
            if (argc < 2) return -1;
            const u32 i = index(0);
            if (i >= b.v.size()) return -1;
            changed = vector::pathedit::set_point_type(b, i, args[1] > 0.5f);
            result = static_cast<i32>(i);
            break;
        }
        case PathOp::Append: {
            if (argc < 3) return -1;
            result = static_cast<i32>(vector::pathedit::append_point(b, Vec2{args[0], args[1]}, args[2] > 0.5f));
            changed = true;
            break;
        }
        case PathOp::SetClosed: {
            if (argc < 1) return -1;
            changed = vector::pathedit::set_closed(b, args[0] > 0.5f);
            break;
        }
        default:
            return -1;
    }
    if (!changed) return result;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "editar pontos");
    bump_model_revision_locked();
    if (vp.kind != VectorPathKind::Free) vector::make_editable(vp);
    if (op == PathOp::SetClosed) {
        // Abrir/fechar é da estrutura: vale para a forma parada e todos os keyframes.
        vp.path.closed = b.closed;
        for (PathKey& k : vp.keys) k.path.closed = b.closed && k.path.v.size() >= 2;
    }
    if (vp.keys.empty()) {
        vp.path = std::move(b);
    } else {
        // Forma animada: o que se edita é o keyframe do cabeçote (cria se não houver).
        auto it = std::find_if(vp.keys.begin(), vp.keys.end(), [&](const PathKey& k) { return k.frame == local; });
        if (it != vp.keys.end()) {
            it->path = std::move(b);
        } else {
            PathKey k;
            k.frame = local;
            k.path = std::move(b);
            vp.keys.insert(std::upper_bound(vp.keys.begin(), vp.keys.end(), local, [](i64 f, const PathKey& x) { return f < x.frame; }),
                           std::move(k));
        }
    }
    project_->mark_dirty();
    request_render();
    return result;
}

// =============================================================================
// Texto: largura da caixa pela alça do palco (P1-10)
// =============================================================================
bool Engine::drag_text_box_width(u64 layerId, f32 dx, f32 dy, bool continuing) noexcept {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || l->kind != LayerKind::Text) return false;
    const vector::Affine2 world = content_affine(layer_world_matrix(*comp, *l, playback_.current()));
    const Vec2 d = world.inverse().apply_vec(Vec2{dx, dy});
    TextData& t = l->text;
    f32 width = t.box.w;
    if (t.boxMode == 0) {
        // Texto de ponto: a caixa nasce com a largura que ele tem agora.
        if (const auto font = text::FontManager::instance().font_for(t)) width = std::ceil(text::measure(*font, t).width) + 1.0f;
    }
    const f32 grow = t.alignment == 1 ? 2.0f * d.x : t.alignment == 2 ? -d.x : d.x;
    const f32 next = std::round(std::max(50.0f, width + grow));
    if (t.boxMode != 0 && std::fabs(next - t.box.w) < 0.5f) return true;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "largura da caixa de texto");
    bump_model_revision_locked();
    if (t.boxMode == 0) {
        t.boxMode = 1;
        t.autoSize = false;
        t.box.h = std::max(t.box.h, 10.0f);
    }
    t.box.w = std::clamp(next, 10.0f, 20000.0f);
    // A borda oposta à alça fica parada: o centro (a âncora) anda meia diferença
    // no eixo X da camada. Com posição animada a caixa cresce em volta do centro.
    const f32 applied = t.box.w - width;
    const f32 shift = t.alignment == 0 ? applied * 0.5f : t.alignment == 2 ? -applied * 0.5f : 0.0f;
    Track* px = l->tracks.find(TrackProperty::PositionX);
    Track* py = l->tracks.find(TrackProperty::PositionY);
    const bool animated = (px && px->driven()) || (py && py->driven());
    if (std::fabs(shift) > 1e-4f && !animated) {
        const f32 rz = l->transform.rotation.z * kDeg2Rad;
        const f32 sx = l->transform.scale.x;
        l->transform.position.x += std::cos(rz) * shift * sx;
        l->transform.position.y += std::sin(rz) * shift * sx;
        if (px) px->staticValue = l->transform.position.x;
        if (py) py->staticValue = l->transform.position.y;
    }
    recenter_text(*l);
    project_->mark_dirty();
    request_render();
    return true;
}

// =============================================================================
// Forma: alça de tamanho do centro ou da borda (P1-9)
// =============================================================================
bool Engine::resize_shape_handle(u64 layerId, u32 handle, f32 du, f32 dv, bool keepRatio, bool fromCenter, bool continuing) noexcept {
    static constexpr i32 kSignX[8] = {-1, 1, 1, -1, 0, 1, 0, -1};
    static constexpr i32 kSignY[8] = {-1, -1, 1, 1, -1, 0, 1, 0};
    if (handle > 7 || !std::isfinite(du) || !std::isfinite(dv)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    Layer* l = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!l || l->locked || l->kind != LayerKind::Shape || l->shape.shapeType == kShapeVector) return false;
    const FrameIndex local = l->local_time(playback_.current());
    auto current = [&](u32 param) {
        const f32* r = shape::param_field(l->shape, param);
        const Track* tr = l->tracks.find(TrackProperty::ShapeParam, 0, param);
        return tr && !tr->keys.empty() ? tr->sample(local) : (r ? *r : 1.0f);
    };
    const f32 w0 = std::max(1.0f, current(shape::kParamWidth));
    const f32 h0 = std::max(1.0f, current(shape::kParamHeight));
    const i32 sx = kSignX[handle], sy = kSignY[handle];
    const f32 k = fromCenter ? 2.0f : 1.0f;   // do centro: o lado oposto espelha
    f32 w = sx != 0 ? w0 + k * static_cast<f32>(sx) * du : w0;
    f32 h = sy != 0 ? h0 + k * static_cast<f32>(sy) * dv : h0;
    if (keepRatio) {
        const f32 kx = w / w0, ky = h / h0;
        const f32 s = sx == 0 ? ky : sy == 0 ? kx : (std::fabs(kx - 1.0f) >= std::fabs(ky - 1.0f) ? kx : ky);
        w = w0 * s;
        h = h0 * s;
    }
    w = std::clamp(w, 1.0f, 16384.0f);
    h = std::clamp(h, 1.0f, 16384.0f);
    if (std::fabs(w - w0) < 1e-4f && std::fabs(h - h0) < 1e-4f) return true;
    if (!continuing) history_.before_mutation(*comp, project_->timeline().current(), "tamanho da forma");
    bump_model_revision_locked();
    // O mesmo caminho de `set_shape_param`: com keyframes grava no cabeçote;
    // sem, o valor parado e a âncora no centro (enquanto nenhuma trilha existe).
    auto write = [&](u32 param, f32 value) {
        f32* r = shape::param_field(l->shape, param);
        if (!r) return;
        value = shape::clamp_param(param, value);
        Track* tr = l->tracks.find(TrackProperty::ShapeParam, 0, param);
        if (tr && !tr->keys.empty()) {
            const u32 at = tr->find_exact(local);
            if (at != kInvalidIndex) tr->keys[at].value = value;
            else (void)tr->set(local, value, Interpolation::Bezier);
            return;
        }
        *r = value;
        bool centered = false;
        for (u32 p = 1; p <= shape::kParamHeight; ++p) centered |= l->tracks.find(TrackProperty::ShapeParam, 0, p) != nullptr;
        if (!centered) {
            if (param == shape::kParamWidth) l->transform.anchor.x = value * 0.5f;
            if (param == shape::kParamHeight) l->transform.anchor.y = value * 0.5f;
        }
    };
    write(shape::kParamWidth, w);
    write(shape::kParamHeight, h);
    if (!fromCenter) {
        // A borda oposta fica: o centro anda metade da diferença para o lado da alça.
        const f32 cu = static_cast<f32>(sx) * (w - w0) * 0.5f;
        const f32 cv = static_cast<f32>(sy) * (h - h0) * 0.5f;
        Track* px = l->tracks.find(TrackProperty::PositionX);
        Track* py = l->tracks.find(TrackProperty::PositionY);
        const bool animated = (px && px->driven()) || (py && py->driven());
        if (!animated && (std::fabs(cu) > 1e-5f || std::fabs(cv) > 1e-5f)) {
            const f32 rz = l->transform.rotation.z * kDeg2Rad;
            const f32 c = std::cos(rz), s = std::sin(rz);
            const f32 ux = cu * l->transform.scale.x, vy = cv * l->transform.scale.y;
            l->transform.position.x += c * ux - s * vy;
            l->transform.position.y += s * ux + c * vy;
            if (px) px->staticValue = l->transform.position.x;
            if (py) py->staticValue = l->transform.position.y;
        }
    }
    project_->mark_dirty();
    request_render();
    return true;
}

// =============================================================================
// Fontes importadas: apagar (P1-12)
// =============================================================================
bool Engine::remove_imported_font(const std::string& path) noexcept {
    if (path.empty()) return false;
    text::FontManager& fonts = text::FontManager::instance();
    if (!fonts.remove_imported(path)) return false;
    std::lock_guard<std::mutex> lock(modelMutex_);
    if (project_) {
        bool touched = false;
        project_->timeline().for_each_composition([&](CompositionId, Composition& c) {
            c.layers().for_each([&](LayerId, Layer& layer) {
                if (layer.kind != LayerKind::Text || layer.text.fontPath.empty()) return;
                if (layer.text.fontPath != path && fonts.resolve(layer.text.fontPath) != path) return;
                // O texto volta para a fonte padrão do aparelho.
                layer.text.fontPath.clear();
                layer.text.fontFamily.clear();
                layer.text.fontWeight = 400;
                layer.text.fontItalic = false;
                recenter_text(layer);
                touched = true;
            });
        });
        if (touched) {
            bump_model_revision_locked();
            project_->mark_dirty();
        }
    }
    request_render();
    return true;
}

} // namespace aurea
