// Controles de efeito: propriedades fixas do clipe (Movimento, Opacidade,
// Tempo, Áudio, Texto, Forma) e os efeitos aplicados, cada linha com cronômetro
// de animação, valor arrastável, navegação de keyframes e a mini linha do tempo
// do clipe à direita, onde os keyframes são escolhidos e arrastados.
#include "Studio.hpp"
#include "imgui_internal.h"
#include <commdlg.h>
#include <cmath>
#include <cstdio>
#include <set>

namespace aurea::studio {
namespace {

const char* kBlendNames[] = {"Normal", "Adição", "Subtração", "Multiplicação", "Tela", "Sobrepor", "Escurecer", "Clarear",
                             "Subexposição de cores", "Superexposição de cores", "Luz direta", "Luz indireta", "Diferença", "Exclusão",
                             "Matiz", "Saturação", "Cor", "Luminosidade", "Dividir", "Luz vívida", "Subexposição linear",
                             "Superexposição linear", "Máscara", "Excluir", "Cor mais escura", "Cor mais clara", "Luz linear",
                             "Luz do pino", "Sobreposição suave"};

struct Lane {
    float x0 = 0, x1 = 0;
    i64 start = 0, end = 1;   ///< faixa do clipe na linha do tempo
    i64 offset = 0;
    [[nodiscard]] float x(double timelineFrame) const { return x0 + float((timelineFrame - start) / double(std::max<i64>(1, end - start))) * (x1 - x0); }
    [[nodiscard]] double frame(float px) const { return start + (px - x0) / std::max(1.f, x1 - x0) * double(end - start); }
};

struct EcState {
    float split = .58f;
    bool dragSplit = false;
    // Arrasto de keyframes na mini linha do tempo.
    bool dragging = false;
    float downX = 0;
    i32 lastDelta = 0;
    std::vector<std::pair<TrackKey, i32>> origin;
    char textBuffer[4096]{};
    u64 textLayer = 0;
    u32 textRevision = ~0u;
    int curveChannel = 0;
    int curvePoint = -1;
} E;

Lane gLane;
float gValueX = 0, gNavX = 0;

bool is_open(const char* key, bool initial = true) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    return st->GetBool(ImGui::GetID(key), initial);
}
void set_open(const char* key, bool v) { ImGui::GetStateStorage()->SetBool(ImGui::GetID(key), v); }

std::vector<i32> key_times(Studio& s, const std::vector<TrackKey>& tracks) {
    std::set<i32> t;
    for (auto& k : s.layer_keys(s.primary))
        for (auto& tr : tracks)
            if (k.property == tr.property && k.effectIndex == tr.effect && k.paramIndex == tr.param) t.insert(k.time);
    return {t.begin(), t.end()};
}

bool key_selected(Studio& s, const TrackKey& t, i32 time) {
    for (auto& [k, tt] : s.selectedKeys) if (k == t && tt == time) return true;
    return false;
}

void interpolation_menu(Studio& s) {
    if (ImGui::MenuItem("Linear")) s.apply_ease(3);
    if (ImGui::MenuItem("Bezier — Suavização fácil", "F9")) s.apply_ease(0);
    if (ImGui::MenuItem("Suavização fácil de entrada", "Shift+F9")) s.apply_ease(1);
    if (ImGui::MenuItem("Suavização fácil de saída", "Ctrl+Shift+F9")) s.apply_ease(2);
    if (ImGui::MenuItem("Manter")) s.apply_ease(4);
    ImGui::Separator();
    if (ImGui::MenuItem("Quicar")) s.apply_ease(5);
    if (ImGui::MenuItem("Elástico")) s.apply_ease(6);
    if (ImGui::MenuItem("Ultrapassar")) s.apply_ease(7);
    ImGui::Separator();
    if (ImGui::MenuItem("Excluir", "Del")) { s.delete_keys(s.primary, s.selectedKeys); s.selectedKeys.clear(); }
}

// Mini linha do tempo de uma linha: losangos, seleção e arrasto.
void lane_row(Studio& s, const char* id, float y0, float y1, const std::vector<TrackKey>& tracks) {
    if (tracks.empty() || gLane.x1 <= gLane.x0) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 back = ImGui::GetCursorScreenPos();
    const float cy = (y0 + y1) * .5f;
    dl->AddLine(ImVec2(gLane.x0, y1 - 1), ImVec2(gLane.x1, y1 - 1), col::alpha(col::Border, .5f));
    const auto times = key_times(s, tracks);
    ImGui::SetCursorScreenPos(ImVec2(gLane.x0, y0));
    ImGui::PushID(id);
    ImGui::InvisibleButton("lane", ImVec2(gLane.x1 - gLane.x0, std::max(1.f, y1 - y0)), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const ImVec2 m = ImGui::GetIO().MousePos;
    int hovered = -1;
    for (size_t i = 0; i < times.size(); ++i) {
        const float x = gLane.x(double(times[i]) + gLane.start - gLane.offset);
        if (ImGui::IsItemHovered() && std::abs(m.x - x) < S(6)) hovered = int(i);
    }
    for (size_t i = 0; i < times.size(); ++i) {
        const float x = gLane.x(double(times[i]) + gLane.start - gLane.offset);
        if (x < gLane.x0 - S(6) || x > gLane.x1 + S(6)) continue;
        const bool sel = key_selected(s, tracks.front(), times[i]);
        const bool here = times[i] == s.detail.localPlayhead;
        draw_diamond(dl, ImVec2(x, cy), S(5), sel ? col::Warning : col::Keyframe, sel ? col::Text : here ? col::Text : col::Stage);
        if (int(i) == hovered) dl->AddCircle(ImVec2(x, cy), S(8), col::alpha(col::Text, .4f));
    }
    if (ImGui::IsItemClicked(0) || ImGui::IsItemClicked(1)) {
        if (hovered >= 0) {
            const i32 t = times[size_t(hovered)];
            const bool add = ImGui::GetIO().KeyShift || ImGui::GetIO().KeyCtrl;
            if (!key_selected(s, tracks.front(), t)) {
                if (!add) s.selectedKeys.clear();
                for (auto& tr : tracks) s.selectedKeys.push_back({tr, t});
            }
            if (ImGui::IsMouseDoubleClicked(0)) s.seek(i64(t) + gLane.start - gLane.offset);
            else if (ImGui::IsItemClicked(0)) { E.dragging = true; E.downX = m.x; E.lastDelta = 0; E.origin = s.selectedKeys; s.begin_group(); }
            if (ImGui::IsItemClicked(1)) ImGui::OpenPopup("##keymenu");
        } else if (ImGui::IsItemClicked(0)) {
            s.selectedKeys.clear();
            s.seek(i64(std::llround(gLane.frame(m.x))));
        }
    }
    if (ImGui::BeginPopup("##keymenu")) { interpolation_menu(s); ImGui::EndPopup(); }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(back);
}

// Linha de propriedade: cronômetro, rótulo, valores, navegação e a faixa de keyframes.
template <typename Values>
void property_row(Studio& s, const char* label, const std::vector<TrackKey>& tracks, bool animated, bool keyHere, bool animatable,
                  Values&& draw_values, const std::vector<float>& current, float indent = 0) {
    ImGui::PushID(label);
    const float y0 = ImGui::GetCursorScreenPos().y;
    const float left = ImGui::GetCursorScreenPos().x;
    ImGui::SetCursorScreenPos(ImVec2(left + S(20) + indent, y0));
    if (animatable) {
        if (stopwatch("sw", animated)) s.toggle_track_animation(s.primary, tracks, current);
    } else ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
    ImGui::SameLine(0, S(4));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Muted), "%s", label);
    ImGui::SameLine(std::max(gValueX - left, ImGui::GetCursorPosX() + S(8)));
    draw_values();
    if (animated && animatable) {
        ImGui::SameLine(gNavX - left);
        bool prev = false, next = false;
        if (key_navigator("nav", keyHere, &prev, &next)) s.toggle_key_here(s.primary, tracks, current);
        if (prev || next) {
            const auto times = key_times(s, tracks);
            const i32 here = s.detail.localPlayhead;
            i32 target = here;
            if (prev) for (auto it = times.rbegin(); it != times.rend(); ++it) if (*it < here) { target = *it; break; }
            if (next) for (i32 t : times) if (t > here) { target = t; break; }
            if (target != here) s.seek(i64(target) + gLane.start - gLane.offset);
        }
    } else ImGui::NewLine();
    const float y1 = ImGui::GetCursorScreenPos().y;
    lane_row(s, "k", y0, y1, tracks);
    ImGui::PopID();
}

