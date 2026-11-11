// =============================================================================
//  Aurea / effects / Headbop.hpp
//
//  Efeito "Headbop" (`aurea.distort.headbop`, 0.5.2): a pessoa da foto ou do
//  vídeo balança a cabeça e o corpo na batida. Procedural (sem IA generativa):
//  uma grade deformada no vértice pela pose do motor de movimento
//  (HeadbopMotion.hpp) e desenhada sobre o fundo preenchido.
//
//  Este cabeçalho é o RIG e o RENDER:
//
//   - índices dos parâmetros (contrato do projeto salvo: só acrescentar);
//   - `RigFace`/`RigFrame`: cabeça (elipse, giro), pivô do pescoço e linha dos
//     ombros por pessoa, em FRAÇÃO da camada; vêm dos pontos de corpo que as
//     plataformas detectam (ML Kit Pose / Vision, o contrato de 33 pontos de
//     rig/Mocap.hpp) — nenhum modelo novo;
//   - `Uniforms`: o bloco std140 do passe (o vértice calcula os pesos de
//     cabeça/tronco/fundo pela geometria do rig, sem textura por quadro);
//   - `deform_point`/`weights`: o MESMO cálculo do vértice na CPU (testes de
//     continuidade e a alça do palco);
//   - `RigStore`: a trilha do rig por quadro-fonte (vídeo) e a grade de
//     batidas, em memória e no cache do motor (`<cache>/headbop/<ficha>.hbr`,
//     regenerável: a análise refaz).
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/effects/HeadbopMotion.hpp"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace aurea::headbop {

inline constexpr const char* kHeadbopKey = "aurea.distort.headbop";

// ---- Parâmetros (ordem = contrato; novos só no fim) ----------------------------

enum Param : u32 {
    kPreset = 0,       ///< enum Bop/Nod/Sway/Bounce/Groove
    kIntensity,        ///< %
    kHead,             ///< %
    kBody,             ///< %
    kJolt,             ///< % "Tranco"
    kSway,             ///< % "Balanço lateral"
    kTurn3d,           ///< % "Giro 3D"
    kDelay,            ///< % "Atraso"
    kSmoothness,       ///< % "Suavidade"
    kVariation,        ///< % "Variação"
    kPhase,            ///< batidas
    kSubdivision,      ///< enum 1/2, 1, 2 (batidas por aceno)
    kBeatZoom,         ///< %
    kBeatSource,       ///< enum Automática / BPM manual / Tap
    kBpm,              ///< BPM manual
    kAudioLayer,       ///< referência de camada (0 = a primeira com som)
    kPoseSource,       ///< enum Batida / Keyframes
    kOutput,           ///< enum Normal / Só a pessoa / Só a pessoa em cor sólida
    kSolidColor,       ///< cor
    kMotionBlur,       ///< bool
    kPivotManual,      ///< bool: o pivô do pescoço vem do ponto abaixo
    kPivot,            ///< ponto (fração da camada)
    kCutout,           ///< bool "Recortar pessoa" (máscara + fundo preenchido)
    kSeed,             ///< oculto: semente
    kBakeFirst,        ///< oculto, animável: os 11 primeiros canais da pose ("Converter em keyframes")
    kAutoBpm = kBakeFirst + 11,  ///< oculto: BPM da análise do áudio
    kAutoPhase,        ///< oculto: fase (s da composição) da análise
    kTapBpm,           ///< oculto
    kTapPhase,         ///< oculto
    kRigToken,         ///< oculto: ficha da trilha do rig no RigStore (0 = sem)
    kRigState,         ///< oculto: 0 = sem análise, 1 = analisado (sem pessoa), 2 = com pessoa
    kRigFirst,         ///< oculto: rig parado (foto, ou a mediana do vídeo), kFaceParams por pessoa
};

