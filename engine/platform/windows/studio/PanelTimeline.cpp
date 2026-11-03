// Linha do tempo: faixas de vídeo (V1 embaixo) e de áudio, régua, indicador
// de reprodução, clipes com miniatura/forma de onda, aparar, mover, lâmina,
// seleção por retângulo, encaixe e a barra de rolagem que também dá zoom.
#include "Studio.hpp"
#include "imgui_internal.h"
#include <cmath>
#include <cstdio>
#include <set>

namespace aurea::studio {
namespace {

struct ClipHit {
    ImRect rect;
    int layer = -1;
    bool mirror = false;
};

enum class Drag { None, Scrub, Move, TrimIn, TrimOut, Marquee, Pan, ZoomThumb, ZoomLeft, ZoomRight, Playhead };

struct TimelineState {
    Drag drag = Drag::None;
    ImVec2 down{};
    double downScroll = 0, downPx = 0, downFrames = 0;
    float downTrackScroll = 0;
    i64 lastDelta = 0;
    bool moved = false;
    struct Origin { u64 id; i64 start, end, offset; };
    std::vector<Origin> origins;
    u64 dragLayer = 0;
    float snapX = -1;
    std::set<std::string> expanded;
    std::unordered_map<u64, std::vector<u8>> waves;
    u32 waveRevision = ~0u, waveGeneration = ~0u;
};
TimelineState T;

double nice_step(double minFrames, double fps) {
    const double f = std::max(1.0, std::round(fps));
    const double steps[] = {1, 2, 5, 10, f / 2, f, 2 * f, 5 * f, 10 * f, 15 * f, 30 * f, 60 * f, 120 * f, 300 * f, 600 * f, 1800 * f, 3600 * f};
    for (double st : steps) if (st >= minFrames && st >= 1) return st;
    return 7200 * f;
}

const std::vector<u8>& waveform(Studio& s, u64 layer, double firstFrame, double framesPerBucket, u32 count) {
    if (T.waveRevision != s.revision || T.waveGeneration != s.thumbGeneration || T.waves.size() > 512) {
        T.waves.clear();
        T.waveRevision = s.revision;
        T.waveGeneration = s.thumbGeneration;
    }
    const u64 key = layer * 0x9E3779B97F4A7C15ull ^ u64(std::llround(firstFrame * 4)) * 1315423911ull ^ u64(std::llround(framesPerBucket * 1000)) << 7 ^ count;
    auto it = T.waves.find(key);
    if (it != T.waves.end()) return it->second;
    std::vector<u8> out(count);
    const u32 n = s.engine.query_waveform(layer, firstFrame, framesPerBucket, count, out.data());
    out.resize(n);
    return T.waves.emplace(key, std::move(out)).first->second;
}

std::vector<i64> snap_targets(Studio& s) {
    std::vector<i64> t{0, s.playhead()};
    for (size_t i = 0; i + 2 < s.markers.size() + 2 && i < s.markers.size(); i += 3) t.push_back(s.markers[i]);
    for (auto& l : s.layers) {
        if (s.is_selected(l.row.id)) continue;
        t.push_back(l.row.startFrame);
        t.push_back(l.row.endFrame);
    }
    return t;
}

void clip_menu(Studio& s, LayerView& l) {
    const u64 id = l.row.id;
    if (ImGui::MenuItem(l.visible() ? "Desativar" : "Ativar", "Shift+E")) {
        Command c; c.type = CommandType::LayerSetVisible; c.layer_visible = {LayerId::unpack(id), !l.visible()}; s.send(c);
    }
    if (ImGui::MenuItem(l.locked() ? "Desbloquear" : "Bloquear")) {
        Command c; c.type = CommandType::LayerSetLocked; c.layer_locked = {LayerId::unpack(id), !l.locked()}; s.send(c);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Adicionar edição no indicador", "Ctrl+K")) s.split_at_playhead();
    if (ImGui::MenuItem("Duplicar", "Ctrl+D")) { Command c; c.type = CommandType::LayerDuplicate; c.layer_ref.layer = LayerId::unpack(id); s.send(c); }
    if (ImGui::MenuItem("Copiar", "Ctrl+C")) s.engine.copy_layers(s.selection.data(), u32(s.selection.size()));
    if (l.row.kind == u32(LayerKind::Video) && ImGui::MenuItem("Extrair áudio")) { auto r = s.engine.extract_audio(id); s.error(r.status()); }
    if ((l.row.kind == u32(LayerKind::Video) || l.row.kind == u32(LayerKind::Composition)) && ImGui::MenuItem("Congelar quadro (2 s)")) {
        auto r = s.engine.freeze_frame(id, s.playhead(), std::llround(s.fps() * 2));
        s.error(r.status());
    }
    if (ImGui::MenuItem("Aninhar…")) {
        auto r = s.engine.precompose(s.selection.data(), u32(s.selection.size()), "Sequência aninhada");
        if (r.ok()) s.select(*r); else s.error(r.status());
    }
    if (ImGui::BeginMenu("Velocidade")) {
        for (float sp : {.25f, .5f, 1.f, 1.5f, 2.f, 4.f}) {
            char b[32];
            std::snprintf(b, sizeof b, "%g%%", double(sp * 100));
            if (ImGui::MenuItem(b)) { Command c; c.type = CommandType::LayerSetSpeed; c.audio_gain = {LayerId::unpack(id), sp}; s.send(c); }
        }
        if (ImGui::MenuItem("Inverter")) { Command c; c.type = CommandType::LayerSetReversed; c.audio_flag = {LayerId::unpack(id), true}; s.send(c); }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Limpar", "Del")) s.delete_selection();
}

void track_header(Studio& s, ImDrawList* dl, TrackView& t, const ImRect& r) {
    const std::string label = (t.audio ? "A" : "V") + std::to_string(t.number);
    dl->AddRectFilled(r.Min, r.Max, col::SurfaceHigh);
    dl->AddLine(ImVec2(r.Min.x, r.Max.y - 1), ImVec2(r.Max.x, r.Max.y - 1), col::Background);
    ImGui::PushID(label.c_str());
    const float b = std::min(S(22), r.GetHeight() - S(4));
    const float cy = r.Min.y + std::min(r.GetHeight(), S(36)) * .5f - b * .5f;
    std::vector<int> all = t.layers;
    all.insert(all.end(), t.mirrors.begin(), t.mirrors.end());
    bool locked = !all.empty(), visible = false, muted = !all.empty(), solo = false;
    for (int i : all) {
        const auto& l = s.layers[i];
        locked &= l.locked();
        visible |= l.visible();
        muted &= bool(l.row.flags & bridge::kLayerRowFlagSolo) ? false : false;
        solo |= bool(l.row.flags & bridge::kLayerRowFlagSolo);
    }
    if (all.empty()) visible = true;
    ImGui::SetCursorScreenPos(ImVec2(r.Min.x + S(4), cy));
    if (icon_button("lock", locked ? Icon::Lock : Icon::Unlock, "Ativar/desativar o bloqueio da faixa", locked, b))
        for (int i : t.layers) { Command c; c.type = CommandType::LayerSetLocked; c.layer_locked = {LayerId::unpack(s.layers[i].row.id), !locked}; s.send(c); }
    ImGui::SameLine(0, S(4));
    // Etiqueta da faixa (alvo).
    const ImVec2 chip = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("target", ImVec2(S(30), b));
    dl->AddRectFilled(chip, ImVec2(chip.x + S(30), chip.y + b), t.audio ? col::alpha(col::ClipAudio, .55f) : col::alpha(col::Brand, .9f), S(3));
    ImGui::PushFont(gFontBold, gFontBold->LegacySize);
    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    dl->AddText(ImVec2(chip.x + (S(30) - ts.x) * .5f, chip.y + (b - ts.y) * .5f), col::Text, label.c_str());
    ImGui::PopFont();
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        if (T.expanded.count(label)) T.expanded.erase(label); else T.expanded.insert(label);
    }
    tooltip("Clique duplo: expandir ou recolher a faixa");
    ImGui::SameLine(0, S(4));
    if (!t.audio) {
        if (icon_button("eye", visible ? Icon::Eye : Icon::EyeOff, "Ativar/desativar a saída da faixa", false, b))
            for (int i : t.layers) { Command c; c.type = CommandType::LayerSetVisible; c.layer_visible = {LayerId::unpack(s.layers[i].row.id), !visible}; s.send(c); }
    } else {
        bool anyMuted = false;
        for (int i : all) {
            bridge::LayerDetailPOD d{};
            if (s.engine.query_layer_detail(s.layers[i].row.id, d) && (d.audioFlags & 1u)) anyMuted = true;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(5), (b - ImGui::GetFontSize()) * .5f));
        if (small_toggle("m", "M", anyMuted, "Silenciar faixa"))
            for (int i : all) { Command c; c.type = CommandType::AudioSetMuted; c.audio_flag = {LayerId::unpack(s.layers[i].row.id), !anyMuted}; s.send(c); }
        ImGui::SameLine(0, S(3));
        if (small_toggle("s", "S", solo, "Faixa solo"))
            for (int i : all) { Command c; c.type = CommandType::AudioSetSolo; c.audio_flag = {LayerId::unpack(s.layers[i].row.id), !solo}; s.send(c); }
        ImGui::PopStyleVar();
        ImGui::SameLine(0, S(3));
        icon_button("mic", Icon::Mic, "Gravação de voz (não disponível nesta versão)", false, b, false);
    }
    if (r.GetHeight() > S(44)) {
        dl->AddText(ImVec2(r.Min.x + S(8), r.Min.y + S(36)), col::Subtle, t.audio ? ("Áudio " + std::to_string(t.number)).c_str() : ("Vídeo " + std::to_string(t.number)).c_str());
    }
    ImGui::PopID();
}

} // namespace

void draw_timeline(Studio& s) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin("Linha do tempo", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open) { ImGui::End(); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 o = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    const float headerW = S(200), topH = S(58), zoomH = S(16), vbarW = S(10);
    const float x0 = o.x, y0 = o.y, x1 = o.x + avail.x, y1 = o.y + avail.y;
    const float laneX0 = x0 + headerW, laneX1 = x1 - vbarW, tracksY0 = y0 + topH, tracksY1 = y1 - zoomH;
    if (laneX1 - laneX0 < S(40) || tracksY1 - tracksY0 < S(20)) { ImGui::End(); return; }
    const double fps = s.fps();
    s.timelineWidth = laneX1 - laneX0;
    s.pxPerFrame = std::clamp(s.pxPerFrame, 0.01, 80.0);
    const double visibleFrames = (laneX1 - laneX0) / s.pxPerFrame;
    s.scrollFrame = std::max(0.0, s.scrollFrame);
    auto X = [&](double frame) { return float(laneX0 + (frame - s.scrollFrame) * s.pxPerFrame); };
    auto F = [&](float x) { return s.scrollFrame + (x - laneX0) / s.pxPerFrame; };
    const i64 playhead = s.playhead();

    // Página acompanha o indicador durante a reprodução.
    if (s.status.playing && s.followPlayhead && T.drag == Drag::None) {
        const float px = X(double(playhead));
        if (px > laneX1 - S(20) || px < laneX0) s.scrollFrame = std::max(0.0, double(playhead) - visibleFrames * .05);
    }

    // --- Canto superior esquerdo: código de tempo e opções ----------------
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(laneX0, tracksY0), col::Surface);
    ImGui::PushFont(gFontTimecode, gFontTimecode->LegacySize);
    const std::string tc = s.timecode(playhead);
    ImGui::SetCursorScreenPos(ImVec2(x0 + S(10), y0 + S(4)));
    ImGui::PushStyleColor(ImGuiCol_Text, col::Accent);
    ImGui::TextUnformatted(tc.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    // Arrastar o código de tempo percorre os quadros (como o valor quente).
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) { T.drag = Drag::Playhead; T.down = io.MousePos; T.downFrames = double(playhead); }
    ImGui::SetCursorScreenPos(ImVec2(x0 + S(8), y0 + topH - S(26)));
    const float ib = S(22);
    if (icon_button("snap", Icon::Magnet, "Encaixar na linha do tempo (S)", s.snap, ib)) s.snap = !s.snap;
    ImGui::SameLine(0, S(2));
    if (icon_button("link", Icon::Link, "Seleção vinculada", s.linkedSelection, ib)) s.linkedSelection = !s.linkedSelection;
    ImGui::SameLine(0, S(2));
    if (icon_button("mark", Icon::Marker, "Adicionar marcador (M)", false, ib)) s.add_marker();
    ImGui::SameLine(0, S(2));
    if (icon_button("follow", Icon::Play, "Acompanhar o indicador na reprodução", s.followPlayhead, ib)) s.followPlayhead = !s.followPlayhead;
    ImGui::SameLine(0, S(2));
    if (icon_button("settings", Icon::Wrench, "Configurações de exibição da linha do tempo", false, ib)) ImGui::OpenPopup("##tlsettings");
    if (ImGui::BeginPopup("##tlsettings")) {
        ImGui::MenuItem("Mostrar miniaturas de vídeo", nullptr, &s.showThumbs);
        ImGui::MenuItem("Mostrar formas de onda de áudio", nullptr, &s.showWaveforms);
        ImGui::MenuItem("Mostrar keyframes nos clipes", nullptr, &s.showKeyframes);
        ImGui::Separator();
        if (ImGui::MenuItem("Expandir todas as faixas")) { for (auto& t : s.videoTracks) T.expanded.insert("V" + std::to_string(t.number)); for (auto& t : s.audioTracks) T.expanded.insert("A" + std::to_string(t.number)); }
        if (ImGui::MenuItem("Minimizar todas as faixas")) T.expanded.clear();
        ImGui::EndPopup();
    }

