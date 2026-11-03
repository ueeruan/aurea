// Tema AUREA, ícones vetoriais e controles de edição (valor arrastável,
// cronômetro de animação, navegação de keyframes).
#include "Studio.hpp"
#include "imgui_internal.h"
#include <cmath>
#include <cstdio>

namespace aurea::studio {

ImFont* gFontUi = nullptr;
ImFont* gFontBold = nullptr;
ImFont* gFontTimecode = nullptr;
ImFont* gFontSmall = nullptr;

float S(float v) { return v * (gStudio ? gStudio->dpi : 1.f); }

ImU32 col::mix(ImU32 a, ImU32 b, float t) {
    auto ch = [&](int s) { return u32(std::lround(((a >> s) & 255) * (1 - t) + ((b >> s) & 255) * t)) << s; };
    return ch(0) | ch(8) | ch(16) | ch(24);
}

static ImVec4 v4(ImU32 c, float a = -1) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    if (a >= 0) v.w = a;
    return v;
}

void apply_theme(float dpi) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = {8, 6};
    s.FramePadding = {6, 3};
    s.ItemSpacing = {6, 4};
    s.ItemInnerSpacing = {4, 4};
    s.CellPadding = {6, 2};
    s.IndentSpacing = 14;
    s.ScrollbarSize = 11;
    s.GrabMinSize = 9;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 0;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.TabBarBorderSize = 1;
    s.WindowRounding = 3;
    s.ChildRounding = 2;
    s.FrameRounding = 3;
    s.PopupRounding = 4;
    s.ScrollbarRounding = 6;
    s.GrabRounding = 3;
    s.TabRounding = 0;
    s.DockingSeparatorSize = 3;
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize = 1;
    s.TabBarOverlineSize = 2;
    ImVec4* c = s.Colors;
    using namespace col;
    c[ImGuiCol_Text] = v4(Text);
    c[ImGuiCol_TextDisabled] = v4(Subtle);
    c[ImGuiCol_WindowBg] = v4(Surface);
    c[ImGuiCol_ChildBg] = v4(Surface, 0);
    c[ImGuiCol_PopupBg] = v4(SurfaceHigh, .98f);
    c[ImGuiCol_Border] = v4(Border);
    c[ImGuiCol_BorderShadow] = v4(0, 0);
    c[ImGuiCol_FrameBg] = v4(Background);
    c[ImGuiCol_FrameBgHovered] = v4(Chip);
    c[ImGuiCol_FrameBgActive] = v4(ChipHigh);
    c[ImGuiCol_TitleBg] = v4(Background);
    c[ImGuiCol_TitleBgActive] = v4(Background);
    c[ImGuiCol_TitleBgCollapsed] = v4(Background);
    c[ImGuiCol_MenuBarBg] = v4(Background);
    c[ImGuiCol_ScrollbarBg] = v4(Surface, 0);
    c[ImGuiCol_ScrollbarGrab] = v4(Chip);
    c[ImGuiCol_ScrollbarGrabHovered] = v4(ChipHigh);
    c[ImGuiCol_ScrollbarGrabActive] = v4(TickStrong);
    c[ImGuiCol_CheckMark] = v4(Accent);
    c[ImGuiCol_SliderGrab] = v4(Accent);
    c[ImGuiCol_SliderGrabActive] = v4(Keyframe);
    c[ImGuiCol_Button] = v4(Chip);
    c[ImGuiCol_ButtonHovered] = v4(ChipHigh);
    c[ImGuiCol_ButtonActive] = v4(Brand);
    c[ImGuiCol_Header] = v4(AccentDim);
    c[ImGuiCol_HeaderHovered] = v4(Chip);
    c[ImGuiCol_HeaderActive] = v4(Brand);
    c[ImGuiCol_Separator] = v4(Border);
    c[ImGuiCol_SeparatorHovered] = v4(Accent, .7f);
    c[ImGuiCol_SeparatorActive] = v4(Accent);
    c[ImGuiCol_ResizeGrip] = v4(0, 0);
    c[ImGuiCol_ResizeGripHovered] = v4(Accent, .5f);
    c[ImGuiCol_ResizeGripActive] = v4(Accent);
    c[ImGuiCol_InputTextCursor] = v4(Text);
    c[ImGuiCol_Tab] = v4(Background);
    c[ImGuiCol_TabHovered] = v4(Chip);
    c[ImGuiCol_TabSelected] = v4(Surface);
    c[ImGuiCol_TabSelectedOverline] = v4(Accent);
    c[ImGuiCol_TabDimmed] = v4(Background);
    c[ImGuiCol_TabDimmedSelected] = v4(Surface);
    c[ImGuiCol_TabDimmedSelectedOverline] = v4(Border);
    c[ImGuiCol_DockingPreview] = v4(Accent, .35f);
    c[ImGuiCol_DockingEmptyBg] = v4(Stage);
    c[ImGuiCol_PlotLines] = v4(Accent);
    c[ImGuiCol_PlotHistogram] = v4(Accent);
    c[ImGuiCol_TableHeaderBg] = v4(Background);
    c[ImGuiCol_TableBorderStrong] = v4(Border);
    c[ImGuiCol_TableBorderLight] = v4(Hairline);
    c[ImGuiCol_TableRowBg] = v4(0, 0);
    c[ImGuiCol_TableRowBgAlt] = v4(Text, .025f);
    c[ImGuiCol_TextSelectedBg] = v4(Accent, .35f);
    c[ImGuiCol_DragDropTarget] = v4(Accent);
    c[ImGuiCol_NavCursor] = v4(Accent);
    c[ImGuiCol_ModalWindowDimBg] = v4(Stage, .6f);
    s.ScaleAllSizes(dpi);
    s.FontScaleDpi = dpi;
}

