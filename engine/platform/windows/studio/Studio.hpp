#pragma once
// AUREA Studio (PC). Arranjo de edição no estilo das suítes de vídeo para desktop:
// projeto, controles de efeito, programa, linha do tempo e editor de gráfico.
// A UI só controla o motor: edição, animação, render e arquivo ficam no aurea_core.
#include "../WindowsMedia.hpp"
#include "imgui.h"
#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <functional>
#include <tuple>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace aurea::studio {

// ---------------------------------------------------------------------------
// Paleta AUREA (android ui/theme/AureaTokens.kt, paleta "Aurea").
// ---------------------------------------------------------------------------
namespace col {
inline constexpr ImU32 rgb(u32 hex, u32 a = 255) { return IM_COL32((hex >> 16) & 255, (hex >> 8) & 255, hex & 255, a); }
inline constexpr ImU32 Stage = rgb(0x0A0E13);
inline constexpr ImU32 Background = rgb(0x0F141A);
inline constexpr ImU32 Surface = rgb(0x151C24);
inline constexpr ImU32 SurfaceHigh = rgb(0x1B2530);
inline constexpr ImU32 Chip = rgb(0x212D3A);
inline constexpr ImU32 ChipHigh = rgb(0x323D49);
inline constexpr ImU32 Border = rgb(0x273442);
inline constexpr ImU32 Hairline = rgb(0x273442, 184);
inline constexpr ImU32 Text = rgb(0xF7F9FB);
inline constexpr ImU32 Muted = rgb(0xAAB6C3);
inline constexpr ImU32 Subtle = rgb(0x7C8A99);
inline constexpr ImU32 Brand = rgb(0x245D8C);
inline constexpr ImU32 BrandDeep = rgb(0x123A63);
inline constexpr ImU32 Accent = rgb(0x6FAED9);
inline constexpr ImU32 AccentDim = rgb(0x1D3A55);
inline constexpr ImU32 Keyframe = rgb(0xA9D3EC);
inline constexpr ImU32 KeyframeDim = rgb(0x22405A);
inline constexpr ImU32 TickWeak = rgb(0x43516A);
inline constexpr ImU32 TickStrong = rgb(0x7485A3);
inline constexpr ImU32 CurveGrid = rgb(0x34405A);
inline constexpr ImU32 Playhead = rgb(0xFFFFFF);
inline constexpr ImU32 Danger = rgb(0xFF6B6B);
inline constexpr ImU32 Warning = rgb(0xFFC978);
inline constexpr ImU32 Success = rgb(0x4CD08A);
inline constexpr ImU32 Beta = rgb(0xFFB020);
// Clipes: matizes da marca, um por tipo de mídia.
inline constexpr ImU32 ClipVideo = rgb(0x3D72A3);
inline constexpr ImU32 ClipImage = rgb(0x2E7F8C);
inline constexpr ImU32 ClipGraphic = rgb(0x8B5FA6);
inline constexpr ImU32 Clip3D = rgb(0x9C6B3C);
inline constexpr ImU32 ClipAudio = rgb(0x3C8A63);
inline constexpr ImU32 ClipUtility = rgb(0x5D6B82);
// Eixos do editor de gráfico (X, Y, Z).
inline constexpr ImU32 AxisX = rgb(0xFF6B6B);
inline constexpr ImU32 AxisY = rgb(0x4CD08A);
inline constexpr ImU32 AxisZ = rgb(0x6FAED9);
inline ImU32 alpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | (ImU32(std::clamp(a, 0.f, 1.f) * ((c >> 24) & 255)) << 24); }
ImU32 mix(ImU32 a, ImU32 b, float t);
} // namespace col

enum class Tool : u8 { Select, TrackForward, Ripple, Razor, Slip, Pen, Hand, Type };
enum class Workspace : u8 { Assembly, Editing, Effects, Audio, Graphics, Count };