    // --- Régua --------------------------------------------------------------
    const ImRect ruler(ImVec2(laneX0, y0), ImVec2(laneX1, tracksY0));
    dl->AddRectFilled(ruler.Min, ruler.Max, col::Background);
    dl->PushClipRect(ruler.Min, ruler.Max, true);
    const double major = nice_step(S(90) / s.pxPerFrame, fps);
    const int minorCount = major >= fps ? 4 : major >= 5 ? 5 : int(major);
    const double minor = major / std::max(1, minorCount);
    for (double f = std::floor(s.scrollFrame / major) * major; X(f) < laneX1 + 1; f += major) {
        const float x = X(f);
        dl->AddLine(ImVec2(x, tracksY0 - S(16)), ImVec2(x, tracksY0), col::TickStrong);
        dl->AddText(gFontSmall, gFontSmall->LegacySize * s.dpi, ImVec2(x + S(4), tracksY0 - S(30)), col::Muted, s.timecode(i64(std::llround(f))).c_str());
        for (int k = 1; k < minorCount; ++k) {
            const float xx = X(f + minor * k);
            dl->AddLine(ImVec2(xx, tracksY0 - S(6)), ImVec2(xx, tracksY0), col::TickWeak);
        }
    }
    // Marcadores na régua.
    for (size_t i = 0; i + 2 < s.markers.size() + 1 && i < s.markers.size(); i += 3) {
        const float x = X(double(s.markers[i]));
        if (x < laneX0 - S(8) || x > laneX1 + S(8)) continue;
        draw_icon(dl, Icon::Marker, ImVec2(x, y0 + S(10)), S(12), col::Warning);
    }
    // Área com conteúdo (barra fina, como a área de trabalho).
    const i64 contentEnd = s.engine.query_navigation_end();
    if (contentEnd > 0) dl->AddRectFilled(ImVec2(X(0), y0 + S(19)), ImVec2(X(double(contentEnd)), y0 + S(22)), col::alpha(col::Accent, .35f));
    dl->PopClipRect();
    dl->AddLine(ImVec2(x0, tracksY0 - 1), ImVec2(x1, tracksY0 - 1), col::Border);

