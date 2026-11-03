// AUREA Studio (PC): janela, Direct3D 11 da interface, laço orientado a eventos
// e a janela filha onde o motor apresenta a prévia pelo Vulkan.
//
// Desempenho: a prévia é desenhada pela thread de render do motor direto na
// swapchain Vulkan da janela filha (sem cópia para a interface). A interface só
// redesenha quando há entrada, quando o motor muda (revisão, cabeçote, miniatura)
// ou durante a reprodução; parada, a CPU fica ociosa.
#include "aurea/export/FFmpegExportSink.hpp"
#include "Studio.hpp"
#include "imgui_internal.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <dxgi1_3.h>
#include <shellapi.h>
#include <shlobj.h>
// VulkanLoader removes legacy near/far macros after the Windows SDK headers.
#include "VulkanBackend.hpp"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <set>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace fs = std::filesystem;

namespace aurea::studio {

Studio* gStudio = nullptr;

std::string utf8(const std::wstring& s) { return windows::utf8(s); }
std::wstring wide(const std::string& s) { return windows::wide(s); }

namespace {
IDXGISwapChain2* gSwap = nullptr;
ID3D11RenderTargetView* gTarget = nullptr;
HANDLE gFrameWait = nullptr;
UINT gWidth = 0, gHeight = 0;
bool gOccluded = false;
constexpr UINT WM_STUDIO_WAKE = WM_APP + 7;
double now_seconds() {
    static LARGE_INTEGER f{};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart) / double(f.QuadPart);
}
double gLastInput = 0;

void create_target() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(gSwap->GetBuffer(0, IID_PPV_ARGS(&back)))) {
        gStudio->device->CreateRenderTargetView(back, nullptr, &gTarget);
        back->Release();
    }
}
void release_target() { if (gTarget) { gTarget->Release(); gTarget = nullptr; } }

bool create_device(HWND hwnd) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL got;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                                   &gStudio->device, &got, &gStudio->context);
    if (FAILED(hr)) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 2, D3D11_SDK_VERSION,
                                           &gStudio->device, &got, &gStudio->context);
    if (FAILED(hr)) return false;
    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    gStudio->device->QueryInterface(IID_PPV_ARGS(&dxgi));
    dxgi->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&factory));
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    d.Scaling = DXGI_SCALING_NONE;
    d.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    IDXGISwapChain1* swap = nullptr;
    hr = factory->CreateSwapChainForHwnd(gStudio->device, hwnd, &d, nullptr, nullptr, &swap);
    if (SUCCEEDED(hr)) {
        swap->QueryInterface(IID_PPV_ARGS(&gSwap));
        swap->Release();
        gSwap->SetMaximumFrameLatency(1);
        gFrameWait = gSwap->GetFrameLatencyWaitableObject();
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    }
    factory->Release();
    adapter->Release();
    dxgi->Release();
    if (!gSwap) return false;
    create_target();
    return true;
}

void destroy_device() {
    release_target();
    if (gFrameWait) CloseHandle(gFrameWait);
    if (gSwap) gSwap->Release();
    if (gStudio->context) gStudio->context->Release();
    if (gStudio->device) gStudio->device->Release();
}

// ---------------------------------------------------------------------------
// Janela filha da prévia: o motor apresenta nela; aqui só os gestos.
// ---------------------------------------------------------------------------
LRESULT CALLBACK preview_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    Studio* s = gStudio;
    if (!s) return DefWindowProcW(w, msg, wp, lp);
    auto wake = [&] { PostMessageW(s->window, WM_STUDIO_WAKE, 0, 0); };
    auto comp_point = [&](LPARAM l, float& cx, float& cy) {
        RECT r;
        GetClientRect(w, &r);
        const float scale = std::min(float(r.right) / std::max(1u, s->status.compWidth), float(r.bottom) / std::max(1u, s->status.compHeight));
        const float ox = (r.right - s->status.compWidth * scale) * .5f, oy = (r.bottom - s->status.compHeight * scale) * .5f;
        cx = (GET_X_LPARAM(l) - ox) / std::max(1e-3f, scale);
        cy = (GET_Y_LPARAM(l) - oy) / std::max(1e-3f, scale);
        return scale;
    };
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, s->pick.effect != kInvalidIndex ? IDC_CROSS : s->primary && s->tool == Tool::Select ? IDC_SIZEALL : IDC_ARROW));
        return TRUE;
    case WM_LBUTTONDOWN: {
        SetFocus(s->window);
        float cx, cy;
        comp_point(lp, cx, cy);
        if (s->pick.effect != kInvalidIndex) {
            for (auto& fx : s->effects) if (fx.row.effectId == s->pick.effect)
                for (auto& p : fx.params) if (p.index == s->pick.param) {
                    float v[4] = {p.value[0], p.value[1], p.value[2], p.value[3]};
                    const bool relative = p.flags & kParamRelative;
                    v[0] = relative ? cx / std::max(1u, s->status.compWidth) : cx;
                    v[1] = relative ? cy / std::max(1u, s->status.compHeight) : cy;
                    s->set_effect_vector(fx, p, v);
                }
            s->pick = {};
            s->message("Ponto definido na prévia.");
            wake();
            return 0;
        }
        if (s->primary && s->tool == Tool::Select) {
            const LayerView* l = s->layer(s->primary);
            if (l && !l->locked()) {
                s->previewDrag = true;
                s->previewDown = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                s->dragStartPos[0] = s->detail.position[0];
                s->dragStartPos[1] = s->detail.position[1];
                s->begin_group();
                SetCapture(w);
            }
        }
        wake();
        return 0;
    }
    case WM_MOUSEMOVE:
        if (s->previewDrag && (wp & MK_LBUTTON)) {
            float cx, cy;
            const float scale = comp_point(lp, cx, cy);
            if (scale > 0) {
                float dx = (GET_X_LPARAM(lp) - s->previewDown.x) / scale, dy = (GET_Y_LPARAM(lp) - s->previewDown.y) / scale;
                if (wp & MK_SHIFT) { if (std::abs(dx) > std::abs(dy)) dy = 0; else dx = 0; }
                s->set_transform(0, s->dragStartPos[0] + dx);
                s->set_transform(1, s->dragStartPos[1] + dy);
            }
            wake();
        }
        return 0;
    case WM_LBUTTONUP:
        if (s->previewDrag) { s->previewDrag = false; s->end_group(); ReleaseCapture(); s->revision = ~0u; wake(); }
        return 0;
    case WM_RBUTTONUP:
    case WM_MOUSEWHEEL:
        wake();
        return 0;
    case WM_SIZE:
        if (s->ready && LOWORD(lp) && HIWORD(lp)) { (void)s->engine.resize_surface(LOWORD(lp), HIWORD(lp)); s->engine.request_render(); }
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