void load_fonts(float) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    auto load = [&](const char* file, float size) -> ImFont* {
        char path[MAX_PATH];
        std::snprintf(path, sizeof path, "C:\\Windows\\Fonts\\%s", file);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return nullptr;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        return io.Fonts->AddFontFromFileTTF(path, size, &cfg);
    };
    gFontUi = load("segoeui.ttf", 13.f);
    if (!gFontUi) gFontUi = io.Fonts->AddFontDefault();
    gFontBold = load("seguisb.ttf", 13.f);
    if (!gFontBold) gFontBold = gFontUi;
    gFontTimecode = load("seguisb.ttf", 19.f);
    if (!gFontTimecode) gFontTimecode = gFontBold;
    gFontSmall = load("segoeui.ttf", 11.f);
    if (!gFontSmall) gFontSmall = gFontUi;
    io.FontDefault = gFontUi;
}

// ---------------------------------------------------------------------------
// Ícones: geometria simples em grade de 16 unidades, escalada pelo tamanho.
// ---------------------------------------------------------------------------
void draw_icon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 k) {
    const float u = size / 16.f, t = std::max(1.f, 1.35f * u);
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 8) * u, c.y + (y - 8) * u); };
    auto line = [&](float x0, float y0, float x1, float y1, float w = 0) { dl->AddLine(P(x0, y0), P(x1, y1), k, w > 0 ? w * u : t); };
    auto rect = [&](float x0, float y0, float x1, float y1, bool fill = false, float r = 0) {
        if (fill) dl->AddRectFilled(P(x0, y0), P(x1, y1), k, r * u); else dl->AddRect(P(x0, y0), P(x1, y1), k, r * u, t);
    };
    auto tri = [&](float x0, float y0, float x1, float y1, float x2, float y2) { dl->AddTriangleFilled(P(x0, y0), P(x1, y1), P(x2, y2), k); };
    auto circle = [&](float x, float y, float r, bool fill = false) { if (fill) dl->AddCircleFilled(P(x, y), r * u, k); else dl->AddCircle(P(x, y), r * u, k, 0, t); };
    switch (icon) {
    case Icon::Play: tri(4.5f, 2.5f, 13.5f, 8, 4.5f, 13.5f); break;
    case Icon::Pause: rect(4, 3, 7, 13, true, .5f); rect(9, 3, 12, 13, true, .5f); break;
    case Icon::StepBack: tri(12, 3, 5, 8, 12, 13); rect(3, 3, 4.6f, 13, true); break;
    case Icon::StepForward: tri(4, 3, 11, 8, 4, 13); rect(11.4f, 3, 13, 13, true); break;
    case Icon::ToStart: tri(14, 3, 8, 8, 14, 13); tri(9, 3, 3, 8, 9, 13); rect(2, 3, 3.3f, 13, true); break;
    case Icon::ToEnd: tri(2, 3, 8, 8, 2, 13); tri(7, 3, 13, 8, 7, 13); rect(12.7f, 3, 14, 13, true); break;
    case Icon::Marker: dl->PathLineTo(P(4, 2.5f)); dl->PathLineTo(P(12, 2.5f)); dl->PathLineTo(P(12, 10)); dl->PathLineTo(P(8, 13.5f)); dl->PathLineTo(P(4, 10)); dl->PathFillConvex(k); break;
    case Icon::MarkIn: line(5, 2, 5, 14); line(5, 2, 10, 2); line(5, 14, 10, 14); break;
    case Icon::MarkOut: line(11, 2, 11, 14); line(6, 2, 11, 2); line(6, 14, 11, 14); break;
    case Icon::Camera: rect(1.5f, 4.5f, 14.5f, 13, false, 1.5f); circle(8, 8.7f, 2.6f); rect(5.5f, 2.5f, 10.5f, 4.5f, true); break;
    case Icon::Lock: rect(3.5f, 7, 12.5f, 14, true, 1); dl->PathArcTo(P(8, 7), 3 * u, IM_PI, 2 * IM_PI); dl->PathStroke(k, t); line(5, 7, 5, 6.5f); line(11, 7, 11, 6.5f); break;
    case Icon::Unlock: rect(3.5f, 7, 12.5f, 14, false, 1); dl->PathArcTo(P(8, 5), 3 * u, IM_PI, 2 * IM_PI); dl->PathStroke(k, t); line(5, 5, 5, 7); break;
    case Icon::Eye: case Icon::EyeOff:
        dl->PathArcTo(P(8, 13), 7.5f * u, -IM_PI * .82f, -IM_PI * .18f); dl->PathStroke(k, t);
        dl->PathArcTo(P(8, 3), 7.5f * u, IM_PI * .18f, IM_PI * .82f); dl->PathStroke(k, t);
        circle(8, 8, 2.2f, true);
        if (icon == Icon::EyeOff) line(2.5f, 13.5f, 13.5f, 2.5f);
        break;
    case Icon::Speaker: case Icon::Mute:
        dl->PathLineTo(P(2, 6)); dl->PathLineTo(P(5, 6)); dl->PathLineTo(P(9, 2.5f)); dl->PathLineTo(P(9, 13.5f)); dl->PathLineTo(P(5, 10)); dl->PathLineTo(P(2, 10)); dl->PathFillConvex(k);
        if (icon == Icon::Mute) { line(11, 5.5f, 15, 10.5f); line(15, 5.5f, 11, 10.5f); }
        else { dl->PathArcTo(P(9, 8), 3.5f * u, -.9f, .9f); dl->PathStroke(k, t); dl->PathArcTo(P(9, 8), 6 * u, -.9f, .9f); dl->PathStroke(k, t); }
        break;
    case Icon::Mic: rect(6, 1.5f, 10, 10, true, 2); dl->PathArcTo(P(8, 8), 4 * u, 0, IM_PI); dl->PathStroke(k, t); line(8, 12, 8, 14.5f); line(5.5f, 14.5f, 10.5f, 14.5f); break;
    case Icon::Magnet:
        dl->PathArcTo(P(8, 8), 5 * u, 0, IM_PI); dl->PathStroke(k, 3 * u);
        rect(1.5f, 3, 4.5f, 8, true); rect(11.5f, 3, 14.5f, 8, true);
        break;
    case Icon::Link: rect(1.5f, 5.5f, 9, 10.5f, false, 2.5f); rect(7, 5.5f, 14.5f, 10.5f, false, 2.5f); break;
    case Icon::Wrench: line(3, 13, 9.5f, 6.5f, 2.2f); circle(11, 5, 3); break;
    case Icon::Stopwatch: circle(8, 9, 5.5f); line(8, 9, 8, 5.5f); line(6.5f, 1.8f, 9.5f, 1.8f); line(8, 1.8f, 8, 3.5f); line(12.3f, 4, 13.5f, 2.8f); break;
    case Icon::Diamond: dl->AddQuadFilled(P(8, 2), P(14, 8), P(8, 14), P(2, 8), k); break;
    case Icon::ChevronLeft: tri(10.5f, 3, 4.5f, 8, 10.5f, 13); break;
    case Icon::ChevronRight: tri(5.5f, 3, 11.5f, 8, 5.5f, 13); break;
    case Icon::ChevronDown: tri(3, 5.5f, 13, 5.5f, 8, 11.5f); break;
    case Icon::Plus: line(8, 2.5f, 8, 13.5f, 1.6f); line(2.5f, 8, 13.5f, 8, 1.6f); break;
    case Icon::Close: line(3.5f, 3.5f, 12.5f, 12.5f, 1.6f); line(12.5f, 3.5f, 3.5f, 12.5f, 1.6f); break;
    case Icon::Trash: rect(4, 5, 12, 14.5f, false, 1); line(2.5f, 4, 13.5f, 4); rect(6, 1.5f, 10, 4); line(6.8f, 7, 6.8f, 12.5f); line(9.2f, 7, 9.2f, 12.5f); break;
    case Icon::Fx: {
        ImFont* f = gFontBold ? gFontBold : ImGui::GetFont();
        const float fs = 11.5f * u;
        const ImVec2 sz = f->CalcTextSizeA(fs, FLT_MAX, 0, "fx");
        dl->AddText(f, fs, ImVec2(c.x - sz.x * .5f, c.y - sz.y * .5f), k, "fx");
        break;
    }
    case Icon::Folder: dl->PathLineTo(P(1.5f, 3.5f)); dl->PathLineTo(P(6, 3.5f)); dl->PathLineTo(P(7.5f, 5)); dl->PathLineTo(P(14.5f, 5)); dl->PathLineTo(P(14.5f, 13)); dl->PathLineTo(P(1.5f, 13)); dl->PathFillConvex(k); break;
    case Icon::Film: rect(2, 2.5f, 14, 13.5f, false, 1); line(4.8f, 2.5f, 4.8f, 13.5f); line(11.2f, 2.5f, 11.2f, 13.5f); line(2, 8, 4.8f, 8); line(11.2f, 8, 14, 8); break;
    case Icon::Image: rect(2, 3, 14, 13, false, 1); tri(3.5f, 11.5f, 7.5f, 6.5f, 10.5f, 11.5f); tri(8.5f, 11.5f, 11, 8.5f, 12.8f, 11.5f); circle(11, 5.8f, 1.2f, true); break;
    case Icon::Audio: for (int i = 0; i < 6; ++i) { const float h = (i % 3 == 1 ? 5.5f : i % 2 ? 3.f : 4.2f); line(3 + i * 2.f, 8 - h, 3 + i * 2.f, 8 + h, 1.4f); } break;
    case Icon::Text: case Icon::Type: line(3, 3, 13, 3, 1.8f); line(8, 3, 8, 13.5f, 1.8f); if (icon == Icon::Type) line(5.5f, 13.5f, 10.5f, 13.5f); break;
    case Icon::Shape: rect(2, 6, 9.5f, 13.5f, true, 1); circle(10.5f, 5.5f, 4); break;
    case Icon::Null: rect(3, 3, 13, 13, false, 1); line(3, 3, 13, 13); line(13, 3, 3, 13); break;
    case Icon::Light: circle(8, 6.5f, 4.2f); line(6, 12, 10, 12); line(6.5f, 14, 9.5f, 14); break;
    case Icon::Search: circle(7, 7, 4.5f); line(10.3f, 10.3f, 14, 14, 1.8f); break;
    case Icon::List: for (int i = 0; i < 3; ++i) { rect(2, 3.f + i * 4, 4, 5.f + i * 4, true); line(6, 4.f + i * 4, 14, 4.f + i * 4); } break;
    case Icon::Grid: rect(2, 2, 7, 7, true, .7f); rect(9, 2, 14, 7, true, .7f); rect(2, 9, 7, 14, true, .7f); rect(9, 9, 14, 14, true, .7f); break;
    case Icon::Select: dl->PathLineTo(P(4, 1.5f)); dl->PathLineTo(P(12.5f, 10)); dl->PathLineTo(P(8.5f, 10.3f)); dl->PathLineTo(P(10.8f, 14.5f)); dl->PathLineTo(P(9.2f, 15)); dl->PathLineTo(P(7, 10.8f)); dl->PathLineTo(P(4, 13.5f)); dl->PathFillConcave(k); break;
    case Icon::TrackForward: tri(2, 3, 7, 8, 2, 13); tri(8, 3, 13, 8, 8, 13); line(14.5f, 2, 14.5f, 14); break;
    case Icon::Ripple: line(8, 2, 8, 14); tri(7, 4, 2, 8, 7, 12); line(10, 5, 14, 8); line(10, 11, 14, 8); break;
    case Icon::Razor: dl->AddQuadFilled(P(3, 13), P(10, 3), P(13, 5.5f), P(6, 15), k); circle(9.3f, 6.7f, 1.1f, true); break;
    case Icon::Slip: line(2, 8, 14, 8); tri(2, 8, 5, 5, 5, 11); tri(14, 8, 11, 5, 11, 11); line(6, 3, 6, 13); line(10, 3, 10, 13); break;
    case Icon::Pen: dl->AddQuadFilled(P(8, 1.5f), P(13, 8), P(8, 14.5f), P(3, 8), k); circle(8, 8.5f, 1.3f, false); break;
    case Icon::Hand: rect(4.5f, 6, 12.5f, 14, true, 2.5f); for (int i = 0; i < 4; ++i) rect(4.5f + i * 2.f, 2.5f + (i == 0 ? 2 : 0), 6.f + i * 2.f, 8, true, .7f); break;
    case Icon::Reset: dl->PathArcTo(P(8, 8), 5 * u, -IM_PI * .1f, IM_PI * 1.45f); dl->PathStroke(k, t); tri(11.5f, 1.8f, 15, 6, 10, 6.5f); break;
    case Icon::Loop: rect(2.5f, 4.5f, 13.5f, 11.5f, false, 3); tri(9, 2, 12, 4.5f, 9, 7); break;
    case Icon::Export: line(8, 10, 8, 2); tri(4.5f, 5, 8, 1.5f, 11.5f, 5); line(3, 9, 3, 14); line(3, 14, 13, 14); line(13, 14, 13, 9); break;
    case Icon::Undo: dl->PathArcTo(P(8.5f, 9.5f), 4.5f * u, -IM_PI, 0.6f); dl->PathStroke(k, t); tri(1.5f, 8, 6.5f, 8, 4, 4.5f); break;
    case Icon::Redo: dl->PathArcTo(P(7.5f, 9.5f), 4.5f * u, IM_PI - .6f, 2 * IM_PI); dl->PathStroke(k, t); tri(9.5f, 8, 14.5f, 8, 12, 4.5f); break;
    case Icon::Graph: line(2, 14, 14, 14); line(2, 2, 2, 14); dl->AddBezierCubic(P(3, 13), P(8, 13), P(7, 3), P(14, 3), k, t); break;
    case Icon::Ease: dl->AddBezierCubic(P(2, 13), P(9, 13), P(7, 3), P(14, 3), k, t * 1.2f); dl->AddQuadFilled(P(2, 11), P(4, 13), P(2, 15), P(0, 13), k); dl->AddQuadFilled(P(14, 1), P(16, 3), P(14, 5), P(12, 3), k); break;
    case Icon::Hold: line(2, 12, 8, 12, 1.6f); line(8, 12, 8, 4, 1.6f); line(8, 4, 14, 4, 1.6f); break;
    case Icon::Linear: line(2, 13, 14, 3, 1.6f); circle(2.5f, 12.6f, 1.5f, true); circle(13.5f, 3.4f, 1.5f, true); break;
    case Icon::Pick: circle(8, 8, 4.5f); line(8, 1, 8, 5); line(8, 11, 8, 15); line(1, 8, 5, 8); line(11, 8, 15, 8); break;
    case Icon::Gear: circle(8, 8, 3.2f); for (int i = 0; i < 8; ++i) { const float a = i * IM_PI / 4; line(8 + std::cos(a) * 4.6f, 8 + std::sin(a) * 4.6f, 8 + std::cos(a) * 6.6f, 8 + std::sin(a) * 6.6f, 2); } break;
    case Icon::Layers: dl->AddQuadFilled(P(8, 2), P(14, 5.5f), P(8, 9), P(2, 5.5f), k); dl->PathLineTo(P(2, 8.5f)); dl->PathLineTo(P(8, 12)); dl->PathLineTo(P(14, 8.5f)); dl->PathStroke(k, t); dl->PathLineTo(P(2, 11.5f)); dl->PathLineTo(P(8, 15)); dl->PathLineTo(P(14, 11.5f)); dl->PathStroke(k, t); break;
    }
}