bool fx_header(Studio& s, const char* key, const char* label, bool* enabled, bool hasReset, bool* reset, ImU32 band = col::SurfaceHigh) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight() + S(2);
    dl->AddRectFilled(a, ImVec2(gLane.x0 - S(2), a.y + h), band);
    bool open = is_open(key);
    ImGui::PushID(key);
    ImGui::SetCursorScreenPos(ImVec2(a.x + S(2), a.y + S(1)));
    if (twirl("tw", &open)) set_open(key, open);
    ImGui::SameLine(0, S(2));
    if (enabled) {
        const bool on = *enabled;
        ImGui::InvisibleButton("fxon", ImVec2(S(22), ImGui::GetFrameHeight()));
        const ImVec2 b = ImGui::GetItemRectMin();
        dl->AddRect(ImVec2(b.x + S(2), b.y + S(4)), ImVec2(b.x + S(20), b.y + ImGui::GetFrameHeight() - S(4)), on ? col::Accent : col::Subtle, S(2));
        draw_icon(dl, Icon::Fx, ImVec2(b.x + S(11), b.y + ImGui::GetFrameHeight() * .5f), S(13), on ? col::Accent : col::Subtle);
        if (ImGui::IsItemClicked()) *enabled = !on;
        tooltip(on ? "Desativar o efeito" : "Ativar o efeito");
        ImGui::SameLine(0, S(6));
    }
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(gFontBold, gFontBold->LegacySize);
    ImGui::TextUnformatted(label);
    ImGui::PopFont();
    if (ImGui::IsItemClicked()) { open = !open; set_open(key, open); }
    if (hasReset) {
        ImGui::SameLine(gLane.x0 - a.x - S(28));
        *reset = icon_button("reset", Icon::Reset, "Redefinir", false, ImGui::GetFrameHeight());
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h + S(1)));
    (void)s;
    return open;
}

std::string rotation_text(float deg) {
    const int turns = int(deg / 360.f);
    const float rest = deg - turns * 360.f;
    char b[48];
    std::snprintf(b, sizeof b, "%dx%+.1f°", turns, double(rest));
    std::string t = b;
    for (char& c : t) if (c == '.') c = ',';
    return t;
}

void transform_section(Studio& s) {
    bool reset = false;
    if (!fx_header(s, "motion", "Movimento", nullptr, true, &reset)) return;
    const auto& d = s.detail;
    const bool threeD = d.flags & bridge::kLayerRowFlagThreeD;
    const u32 am = d.animatedMask, kh = d.keyAtPlayheadMask;
    auto anim = [&](std::initializer_list<u32> p) { for (u32 q : p) if (am & (1u << q)) return true; return false; };
    auto here = [&](std::initializer_list<u32> p) { for (u32 q : p) if (kh & (1u << q)) return true; return false; };
    auto tk = [](std::initializer_list<u32> p) { std::vector<TrackKey> v; for (u32 q : p) v.push_back({q, kInvalidIndex, 0}); return v; };
    if (reset) {
        s.begin_group();
        for (u32 p : {0u, 1u, 2u, 6u, 7u, 8u, 9u, 10u, 11u}) s.set_transform(p, 0);
        for (u32 p : {3u, 4u, 5u}) s.set_transform(p, 1);
        s.end_group();
    }
    // Posição
    {
        std::vector<u32> props = threeD ? std::vector<u32>{0, 1, 2} : std::vector<u32>{0, 1};
        std::vector<float> cur;
        for (u32 p : props) cur.push_back(s.transform_value(p));
        auto tracks = threeD ? tk({0, 1, 2}) : tk({0, 1});
        property_row(s, "Posição", tracks, anim({0, 1, 2}), here({0, 1, 2}), true, [&] {
            for (size_t i = 0; i < props.size(); ++i) {
                float v = cur[i];
                if (i) ImGui::SameLine(0, S(10));
                char id[8];
                std::snprintf(id, sizeof id, "p%zu", i);
                if (scrub_float(id, &v, 1.f, 0, 0, "%.1f")) s.set_transform(props[i], v);
            }
        }, cur);
    }
    // Escala
    if (s.uniformScale) {
        std::vector<float> cur{s.transform_value(3), s.transform_value(4), s.transform_value(5)};
        property_row(s, "Escala", tk({3, 4, 5}), anim({3, 4, 5}), here({3, 4, 5}), true, [&] {
            float v = cur[0];
            if (scrub_float("sc", &v, .005f, 0, 0, "%.1f", col::Accent, 100.f)) {
                const float ratio = std::abs(cur[0]) > 1e-6f ? v / cur[0] : 1.f;
                s.begin_group();
                s.set_transform(3, v);
                s.set_transform(4, std::abs(cur[0]) > 1e-6f ? cur[1] * ratio : v);
                if (threeD) s.set_transform(5, std::abs(cur[0]) > 1e-6f ? cur[2] * ratio : v);
                s.end_group();
            }
        }, cur);
    } else {
        for (u32 p : {3u, 4u}) {
            std::vector<float> cur{s.transform_value(p)};
            property_row(s, p == 3 ? "Escala da largura" : "Escala da altura", tk({p}), anim({p}), here({p}), true, [&] {
                float v = cur[0];
                if (scrub_float("s", &v, .005f, 0, 0, "%.1f", col::Accent, 100.f)) s.set_transform(p, v);
            }, cur);
        }
    }
    {
        const float h0 = ImGui::GetCursorScreenPos().x;
        ImGui::SetCursorScreenPos(ImVec2(gValueX, ImGui::GetCursorScreenPos().y));
        ImGui::Checkbox("Escala uniforme", &s.uniformScale);
        ImGui::SetCursorScreenPos(ImVec2(h0, ImGui::GetCursorScreenPos().y));
    }
    // Rotação
    for (u32 p : threeD ? std::vector<u32>{6, 7, 8} : std::vector<u32>{8}) {
        std::vector<float> cur{s.transform_value(p)};
        const char* name = p == 6 ? "Rotação X" : p == 7 ? "Rotação Y" : threeD ? "Rotação Z" : "Rotação";
        property_row(s, name, tk({p}), anim({p}), here({p}), true, [&] {
            float v = cur[0];
            ImGui::PushID(int(p));
            const std::string t = rotation_text(v);
            if (scrub_float("r", &v, .5f, 0, 0, "%.1f°")) s.set_transform(p, v);
            ImGui::SameLine(0, S(8));
            ImGui::TextDisabled("%s", t.c_str());
            ImGui::PopID();
        }, cur);
    }
    // Ponto de ancoragem
    {
        std::vector<float> cur{s.transform_value(9), s.transform_value(10)};
        property_row(s, "Ponto de ancoragem", tk({9, 10}), anim({9, 10}), here({9, 10}), true, [&] {
            for (int i = 0; i < 2; ++i) {
                float v = cur[i];
                if (i) ImGui::SameLine(0, S(10));
                if (scrub_float(i ? "ay" : "ax", &v, 1.f, 0, 0, "%.1f")) s.set_transform(9 + i, v);
            }
        }, cur);
    }
    {
        std::vector<float> cur{s.transform_value(13)};
        property_row(s, "Inclinação", tk({13}), anim({13}), here({13}), true, [&] {
            float v = cur[0];
            if (scrub_float("sk", &v, .25f, -85, 85, "%.1f°")) s.set_transform(13, v);
        }, cur);
    }
}