inline constexpr u32 kMaxFaces = 4;
/// Pontos (Point2D) por pessoa no rig parado: centro, eixos, (roll, yaw),
/// (pitch, profundidade), pescoço, ombro esquerdo (da imagem), ombro direito.
inline constexpr u32 kFaceParams = 7;
/// Oculto, animável: o 12º canal da pose (`frameDx`, solavanco diagonal) — no fim.
inline constexpr u32 kBakeFrameDx = kRigFirst + kMaxFaces * kFaceParams;
/// 0.5.2 movimento secundário (no fim): Cabelo, Roupa, Física (%), e os 11 canais
/// secundários da pose ("Converter em keyframes"), ocultos e animáveis.
inline constexpr u32 kHair = kBakeFrameDx + 1;
inline constexpr u32 kCloth = kHair + 1;
inline constexpr u32 kPhysics = kCloth + 1;
inline constexpr u32 kBakeSecondFirst = kPhysics + 1;
inline constexpr u32 kSecondaryChannels = 11;
inline constexpr u32 kParamCount = kBakeSecondFirst + kSecondaryChannels;

enum class BeatSource : u32 { Automatic = 0, Manual = 1, Tap = 2 };
enum class PoseSource : u32 { Beat = 0, Keyframes = 1 };
enum class Output : u32 { Normal = 0, PersonOnly = 1, PersonSolid = 2 };

/// Canais da pose: os 11 de `kBakeFirst` e `frameDx` (o 12º, em `kBakeFrameDx`).
inline constexpr u32 kPoseChannels = 12 + 11;
/// Parâmetro oculto do canal `c` da pose (0..10 em kBakeFirst, 11 = frameDx, 12.. secundários no fim).
[[nodiscard]] constexpr u32 bake_param(u32 c) noexcept {
    return c < 11 ? kBakeFirst + c : c == 11 ? kBakeFrameDx : kBakeSecondFirst + (c - 12u);
}
[[nodiscard]] std::array<f32, kPoseChannels> pose_channels(const Pose& p) noexcept;
[[nodiscard]] Pose pose_from_channels(const f32* c) noexcept;

// ---- Rig ---------------------------------------------------------------------

/// Uma pessoa, em FRAÇÃO da camada (x / largura, y / altura); ângulos em graus.
struct RigFace {
    Vec2 center{0.5f, 0.35f};   ///< centro da elipse da cabeça
    Vec2 axes{0.12f, 0.1f};     ///< semi-eixos (rx em fração da LARGURA, ry em fração da ALTURA)
    f32 roll = 0.0f;            ///< giro da cabeça em repouso (graus, horário na tela)
    f32 yaw = 0.0f;             ///< + = olha para a direita da imagem
    f32 pitch = 0.0f;           ///< + = queixo para baixo
    f32 depth = 0.85f;          ///< profundidade do elipsoide / rx
    Vec2 neck{0.5f, 0.5f};      ///< pivô do pescoço
    Vec2 shoulderA{0.3f, 0.62f}; ///< ombro à esquerda da imagem
    Vec2 shoulderB{0.7f, 0.62f}; ///< ombro à direita da imagem
    f32 confidence = 0.0f;      ///< 0..1
};

struct RigFrame {
    f64 time = 0.0;             ///< segundos da FONTE
    u32 count = 0;              ///< pessoas (0 = quadro inteiro)
    std::array<RigFace, kMaxFaces> faces{};
};

/// Quadro empacotado das pontes: [tempo s, pessoa (0..3), 33 × (x, y, z, confiança)]
/// — o MESMO formato do Motion Capture (rig/Mocap.hpp), com o 2º número sendo a
/// pessoa (o detector de pose dá a 0; o de rostos, as outras).
inline constexpr u32 kFrameFloats = 2 + 33 * 4;

/// Rig de uma pessoa a partir dos 33 pontos (x, y normalizados na imagem, y
/// para baixo). `aspect` = largura/altura da imagem. Falso = sem olhos/nariz
/// confiáveis (a pessoa não entra).
[[nodiscard]] bool face_from_landmarks(const f32* pts33x4, f32 aspect, RigFace& out, f32 minConfidence = 0.35f) noexcept;

/// Rig de um quadro com várias pessoas (pessoa duplicada — o mesmo rosto pelos
/// dois detectores — vira uma só). Ordena da esquerda para a direita.
void frame_from_people(const std::vector<RigFace>& people, RigFrame& out) noexcept;

/// Trilha por quadro-fonte (vídeo) e a interpolação dela.
struct RigTrack {
    std::vector<RigFrame> frames;   ///< ordenados por tempo
};
[[nodiscard]] RigFrame sample_track(const RigTrack& track, f64 seconds) noexcept;
/// Mediana por pessoa (o rig parado do vídeo).
[[nodiscard]] RigFrame median_frame(const RigTrack& track) noexcept;
/// Suaviza no tempo (média móvel de 5 quadros, pesos 1-2-3-2-1) as pessoas
/// presentes nos vizinhos — o tremor do detector não vira dança.
void smooth_track(RigTrack& track) noexcept;