// Ícones vetoriais desenhados na hora (nenhum asset de terceiros).
enum class Icon : u8 {
    Play, Pause, StepBack, StepForward, ToStart, ToEnd, Marker, MarkIn, MarkOut, Camera,
    Lock, Unlock, Eye, EyeOff, Speaker, Mute, Mic, Magnet, Link, Wrench, Stopwatch, Diamond,
    ChevronLeft, ChevronRight, ChevronDown, Plus, Trash, Fx, Folder, Film, Image, Audio, Text,
    Shape, Null, Light, Search, List, Grid, Select, TrackForward, Ripple, Razor, Slip, Pen, Hand,
    Type, Reset, Loop, Export, Undo, Redo, Graph, Ease, Hold, Linear, Pick, Close, Gear, Layers,
};
void draw_icon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);
bool icon_button(const char* id, Icon icon, const char* tooltip, bool active = false, float size = 0.f, bool enabled = true);

struct LayerView {
    bridge::LayerRow row{};
    std::string name;
    [[nodiscard]] bool audio() const { return row.kind == u32(LayerKind::Audio); }
    [[nodiscard]] bool visible() const { return row.flags & bridge::kLayerRowFlagVisible; }
    [[nodiscard]] bool locked() const { return row.flags & bridge::kLayerRowFlagLocked; }
};

struct TrackView {
    bool audio = false;
    int number = 1;               ///< V1, V2… / A1, A2…
    std::vector<int> layers;      ///< índices em Studio::layers
    std::vector<int> mirrors;     ///< vídeo com som: o mesmo clipe mostrado na faixa de áudio
    float height = 0.f;
};

struct EffectView {
    bridge::LayerEffectRow row{};
    std::string name;
    std::vector<bridge::EffectParamRow> params;
    std::string blob;
    [[nodiscard]] std::string text(u32 offset, u32 length) const {
        return offset + length <= blob.size() ? blob.substr(offset, length) : std::string{};
    }
};

struct CatalogItem { u32 type = 0; std::string name, category; bool fresh = false; };

/// Uma trilha de keyframes: propriedade + efeito + parâmetro (param*4+componente).
struct TrackKey {
    u32 property = 0, effect = kInvalidIndex, param = 0;
    bool operator==(const TrackKey&) const = default;
    bool operator<(const TrackKey& o) const { return std::tie(property, effect, param) < std::tie(o.property, o.effect, o.param); }
};

struct Thumb { ID3D11ShaderResourceView* srv = nullptr; u32 w = 0, h = 0; u32 used = 0; bool pending = false; u32 generation = 0; };

struct ExportDialog {
    bool open = false;
    char path[1024]{};
    int resolution = 0;   ///< 0 composição, 1 2160, 2 1440, 3 1080, 4 720
    int fps = 0;          ///< 0 composição, 1 24, 2 25, 3 30, 4 50, 5 60
    int codec = 0;        ///< 0 H.264, 1 HEVC
    int container = 0;    ///< 0 MP4, 1 MOV
    int bitrate = 0;      ///< 0 automático, senão Mbps
    int audio = 1;        ///< 0 128, 1 192, 2 320 kbps
};

struct SequenceDialog { bool open = false; int width = 1920, height = 1080; float fps = 30, seconds = 10; float bg[4]{0, 0, 0, 1}; };