void tooltip(const char* text) {
    if (text && *text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

bool icon_button(const char* id, Icon icon, const char* tip, bool active, float size, bool enabled) {
    const float side = size > 0 ? size : ImGui::GetFrameHeight();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(side, side));
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered() && enabled, held = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    if (active) dl->AddRectFilled(a, b, col::AccentDim, S(3));
    else if (held) dl->AddRectFilled(a, b, col::ChipHigh, S(3));
    else if (hovered) dl->AddRectFilled(a, b, col::Chip, S(3));
    const ImU32 k = !enabled ? col::alpha(col::Subtle, .45f) : active ? col::Accent : hovered ? col::Text : col::Muted;
    draw_icon(dl, icon, ImVec2((a.x + b.x) * .5f, (a.y + b.y) * .5f), side * .62f, k);
    tooltip(tip);
    return pressed && enabled;
}

std::string format_value(float v, int decimals) {
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", std::clamp(decimals, 0, 6), double(v));
    std::string s = b;
    for (char& ch : s) if (ch == '.') ch = ',';
    if (s == "-0" || s == "-0,0" || s == "-0,00" || s == "-0,000") s.erase(0, 1);
    return s;
}

static bool parse_value(const char* text, float& out) {
    std::string s = text;
    for (char& ch : s) if (ch == ',') ch = '.';
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    while (end && *end == ' ') ++end;
    if (!end || end == s.c_str() || !std::isfinite(v)) return false;
    out = float(v);
    return true;
}

// Valor quente: arrastar na horizontal muda (Shift = 10×, Ctrl = 0,1×);
// clicar sem arrastar abre o campo para digitar. Um arrasto é um passo de desfazer.
bool scrub_float(const char* id, float* value, float speed, float minimum, float maximum, const char* format, ImU32 color, float scale) {
    ImGuiStorage* store = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID(id);
    const ImGuiID editKey = key ^ 0x5EDF00Du;
    bool changed = false;
    const float shown = *value * scale;
    char label[64];
    std::snprintf(label, sizeof label, format, double(shown));
    for (char* p = label; *p; ++p) if (*p == '.') *p = ',';
    if (store->GetBool(editKey)) {
        static char buffer[64];
        if (store->GetInt(editKey ^ 1u) == 0) {
            std::snprintf(buffer, sizeof buffer, "%s", label);
            store->SetInt(editKey ^ 1u, 1);
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(S(64));
        const bool done = ImGui::InputText(id, buffer, sizeof buffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (done || ImGui::IsItemDeactivated()) {
            float typed;
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && parse_value(buffer, typed)) {
                typed /= scale;
                if (maximum > minimum) typed = std::clamp(typed, minimum, maximum);
                if (typed != *value) { *value = typed; changed = true; }
            }
            store->SetBool(editKey, false);
            store->SetInt(editKey ^ 1u, 0);
        }
        return changed;
    }
    const ImVec2 size = ImGui::CalcTextSize(label);
    ImGui::InvisibleButton(id, ImVec2(size.x + S(2), ImGui::GetFrameHeight()));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActivated()) { store->SetFloat(key, 0); if (gStudio) gStudio->begin_group(); }
    if (active) {
        const float dx = ImGui::GetIO().MouseDelta.x;
        store->SetFloat(key, store->GetFloat(key) + std::abs(dx));
        if (dx != 0) {
            float mult = ImGui::GetIO().KeyShift ? 10.f : ImGui::GetIO().KeyCtrl ? .1f : 1.f;
            float next = *value + dx * speed * mult;
            if (maximum > minimum) next = std::clamp(next, minimum, maximum);
            if (next != *value) { *value = next; changed = true; }
        }
    }
    if (ImGui::IsItemDeactivated()) {
        if (gStudio) gStudio->end_group();
        if (store->GetFloat(key) < 2.f) store->SetBool(editKey, true);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin();
    const float y = a.y + (ImGui::GetFrameHeight() - size.y) * .5f;
    const ImU32 k = active ? col::Keyframe : hovered ? col::mix(color, col::Text, .35f) : color;
    dl->AddText(ImVec2(a.x, y), k, label);
    // Sublinhado pontilhado do valor quente.
    for (float x = a.x; x < a.x + size.x; x += S(3)) dl->AddLine(ImVec2(x, y + size.y), ImVec2(std::min(x + S(1.5f), a.x + size.x), y + size.y), col::alpha(k, .7f));
    return changed;
}

bool stopwatch(const char* id, bool animated) {
    const float side = ImGui::GetFrameHeight();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(side, side));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 a = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (animated) dl->AddCircleFilled(ImVec2(a.x + side * .5f, a.y + side * .55f), side * .36f, col::AccentDim);
    draw_icon(dl, Icon::Stopwatch, ImVec2(a.x + side * .5f, a.y + side * .5f), side * .66f,
              animated ? col::Accent : hovered ? col::Text : col::Subtle);
    tooltip(animated ? "Animação ligada (clique para remover os keyframes)" : "Ativar animação: cria um keyframe no indicador");
    return pressed;
}

void draw_diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 border, bool filled) {
    const ImVec2 p[4] = {{c.x, c.y - r}, {c.x + r, c.y}, {c.x, c.y + r}, {c.x - r, c.y}};
    if (filled) dl->AddConvexPolyFilled(p, 4, fill);
    dl->AddPolyline(p, 4, border, std::max(1.f, S(1)), ImDrawFlags_Closed);
}