// ---- Pesos e deformação (o MESMO do vértice) ------------------------------------

/// Bloco de uma pessoa no passe (px da camada).
struct GpuFace {
    Vec4 head;       ///< cx, cy, rx, ry
    Vec4 axis;       ///< cos(roll), sin(roll), rz (profundidade px), queda (fração da elipse)
    Vec4 neck;       ///< pivô x, y; meio dos ombros x, y
    Vec4 body;       ///< meia largura dos ombros, comprimento do tronco, 0, 0
    Vec4 torso;      ///< dx, dy (px), roll (rad), 0
    Vec4 headT;      ///< dx, dy (px), roll (rad), pitch (rad)
    Vec4 headR;      ///< yaw (rad), 0, 0, 0
    Vec4 prevTorso;  ///< o mesmo meio obturador antes (desfoque de movimento)
    Vec4 prevHeadT;
    Vec4 prevHeadR;
    // 0.5.2: movimento secundário (diferenças em relação à cabeça/tronco, px e rad).
    Vec4 hairT;      ///< dx, dy, roll, pitch do cabelo − cabeça
    Vec4 clothT;     ///< dx, dy, roll da roupa − tronco; w = amplitude da onda (px)
    Vec4 flutter;    ///< tremulação x, y (px), fase da onda (rad), 0
    Vec4 prevHairT;
    Vec4 prevClothT;
    Vec4 prevFlutter;
};
static_assert(sizeof(GpuFace) == 256, "std140");

inline constexpr u32 kGridX = 32, kGridY = 56;   ///< retrato; paisagem troca
/// Com cabelo/roupa (0.5.2) a grade fica mais densa (as pontas do cabelo dobram).
inline constexpr u32 kGridXLoose = 40, kGridYLoose = 72;
inline constexpr u32 kGridMax = 72;

struct Uniforms {
    Vec4 region;       ///< saída (px da camada)
    Vec4 inputRegion;  ///< entrada
    Vec4 layer;        ///< largura, altura, pontos da grade em x, em y
    Vec4 frame;        ///< zoom global, deslocamento y global (px), zoom e y meio obturador antes
    Vec4 mode;         ///< pessoas, quadro inteiro (1), saída, bits (1 máscara, 2 fundo preenchido)
    Vec4 color;        ///< cor sólida (linear, pré-multiplicada)
    Vec4 blur;         ///< amostras do desfoque (0 = desligado), 0, 0, 0
    Vec4 window;       ///< largura da borda presa (px), cabelo ligado (1/0), roupa ligada (1/0), 0
    Vec4 frameShift;   ///< deslocamento x global (px), o mesmo meio obturador antes, 0, 0
    Vec4 gridX[kGridMax / 4];   ///< coordenadas x das linhas da grade (px), 4 por vec4
    Vec4 gridY[kGridMax / 4];
    GpuFace face[kMaxFaces];
};
static_assert(sizeof(Uniforms) % 16 == 0 && sizeof(Uniforms) <= 4096, "teto de uniforms do passe");

/// Pesos (cabeça, tronco) da pessoa `f` no ponto de repouso `p` (px).
struct Weights { f32 head = 0.0f, torso = 0.0f; };
[[nodiscard]] Weights weights(const Uniforms& u, u32 f, Vec2 p) noexcept;
/// Posição deformada (antes do zoom/solavanco globais) do ponto de repouso `p`.
/// `previous` = a pose meio obturador antes.
[[nodiscard]] Vec2 deform_point(const Uniforms& u, Vec2 p, bool previous = false, f32 mask = 1.0f) noexcept;
/// Com o zoom/solavanco globais.
[[nodiscard]] Vec2 final_point(const Uniforms& u, Vec2 p, bool previous = false, f32 mask = 1.0f) noexcept;