    ImGui::SetCursorScreenPos(ruler.Min);
    ImGui::InvisibleButton("##ruler", ruler.GetSize());
    if (ImGui::IsItemActivated()) {
        T.drag = Drag::Scrub;
        s.command(CommandType::PlaybackScrubBegin);
    }
    if (ImGui::IsItemHovered()) {
        // Clique num marcador: vai até ele; clique direito: remove.
        for (size_t i = 0; i < s.markers.size(); i += 3)
            if (std::abs(io.MousePos.x - X(double(s.markers[i]))) < S(6) && io.MousePos.y < y0 + S(18)) {
                ImGui::SetTooltip("Marcador em %s\nClique direito para remover", s.timecode(s.markers[i]).c_str());
                if (ImGui::IsMouseClicked(1)) { s.engine.delete_marker(s.markers[i]); s.revision = ~0u; }
            }
    }

    // --- Faixas ----------------------------------------------------------------
    auto pad = [](std::vector<TrackView>& v, bool audio) {
        while (v.size() < 3) { TrackView t; t.audio = audio; t.number = int(v.size()) + 1; v.push_back(t); }
    };
    std::vector<TrackView> video = s.videoTracks, audio = s.audioTracks;
    pad(video, false);
    pad(audio, true);
    const float baseH = S(34), tallH = S(74), gap = S(6);
    for (auto& t : video) t.height = T.expanded.count("V" + std::to_string(t.number)) ? tallH : baseH;
    for (auto& t : audio) t.height = T.expanded.count("A" + std::to_string(t.number)) ? tallH : baseH;
    float contentH = gap;
    for (auto& t : video) contentH += t.height;
    for (auto& t : audio) contentH += t.height;
    const float viewH = tracksY1 - tracksY0;
    // Vídeo fica encostado na divisória, como nas suítes de edição.
    float videoH = 0;
    for (auto& t : video) videoH += t.height;
    s.trackScroll = std::clamp(s.trackScroll, std::min(0.f, -(viewH * .5f - videoH)), std::max(0.f, contentH - viewH));