LRESULT CALLBACK window_proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(w, msg, wp, lp)) return 1;
    Studio* s = gStudio;
    switch (msg) {
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) return 0;
        gWidth = LOWORD(lp);
        gHeight = HIWORD(lp);
        if (gSwap && gWidth && gHeight) {
            release_target();
            gSwap->ResizeBuffers(0, gWidth, gHeight, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
            create_target();
        }
        return 0;
    case WM_DPICHANGED: {
        const RECT* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(w, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        if (s) { s->dpi = HIWORD(wp) / 96.f; apply_theme(s->dpi); }
        return 0;
    }
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wp);
        std::vector<fs::path> files;
        const UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            std::wstring path(DragQueryFileW(drop, i, nullptr, 0) + 1, L'\0');
            DragQueryFileW(drop, i, path.data(), UINT(path.size()));
            path.resize(wcslen(path.c_str()));
            files.emplace_back(path);
        }
        DragFinish(drop);
        if (s) s->import_files(std::move(files));
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wp & 0xfff0) == SC_KEYMENU) return 0;  // Alt não abre o menu do sistema
        break;
    case WM_CLOSE:
        if (s && !s->may_discard()) return 0;
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

// Recorta da prévia as áreas cobertas por menus, popups e painéis flutuantes:
// a janela filha fica acima da interface, então o que flutua sobre ela precisa de um furo.
void update_preview_window(Studio& s) {
    if (!s.preview) return;
    if (!s.previewWanted) {
        if (s.previewShown) { ShowWindow(s.preview, SW_HIDE); s.previewShown = false; }
        return;
    }
    RECT current{};
    GetWindowRect(s.preview, &current);
    MapWindowPoints(HWND_DESKTOP, s.window, reinterpret_cast<POINT*>(&current), 2);
    const RECT& r = s.previewRect;
    if (!EqualRect(&current, &r) || !s.previewShown)
        SetWindowPos(s.preview, nullptr, r.left, r.top, std::max(1L, r.right - r.left), std::max(1L, r.bottom - r.top),
                     SWP_NOZORDER | SWP_NOACTIVATE | (s.previewShown ? 0 : SWP_SHOWWINDOW));
    s.previewShown = true;
    std::vector<RECT> holes;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (ImGuiWindow* win : g.Windows) {
        if (!win->WasActive || win->Hidden) continue;
        const bool popup = win->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_ChildMenu | ImGuiWindowFlags_Modal);
        const bool floating = !(win->Flags & ImGuiWindowFlags_ChildWindow) && !win->DockIsActive && !(win->Flags & ImGuiWindowFlags_NoDocking)
                              && win->RootWindowDockTree == win && !(win->Flags & ImGuiWindowFlags_NoBackground);
        if (!popup && !floating) continue;
        RECT h{LONG(win->Pos.x) - r.left, LONG(win->Pos.y) - r.top, LONG(win->Pos.x + win->Size.x) - r.left + 1, LONG(win->Pos.y + win->Size.y) - r.top + 1};
        RECT inside{};
        RECT local{0, 0, r.right - r.left, r.bottom - r.top};
        if (IntersectRect(&inside, &h, &local)) holes.push_back(inside);
    }
    const bool same = holes.size() == s.previewHoles.size() &&
                      std::equal(holes.begin(), holes.end(), s.previewHoles.begin(), [](const RECT& a, const RECT& b) { return EqualRect(&a, &b); });
    if (same) return;
    s.previewHoles = holes;
    if (holes.empty()) { SetWindowRgn(s.preview, nullptr, TRUE); return; }
    HRGN region = CreateRectRgn(0, 0, r.right - r.left, r.bottom - r.top);
    for (const RECT& h : holes) {
        HRGN hole = CreateRectRgnIndirect(&h);
        CombineRgn(region, region, hole, RGN_DIFF);
        DeleteObject(hole);
    }
    SetWindowRgn(s.preview, region, TRUE);  // o sistema passa a ser dono da região
}

void render_frame(Studio& s, ImGuiID& dockspace) {
    if (s.pendingWorkspace != Workspace::Count || s.resetLayout) {
        // Cada espaço de trabalho guarda o arranjo que a pessoa deixou.
        const Workspace target = s.pendingWorkspace != Workspace::Count ? s.pendingWorkspace : s.workspace;
        if (!s.resetLayout) s.layouts[int(s.workspace)] = ImGui::SaveIniSettingsToMemory();
        s.workspace = target;
        auto saved = s.layouts.find(int(target));
        if (!s.resetLayout && saved != s.layouts.end() && !saved->second.empty()) ImGui::LoadIniSettingsFromMemory(saved->second.c_str());
        else dockspace = 0;  // reconstrói abaixo
        s.layouts.erase(int(target));
        s.pendingWorkspace = Workspace::Count;
        s.resetLayout = false;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    s.refresh();
    handle_shortcuts(s);
    s.previewWanted = false;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float headerH = ImGui::GetFrameHeight() + S(12), statusH = ImGui::GetFrameHeight();
    // Barra de menus + cabeçalho com os espaços de trabalho.
    draw_menu(s);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, headerH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), S(6)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::Background);
    ImGui::Begin("##header", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
    draw_header(s);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + headerH));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - headerH - statusH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(4), S(2)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::Background);
    ImGui::Begin("##dockhost", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavFocus);
    const ImGuiID id = ImGui::GetID("AureaDock");
    if (dockspace == 0 || !ImGui::DockBuilderGetNode(id) || ImGui::DockBuilderGetNode(id)->IsEmpty()) {
        build_layout(s.workspace, id);
        dockspace = id;
    }
    ImGui::DockSpace(id, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // A ordem decide a aba visível de cada grupo na primeira abertura.
    draw_project(s);
    draw_effect_controls(s);
    draw_program(s);
    draw_timeline(s);
    draw_effects_browser(s);
    draw_audio_mixer(s);
    draw_graph_editor(s);
    draw_markers(s);
    draw_history(s);
    draw_info(s);
    draw_tools(s);
    draw_meters(s);
    draw_dialogs(s);

    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - statusH));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, statusH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(10), 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, col::Stage);
    ImGui::Begin("##status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoMove);
    draw_status_bar(s);
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::Render();
    update_preview_window(s);
    const float clear[4] = {0.039f, 0.055f, 0.075f, 1.f};
    s.context->OMSetRenderTargets(1, &gTarget, nullptr);
    s.context->ClearRenderTargetView(gTarget, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    const HRESULT hr = gSwap->Present(1, 0);
    gOccluded = hr == DXGI_STATUS_OCCLUDED;
    ++s.frameCounter;
    if ((s.frameCounter & 255) == 0) s.trim_thumbnails();
}

std::wstring known_folder(REFKNOWNFOLDERID id) {
    PWSTR data = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &data))) out = data;
    CoTaskMemFree(data);
    return out;
}
} // namespace