void opacity_section(Studio& s) {
    bool reset = false;
    if (!fx_header(s, "opacity", "Opacidade", nullptr, true, &reset)) return;
    if (reset) s.set_transform(12, 1);
    const auto& d = s.detail;
    std::vector<float> cur{d.opacity};
    property_row(s, "Opacidade", {{12, kInvalidIndex, 0}}, d.animatedMask & (1u << 12), d.keyAtPlayheadMask & (1u << 12), true, [&] {
        float v = d.opacity;
        if (scrub_float("op", &v, .005f, 0, 1, "%.1f %%", col::Accent, 100.f)) s.set_transform(12, v);
    }, cur);
    property_row(s, "Modo de mesclagem", {}, false, false, false, [&] {
        ImGui::SetNextItemWidth(S(150));
        const int current = std::clamp<int>(int(d.blendMode), 0, int(std::size(kBlendNames)) - 1);
        if (ImGui::BeginCombo("##blend", kBlendNames[current])) {
            for (int i = 0; i < int(std::size(kBlendNames)); ++i) {
                if (i == 1 || i == 6 || i == 8 || i == 12 || i == 14 || i == 22) ImGui::Separator();
                if (ImGui::Selectable(kBlendNames[i], i == current)) {
                    Command c; c.type = CommandType::LayerSetBlendMode; c.layer_blend = {LayerId::unpack(s.primary), BlendMode(i)}; s.send(c);
                }
            }
            ImGui::EndCombo();
        }
    }, {});
}

void time_section(Studio& s) {
    const auto kind = LayerKind(s.detail.kind);
    if (kind != LayerKind::Video && kind != LayerKind::Audio && kind != LayerKind::Composition) return;
    bool reset = false;
    if (!fx_header(s, "time", "Remapeamento de tempo", nullptr, true, &reset)) return;
    if (reset) { Command c; c.type = CommandType::LayerSetSpeed; c.audio_gain = {LayerId::unpack(s.primary), 1.f}; s.send(c); }
    property_row(s, "Velocidade", {{u32(TrackProperty::Speed), kInvalidIndex, 0}}, s.detail.timeFlags & (1u << 6), false, false, [&] {
        float v = s.detail.speed;
        if (scrub_float("sp", &v, .005f, 0.05f, 16.f, "%.1f %%", col::Accent, 100.f)) {
            Command c; c.type = CommandType::LayerSetSpeed; c.audio_gain = {LayerId::unpack(s.primary), v}; s.send(c);
            s.detailFrame = -1;
        }
        ImGui::SameLine(0, S(12));
        bool reversed = s.detail.timeFlags & 1u;
        if (ImGui::Checkbox("Inverter", &reversed)) { Command c; c.type = CommandType::LayerSetReversed; c.audio_flag = {LayerId::unpack(s.primary), reversed}; s.send(c); }
    }, {});
}

void audio_section(Studio& s) {
    const auto& d = s.detail;
    if (!(d.audioFlags & bridge::kAudioFlagHasAudio) && d.kind != u32(LayerKind::Audio)) return;
    bool reset = false;
    if (!fx_header(s, "audio", "Volume e panorâmica", nullptr, true, &reset)) return;
    if (reset) {
        s.begin_group();
        Command c; c.type = CommandType::AudioSetVolume; c.audio_gain = {LayerId::unpack(s.primary), 1.f}; s.send(c);
        Command p; p.type = CommandType::AudioSetPan; p.audio_gain = {LayerId::unpack(s.primary), 0.f}; s.send(p);
        s.end_group();
    }
    const u32 vol = u32(TrackProperty::AudioVolume);
    const bool animated = d.audioFlags & bridge::kAudioFlagVolumeAnimated;
    std::vector<float> cur{d.audioVolume};
    const auto times = key_times(s, {{vol, kInvalidIndex, 0}});
    const bool keyHere = std::find(times.begin(), times.end(), d.localPlayhead) != times.end();
    property_row(s, "Nível", {{vol, kInvalidIndex, 0}}, animated, keyHere, true, [&] {
        float db = d.audioVolume > 1e-5f ? 20.f * std::log10(d.audioVolume) : -96.f;
        if (scrub_float("db", &db, .1f, -96.f, 15.f, "%.1f dB")) {
            const float linear = db <= -95.9f ? 0.f : std::pow(10.f, db / 20.f);
            if (animated || s.autoKey) {
                Command c; c.type = CommandType::KeyframeInsert;
                c.keyframe = {TrackRef{LayerId::unpack(s.primary), TrackProperty::AudioVolume, kInvalidIndex, 0}, FrameIndex{d.localPlayhead}, linear, 0};
                s.send(c);
            } else { Command c; c.type = CommandType::AudioSetVolume; c.audio_gain = {LayerId::unpack(s.primary), linear}; s.send(c); }
            s.detailFrame = -1;
        }
    }, cur);
    property_row(s, "Panorâmica", {}, false, false, false, [&] {
        float pan = d.audioPan;
        if (scrub_float("pan", &pan, .005f, -1, 1, "%.0f", col::Accent, 100.f)) {
            Command c; c.type = CommandType::AudioSetPan; c.audio_gain = {LayerId::unpack(s.primary), pan}; s.send(c);
            s.detailFrame = -1;
        }
        ImGui::SameLine(0, S(12));
        bool muted = d.audioFlags & bridge::kAudioFlagMuted;
        if (ImGui::Checkbox("Sem som", &muted)) { Command c; c.type = CommandType::AudioSetMuted; c.audio_flag = {LayerId::unpack(s.primary), muted}; s.send(c); }
    }, {});
}

