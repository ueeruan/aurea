// Editor de gráfico: gráfico de valor (com alças Bézier por trecho) e gráfico
// de velocidade das propriedades animadas do clipe principal. A curva desenhada
// vem do motor (query_track_curve): é exatamente o que a prévia e a exportação usam.
#include "Studio.hpp"
#include "imgui_internal.h"
#include <cmath>
#include <cstdio>
#include <set>

namespace aurea::studio {
namespace {

struct GraphState {
    double viewStart = 0, viewFrames = 300;
    bool autoFit = true;
    float vMin = 0, vMax = 1;
    u64 layer = 0;
    enum class Drag { None, Keys, HandleOut, HandleIn, Box, Pan, Scrub } drag = Drag::None;
    ImVec2 down{};
    double downStart = 0;
    float downMin = 0, downMax = 0;
    struct Orig { TrackKey t; i32 time; float value; };
    std::vector<Orig> origin;
    i32 lastDt = 0;
    TrackKey handleTrack{};
    i32 handleOwner = 0;            ///< keyframe dono do trecho (esquerda)
    float handleEase[4]{};
    i32 segStart = 0, segEnd = 0;
    float segV0 = 0, segV1 = 0;
    // Curvas amostradas em cache: só refeitas quando algo muda.
    struct Curve { TrackKey t; std::vector<float> v; };
    std::vector<Curve> curves;
    u32 cacheRevision = ~0u;
    double cacheStart = -1, cacheFrames = -1;
    int cacheWidth = 0;
    size_t cacheTracks = 0;
} G;

std::string track_label(Studio& s, const TrackKey& t) {
    if (t.property == u32(TrackProperty::EffectParam)) {
        for (auto& fx : s.effects) if (fx.row.effectId == t.effect) {
            for (auto& p : fx.params) if (p.index == t.param / 4) {
                std::string l = fx.name + " / " + fx.text(p.labelOffset, p.labelLength);
                const u32 comps = p.type == u32(ParamType::Point2D) ? 2 : p.type == u32(ParamType::Point3D) ? 3 : p.type == u32(ParamType::Color) ? 4 : 1;
                if (comps > 1) l += std::string(" ") + (p.type == u32(ParamType::Color) ? "RGBA" : "XYZ")[t.param % 4];
                return l;
            }
            return fx.name;
        }
        return "Efeito";
    }
    return track_property_name(t.property);
}

ImU32 track_color(const TrackKey& t, size_t index) {
    if (t.property < 12) { const u32 axis = t.property % 3; return axis == 0 ? col::AxisX : axis == 1 ? col::AxisY : col::AxisZ; }
    if (t.property == u32(TrackProperty::EffectParam)) { const u32 c = t.param % 4; if (c == 1) return col::AxisY; if (c == 2) return col::AxisZ; }
    const ImU32 palette[] = {col::Keyframe, col::Warning, col::rgb(0xC792EA), col::rgb(0xF78C6C), col::Success, col::Accent};
    return palette[index % std::size(palette)];
}

std::vector<TrackKey> animated_tracks(Studio& s) {
    std::set<TrackKey> set;
    for (auto& k : s.layer_keys(s.primary)) {
        if (k.property == u32(TrackProperty::MeshWarp) || k.property == u32(TrackProperty::RigBone) || k.property == u32(TrackProperty::BonePose)) continue;
        set.insert({k.property, k.effectIndex, k.paramIndex});
    }
    return {set.begin(), set.end()};
}

struct KeyPoint { i32 time; float value; u32 interp; };
std::vector<KeyPoint> track_keys(Studio& s, const TrackKey& t) {
    std::vector<KeyPoint> out;
    for (auto& k : s.layer_keys(s.primary)) if (k.property == t.property && k.effectIndex == t.effect && k.paramIndex == t.param) out.push_back({k.time, k.value, k.interpolation});
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.time < b.time; });
    return out;
}

bool selected(Studio& s, const TrackKey& t, i32 time) {
    for (auto& [k, tt] : s.selectedKeys) if (k == t && tt == time) return true;
    return false;
}

void ease(Studio& s, const TrackKey& t, const KeyPoint& k, float out[4]) {
    if (k.interp != u32(Interpolation::Bezier) || !s.key_easing(s.primary, t, k.time, out)) {
        out[0] = 1 / 3.f; out[1] = 1 / 3.f; out[2] = 2 / 3.f; out[3] = 2 / 3.f;
    }
}

} // namespace