// ---------------------------------------------------------------------------
// Studio: ponte entre os painéis e o motor.
// ---------------------------------------------------------------------------
void Studio::send(Command c, const std::string& textPayload) {
    if (!ready) return;
    c.stringLength = u32(textPayload.size());
    if (engine.submit_commands(&c, 1, textPayload.empty() ? nullptr : textPayload.data(), u32(textPayload.size())) != 1)
        message("A fila de edição está ocupada. Tente de novo.");
    engine.request_render();
}

void Studio::message(std::string t) { notice = std::move(t); noticeTime = now_seconds(); }

void Studio::error(const Status& st, const char* what) {
    if (st.ok()) return;
    std::string m = what ? what : "Não foi possível concluir";
    const std::string detail(st.message());
    if (!detail.empty()) m += ": " + detail;
    message(m);
}

std::string Studio::timecode(i64 frame) const {
    const i64 f = std::max<i64>(1, std::llround(fps()));
    frame = std::max<i64>(0, frame);
    char b[48];
    std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld:%02lld", frame / f / 3600, (frame / f / 60) % 60, (frame / f) % 60, frame % f);
    return b;
}

LayerView* Studio::layer(u64 id) {
    auto it = layerIndex.find(id);
    return it == layerIndex.end() ? nullptr : &layers[it->second];
}

const std::vector<bridge::KeyframeRow>& Studio::layer_keys(u64 id) const {
    static const std::vector<bridge::KeyframeRow> none;
    auto it = keys.find(id);
    return it == keys.end() ? none : it->second;
}

bool Studio::is_selected(u64 id) const { return std::find(selection.begin(), selection.end(), id) != selection.end(); }

void Studio::set_selection(const std::vector<u64>& ids) {
    selection = ids;
    engine.set_selection(ids.data(), u32(ids.size()));
    primary = ids.empty() ? 0 : ids.front();
    selectedKeys.clear();
    graphTracks.clear();
    revision = ~0u;
}

void Studio::select(u64 id, bool add) {
    std::vector<u64> next = add ? selection : std::vector<u64>{};
    if (add && is_selected(id)) next.erase(std::find(next.begin(), next.end(), id));
    else if (id) { next.erase(std::remove(next.begin(), next.end(), id), next.end()); next.insert(next.begin(), id); }
    set_selection(next);
}

void Studio::seek(i64 frame) {
    Command c;
    c.type = CommandType::PlaybackSeek;
    c.seek.time = tick_at(FrameIndex{std::clamp<i64>(frame, 0, std::max<i64>(0, duration() - 1))}, fps());
    send(c);
}

void Studio::refresh() {
    if (!ready) return;
    status = engine.read_status();
    const bool importing = busy.load();
    if (!importing && status.modelRevision != revision) {
        revision = status.modelRevision;
        detailFrame = -1;
        const u32 capacity = std::max(32u, status.layerCount + 32);
        std::vector<bridge::LayerRow> rows(capacity);
        std::string blob(std::max<size_t>(65536, size_t(capacity) * 256), '\0');
        const u32 count = std::min(capacity, engine.query_layers(rows.data(), capacity, blob.data(), u32(blob.size())));
        layers.clear();
        layerIndex.clear();
        for (u32 i = 0; i < count; ++i) {
            LayerView v;
            v.row = rows[i];
            v.name = v.row.nameOffset + v.row.nameLength <= blob.size() ? blob.substr(v.row.nameOffset, v.row.nameLength) : "Camada";
            layerIndex[v.row.id] = int(layers.size());
            layers.push_back(std::move(v));
        }
        // Todos os keyframes numa consulta só.
        keys.clear();
        std::vector<bridge::KeyframeIndexRow> index(count + 8);
        std::vector<bridge::KeyframeRow> all(4096);
        u32 layerCount = 0;
        u32 total = engine.query_all_keyframes(index.data(), u32(index.size()), all.data(), u32(all.size()), &layerCount);
        if (total > all.size()) { all.resize(total + 256); total = engine.query_all_keyframes(index.data(), u32(index.size()), all.data(), u32(all.size()), &layerCount); }
        if (total <= all.size()) {
            size_t at = 0;
            for (u32 i = 0; i < std::min<u32>(layerCount, u32(index.size())); ++i) {
                auto& list = keys[index[i].layerId];
                list.assign(all.begin() + at, all.begin() + std::min(all.size(), at + index[i].count));
                at += index[i].count;
            }
        }
        // Seleção do motor (desfazer pode trazer outra).
        std::vector<u64> sel(std::max(16u, status.selectedCount + 4));
        sel.resize(engine.get_selection(sel.data(), u32(sel.size())));
        sel.erase(std::remove_if(sel.begin(), sel.end(), [&](u64 id) { return !layerIndex.count(id); }), sel.end());
        if (sel != selection) {
            // Mantém a primária quando ela continua escolhida.
            selection = sel;
            if (!is_selected(primary)) primary = sel.empty() ? 0 : sel.front();
        }
        if (primary && !layerIndex.count(primary)) primary = 0;
        // Faixas: camadas visuais pela linha (trackId), de trás para frente = V1, V2…
        videoTracks.clear();
        audioTracks.clear();
        std::map<u64, std::vector<int>> visualRows, soundRows;
        std::map<u64, u32> rowOrder;
        for (int i = 0; i < int(layers.size()); ++i) {
            const auto& l = layers[i];
            const u64 key = l.row.trackId ? u64(l.row.trackId) : (u64(1) << 40) | l.row.id;
            auto& rowsFor = l.audio() ? soundRows : visualRows;
            rowsFor[key].push_back(i);
            auto [it, inserted] = rowOrder.emplace(key, l.row.zIndex);
            if (!inserted) it->second = std::min(it->second, l.row.zIndex);
        }
        auto ordered = [&](std::map<u64, std::vector<int>>& m) {
            std::vector<std::pair<u32, std::vector<int>>> out;
            for (auto& [k, v] : m) out.push_back({rowOrder[k], v});
            std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.first > b.first; });  // fundo primeiro
            return out;
        };
        int n = 1;
        for (auto& [z, idx] : ordered(visualRows)) {
            TrackView t;
            t.number = n++;
            t.layers = idx;
            videoTracks.push_back(std::move(t));
        }
        // Vídeo com som aparece também na faixa de áudio correspondente (clipe vinculado).
        n = 1;
        for (auto& vt : videoTracks) {
            TrackView a;
            a.audio = true;
            a.number = n;
            for (int i : vt.layers) {
                const auto& l = layers[i];
                if (l.row.kind != u32(LayerKind::Video)) continue;
                auto known = hasSound.find(l.row.id);
                if (known == hasSound.end()) {
                    u8 probe[4]{};
                    const bool sound = engine.query_waveform(l.row.id, l.row.startFrame, std::max(1.0, (l.row.endFrame - l.row.startFrame) / 4.0), 4, probe) > 0;
                    known = hasSound.emplace(l.row.id, sound).first;
                }
                if (known->second) a.mirrors.push_back(i);
            }
            if (!a.mirrors.empty()) { audioTracks.push_back(std::move(a)); ++n; }
        }
        for (auto& [z, idx] : ordered(soundRows)) {
            TrackView t;
            t.audio = true;
            t.number = n++;
            t.layers = idx;
            audioTracks.push_back(std::move(t));
        }
        // Marcas.
        std::vector<i64> marks(3 * 1024);
        const u32 m = std::min(1024u, engine.query_markers(marks.data(), 1024));
        markers.assign(marks.begin(), marks.begin() + m * 3);
        // Efeitos da primária.
        effects.clear();
        hasText = false;
        if (primary) {
            std::vector<bridge::LayerEffectRow> fx(128);
            std::string names(65536, '\0');
            const u32 nfx = std::min<u32>(128, engine.query_layer_effects(primary, fx.data(), 128, names.data(), u32(names.size())));
            for (u32 i = 0; i < nfx; ++i) {
                EffectView e;
                e.row = fx[i];
                e.name = names.substr(fx[i].nameOffset, fx[i].nameLength);
                effects.push_back(std::move(e));
            }
            hasText = engine.query_text(primary, text);
        }
        u64 id = 0;
        u32 w = 0, h = 0;
        f64 f = 0;
        i64 d = 0;
        f32 bg[4]{};
        if (engine.query_composition(id, w, h, f, d, bg)) compId = id;
    }
    if (primary && (detailFrame != status.playhead.value || detailFrame < 0)) {
        detailFrame = status.playhead.value;
        if (!engine.query_layer_detail(primary, detail)) detail = {};
        for (auto& e : effects) {
            e.params.resize(256);
            e.blob.assign(65536, '\0');
            const u32 np = std::min<u32>(256, engine.query_effect_params(primary, e.row.effectId, e.params.data(), 256, e.blob.data(), u32(e.blob.size())));
            e.params.resize(np);
        }
    } else if (!primary) {
        detail = {};
    }
    if (status.thumbnailGeneration != thumbGeneration) {
        thumbGeneration = status.thumbnailGeneration;
        for (auto& [k, t] : thumbs) if (!t.srv) t.pending = false;
    }
    finish_tasks();
    autosave(now_seconds());
    update_meters(now_seconds(), ImGui::GetIO().DeltaTime);
}