bool color_button(const char* id, float rgba[4], bool linear) {
    float shown[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    if (linear) for (int i = 0; i < 3; ++i) shown[i] = linear_to_srgb(rgba[i]);
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::ColorButton("##sw", ImVec4(shown[0], shown[1], shown[2], 1), ImGuiColorEditFlags_NoTooltip, ImVec2(S(34), ImGui::GetFrameHeight() - S(4))))
        ImGui::OpenPopup("##picker");
    if (ImGui::BeginPopup("##picker")) {
        if (ImGui::IsWindowAppearing() && gStudio) gStudio->begin_group();
        if (ImGui::ColorPicker4("##p", shown, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_DisplayHex)) {
            for (int i = 0; i < 4; ++i) rgba[i] = linear && i < 3 ? srgb_to_linear(shown[i]) : shown[i];
            changed = true;
        }
        ImGui::EndPopup();
    } else if (gStudio && ImGui::GetStateStorage()->GetBool(ImGui::GetID("open"))) {
        gStudio->end_group();
    }
    ImGui::GetStateStorage()->SetBool(ImGui::GetID("open"), ImGui::IsPopupOpen("##picker"));
    ImGui::PopID();
    return changed;
}

void text_section(Studio& s) {
    if (!s.hasText) return;
    bool reset = false;
    if (!fx_header(s, "text", "Texto", nullptr, false, &reset)) return;
    if (E.textLayer != s.primary || E.textRevision != s.revision) {
        if (!ImGui::IsAnyItemActive()) {
            std::snprintf(E.textBuffer, sizeof E.textBuffer, "%s", s.text.content.c_str());
            E.textLayer = s.primary;
            E.textRevision = s.revision;
        }
    }
    const float left = ImGui::GetCursorScreenPos().x;
    ImGui::SetCursorScreenPos(ImVec2(left + S(24), ImGui::GetCursorScreenPos().y));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Muted), "Texto de origem");
    ImGui::SetCursorScreenPos(ImVec2(left + S(24), ImGui::GetCursorScreenPos().y));
    ImGui::InputTextMultiline("##src", E.textBuffer, sizeof E.textBuffer, ImVec2(gLane.x0 - left - S(32), S(64)));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        Command c; c.type = CommandType::TextSetContent; c.layer_ref.layer = LayerId::unpack(s.primary); s.send(c, E.textBuffer);
    }
    property_row(s, "Tamanho da fonte", {}, false, false, false, [&] {
        float v = s.text.size;
        if (scrub_float("fs", &v, .5f, 1, 2000, "%.0f")) { Command c; c.type = CommandType::TextSetSize; c.text_size = {LayerId::unpack(s.primary), v}; s.send(c); s.text.size = v; }
    }, {});
    property_row(s, "Preenchimento", {}, false, false, false, [&] {
        float c4[4] = {s.text.color.x, s.text.color.y, s.text.color.z, s.text.color.w};
        if (color_button("fill", c4, false)) { Command c; c.type = CommandType::TextSetColor; c.text_color = {LayerId::unpack(s.primary), c4[0], c4[1], c4[2], c4[3]}; s.send(c); s.text.color = {c4[0], c4[1], c4[2], c4[3]}; }
    }, {});
    property_row(s, "Traçado", {}, false, false, false, [&] {
        float c4[4] = {s.text.strokeColor.x, s.text.strokeColor.y, s.text.strokeColor.z, s.text.strokeColor.w};
        if (color_button("stroke", c4, false)) { Command c; c.type = CommandType::TextSetStrokeColor; c.text_color = {LayerId::unpack(s.primary), c4[0], c4[1], c4[2], c4[3]}; s.send(c); s.text.strokeColor = {c4[0], c4[1], c4[2], c4[3]}; }
        ImGui::SameLine(0, S(10));
        float w = s.text.strokeWidth;
        if (scrub_float("sw", &w, .25f, 0, 200, "%.1f px")) { Command c; c.type = CommandType::TextSetStrokeWidth; c.text_stroke_width = {LayerId::unpack(s.primary), w}; s.send(c); s.text.strokeWidth = w; }
    }, {});
    property_row(s, "Alinhamento", {}, false, false, false, [&] {
        const char* names[] = {"Esquerda", "Centro", "Direita"};
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine(0, S(2));
            if (ImGui::SmallButton(names[i])) { Command c; c.type = CommandType::TextSetAlignment; c.text_align = {LayerId::unpack(s.primary), u32(i)}; s.send(c); }
        }
    }, {});
    property_row(s, "Fonte", {}, false, false, false, [&] {
        static std::vector<text::FontEntry> fonts;
        if (fonts.empty()) fonts = s.engine.list_fonts();
        ImGui::SetNextItemWidth(S(170));
        if (ImGui::BeginCombo("##font", s.text.fontFamily.empty() ? "Padrão" : s.text.fontFamily.c_str(), ImGuiComboFlags_HeightLarge)) {
            std::string last;
            for (auto& f : fonts) {
                const std::string name = f.family + (f.style.empty() || f.style == "Regular" ? "" : " " + f.style);
                if (name == last) continue;
                last = name;
                if (ImGui::Selectable(name.c_str(), f.family == s.text.fontFamily && f.weight == s.text.fontWeight))
                    s.engine.set_text_font(s.primary, f.family, f.weight, f.italic, f.path), s.revision = ~0u;
            }
            ImGui::EndCombo();
        }
    }, {});
}

void shape_section(Studio& s) {
    if (s.detail.kind != u32(LayerKind::Shape)) return;
    bool reset = false;
    if (!fx_header(s, "shape", "Forma", nullptr, false, &reset)) return;
    auto unpack = [](u32 c, float out[4]) { for (int i = 0; i < 4; ++i) out[i] = ((c >> (i * 8)) & 255) / 255.f; };
    property_row(s, "Preenchimento", {}, false, false, false, [&] {
        float c4[4];
        unpack(s.detail.shapeFill, c4);
        if (color_button("fill", c4, false)) { Command c; c.type = CommandType::ShapeSetFill; c.text_color = {LayerId::unpack(s.primary), c4[0], c4[1], c4[2], c4[3]}; s.send(c); s.detailFrame = -1; }
    }, {});
    property_row(s, "Traçado", {}, false, false, false, [&] {
        float c4[4];
        unpack(s.detail.shapeStroke, c4);
        if (color_button("stroke", c4, false)) { Command c; c.type = CommandType::ShapeSetStroke; c.text_color = {LayerId::unpack(s.primary), c4[0], c4[1], c4[2], c4[3]}; s.send(c); s.detailFrame = -1; }
        ImGui::SameLine(0, S(10));
        float w = s.detail.shapeStrokeWidth;
        if (scrub_float("w", &w, .25f, 0, 500, "%.1f px")) { Command c; c.type = CommandType::ShapeSetParam; c.shape_param = {LayerId::unpack(s.primary), 4, w}; s.send(c); s.detailFrame = -1; }
    }, {});
    property_row(s, "Arredondamento", {}, false, false, false, [&] {
        float r = s.detail.shapeCorner;
        if (scrub_float("cr", &r, .5f, 0, 2000, "%.1f px")) { Command c; c.type = CommandType::ShapeSetParam; c.shape_param = {LayerId::unpack(s.primary), 1, r}; s.send(c); s.detailFrame = -1; }
    }, {});
}