bool key_navigator(const char* id, bool keyHere, bool* previous, bool* next) {
    ImGui::PushID(id);
    const float h = ImGui::GetFrameHeight(), w = h * .72f;
    bool toggled = false;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto arrow = [&](const char* sub, bool left, bool* out) {
        ImGui::InvisibleButton(sub, ImVec2(w, h));
        *out = ImGui::IsItemClicked();
        const ImVec2 a = ImGui::GetItemRectMin();
        draw_icon(dl, left ? Icon::ChevronLeft : Icon::ChevronRight, ImVec2(a.x + w * .5f, a.y + h * .5f), h * .5f,
                  ImGui::IsItemHovered() ? col::Text : col::Subtle);
        tooltip(left ? "Ir para o keyframe anterior" : "Ir para o próximo keyframe");
        ImGui::SameLine(0, 0);
    };
    arrow("p", true, previous);
    ImGui::InvisibleButton("k", ImVec2(w, h));
    toggled = ImGui::IsItemClicked();
    const ImVec2 a = ImGui::GetItemRectMin();
    const bool hov = ImGui::IsItemHovered();
    draw_diamond(dl, ImVec2(a.x + w * .5f, a.y + h * .5f), h * .24f, col::Keyframe, keyHere ? col::Keyframe : hov ? col::Text : col::Subtle, keyHere);
    tooltip(keyHere ? "Remover keyframe" : "Adicionar keyframe");
    ImGui::SameLine(0, 0);
    arrow("n", false, next);
    ImGui::NewLine();
    ImGui::PopID();
    return toggled;
}