float Studio::transform_value(u32 p) const {
    if (p < 3) return detail.position[p];
    if (p < 6) return detail.scale[p - 3];
    if (p < 9) return detail.rotation[p - 6];
    if (p < 12) return detail.anchor[p - 9];
    if (p == 12) return detail.opacity;
    if (p < 15) return detail.skew[p - 13];
    return 0;
}

void Studio::set_transform(u32 p, float value) {
    if (!primary || !std::isfinite(value)) return;
    const bool animate = autoKey || (detail.animatedMask & (1u << p));
    Command c;
    if (animate) {
        c.type = CommandType::KeyframeInsert;
        c.keyframe = {TrackRef{LayerId::unpack(primary), TrackProperty(p), kInvalidIndex, 0}, FrameIndex{detail.localPlayhead}, value, autoKey ? kAutoKeyOnlyIfChanged : 0u};
    } else {
        c.type = CommandType::LayerLayoutTransform;
        c.shape_param = {LayerId::unpack(primary), p, value};
    }
    send(c);
    // Valor local imediato: o painel não espera o motor para acompanhar o arrasto.
    if (p < 3) detail.position[p] = value;
    else if (p < 6) detail.scale[p - 3] = value;
    else if (p < 9) detail.rotation[p - 6] = value;
    else if (p < 12) detail.anchor[p - 9] = value;
    else if (p == 12) detail.opacity = value;
    else if (p < 15) detail.skew[p - 13] = value;
    detailFrame = -1;
}

static Command key_insert(u64 layer, const TrackKey& t, i32 time, float value) {
    Command c;
    c.type = CommandType::KeyframeInsert;
    c.keyframe = {TrackRef{LayerId::unpack(layer), TrackProperty(t.property), t.effect, t.param}, FrameIndex{time}, value, 0};
    return c;
}

void Studio::toggle_track_animation(u64 layerId, const std::vector<TrackKey>& tracks, const std::vector<float>& values) {
    if (!layerId) return;
    const auto& list = layer_keys(layerId);
    std::vector<std::pair<TrackKey, i32>> existing;
    for (auto& k : list) for (auto& t : tracks)
        if (k.property == t.property && k.effectIndex == t.effect && k.paramIndex == t.param) existing.push_back({t, k.time});
    begin_group();
    if (!existing.empty()) delete_keys(layerId, existing);
    else for (size_t i = 0; i < tracks.size(); ++i) send(key_insert(layerId, tracks[i], detail.localPlayhead, values[i]));
    end_group();
    detailFrame = -1;
}

void Studio::toggle_key_here(u64 layerId, const std::vector<TrackKey>& tracks, const std::vector<float>& values) {
    const auto& list = layer_keys(layerId);
    std::vector<std::pair<TrackKey, i32>> here;
    for (auto& k : list) for (auto& t : tracks)
        if (k.property == t.property && k.effectIndex == t.effect && k.paramIndex == t.param && k.time == detail.localPlayhead) here.push_back({t, k.time});
    begin_group();
    if (!here.empty()) delete_keys(layerId, here);
    else for (size_t i = 0; i < tracks.size(); ++i) send(key_insert(layerId, tracks[i], detail.localPlayhead, values[i]));
    end_group();
    detailFrame = -1;
}

void Studio::delete_keys(u64 layerId, const std::vector<std::pair<TrackKey, i32>>& which) {
    begin_group();
    for (auto& [t, time] : which) {
        Command c;
        c.type = CommandType::KeyframeDelete;
        c.keyframe = {TrackRef{LayerId::unpack(layerId), TrackProperty(t.property), t.effect, t.param}, FrameIndex{time}, 0, 0};
        send(c);
    }
    end_group();
}

void Studio::move_key(u64 layerId, const TrackKey& t, i32 from, i32 to) {
    if (from == to) return;
    Command c;
    c.type = CommandType::KeyframeMove;
    c.keyframe_move = {TrackRef{LayerId::unpack(layerId), TrackProperty(t.property), t.effect, t.param}, FrameIndex{from}, FrameIndex{to}};
    send(c);
}

void Studio::set_key_value(u64 layerId, const TrackKey& t, i32 time, float value) {
    Command c;
    c.type = CommandType::KeyframeSetValue;
    c.keyframe = {TrackRef{LayerId::unpack(layerId), TrackProperty(t.property), t.effect, t.param}, FrameIndex{time}, value, 0};
    send(c);
}