// Editor de Curvas (tom): grade 4×4, canais RGB/R/G/B, pontos arrastáveis.
void curves_editor(Studio& s, const EffectView& fx, const bridge::EffectParamRow& p) {
    const float left = ImGui::GetCursorScreenPos().x;
    const float size = std::clamp(gLane.x0 - left - S(48), S(140), S(300));
    ImGui::SetCursorScreenPos(ImVec2(left + S(24), ImGui::GetCursorScreenPos().y));
    ImGui::PushID(int(p.index));
    const char* channels[] = {"RGB", "Vermelho", "Verde", "Azul"};
    const ImU32 colors[] = {col::Text, col::AxisX, col::AxisY, col::AxisZ};
    ImGui::SetNextItemWidth(S(110));
    ImGui::Combo("##ch", &E.curveChannel, channels, 4);
    ImGui::SameLine();
    if (ImGui::SmallButton("Redefinir canal")) s.engine.edit_effect_curve(s.primary, fx.row.effectId, p.index, u32(E.curveChannel), 3, 0, 0, 0), s.engine.request_render();
    ImGui::SetCursorScreenPos(ImVec2(left + S(24), ImGui::GetCursorScreenPos().y));
    const ImVec2 a = ImGui::GetCursorScreenPos(), b(a.x + size, a.y + size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, col::Stage);
    for (int i = 1; i < 4; ++i) {
        dl->AddLine(ImVec2(a.x + size * i / 4, a.y), ImVec2(a.x + size * i / 4, b.y), col::CurveGrid);
        dl->AddLine(ImVec2(a.x, a.y + size * i / 4), ImVec2(b.x, a.y + size * i / 4), col::CurveGrid);
    }
    dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), col::alpha(col::CurveGrid, .6f));
    dl->AddRect(a, b, col::Border);
    const u32 ch = u32(E.curveChannel);
    float samples[256], pts[64];
    const u32 ns = s.engine.query_effect_curve(s.primary, fx.row.effectId, p.index, ch, true, samples, 256);
    const u32 np = s.engine.query_effect_curve(s.primary, fx.row.effectId, p.index, ch, false, pts, 64) / 2;
    auto P = [&](float x, float y) { return ImVec2(a.x + x * size, b.y - std::clamp(y, 0.f, 1.f) * size); };
    for (u32 i = 1; i < ns; ++i) dl->AddLine(P(float(i - 1) / (ns - 1), samples[i - 1]), P(float(i) / (ns - 1), samples[i]), colors[ch], S(1.6f));
    ImGui::InvisibleButton("##curve", ImVec2(size, size));
    const ImVec2 m = ImGui::GetIO().MousePos;
    const float mx = std::clamp((m.x - a.x) / size, 0.f, 1.f), my = std::clamp((b.y - m.y) / size, 0.f, 1.f);
    int hovered = -1;
    for (u32 i = 0; i < np; ++i) {
        const ImVec2 q = P(pts[i * 2], pts[i * 2 + 1]);
        if (ImGui::IsItemHovered() && std::abs(q.x - m.x) < S(7) && std::abs(q.y - m.y) < S(7)) hovered = int(i);
        dl->AddRectFilled(ImVec2(q.x - S(3.5f), q.y - S(3.5f)), ImVec2(q.x + S(3.5f), q.y + S(3.5f)), int(i) == E.curvePoint || int(i) == hovered ? col::Warning : colors[ch]);
    }
    if (ImGui::IsItemActivated()) {
        s.begin_group();
        E.curvePoint = hovered >= 0 ? hovered : s.engine.edit_effect_curve(s.primary, fx.row.effectId, p.index, ch, 1, 0, mx, my);
    }
    if (ImGui::IsItemActive() && E.curvePoint >= 0) {
        const bool outside = (m.x < a.x - S(20) || m.x > b.x + S(20) || m.y < a.y - S(20) || m.y > b.y + S(20));
        if (outside && E.curvePoint > 0 && E.curvePoint + 1 < int(np)) {
            s.engine.edit_effect_curve(s.primary, fx.row.effectId, p.index, ch, 2, u32(E.curvePoint), 0, 0);
            E.curvePoint = -1;
        } else if (!outside) {
            const int moved = s.engine.edit_effect_curve(s.primary, fx.row.effectId, p.index, ch, 0, u32(E.curvePoint), mx, my);
            if (moved >= 0) E.curvePoint = moved;
        }
        s.engine.request_render();
    }
    if (ImGui::IsItemDeactivated()) { E.curvePoint = -1; s.end_group(); s.revision = ~0u; }
    if (hovered >= 0 && ImGui::IsMouseClicked(1)) { s.engine.edit_effect_curve(s.primary, fx.row.effectId, p.index, ch, 2, u32(hovered), 0, 0); s.engine.request_render(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Entrada %.0f  Saída %.0f\nClique para criar um ponto; arraste para fora ou clique direito para remover.", double(mx * 255), double(my * 255));
    ImGui::PopID();
}

void effect_section(Studio& s, EffectView& fx, int index) {
    char key[48];
    std::snprintf(key, sizeof key, "fx%u", fx.row.effectId);
    bool enabled = fx.row.enabled != 0, reset = false;
    const std::string title = fx.row.known ? fx.name : fx.name + " (desconhecido)";
    const bool open = fx_header(s, key, title.c_str(), &enabled, true, &reset);
    if (enabled != (fx.row.enabled != 0)) {
        Command c; c.type = CommandType::EffectSetEnabled; c.effect_enabled = {LayerId::unpack(s.primary), EffectId{fx.row.effectId, 0}, enabled}; s.send(c);
    }
    // Menu do efeito no título.
    {
        const ImVec2 back = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(back.x, back.y - ImGui::GetFrameHeight() - S(3)));
        ImGui::InvisibleButton("##fxctx", ImVec2(std::max(1.f, gLane.x0 - back.x - S(32)), ImGui::GetFrameHeight()));
        if (ImGui::BeginPopupContextItem(key)) {
            if (ImGui::MenuItem("Remover efeito")) { Command c; c.type = CommandType::EffectRemove; c.effect_ref = {LayerId::unpack(s.primary), EffectId{fx.row.effectId, 0}}; s.send(c); }
            if (index > 0 && ImGui::MenuItem("Mover para cima")) { Command c; c.type = CommandType::EffectReorder; c.effect_reorder = {LayerId::unpack(s.primary), EffectId{fx.row.effectId, 0}, u32(index - 1)}; s.send(c); }
            if (index + 1 < int(s.effects.size()) && ImGui::MenuItem("Mover para baixo")) { Command c; c.type = CommandType::EffectReorder; c.effect_reorder = {LayerId::unpack(s.primary), EffectId{fx.row.effectId, 0}, u32(index + 1)}; s.send(c); }
            const auto presets = s.engine.effect_presets(fx.row.typeId);
            if (!presets.empty() && ImGui::BeginMenu("Predefinições")) {
                for (u32 i = 0; i < presets.size(); ++i)
                    if (ImGui::MenuItem(presets[i].first.c_str())) { s.engine.apply_effect_preset(s.primary, fx.row.effectId, i); s.revision = ~0u; }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(back);
    }
    if (reset) {
        s.begin_group();
        for (auto& p : fx.params) {
            if (p.type == u32(ParamType::Curve) || p.type == u32(ParamType::Gradient) || p.type >= u32(ParamType::LayerReference)) continue;
            const u32 comps = p.type == u32(ParamType::Point2D) ? 2 : p.type == u32(ParamType::Point3D) ? 3 : p.type == u32(ParamType::Color) ? 4 : 1;
            if (comps > 1) { Command c; c.type = CommandType::EffectSetColorParam; c.effect_color = {LayerId::unpack(s.primary), EffectId{fx.row.effectId, 0}, p.index, p.defaultValue[0], p.defaultValue[1], p.defaultValue[2], p.defaultValue[3]}; s.send(c); }
            else { Command c; c.type = CommandType::EffectSetParam; c.effect_param = {LayerId::unpack(s.primary), EffectId{fx.row.effectId, 0}, p.index, p.defaultValue[0]}; s.send(c); }
        }
        s.end_group();
    }
    if (!open) return;
    ImGui::PushID(key);
    u32 section = 0;
    bool sectionOpen = true;
    for (auto& p : fx.params) {
        if (p.flags & kParamHidden) continue;
        if (p.section != section) {
            section = p.section;
            if (section) {
                const std::string slabel = fx.text(p.sectionLabelOffset, p.sectionLabelLength);
                char skey[64];
                std::snprintf(skey, sizeof skey, "sec%u", section);
                sectionOpen = p.sectionFlags & bridge::kEffectSectionOpen;
                const ImVec2 a = ImGui::GetCursorScreenPos();
                ImGui::SetCursorScreenPos(ImVec2(a.x + S(20), a.y));
                ImGui::PushID(skey);
                bool o = sectionOpen;
                if (twirl("t", &o)) { s.engine.set_effect_section_open(s.primary, fx.row.effectId, section, o); s.revision = ~0u; }
                ImGui::SameLine(0, S(2));
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(slabel.empty() ? "Seção" : slabel.c_str());
                if (p.sectionToggle >= 0) {
                    for (auto& q : fx.params) if (int(q.index) == p.sectionToggle) {
                        ImGui::SameLine();
                        bool on = q.value[0] > .5f;
                        if (ImGui::Checkbox("##on", &on)) s.set_effect_value(fx, q, 0, on ? 1.f : 0.f);
                    }
                }
                ImGui::PopID();
                sectionOpen = o;
            } else sectionOpen = true;
        }
        if (!sectionOpen || int(p.index) == p.sectionToggle) continue;
        const std::string label = fx.text(p.labelOffset, p.labelLength), unit = fx.text(p.unitOffset, p.unitLength);
        const bool rowEnabled = p.rowFlags & bridge::kEffectRowEnabled;
        const bool animatable = (p.flags & kParamAnimatable) && p.type != u32(ParamType::Bool) && p.type != u32(ParamType::Enum) &&
                                p.type < u32(ParamType::Curve);
        const u32 comps = p.type == u32(ParamType::Point2D) ? 2 : p.type == u32(ParamType::Point3D) ? 3 : p.type == u32(ParamType::Color) ? 4 : 1;
        std::vector<TrackKey> tracks;
        std::vector<float> cur;
        for (u32 c = 0; c < comps; ++c) { tracks.push_back({u32(TrackProperty::EffectParam), fx.row.effectId, p.index * 4 + c}); cur.push_back(p.value[c]); }
        const auto times = key_times(s, tracks);
        const bool keyHere = std::find(times.begin(), times.end(), s.detail.localPlayhead) != times.end();
        const bool percent = p.flags & kParamPercent;
        const float scale = percent ? 100.f : 1.f;
        const float range = p.maxValue > p.minValue ? p.maxValue - p.minValue : 100.f;
        const float speed = p.step > 0 ? p.step : range / 300.f;
        const int decimals = p.type == u32(ParamType::Int) ? 0 : (p.rowFlags & bridge::kEffectRowStepDeclared) && p.step >= 1 ? 0 : range * scale >= 100 ? 1 : 2;
        char fmt[32];
        std::snprintf(fmt, sizeof fmt, "%%.%df%s%s", decimals, unit.empty() && !percent ? "" : " ", percent ? "%%" : unit.c_str());
        ImGui::PushID(int(p.index));
        ImGui::BeginDisabled(!rowEnabled);
        char twirlKey[32];
        std::snprintf(twirlKey, sizeof twirlKey, "slider%u", p.index);
        const bool canSlide = comps == 1 && p.maxValue > p.minValue && (p.type == u32(ParamType::Float) || p.type == u32(ParamType::Int));
        bool slider = is_open(twirlKey, false);
        const ImVec2 rowStart = ImGui::GetCursorScreenPos();
        if (canSlide) {
            ImGui::SetCursorScreenPos(ImVec2(rowStart.x + S(6), rowStart.y));
            if (twirl("st", &slider)) set_open(twirlKey, slider);
            ImGui::SetCursorScreenPos(rowStart);
        }
        property_row(s, label.c_str(), tracks, p.animated != 0, keyHere, animatable, [&] {
            switch (ParamType(p.type)) {
            case ParamType::Bool: {
                bool on = p.value[0] > .5f;
                if (ImGui::Checkbox("##b", &on)) s.set_effect_value(fx, p, 0, on ? 1.f : 0.f);
                break;
            }
            case ParamType::Enum: {
                std::vector<std::string> names;
                const std::string all = fx.text(p.enumOffset, p.enumLength);
                for (size_t at = 0; at <= all.size();) { size_t bar = all.find('|', at); if (bar == std::string::npos) bar = all.size(); names.push_back(all.substr(at, bar - at)); at = bar + 1; }
                const int current = std::clamp(int(std::lround(p.value[0])), 0, std::max(0, int(names.size()) - 1));
                ImGui::SetNextItemWidth(S(160));
                if (ImGui::BeginCombo("##e", names.empty() ? "" : names[size_t(current)].c_str())) {
                    for (int i = 0; i < int(names.size()); ++i) if (ImGui::Selectable(names[size_t(i)].c_str(), i == current)) s.set_effect_value(fx, p, 0, float(i));
                    ImGui::EndCombo();
                }
                break;
            }
            case ParamType::Color: {
                float c4[4] = {p.value[0], p.value[1], p.value[2], p.value[3]};
                if (color_button("c", c4, true)) s.set_effect_vector(fx, p, c4);
                break;
            }
            case ParamType::Point2D: case ParamType::Point3D: {
                const bool relative = p.flags & kParamRelative;
                for (u32 c = 0; c < comps; ++c) {
                    float v = p.value[c];
                    if (c) ImGui::SameLine(0, S(10));
                    char id[8];
                    std::snprintf(id, sizeof id, "v%u", c);
                    if (scrub_float(id, &v, relative ? .002f : 1.f, 0, 0, relative ? "%.1f%%" : "%.1f", col::Accent, relative ? 100.f : 1.f)) s.set_effect_value(fx, p, c, v);
                }
                ImGui::SameLine(0, S(6));
                if (icon_button("pick", Icon::Pick, "Escolher o ponto clicando na prévia", s.pick.effect == fx.row.effectId && s.pick.param == p.index, ImGui::GetFrameHeight()))
                    s.pick = {fx.row.effectId, p.index}, s.message("Clique na prévia do Programa para posicionar o ponto.");
                break;
            }
            case ParamType::Curve:
                ImGui::TextDisabled("Editor abaixo");
                break;
            case ParamType::LayerReference: {
                const int current = int(std::lround(p.value[0]));
                std::string name = "Nenhuma";
                for (auto& l : s.layers) if (int(LayerId::unpack(l.row.id).index) == current) name = l.name;
                ImGui::SetNextItemWidth(S(160));
                if (ImGui::BeginCombo("##lr", name.c_str())) {
                    if (ImGui::Selectable("Nenhuma", current < 0)) s.set_effect_value(fx, p, 0, -1.f);
                    for (auto& l : s.layers) if (l.row.id != s.primary && ImGui::Selectable(l.name.c_str(), int(LayerId::unpack(l.row.id).index) == current))
                        s.set_effect_value(fx, p, 0, float(LayerId::unpack(l.row.id).index));
                    ImGui::EndCombo();
                }
                break;
            }
            case ParamType::TextureReference: {
                const std::string img = s.engine.effect_image_name(s.primary, fx.row.effectId);
                if (ImGui::SmallButton(img.empty() ? "Escolher imagem…" : img.c_str())) {
                    wchar_t file[MAX_PATH]{};
                    OPENFILENAMEW ofn{sizeof(ofn)};
                    ofn.hwndOwner = s.window;
                    ofn.lpstrFilter = L"Imagens e LUTs\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp;*.cube\0\0";
                    ofn.lpstrFile = file;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameW(&ofn)) {
                        const std::string path = utf8(file);
                        const bool lut = path.size() > 5 && (path.substr(path.size() - 5) == ".cube" || path.substr(path.size() - 5) == ".CUBE");
                        s.error(lut ? s.engine.import_color_lut(s.primary, fx.row.effectId, path.c_str()) : s.engine.set_effect_image(s.primary, fx.row.effectId, path.c_str()));
                        s.revision = ~0u;
                    }
                }
                break;
            }
            case ParamType::Gradient:
                ImGui::TextDisabled("Gradiente");
                break;
            case ParamType::Angle: {
                float v = p.value[0];
                if (scrub_float("a", &v, .5f, 0, 0, "%.1f°")) s.set_effect_value(fx, p, 0, v);
                ImGui::SameLine(0, S(8));
                ImGui::TextDisabled("%s", rotation_text(v).c_str());
                break;
            }
            default: {
                float v = p.value[0];
                const float lo = p.hardMax > p.hardMin ? p.hardMin : 0, hi = p.hardMax > p.hardMin ? p.hardMax : 0;
                if (scrub_float("f", &v, speed, lo, hi, fmt, col::Accent, scale)) s.set_effect_value(fx, p, 0, p.type == u32(ParamType::Int) ? std::round(v) : v);
            }
            }
        }, cur);
        if (canSlide && slider) {
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(gValueX, at.y));
            float v = p.value[0];
            ImGui::SetNextItemWidth(std::max(S(80), gNavX - gValueX - S(8)));
            if (ImGui::SliderFloat("##sl", &v, p.minValue, p.maxValue, "")) s.set_effect_value(fx, p, 0, p.type == u32(ParamType::Int) ? std::round(v) : v);
            if (ImGui::IsItemActivated()) s.begin_group();
            if (ImGui::IsItemDeactivated()) s.end_group();
            ImGui::SetCursorScreenPos(ImVec2(at.x, ImGui::GetCursorScreenPos().y));
        }
        if (p.type == u32(ParamType::Curve)) curves_editor(s, fx, p);
        ImGui::EndDisabled();
        const std::string tip = fx.text(p.tipOffset, p.tipLength);
        (void)tip;
        ImGui::PopID();
    }
    ImGui::PopID();
}

} // namespace