bool twirl(const char* id, bool* open) {
    const float h = ImGui::GetFrameHeight();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(h * .7f, h));
    if (pressed) *open = !*open;
    const ImVec2 a = ImGui::GetItemRectMin();
    draw_icon(ImGui::GetWindowDrawList(), *open ? Icon::ChevronDown : Icon::ChevronRight, ImVec2(a.x + h * .35f, a.y + h * .5f), h * .42f,
              ImGui::IsItemHovered() ? col::Text : col::Muted);
    return pressed;
}

bool small_toggle(const char* id, const char* label, bool on, const char* tip) {
    ImGui::PushStyleColor(ImGuiCol_Button, on ? col::AccentDim : col::Chip);
    ImGui::PushStyleColor(ImGuiCol_Text, on ? col::Accent : col::Muted);
    ImGui::PushID(id);
    const bool pressed = ImGui::SmallButton(label);
    ImGui::PopID();
    ImGui::PopStyleColor(2);
    tooltip(tip);
    return pressed;
}

void section_band(const char* label) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight();
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), col::Background);
    ImGui::PushFont(gFontBold, gFontBold->LegacySize);
    dl->AddText(ImVec2(a.x + S(8), a.y + (h - ImGui::GetFontSize()) * .5f), col::Muted, label);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(w, h));
}