void draw_graph_editor(Studio& s) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin("Editor de gráfico", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) { ImGui::End(); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 o = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    if (!s.primary) {
        ImGui::SetCursorScreenPos(ImVec2(o.x + S(16), o.y + S(16)));
        ImGui::TextDisabled("Selecione um clipe com keyframes para editar as curvas de animação.");
        ImGui::End();
        return;
    }
    const auto tracks = animated_tracks(s);
    if (G.layer != s.primary) {
        G.layer = s.primary;
        G.viewStart = s.detail.startFrame;
        G.viewFrames = std::max(10, s.detail.endFrame - s.detail.startFrame);
        G.autoFit = true;
        s.graphTracks.clear();
    }
    // Sem escolha explícita: todas as animadas (como mostrar as propriedades selecionadas).
    std::vector<TrackKey> shown = s.graphTracks;
    shown.erase(std::remove_if(shown.begin(), shown.end(), [&](auto& t) { return std::find(tracks.begin(), tracks.end(), t) == tracks.end(); }), shown.end());
    if (shown.empty()) shown = tracks;

    const float listW = S(210), toolH = S(32), rulerH = S(24);
    // --- Lista de propriedades ------------------------------------------------
    dl->AddRectFilled(o, ImVec2(o.x + listW, o.y + avail.y), col::Surface);
    ImGui::SetCursorScreenPos(ImVec2(o.x, o.y));
    ImGui::BeginChild("##props", ImVec2(listW, avail.y), ImGuiChildFlags_None);
    ImGui::Dummy(ImVec2(1, S(2)));
    if (tracks.empty()) { ImGui::SetCursorPosX(S(10)); ImGui::TextDisabled("Sem keyframes."); }
    for (size_t i = 0; i < tracks.size(); ++i) {
        const auto& t = tracks[i];
        ImGui::PushID(int(i));
        const bool on = std::find(shown.begin(), shown.end(), t) != shown.end();
        const ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x + S(8), a.y + S(5)), ImVec2(a.x + S(16), a.y + S(13)), track_color(t, i), S(2));
        ImGui::SetCursorScreenPos(ImVec2(a.x + S(22), a.y));
        const std::string label = track_label(s, t);
        if (ImGui::Selectable(label.c_str(), on && !s.graphTracks.empty())) {
            if (io.KeyCtrl || io.KeyShift) {
                if (s.graphTracks.empty()) s.graphTracks = shown;
                auto it = std::find(s.graphTracks.begin(), s.graphTracks.end(), t);
                if (it != s.graphTracks.end()) s.graphTracks.erase(it); else s.graphTracks.push_back(t);
            } else s.graphTracks = {t};
        }
        ImGui::PopID();
    }
    if (!s.graphTracks.empty()) {
        ImGui::Dummy(ImVec2(1, S(6)));
        ImGui::SetCursorPosX(S(10));
        if (ImGui::SmallButton("Mostrar todas")) s.graphTracks.clear();
    }
    ImGui::EndChild();

    // --- Área do gráfico -----------------------------------------------------
    const float gx0 = o.x + listW + 1, gx1 = o.x + avail.x, gy0 = o.y + rulerH, gy1 = o.y + avail.y - toolH;
    if (gx1 - gx0 < S(60) || gy1 - gy0 < S(40)) { ImGui::End(); return; }
    const double fps = s.fps();
    G.viewFrames = std::clamp(G.viewFrames, 4.0, 1e6);
    auto X = [&](double timelineFrame) { return float(gx0 + (timelineFrame - G.viewStart) / G.viewFrames * (gx1 - gx0)); };
    auto Fr = [&](float x) { return G.viewStart + (x - gx0) / (gx1 - gx0) * G.viewFrames; };
    const double toTimeline = double(s.detail.startFrame) - s.detail.offsetFrames;  // local → linha do tempo

    // Amostragem em cache.
    const int width = int(gx1 - gx0);
    const int count = std::clamp(width / 2, 16, 2048);
    if (G.cacheRevision != s.revision || G.cacheStart != G.viewStart || G.cacheFrames != G.viewFrames || G.cacheWidth != width || G.cacheTracks != shown.size()) {
        G.cacheRevision = s.revision;
        G.cacheStart = G.viewStart;
        G.cacheFrames = G.viewFrames;
        G.cacheWidth = width;
        G.cacheTracks = shown.size();
        G.curves.clear();
        const i32 from = i32(std::floor(G.viewStart - toTimeline)), to = i32(std::ceil(G.viewStart + G.viewFrames - toTimeline));
        for (auto& t : shown) {
            GraphState::Curve c;
            c.t = t;
            c.v.resize(size_t(count));
            const u32 n = s.engine.query_track_curve(s.primary, t.property, t.effect, t.param, from, std::max(from + 1, to), c.v.data(), u32(count));
            c.v.resize(n);
            G.curves.push_back(std::move(c));
        }
    }
    const float sampleSpanFrames = float(std::ceil(G.viewStart + G.viewFrames - toTimeline) - std::floor(G.viewStart - toTimeline));
    const double sampleStart = std::floor(G.viewStart - toTimeline) + toTimeline;
    // Valores ou velocidades exibidos.
    std::vector<std::vector<float>> plot(G.curves.size());
    float lo = FLT_MAX, hi = -FLT_MAX;
    for (size_t i = 0; i < G.curves.size(); ++i) {
        const auto& v = G.curves[i].v;
        auto& p = plot[i];
        p.resize(v.size());
        const float dt = v.size() > 1 ? sampleSpanFrames / float(v.size() - 1) : 1.f;
        for (size_t k = 0; k < v.size(); ++k) {
            if (s.speedGraph) {
                const size_t a = k > 0 ? k - 1 : k, b = k + 1 < v.size() ? k + 1 : k;
                p[k] = b > a ? (v[b] - v[a]) / (float(b - a) * dt) * float(fps) : 0.f;
            } else p[k] = v[k];
            lo = std::min(lo, p[k]);
            hi = std::max(hi, p[k]);
        }
    }
    if (lo > hi) { lo = 0; hi = 1; }
    if (G.autoFit && G.drag == GraphState::Drag::None) {
        const float pad = std::max((hi - lo) * .12f, std::abs(hi) * .02f + 1e-3f);
        G.vMin = lo - pad;
        G.vMax = hi + pad;
    }
    if (G.vMax - G.vMin < 1e-5f) G.vMax = G.vMin + 1e-3f;
    auto Y = [&](float v) { return gy1 - (v - G.vMin) / (G.vMax - G.vMin) * (gy1 - gy0); };
    auto V = [&](float y) { return G.vMin + (gy1 - y) / (gy1 - gy0) * (G.vMax - G.vMin); };

    dl->AddRectFilled(ImVec2(gx0, gy0), ImVec2(gx1, gy1), col::Stage);
    // Faixa do clipe.
    dl->AddRectFilled(ImVec2(std::max(gx0, X(s.detail.startFrame)), gy0), ImVec2(std::min(gx1, X(s.detail.endFrame)), gy1), col::alpha(col::Background, .9f));
    dl->PushClipRect(ImVec2(gx0, o.y), ImVec2(gx1, gy1), true);
    // Grade horizontal com valores.
    {
        const float range = G.vMax - G.vMin;
        const float raw = range / std::max(2.f, (gy1 - gy0) / S(44));
        const float mag = std::pow(10.f, std::floor(std::log10(raw)));
        const float step = raw / mag < 2 ? 2 * mag : raw / mag < 5 ? 5 * mag : 10 * mag;
        for (float v = std::ceil(G.vMin / step) * step; v <= G.vMax; v += step) {
            const float y = Y(v);
            dl->AddLine(ImVec2(gx0, y), ImVec2(gx1, y), std::abs(v) < step * .01f ? col::TickWeak : col::alpha(col::CurveGrid, .55f));
            char b[32];
            std::snprintf(b, sizeof b, step >= 1 ? "%.0f" : step >= .1f ? "%.1f" : "%.2f", double(v));
            dl->AddText(gFontSmall, gFontSmall->LegacySize * s.dpi, ImVec2(gx0 + S(4), y - S(13)), col::Subtle, b);
        }
    }
    // Régua e grade vertical.
    dl->AddRectFilled(ImVec2(gx0, o.y), ImVec2(gx1, gy0), col::Background);
    {
        const double fpsR = std::max(1.0, std::round(fps));
        const double steps[] = {1, 2, 5, 10, fpsR / 2, fpsR, 2 * fpsR, 5 * fpsR, 10 * fpsR, 30 * fpsR, 60 * fpsR, 300 * fpsR};
        double major = steps[std::size(steps) - 1];
        for (double st : steps) if (st >= 1 && st / G.viewFrames * (gx1 - gx0) >= S(80)) { major = st; break; }
        for (double f = std::floor(G.viewStart / major) * major; X(f) < gx1; f += major) {
            const float x = X(f);
            dl->AddLine(ImVec2(x, gy0 - S(8)), ImVec2(x, gy1), col::alpha(col::CurveGrid, .45f));
            dl->AddText(gFontSmall, gFontSmall->LegacySize * s.dpi, ImVec2(x + S(3), o.y + S(3)), col::Muted, s.timecode(i64(std::llround(f))).c_str());
        }
    }
    // Curvas.
    for (size_t i = 0; i < plot.size(); ++i) {
        const auto& p = plot[i];
        if (p.size() < 2) continue;
        const ImU32 c = track_color(G.curves[i].t, size_t(std::find(tracks.begin(), tracks.end(), G.curves[i].t) - tracks.begin()));
        std::vector<ImVec2> pts(p.size());
        for (size_t k = 0; k < p.size(); ++k) pts[k] = ImVec2(X(sampleStart + double(k) * sampleSpanFrames / (p.size() - 1)), Y(p[k]));
        dl->AddPolyline(pts.data(), int(pts.size()), c, S(1.6f));
    }
    // Keyframes e alças.
    struct Hit { ImRect r; TrackKey t; i32 time; float value; int kind; i32 owner; float e[4]; i32 a, b; float va, vb; };
    std::vector<Hit> hits;
    if (!s.speedGraph) {
        for (auto& t : shown) {
            const auto ks = track_keys(s, t);
            for (size_t i = 0; i < ks.size(); ++i) {
                const ImVec2 p(X(ks[i].time + toTimeline), Y(ks[i].value));
                const bool sel = selected(s, t, ks[i].time);
                if (sel) {
                    // Alça de saída (trecho i → i+1) e de chegada (trecho i-1 → i).
                    if (i + 1 < ks.size() && ks[i].interp != u32(Interpolation::Hold)) {
                        float e[4];
                        ease(s, t, ks[i], e);
                        const float ta = float(ks[i].time), tb = float(ks[i + 1].time), va = ks[i].value, vb = ks[i + 1].value;
                        const ImVec2 h(X(ta + e[0] * (tb - ta) + toTimeline), Y(va + e[1] * (vb - va)));
                        dl->AddLine(p, h, col::Warning, S(1));
                        dl->AddCircleFilled(h, S(4), col::Warning);
                        Hit hit{ImRect(ImVec2(h.x - S(6), h.y - S(6)), ImVec2(h.x + S(6), h.y + S(6))), t, ks[i].time, ks[i].value, 1, ks[i].time, {e[0], e[1], e[2], e[3]}, ks[i].time, ks[i + 1].time, va, vb};
                        hits.push_back(hit);
                    }
                    if (i > 0 && ks[i - 1].interp != u32(Interpolation::Hold)) {
                        float e[4];
                        ease(s, t, ks[i - 1], e);
                        const float ta = float(ks[i - 1].time), tb = float(ks[i].time), va = ks[i - 1].value, vb = ks[i].value;
                        const ImVec2 h(X(ta + e[2] * (tb - ta) + toTimeline), Y(va + e[3] * (vb - va)));
                        dl->AddLine(p, h, col::Warning, S(1));
                        dl->AddCircleFilled(h, S(4), col::Warning);
                        Hit hit{ImRect(ImVec2(h.x - S(6), h.y - S(6)), ImVec2(h.x + S(6), h.y + S(6))), t, ks[i].time, ks[i].value, 2, ks[i - 1].time, {e[0], e[1], e[2], e[3]}, ks[i - 1].time, ks[i].time, va, vb};
                        hits.push_back(hit);
                    }
                }
                const float r = S(4);
                dl->AddRectFilled(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), sel ? col::Warning : col::Stage);
                dl->AddRect(ImVec2(p.x - r, p.y - r), ImVec2(p.x + r, p.y + r), sel ? col::Text : col::Keyframe, 0, S(1.2f));
                hits.push_back({ImRect(ImVec2(p.x - S(6), p.y - S(6)), ImVec2(p.x + S(6), p.y + S(6))), t, ks[i].time, ks[i].value, 0, 0, {}, 0, 0, 0, 0});
            }
        }
    } else {
        // Velocidade: os keyframes ficam marcados na base do gráfico.
        for (auto& t : shown) for (auto& k : track_keys(s, t)) {
            const float x = X(k.time + toTimeline);
            const bool sel = selected(s, t, k.time);
            draw_diamond(dl, ImVec2(x, gy1 - S(8)), S(4.5f), sel ? col::Warning : col::Keyframe, col::Stage);
            hits.push_back({ImRect(ImVec2(x - S(6), gy1 - S(15)), ImVec2(x + S(6), gy1 - S(1))), t, k.time, k.value, 0, 0, {}, 0, 0, 0, 0});
        }
    }
    // Indicador.
    const float px = X(double(s.playhead()));
    if (px >= gx0 && px <= gx1) {
        dl->AddLine(ImVec2(px, o.y), ImVec2(px, gy1), col::Playhead);
        const ImVec2 head[3] = {{px - S(5), o.y + S(1)}, {px + S(5), o.y + S(1)}, {px, o.y + S(9)}};
        dl->AddConvexPolyFilled(head, 3, col::Accent);
    }
    dl->PopClipRect();
    dl->AddText(ImVec2(gx1 - S(150), gy0 + S(6)), col::Subtle, s.speedGraph ? "Gráfico de velocidade (un./s)" : "Gráfico de valor");

    // --- Interação -------------------------------------------------------------
    ImGui::SetCursorScreenPos(ImVec2(gx0, o.y));
    ImGui::InvisibleButton("##graph", ImVec2(gx1 - gx0, gy1 - o.y), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 m = io.MousePos;
    const Hit* under = nullptr;
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (it->r.Contains(m)) { under = &*it; break; }
    if (ImGui::IsItemHovered()) {
        if (io.MouseWheel != 0) {
            if (io.KeyCtrl) {
                G.autoFit = false;
                const float at = V(m.y), k = float(std::pow(1.15, -io.MouseWheel));
                G.vMin = at - (at - G.vMin) * k;
                G.vMax = at + (G.vMax - at) * k;
            } else {
                const double at = Fr(m.x), k = std::pow(1.15, -io.MouseWheel);
                G.viewFrames *= k;
                G.viewStart = at - (m.x - gx0) / (gx1 - gx0) * G.viewFrames;
            }
        }
        if (under && under->kind) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseDoubleClicked(0) && !under) G.autoFit = true;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) { G.drag = GraphState::Drag::Pan; G.down = m; G.downStart = G.viewStart; G.downMin = G.vMin; G.downMax = G.vMax; }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (under && !under->kind && !selected(s, under->t, under->time)) { s.selectedKeys.clear(); s.selectedKeys.push_back({under->t, under->time}); }
        ImGui::OpenPopup("##graphmenu");
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        G.down = m;
        G.lastDt = 0;
        if (m.y < gy0) { G.drag = GraphState::Drag::Scrub; }
        else if (under && under->kind) {
            G.drag = under->kind == 1 ? GraphState::Drag::HandleOut : GraphState::Drag::HandleIn;
            G.handleTrack = under->t;
            G.handleOwner = under->owner;
            std::copy(under->e, under->e + 4, G.handleEase);
            G.segStart = under->a; G.segEnd = under->b; G.segV0 = under->va; G.segV1 = under->vb;
            s.begin_group();
        } else if (under) {
            const bool add = io.KeyShift || io.KeyCtrl;
            if (!selected(s, under->t, under->time)) { if (!add) s.selectedKeys.clear(); s.selectedKeys.push_back({under->t, under->time}); }
            else if (add) s.selectedKeys.erase(std::remove_if(s.selectedKeys.begin(), s.selectedKeys.end(), [&](auto& k) { return k.first == under->t && k.second == under->time; }), s.selectedKeys.end());
            G.drag = GraphState::Drag::Keys;
            G.origin.clear();
            for (auto& [t, time] : s.selectedKeys) for (auto& k : track_keys(s, t)) if (k.time == time) G.origin.push_back({t, time, k.value});
            s.begin_group();
        } else {
            if (!io.KeyShift && !io.KeyCtrl) s.selectedKeys.clear();
            G.drag = GraphState::Drag::Box;
        }
    }
    if (ImGui::BeginPopup("##graphmenu")) {
        if (ImGui::MenuItem("Suavização fácil", "F9")) s.apply_ease(0);
        if (ImGui::MenuItem("Suavização fácil de entrada", "Shift+F9")) s.apply_ease(1);
        if (ImGui::MenuItem("Suavização fácil de saída", "Ctrl+Shift+F9")) s.apply_ease(2);
        ImGui::Separator();
        if (ImGui::MenuItem("Linear")) s.apply_ease(3);
        if (ImGui::MenuItem("Manter")) s.apply_ease(4);
        if (ImGui::MenuItem("Quicar")) s.apply_ease(5);
        if (ImGui::MenuItem("Elástico")) s.apply_ease(6);
        if (ImGui::MenuItem("Ultrapassar")) s.apply_ease(7);
        ImGui::Separator();
        if (ImGui::MenuItem("Excluir keyframes", "Del")) { s.delete_keys(s.primary, s.selectedKeys); s.selectedKeys.clear(); }
        if (ImGui::MenuItem("Ajustar tudo à visualização")) { G.autoFit = true; G.viewStart = s.detail.startFrame; G.viewFrames = std::max(10, s.detail.endFrame - s.detail.startFrame); }
        ImGui::EndPopup();
    }
    if (G.drag != GraphState::Drag::None) {
        if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2)) {
            if (G.drag == GraphState::Drag::Keys || G.drag == GraphState::Drag::HandleIn || G.drag == GraphState::Drag::HandleOut) s.end_group();
            if (G.drag == GraphState::Drag::Box) {
                const ImRect box(ImMin(G.down, m), ImMax(G.down, m));
                for (auto& h : hits) if (!h.kind && box.Overlaps(h.r) && !selected(s, h.t, h.time)) s.selectedKeys.push_back({h.t, h.time});
            }
            G.drag = GraphState::Drag::None;
        } else {
            switch (G.drag) {
            case GraphState::Drag::Pan:
                G.viewStart = G.downStart - (m.x - G.down.x) / (gx1 - gx0) * G.viewFrames;
                if (!G.autoFit) {
                    const float dv = (m.y - G.down.y) / (gy1 - gy0) * (G.downMax - G.downMin);
                    G.vMin = G.downMin + dv; G.vMax = G.downMax + dv;
                }
                break;
            case GraphState::Drag::Scrub:
                s.seek(i64(std::llround(Fr(m.x))));
                break;
            case GraphState::Drag::Box:
                dl->AddRectFilled(ImMin(G.down, m), ImMax(G.down, m), col::alpha(col::Accent, .12f));
                dl->AddRect(ImMin(G.down, m), ImMax(G.down, m), col::Accent);
                break;
            case GraphState::Drag::Keys: {
                float dx = m.x - G.down.x, dy = m.y - G.down.y;
                if (io.KeyShift) { if (std::abs(dx) > std::abs(dy)) dy = 0; else dx = 0; }
                const i32 dt = i32(std::llround(dx / (gx1 - gx0) * G.viewFrames));
                const float dv = s.speedGraph ? 0.f : -dy / (gy1 - gy0) * (G.vMax - G.vMin);
                for (auto& orig : G.origin) {
                    if (dt != G.lastDt) s.move_key(s.primary, orig.t, orig.time + G.lastDt, orig.time + dt);
                    if (!s.speedGraph && std::abs(dy) > 0) s.set_key_value(s.primary, orig.t, orig.time + dt, orig.value + dv);
                }
                if (dt != G.lastDt) {
                    G.lastDt = dt;
                    s.selectedKeys.clear();
                    for (auto& orig : G.origin) s.selectedKeys.push_back({orig.t, orig.time + dt});
                }
                if (!G.origin.empty()) ImGui::SetTooltip("%s   %s", s.timecode(i64(G.origin.front().time + dt) + i64(toTimeline)).c_str(), format_value(G.origin.front().value + dv, 2).c_str());
                break;
            }
            case GraphState::Drag::HandleOut: case GraphState::Drag::HandleIn: {
                const float span = float(G.segEnd - G.segStart), dvSeg = G.segV1 - G.segV0;
                const float t = float(Fr(m.x) - toTimeline), v = V(m.y);
                const float bx = std::clamp((t - G.segStart) / std::max(1.f, span), 0.01f, .99f);
                const float by = std::abs(dvSeg) > 1e-6f ? (v - G.segV0) / dvSeg : (G.drag == GraphState::Drag::HandleOut ? 0.f : 1.f);
                float e[4] = {G.handleEase[0], G.handleEase[1], G.handleEase[2], G.handleEase[3]};
                if (G.drag == GraphState::Drag::HandleOut) { e[0] = bx; e[1] = by; } else { e[2] = bx; e[3] = by; }
                s.set_bezier(s.primary, G.handleTrack, G.handleOwner, Interpolation::Bezier, e[0], e[1], e[2], e[3]);
                ImGui::SetTooltip("Influência %.0f%%", double((G.drag == GraphState::Drag::HandleOut ? bx : 1 - bx) * 100));
                break;
            }
            default: break;
            }
        }
    }
    if (ImGui::BeginDragDropTargetCustom(ImRect(ImVec2(gx0, gy0), ImVec2(gx1, gy1)), ImGui::GetID("##gdrop"))) ImGui::EndDragDropTarget();

    // --- Barra de ferramentas inferior ---------------------------------------------
    dl->AddRectFilled(ImVec2(gx0, gy1), ImVec2(gx1, o.y + avail.y), col::Surface);
    ImGui::SetCursorScreenPos(ImVec2(gx0 + S(6), gy1 + (toolH - ImGui::GetFrameHeight()) * .5f));
    const float b = ImGui::GetFrameHeight();
    if (icon_button("value", Icon::Graph, "Gráfico de valor", !s.speedGraph, b)) s.speedGraph = false;
    ImGui::SameLine(0, S(2));
    if (icon_button("speed", Icon::Ease, "Gráfico de velocidade", s.speedGraph, b)) s.speedGraph = true;
    ImGui::SameLine(0, S(10));
    if (icon_button("fit", Icon::Grid, "Ajustar tudo à visualização", false, b)) { G.autoFit = true; G.viewStart = s.detail.startFrame; G.viewFrames = std::max(10, s.detail.endFrame - s.detail.startFrame); }
    ImGui::SameLine(0, S(2));
    if (icon_button("auto", Icon::Magnet, "Zoom automático da altura do gráfico", G.autoFit, b)) G.autoFit = !G.autoFit;
    ImGui::SameLine(0, S(14));
    const bool any = !s.selectedKeys.empty();
    if (icon_button("hold", Icon::Hold, "Converter em Manter", false, b, any)) s.apply_ease(4);
    ImGui::SameLine(0, S(2));
    if (icon_button("linear", Icon::Linear, "Converter em Linear", false, b, any)) s.apply_ease(3);
    ImGui::SameLine(0, S(2));
    if (icon_button("easy", Icon::Ease, "Suavização fácil (F9)", false, b, any)) s.apply_ease(0);
    ImGui::SameLine(0, S(2));
    ImGui::BeginDisabled(!any);
    if (ImGui::Button("Entrada")) s.apply_ease(1);
    tooltip("Suavização fácil de entrada (Shift+F9)");
    ImGui::SameLine(0, S(2));
    if (ImGui::Button("Saída")) s.apply_ease(2);
    tooltip("Suavização fácil de saída (Ctrl+Shift+F9)");
    ImGui::SameLine(0, S(10));
    if (ImGui::Button("Quicar")) s.apply_ease(5);
    ImGui::SameLine(0, S(2));
    if (ImGui::Button("Elástico")) s.apply_ease(6);
    ImGui::SameLine(0, S(2));
    if (ImGui::Button("Ultrapassar")) s.apply_ease(7);
    ImGui::EndDisabled();
    if (any) {
        ImGui::SameLine(0, S(14));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%zu keyframe%s selecionado%s", s.selectedKeys.size(), s.selectedKeys.size() == 1 ? "" : "s", s.selectedKeys.size() == 1 ? "" : "s");
    }
    ImGui::End();
}

} // namespace aurea::studio