    std::vector<ClipHit> hits;
    std::vector<std::pair<ImRect, TrackView*>> rows;
    float y = tracksY0 - s.trackScroll + std::max(0.f, viewH * .5f - videoH);
    for (int i = int(video.size()) - 1; i >= 0; --i) { rows.push_back({ImRect(ImVec2(x0, y), ImVec2(x1, y + video[i].height)), &video[i]}); y += video[i].height; }
    const float divider = y;
    y += gap;
    for (auto& t : audio) { rows.push_back({ImRect(ImVec2(x0, y), ImVec2(x1, y + t.height)), &t}); y += t.height; }

    dl->PushClipRect(ImVec2(x0, tracksY0), ImVec2(x1, tracksY1), true);
    dl->AddRectFilled(ImVec2(laneX0, tracksY0), ImVec2(laneX1, tracksY1), col::Stage);
    dl->AddRectFilled(ImVec2(x0, divider), ImVec2(x1, divider + gap), col::Background);
    // Grade vertical sutil dos segundos.
    for (double f = std::floor(s.scrollFrame / major) * major; X(f) < laneX1; f += major)
        dl->AddLine(ImVec2(X(f), tracksY0), ImVec2(X(f), tracksY1), col::alpha(col::Border, .45f));

    const float thumbH = 0;
    (void)thumbH;
    for (auto& [r, t] : rows) {
        if (r.Max.y < tracksY0 || r.Min.y > tracksY1) continue;
        dl->AddRectFilled(ImVec2(laneX0, r.Min.y), ImVec2(laneX1, r.Max.y), (t->number & 1) ? col::Stage : col::alpha(col::Background, .7f));
        dl->AddLine(ImVec2(laneX0, r.Max.y - 1), ImVec2(laneX1, r.Max.y - 1), col::alpha(col::Border, .6f));
        auto draw_clip = [&](int index, bool mirror) {
            LayerView& l = s.layers[index];
            const float a = X(l.row.startFrame), b = X(l.row.endFrame);
            if (b < laneX0 || a > laneX1) return;
            const bool selected = s.is_selected(l.row.id);
            ImRect c(ImVec2(std::max(a, laneX0 - S(2)), r.Min.y + S(2)), ImVec2(std::min(b, laneX1 + S(2)), r.Max.y - S(2)));
            ImU32 fill = mirror ? col::ClipAudio : clip_color(l);
            if (!l.visible()) fill = col::mix(fill, col::Stage, .55f);
            if (selected) fill = col::mix(fill, col::Text, .22f);
            dl->AddRectFilled(c.Min, c.Max, fill, S(2));
            dl->AddRectFilled(c.Min, ImVec2(c.Max.x, c.Min.y + S(3)), col::alpha(col::Text, .18f), S(2), ImDrawFlags_RoundCornersTop);
            dl->PushClipRect(c.Min, c.Max, true);
            float textX = std::max(a, laneX0) + S(6);
            // Miniatura no começo do clipe (vídeo/imagem).
            const bool visual = !mirror && (l.row.kind == u32(LayerKind::Video) || l.row.kind == u32(LayerKind::Image) || l.row.kind == u32(LayerKind::Composition));
            if (visual && s.showThumbs && c.GetHeight() > S(22)) {
                const u32 th = u32(std::clamp(c.GetHeight() - S(4), 16.f, 96.f));
                u32 tw = 0, thh = 0;
                if (auto* srv = s.thumbnail(l.row.id, l.row.startFrame, th, &tw, &thh)) {
                    const float h = c.GetHeight() - S(4), w = h * float(tw) / float(std::max(1u, thh));
                    if (a + S(2) + w < b - S(2)) {
                        const ImVec2 p0(a + S(2), c.Min.y + S(2));
                        dl->AddImage(ImTextureRef(ImTextureID(srv)), p0, ImVec2(p0.x + w, p0.y + h));
                        textX = std::max(textX, p0.x + w + S(6));
                        // Final do clipe também (cabeça e cauda).
                        if (b - a > w * 3) {
                            u32 tw2 = 0, th2 = 0;
                            if (auto* tail = s.thumbnail(l.row.id, l.row.endFrame - 1, th, &tw2, &th2)) {
                                const ImVec2 q0(b - S(2) - w, c.Min.y + S(2));
                                dl->AddImage(ImTextureRef(ImTextureID(tail)), q0, ImVec2(q0.x + w, q0.y + h));
                            }
                        }
                    }
                }
            }
            // Forma de onda.
            if ((mirror || l.audio()) && s.showWaveforms) {
                const float wx0 = std::max(a, laneX0), wx1 = std::min(b, laneX1);
                const u32 count = u32(std::clamp((wx1 - wx0) / S(2), 1.f, 2048.f));
                const double first = F(wx0), perBucket = (wx1 - wx0) / count / s.pxPerFrame;
                const auto& wave = waveform(s, l.row.id, first, perBucket, count);
                const float mid = (c.Min.y + c.Max.y) * .5f + S(4), amp = (c.GetHeight() - S(16)) * .5f;
                for (u32 i = 0; i < wave.size(); ++i) {
                    const float xx = wx0 + (i + .5f) * (wx1 - wx0) / count, v = wave[i] / 255.f * amp;
                    dl->AddLine(ImVec2(xx, mid - v), ImVec2(xx, mid + v), col::alpha(col::Text, .55f), S(1.2f));
                }
            }
            // Nome e selo de efeitos.
            ImGui::PushFont(gFontSmall, gFontSmall->LegacySize);
            if (l.row.effectCount > 0 && !mirror) {
                const ImVec2 fx(textX, c.Min.y + S(4));
                dl->AddRectFilled(fx, ImVec2(fx.x + S(15), fx.y + S(11)), col::Warning, S(2));
                draw_icon(dl, Icon::Fx, ImVec2(fx.x + S(7.5f), fx.y + S(5.5f)), S(11), col::Stage);
                textX += S(19);
            }
            dl->AddText(ImVec2(textX, c.Min.y + S(3)), col::alpha(col::Text, .95f), l.name.c_str());
            ImGui::PopFont();
            // Keyframes no clipe (tempo local → linha do tempo).
            if (s.showKeyframes && !mirror) {
                std::set<i32> times;
                for (auto& k : s.layer_keys(l.row.id)) if (!(k.timelineFlags & bridge::kKeyframeTimelineHidden)) times.insert(k.time);
                for (i32 t2 : times) {
                    const float kx = X(double(t2) + l.row.startFrame - l.row.offsetFrames);
                    draw_diamond(dl, ImVec2(kx, c.Max.y - S(6)), S(3.5f), col::Keyframe, col::Stage);
                }
            }
            dl->PopClipRect();
            if (selected) dl->AddRect(c.Min, c.Max, col::Text, S(2), S(1.2f));
            else dl->AddRect(c.Min, c.Max, col::alpha(col::Stage, .8f), S(2));
            hits.push_back({c, index, mirror});
        };
        for (int i : t->layers) draw_clip(i, false);
        for (int i : t->mirrors) draw_clip(i, true);
    }