void Studio::set_bezier(u64 layerId, const TrackKey& t, i32 time, Interpolation in, float bx1, float by1, float bx2, float by2) {
    Command c;
    c.type = in == Interpolation::Bezier ? CommandType::KeyframeSetBezier : CommandType::KeyframeSetEasing;
    c.keyframe_interp.track = TrackRef{LayerId::unpack(layerId), TrackProperty(t.property), t.effect, t.param};
    c.keyframe_interp.time = FrameIndex{time};
    c.keyframe_interp.interp = in;
    c.keyframe_interp.power = 1;
    c.keyframe_interp.linkAxes = 0;
    c.keyframe_interp.bx1 = bx1;
    c.keyframe_interp.by1 = by1;
    c.keyframe_interp.bx2 = bx2;
    c.keyframe_interp.by2 = by2;
    send(c);
}

bool Studio::key_easing(u64 layerId, const TrackKey& t, i32 time, float out[4]) {
    return engine.query_keyframe_easing(layerId, t.property, t.effect, t.param, time, out);
}

// Suavização no estilo das suítes de animação: cada trecho é uma cúbica
// normalizada guardada no keyframe da esquerda; "entrada" mexe na alça de
// chegada (trecho anterior), "saída" na de partida (trecho seguinte).
void Studio::apply_ease(int kind) {
    if (selectedKeys.empty() || !primary) { message("Selecione keyframes no editor de gráfico ou nos controles de efeito."); return; }
    begin_group();
    for (auto& [t, time] : selectedKeys) {
        std::vector<i32> times;
        for (auto& k : layer_keys(primary)) if (k.property == t.property && k.effectIndex == t.effect && k.paramIndex == t.param) times.push_back(k.time);
        std::sort(times.begin(), times.end());
        auto at = std::find(times.begin(), times.end(), time);
        if (at == times.end()) continue;
        const bool hasPrev = at != times.begin(), hasNext = at + 1 != times.end();
        auto current = [&](i32 key, float e[4]) {
            if (!key_easing(primary, t, key, e)) { e[0] = 1 / 3.f; e[1] = 1 / 3.f; e[2] = 2 / 3.f; e[3] = 2 / 3.f; }
        };
        float e[4];
        if (kind == 3 || kind == 4) {
            const Interpolation in = kind == 3 ? Interpolation::Linear : Interpolation::Hold;
            set_bezier(primary, t, time, in, 1 / 3.f, 1 / 3.f, 2 / 3.f, 2 / 3.f);
            continue;
        }
        if (kind >= 5) {
            const Interpolation families[] = {Interpolation::Bounce, Interpolation::Elastic, Interpolation::Overshoot};
            const Interpolation in = families[std::min(kind - 5, 2)];
            Command c;
            c.type = CommandType::KeyframeSetEasing;
            c.keyframe_interp.track = TrackRef{LayerId::unpack(primary), TrackProperty(t.property), t.effect, t.param};
            c.keyframe_interp.time = FrameIndex{time};
            c.keyframe_interp.interp = in;
            c.keyframe_interp.power = 1;
            c.keyframe_interp.bx1 = in == Interpolation::Elastic ? 3.f : in == Interpolation::Overshoot ? 1.70158f : .42f;
            c.keyframe_interp.by1 = 0;
            c.keyframe_interp.bx2 = .58f;
            c.keyframe_interp.by2 = 1;
            send(c);
            continue;
        }
        if ((kind == 0 || kind == 2) && hasNext) {  // saída
            current(time, e);
            set_bezier(primary, t, time, Interpolation::Bezier, 1 / 3.f, 0, e[2], e[3]);
        }
        if ((kind == 0 || kind == 1) && hasPrev) {  // entrada
            const i32 prev = *(at - 1);
            current(prev, e);
            set_bezier(primary, t, prev, Interpolation::Bezier, e[0], e[1], 2 / 3.f, 1);
        }
    }
    end_group();
    static const char* names[] = {"Suavização fácil", "Suavização fácil de entrada", "Suavização fácil de saída", "Linear", "Manter", "Quicar", "Elástico", "Ultrapassar"};
    message(std::string(names[std::clamp(kind, 0, 7)]) + " aplicada.");
}

void Studio::set_effect_value(const EffectView& fx, const bridge::EffectParamRow& p, u32 component, float value) {
    if (!primary || !std::isfinite(value)) return;
    const u32 comps = p.type == u32(ParamType::Point2D) ? 2 : p.type == u32(ParamType::Point3D) ? 3 : p.type == u32(ParamType::Color) ? 4 : 1;
    if (p.animated || autoKey) {
        send(key_insert(primary, {u32(TrackProperty::EffectParam), fx.row.effectId, p.index * 4 + component}, detail.localPlayhead, value));
    } else if (comps > 1) {
        float v[4] = {p.value[0], p.value[1], p.value[2], p.value[3]};
        v[component] = value;
        set_effect_vector(fx, p, v);
        return;
    } else {
        Command c;
        c.type = CommandType::EffectSetParam;
        c.effect_param = {LayerId::unpack(primary), EffectId{fx.row.effectId, 0}, p.index, value};
        send(c);
    }
    detailFrame = -1;
}

void Studio::set_effect_vector(const EffectView& fx, const bridge::EffectParamRow& p, const float* v) {
    if (!primary) return;
    if (p.animated || autoKey) {
        const u32 comps = p.type == u32(ParamType::Point2D) ? 2 : p.type == u32(ParamType::Point3D) ? 3 : 4;
        begin_group();
        for (u32 c = 0; c < comps; ++c)
            send(key_insert(primary, {u32(TrackProperty::EffectParam), fx.row.effectId, p.index * 4 + c}, detail.localPlayhead, v[c]));
        end_group();
    } else {
        Command c;
        c.type = CommandType::EffectSetColorParam;
        c.effect_color = {LayerId::unpack(primary), EffectId{fx.row.effectId, 0}, p.index, v[0], v[1], v[2], v[3]};
        send(c);
    }
    detailFrame = -1;
}

void Studio::apply_effect(u32 type, u64 target) {
    if (!target) target = primary;
    if (!target) { message("Selecione um clipe na linha do tempo antes de aplicar o efeito."); return; }
    Command c;
    c.type = CommandType::EffectAdd;
    c.effect_add = {LayerId::unpack(target), type, UINT32_MAX};
    send(c);
    if (target != primary) select(target);
    for (auto& item : catalog) if (item.type == type) message("Efeito aplicado: " + item.name);
}

void Studio::add_marker() {
    engine.toggle_marker(playhead());
    revision = ~0u;
}

void Studio::navigate(int direction) {
    i64 target = 0;
    if (engine.query_timeline_navigation(primary, playhead(), direction, target)) {
        Command c;
        c.type = CommandType::PlaybackSeek;
        c.seek.time = tick_at(FrameIndex{target}, fps());
        c.seek.flags = kSeekTimelineEvent;
        send(c);
    }
}

