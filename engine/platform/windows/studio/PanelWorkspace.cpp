// Espaço de trabalho: menus, cabeçalho, arranjos de painéis (Montagem, Edição,
// Efeitos, Áudio, Gráficos), Projeto, navegador de Efeitos, Programa,
// Ferramentas, Medidores, painéis de apoio, diálogos e atalhos de teclado.
#include "Studio.hpp"
#include "imgui_internal.h"
#include <commdlg.h>
#include <shellapi.h>
#include <cmath>
#include <cstdio>

namespace aurea::studio {
namespace {

void add_item(Studio& s, int kind) {
    Result<u64> r{Status{Errc::InvalidArgument}};
    switch (kind) {
    case 0: r = s.engine.add_text("Título"); break;
    case 1: r = s.engine.add_shape(0); break;
    case 2: r = s.engine.add_shape(1); break;
    case 3: r = s.engine.add_null(false); break;
    case 4: r = s.engine.add_camera(); break;
    case 5: r = s.engine.add_light(0); break;
    case 6: r = s.engine.add_particles(0); break;
    case 7: r = s.engine.add_aurora(); break;
    case 8: r = s.engine.add_shape3d(0, "Cubo"); break;
    case 9: r = s.engine.add_reflective_floor(); break;
    }
    if (r.ok()) s.select(*r); else s.error(r.status(), "Não foi possível criar o item");
}

void new_item_menu(Studio& s) {
    if (ImGui::MenuItem("Texto", "Ctrl+T")) add_item(s, 0);
    if (ImGui::MenuItem("Retângulo")) add_item(s, 1);
    if (ImGui::MenuItem("Elipse")) add_item(s, 2);
    ImGui::Separator();
    if (ImGui::MenuItem("Objeto nulo")) add_item(s, 3);
    if (ImGui::MenuItem("Câmera")) add_item(s, 4);
    if (ImGui::MenuItem("Luz")) add_item(s, 5);
    ImGui::Separator();
    if (ImGui::MenuItem("Partículas")) add_item(s, 6);
    if (ImGui::MenuItem("Aurora")) add_item(s, 7);
    if (ImGui::MenuItem("Cubo 3D")) add_item(s, 8);
    if (ImGui::MenuItem("Chão reflexivo")) add_item(s, 9);
}

void select_all(Studio& s) {
    std::vector<u64> ids;
    for (auto& l : s.layers) ids.push_back(l.row.id);
    s.set_selection(ids);
}

void duplicate_selection(Studio& s) {
    s.begin_group();
    for (u64 id : s.selection) { Command c; c.type = CommandType::LayerDuplicate; c.layer_ref.layer = LayerId::unpack(id); s.send(c); }
    s.end_group();
}

void toggle_enable(Studio& s) {
    s.begin_group();
    for (u64 id : s.selection) if (auto* l = s.layer(id)) { Command c; c.type = CommandType::LayerSetVisible; c.layer_visible = {LayerId::unpack(id), !l->visible()}; s.send(c); }
    s.end_group();
}

void zoom_timeline(Studio& s, double factor) {
    const double at = double(s.playhead());
    const double screen = (at - s.scrollFrame) * s.pxPerFrame;
    s.pxPerFrame = std::clamp(s.pxPerFrame * factor, 0.01, 80.0);
    s.scrollFrame = std::max(0.0, at - screen / s.pxPerFrame);
}

void fit_timeline(Studio& s) {
    s.pxPerFrame = s.timelineWidth / double(std::max<i64>(1, s.duration())) * .95;
    s.scrollFrame = 0;
}

void set_preview_scale(Studio& s, int choice) {
    s.previewScale = choice;
    Command c;
    c.type = CommandType::ViewportSetPreviewScale;
    const u32 den[] = {1, 1, 2, 4, 8};
    c.preview_scale = {1, den[std::clamp(choice, 0, 4)], choice == 0};
    s.send(c);
}

void open_export(Studio& s) {
    if (!s.exporter.path[0]) {
        wchar_t videos[MAX_PATH]{};
        std::wstring base = GetEnvironmentVariableW(L"USERPROFILE", videos, MAX_PATH) ? std::wstring(videos) + L"\\Videos\\" : L"";
        const std::string p = utf8(base + s.title + L".mp4");
        std::snprintf(s.exporter.path, sizeof s.exporter.path, "%s", p.c_str());
    }
    s.exporter.open = true;
}

} // namespace

const char* workspace_name(Workspace ws) {
    switch (ws) {
    case Workspace::Assembly: return "Montagem";
    case Workspace::Editing: return "Edição";
    case Workspace::Effects: return "Efeitos";
    case Workspace::Audio: return "Áudio";
    case Workspace::Graphics: return "Gráficos";
    default: return "";
    }
}

void build_layout(Workspace ws, ImGuiID id) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, ImVec2(vp->WorkSize.x, vp->WorkSize.y - S(60)));
    const float w = std::max(800.f, vp->WorkSize.x);
    auto dock = [](const char* name, ImGuiID node) { ImGui::DockBuilderDockWindow(name, node); };
    auto compact = [](ImGuiID node) { if (auto* n = ImGui::DockBuilderGetNode(node)) n->LocalFlags |= ImGuiDockNodeFlags_AutoHideTabBar; };
    ImGuiID top = 0, bottom = 0, topL = 0, topR = 0, proj = 0, rest = 0, tools = 0, rest2 = 0, meters = 0, timeline = 0, right = 0, main = 0;
    switch (ws) {
    case Workspace::Assembly:
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Down, .42f, &bottom, &top);
        ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, .50f, &topL, &topR);
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, S(42) / w, &tools, &rest);
        ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, S(70) / w, &meters, &timeline);
        dock("Projeto", topL); dock("Efeitos", topL); dock("Marcadores", topL);
        dock("###Programa", topR);
        dock("Ferramentas", tools); dock("Medidores de áudio", meters);
        dock("Linha do tempo", timeline); dock("Editor de gráfico", timeline);
        dock("Controles de efeito", topL); dock("Mixer de clipes de áudio", topL); dock("Histórico", topL); dock("Informações", topL);
        compact(tools); compact(meters);
        break;
    case Workspace::Effects:
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, .20f, &right, &main);
        ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, .44f, &bottom, &top);
        ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, .46f, &topL, &topR);
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, S(42) / w, &tools, &timeline);
        dock("Controles de efeito", topL); dock("Mixer de clipes de áudio", topL);
        dock("###Programa", topR);
        dock("Editor de gráfico", timeline); dock("Linha do tempo", timeline);
        dock("Efeitos", right); dock("Projeto", right); dock("Informações", right); dock("Marcadores", right); dock("Histórico", right);
        dock("Ferramentas", tools);
        dock("Medidores de áudio", right);
        compact(tools);
        break;
    case Workspace::Audio:
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Down, .42f, &bottom, &top);
        ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, .50f, &topL, &topR);
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, .22f, &proj, &rest);
        ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, S(90) / w, &meters, &timeline);
        dock("Mixer de clipes de áudio", topL); dock("Controles de efeito", topL);
        dock("###Programa", topR);
        dock("Projeto", proj); dock("Efeitos", proj); dock("Marcadores", proj); dock("Histórico", proj); dock("Informações", proj);
        dock("Linha do tempo", timeline); dock("Editor de gráfico", timeline);
        dock("Medidores de áudio", meters); dock("Ferramentas", proj);
        compact(meters);
        break;
    case Workspace::Graphics:
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, .26f, &right, &main);
        ImGui::DockBuilderSplitNode(main, ImGuiDir_Down, .40f, &bottom, &top);
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, .24f, &proj, &rest);
        ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, S(42) / w, &tools, &timeline);
        dock("###Programa", top);
        dock("Controles de efeito", right); dock("Efeitos", right); dock("Informações", right);
        dock("Projeto", proj); dock("Marcadores", proj); dock("Histórico", proj); dock("Mixer de clipes de áudio", proj);
        dock("Linha do tempo", timeline); dock("Editor de gráfico", timeline);
        dock("Ferramentas", tools); dock("Medidores de áudio", right);
        compact(tools);
        break;
    default:  // Edição
        ImGui::DockBuilderSplitNode(id, ImGuiDir_Down, .46f, &bottom, &top);
        ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, .44f, &topL, &topR);
        ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, .25f, &proj, &rest);
        ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, S(42) / (w * .75f), &tools, &rest2);
        ImGui::DockBuilderSplitNode(rest2, ImGuiDir_Right, S(70) / (w * .72f), &meters, &timeline);
        dock("Controles de efeito", topL); dock("Mixer de clipes de áudio", topL); dock("Informações", topL);
        dock("###Programa", topR);
        dock("Projeto", proj); dock("Efeitos", proj); dock("Marcadores", proj); dock("Histórico", proj);
        dock("Ferramentas", tools);
        dock("Linha do tempo", timeline); dock("Editor de gráfico", timeline);
        dock("Medidores de áudio", meters);
        compact(tools); compact(meters);
        break;
    }
    ImGui::DockBuilderFinish(id);
}