    // Marcadores e indicador atravessam as faixas.
    for (size_t i = 0; i < s.markers.size(); i += 3) {
        const float x = X(double(s.markers[i]));
        if (x >= laneX0 && x <= laneX1) dl->AddLine(ImVec2(x, tracksY0), ImVec2(x, tracksY1), col::alpha(col::Warning, .35f));
    }
    if (T.snapX >= 0) dl->AddLine(ImVec2(T.snapX, tracksY0), ImVec2(T.snapX, tracksY1), col::Warning, S(1.5f));
    dl->PopClipRect();

    // Cabeçalhos das faixas (por cima, presos à esquerda).
    dl->PushClipRect(ImVec2(x0, tracksY0), ImVec2(laneX0, tracksY1), true);
    ImGui::PushClipRect(ImVec2(x0, tracksY0), ImVec2(laneX0, tracksY1), true);
    for (auto& [r, t] : rows) {
        if (r.Max.y < tracksY0 || r.Min.y > tracksY1) continue;
        track_header(s, dl, *t, ImRect(r.Min, ImVec2(laneX0 - 1, r.Max.y)));
    }
    ImGui::PopClipRect();
    dl->PopClipRect();
    dl->AddLine(ImVec2(laneX0 - 1, y0), ImVec2(laneX0 - 1, tracksY1), col::Background, S(2));