void Studio::split_at_playhead() {
    // Adicionar edição: os clipes escolhidos; sem escolha, todos sob o indicador.
    std::vector<u64> targets;
    for (u64 id : selection) if (auto* l = layer(id); l && l->row.startFrame < playhead() && l->row.endFrame > playhead()) targets.push_back(id);
    if (targets.empty()) for (auto& l : layers) if (!l.locked() && l.row.startFrame < playhead() && l.row.endFrame > playhead()) targets.push_back(l.row.id);
    if (targets.empty()) { message("Nenhum clipe sob o indicador de reprodução."); return; }
    begin_group();
    for (u64 id : targets) {
        Command c;
        c.type = CommandType::LayerSplit;
        c.layer_split = {LayerId::unpack(id), status.playhead, {}};
        send(c);
    }
    end_group();
}

void Studio::delete_selection() {
    if (!selectedKeys.empty() && primary) {
        delete_keys(primary, selectedKeys);
        selectedKeys.clear();
        return;
    }
    if (selection.empty()) return;
    begin_group();
    for (u64 id : selection) {
        Command c;
        c.type = CommandType::LayerDelete;
        c.layer_ref.layer = LayerId::unpack(id);
        send(c);
    }
    end_group();
    set_selection({});
}

// --- Arquivo -----------------------------------------------------------------
static std::wstring choose_file(HWND owner, bool saving, const wchar_t* filter, const wchar_t* ext, const std::wstring& initial = {}, bool multi = false) {
    std::wstring buffer(65536, L'\0');
    if (!initial.empty()) wcsncpy_s(buffer.data(), buffer.size(), initial.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = DWORD(buffer.size());
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (saving ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST) | (multi ? OFN_ALLOWMULTISELECT : 0);
    if (!(saving ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn))) return {};
    if (multi) return std::wstring(buffer.data(), buffer.size());
    return buffer.c_str();
}

bool Studio::save(bool as) {
    if (busy) return false;
    std::wstring path = projectPath.wstring();
    if (path.empty() || as) path = choose_file(window, true, L"Projeto AUREA (*.aurea)\0*.aurea\0\0", L"aurea", title + L".aurea");
    if (path.empty()) return false;
    const Status r = engine.save_project(utf8(path).c_str());
    if (!r.ok()) { error(r, "Não foi possível salvar"); return false; }
    projectPath = path;
    title = projectPath.stem().wstring();
    message("Projeto salvo. As mídias continuam nas pastas de origem.");
    return true;
}

bool Studio::may_discard() {
    if (busy) { message("Aguarde a importação terminar."); return false; }
    if (engine.export_progress().running) { message("Aguarde a exportação ou cancele-a."); return false; }
    if (!engine.read_status().dirty) return true;
    const int answer = MessageBoxW(window, L"Salvar as alterações do projeto antes de continuar?", L"AUREA Studio", MB_YESNOCANCEL | MB_ICONQUESTION);
    return answer == IDNO || (answer == IDYES && save());
}

void Studio::new_project() {
    if (!may_discard()) return;
    error(engine.new_project(1920, 1080, 30, "Sequência 01"), "Não foi possível criar o projeto");
    projectPath.clear();
    title = L"Sem título";
    set_selection({});
    scrollFrame = 0;
    hasSound.clear();
    message("Novo projeto: 1920 × 1080, 30 qps.");
}

void Studio::open_project() {
    const auto p = choose_file(window, false, L"Projeto AUREA (*.aurea)\0*.aurea\0\0", L"aurea");
    if (!p.empty()) import_files({fs::path(p)});
}

void Studio::import_dialog() {
    const std::wstring raw = choose_file(window, false,
        L"Mídia e projetos\0*.mp4;*.mov;*.m4v;*.mkv;*.webm;*.avi;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp;*.gif;*.psd;*.svg;*.mp3;*.wav;*.m4a;*.aac;*.flac;*.ogg;*.glb;*.gltf;*.fbx;*.obj;*.aurea\0Todos os arquivos\0*.*\0\0",
        nullptr, {}, true);
    if (raw.empty() || !raw[0]) return;
    std::vector<fs::path> files;
    std::vector<std::wstring> parts;
    for (size_t at = 0; at < raw.size() && raw[at];) {
        const size_t end = raw.find(L'\0', at);
        parts.push_back(raw.substr(at, end - at));
        at = end + 1;
    }
    if (parts.size() == 1) files.emplace_back(parts[0]);
    else for (size_t i = 1; i < parts.size(); ++i) files.push_back(fs::path(parts[0]) / parts[i]);
    import_files(std::move(files));
}

void Studio::run(std::function<void()> work) {
    if (busy) { message("Aguarde a tarefa em andamento."); return; }
    busy = true;
    task = std::async(std::launch::async, [this, work = std::move(work)] {
        work();
        busy = false;
        PostMessageW(window, WM_STUDIO_WAKE, 0, 0);
    });
}

void Studio::finish_tasks() {
    if (busy || !task.valid()) return;
    task.get();
    std::lock_guard lock(taskMutex);
    if (!taskNotices.empty()) message(taskNotices.back());
    taskNotices.clear();
    if (!taskSelect.empty()) set_selection({taskSelect.back()});
    taskSelect.clear();
    revision = ~0u;
}

void Studio::import_files(std::vector<fs::path> files) {
    if (files.empty()) return;
    for (auto& f : files) {
        auto ext = f.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        if (ext == L".aurea") {
            if (!may_discard()) return;
            const fs::path path = f;
            run([this, path] {
                const Status st = engine.load_project(utf8(path.wstring()).c_str());
                std::lock_guard lock(taskMutex);
                if (st.ok()) {
                    projectPath = path;
                    title = path.stem().wstring();
                    hasSound.clear();
                    taskNotices.push_back(engine.last_load_notice() ? "Projeto aberto com aviso: confira as mídias ausentes." : "Projeto aberto.");
                } else taskNotices.push_back("Não foi possível abrir o projeto: " + std::string(st.message()));
            });
            return;
        }
    }
    message("Importando " + std::to_string(files.size()) + (files.size() == 1 ? " arquivo…" : " arquivos…"));
    run([this, files] {
        for (const auto& path : files) {
            auto ext = path.extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            const std::string p = utf8(path.wstring()), name = utf8(path.stem().wstring());
            Result<u64> result{Status{Errc::UnsupportedFormat}};
            if (ext == L".glb" || ext == L".gltf" || ext == L".fbx" || ext == L".obj") { ModelImport r; r.path = p; r.displayName = name; result = engine.import_model(r); }
            else if (ext == L".svg") { std::ifstream in(path, std::ios::binary); std::string data((std::istreambuf_iterator<char>(in)), {}); result = engine.import_svg(data, name.c_str()); }
            else if (ext == L".psd") result = engine.import_psd(p, name.c_str());
            else if (ext == L".gif") result = engine.import_gif(p, name.c_str());
            else if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" || ext == L".tif" || ext == L".tiff" || ext == L".webp") {
                ImagePixels img;
                if (windows::load_image(p.c_str(), img, nullptr)) result = engine.import_image(img.rgba.data(), img.width, img.height, name.c_str(), p.c_str());
                else result = Status{Errc::UnsupportedFormat, "imagem não reconhecida pelo Windows"};
            } else {
                MediaProbe probe;
                if (media.probe(p.c_str(), probe)) {
                    VideoImport r;
                    r.sourcePath = p;
                    r.displayName = name;
                    result = probe.hasVideo ? engine.import_video(r) : engine.import_audio(r);
                } else result = Status{Errc::UnsupportedCodec, "use vídeo H.264/HEVC ou áudio compatível com o Windows"};
            }
            std::lock_guard lock(taskMutex);
            if (result.ok()) { taskSelect.push_back(*result); taskNotices.push_back("Importado: " + utf8(path.filename().wstring())); }
            else taskNotices.push_back("Não foi possível importar " + utf8(path.filename().wstring()) + ": " + std::string(result.status().message()));
        }
    });
}