struct Studio {
    // --- Plataforma ---------------------------------------------------------
    HWND window = nullptr, preview = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    float dpi = 1.f;
    std::filesystem::path appDirectory, projectPath;
    std::wstring title = L"Sem título";
    // --- Motor --------------------------------------------------------------
    Engine engine;
    windows::MediaFactory media;
    std::unique_ptr<audio::AudioOutput> audioOut;
    bool ready = false;
    EngineStatus status{};
    u32 revision = ~0u;
    i64 detailFrame = -1;
    u32 thumbGeneration = ~0u;
    // --- Modelo lido do motor (somente leitura; relido por revisão) ---------
    std::vector<LayerView> layers;
    std::unordered_map<u64, int> layerIndex;
    std::unordered_map<u64, std::vector<bridge::KeyframeRow>> keys;
    std::vector<TrackView> videoTracks, audioTracks;
    std::unordered_map<u64, bool> hasSound;
    std::vector<u64> selection;
    u64 primary = 0;
    bridge::LayerDetailPOD detail{};
    std::vector<EffectView> effects;
    std::vector<CatalogItem> catalog;
    std::vector<std::string> categories;
    std::vector<i64> markers;  ///< frame, cor, tipo
    TextData text{};
    bool hasText = false;
    u64 compId = 0;
    // --- Estado da interface ------------------------------------------------
    Tool tool = Tool::Select;
    Workspace workspace = Workspace::Editing;
    Workspace pendingWorkspace = Workspace::Count;
    bool resetLayout = false;
    std::map<int, std::string> layouts;
    bool snap = true, linkedSelection = true, autoKey = false, uniformScale = true;
    bool showThumbs = true, showWaveforms = true, showKeyframes = true, showPerf = true;
    double pxPerFrame = 6.0, scrollFrame = 0.0;
    float trackScroll = 0.f;
    float timelineWidth = 1000.f;  ///< largura das faixas no último quadro (zoom pelo teclado)
    bool loop = false;
    bool projectIcons = false;
    bool followPlayhead = true;
    std::vector<TrackKey> graphTracks;        ///< curvas visíveis no editor de gráfico
    std::vector<std::pair<TrackKey, i32>> selectedKeys;  ///< trilha + tempo local
    bool speedGraph = false;
    int previewScale = 0;  ///< 0 auto, 1 completa, 2 1/2, 3 1/4, 4 1/8
    std::string notice = "Pronto. Importe mídia (Ctrl+I) ou arraste arquivos para a janela.";
    double noticeTime = 0;
    std::string search;
    int groupDepth = 0;
    ExportDialog exporter;
    SequenceDialog sequence;
    bool showShortcuts = false, showAbout = false;
    // Prévia: a janela filha do Vulkan segue o retângulo do painel Programa.
    RECT previewRect{};
    bool previewShown = false, previewWanted = false;
    std::vector<RECT> previewHoles;
    // Ponto de efeito escolhido clicando na prévia (mira do parâmetro).
    struct PointPick { u32 effect = kInvalidIndex, param = 0; } pick;
    // Arrasto na prévia (thread de UI, mensagens da janela filha).
    bool previewDrag = false;
    POINT previewDown{};
    float dragStartPos[2]{};
    // --- Tarefas longas -----------------------------------------------------
    std::future<void> task;
    std::atomic<bool> busy{false};
    std::mutex taskMutex;
    std::vector<std::string> taskNotices;
    std::vector<u64> taskSelect;
    // --- Miniaturas ---------------------------------------------------------
    std::unordered_map<u64, Thumb> thumbs;
    u32 frameCounter = 0;
    double lastAutosave = 0;
    // --- Medidores de áudio --------------------------------------------------
    float meterLevel[2]{}, meterPeak[2]{};
    double meterPeakTime[2]{};