    // --- Interação nas faixas -------------------------------------------------
    const ImRect lanes(ImVec2(laneX0, tracksY0), ImVec2(laneX1, tracksY1));
    ImGui::SetCursorScreenPos(lanes.Min);
    ImGui::InvisibleButton("##lanes", lanes.GetSize(), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool lanesHovered = ImGui::IsItemHovered();
    const ImVec2 m = io.MousePos;
    const ClipHit* under = nullptr;
    for (auto it = hits.rbegin(); it != hits.rend(); ++it) if (it->rect.Contains(m)) { under = &*it; break; }
    const float edge = S(6);
    const bool onIn = under && std::abs(m.x - X(s.layers[under->layer].row.startFrame)) < edge;
    const bool onOut = under && !onIn && std::abs(m.x - X(s.layers[under->layer].row.endFrame)) < edge;

    // Arrastar efeitos do navegador para um clipe.
    if (ImGui::BeginDragDropTarget()) {
        if (under) dl->AddRect(under->rect.Min, under->rect.Max, col::Accent, S(2), S(2));
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("AUREA_FX")) {
            if (under) s.apply_effect(*static_cast<const u32*>(p->Data), s.layers[under->layer].row.id);
        }
        ImGui::EndDragDropTarget();
    }

    if (lanesHovered && T.drag == Drag::None) {
        if (s.tool == Tool::Hand) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        else if (s.tool == Tool::Razor) {
            const float rx = s.snap && std::abs(m.x - X(double(playhead))) < S(8) ? X(double(playhead)) : m.x;
            if (under) dl->AddLine(ImVec2(rx, under->rect.Min.y), ImVec2(rx, under->rect.Max.y), col::Text, S(1.5f));
        } else if ((onIn || onOut) && s.tool != Tool::Type) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        // Roda: horizontal; Ctrl/Alt + roda: zoom no ponto; Shift + roda: faixas.
        if (io.MouseWheel != 0) {
            if (io.KeyCtrl || io.KeyAlt) {
                const double at = F(m.x);
                s.pxPerFrame = std::clamp(s.pxPerFrame * std::pow(1.18, io.MouseWheel), 0.01, 80.0);
                s.scrollFrame = std::max(0.0, at - (m.x - laneX0) / s.pxPerFrame);
            } else if (io.KeyShift) s.trackScroll -= io.MouseWheel * S(40);
            else s.scrollFrame = std::max(0.0, s.scrollFrame - io.MouseWheel * S(60) / s.pxPerFrame);
        }
        if (io.MouseWheelH != 0) s.scrollFrame = std::max(0.0, s.scrollFrame - io.MouseWheelH * S(60) / s.pxPerFrame);
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Middle)) { T.drag = Drag::Pan; T.down = m; T.downScroll = s.scrollFrame; T.downTrackScroll = s.trackScroll; }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (under) {
            if (!s.is_selected(s.layers[under->layer].row.id)) s.select(s.layers[under->layer].row.id);
            ImGui::OpenPopup("##clipmenu");
        } else ImGui::OpenPopup("##lanemenu");
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        T.down = m;
        T.moved = false;
        T.lastDelta = 0;
        T.origins.clear();
        const i64 frameAt = i64(std::llround(F(m.x)));
        switch (s.tool) {
        case Tool::Hand:
            T.drag = Drag::Pan; T.downScroll = s.scrollFrame; T.downTrackScroll = s.trackScroll;
            break;
        case Tool::Razor:
            if (under && !s.layers[under->layer].locked()) {
                i64 at = frameAt;
                if (s.snap && std::abs(m.x - X(double(playhead))) < S(8)) at = playhead;
                const auto& l = s.layers[under->layer];
                if (at > l.row.startFrame && at < l.row.endFrame) {
                    Command c; c.type = CommandType::LayerSplit; c.layer_split = {LayerId::unpack(l.row.id), FrameIndex{at}, {}}; s.send(c);
                }
            }
            break;
        case Tool::Type: {
            s.seek(frameAt);
            auto r = s.engine.add_text("Título");
            if (r.ok()) s.select(*r); else s.error(r.status());
            s.tool = Tool::Select;
            break;
        }
        case Tool::TrackForward: {
            std::vector<u64> ids;
            for (auto& l : s.layers) if (l.row.endFrame > frameAt) ids.push_back(l.row.id);
            s.set_selection(ids);
            break;
        }
        default:
            if (under) {
                const u64 id = s.layers[under->layer].row.id;
                if (io.KeyCtrl || io.KeyShift) s.select(id, true);
                else if (!s.is_selected(id)) s.select(id);
                else if (s.primary != id) { auto sel = s.selection; sel.erase(std::find(sel.begin(), sel.end(), id)); sel.insert(sel.begin(), id); s.set_selection(sel); }
                T.dragLayer = id;
                T.drag = onIn ? Drag::TrimIn : onOut ? Drag::TrimOut : Drag::Move;
                for (u64 sid : (T.drag == Drag::Move ? s.selection : std::vector<u64>{id}))
                    if (auto* l = s.layer(sid); l && !l->locked()) T.origins.push_back({sid, l->row.startFrame, l->row.endFrame, l->row.offsetFrames});
                if (ImGui::IsMouseDoubleClicked(0)) ImGui::SetWindowFocus("Controles de efeito");
            } else {
                if (!io.KeyCtrl && !io.KeyShift) s.set_selection({});
                T.drag = Drag::Marquee;
            }
        }
    }
    if (ImGui::BeginPopup("##clipmenu")) {
        if (auto* l = s.layer(s.primary)) clip_menu(s, *l);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##lanemenu")) {
        if (ImGui::MenuItem("Colar no indicador", "Ctrl+V")) s.engine.paste_layers(playhead);
        if (ImGui::MenuItem("Adicionar marcador", "M")) s.add_marker();
        if (ImGui::MenuItem("Ajustar zoom à sequência", "\\")) { s.pxPerFrame = (laneX1 - laneX0) / double(std::max<i64>(s.duration(), 1)) * .95; s.scrollFrame = 0; }
        ImGui::EndPopup();
    }

    // --- Arrastos em andamento ---------------------------------------------
    T.snapX = -1;
    if (T.drag != Drag::None) {
        if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2)) {
            if (T.drag == Drag::Scrub) s.command(CommandType::PlaybackScrubEnd);
            if ((T.drag == Drag::Move || T.drag == Drag::TrimIn || T.drag == Drag::TrimOut) && T.moved) s.end_group();
            if (T.drag == Drag::Marquee && T.moved) {
                const ImRect box(ImMin(T.down, m), ImMax(T.down, m));
                std::vector<u64> ids = (io.KeyCtrl || io.KeyShift) ? s.selection : std::vector<u64>{};
                for (auto& h : hits) if (box.Overlaps(h.rect)) {
                    const u64 id = s.layers[h.layer].row.id;
                    if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
                }
                s.set_selection(ids);
            }
            T.drag = Drag::None;
        } else {
            const double dx = m.x - T.down.x;
            if (std::abs(dx) > S(3) || std::abs(m.y - T.down.y) > S(3)) {
                if (!T.moved && (T.drag == Drag::Move || T.drag == Drag::TrimIn || T.drag == Drag::TrimOut)) s.begin_group();
                T.moved = true;
            }
            switch (T.drag) {
            case Drag::Scrub: {
                Command c;
                c.type = CommandType::PlaybackScrub;
                i64 f = i64(std::llround(F(m.x)));
                if (s.snap) for (size_t i = 0; i < s.markers.size(); i += 3) if (std::abs(X(double(s.markers[i])) - m.x) < S(6)) f = s.markers[i];
                c.seek.time = tick_at(FrameIndex{std::clamp<i64>(f, 0, std::max<i64>(0, s.duration() - 1))}, fps);
                s.send(c);
                if (m.x > laneX1) s.scrollFrame += (m.x - laneX1) * .2 / s.pxPerFrame;
                if (m.x < laneX0) s.scrollFrame = std::max(0.0, s.scrollFrame - (laneX0 - m.x) * .2 / s.pxPerFrame);
                break;
            }
            case Drag::Playhead:
                s.seek(i64(std::llround(T.downFrames + (m.x - T.down.x) / S(3))));
                break;
            case Drag::Pan:
                s.scrollFrame = std::max(0.0, T.downScroll - dx / s.pxPerFrame);
                s.trackScroll = T.downTrackScroll - (m.y - T.down.y);
                break;
            case Drag::Move: case Drag::TrimIn: case Drag::TrimOut: {
                if (!T.moved || T.origins.empty()) break;
                i64 delta = i64(std::llround(dx / s.pxPerFrame));
                const auto* lead = &T.origins.front();
                for (auto& oo : T.origins) if (oo.id == T.dragLayer) lead = &oo;
                if (s.snap && !io.KeyAlt) {
                    double best = S(8) / s.pxPerFrame;
                    i64 bestDelta = delta;
                    for (i64 target : snap_targets(s)) {
                        const i64 edges[2] = {lead->start, lead->end};
                        for (int e = 0; e < 2; ++e) {
                            if (T.drag == Drag::TrimIn && e == 1) continue;
                            if (T.drag == Drag::TrimOut && e == 0) continue;
                            const double d = std::abs(double(edges[e] + delta - target));
                            if (d < best) { best = d; bestDelta = target - edges[e]; T.snapX = X(double(target)); }
                        }
                    }
                    delta = bestDelta;
                }
                if (T.drag == Drag::Move) {
                    i64 minStart = INT64_MAX;
                    for (auto& oo : T.origins) minStart = std::min(minStart, oo.start);
                    delta = std::max(delta, -minStart);
                }
                if (delta == T.lastDelta) break;
                T.lastDelta = delta;
                for (auto& oo : T.origins) {
                    i64 start = oo.start, end = oo.end, offset = oo.offset;
                    if (T.drag == Drag::Move) { start += delta; end += delta; }
                    else if (T.drag == Drag::TrimIn) { start = std::clamp<i64>(start + delta, std::max<i64>(0, oo.start - oo.offset), end - 1); offset += start - oo.start; }
                    else end = std::max<i64>(start + 1, end + delta);
                    Command c;
                    c.type = CommandType::LayerSetTimeRange;
                    c.layer_range = {LayerId::unpack(oo.id), FrameIndex{start}, FrameIndex{end}, FrameIndex{offset}, 1};
                    s.send(c);
                }
                ImGui::SetTooltip("%s%s", delta >= 0 ? "+" : "-", s.timecode(std::abs(delta)).c_str());
                break;
            }
            case Drag::Marquee:
                if (T.moved) {
                    dl->AddRectFilled(ImMin(T.down, m), ImMax(T.down, m), col::alpha(col::Accent, .12f));
                    dl->AddRect(ImMin(T.down, m), ImMax(T.down, m), col::Accent);
                }
                break;
            default: break;
            }
        }
    }

    // Indicador de reprodução: cabeça na régua e linha nas faixas.
    {
        const float px = X(double(playhead));
        if (px >= laneX0 - 1 && px <= laneX1 + 1) {
            dl->PushClipRect(ImVec2(laneX0 - S(8), y0), ImVec2(laneX1 + S(8), tracksY1), true);
            const float hy = tracksY0 - S(16);
            const ImVec2 head[5] = {{px - S(6), hy - S(6)}, {px + S(6), hy - S(6)}, {px + S(6), hy + S(6)}, {px, hy + S(12)}, {px - S(6), hy + S(6)}};
            dl->AddConvexPolyFilled(head, 5, col::Accent);
            dl->AddLine(ImVec2(px, hy + S(10)), ImVec2(px, tracksY1), col::Playhead, S(1));
            dl->PopClipRect();
        }
    }

    // --- Barra de rolagem/zoom (alças nas pontas) ---------------------------
    {
        const ImRect bar(ImVec2(laneX0, tracksY1 + S(2)), ImVec2(laneX1, y1 - S(2)));
        dl->AddRectFilled(ImVec2(x0, tracksY1), ImVec2(x1, y1), col::Surface);
        const double total = std::max<double>(double(s.duration()) + fps * 5, s.scrollFrame + visibleFrames);
        const float tx0 = bar.Min.x + float(s.scrollFrame / total) * bar.GetWidth();
        const float tx1 = bar.Min.x + float((s.scrollFrame + visibleFrames) / total) * bar.GetWidth();
        const float r = bar.GetHeight() * .5f;
        dl->AddRectFilled(ImVec2(tx0, bar.Min.y), ImVec2(std::max(tx1, tx0 + r * 2), bar.Max.y), col::ChipHigh, r);
        dl->AddCircleFilled(ImVec2(tx0 + r, bar.Min.y + r), r * .7f, col::Muted);
        dl->AddCircleFilled(ImVec2(std::max(tx1, tx0 + r * 2) - r, bar.Min.y + r), r * .7f, col::Muted);
        ImGui::SetCursorScreenPos(bar.Min);
        ImGui::InvisibleButton("##zoombar", bar.GetSize());
        if (ImGui::IsItemActivated()) {
            T.down = m;
            T.downScroll = s.scrollFrame;
            T.downPx = s.pxPerFrame;
            T.drag = std::abs(m.x - tx0) < r * 2 ? Drag::ZoomLeft : std::abs(m.x - tx1) < r * 2 ? Drag::ZoomRight : Drag::ZoomThumb;
            if (m.x < tx0 - r * 2 || m.x > tx1 + r * 2) { s.scrollFrame = std::max(0.0, (m.x - bar.Min.x) / bar.GetWidth() * total - visibleFrames * .5); T.downScroll = s.scrollFrame; T.drag = Drag::ZoomThumb; }
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { s.pxPerFrame = (laneX1 - laneX0) / double(s.duration()) * .95; s.scrollFrame = 0; }
        if (ImGui::IsItemActive()) {
            const double df = (m.x - T.down.x) / bar.GetWidth() * total;
            const double downVisible = (laneX1 - laneX0) / T.downPx;
            if (T.drag == Drag::ZoomThumb) s.scrollFrame = std::max(0.0, T.downScroll + df);
            else if (T.drag == Drag::ZoomLeft) {
                const double start = std::clamp(T.downScroll + df, 0.0, T.downScroll + downVisible - 2);
                s.pxPerFrame = (laneX1 - laneX0) / (T.downScroll + downVisible - start);
                s.scrollFrame = start;
            } else if (T.drag == Drag::ZoomRight) s.pxPerFrame = (laneX1 - laneX0) / std::max(2.0, downVisible + df);
        }
        if (ImGui::IsItemDeactivated()) T.drag = Drag::None;
        tooltip("Arraste para rolar; puxe as pontas para ampliar. Clique duplo: ajustar à sequência.");
    }
    // Barra vertical das faixas.
    if (contentH > viewH) {
        const float h = tracksY1 - tracksY0, frac = viewH / contentH;
        const float ty = tracksY0 + (s.trackScroll / contentH) * h;
        dl->AddRectFilled(ImVec2(laneX1 + S(2), ty), ImVec2(x1 - S(2), ty + h * frac), col::Chip, S(4));
    }
    ImGui::End();
}

} // namespace aurea::studio