// --- Menus -------------------------------------------------------------------
void draw_menu(Studio& s) {
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, col::Stage);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(8), S(5)));
    if (ImGui::BeginMainMenuBar()) {
        ImGui::PopStyleVar();
        const bool exporting = s.engine.export_progress().running;
        if (ImGui::BeginMenu("Arquivo")) {
            if (ImGui::MenuItem("Novo projeto", "Ctrl+Alt+N")) s.new_project();
            if (ImGui::MenuItem("Abrir projeto…", "Ctrl+O")) s.open_project();
            ImGui::Separator();
            if (ImGui::MenuItem("Salvar", "Ctrl+S")) s.save();
            if (ImGui::MenuItem("Salvar como…", "Ctrl+Shift+S")) s.save(true);
            ImGui::Separator();
            if (ImGui::MenuItem("Importar…", "Ctrl+I")) s.import_dialog();
            if (ImGui::BeginMenu("Novo item")) { new_item_menu(s); ImGui::EndMenu(); }
            ImGui::Separator();
            if (ImGui::MenuItem("Exportar mídia…", "Ctrl+M", false, !exporting)) open_export(s);
            if (ImGui::MenuItem("Exportar quadro…", "Ctrl+Shift+E")) s.export_frame();
            if (exporting && ImGui::MenuItem("Cancelar exportação")) (void)s.engine.cancel_export();
            ImGui::Separator();
            if (ImGui::MenuItem("Sair", "Alt+F4")) PostMessageW(s.window, WM_CLOSE, 0, 0);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Editar")) {
            if (ImGui::MenuItem("Desfazer", "Ctrl+Z", false, s.status.canUndo)) s.command(CommandType::Undo);
            if (ImGui::MenuItem("Refazer", "Ctrl+Shift+Z", false, s.status.canRedo)) s.command(CommandType::Redo);
            ImGui::Separator();
            if (ImGui::MenuItem("Copiar", "Ctrl+C", false, !s.selection.empty())) s.engine.copy_layers(s.selection.data(), u32(s.selection.size()));
            if (ImGui::MenuItem("Colar", "Ctrl+V")) s.engine.paste_layers(s.playhead());
            if (ImGui::MenuItem("Duplicar", "Ctrl+D", false, !s.selection.empty())) duplicate_selection(s);
            if (ImGui::MenuItem("Limpar", "Del", false, !s.selection.empty() || !s.selectedKeys.empty())) s.delete_selection();
            ImGui::Separator();
            if (ImGui::MenuItem("Selecionar tudo", "Ctrl+A")) select_all(s);
            if (ImGui::MenuItem("Desmarcar tudo", "Ctrl+Shift+A")) s.set_selection({});
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Clipe")) {
            const bool any = !s.selection.empty();
            if (ImGui::MenuItem("Ativar / desativar", "Shift+E", false, any)) toggle_enable(s);
            if (ImGui::MenuItem("Aninhar…", nullptr, false, any)) {
                auto r = s.engine.precompose(s.selection.data(), u32(s.selection.size()), "Sequência aninhada");
                if (r.ok()) s.select(*r); else s.error(r.status());
            }
            if (ImGui::MenuItem("Desaninhar", nullptr, false, any)) { auto r = s.engine.ungroup_precomps(s.selection.data(), u32(s.selection.size())); s.error(r.status()); }
            if (ImGui::MenuItem("Extrair áudio", nullptr, false, s.detail.kind == u32(LayerKind::Video))) { auto r = s.engine.extract_audio(s.primary); s.error(r.status()); }
            ImGui::Separator();
            if (ImGui::MenuItem("Copiar estilo", nullptr, false, s.primary != 0)) s.engine.copy_style(s.primary);
            if (ImGui::MenuItem("Colar estilo", nullptr, false, any && s.engine.copied_style_parts())) s.engine.paste_style_parts(s.selection.data(), u32(s.selection.size()), ~0u);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Sequência")) {
            if (ImGui::MenuItem("Configurações da sequência…")) {
                u64 id = 0; u32 w = 0, h = 0; f64 f = 0; i64 d = 0; f32 bg[4]{};
                if (s.engine.query_composition(id, w, h, f, d, bg)) {
                    s.sequence = {true, int(w), int(h), float(f), float(d / std::max(1.0, f)), {linear_to_srgb(bg[0]), linear_to_srgb(bg[1]), linear_to_srgb(bg[2]), bg[3]}};
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Adicionar edição", "Ctrl+K")) s.split_at_playhead();
            if (ImGui::MenuItem("Encaixar na linha do tempo", "S", s.snap)) s.snap = !s.snap;
            ImGui::Separator();
            if (ImGui::MenuItem("Aproximar", "=")) zoom_timeline(s, 1.25);
            if (ImGui::MenuItem("Afastar", "-")) zoom_timeline(s, 1 / 1.25);
            if (ImGui::MenuItem("Ajustar à sequência", "\\")) fit_timeline(s);
            if (s.engine.precomp_depth() > 0 && ImGui::MenuItem("Voltar à sequência principal")) { s.engine.close_precomp(); s.revision = ~0u; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Marcadores")) {
            if (ImGui::MenuItem("Adicionar marcador", "M")) s.add_marker();
            if (ImGui::MenuItem("Ir para o próximo", "Shift+M")) s.navigate(1);
            if (ImGui::MenuItem("Ir para o anterior", "Ctrl+Shift+M")) s.navigate(-1);
            if (ImGui::MenuItem("Limpar todos os marcadores")) { for (size_t i = 0; i < s.markers.size(); i += 3) s.engine.delete_marker(s.markers[i]); s.revision = ~0u; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Gráficos")) { new_item_menu(s); ImGui::EndMenu(); }
        if (ImGui::BeginMenu("Exibir")) {
            const char* scales[] = {"Automática", "Completa", "1/2", "1/4", "1/8"};
            if (ImGui::BeginMenu("Resolução da prévia")) {
                for (int i = 0; i < 5; ++i) if (ImGui::MenuItem(scales[i], nullptr, s.previewScale == i)) set_preview_scale(s, i);
                ImGui::EndMenu();
            }
            ImGui::MenuItem("Mostrar desempenho na barra de status", nullptr, &s.showPerf);
            ImGui::MenuItem("Auto-keyframe ao editar", nullptr, &s.autoKey);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Janela")) {
            if (ImGui::BeginMenu("Espaços de trabalho")) {
                for (int i = 0; i < int(Workspace::Count); ++i)
                    if (ImGui::MenuItem(workspace_name(Workspace(i)), nullptr, s.workspace == Workspace(i))) s.pendingWorkspace = Workspace(i);
                ImGui::Separator();
                if (ImGui::MenuItem("Redefinir para o layout salvo", "Alt+Shift+0")) s.resetLayout = true;
                ImGui::EndMenu();
            }
            ImGui::Separator();
            for (const char* name : {"Projeto", "Efeitos", "Controles de efeito", "Mixer de clipes de áudio", "Programa", "Linha do tempo", "Editor de gráfico",
                                     "Ferramentas", "Medidores de áudio", "Marcadores", "Histórico", "Informações"})
                if (ImGui::MenuItem(name)) ImGui::SetWindowFocus(std::string(name) == "Programa" ? "###Programa" : name);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Ajuda")) {
            if (ImGui::MenuItem("Atalhos de teclado", "F1")) s.showShortcuts = true;
            if (ImGui::MenuItem("Sobre o AUREA Studio")) s.showAbout = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    } else ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void draw_header(Studio& s) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    dl->AddLine(ImVec2(a.x, a.y + size.y - 1), ImVec2(a.x + size.x, a.y + size.y - 1), col::Border);
    // Marca: losango AUREA e nome.
    const float cy = a.y + size.y * .5f;
    const ImVec2 c(a.x + S(22), cy);
    dl->AddQuadFilled(ImVec2(c.x, c.y - S(9)), ImVec2(c.x + S(9), c.y), ImVec2(c.x, c.y + S(9)), ImVec2(c.x - S(9), c.y), col::Brand);
    dl->AddQuadFilled(ImVec2(c.x, c.y - S(5)), ImVec2(c.x + S(5), c.y), ImVec2(c.x, c.y + S(5)), ImVec2(c.x - S(5), c.y), col::Accent);
    ImGui::SetCursorScreenPos(ImVec2(a.x + S(40), cy - ImGui::GetFontSize() * .5f - S(1)));
    ImGui::PushFont(gFontBold, gFontBold->LegacySize);
    ImGui::TextUnformatted("AUREA");
    ImGui::PopFont();
    ImGui::SameLine(0, S(4));
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Muted), "Studio");
    // Espaços de trabalho no centro.
    float total = 0;
    for (int i = 0; i < int(Workspace::Count); ++i) total += ImGui::CalcTextSize(workspace_name(Workspace(i))).x + S(28);
    float x = a.x + (size.x - total) * .5f;
    for (int i = 0; i < int(Workspace::Count); ++i) {
        const char* name = workspace_name(Workspace(i));
        const float w = ImGui::CalcTextSize(name).x + S(28);
        ImGui::SetCursorScreenPos(ImVec2(x, a.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("ws", ImVec2(w, size.y - 1))) s.pendingWorkspace = Workspace(i);
        const bool hovered = ImGui::IsItemHovered(), active = s.workspace == Workspace(i);
        ImGui::PopID();
        dl->AddText(ImVec2(x + S(14), cy - ImGui::GetFontSize() * .5f), active ? col::Accent : hovered ? col::Text : col::Muted, name);
        if (active) dl->AddRectFilled(ImVec2(x + S(10), a.y + size.y - S(3)), ImVec2(x + w - S(10), a.y + size.y - 1), col::Accent, S(1));
        x += w;
    }
    // Direita: projeto e exportar.
    const std::string title = utf8(s.title) + (s.status.dirty ? " *" : "");
    const float ew = S(96);
    ImGui::SetCursorScreenPos(ImVec2(a.x + size.x - ew - S(10), cy - ImGui::GetFrameHeight() * .5f));
    ImGui::PushStyleColor(ImGuiCol_Button, col::Brand);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col::mix(col::Brand, col::Accent, .35f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col::BrandDeep);
    const bool exporting = s.engine.export_progress().running;
    if (ImGui::Button(exporting ? "Exportando…" : "Exportar", ImVec2(ew, 0))) open_export(s);
    ImGui::PopStyleColor(3);
    tooltip("Exportar mídia (Ctrl+M)");
    const float tw = ImGui::CalcTextSize(title.c_str()).x;
    dl->AddText(ImVec2(a.x + size.x - ew - S(26) - tw, cy - ImGui::GetFontSize() * .5f), col::Muted, title.c_str());
}

// --- Projeto ---------------------------------------------------------------
void draw_project(Studio& s) {
    if (!ImGui::Begin("Projeto")) { ImGui::End(); return; }
    ImGui::PushFont(gFontBold, gFontBold->LegacySize);
    ImGui::TextUnformatted((utf8(s.title) + ".aurea").c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu itens", s.layers.size());
    static char filter[128]{};
    ImGui::SetNextItemWidth(std::max(S(80), ImGui::GetContentRegionAvail().x - S(4) * 4 - ImGui::GetFrameHeight() * 4));
    ImGui::InputTextWithHint("##find", "Pesquisar", filter, sizeof filter);
    ImGui::SameLine(0, S(4));
    if (icon_button("list", Icon::List, "Exibição de lista", !s.projectIcons)) s.projectIcons = false;
    ImGui::SameLine(0, S(2));
    if (icon_button("grid", Icon::Grid, "Exibição de ícones", s.projectIcons)) s.projectIcons = true;
    ImGui::SameLine(0, S(2));
    if (icon_button("new", Icon::Plus, "Novo item")) ImGui::OpenPopup("##newitem");
    ImGui::SameLine(0, S(2));
    if (icon_button("import", Icon::Folder, "Importar (Ctrl+I)")) s.import_dialog();
    if (ImGui::BeginPopup("##newitem")) { new_item_menu(s); ImGui::EndPopup(); }
    std::string f = filter;
    for (char& ch : f) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    auto matches = [&](const LayerView& l) {
        if (f.empty()) return true;
        std::string n = l.name;
        for (char& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
        return n.find(f) != std::string::npos;
    };
    auto kind_name = [](const LayerView& l) {
        switch (LayerKind(l.row.kind)) {
        case LayerKind::Video: return "Vídeo"; case LayerKind::Image: return "Imagem"; case LayerKind::Audio: return "Áudio";
        case LayerKind::Text: return "Texto"; case LayerKind::Shape: return "Forma"; case LayerKind::Null: return "Nulo";
        case LayerKind::Camera: return "Câmera"; case LayerKind::Light: return "Luz"; case LayerKind::Model3D: return "Modelo 3D";
        case LayerKind::ParticleSystem: return "Partículas"; case LayerKind::Composition: return "Sequência"; case LayerKind::Adjustment: return "Ajuste";
        default: return "Item";
        }
    };
    if (s.layers.empty()) {
        ImGui::Dummy(ImVec2(1, S(20)));
        ImGui::TextDisabled("Importe mídia para iniciar.");
        ImGui::TextDisabled("Ctrl+I, ou arraste arquivos do Explorer para a janela.");
        if (ImGui::Button("Importar mídia…")) s.import_dialog();
    } else if (s.projectIcons) {
        const float cell = S(132), thumbH = S(74);
        const int columns = std::max(1, int(ImGui::GetContentRegionAvail().x / (cell + S(8))));
        int n = 0;
        ImGui::BeginChild("##icons");
        for (auto& l : s.layers) {
            if (!matches(l)) continue;
            if (n++ % columns) ImGui::SameLine(0, S(8));
            ImGui::PushID(int(l.row.id & 0x7fffffff));
            const ImVec2 a = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool sel = s.is_selected(l.row.id);
            if (ImGui::InvisibleButton("item", ImVec2(cell, thumbH + S(34)))) s.select(l.row.id, ImGui::GetIO().KeyCtrl);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) s.seek(l.row.startFrame);
            dl->AddRectFilled(a, ImVec2(a.x + cell, a.y + thumbH), col::Stage, S(3));
            u32 tw = 0, th = 0;
            ID3D11ShaderResourceView* srv = (l.row.kind == u32(LayerKind::Video) || l.row.kind == u32(LayerKind::Image)) ? s.thumbnail(l.row.id, l.row.startFrame, u32(thumbH), &tw, &th) : nullptr;
            if (srv) {
                const float w = std::min(cell, thumbH * tw / std::max(1u, th)), x = a.x + (cell - w) * .5f;
                dl->AddImage(ImTextureRef(ImTextureID(srv)), ImVec2(x, a.y), ImVec2(x + w, a.y + thumbH));
            } else draw_icon(dl, layer_icon(l), ImVec2(a.x + cell * .5f, a.y + thumbH * .5f), S(30), clip_color(l));
            const std::string dur = s.timecode(l.row.endFrame - l.row.startFrame);
            const ImVec2 ds = ImGui::CalcTextSize(dur.c_str());
            dl->AddRectFilled(ImVec2(a.x + cell - ds.x - S(8), a.y + thumbH - ds.y - S(4)), ImVec2(a.x + cell - S(2), a.y + thumbH - S(2)), col::alpha(col::Stage, .8f), S(2));
            dl->AddText(ImVec2(a.x + cell - ds.x - S(5), a.y + thumbH - ds.y - S(3)), col::Text, dur.c_str());
            if (sel) dl->AddRect(a, ImVec2(a.x + cell, a.y + thumbH), col::Accent, S(3), S(2));
            dl->AddRectFilled(ImVec2(a.x, a.y + thumbH + S(6)), ImVec2(a.x + S(4), a.y + thumbH + S(20)), clip_color(l));
            dl->PushClipRect(ImVec2(a.x, a.y + thumbH), ImVec2(a.x + cell, a.y + thumbH + S(34)), true);
            dl->AddText(ImVec2(a.x + S(8), a.y + thumbH + S(5)), sel ? col::Text : col::Muted, l.name.c_str());
            dl->PopClipRect();
            ImGui::PopID();
        }
        ImGui::EndChild();
    } else if (ImGui::BeginTable("##items", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch, 3.f);
        ImGui::TableSetupColumn("Tipo", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::TableSetupColumn("Início", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Duração", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Efeitos", ImGuiTableColumnFlags_WidthStretch, .8f);
        ImGui::TableHeadersRow();
        for (auto& l : s.layers) {
            if (!matches(l)) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(int(l.row.id & 0x7fffffff));
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetTextLineHeight();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x, a.y + S(2)), ImVec2(a.x + S(3), a.y + h - S(1)), clip_color(l));
            draw_icon(ImGui::GetWindowDrawList(), layer_icon(l), ImVec2(a.x + S(14), a.y + h * .5f), h * .8f, col::Muted);
            ImGui::SetCursorScreenPos(ImVec2(a.x + S(26), a.y));
            if (ImGui::Selectable(l.name.c_str(), s.is_selected(l.row.id), ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                s.select(l.row.id, ImGui::GetIO().KeyCtrl);
                if (ImGui::IsMouseDoubleClicked(0)) s.seek(l.row.startFrame);
            }
            if (ImGui::BeginPopupContextItem()) {
                if (!s.is_selected(l.row.id)) s.select(l.row.id);
                if (ImGui::MenuItem("Ir para o início do clipe")) s.seek(l.row.startFrame);
                if (l.row.kind == u32(LayerKind::Composition) && ImGui::MenuItem("Abrir sequência aninhada")) { s.engine.open_precomp(l.row.id); s.revision = ~0u; }
                const std::string path = s.engine.layer_source_path(l.row.id);
                if (!path.empty() && ImGui::MenuItem("Revelar no Explorer")) {
                    const std::wstring arg = L"/select,\"" + wide(path) + L"\"";
                    ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Limpar")) s.delete_selection();
                ImGui::EndPopup();
            }
            ImGui::PopID();
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", kind_name(l));
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", s.timecode(l.row.startFrame).c_str());
            ImGui::TableNextColumn(); ImGui::TextDisabled("%s", s.timecode(l.row.endFrame - l.row.startFrame).c_str());
            ImGui::TableNextColumn(); if (l.row.effectCount) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Warning), "fx %u", l.row.effectCount);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// --- Navegador de efeitos ------------------------------------------------------
void draw_effects_browser(Studio& s) {
    if (!ImGui::Begin("Efeitos")) { ImGui::End(); return; }
    static char filter[128]{};
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##fxfind", "Pesquisar efeitos", filter, sizeof filter);
    std::string f = filter;
    for (char& ch : f) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    ImGui::BeginChild("##fxtree");
    auto item = [&](const CatalogItem& c) {
        ImGui::PushID(int(c.type));
        const ImVec2 a = ImGui::GetCursorScreenPos();
        draw_icon(ImGui::GetWindowDrawList(), Icon::Fx, ImVec2(a.x + S(9), a.y + ImGui::GetTextLineHeight() * .5f), S(13), col::Accent);
        ImGui::SetCursorScreenPos(ImVec2(a.x + S(22), a.y));
        if (ImGui::Selectable(c.name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(0)) s.apply_effect(c.type);
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("AUREA_FX", &c.type, sizeof c.type);
            ImGui::Text("fx  %s", c.name.c_str());
            ImGui::EndDragDropSource();
        }
        tooltip("Clique duplo aplica ao clipe selecionado; ou arraste para um clipe.");
        if (c.fresh) { ImGui::SameLine(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Beta), "NOVO"); }
        ImGui::PopID();
    };
    if (!f.empty()) {
        for (auto& c : s.catalog) {
            std::string n = c.name + " " + c.category;
            for (char& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            if (n.find(f) != std::string::npos) item(c);
        }
    } else {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if (ImGui::TreeNodeEx("Efeitos de vídeo", ImGuiTreeNodeFlags_SpanAvailWidth)) {
            for (auto& cat : s.categories) {
                if (ImGui::TreeNodeEx(cat.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    for (auto& c : s.catalog) if (c.category == cat) item(c);
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void draw_markers(Studio& s) {
    if (!ImGui::Begin("Marcadores")) { ImGui::End(); return; }
    if (s.markers.empty()) ImGui::TextDisabled("Sem marcadores. Pressione M para adicionar no indicador.");
    if (ImGui::BeginTable("##marks", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Tempo", ImGuiTableColumnFlags_WidthFixed, S(90));
        ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, S(26));
        for (size_t i = 0; i < s.markers.size(); i += 3) {
            const i64 frame = s.markers[i];
            ImGui::TableNextRow();
            ImGui::PushID(int(i));
            ImGui::TableNextColumn();
            if (ImGui::Selectable(s.timecode(frame).c_str(), frame == s.playhead())) s.seek(frame);
            ImGui::TableNextColumn();
            static std::map<i64, std::string> buffers;
            auto& label = buffers[frame];
            if (label.empty()) label = s.engine.marker_label(frame);
            char b[128];
            std::snprintf(b, sizeof b, "%s", label.c_str());
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputTextWithHint("##l", "Nome do marcador", b, sizeof b)) label = b;
            if (ImGui::IsItemDeactivatedAfterEdit()) { s.engine.edit_marker(frame, frame, u32(s.markers[i + 1]), label); s.revision = ~0u; }
            ImGui::TableNextColumn();
            if (icon_button("del", Icon::Trash, "Remover marcador", false, ImGui::GetTextLineHeight() + S(4))) { s.engine.delete_marker(frame); s.revision = ~0u; buffers.erase(frame); }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void draw_history(Studio& s) {
    if (!ImGui::Begin("Histórico")) { ImGui::End(); return; }
    ImGui::TextDisabled("%u etapas para desfazer", s.status.undoDepth);
    ImGui::BeginDisabled(!s.status.canUndo);
    if (icon_button("undo", Icon::Undo, "Desfazer (Ctrl+Z)")) s.command(CommandType::Undo);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.status.canRedo);
    if (icon_button("redo", Icon::Redo, "Refazer (Ctrl+Shift+Z)")) s.command(CommandType::Redo);
    ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::TextDisabled("Cada arrasto, valor digitado ou efeito aplicado é uma etapa.");
    ImGui::End();
}

void draw_info(Studio& s) {
    if (!ImGui::Begin("Informações")) { ImGui::End(); return; }
    auto row = [](const char* k, const std::string& v) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Muted), "%s", k);
        ImGui::SameLine(S(110));
        ImGui::TextUnformatted(v.c_str());
    };
    if (auto* l = s.layer(s.primary)) {
        ImGui::PushFont(gFontBold, gFontBold->LegacySize);
        ImGui::TextUnformatted(l->name.c_str());
        ImGui::PopFont();
        row("Início", s.timecode(l->row.startFrame));
        row("Fim", s.timecode(l->row.endFrame));
        row("Duração", s.timecode(l->row.endFrame - l->row.startFrame));
        if (s.detail.sourceWidth) row("Mídia", std::to_string(s.detail.sourceWidth) + " × " + std::to_string(s.detail.sourceHeight) + (s.detail.sourceFps > 0 ? "  " + format_value(s.detail.sourceFps, 2) + " qps" : ""));
        row("Efeitos", std::to_string(l->row.effectCount));
        row("Keyframes", std::to_string(l->row.keyframeCount));
        const std::string path = s.engine.layer_source_path(l->row.id);
        if (!path.empty()) { ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Muted), "Arquivo"); ImGui::PushTextWrapPos(); ImGui::TextDisabled("%s", path.c_str()); ImGui::PopTextWrapPos(); }
        ImGui::Separator();
    }
    ImGui::PushFont(gFontBold, gFontBold->LegacySize);
    ImGui::TextUnformatted(s.engine.current_composition_name().c_str());
    ImGui::PopFont();
    row("Quadro", std::to_string(s.status.compWidth) + " × " + std::to_string(s.status.compHeight));
    row("Taxa", format_value(float(s.fps()), 3) + " qps");
    row("Duração", s.timecode(s.duration()));
    row("Indicador", s.timecode(s.playhead()));
    ImGui::Separator();
    row("Prévia", std::to_string(s.status.previewWidth) + " × " + std::to_string(s.status.previewHeight));
    row("Desempenho", format_value(s.status.currentFps, 1) + " qps · " + format_value(s.status.averageFrameMs, 1) + " ms");
    row("GPU / CPU", format_value(s.status.gpuMs, 1) + " / " + format_value(s.status.cpuMs, 1) + " ms");
    row("Decodificação", format_value(s.status.decodeMs, 1) + " ms");
    row("Cache", format_value(s.status.cacheHitRate * 100, 0) + "% de acertos");
    row("Memória GPU", std::to_string(s.status.gpuMemoryBytes >> 20) + " MB");
    row("Quadros perdidos", std::to_string(s.status.droppedFrames));
    ImGui::End();
}

// --- Programa (prévia do motor) ----------------------------------------------
void draw_program(Studio& s) {
    char title[200];
    std::snprintf(title, sizeof title, "Programa: %s###Programa", s.engine.current_composition_name().c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const bool open = ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    if (!open || ImGui::GetCurrentWindow()->Hidden) { ImGui::End(); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    const float controlsH = S(76);
    const ImVec2 v0 = o, v1(o.x + avail.x, o.y + std::max(S(40), avail.y - controlsH));
    dl->AddRectFilled(v0, v1, col::Stage);
    // A janela filha cobre exatamente esta área; o motor desenha nela.
    s.previewRect = {LONG(std::lround(v0.x)), LONG(std::lround(v0.y)), LONG(std::lround(v1.x)), LONG(std::lround(v1.y))};
    s.previewWanted = v1.x - v0.x > 8 && v1.y - v0.y > 8;
    ImGui::SetCursorScreenPos(v0);
    ImGui::InvisibleButton("##view", ImVec2(v1.x - v0.x, v1.y - v0.y));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("AUREA_FX")) s.apply_effect(*static_cast<const u32*>(p->Data));
        ImGui::EndDragDropTarget();
    }
    // Controles abaixo da imagem.
    const float y0 = v1.y;
    dl->AddRectFilled(ImVec2(o.x, y0), ImVec2(o.x + avail.x, o.y + avail.y), col::Surface);
    ImGui::SetCursorScreenPos(ImVec2(o.x + S(10), y0 + S(4)));
    ImGui::PushFont(gFontTimecode, 15.f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Accent), "%s", s.timecode(s.playhead()).c_str());
    ImGui::PopFont();
    ImGui::SameLine(0, S(14));
    ImGui::SetNextItemWidth(S(96));
    const char* fit[] = {"Ajustar"};
    int fitChoice = 0;
    ImGui::Combo("##fit", &fitChoice, fit, 1);
    tooltip("Nível de zoom da prévia");
    ImGui::SameLine(0, S(6));
    ImGui::SetNextItemWidth(S(104));
    const char* scales[] = {"Automática", "Completa", "1/2", "1/4", "1/8"};
    int sc = s.previewScale;
    if (ImGui::Combo("##res", &sc, scales, 5)) set_preview_scale(s, sc);
    tooltip("Resolução de reprodução: menor = mais leve em mídias pesadas");
    const std::string dur = s.timecode(s.duration());
    ImGui::SameLine(o.x + avail.x - ImGui::CalcTextSize(dur.c_str()).x * 1.15f - S(12) - ImGui::GetWindowPos().x);
    ImGui::PushFont(gFontTimecode, 15.f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Muted), "%s", dur.c_str());
    ImGui::PopFont();
    // Barra de navegação (toda a sequência).
    const float by = y0 + S(32);
    const ImRect bar(ImVec2(o.x + S(10), by), ImVec2(o.x + avail.x - S(10), by + S(10)));
    dl->AddRectFilled(ImVec2(bar.Min.x, bar.Min.y + S(4)), ImVec2(bar.Max.x, bar.Min.y + S(6)), col::Chip, S(1));
    for (size_t i = 0; i < s.markers.size(); i += 3) {
        const float mx = bar.Min.x + float(s.markers[i]) / float(s.duration()) * bar.GetWidth();
        dl->AddRectFilled(ImVec2(mx - 1, bar.Min.y), ImVec2(mx + 1, bar.Max.y), col::Warning);
    }
    const float hx = bar.Min.x + float(s.playhead()) / float(std::max<i64>(1, s.duration() - 1)) * bar.GetWidth();
    dl->AddTriangleFilled(ImVec2(hx - S(5), bar.Min.y - S(2)), ImVec2(hx + S(5), bar.Min.y - S(2)), ImVec2(hx, bar.Min.y + S(5)), col::Accent);
    dl->AddLine(ImVec2(hx, bar.Min.y), ImVec2(hx, bar.Max.y), col::Accent, S(1.5f));
    ImGui::SetCursorScreenPos(ImVec2(bar.Min.x, bar.Min.y - S(3)));
    ImGui::InvisibleButton("##navbar", ImVec2(bar.GetWidth(), bar.GetHeight() + S(6)));
    if (ImGui::IsItemActivated()) s.command(CommandType::PlaybackScrubBegin);
    if (ImGui::IsItemActive()) {
        Command c;
        c.type = CommandType::PlaybackScrub;
        const double t = std::clamp((ImGui::GetIO().MousePos.x - bar.Min.x) / bar.GetWidth(), 0.f, 1.f);
        c.seek.time = tick_at(FrameIndex{i64(std::llround(t * double(s.duration() - 1)))}, s.fps());
        s.send(c);
    }
    if (ImGui::IsItemDeactivated()) s.command(CommandType::PlaybackScrubEnd);
    // Transporte.
    const float b = S(26);
    const int buttons = 9;
    ImGui::SetCursorScreenPos(ImVec2(o.x + (avail.x - buttons * (b + S(4))) * .5f, by + S(14)));
    auto step = [&](int frames) { Command c; c.type = CommandType::PlaybackStep; c.step.frames = frames; s.send(c); };
    if (icon_button("mark", Icon::Marker, "Adicionar marcador (M)", false, b)) s.add_marker();
    ImGui::SameLine(0, S(4));
    if (icon_button("prev", Icon::ToStart, "Ir para o ponto de edição anterior (Seta para cima)", false, b)) s.navigate(-1);
    ImGui::SameLine(0, S(4));
    if (icon_button("back", Icon::StepBack, "Voltar 1 quadro (Seta para a esquerda)", false, b)) step(-1);
    ImGui::SameLine(0, S(4));
    if (icon_button("play", s.status.playing ? Icon::Pause : Icon::Play, "Reproduzir / parar (Espaço)", s.status.playing, b)) s.command(CommandType::PlaybackToggle);
    ImGui::SameLine(0, S(4));
    if (icon_button("fwd", Icon::StepForward, "Avançar 1 quadro (Seta para a direita)", false, b)) step(1);
    ImGui::SameLine(0, S(4));
    if (icon_button("next", Icon::ToEnd, "Ir para o próximo ponto de edição (Seta para baixo)", false, b)) s.navigate(1);
    ImGui::SameLine(0, S(4));
    if (icon_button("loop", Icon::Loop, "Repetir reprodução", s.loop, b)) { s.loop = !s.loop; Command c; c.type = CommandType::PlaybackSetLoop; c.loop.loop = s.loop; s.send(c); }
    ImGui::SameLine(0, S(4));
    if (icon_button("frame", Icon::Camera, "Exportar quadro (Ctrl+Shift+E)", false, b)) s.export_frame();
    ImGui::SameLine(0, S(4));
    if (icon_button("export", Icon::Export, "Exportar mídia (Ctrl+M)", false, b)) open_export(s);
    ImGui::End();
}

void draw_tools(Studio& s) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(4), S(6)));
    const bool open = ImGui::Begin("Ferramentas", nullptr, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (!open) { ImGui::End(); return; }
    struct T { Tool tool; Icon icon; const char* tip; };
    const T tools[] = {
        {Tool::Select, Icon::Select, "Ferramenta Seleção (V)"}, {Tool::TrackForward, Icon::TrackForward, "Seleção de faixa para a frente (A)"},
        {Tool::Ripple, Icon::Ripple, "Edição em cascata (B)"}, {Tool::Razor, Icon::Razor, "Ferramenta Lâmina (C)"},
        {Tool::Slip, Icon::Slip, "Deslizar (Y)"}, {Tool::Pen, Icon::Pen, "Caneta (P)"},
        {Tool::Hand, Icon::Hand, "Ferramenta Mão (H)"}, {Tool::Type, Icon::Type, "Ferramenta Texto (T)"}};
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool vertical = avail.y > avail.x;
    const float b = std::min(S(30), vertical ? avail.x : avail.y);
    for (size_t i = 0; i < std::size(tools); ++i) {
        if (i && !vertical) ImGui::SameLine(0, S(2));
        if (vertical) ImGui::SetCursorPosX((ImGui::GetWindowWidth() - b) * .5f);
        if (icon_button(tools[i].tip, tools[i].icon, tools[i].tip, s.tool == tools[i].tool, b)) s.tool = tools[i].tool;
    }
    ImGui::End();
}

void draw_meters(Studio& s) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(4), S(6)));
    const bool open = ImGui::Begin("Medidores de áudio", nullptr, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (!open) { ImGui::End(); return; }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos(), avail = ImGui::GetContentRegionAvail();
    const float labelW = S(22), barW = std::clamp((avail.x - labelW - S(8)) * .5f, S(5), S(16));
    const float top = o.y + S(4), bottom = o.y + avail.y - S(14);
    auto Ydb = [&](float db) { return top + (std::clamp(-db, 0.f, 60.f) / 60.f) * (bottom - top); };
    for (int db = 0; db >= -54; db -= 6) {
        char b[8];
        std::snprintf(b, sizeof b, "%d", db);
        dl->AddText(gFontSmall, gFontSmall->LegacySize * s.dpi, ImVec2(o.x, Ydb(float(db)) - S(6)), col::Subtle, b);
    }
    for (int c = 0; c < 2; ++c) {
        const float x0 = o.x + labelW + c * (barW + S(3)), x1 = x0 + barW;
        dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, bottom), col::Stage);
        const float level = s.meterLevel[c];
        const float db = level > 1e-6f ? 20 * std::log10(level) : -90.f;
        const float y = Ydb(db);
        // Verde até -12 dB, amarelo até -6 dB, vermelho acima.
        const float yG = Ydb(-12), yY = Ydb(-6);
        if (y < bottom) dl->AddRectFilled(ImVec2(x0, std::max(y, yG)), ImVec2(x1, bottom), col::Success);
        if (y < yG) dl->AddRectFilled(ImVec2(x0, std::max(y, yY)), ImVec2(x1, yG), col::Warning);
        if (y < yY) dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, yY), col::Danger);
        const float peak = s.meterPeak[c];
        if (peak > 1e-6f) {
            const float py = Ydb(20 * std::log10(peak));
            dl->AddLine(ImVec2(x0, py), ImVec2(x1, py), col::Text, S(1.5f));
        }
        dl->AddRectFilled(ImVec2(x0, top - S(4)), ImVec2(x1, top - S(1)), db > -0.1f ? col::Danger : col::Chip);
        dl->AddText(gFontSmall, gFontSmall->LegacySize * s.dpi, ImVec2(x0 + barW * .5f - S(3), bottom + S(1)), col::Subtle, c ? "D" : "E");
    }
    ImGui::Dummy(avail);
    ImGui::End();
}