/// Entradas do preenchimento dos uniforms (o que o efeito resolve no planejamento).
struct BuildInput {
    f32 width = 1, height = 1;   ///< tamanho natural da camada
    RigFrame rig;                ///< count 0 = quadro inteiro
    std::array<Pose, kMaxFaces> pose{};       ///< uma pose por pessoa (ou [0] no quadro inteiro)
    std::array<Pose, kMaxFaces> prevPose{};
    bool pivotManual = false;
    Vec2 pivot{0.5f, 0.5f};      ///< fração da camada
    Output output = Output::Normal;
    Vec4 solid{1, 1, 1, 1};
    bool mask = false, plate = false;
    u32 blurSamples = 0;
    bool hair = false, cloth = false;   ///< 0.5.2: movimento secundário ligado (só com a máscara)
};
/// Soltura (0..1) de um ponto de repouso: cabelo preso à cabeça (`hairRegion`,
/// rígido até o couro), cabelo com as pontas (`hair`) e roupa (`cloth`).
/// `mask` = 1 com o recorte da pessoa (sem ele nada solta: o fundo balançaria junto).
struct Looseness { f32 hairRegion = 0, hair = 0, cloth = 0; };
[[nodiscard]] Looseness looseness(const Uniforms& u, u32 f, Vec2 p, f32 mask) noexcept;
void fill_uniforms(const BuildInput& in, Uniforms& out) noexcept;

/// Vértices do passe (6 por célula da grade).
[[nodiscard]] constexpr u32 vertex_count(u32 gx, u32 gy) noexcept { return gx > 1 && gy > 1 ? (gx - 1) * (gy - 1) * 6 : 0; }

/// Fase/intensidade por pessoa (semeadas): `phase` em batidas, `gain` multiplica.
void person_offset(u32 seed, u32 person, f64& phase, f32& gain) noexcept;

/// Parâmetros do motor de movimento a partir dos valores do efeito.
[[nodiscard]] Params motion_params(const f32* values, u32 count) noexcept;
/// Cabelo/Roupa/Física (%) → Params.hair/cloth/physics (0.5.2).
void apply_secondary(Params& p, f32 hairPct, f32 clothPct, f32 physicsPct) noexcept;

// ---- Armazenamento (trilha do rig + grade de batidas) ---------------------------

struct StoreEntry {
    RigTrack track;
    BeatGrid grid;   ///< segundos da COMPOSIÇÃO (vazia = usar BPM/fase ocultos)
};

/// A grade que vale para a fonte da batida (`BeatSource`): a da análise
/// (`entry`), BPM/fase ocultos, os toques; sem nada, 120 BPM desde o zero.
[[nodiscard]] BeatGrid beat_grid(u32 source, f32 autoBpm, f32 autoPhase, f32 tapBpm, f32 tapPhase,
                                 const StoreEntry* entry);

class RigStore {
public:
    static RigStore& shared();
    /// Pasta do cache (o Engine passa `<cacheDirectory>/headbop`). Vazia = só memória.
    void set_directory(std::string dir);
    [[nodiscard]] std::string directory() const;
    /// Grava (memória + disco). `token` 1..2^24-1.
    bool put(u32 token, std::shared_ptr<const StoreEntry> entry);
    /// Memória, ou lê do disco na primeira vez. Nula = não existe.
    [[nodiscard]] std::shared_ptr<const StoreEntry> get(u32 token);
    /// Ficha nova que ainda não existe na memória nem no disco.
    [[nodiscard]] u32 new_token();
    void clear_memory();

private:
    mutable std::mutex mutex_;
    std::string dir_;
    std::unordered_map<u32, std::shared_ptr<const StoreEntry>> map_;
    std::unordered_map<u32, bool> missing_;
};

[[nodiscard]] std::string serialize_entry(const StoreEntry& e);
[[nodiscard]] bool deserialize_entry(const std::string& blob, StoreEntry& out);

/// Aparelho da classe mais fraca: o desfoque de movimento fica desligado
/// (preview e export do MESMO aparelho continuam iguais).
void set_low_end_device(bool low) noexcept;
[[nodiscard]] bool low_end_device() noexcept;

/// Estado da análise (a UI pergunta): progresso 0..1 e o resultado.
struct AnalysisStatus {
    std::atomic<i32> state{0};      ///< 0 parado, 1 rodando, 2 pronto, 3 falhou, 4 cancelado
    std::atomic<f32> progress{0.0f};
    std::atomic<i32> people{0};
    std::atomic<f32> bpm{0.0f};
    std::atomic<i32> frames{0};
    std::atomic<bool> cancel{false};
};

} // namespace aurea::headbop