void Studio::export_frame() {
    const auto path = choose_file(window, true, L"Imagem PNG\0*.png\0\0", L"png", title + L" " + wide(timecode(playhead())).substr(0, 0) + L".png");
    if (path.empty()) return;
    run([this, path] {
        std::vector<u8> pixels;
        u32 w = 0, h = 0;
        Status st = engine.capture_frame_rgba(std::max(status.compWidth, status.compHeight), pixels, w, h);
        if (st.ok() && !windows::save_png(path, pixels.data(), w, h)) st = Status{Errc::IoError};
        std::lock_guard lock(taskMutex);
        taskNotices.push_back(st.ok() ? "Quadro exportado em PNG." : "Não foi possível exportar o quadro: " + std::string(st.message()));
    });
}

void Studio::start_export() {
    ExportDialog& d = exporter;
    if (!d.path[0]) return;
    ExportSettings settings;
    const u32 shortSide = std::min(status.compWidth, status.compHeight);
    const u32 sides[] = {shortSide, 2160, 1440, 1080, 720};
    settings.height = sides[std::clamp(d.resolution, 0, 4)];
    settings.width = status.compWidth;
    const double rates[] = {0, 24, 25, 30, 50, 60};
    settings.fps = rates[std::clamp(d.fps, 0, 5)];
    settings.videoCodec = d.codec == 1 ? ExportCodec::HEVC : ExportCodec::H264;
    settings.container = u32(d.container);
    settings.videoBitrateMbps = u32(std::max(0, d.bitrate));
    const u32 audioRates[] = {128, 192, 320};
    settings.audioBitrateKbps = audioRates[std::clamp(d.audio, 0, 2)];
    const Status st = engine.start_export(settings, d.path);
    if (st.ok()) message("Exportando…"); else error(st, "Não foi possível exportar");
}

void Studio::autosave(double now) {
    if (now - lastAutosave < 90) return;
    lastAutosave = now;
    if (status.dirty && !busy && !engine.export_progress().running) (void)engine.autosave_project();
}

// --- Miniaturas: texturas D3D11 em cache, pedidas ao motor sem bloquear ----
ID3D11ShaderResourceView* Studio::thumbnail(u64 layerId, i32 frame, u32 height, u32* outW, u32* outH) {
    const u64 key = (layerId * 1000003ull) ^ (u64(u32(frame)) << 20) ^ height;
    Thumb& t = thumbs[key];
    t.used = frameCounter;
    if (t.srv) { *outW = t.w; *outH = t.h; return t.srv; }
    if (t.pending && t.generation == thumbGeneration) return nullptr;
    t.pending = true;
    t.generation = thumbGeneration;
    static std::vector<u8> pixels;
    pixels.resize(size_t(height) * height * 4 * 4);
    u32 w = 0;
    const u32 bytes = engine.query_thumbnail(layerId, frame, height, pixels.data(), u32(pixels.size()), &w);
    if (!bytes || !w) return nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), w * 4, 0};
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &data, &tex))) return nullptr;
    device->CreateShaderResourceView(tex, nullptr, &t.srv);
    tex->Release();
    t.w = w;
    t.h = height;
    *outW = w;
    *outH = height;
    return t.srv;
}

void Studio::trim_thumbnails() {
    for (auto it = thumbs.begin(); it != thumbs.end();) {
        if (frameCounter - it->second.used > 600) { if (it->second.srv) it->second.srv->Release(); it = thumbs.erase(it); }
        else ++it;
    }
}

// Medidores: nível do som no indicador, pela forma de onda que o motor já calcula.
void Studio::update_meters(double now, float dt) {
    float level[2] = {0, 0};
    const i64 f = playhead();
    auto take = [&](const LayerView& l, float gainPan) {
        if (f < l.row.startFrame || f >= l.row.endFrame || !l.visible()) return;
        u8 v = 0;
        if (engine.query_waveform(l.row.id, double(f), 1.0, 1, &v) == 0) return;
        const float a = (v / 255.f) * (v / 255.f) * gainPan;
        level[0] = std::max(level[0], a);
        level[1] = std::max(level[1], a);
    };
    for (auto& t : audioTracks) {
        for (int i : t.layers) take(layers[i], 1);
        for (int i : t.mirrors) take(layers[i], 1);
    }
    for (int c = 0; c < 2; ++c) {
        const float target = status.playing ? level[c] : 0.f;
        meterLevel[c] = target > meterLevel[c] ? target : std::max(target, meterLevel[c] - dt * 1.6f);
        if (meterLevel[c] >= meterPeak[c]) { meterPeak[c] = meterLevel[c]; meterPeakTime[c] = now; }
        else if (now - meterPeakTime[c] > 1.2) meterPeak[c] = std::max(meterLevel[c], meterPeak[c] - dt * .8f);
    }
}

} // namespace aurea::studio