// --- Diálogos -------------------------------------------------------------------
void draw_dialogs(Studio& s) {
    if (s.exporter.open) { ImGui::OpenPopup("Exportar mídia"); s.exporter.open = false; }
    ImGui::SetNextWindowSize(ImVec2(S(560), 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Exportar mídia", nullptr, ImGuiWindowFlags_NoResize)) {
        auto& d = s.exporter;
        const auto progress = s.engine.export_progress();
        if (progress.running) {
            ImGui::TextUnformatted("Exportando a sequência…");
            const float frac = progress.framesTotal ? float(progress.framesDone) / float(progress.framesTotal) : 0.f;
            char label[96];
            std::snprintf(label, sizeof label, "%u / %u quadros", progress.framesDone, progress.framesTotal);
            ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), label);
            ImGui::TextDisabled("%.1f qps · restam cerca de %u s", double(progress.fps), progress.etaSeconds);
            if (progress.message[0]) ImGui::TextDisabled("%s", progress.message);
            if (ImGui::Button("Cancelar exportação", ImVec2(-FLT_MIN, 0))) (void)s.engine.cancel_export();
        } else {
            if (progress.finished) {
                if (progress.result == Errc::Ok) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Success), "Exportação concluída.");
                else ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Danger), "A exportação falhou: %s", progress.message);
                ImGui::Separator();
            }
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Muted), "Arquivo de saída");
            ImGui::SetNextItemWidth(-S(90));
            ImGui::InputText("##path", d.path, sizeof d.path);
            ImGui::SameLine();
            if (ImGui::Button("Procurar…", ImVec2(-FLT_MIN, 0))) {
                std::wstring buffer(4096, L'\0');
                const std::wstring current = wide(d.path);
                wcsncpy_s(buffer.data(), buffer.size(), current.c_str(), _TRUNCATE);
                OPENFILENAMEW ofn{sizeof(ofn)};
                ofn.hwndOwner = s.window;
                ofn.lpstrFilter = d.container ? L"QuickTime (*.mov)\0*.mov\0\0" : L"MPEG-4 (*.mp4)\0*.mp4\0\0";
                ofn.lpstrDefExt = d.container ? L"mov" : L"mp4";
                ofn.lpstrFile = buffer.data();
                ofn.nMaxFile = DWORD(buffer.size());
                ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
                if (GetSaveFileNameW(&ofn)) std::snprintf(d.path, sizeof d.path, "%s", utf8(buffer.c_str()).c_str());
            }
            ImGui::Spacing();
            const char* codecs[] = {"H.264", "HEVC (H.265)"};
            const char* containers[] = {"MP4", "MOV"};
            const char* res[] = {"Igual à sequência", "2160p (4K)", "1440p", "1080p", "720p"};
            const char* rates[] = {"Igual à sequência", "24", "25", "30", "50", "60"};
            const char* audio[] = {"AAC 128 kbps", "AAC 192 kbps", "AAC 320 kbps"};
            if (ImGui::BeginTable("##ex", 2)) {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, S(150));
                ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
                auto row = [](const char* k) { ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Muted), "%s", k); ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-FLT_MIN); };
                row("Formato"); ImGui::Combo("##codec", &d.codec, codecs, 2);
                row("Contêiner"); ImGui::Combo("##cont", &d.container, containers, 2);
                row("Resolução"); ImGui::Combo("##res", &d.resolution, res, 5);
                row("Taxa de quadros"); ImGui::Combo("##fps", &d.fps, rates, 6);
                row("Taxa de bits (Mbps)"); ImGui::SliderInt("##br", &d.bitrate, 0, 120, d.bitrate == 0 ? "Automática" : "%d Mbps");
                row("Áudio"); ImGui::Combo("##au", &d.audio, audio, 3);
                ImGui::EndTable();
            }
            const u32 shortSide = std::min(s.status.compWidth, s.status.compHeight);
            const u32 sides[] = {shortSide, 2160, 1440, 1080, 720};
            const u32 outShort = sides[std::clamp(d.resolution, 0, 4)];
            const double ratio = double(std::max(s.status.compWidth, s.status.compHeight)) / std::max(1u, shortSide);
            const u32 outLong = u32(std::lround(outShort * ratio)) & ~1u;
            const bool landscape = s.status.compWidth >= s.status.compHeight;
            ImGui::Spacing();
            ImGui::TextDisabled("Saída: %u × %u · %s qps · %s", landscape ? outLong : outShort, landscape ? outShort : outLong,
                               d.fps ? rates[d.fps] : format_value(float(s.fps()), 2).c_str(), s.timecode(s.engine.query_export_duration()).c_str());
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button, col::Brand);
            if (ImGui::Button("Exportar", ImVec2(S(120), 0))) s.start_export();
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Fechar", ImVec2(S(100), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (s.sequence.open) { ImGui::OpenPopup("Configurações da sequência"); s.sequence.open = false; }
    if (ImGui::BeginPopupModal("Configurações da sequência", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        auto& q = s.sequence;
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Muted), "Tamanho do quadro");
        ImGui::SetNextItemWidth(S(90)); ImGui::InputInt("##w", &q.width, 0);
        ImGui::SameLine(); ImGui::TextUnformatted("×"); ImGui::SameLine();
        ImGui::SetNextItemWidth(S(90)); ImGui::InputInt("##h", &q.height, 0);
        ImGui::SameLine();
        if (ImGui::SmallButton("16:9")) { q.width = 1920; q.height = 1080; }
        ImGui::SameLine(); if (ImGui::SmallButton("9:16")) { q.width = 1080; q.height = 1920; }
        ImGui::SameLine(); if (ImGui::SmallButton("1:1")) { q.width = 1080; q.height = 1080; }
        ImGui::SameLine(); if (ImGui::SmallButton("4K")) { q.width = 3840; q.height = 2160; }
        ImGui::SetNextItemWidth(S(120)); ImGui::InputFloat("Base de tempo (qps)", &q.fps, 0, 0, "%.3f");
        ImGui::SetNextItemWidth(S(120)); ImGui::InputFloat("Duração (s)", &q.seconds, 0, 0, "%.2f");
        ImGui::ColorEdit3("Cor de fundo", q.bg, ImGuiColorEditFlags_NoInputs);
        ImGui::Separator();
        if (ImGui::Button("OK", ImVec2(S(100), 0))) {
            const CompositionId comp = CompositionId::unpack(s.compId);
            s.begin_group();
            Command a; a.type = CommandType::CompositionSetSize; a.comp_size = {comp, u32(std::clamp(q.width, 16, 8192)) & ~1u, u32(std::clamp(q.height, 16, 8192)) & ~1u}; s.send(a);
            Command b; b.type = CommandType::CompositionSetFps; b.comp_fps = {comp, std::clamp(double(q.fps), 1.0, 240.0)}; s.send(b);
            Command c; c.type = CommandType::CompositionSetDuration; c.comp_duration = {comp, FrameIndex{std::max<i64>(1, std::llround(q.seconds * q.fps))}}; s.send(c);
            Command d; d.type = CommandType::CompositionSetBackground; d.comp_background = {comp, srgb_to_linear(q.bg[0]), srgb_to_linear(q.bg[1]), srgb_to_linear(q.bg[2]), 1}; s.send(d);
            s.end_group();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar", ImVec2(S(100), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (s.showShortcuts) { ImGui::OpenPopup("Atalhos de teclado"); s.showShortcuts = false; }
    if (ImGui::BeginPopupModal("Atalhos de teclado", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* rows[][2] = {
            {"Espaço", "Reproduzir / parar"}, {"J  K  L", "Voltar quadro · parar · reproduzir"}, {"← / →", "Quadro anterior / próximo (Shift: 5)"},
            {"↑ / ↓", "Ponto de edição anterior / próximo"}, {"Home / End", "Início / fim da sequência"}, {"M", "Adicionar marcador"},
            {"Ctrl+K", "Adicionar edição (dividir no indicador)"}, {"V A B C Y P H T", "Ferramentas"}, {"Del", "Limpar seleção"},
            {"Ctrl+C / Ctrl+V / Ctrl+D", "Copiar / colar / duplicar"}, {"Ctrl+Z / Ctrl+Shift+Z", "Desfazer / refazer"},
            {"= / - / \\", "Aproximar / afastar / ajustar a linha do tempo"}, {"Ctrl+roda", "Zoom na linha do tempo ou no gráfico"},
            {"F9", "Suavização fácil"}, {"Shift+F9 / Ctrl+Shift+F9", "Suavização de entrada / saída"}, {"Shift+E", "Ativar / desativar clipe"},
            {"Shift+1 / 3 / 4 / 5 / 7", "Projeto / Linha do tempo / Programa / Controles / Efeitos"},
            {"Ctrl+I / Ctrl+M", "Importar / exportar mídia"}, {"Ctrl+S / Ctrl+O", "Salvar / abrir projeto"}, {"Alt+Shift+0", "Redefinir o espaço de trabalho"},
            {"Arrastar valor azul", "Muda o valor (Shift 10×, Ctrl 0,1×); clique para digitar"}};
        if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg)) {
            for (auto& r : rows) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col::Accent), "%s", r[0]);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(r[1]);
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Fechar", ImVec2(-FLT_MIN, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (s.showAbout) { ImGui::OpenPopup("Sobre o AUREA Studio"); s.showAbout = false; }
    if (ImGui::BeginPopupModal("Sobre o AUREA Studio", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(gFontBold, gFontBold->LegacySize);
        ImGui::TextUnformatted("AUREA Studio 0.1 beta");
        ImGui::PopFont();
        ImGui::TextDisabled("Editor de vídeo e motion para Windows, com o motor C++ do AUREA.");
        ImGui::TextDisabled("Prévia: Vulkan (thread própria) · Interface: Dear ImGui (MIT) + Direct3D 11.");
        ImGui::TextDisabled("GPU: %s", s.status.lastErrorDetail[0] ? s.status.lastErrorDetail : "Vulkan ativo");
        if (ImGui::Button("Fechar", ImVec2(-FLT_MIN, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void draw_status_bar(Studio& s) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const float cy = a.y + (size.y - ImGui::GetFontSize()) * .5f;
    const double age = ImGui::GetTime() - 0;
    (void)age;
    const ImU32 noticeColor = s.busy ? col::Accent : col::Muted;
    dl->AddText(ImVec2(a.x + S(10), cy), noticeColor, s.notice.c_str());
    std::string right = "AUREA Studio 0.1 beta";
    if (s.showPerf) {
        char b[200];
        const char* scales[] = {"auto", "completa", "1/2", "1/4", "1/8"};
        std::snprintf(b, sizeof b, "Prévia %.1f qps · %.1f ms · GPU %.1f ms · perdidos %u · %s  |  ", double(s.status.currentFps), double(s.status.averageFrameMs),
                      double(s.status.gpuMs), s.status.droppedFrames, scales[std::clamp(s.previewScale, 0, 4)]);
        right = b + right;
        for (char& ch : right) if (ch == '.') ch = ',';
    }
    const auto ex = s.engine.export_progress();
    if (ex.running && ex.framesTotal) {
        char b[96];
        std::snprintf(b, sizeof b, "Exportando %u%%  |  ", ex.framesDone * 100 / ex.framesTotal);
        right = b + right;
        const float w = S(120), x = a.x + size.x * .5f - w * .5f;
        dl->AddRectFilled(ImVec2(x, a.y + size.y * .5f - S(3)), ImVec2(x + w, a.y + size.y * .5f + S(3)), col::Chip, S(3));
        dl->AddRectFilled(ImVec2(x, a.y + size.y * .5f - S(3)), ImVec2(x + w * ex.framesDone / ex.framesTotal, a.y + size.y * .5f + S(3)), col::Accent, S(3));
    }
    const float tw = ImGui::CalcTextSize(right.c_str()).x;
    dl->AddText(ImVec2(a.x + size.x - tw - S(10), cy), col::Subtle, right.c_str());
}

// --- Atalhos -------------------------------------------------------------------
void handle_shortcuts(Studio& s) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
    auto pressed = [](ImGuiKey k, bool repeat = false) { return ImGui::IsKeyPressed(k, repeat); };
    auto step = [&](int frames) { Command c; c.type = CommandType::PlaybackStep; c.step.frames = frames; s.send(c); };
    if (ctrl) {
        if (pressed(ImGuiKey_Z)) s.command(shift ? CommandType::Redo : CommandType::Undo);
        if (pressed(ImGuiKey_Y)) s.command(CommandType::Redo);
        if (pressed(ImGuiKey_S)) s.save(shift);
        if (pressed(ImGuiKey_O)) s.open_project();
        if (pressed(ImGuiKey_I)) s.import_dialog();
        if (pressed(ImGuiKey_M)) { if (shift) s.navigate(-1); else open_export(s); }
        if (pressed(ImGuiKey_E) && shift) s.export_frame();
        if (pressed(ImGuiKey_N) && alt) s.new_project();
        if (pressed(ImGuiKey_K)) s.split_at_playhead();
        if (pressed(ImGuiKey_C) && !s.selection.empty()) { s.engine.copy_layers(s.selection.data(), u32(s.selection.size())); s.message("Copiado."); }
        if (pressed(ImGuiKey_V)) s.engine.paste_layers(s.playhead());
        if (pressed(ImGuiKey_D)) duplicate_selection(s);
        if (pressed(ImGuiKey_A)) { if (shift) s.set_selection({}); else select_all(s); }
        if (pressed(ImGuiKey_T)) add_item(s, 0);
        if (pressed(ImGuiKey_F9) && shift) s.apply_ease(2);
        return;
    }
    if (alt && shift && pressed(ImGuiKey_0)) { s.resetLayout = true; return; }
    if (pressed(ImGuiKey_Space)) s.command(CommandType::PlaybackToggle);
    if (pressed(ImGuiKey_K)) s.command(CommandType::PlaybackPause);
    if (pressed(ImGuiKey_L)) s.command(CommandType::PlaybackPlay);
    if (pressed(ImGuiKey_J, true)) { s.command(CommandType::PlaybackPause); step(-1); }
    if (pressed(ImGuiKey_LeftArrow, true)) step(shift ? -5 : -1);
    if (pressed(ImGuiKey_RightArrow, true)) step(shift ? 5 : 1);
    if (pressed(ImGuiKey_UpArrow, true)) s.navigate(-1);
    if (pressed(ImGuiKey_DownArrow, true)) s.navigate(1);
    if (pressed(ImGuiKey_Home)) s.seek(0);
    if (pressed(ImGuiKey_End)) s.seek(s.duration() - 1);
    if (pressed(ImGuiKey_Delete) || pressed(ImGuiKey_Backspace)) s.delete_selection();
    if (pressed(ImGuiKey_F1)) s.showShortcuts = true;
    if (pressed(ImGuiKey_F9)) s.apply_ease(shift ? 1 : 0);
    if (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd)) zoom_timeline(s, 1.25);
    if (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract)) zoom_timeline(s, 1 / 1.25);
    if (pressed(ImGuiKey_Backslash)) fit_timeline(s);
    if (shift) {
        if (pressed(ImGuiKey_E)) toggle_enable(s);
        if (pressed(ImGuiKey_M)) s.navigate(1);
        if (pressed(ImGuiKey_1)) ImGui::SetWindowFocus("Projeto");
        if (pressed(ImGuiKey_3)) ImGui::SetWindowFocus("Linha do tempo");
        if (pressed(ImGuiKey_4)) ImGui::SetWindowFocus("###Programa");
        if (pressed(ImGuiKey_5)) ImGui::SetWindowFocus("Controles de efeito");
        if (pressed(ImGuiKey_7)) ImGui::SetWindowFocus("Efeitos");
        return;
    }
    if (pressed(ImGuiKey_M)) s.add_marker();
    if (pressed(ImGuiKey_S)) s.snap = !s.snap;
    if (pressed(ImGuiKey_V)) s.tool = Tool::Select;
    if (pressed(ImGuiKey_A)) s.tool = Tool::TrackForward;
    if (pressed(ImGuiKey_B)) s.tool = Tool::Ripple;
    if (pressed(ImGuiKey_C)) s.tool = Tool::Razor;
    if (pressed(ImGuiKey_Y)) s.tool = Tool::Slip;
    if (pressed(ImGuiKey_P)) s.tool = Tool::Pen;
    if (pressed(ImGuiKey_H)) s.tool = Tool::Hand;
    if (pressed(ImGuiKey_T)) s.tool = Tool::Type;
    if (pressed(ImGuiKey_Escape)) { s.pick = {}; s.selectedKeys.clear(); }
}

} // namespace aurea::studio