float srgb_to_linear(float c) { return c <= .04045f ? c / 12.92f : std::pow((c + .055f) / 1.055f, 2.4f); }
float linear_to_srgb(float c) { c = std::max(0.f, c); return c <= .0031308f ? c * 12.92f : 1.055f * std::pow(c, 1 / 2.4f) - .055f; }

ImU32 clip_color(const LayerView& l) {
    switch (LayerKind(l.row.kind)) {
    case LayerKind::Video: return col::ClipVideo;
    case LayerKind::Image: return col::ClipImage;
    case LayerKind::Audio: return col::ClipAudio;
    case LayerKind::Text: case LayerKind::Shape: case LayerKind::ParticleSystem: return col::ClipGraphic;
    case LayerKind::Model3D: return col::Clip3D;
    case LayerKind::Composition: return col::Brand;
    default: return col::ClipUtility;
    }
}

Icon layer_icon(const LayerView& l) {
    switch (LayerKind(l.row.kind)) {
    case LayerKind::Video: return Icon::Film;
    case LayerKind::Image: return Icon::Image;
    case LayerKind::Audio: return Icon::Audio;
    case LayerKind::Text: return Icon::Text;
    case LayerKind::Shape: return Icon::Shape;
    case LayerKind::Null: return Icon::Null;
    case LayerKind::Camera: return Icon::Camera;
    case LayerKind::Light: return Icon::Light;
    case LayerKind::Composition: return Icon::Layers;
    case LayerKind::Adjustment: return Icon::Fx;
    default: return Icon::Shape;
    }
}

const char* track_property_name(u32 p) {
    static const char* names[] = {
        "Posição X", "Posição Y", "Posição Z", "Escala X", "Escala Y", "Escala Z", "Rotação X", "Rotação Y", "Rotação Z",
        "Ponto de ancoragem X", "Ponto de ancoragem Y", "Ponto de ancoragem Z", "Opacidade", "Inclinação X", "Inclinação Y",
        "Campo de visão", "Distância focal", "Distância de foco", "Abertura", "Plano próximo", "Plano distante",
        "Intensidade da luz", "Cor R", "Cor G", "Cor B", "Ângulo do cone", "Penumbra", "Metálico", "Rugosidade",
        "Espaçamento", "Remapear tempo", "Parâmetro de efeito", "Volume", "Animador de texto", "Vetor", "Forma",
        "Partículas", "Material", "Desfoque da lente", "Velocidade", "Animador de camada", "Osso", "Parte 3D",
        "Máscara", "Malha", "Pose", "Margem inicial", "Margem final"};
    return p < std::size(names) ? names[p] : "Propriedade";
}

} // namespace aurea::studio