    // ---------------------------------------------------------------------
    void send(Command c, const std::string& text = {});
    void command(CommandType type) { Command c; c.type = type; send(c); }
    void begin_group() { if (groupDepth++ == 0) command(CommandType::UndoBeginGroup); }
    void end_group() { if (groupDepth > 0 && --groupDepth == 0) command(CommandType::UndoEndGroup); }
    void message(std::string text);
    void error(const Status& s, const char* what = nullptr);
    void refresh();
    void seek(i64 frame);
    void select(u64 id, bool add = false);
    void set_selection(const std::vector<u64>& ids);
    [[nodiscard]] bool is_selected(u64 id) const;
    [[nodiscard]] LayerView* layer(u64 id);
    [[nodiscard]] const std::vector<bridge::KeyframeRow>& layer_keys(u64 id) const;
    [[nodiscard]] double fps() const { return status.compFps > 0 ? status.compFps : 30.0; }
    [[nodiscard]] i64 duration() const { return std::max<i64>(1, status.duration.value); }
    [[nodiscard]] i64 playhead() const { return status.playhead.value; }
    [[nodiscard]] std::string timecode(i64 frame) const;
    [[nodiscard]] float transform_value(u32 property) const;
    void set_transform(u32 property, float value);
    void toggle_track_animation(u64 layer, const std::vector<TrackKey>& tracks, const std::vector<float>& values);
    void toggle_key_here(u64 layer, const std::vector<TrackKey>& tracks, const std::vector<float>& values);
    void set_effect_value(const EffectView& fx, const bridge::EffectParamRow& p, u32 component, float value);
    void set_effect_vector(const EffectView& fx, const bridge::EffectParamRow& p, const float* v);
    void delete_keys(u64 layer, const std::vector<std::pair<TrackKey, i32>>& which);
    void move_key(u64 layer, const TrackKey& t, i32 from, i32 to);
    void set_key_value(u64 layer, const TrackKey& t, i32 time, float value);
    void set_bezier(u64 layer, const TrackKey& t, i32 time, Interpolation in, float bx1, float by1, float bx2, float by2);
    bool key_easing(u64 layer, const TrackKey& t, i32 time, float out[4]);
    void apply_ease(int kind);  ///< 0 suavização fácil, 1 entrada, 2 saída, 3 linear, 4 manter, 5+ família do motor
    void apply_effect(u32 type, u64 layer = 0);
    void add_marker();
    void navigate(int direction);
    void split_at_playhead();
    void delete_selection();
    // Arquivo
    bool save(bool as = false);
    bool may_discard();
    void new_project();
    void open_project();
    void import_dialog();
    void import_files(std::vector<std::filesystem::path> files);
    void export_frame();
    void start_export();
    void run(std::function<void()> work);
    void finish_tasks();
    void autosave(double now);
    // Miniaturas e áudio
    ID3D11ShaderResourceView* thumbnail(u64 layer, i32 frame, u32 height, u32* w, u32* h);
    void trim_thumbnails();
    void update_meters(double now, float dt);
};

extern Studio* gStudio;

// Tema e widgets (StudioUi.cpp)
void apply_theme(float dpi);
void load_fonts(float dpi);
extern ImFont* gFontUi;
extern ImFont* gFontBold;
extern ImFont* gFontTimecode;
extern ImFont* gFontSmall;
float S(float v);  ///< pixels lógicos → pixels da tela
bool scrub_float(const char* id, float* value, float speed, float minimum, float maximum, const char* format, ImU32 color = col::Accent, float scale = 1.f);
bool stopwatch(const char* id, bool animated);
bool key_navigator(const char* id, bool keyHere, bool* previous, bool* next);
bool twirl(const char* id, bool* open);
bool small_toggle(const char* id, const char* label, bool on, const char* tooltip = nullptr);
void draw_diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 border, bool filled = true);
void section_band(const char* label);
void tooltip(const char* text);
std::string format_value(float v, int decimals);

// Painéis
void draw_header(Studio&);
void draw_menu(Studio&);
void draw_project(Studio&);
void draw_effects_browser(Studio&);
void draw_markers(Studio&);
void draw_history(Studio&);
void draw_info(Studio&);
void draw_effect_controls(Studio&);
void draw_audio_mixer(Studio&);
void draw_program(Studio&);
void draw_timeline(Studio&);
void draw_graph_editor(Studio&);
void draw_tools(Studio&);
void draw_meters(Studio&);
void draw_dialogs(Studio&);
void draw_status_bar(Studio&);
void build_layout(Workspace ws, ImGuiID dockspace);
const char* workspace_name(Workspace ws);
void handle_shortcuts(Studio&);

// Utilidades
std::string utf8(const std::wstring& s);
std::wstring wide(const std::string& s);
const char* track_property_name(u32 property);
ImU32 clip_color(const LayerView& l);
Icon layer_icon(const LayerView& l);
float srgb_to_linear(float c);
float linear_to_srgb(float c);

} // namespace aurea::studio