void draw_effect_controls(Studio& s) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin("Controles de efeito", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) { ImGui::End(); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    LayerView* l = s.layer(s.primary);
    // Cabeçalho: origem e sequência.
    const float headH = ImGui::GetFrameHeight() + S(4);
    dl->AddRectFilled(o, ImVec2(o.x + avail.x, o.y + headH), col::Background);
    ImGui::SetCursorScreenPos(ImVec2(o.x + S(10), o.y + S(2)));
    ImGui::AlignTextToFramePadding();
    if (!l) {
        ImGui::TextDisabled("(nenhum clipe selecionado)");
        ImGui::SetCursorScreenPos(ImVec2(o.x + S(20), o.y + headH + S(30)));
        ImGui::PushTextWrapPos(o.x + avail.x - S(20));
        ImGui::TextDisabled("Selecione um clipe na linha do tempo para ver Movimento, Opacidade, Tempo e os efeitos aplicados. "
                            "Arraste efeitos do painel Efeitos para cá ou para o clipe.");
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Muted), "Principal *");
    ImGui::SameLine(0, S(4));
    ImGui::TextUnformatted(l->name.c_str());
    ImGui::SameLine(0, S(14));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Subtle), ">  %s * %s", s.engine.current_composition_name().c_str(), l->name.c_str());

    // Divisória arrastável entre propriedades e a mini linha do tempo.
    const float laneW = avail.x * (1 - E.split);
    gLane.x0 = o.x + avail.x * E.split;
    gLane.x1 = o.x + avail.x - S(12);
    gLane.start = s.detail.startFrame;
    gLane.end = std::max<i64>(s.detail.endFrame, s.detail.startFrame + 1);
    gLane.offset = s.detail.offsetFrames;
    gValueX = o.x + std::clamp(avail.x * E.split * .40f, S(130), S(200));
    gNavX = gLane.x0 - S(64);
    (void)laneW;
    // Régua da mini linha do tempo com a barra do clipe.
    const float rulerY0 = o.y + headH, rulerY1 = rulerY0 + S(26);
    dl->AddRectFilled(ImVec2(o.x, rulerY0), ImVec2(o.x + avail.x, rulerY1), col::Surface);
    dl->AddRectFilled(ImVec2(gLane.x0, rulerY0 + S(15)), ImVec2(gLane.x1, rulerY1 - S(2)), col::alpha(clip_color(*l), .85f), S(2));
    dl->AddText(gFontSmall, gFontSmall->LegacySize * s.dpi, ImVec2(gLane.x0 + S(4), rulerY0 + S(14)), col::Text, l->name.c_str());
    dl->AddText(gFontSmall, gFontSmall->LegacySize * s.dpi, ImVec2(gLane.x0 + S(2), rulerY0 + S(1)), col::Subtle, s.timecode(gLane.start).c_str());
    const std::string endTc = s.timecode(gLane.end);
    dl->AddText(gFontSmall, gFontSmall->LegacySize * s.dpi, ImVec2(gLane.x1 - ImGui::CalcTextSize(endTc.c_str()).x * .85f, rulerY0 + S(1)), col::Subtle, endTc.c_str());
    ImGui::SetCursorScreenPos(ImVec2(gLane.x0, rulerY0));
    ImGui::InvisibleButton("##ecruler", ImVec2(std::max(1.f, gLane.x1 - gLane.x0), rulerY1 - rulerY0));
    if (ImGui::IsItemActive()) s.seek(i64(std::llround(gLane.frame(ImGui::GetIO().MousePos.x))));
    // Divisória.
    ImGui::SetCursorScreenPos(ImVec2(gLane.x0 - S(3), rulerY0));
    ImGui::InvisibleButton("##split", ImVec2(S(6), avail.y - headH));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) E.split = std::clamp((ImGui::GetIO().MousePos.x - o.x) / avail.x, .35f, .85f);
    dl->AddLine(ImVec2(gLane.x0 - 1, rulerY0), ImVec2(gLane.x0 - 1, o.y + avail.y), col::Border);

    // Linhas, roláveis juntas (propriedades + keyframes).
    ImGui::SetCursorScreenPos(ImVec2(o.x, rulerY1));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(6), S(2)));
    ImGui::BeginChild("##ecrows", ImVec2(avail.x, o.y + avail.y - rulerY1), ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    ImDrawList* cl = ImGui::GetWindowDrawList();
    const ImVec2 childMin = ImGui::GetWindowPos();
    const float childBottom = childMin.y + ImGui::GetWindowHeight();
    cl->AddRectFilled(ImVec2(gLane.x0, childMin.y), ImVec2(gLane.x1 + S(12), childBottom + ImGui::GetScrollMaxY() + 4000), col::Stage);
    section_band("Efeitos de vídeo");
    transform_section(s);
    opacity_section(s);
    time_section(s);
    text_section(s);
    shape_section(s);
    for (int i = 0; i < int(s.effects.size()); ++i) effect_section(s, s.effects[size_t(i)], i);
    if (s.detail.audioFlags & bridge::kAudioFlagHasAudio || s.detail.kind == u32(LayerKind::Audio)) {
        section_band("Efeitos de áudio");
        audio_section(s);
    }
    ImGui::Dummy(ImVec2(1, S(40)));
    // Alvo de soltura para efeitos arrastados do navegador.
    if (ImGui::BeginDragDropTargetCustom(ImRect(childMin, ImVec2(childMin.x + avail.x, childBottom)), ImGui::GetID("##ecdrop"))) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("AUREA_FX")) s.apply_effect(*static_cast<const u32*>(p->Data));
        ImGui::EndDragDropTarget();
    }
    // Indicador de reprodução na mini linha do tempo.
    const float px = gLane.x(double(s.playhead()));
    if (px >= gLane.x0 && px <= gLane.x1) cl->AddLine(ImVec2(px, childMin.y), ImVec2(px, childBottom), col::Playhead);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    if (px >= gLane.x0 && px <= gLane.x1) {
        const ImVec2 head[3] = {{px - S(5), rulerY0 + S(2)}, {px + S(5), rulerY0 + S(2)}, {px, rulerY0 + S(10)}};
        dl->AddConvexPolyFilled(head, 3, col::Accent);
        dl->AddLine(ImVec2(px, rulerY0 + S(8)), ImVec2(px, rulerY1), col::Playhead);
    }
    // Arrasto de keyframes escolhidos (todas as linhas).
    if (E.dragging) {
        if (!ImGui::IsMouseDown(0)) { E.dragging = false; s.end_group(); }
        else {
            const double framesPerPx = double(gLane.end - gLane.start) / std::max(1.f, gLane.x1 - gLane.x0);
            const i32 delta = i32(std::llround((ImGui::GetIO().MousePos.x - E.downX) * framesPerPx));
            if (delta != E.lastDelta) {
                for (auto& [t, time] : E.origin) s.move_key(s.primary, t, time + E.lastDelta, time + delta);
                E.lastDelta = delta;
                s.selectedKeys = E.origin;
                for (auto& [t, time] : s.selectedKeys) time += delta;
            }
        }
    }
    ImGui::End();
}