using namespace aurea;
using namespace aurea::studio;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    ImGui_ImplWin32_EnableDpiAwareness();
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!windows::initialize_media()) {
        MessageBoxW(nullptr, L"O Windows não conseguiu iniciar os serviços de mídia.", L"AUREA Studio", MB_ICONERROR);
        LocalFree(argv);
        return 1;
    }
    int exitCode = 0;
    {
        auto studio = std::make_unique<Studio>();
        Studio& s = *studio;
        gStudio = &s;
        const std::wstring local = known_folder(FOLDERID_LocalAppData);
        s.appDirectory = local.empty() ? fs::temp_directory_path() / L"AureaStudio" : fs::path(local) / L"AUREA" / L"Studio";
        std::error_code ec;
        fs::create_directories(s.appDirectory / L"cache", ec);

        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = window_proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
        wc.hbrBackground = CreateSolidBrush(RGB(15, 20, 26));
        wc.lpszClassName = L"AureaStudio";
        RegisterClassExW(&wc);
        WNDCLASSW pc{};
        pc.lpfnWndProc = preview_proc;
        pc.hInstance = instance;
        pc.lpszClassName = L"AureaStudioProgram";
        pc.style = CS_DBLCLKS;
        pc.hbrBackground = CreateSolidBrush(RGB(10, 14, 19));
        RegisterClassW(&pc);
        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"AUREA Studio", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1600, 960, nullptr, nullptr, instance, nullptr);
        if (!hwnd) { LocalFree(argv); return 1; }
        s.window = hwnd;
        s.dpi = GetDpiForWindow(hwnd) / 96.f;
        // Barra de título escura na cor de fundo do AUREA.
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark);
        COLORREF caption = RGB(15, 20, 26), captionText = RGB(247, 249, 251);
        DwmSetWindowAttribute(hwnd, 35, &caption, sizeof caption);
        DwmSetWindowAttribute(hwnd, 36, &captionText, sizeof captionText);
        if (!create_device(hwnd)) {
            MessageBoxW(hwnd, L"Não foi possível iniciar o Direct3D 11 da interface.", L"AUREA Studio", MB_ICONERROR);
            DestroyWindow(hwnd);
            LocalFree(argv);
            return 1;
        }
        s.preview = CreateWindowExW(0, pc.lpszClassName, L"Programa", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 16, 9, hwnd, nullptr, instance, nullptr);
        DragAcceptFiles(hwnd, TRUE);
        ShowWindow(hwnd, show == SW_SHOWDEFAULT || show == SW_SHOWNORMAL ? SW_SHOWMAXIMIZED : show);
        UpdateWindow(hwnd);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.ConfigDockingWithShift = false;
        static std::string ini = utf8((s.appDirectory / L"studio-layout.ini").wstring());
        io.IniFilename = ini.c_str();
        load_fonts(s.dpi);
        apply_theme(s.dpi);
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(s.device, s.context);

        s.audioOut = windows::make_audio_output();
        EngineConfig cfg;
        cfg.backend = new vk::Backend;
        cfg.mediaFactory = &s.media;
        cfg.imageLoader = windows::load_image;
        cfg.exportSinkFactory = make_ffmpeg_export_sink;
        cfg.audioOutput = s.audioOut.get();
        cfg.documentsDirectory = utf8(s.appDirectory.wstring());
        cfg.cacheDirectory = utf8((s.appDirectory / L"cache").wstring());
        cfg.defaultFontPath = "C:/Windows/Fonts/segoeui.ttf";
        cfg.workerCount = std::clamp<u32>(std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 2 : 2, 2, 12);
        const Status init = s.engine.initialize(cfg);
        if (!init.ok()) {
            MessageBoxW(hwnd, (L"Não foi possível iniciar a GPU do AUREA.\nAtualize o driver de vídeo com suporte a Vulkan.\n\n" + wide(std::string(init.message()))).c_str(), L"AUREA Studio", MB_ICONERROR);
            exitCode = 1;
        } else {
            s.ready = true;
            (void)s.engine.new_project(1920, 1080, 30, "Sequência 01");
            if (s.engine.read_status().recoveryAvailable) {
                if (MessageBoxW(hwnd, L"Há uma sessão que não foi salva. Recuperar?", L"AUREA Studio", MB_YESNO | MB_ICONQUESTION) == IDYES) s.error(s.engine.recover_session(), "Não foi possível recuperar");
                else s.engine.discard_recovery();
            }
            std::vector<bridge::EffectCatalogRow> rows(4096);
            std::string blob(1 << 20, '\0');
            const u32 n = std::min<u32>(u32(rows.size()), s.engine.query_effect_catalog(rows.data(), u32(rows.size()), blob.data(), u32(blob.size())));
            std::set<std::string> cats;
            for (u32 i = 0; i < n; ++i) {
                CatalogItem item;
                item.type = rows[i].typeId;
                item.name = blob.substr(rows[i].nameOffset, rows[i].nameLength);
                item.category = blob.substr(rows[i].categoryOffset, rows[i].categoryLength);
                if (item.category.empty()) item.category = "Outros";
                item.fresh = rows[i].reserved & 1u;
                cats.insert(item.category);
                s.catalog.push_back(std::move(item));
            }
            std::sort(s.catalog.begin(), s.catalog.end(), [](auto& a, auto& b) { return a.name < b.name; });
            s.categories.assign(cats.begin(), cats.end());
            RECT r{0, 0, 16, 9};
            const Status attached = s.engine.attach_surface(s.preview, 16, 9);
            if (!attached.ok()) s.error(attached, "Prévia indisponível");
            (void)r;
            s.engine.start_render_thread();
            if (argc > 1) s.import_files({fs::path(argv[1])});

            ImGuiID dockspace = GetFileAttributesW((s.appDirectory / L"studio-layout.ini").c_str()) == INVALID_FILE_ATTRIBUTES ? 0 : 1;
            bool running = true;
            int pending = 3;
            EngineStatus last{};
            u32 lastExportFrames = 0;
            while (running) {
                const bool animating = s.status.playing || s.busy || s.engine.export_progress().running || s.previewDrag;
                if (pending <= 0) {
                    const double idle = now_seconds() - gLastInput;
                    const DWORD timeout = animating ? 8 : idle < 1.0 ? 33 : 120;
                    MsgWaitForMultipleObjectsEx(0, nullptr, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                }
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    if (msg.message == WM_QUIT) { running = false; exitCode = int(msg.wParam); break; }
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                    if (msg.message != WM_TIMER) { pending = 3; gLastInput = now_seconds(); }
                }
                if (!running) break;
                const EngineStatus st = s.engine.read_status();
                const auto ex = s.engine.export_progress();
                if (st.playhead.value != last.playhead.value || st.playing != last.playing || st.modelRevision != last.modelRevision ||
                    st.thumbnailGeneration != last.thumbnailGeneration || st.dirty != last.dirty || ex.framesDone != lastExportFrames || ex.running)
                    pending = std::max(pending, 1);
                if (now_seconds() - gLastInput < 1.0) pending = std::max(pending, 1);  // dicas e animações curtas
                last = st;
                lastExportFrames = ex.framesDone;
                if (pending <= 0 || gOccluded) {
                    if (gOccluded && gSwap->Present(0, DXGI_PRESENT_TEST) == S_OK) gOccluded = false;
                    if (gOccluded) { Sleep(50); continue; }
                    continue;
                }
                --pending;
                WaitForSingleObjectEx(gFrameWait, 100, TRUE);
                render_frame(s, dockspace);
            }
            if (s.task.valid()) s.task.wait();
            s.engine.stop_render_thread();
            s.engine.detach_surface();
        }
        for (auto& [k, t] : s.thumbs) if (t.srv) t.srv->Release();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        s.engine.shutdown();
        destroy_device();
        if (IsWindow(hwnd)) DestroyWindow(hwnd);
        gStudio = nullptr;
    }
    LocalFree(argv);
    windows::shutdown_media();
    return exitCode;
}