void draw_audio_mixer(Studio& s) {
    if (!ImGui::Begin("Mixer de clipes de áudio")) { ImGui::End(); return; }
    std::vector<int> channels;
    for (auto& t : s.audioTracks) { for (int i : t.mirrors) channels.push_back(i); for (int i : t.layers) channels.push_back(i); }
    if (channels.empty()) { ImGui::TextDisabled("Nenhum clipe com áudio na sequência."); ImGui::End(); return; }
    const float stripW = S(86);
    for (size_t c = 0; c < channels.size(); ++c) {
        LayerView& l = s.layers[size_t(channels[c])];
        bridge::LayerDetailPOD d{};
        if (!s.engine.query_layer_detail(l.row.id, d)) continue;
        if (c) ImGui::SameLine();
        ImGui::PushID(int(c));
        ImGui::BeginGroup();
        ImGui::PushFont(gFontBold, gFontBold->LegacySize);
        ImGui::TextUnformatted(("A" + std::to_string(c + 1)).c_str());
        ImGui::PopFont();
        ImGui::SetNextItemWidth(stripW);
        float pan = d.audioPan * 100;
        if (ImGui::SliderFloat("##pan", &pan, -100, 100, pan < -.5f ? "E %.0f" : pan > .5f ? "D %.0f" : "C")) {
            Command cmd; cmd.type = CommandType::AudioSetPan; cmd.audio_gain = {LayerId::unpack(l.row.id), pan / 100}; s.send(cmd);
        }
        if (ImGui::IsItemActivated()) s.begin_group();
        if (ImGui::IsItemDeactivated()) s.end_group();
        float db = d.audioVolume > 1e-5f ? 20 * std::log10(d.audioVolume) : -60.f;
        if (ImGui::VSliderFloat("##vol", ImVec2(stripW, S(150)), &db, -60, 15, "%.1f dB")) {
            Command cmd; cmd.type = CommandType::AudioSetVolume; cmd.audio_gain = {LayerId::unpack(l.row.id), db <= -59.9f ? 0.f : std::pow(10.f, db / 20)}; s.send(cmd);
        }
        if (ImGui::IsItemActivated()) s.begin_group();
        if (ImGui::IsItemDeactivated()) s.end_group();
        const bool muted = d.audioFlags & bridge::kAudioFlagMuted, solo = d.audioFlags & bridge::kAudioFlagSolo;
        if (small_toggle("m", "M", muted, "Silenciar")) { Command cmd; cmd.type = CommandType::AudioSetMuted; cmd.audio_flag = {LayerId::unpack(l.row.id), !muted}; s.send(cmd); }
        ImGui::SameLine();
        if (small_toggle("s", "S", solo, "Solo")) { Command cmd; cmd.type = CommandType::AudioSetSolo; cmd.audio_flag = {LayerId::unpack(l.row.id), !solo}; s.send(cmd); }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + stripW);
        ImGui::TextDisabled("%s", l.name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::End();
}

} // namespace aurea::studio
