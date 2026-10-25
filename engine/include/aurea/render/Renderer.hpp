// =============================================================================
//  Aurea / render / Renderer.hpp
//
//  Compositor + renderer: do modelo (composição num instante) aos pixels.
//
//  DUAS FASES, com o lock do modelo no meio:
//
//   prepare()   SOB o lock. Lê a composição NA ORDEM DO CORE (a ordem da
//               timeline É a ordem de composição — não existe outra lista),
//               avalia transform e parâmetros no instante, pega o frame de
//               vídeo de cada layer no cache da fonte, planeja os efeitos.
//               Tudo o que o frame precisa sai daqui COPIADO num snapshot.
//
//   render()    SEM o lock. Monta o FrameGraph a partir do snapshot, executa na
//               GPU e apresenta. A UI pode editar o projeto enquanto a GPU
//               trabalha, sem corrida.
//
//  O MESMO render serve ao preview (saída no swapchain, resolução adaptativa) e
//  ao export/testes (saída numa textura, resolução cheia). Não existe um
//  segundo renderer para o export.
// =============================================================================
#pragma once

#include "aurea/render/ShutterPlan.hpp"
#include "aurea/ai/DepthMapService.hpp"
namespace aurea::ai { class RotoService; struct RotoStroke; }
#include "aurea/effects/EffectGraph.hpp"
#include "aurea/effects/CubeLut.hpp"
#include "aurea/media/MediaManager.hpp"
#include "aurea/memory/Arena.hpp"
#include "aurea/render/FrameGraph.hpp"
#include "aurea/render/HeavyQuality.hpp"
#include "aurea/render/PreviewCachePolicy.hpp"
#include "aurea/render/ParticleExtras.hpp"
#include "aurea/render/ParticleScene.hpp"
#include "aurea/render/RenderScheduler.hpp"
#include "aurea/render/ShaderLibrary.hpp"
#include "aurea/scene3d/SceneRenderer.hpp"
#include "aurea/scene3d/Animation.hpp"
#include "aurea/timeline/Composition.hpp"

#include <atomic>
#include <array>
#include <limits>
#include <memory>
#include <map>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace aurea {

class Project;
namespace audio { class AudioBlockCache; class MixState; }

/// Pixels de uma imagem importada (RGBA8, sRGB, alfa reto).
struct ImagePixels {
    u32 width = 0;
    u32 height = 0;
    std::vector<u8> rgba;
    /// Saída do `imageLoader`: a orientação EXIF do arquivo (1..8; 1 = normal).
    /// Os pixels vêm CRUS; quem gira é o motor (image::reopen_pixels), e só
    /// nos assets importados depois da correção (Asset::profile.profile ==
    /// image::kDisplayOriented). Projetos antigos reabrem com os pixels crus de
    /// sempre: a geometria gravada é essa.
    u8 orientation = 1;
};

// -----------------------------------------------------------------------------
// PRÉVIA ANIMADA DE EFEITO (docs/diagnostico/efeitos-imagem-feed.md §3.2)
//
// N quadros (12..16) do efeito real sobre uma CENA-FONTE nossa, variando o
// tempo local, o progresso do clipe e o parâmetro principal. Cenas: a foto de
// amostra do app, a cartela procedural (shader) e duas cenas geradas por código
// na CPU (formas coloridas em movimento; ambiente escuro de baixa luz) — 0 byte
// de asset. Para efeito de tempo a fonte ANDA entre os quadros.
// -----------------------------------------------------------------------------
enum class EffectPreviewScene : u32 {
    Photo = 0,     ///< a foto de amostra (sem foto: a cartela)
    Plate = 1,     ///< a cartela procedural (effects/preview_plate.frag)
    Shapes = 2,    ///< formas coloridas em movimento (CPU)
    LowLight = 3,  ///< cena escura, de baixa luz, com uma luz que passeia (CPU)
};
inline constexpr u32 kEffectPreviewSceneCount = 4;
inline constexpr u32 kEffectPreviewSequenceMinFrames = 12;
inline constexpr u32 kEffectPreviewSequenceMaxFrames = 16;
/// Quadros da linha do tempo (a 30 qps) entre dois quadros da prévia: 16
/// quadros cobrem ~1 s de efeito, e a UI toca a ~15 qps.
inline constexpr i64 kEffectPreviewSequenceStep = 2;

struct EffectPreviewSequenceInfo {
    u32 frames = 0;
    bool moving = false;    ///< a fonte anda entre os quadros
    bool temporal = false;  ///< efeito de tempo (resultado montado sobre a fonte que anda)
    i32 sweepParam = -1;    ///< parâmetro varrido (-1 = nenhum)
};

/// O que foi preparado para uma sequência (as fontes da CPU já capturadas).
struct EffectPreviewSequence {
    EffectTypeId type = 0;
    u32 width = 0, height = 0, frames = 0;
    EffectPreviewScene scene = EffectPreviewScene::Photo;
    EffectPreviewSequenceInfo info;
    /// Foto/cartela capturada com folga (RGBA8 sRGB) para a fonte andar por
    /// recorte; vazia = cena procedural ou fonte parada (caminho da foto).
    std::vector<u8> base;
    u32 baseW = 0, baseH = 0;
    std::vector<ParamValue> values;   ///< valores de demonstração (efeitos de tempo)
};

class ParameterRegistry;
struct EffectInstance;
/// A VARREDURA da sequência: todo parâmetro numérico, animável e visível cujo
/// valor de demonstração difere do padrão anda junto, do padrão (s = 0) até o
/// de demonstração (s = 1) — o efeito "entrando". Semente, liga/desliga e
/// listas ficam no valor de demonstração. `sweep_param` = o primeiro varrido
/// (-1 = nenhum: o efeito já aparece com os padrões).
[[nodiscard]] i32 effect_preview_sweep_param(const ParameterRegistry& params, const EffectInstance& instance,
                                             const std::vector<ParamValue>& demo) noexcept;
void effect_preview_apply_sweep(const ParameterRegistry& params, const EffectInstance& instance,
                                std::vector<ParamValue>& values, f32 s) noexcept;
/// Cena procedural da CPU (Shapes, LowLight) no instante `t` (quadros a 30 qps),
/// periódica em `period` quadros. RGBA8 sRGB opaco, `w`×`h`.
void effect_preview_procedural_scene(EffectPreviewScene scene, u32 w, u32 h, f64 t, f64 period,
                                     std::vector<u8>& out) noexcept;

/// Origem da imagem de uma layer neste frame.
class Composition;
struct Layer;
/// Matriz composição ← camada no instante, com a cadeia de pais (3D quando a
/// camada ou um pai vive no espaço 3D). O MESMO cálculo do renderer.
[[nodiscard]] Mat4 layer_world_matrix(const Composition& comp, const Layer& l, FrameIndex time) noexcept;
/// Composição ← camada como o renderer desenha: igual a `layer_world_matrix`
/// no 2D; no espaço 3D, já com a câmera e a perspectiva (dividir por w).
[[nodiscard]] Mat4 layer_comp_matrix(const Composition& comp, const Layer& l, FrameIndex time, bool* perspective = nullptr) noexcept;
/// Composição (px) ← mundo 3D: a câmera ativa da composição no instante.
[[nodiscard]] Mat4 comp_view_projection(const Composition& comp, FrameIndex time) noexcept;
/// A camada (ou um pai) vive no espaço 3D no instante? (a mesma regra do render)
[[nodiscard]] bool wants_layer_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept;
/// Mundo 3D da camada com a cadeia de pais (o mesmo dos modelos/luzes).
[[nodiscard]] Mat4 layer_world_3d(const Composition& comp, const Layer& l, FrameIndex time) noexcept;
/// FOV vertical (graus) de uma camada de câmera no instante LOCAL: a trilha de
/// distância focal (mm) manda quando tem keyframe; senão a de FOV; senão o
/// valor parado. É a MESMA conta da projeção do render (preview e export).
[[nodiscard]] f32 camera_fov_deg_at(const Layer& camera, f64 localFrame) noexcept;
/// Foco por toque (Pick Focus): o ponto do mundo da superfície 3D mais perto
/// sob o ponto (px da composição), visto pela câmera ativa. Raio na CPU contra
/// os triângulos dos modelos na pose do quadro (skin incluída). Falso = nada.
[[nodiscard]] bool pick_scene_point(const Composition& comp, FrameIndex time, f32 compX, f32 compY,
                                    std::shared_ptr<const scene3d::SceneAsset> (*lookup)(void* ctx, AssetId id),
                                    void* ctx, Vec3& outWorld) noexcept;

/// A câmera ativa da composição no instante (a mesma do render/export).
[[nodiscard]] scene3d::SceneCamera comp_camera(const Composition& comp, FrameIndex time, u32 w, u32 h) noexcept;
/// Raio do mundo sob o ponto (px da composição) visto por `cam` — a MESMA
/// conta da projeção do render (enquadramento da fonte rastreada incluído).
[[nodiscard]] bool scene_ray(const scene3d::SceneCamera& cam, u32 width, u32 height, f32 compX, f32 compY,
                             Vec3& origin, Vec3& dir) noexcept;
/// Raio × triângulos do modelo da camada na pose do quadro. `best` (distância
/// ao longo do raio) só diminui; verdadeiro = acertou antes de `best`.
[[nodiscard]] bool ray_hits_model_layer(const Composition& comp, const Layer& l, FrameIndex time,
                                        const scene3d::SceneAsset& asset, Vec3 origin, Vec3 dir, f32& best) noexcept;

/// Um glifo na GPU (std430, espelho de shaders/text/glyph.vert).
struct GlyphInstance {
    Vec4 rect;     ///< x0 y0 x1 y1, px da layer
    Vec4 uv;
    Vec4 fill;     ///< linear, alfa
    Vec4 stroke;   ///< linear, alfa
    Mat4 xform = Mat4::identity();   ///< px da layer (Z = profundidade por caractere)
    Vec4 misc;     ///< _, _, k, largura do contorno (px da layer)
    Vec4 extra;    ///< desfoque (px), _, _, _
};
static_assert(sizeof(GlyphInstance) == 160, "layout std430 do glifo");

struct LayerSource {
    /// Adjustment: camada de ajuste — sem fonte; os efeitos dela (plano no
    /// mesmo índice) leem a composição acumulada abaixo dela.
    enum class Kind : u8 { None = 0, Video, Image, Solid, Scene3D, Shape, Nested, Particles, Text, Adjustment, Vector };
    Kind kind = Kind::None;

    /// RGB NO TEMPO (Fase 7.3 §25): a MESMA camada, em até três instantes, um
    /// por canal de cor. Vazio (`channelCount == 0`) = o caminho normal, uma
    /// fonte só.
    ///
    /// Mora aqui, e não no acúmulo de amostras do desfoque de movimento,
    /// porque só o `prepare` fala com o decoder — e é o decoder que decide se
    /// o quadro daquele instante existe. O desfoque varia a MATRIZ, o RGB no
    /// tempo varia a FONTE; são coisas diferentes.
    struct ChannelFrame {
        FrameRef frame;
        Vec3     mask{0, 0, 0};
    };
    static constexpr u32 kMaxChannelFrames = 3;
    ChannelFrame channel[kMaxChannelFrames];
    u32 channelCount = 0;
    u32  width = 0;          ///< tamanho natural da layer (px)
    u32  height = 0;

    // Vídeo
    FrameRef frame;
    bool     frameExact = false;
    /// Mistura de quadros: o quadro seguinte da fonte e o peso dele (0..1).
    /// Detectar movimento: a MESMA fonte no instante t − atraso (quadros da
    /// timeline). Vazio = sem o efeito, ou a camada não tem passado.
    FrameRef historyFrame;
    FrameRef frameB;
    f32      blendT = 0.0f;
    u8       blendMode = 0;   ///< 1 mistura, 2 optical flow
    /// Desfoque vetorial: fração do quadro que o obturador cobre (0 = não).
    f32      vectorBlur = 0.0f;
    /// Lado maior da base do optical flow (px; 0 = o padrão). O Flow Retime e
    /// o Flow Motion Blur pedem mais (ou menos) detalhe nos vetores.
    f32      flowBase = 0.0f;

    // Imagem
    AssetId  image{};
    const ImagePixels* pixels = nullptr;   ///< só usado no prepare

    // Cor sólida (shape retangular), linear
    Vec4     solid{};

    // Grupo 3D: índice em FrameSnapshot::scenes
    u32      sceneGroup = 0;

    // Pré-composição: índice em FrameSnapshot::nested
    u32      nestedIndex = 0;
    /// Pré-composição com efeito de TEMPO (RGB no tempo, Detectar movimento):
    /// a filha renderizada em OUTROS instantes, índices em `FrameSnapshot::nested`
    /// (~0u = ausente). Sem elas, todo "passado" era o quadro de agora — o
    /// detector dava preto e o RGB no tempo sumia (beta 2140).
    u32      nestedChannel[3] = {~0u, ~0u, ~0u};
    u32      nestedHistory = ~0u;

    // Texto (GPU): glifos em FrameSnapshot::glyphs. Com desfoque de movimento
    // por letra, `glyphSets` conjuntos seguidos (um por instante do obturador).
    u32      glyphFirst = 0;
    u32      glyphCount = 0;
    u32      glyphSets = 1;
    Vec4     textPersp{0, 0, 0, 0};   ///< cx, cy da layer, distância focal (px) para o 3D por caractere

    // Partículas (Aurea Particular): o bloco de parâmetros do shader, nº de
    // instâncias e blend. O bloco cresceu de 7 para 20 vec4 quando o sistema
    // deixou de ser três presets fixos e virou sistema parametrizável —
    // emissor, física, rastro, aux e colisão precisam caber.
    static constexpr u32 kParticleBlocks = 20;
    Vec4     particleBlock[kParticleBlocks]{};
    u32      particleSlots = 0;      ///< primárias (o aux multiplica por 1+n)
    bool     particleAdditive = true;
    /// Aurea Particular 8.2: cabeçalho do quadro + pontos/malha da fonte
    /// (render/ParticleExtras.hpp). Nulo = nada além do bloco (o shader não
    /// lê o storage buffer — o caminho de antes).
    std::shared_ptr<const particles::FrameData> particleExtras;

    // Forma vetorial (SDF): tipo, canto, pontas, raio interno, preenchida,
    // cores lineares pré-multiplicadas, largura do contorno (px).
    u32      shapeType = 0;
    Vec4     shapeParams{};      ///< canto, pontas, raio interno, preenchida
    Vec4     shapeFill{};
    Vec4     shapeStroke{};
    f32      shapeStrokeWidth = 0.0f;
    /// Parâmetros das formas paramétricas (shape::SdfParams: profundidade,
    /// ponta, espessura em yzw; abertura, ponta da seta, haste, amplitude; fases do blob).
    Vec4     shapeExtra{};
    Vec4     shapeMore{};
    Vec4     shapeBlob{};

    // Camada vetorial: triângulos em FrameSnapshot::vec (2 vec4 por vértice,
    // a partir de `vecFirst`) e as tintas logo depois (`paintFirst`, em vec4).
    u32      vecFirst = 0;
    u32      vecCount = 0;       ///< vértices
    u32      paintFirst = 0;

    // Rig 2D (imagem): triângulos deformados em FrameSnapshot::vec (1 vec4 por
    // vértice: posição na textura da camada, uv na imagem) a partir de
    // `rigFirst`. A textura da camada é a caixa da pose; a imagem tem o tamanho
    // original `rigImageW × rigImageH`.
    u32      rigFirst = 0;
    u32      rigCount = 0;       ///< vértices (3 por triângulo); 0 = sem rig
    u32      rigImageW = 0, rigImageH = 0;
};

struct RenderLayer {
    LayerId     id{};
    LayerSource source;
    Mat4        compFromLayer = Mat4::identity();
    f32         opacity = 1.0f;
    BlendMode   blend = BlendMode::Normal;
    f32         texelScale = 1.0f;
    /// Desfoque de movimento: composição ← camada em cada amostra do
    /// obturador. Vazio = sem desfoque (ou camada parada no intervalo).
    std::vector<Mat4> blurMatrices;
    Mat4 blurSourceTransform = Mat4::identity();
    bool blurIncludesFold = false;   ///< as amostras já trazem o Transform dobrado de cada instante
    /// Amostras temporais genéricas (eco, RGB no tempo): matriz, peso e
    /// máscara de canal (0 = todos). Com elas, o desfoque fica de fora.
    /// `time`: instante da composição da amostra; `nested`: a pré-composição
    /// renderizada nesse instante (~0u = a mesma fonte da camada). Eco numa
    /// pré-composição precisa do CONTEÚDO de antes, não só da posição de antes.
    /// TimeSlice: `slice` = (dx, dy, índice, número de faixas) — w = 0 fora do
    /// TimeSlice. Em vídeo, `sourceUs` é o instante da fonte da faixa e
    /// `frame` o quadro decodificado dele (vazio = já está no cache da GPU).
    /// `content` (Tempo amplo): como a faixa do TimeSlice, a amostra traz o
    /// conteúdo do SEU instante, mas cobre a camada inteira (sem recorte).
    struct TemporalSample {
        Mat4 m; f32 weight = 1.0f; Vec3 mask{0, 0, 0}; f64 time = 0.0; u32 nested = ~0u;
        Vec4 slice{0, 0, 0, 0}; i64 sourceUs = -1; FrameRef frame;
        bool content = false;
    };
    /// As amostras trazem o conteúdo de cada instante (TimeSlice, Tempo amplo).
    [[nodiscard]] bool per_sample_content() const noexcept {
        return !temporal.empty() && (temporal.front().slice.w > 0.0f || temporal.front().content);
    }
    std::vector<TemporalSample> temporal;
    /// APRIMORAR (temporal, 0.3.0): a FONTE da camada noutros quadros (±1..±3
    /// quadros da fonte), que a cadeia de efeitos recebe como vizinhos
    /// (`LayerImage::neighbors`). Vídeo: `sourceUs` e o quadro decodificado
    /// (vazio = já convertido no cache das faixas, ou ainda a caminho);
    /// pré-composição e camada de ajuste: a filha / o fundo re-renderizado
    /// naquele instante (`nested`). Vazio = sem o efeito ou fonte parada.
    struct NeighborSample {
        i32 offset = 0;          ///< quadros da fonte (o sinal diz o lado)
        f64 time = 0.0;          ///< instante da composição (pré-composição/ajuste)
        i64 sourceUs = -1;       ///< vídeo: o instante da fonte
        FrameRef frame;          ///< vídeo: o quadro a converter (exato ou aproximado)
        u32 nested = ~0u;        ///< índice em FrameSnapshot::nested
        bool approximate = false;///< prévia: o quadro exato ainda não chegou
    };
    std::vector<NeighborSample> neighbors;
    /// Camada 2D no espaço 3D que vive dentro de um grupo de cena (desenhada
    /// com profundidade pelo grupo, não na composição): índice do grupo, ou −1.
    i32 planeGroup = -1;
    /// Plano na cena: mundo (px) ← px da camada (a normal e a posição que as
    /// luzes da composição iluminam).
    Mat4 worldFromLayer = Mat4::identity();
    /// Layer::acceptsLights: só então o plano recebe as luzes da composição.
    bool acceptsLights = false;
    /// Máscaras (render/MaskRaster.hpp): bloco em FrameSnapshot::maskData a
    /// partir de `maskFirst`, `maskCount` ativas, cobertura inicial e a chave
    /// do bloco (cache da cobertura).
    u32 maskFirst = 0;
    u32 maskCount = 0;
    f32 maskStart = 0.0f;
    u64 maskKey = 0;
    /// Track matte: esta camada aparece através da matte `matteIndex` (índice
    /// neste snapshot; −1 = matte ausente no instante).
    MatteMode matteMode = MatteMode::None;
    u64  matteId = 0;          ///< id (com sal da pré-composição) da matte
    i32  matteIndex = -1;
    /// Usada como matte por outra camada: não desenha por conta própria.
    bool matteOnly = false;
    /// COPIAR FUNDO: a metade "janela" da camada (fonte, máscaras, track matte
    /// e os efeitos antes do Copiar fundo). Não desenha: vira o alfa/luma que
    /// recorta a metade "fundo" (`windowIndex` aponta para ela daqui).
    bool windowOnly = false;
    /// COPIAR FUNDO: na metade "fundo" (uma camada de ajuste de tudo abaixo com
    /// os efeitos depois do Copiar fundo), o índice da janela neste snapshot
    /// (−1 = camada de ajuste comum), o modo do mask/track_matte.frag (1 alfa,
    /// 2 alfa invertido, 3 luma, 4 luma invertida) e quanto da cor da própria
    /// camada volta por cima do resultado (0..1).
    i32  windowIndex = -1;
    u8   windowMode = 1;
    f32  windowLayerMix = 0.0f;
    /// MÁSCARA DE RECORTE neste instante: id (com sal) da base que recorta
    /// esta camada (0 = não recortada). A base é a camada de baixo na pilha
    /// das que existem no instante (ver `Layer::clipToBelow`).
    u64  clipBase = 0;
    /// Esta camada é a base de recortadas neste instante: ela e as de cima
    /// viram um grupo isolado, composto na cena com a mistura dela.
    bool clipParent = false;
    /// Camada de ajuste: 0 = vale para tudo abaixo, 1 = só para a camada logo
    /// abaixo (um grupo conta como uma camada), 2 = só as de `adjustTargets`.
    u8   adjustScope = 0;
    std::vector<u64> adjustTargets;   ///< escopo 2: ids (com sal) das camadas escolhidas
    /// Camada de ajuste com efeito de TEMPO (Eco, RGB no tempo, Posterizar,
    /// Low FPS, Remapear tempo, Flow Retime/Motion Blur, Detectar movimento,
    /// Datamosh...): a entrada dela é a composição abaixo, e o passado dessa
    /// composição só existe renderizando-a de novo NAQUELE instante. Índices
    /// em `FrameSnapshot::nested` (~0u = a composição de agora, a de sempre).
    /// Um por alvo no escopo 2 (mesma ordem de `adjustTargets`), um nos outros.
    struct AdjustTime {
        u32 now = ~0u;                 ///< o instante mostrado no lugar do atual
        u32 next = ~0u;                ///< o quadro seguinte (mistura, optical flow)
        f32 blendT = 0.0f;             ///< peso do seguinte (0 = só `now`)
        u8  blendMode = 0;             ///< 1 mistura, 2 movimento (optical flow)
        f32 vectorBlur = 0.0f;         ///< Flow Motion Blur: fração do par que o obturador cobre
        f32 flowBase = 0.0f;
        u32 channel[3] = {~0u, ~0u, ~0u};   ///< RGB no tempo (~0u = agora)
        bool rgb = false;
        u32 history = ~0u;             ///< o passado (Detectar movimento, Datamosh...)
        bool hasHistory = false;
        /// A composição de agora SEM a cor de fundo (efeitos que leem o alfa:
        /// sombra, borda, contorno, Solid Matte). Só no escopo 0.
        u32 bare = ~0u;
        std::vector<TemporalSample> echo;   ///< Eco: amostras (nested ~0u = agora)
        /// Aprimorar: a composição abaixo em t ± k (os vizinhos do efeito).
        std::vector<NeighborSample> neighbors;
        [[nodiscard]] bool active() const noexcept {
            return now != ~0u || rgb || hasHistory || bare != ~0u || !echo.empty() || vectorBlur > 0.0f
                || !neighbors.empty();
        }
    };
    std::vector<AdjustTime> adjustTime;
    /// Partículas (8.2): cena 3D, espaço mundo, histórico e desfoque por tempo.
    ParticleSpace particle;
};

struct FrameSnapshot {
    struct VideoSourceWait {
        LayerId layer{};
        AssetId asset{};
        u64 decodeWork = 0;   ///< successful reads, including discarded preroll
        bool opening = false;
        bool missing = false;
        bool stale = false;
        bool failed = false;
    };
    struct SceneExposureBatch {
        u32 scene = 0;
        u32 samplesPerBatch = 0;
        u64 renderBytesPerSample = 0;
    };
    VideoStreamInfo playbackStream{};
    VideoSource::Stats playbackDecode{};
    i64 playbackTargetUs = 0, playbackPtsUs = -1, playbackDurationUs = 0;
    u32 playbackPixelFormat = 0;

    u32  compWidth = 0;
    u32  compHeight = 0;
    Vec4 background{0, 0, 0, 1};   ///< linear
    FrameIndex time{0};
    std::vector<RenderLayer> layers;   ///< do fundo para a frente
    std::vector<EffectPlan>  plans;    ///< um por layer (mesmo índice)
    /// Grupos 3D do frame (layers 3D consecutivas da pilha). O RenderLayer do
    /// grupo aponta para cá.
    std::vector<scene3d::SceneFrame> scenes;
    /// Pure 3D exposures may retire geometry uploads between sample batches.
    /// All evaluated temporal samples remain immutable in `scenes`.
    std::vector<SceneExposureBatch> sceneExposureBatches;
    u32  videoLayers = 0;
    u32  staleVideoFrames = 0;         ///< mostrando frame aproximado (scrub)
    u32  missingVideoFrames = 0;       ///< nenhum frame ainda (primeiro decode)
    /// Export-only pending sources, including omitted layers and nested clips.
    /// Each source owns its watchdog; another decoder cannot renew its clock.
    std::vector<VideoSourceWait> videoSourceWaits;
    /// Camadas visíveis no instante que ficaram de fora por estarem inteiras
    /// fora da composição (sem decode, sem passe). Fase 8C §20.
    u32  culledLayers = 0;
    /// Pré-composições deste quadro (cada uma com o seu próprio snapshot,
    /// no tempo da fonte da camada). `target` = onde ela foi composta.
    std::vector<std::unique_ptr<FrameSnapshot>> nested;
    FGTexture target{};
    /// Glifos das camadas de texto deste quadro (a camada guarda o trecho).
    std::vector<GlyphInstance> glyphs;
    u32 glyphBase = 0;   ///< onde este snapshot começa no buffer do quadro (render)
    /// Blocos de máscara das camadas deste quadro (cabeçalhos + arestas).
    std::vector<Vec4> maskData;
    u32 maskBase = 0;    ///< onde este snapshot começa no buffer de máscaras (render)
    /// Malhas das camadas vetoriais (vec4) e onde começam no buffer do quadro.
    std::vector<Vec4> vec;
    u32 vecBase = 0;
    /// Histórico das partículas (8.2, render/ParticleScene.hpp) e onde começa.
    std::vector<Vec4> particleData;
    u32 particleBase = 0;
    void release_video_frames() noexcept {
        for (RenderLayer& layer : layers) {
            layer.source.frame.reset();
            layer.source.frameB.reset();
            layer.source.historyFrame.reset();
            for (auto& channel : layer.source.channel) channel.frame.reset();
            for (auto& sample : layer.temporal) sample.frame.reset();
            for (auto& sample : layer.neighbors) sample.frame.reset();
        }
        for (auto& child : nested) if (child) child->release_video_frames();
    }
};

struct SceneEditorView {
    bool enabled = false;
    f32 yaw = -30.0f, pitch = 20.0f, distance = 3.0f;
};
[[nodiscard]] scene3d::SceneCamera scene_editor_camera(u32 width, u32 height, const SceneEditorView& view) noexcept;
[[nodiscard]] Mat4 scene_editor_projection(u32 width, u32 height, const SceneEditorView& view) noexcept;

struct RenderSettings {
    // Nonzero only for the editor's composed-frame cache. Final export ignores it.
    u64 previewCacheRevision = 0;
    u64 previewCacheComposition = 0;
    bool previewCacheOnly = false; // prepare a future frame without presenting it
    bool rawPlayback = false;
    /// Prévia TOCANDO (o relógio do playback anda). Parado, no scrub, no preparo
    /// do buffer antes do play e no export: false. O TimeSlice em vídeo não
    /// tira o decoder do fluxo do playback para buscar faixas enquanto toca.
    bool previewPlaying = false;
    /// Opt-in completed540p composition. Default leaves ordinary preview/export
    /// unchanged; presentation still applies the normal zoom/color/checkerboard.
    TextureHandle cachedComposition{};
    const u16* cachedCompositionPixels = nullptr; // one-frame upload, copied during begin_frame
    u64 mediaGeneration = 0;
    SceneEditorView sceneEditor{}; ///< transient preview observer; ignored by finalQuality/export

    u32  previewNumerator = 1;
    u32  previewDenominator = 1;
    bool dither = true;
    bool gpuTimers = true;
    /// Cor atrás da composição na área de preview (linear). É o fundo da
    /// marca (#0F141A), para o preview não "piscar" contra a interface.
    Vec4 editorBackground{0.0048f, 0.0070f, 0.0103f, 1.0f};
    /// Área de trabalho em volta da composição (letterbox do preview), já
    /// CODIFICADA (o swapchain é UNORM e o passe de saída limpa com ela).
    /// #262C35: clara o bastante para uma composição preta não se confundir
    /// com o entorno, escura o bastante para não brigar com o vídeo.
    Vec4 pasteboard{0.149f, 0.173f, 0.208f, 1.0f};
    f32  viewportZoom = 1.0f;
    Vec2 viewportPan{0.0f, 0.0f};
    /// Export: amostras de desfoque de movimento da qualidade final
    /// (`MotionBlurSettings::samples`); a prévia também, reduzida pela folga do aparelho.
    bool finalQuality = false;
    /// Export (0.3.0, OfflineFrameRenderer): o prazo do quadro exato passou.
    /// Logo depois da entrada de um clipe o aproximado À FRENTE do alvo também
    /// vale (ainda só do mesmo trecho): sem isso o quadro preso logo depois de
    /// um corte não tinha candidato e o export parava ali.
    bool approximateAhead = false;
    /// Exact captures can finish pure, unblurred groups before their 2D graph.
    /// Their immutable scene state, shadow resolution and post quality stay exact.
    bool stageUnblurredScenes = false;
    /// Whole-graph OOM may recover with the same ordered passes. Offline
    /// callers wait bounded fences; display callers resume between render turns.
    bool allowGraphSegments = true;
    /// Incremental, opt-in offline path; authored samples and quality are kept.
    /// Mixed planes/particles and nested compositions retain the original path.
    bool boundedSceneExposure = false;
    u64 sceneExposureBatchBudgetBytes = 32ull << 20;
    const std::atomic<bool>* cancelFlag = nullptr;
    u64 sceneExposureFenceTimeoutNs = 120ull * 1000 * 1000 * 1000;
    /// Capture recovery shares the caller's absolute clock, including the
    /// failed whole compile. Only a completed submitted fence may renew its
    /// no-progress window; export and ordinary preview leave these null.
    u64 (*offscreenFenceDeadline)(void*) = nullptr;
    void (*offscreenFenceSubmitted)(void*, u64) = nullptr;
    void (*offscreenFenceCompleted)(void*, u64) = nullptr;
    void* offscreenFenceContext = nullptr;
    /// Exact offscreen captures enqueue cold local AI, then wait without holding
    /// the renderer lock. Export's private renderer keeps synchronous inference.
    bool deferLocalAi = false;
    /// Preview sob calor (ThermalManager): fração do custo das operações
    /// caras — resolução do optical flow, partículas, amostras de desfoque.
    /// 1 = completo; ≤ 0,25 também troca o movimento de pixels pela mistura.
    /// O export sempre usa 1.
    f32  heavyScale = 1.0f;
    /// Piso do APARELHO (calor/tier) só, sem o AUTO. O Smooth Motion do preview
    /// cai para a mistura por ele: com o AUTO no piso, o fluxo ligava/desligava
    /// a cada medição (beta 08/10, "Smooth Motion não funciona direito").
    f32  flowPolicyScale = 1.0f;
    /// Botões do preview AUTO 2.0 (Fase 8C): cada sistema lê o seu pelo
    /// `Renderer::preview_quality()`. O efetivo é o menor entre isto e
    /// `heavyScale`; com `finalQuality` tudo volta a 1 (o export não muda).
    PreviewQuality quality{};
    /// Sistemas pesados (8E) botão a botão. Com `heavyExplicit` falso sai de
    /// `HeavyQuality::from_scale(heavyScale, previewDenominator)`; o export
    /// (finalQuality) ignora os dois e usa `HeavyQuality::full()`.
    HeavyQuality heavy{};
    bool heavyExplicit = false;
};

/// A qualidade dos sistemas pesados que vale para estes ajustes (export = cheia).
[[nodiscard]] HeavyQuality resolve_heavy(const RenderSettings& s) noexcept;

/// Alvo fora da tela (export, testes visuais): a composição é escrita nesta
/// textura (RGBA16F, tamanho da composição × escala).
struct OffscreenTarget {
    TextureHandle texture{};
    u32 width = 0;
    u32 height = 0;

    /// Export: se válidos, a composição também sai em Y'CbCr 4:2:0 8 bits
    /// BT.709 de faixa limitada — Y em R8 (largura × altura) e CbCr em RG8
    /// (metade de cada lado). É o NV12 que o encoder recebe.
    TextureHandle yPlane{};
    TextureHandle uvPlane{};
    bool encodeDither = true;
    /// Planos em RGBA8 de 1/4 da largura (4 bytes por fragmento, os MESMOS
    /// bytes do R8/RG8): yuv_encode_packed.frag. Exige largura múltipla de 4.
    bool packedYuv = false;
    /// Export sobreposto: se válidos, os planos Y e CbCr são copiados para
    /// estes buffers de leitura NO MESMO frame (sem submissão extra nem espera);
    /// quem chama espera o fence do frame e lê do buffer mapeado.
    BufferHandle yReadback{};
    BufferHandle uvReadback{};
    /// Upscale do export (0.1.7): planos NV12 de `planeWidth`×`planeHeight`
    /// (> a composição). `upscaled` recebe a composição ampliada (textureNicest)
    /// e `upscaleHistory` é a do quadro anterior (TAA modo 4, se válida).
    u32 planeWidth = 0;
    u32 planeHeight = 0;
    TextureHandle upscaled{};
    TextureHandle upscaleHistory{};
    bool upscaleTemporal = false;
    bool upscaleHistoryValid = false;
    /// surface-direct (Android): se válido, a composição também sai em
    /// R'G'B' de 8 bits neste alvo RGBA8 (memória de AHardwareBuffer), do
    /// mesmo tamanho dos planos NV12, que continuam sendo gerados.
    TextureHandle rgb{};
    /// surface-direct confirmado (1o pacote conferido): pula os passes NV12 e
    /// o readback; só o `rgb` é escrito.
    bool rgbOnly = false;

    /// Teste do passe de SAÍDA sem swapchain: quando válido, a composição
    /// (`texture`) ainda é composta, e depois vai para este alvo UNORM pelo
    /// MESMO passe "saida" do preview — letterbox, zoom, pan, pré-rotação e a
    /// média de área do encolhimento — como se ele fosse a imagem do
    /// swapchain com `displayRotation`. É o que o host consegue exercer do
    /// caminho do aparelho (que só existe com superfície). Sem planos NV12.
    TextureHandle display{};
    u32 displayWidth = 0;
    u32 displayHeight = 0;
    SurfaceRotation displayRotation = SurfaceRotation::None;
};

/// Custo medido de um frame, por etapa. GPU vem das timestamp queries (de um
/// frame já concluído); 0 = não medido, nunca "instantâneo".
struct RenderTimings {
    f32 videoUploadMs = 0.0f;
    f32 cpuPrepareMs = 0.0f;
    f32 cpuRecordMs = 0.0f;
    f32 sceneExposureRecordMs = 0.0f;
    f32 sceneExposureWaitMs = 0.0f;
    u32 sceneExposureBatches = 0;
    f32 acquireWaitMs = 0.0f;   ///< espera por imagem do swapchain / fence
    f32 presentMs = 0.0f;
    f32 gpuTotalMs = 0.0f;
    f32 gpuColorConvMs = 0.0f;
    f32 gpuEffectsMs = 0.0f;
    f32 gpuBlurMs = 0.0f;
    f32 gpuGlowMs = 0.0f;
    f32 gpuCompositeMs = 0.0f;
    f32 gpuOutputMs = 0.0f;
    bool gpuMeasured = false;
    /// Latest-submission queries cannot represent a memory-segmented frame.
    bool gpuProfileFragmented = false;
};

class Renderer final : public EffectResources {
public:
    Renderer();
    ~Renderer() override;

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// Cria shaders, samplers e PRÉ-AQUECE todos os pipelines embutidos.
    [[nodiscard]] Status initialize(GPUBackend& backend, const EffectRegistry& effects) noexcept;
    void shutdown() noexcept;
    /// Offline export only: caller proved every submission/upload completed
    /// and excludes new work referencing this renderer through retirement.
    void shutdown_after_gpu_done() noexcept;
    /// O dispositivo morreu: esquece todos os objetos de GPU sem destruí-los.
    void forget_device() noexcept;
    [[nodiscard]] bool ready() const noexcept { return backend_ != nullptr; }

    /// Fase 1 (sob o lock do modelo).
    void prepare(const Composition& comp, const Project& project, FrameIndex time,
                 MediaManager* media, const ImagePixels* (*imageLookup)(void*, AssetId), void* imageCtx,
                 const RenderSettings& settings, u64 frameNumber, i32 playDirection,
                 DecodeMode decodeMode, f32 speed, FrameSnapshot& out);

    void prepare_raw(LayerId id, const Layer& layer, const Asset& asset, i64 mediaUs,
                     MediaManager& media, u64 frameNumber, DecodeMode mode,
                     i32 direction, f32 speed, u64 epoch, FrameSnapshot& out);

    /// Poll final CPU environment jobs without waiting on their futures.
    /// Busy means preparation is still pending; the caller owns one fixed
    /// resource deadline and preserves snapshot/project ownership between polls.
    /// Ready maps upload into the ordinary GPU caches, without extra CPU copies.
    [[nodiscard]] Status prepare_capture_resources(FrameSnapshot& snapshot,
                                                   const RenderSettings& settings) noexcept;

    /// Fase 2 (sem lock). `offscreen` nulo = apresenta no swapchain.
    [[nodiscard]] Status render(FrameSnapshot& snapshot, const RenderSettings& settings,
                                const OffscreenTarget* offscreen, FrameStats& stats,
                                RenderTimings& timings) noexcept;
    [[nodiscard]] bool preview_graph_pending() const noexcept { return previewGraph_.active; }
    /// Returns Busy while genuine GPU work is pending, without waiting for it.
    /// The caller must preserve the prepared snapshot until completion/cancel.
    [[nodiscard]] Status continue_preview_graph(FrameStats&, RenderTimings&) noexcept;
    void cancel_preview_graph() noexcept;
    [[nodiscard]] bool preview_graph_settings_match(const RenderSettings&) const noexcept;

    /// Automatic capture phases are bounded to four pure groups, each at most
    /// 320 x 320. Explicit RenderSettings retain their independent full-quality
    /// multi-scene path; larger automatic captures use ordinary composition.
    [[nodiscard]] static bool can_stage_small_scene_capture(const FrameSnapshot& snapshot,
                                                            u32 width, u32 height) noexcept;
    /// After a partial graph allocation failure, wait for its submitted uploads
    /// before discarding only unleased transient targets. A timeout keeps the
    /// pending frame and blocks the next graph from acquiring those targets.
    [[nodiscard]] Status reclaim_failed_transients(u64 timeoutNs = 0) noexcept;

    /// Descarta texturas de imagem e LUTs (projeto fechado).
    void release_project_resources() noexcept;

    void set_preview_cache_budget(u64 bytes) noexcept;
    [[nodiscard]] u64 preview_cache_budget() const noexcept { return previewCacheBudget_; }
    /// Pressão de memória (trim RUNNING_LOW+): solta todos os quadros guardados
    /// da prévia; devolve quantos tinham textura. Com o lock de render.
    u32 release_preview_cache() noexcept;
    /// Classe de memória LOW (Engine::low_memory_device): sombra do PREVIEW até
    /// 2048 e cache do Roto Brush pela metade. O export não muda.
    void set_low_memory_device(bool low) noexcept;
    [[nodiscard]] bool low_memory_device() const noexcept { return lowMemoryDevice_; }
    /// Edição local: descarta só os quadros guardados em [start, end).
    void invalidate_preview_frames(i64 start, i64 end) noexcept;
    void clear_preview_cache() noexcept;
    [[nodiscard]] u32 configure_preview_cache(u32 width, u32 height, const RenderSettings& settings) noexcept;
    [[nodiscard]] bool preview_cached(FrameIndex time) const noexcept;
    [[nodiscard]] u32 preview_cached_count() const noexcept;
    [[nodiscard]] bool last_preview_cache_hit() const noexcept { return previewCacheHit_; }
    /// Thread-safe UI snapshot; no render lock, GPU wait or cache-vector access.
    /// Writes at most 30 absolute [start, end) frame pairs and returns pairs written.
    [[nodiscard]] u32 copy_preview_buffer_ranges(u64 revision, u64 composition,
                                                 i64* outPairs, u32 capacityRanges) const noexcept;

    /// Pressão de memória do sistema (Fase 8 §13), com o lock de render do
    /// motor. `stage` segue aurea::TrimStage: ≥ 4 solta o cache de render que
    /// não entrou no último quadro (`frameNumber`) — planos de vídeo, flow,
    /// máscara, LUT, malha vetorial e o pool transitório; ≥ 6 solta os
    /// assets 3D sem uso. O quadro na tela não perde nada. Devolve quantas
    /// texturas saíram.
    u32 trim_memory(u8 stage, u64 frameNumber) noexcept;

    /// A PRÉVIA DE UM EFEITO (Fase 7.3 §13–§15): o efeito de verdade, com os
    /// valores PADRÃO da declaração, rodando sobre a cartela de demonstração
    /// (`effects/preview_plate.frag`). Um frame, fora da tela, sem projeto e
    /// sem composição — a prévia de um efeito não depende do que está aberto.
    ///
    /// Sai RGBA8 sRGB de alfa reto, do tamanho pedido. Devolve `NotImplemented`
    /// para efeito que não faz sentido num quadro solto (temporal, global) e
    /// `PipelineCompileFailed` quando o efeito não conseguiu montar os passes —
    /// nos dois casos a UI mostra a cartela genérica.
    /// Foto de base das prévias de efeito (RGBA8 sRGB, alfa reto). Vazia =
    /// a cartela de teste gerada no shader.
    void set_effect_preview_source(std::vector<u8> rgba, u32 width, u32 height) noexcept;
    /// Rig 2D em MONTAGEM: esta camada (id sem sal) aparece sem deformação no
    /// preview, para as juntas baterem com o desenho. 0 = nenhuma. O export
    /// (finalQuality) ignora.
    void set_rig_setup_layer(u64 layerId) noexcept { rigSetupLayer_.store(layerId, std::memory_order_relaxed); }
    [[nodiscard]] Status render_effect_preview(const EffectRegistry& effects, EffectTypeId type,
                                               u32 width, u32 height, std::vector<u8>& outRgba, bool comparison = false) noexcept;
    /// O efeito com os valores de um PRESET sobre uma imagem da CPU (RGBA8 sRGB
    /// de alfa reto, `width`×`height`): as miniaturas dos looks (Filtros).
    [[nodiscard]] Status render_effect_preset_preview(const EffectRegistry& effects, EffectTypeId type, u32 width,
                                                      u32 height, const u8* sourceRgba, const EffectPreset* preset,
                                                      std::vector<u8>& outRgba) noexcept;

    /// PRÉVIA ANIMADA (ver EffectPreviewScene). `prepare` captura as fontes
    /// (a foto/cartela com folga, uma vez); cada `frame` renderiza UM quadro
    /// antes/depois (RGBA8 sRGB de alfa reto, `width`×`height`). Separados para
    /// o motor soltar o mutex de render entre quadros (o preview do editor não
    /// espera a sequência inteira). Efeito de tempo e global entram aqui: o
    /// resultado temporal é montado sobre a cena que anda.
    [[nodiscard]] Status prepare_effect_preview_sequence(const EffectRegistry& effects, EffectTypeId type,
                                                         u32 width, u32 height, u32 frames,
                                                         EffectPreviewScene scene, EffectPreviewSequence& out) noexcept;
    [[nodiscard]] Status render_effect_preview_sequence_frame(const EffectRegistry& effects,
                                                              const EffectPreviewSequence& sequence, u32 index,
                                                              std::vector<u8>& outRgba) noexcept;
    /// Tudo de uma vez (testes): `frames` quadros concatenados em `outRgba`.
    [[nodiscard]] Status render_effect_preview_sequence(const EffectRegistry& effects, EffectTypeId type,
                                                        u32 width, u32 height, u32 frames, EffectPreviewScene scene,
                                                        std::vector<u8>& outRgba,
                                                        EffectPreviewSequenceInfo* info = nullptr) noexcept;

    /// De onde vêm os modelos 3D (o motor guarda os SceneAsset).
    using ModelLookup = std::shared_ptr<const scene3d::SceneAsset> (*)(void* ctx, AssetId id);
    /// O último quadro deixou alguma camada de fora por recurso pendente? (lê e zera)
    [[nodiscard]] bool take_incomplete() noexcept { const bool b = incomplete_; incomplete_ = false; return b; }
    /// Onde o último quadro incompleto foi marcado (arquivo:linha do motor), para
    /// o motivo do export dizer QUAL recurso faltou. Nulo = nenhum ainda.
    /// Export em GPU fraca: degrau de custo 3D (0 = qualidade final cheia).
    /// Cada degrau corta sombra/MSAA do arquivo para o passe 3D caber no prazo
    /// do driver móvel (~2 s por envio), em vez de a GPU ser resetada.
    [[nodiscard]] u32 export_gpu_step() const noexcept { return exportGpuStep_; }
    u32 raise_export_gpu_step() noexcept { if (exportGpuStep_ < 4) ++exportGpuStep_; return exportGpuStep_; }
    /// Export em GPU fraca: teto de amostras de motion blur (0 = sem teto).
    /// Cada chamada corta pela metade (128 -> 64 -> ... -> 2) quando o quadro
    /// inteiro passa de ~4 s de GPU (rastro com centenas de passes).
    [[nodiscard]] u32 export_blur_cap() const noexcept { return exportBlurCap_; }
    /// Trecho leve de novo: o teto dobra (64 ou mais = sem teto).
    u32 raise_export_blur_cap() noexcept {
        if (exportBlurCap_) exportBlurCap_ = exportBlurCap_ >= 32u ? 0u : exportBlurCap_ * 2u;
        return exportBlurCap_;
    }
    /// Teto inicial do export (celular): 0 = sem teto.
    void set_export_blur_cap(u32 cap) noexcept { exportBlurCap_ = cap >= 2 ? cap : 0u; }
    /// Teto rígido do degrau da escada (ExportAttempts.hpp): 0 = nenhum, 1 = sem desfoque.
    void set_export_blur_hard_cap(u32 cap) noexcept { exportBlurHardCap_ = cap; }
    /// Teto das amostras de cena 3D no export: cada amostra é a cena inteira
    /// (sombra, MSAA), então o custo cresce sem limite com o rastro (0.2.7).
    void set_export_scene_blur_cap(u32 cap) noexcept { exportSceneBlurCap_ = cap; }
    /// Piso do teto adaptativo (0.3.0): abaixo disso o rastro vira cópias
    /// separadas da camada (o "fantasma" do export); o próximo passo é o
    /// degrau de custo 3D (sombra/MSAA), e a escada (ExportAttempts) ainda
    /// pode tirar mais amostras se a GPU estourar o prazo.
    static constexpr u32 kMinAdaptiveExportBlur = 8;
    u32 lower_export_blur_cap() noexcept {
        exportBlurCap_ = exportBlurCap_ ? std::max(kMinAdaptiveExportBlur, exportBlurCap_ / 2) : 64u;
        return exportBlurCap_;
    }
    [[nodiscard]] const char* incomplete_file() const noexcept { return incompleteFile_; }
    [[nodiscard]] u32 incomplete_line() const noexcept { return incompleteLine_; }
    [[nodiscard]] u32 local_ai_pending() const noexcept { return localAiPending_; }
    void set_model_lookup(ModelLookup fn, void* ctx) noexcept { modelLookup_ = fn; modelCtx_ = ctx; }
    using HdriLookup = std::shared_ptr<const scene3d::HdriPixels> (*)(void* ctx, AssetId id);
    using CubeLookup = std::shared_ptr<const CubeLut> (*)(void* ctx, AssetId id);
    void set_cube_lookup(CubeLookup fn, void* ctx) noexcept { cubeLookup_ = fn; cubeCtx_ = ctx; }
    void set_hdri_lookup(HdriLookup fn, void* ctx) noexcept { hdriLookup_ = fn; hdriCtx_ = ctx; }
    /// Resolve o ambiente PRÓPRIO de uma camada 3D para a instância (v22):
    /// sem `Custom`, a instância fica com o ambiente do grupo.
    void fill_object_environment(const Layer& l, scene3d::SceneInstance& inst) noexcept;
    [[nodiscard]] const scene3d::SceneStats& scene_stats() const noexcept { return scene3d_.stats(); }
    [[nodiscard]] bool environment_pending() const noexcept { return scene3d_.environment_pending(); }
    /// Uploads (modelo, textura, imagem) ainda na fila: progresso de GPU para o export.
    [[nodiscard]] usize pending_uploads() const noexcept { return uploads_.size(); }
    [[nodiscard]] scene3d::SceneRenderer& scene_renderer() noexcept { return scene3d_; }
    [[nodiscard]] u64 scene_resident_bytes() const noexcept { return scene3d_.resident_bytes(); }
    [[nodiscard]] u64 static_pose_cache_bytes() const noexcept { return staticPoseBytes_; }
    /// Contadores dos sistemas pesados (texto, vetor, máscara, flow, partículas, 3D).
    [[nodiscard]] const HeavyStats& heavy_stats() const noexcept { return heavyStats_; }
    void reset_heavy_stats() noexcept { heavyStats_ = HeavyStats{}; }
    /// A qualidade dos sistemas pesados do último quadro renderizado.
    [[nodiscard]] const HeavyQuality& heavy_quality() const noexcept { return heavyQ_; }
    /// Benchmark A/B do instancing 3D (padrão: ligado).
    void set_scene_instancing(bool on) noexcept { scene3d_.set_instancing(on); }
    /// Orçamento do cache do optical flow (padrão 48 MB; o gerenciador de
    /// memória pode baixar sob pressão). Acima dele sai a camada mais antiga.
    void set_flow_cache_budget(u64 bytes) noexcept { flowCacheBudget_ = bytes; }
    void set_transient_cache_budget(u64 bytes) noexcept { pool_.set_budget(bytes); }
    void set_transient_allocation_limit(u64 bytes) noexcept { pool_.set_allocation_limit(bytes); }
    void set_tracked_resource_budget(u64 bytes, TransientTexturePool::KnownResourceUse knownUse = nullptr, void* context = nullptr) noexcept {
        pool_.set_tracked_resource_budget(bytes, knownUse, context);
    }
    [[nodiscard]] f32 effect_quality() const noexcept override { return heavyQ_.effects; }

    // --- EffectResources -----------------------------------------------------
    [[nodiscard]] TextureHandle curve_lut(const CurveData& curve) noexcept override;
    [[nodiscard]] TextureHandle cube_lut(AssetId id) noexcept override;
    [[nodiscard]] TextureHandle data_texture(u64 key, const Vec4* texels, u32 width, u32 height) noexcept override;
    /// Espectro do som de uma camada no instante do `prepare` em curso (ver
    /// EffectResources). Decodifica o trecho na hora (síncrono, cache de
    /// blocos próprio) e guarda a textura por (asset, amostra, faixas).
    [[nodiscard]] TextureHandle audio_spectrum(const AudioSpectrumRequest& request) noexcept override;
    [[nodiscard]] std::vector<Vec4> repeat_path(const Layer* host, u32 count, f32 phase, u64 guide = 0) noexcept override;
    [[nodiscard]] bool layer_world(const Layer* host, u64 ref, f64 compTime, LayerWorldInfo& out) noexcept override;
    /// Mapa de profundidade da fonte da camada no instante do `prepare` em
    /// curso (render/RendererDepth.cpp). Imagem: síncrono, uma vez. Vídeo: o
    /// export espera o quadro; o preview agenda e mostra o último pronto.
    [[nodiscard]] DepthMapResult depth_map(const DepthMapRequest& request) noexcept override;
    /// Forma de onda / Espectro de áudio (render/RendererAudio.cpp): o som da
    /// camada escolhida, com os efeitos de áudio dela, numa janela em volta do
    /// quadro. Preview: blocos que faltam são pedidos e o quadro volta
    /// (incompleto); export: decodifica na hora.
    [[nodiscard]] AudioAnalysisResult audio_analysis(const AudioAnalysisRequest& request) noexcept override;
    /// O cache de blocos do mixer (os mesmos blocos que tocam) e o resolvedor
    /// de caminhos do motor. Sem ele (testes), um cache próprio.
    void set_audio_source(audio::AudioBlockCache* shared, std::string (*resolve)(void*, const std::string&),
                          void* ctx) noexcept {
        sharedAudio_ = shared;
        audioResolve_ = resolve;
        audioResolveCtx_ = ctx;
    }
    /// O serviço dos mapas (testes e HUD); nulo até o primeiro pedido.
    [[nodiscard]] ai::DepthMapService* depth_service() noexcept { return depth_.get(); }
    [[nodiscard]] ai::DepthMapService* foreground_service() noexcept { return foreground_.get(); }
    /// Roto Brush (traços + propagação, RendererRoto.cpp); nulo até o primeiro pedido.
    [[nodiscard]] ai::RotoService* roto_service() noexcept { return roto_.get(); }
    /// Recorte do Rotobrush JÁ calculado da camada (CPU, RotoService::kSize²,
    /// 0..1) no quadro `frame` da fonte; nulo = ainda não há (o Fantoche usa
    /// como contorno da malha).
    [[nodiscard]] std::shared_ptr<const std::vector<f32>> roto_cached_matte(const Composition* comp, const Layer& layer,
                                                                            const EffectInstance& instance, i64 frame, const Asset* asset) noexcept;
    /// "Propagar clipe": todos os quadros da fonte da camada no worker do Roto.
    bool roto_propagate(const Project& project, const Composition& comp, const Layer& layer,
                        const EffectInstance& instance, MediaManager* media) noexcept;
    /// Estado do Roto DESTA trilha (camada + instância), para a UI:
    /// out[0] quadros propagados, [1] total da propagação, [2] propagando (0/1),
    /// [3] falhou (0/1), [4] recorte do quadro `sourceFrame` pronto (0/1),
    /// [5] quadros do clipe já com recorte válido, [6] quadros do clipe,
    /// [7] há traços (0/1). Falso = camada sem Rotobrush com traços.
    bool roto_status(const Project& project, const Composition& comp, const Layer& layer,
                     const EffectInstance& instance, i64 sourceFrame, i64* out8) noexcept;
    void set_foreground_model_directory(std::string path);

    // --- Consultas -------------------------------------------------------------
    [[nodiscard]] const FrameGraph::Stats& graph_stats() const noexcept { return graph_.stats(); }
    [[nodiscard]] const TransientTexturePool::Stats& pool_stats() const noexcept { return pool_.stats(); }
    /// Tempos de GPU por passe lidos no último `render` com `gpuTimers`
    /// (rótulos estáticos; os de um quadro já concluído pela GPU).
    [[nodiscard]] u32 last_gpu_passes(const GpuTiming*& out) const noexcept { out = timingScratch_.data(); return lastGpuPasses_; }
    [[nodiscard]] ShaderLibrary& shaders() noexcept { return shaders_; }
    [[nodiscard]] u32 pipelines_prewarmed() const noexcept { return prewarmed_; }
    /// Pipelines de efeito/3D compilados pela varredura do projeto (fora do
    /// playback), depois da abertura. Teste de abertura e HUD.
    [[nodiscard]] u32 pipelines_warmed_for_project() const noexcept { return warmedForProject_; }
    [[nodiscard]] bool last_frame_zero_copy() const noexcept { return lastZeroCopy_; }
    [[nodiscard]] u64 video_plane_upload_bytes() const noexcept { return videoPlaneUploadBytes_; }
    [[nodiscard]] std::string graph_dump() const { return graph_.dump(); }
    [[nodiscard]] u32 frames_rendered() const noexcept { return framesRendered_; }
    /// Reduções do preview em vigor no quadro sendo preparado/renderizado
    /// (tudo 1 no export). Os sistemas pesados (3D, partículas, flow, blur)
    /// leem daqui o botão deles.
    [[nodiscard]] const PreviewQuality& preview_quality() const noexcept { return quality_; }
    /// O efetivo de um `RenderSettings` (a mesma regra do prepare/render).
    [[nodiscard]] static PreviewQuality effective_quality(const RenderSettings& s) noexcept;

private:
    struct CompositeDraw {
        FGTexture texture{};
        Rect      region{};
        Mat4      compFromLayer = Mat4::identity();
        f32       opacity = 1.0f;
        BlendMode blend = BlendMode::Normal;
        /// Borda transparente (antisserrilhado de layer girada) para texturas
        /// de imagem; repetição para o sólido de 1x1, que a borda apagaria.
        u64       sampler = 0;
        /// Camada de ajuste: índice do plano de efeitos em `FrameSnapshot::plans`
        /// (a textura só existe no passe, feita do fundo acumulado).
        u32       adjustPlan = kInvalidIndex;
        /// Id (com sal) da camada que gerou o desenho (ajuste de escopo 2).
        u64       layer = 0;
        /// Copiar fundo: a janela (a camada no tamanho da composição) que
        /// recorta o resultado da camada de ajuste, e o modo do
        /// mask/track_matte.frag (1 alfa, 2 alfa invertido, 3 luma, 4 luma inv.).
        FGTexture windowMatte{};
        u8        windowMode = 0;   ///< 0 = sem janela
        /// Recortada (máscara de recorte): entra no grupo da base como
        /// SOURCE-ATOP — fonte·α_destino + destino·(1 − α_fonte), com a
        /// mistura dela; o alfa do grupo continua o da base.
        bool      clipAtop = false;
    };
    /// Os desenhos da pilha → alvo: lotes de blend de hardware (Normal, Add)
    /// no mesmo alvo; modo que lê o fundo ou camada de ajuste = ping-pong.
    /// `isolated`: grupo de recorte — monta sobre transparente (sem a cor de
    /// fundo da composição).
    void composite_draws(FrameSnapshot& snap, FGTexture comp, const TextureDesc& compDesc,
                         const std::vector<CompositeDraw>& draws, EffectBuildContext& ctx,
                         u32 firstDraw = 0, bool isolated = false) noexcept;
    /// Camada de ajuste: a entrada dos efeitos (o fundo de agora, `in`) passa
    /// pelos efeitos de TEMPO (`RenderLayer::AdjustTime`): instante trocado,
    /// mistura/optical flow, RGB no tempo, eco, passado (`in.history`).
    void adjust_time_input(const FrameSnapshot& snap, const RenderLayer& layer, u32 target, LayerImage& in,
                           const TextureDesc& compDesc, u64 frameNumber) noexcept;
    /// O resultado dos efeitos de uma camada de ajuste no quadro inteiro: a
    /// geometria dobrada (Transform/Agitar... no fim da pilha) e, se pedido,
    /// a cor de fundo por baixo (o que a geometria descobre mostra o fundo).
    /// Mapa de profundidade numa camada de ajuste: o da foto/vídeo de cima
    /// entre as que ela ajusta, com a conta quadro → px dessa camada.
    [[nodiscard]] DepthMapResult depth_map_below(const DepthMapRequest& request) noexcept;
    [[nodiscard]] FGTexture adjust_full_frame(const LayerImage& fin, const Mat4& fold, f32 opacity, Vec4 under,
                                              const TextureDesc& compDesc, f32 compW, f32 compH) noexcept;

    /// Textura persistente de planos de vídeo (fallback sem zero-copy): uma
    /// por layer, reaproveitada frame a frame.
    struct PlanarTextures {
        TextureHandle plane[3]{};
        u32 width = 0, height = 0;
        PixelFormat format = PixelFormat::Unknown;
        bool halfP010 = false; ///< exact P010 upload when normalized16 textures are unavailable
        u64 lastFrame = 0;
        u64 contentId = 0;
    };

    // Foto de base das prévias de efeito (sobe na primeira prévia depois de trocada).
    std::vector<u8> previewSrc_;
    u32 previewSrcW_ = 0, previewSrcH_ = 0;
    bool previewSrcDirty_ = false;
    TextureHandle previewSrcTex_{};

    struct ImageTexture {
        TextureHandle texture{};
        u32 width = 0, height = 0;
        u64 lastFrame = 0;
        /// A imagem já no espaço de trabalho (linear, pré-multiplicada) na
        /// densidade da camada: a conversão roda uma vez por tamanho, não a
        /// cada quadro (Fase 8C — eram N passes por quadro com N imagens).
        /// Dois tamanhos: a mesma imagem em duas camadas de escalas diferentes
        /// não se expulsa a cada quadro.
        struct Linear {
            TextureHandle tex{};
            u32 w = 0, h = 0;
            u64 builtFrame = ~0ull;   ///< quadro (do backend) em que o passe foi declarado
            u64 lastFrame = 0;
            FGTexture fg{};           ///< a importação no grafo do quadro `fgFrame`
            u64 fgFrame = ~0ull;
        };
        Linear linear[2];
    };
    void destroy_image_linear(ImageTexture& img) noexcept;
    u32 imageLinearHits_ = 0, imageLinearBuilds_ = 0;
public:
    /// Imagens: conversões reaproveitadas / feitas (testes e HUD).
    void image_cache_stats(u32& hits, u32& builds) const noexcept { hits = imageLinearHits_; builds = imageLinearBuilds_; }
private:

    struct LutTexture {
        TextureHandle texture{};
        u64 lastFrame = 0;
    };

    struct PendingUpload {
        TextureHandle texture{};
        std::vector<u8> data;
        u32 bytesPerRow = 0;
        bool mips = false;   ///< gera a cadeia de mips depois de subir o nível 0
    };

    void fill_scene_context(const Composition& comp, FrameIndex time, FrameSnapshot& out, const scene3d::SceneCamera& camera) noexcept;
    /// Camadas do snapshot → alvo (fonte, efeitos, desfoque, composição). As
    /// pré-composições entram antes, cada uma no seu alvo (recursivo).
    void compose_layers(FrameSnapshot& snap, FGTexture comp, const TextureDesc& compDesc, u64 frameNumber,
                        std::vector<CompositeDraw>& draws, u32 depth) noexcept;
    [[nodiscard]] bool build_source(const RenderLayer& layer, u32 layerIndex, bool hasEffects,
                                    LayerImage& out, std::vector<FrameRef>& framesUsed,
                                    u64 frameNumber, u32 glyphSet = kInvalidIndex) noexcept;
    [[nodiscard]] bool build_video_source(const RenderLayer& layer, u32 layerIndex, u32 w, u32 h,
                                          FGTexture target, u64 frameNumber, DecodedFrame* frame) noexcept;
    [[nodiscard]] bool build_video_source(const RenderLayer& layer, u32 layerIndex, u32 w, u32 h,
                                          FGTexture target, u64 frameNumber) noexcept;
    /// Imagem da camada × cobertura das máscaras (cobertura em cache por camada).
    void apply_masks(const RenderLayer& layer, LayerImage& img, u64 frameNumber) noexcept;
    /// O desenho da camada sozinho num alvo do tamanho da composição.
    [[nodiscard]] FGTexture draw_to_comp(const CompositeDraw& d, const TextureDesc& compDesc, f32 compW, f32 compH,
                                         const char* name) noexcept;
    void flush_uploads() noexcept;
    void configure_scene_quality(const scene3d::SceneFrame& group) noexcept;
    struct StagedSceneTarget {
        const FrameSnapshot* owner = nullptr;
        u32 scene = 0;
        TextureHandle texture{};
        u64 pendingFrame = 0;
    };
    [[nodiscard]] Status render_staged_scene_targets(const FrameSnapshot&, const RenderSettings&,
        u32 width, u32 height, std::span<StagedSceneTarget> targets,
        FrameStats& stats, RenderTimings& timings) noexcept;
    [[nodiscard]] Status render_bounded_scene_exposures(const FrameSnapshot&, const RenderSettings&,
        u32 width, u32 height, std::span<TextureHandle> textures,
        FrameStats& stats, RenderTimings& timings) noexcept;
    [[nodiscard]] Status render_segmented_graph(const RenderSettings&, FrameBegin&,
        RenderTimings&, bool& frameOpen) noexcept;
    struct PreviewFrame;
    struct RenderCompletion {
        FrameBegin frame{};
        FrameStats exposureStats{};
        HeavyStats stagedStats{};
        PreviewFrame* cached = nullptr;
        std::span<StagedSceneTarget> targets{};
        u64 effectsBefore = 0, began = 0, buildFrame = 0;
        u32 width = 0, height = 0;
        bool offscreen = false;
    };
    struct PendingPreviewGraph {
        bool active = false, initial = false;
        u32 nextPass = 0;
        u64 waitingFrame = 0, started = 0;
        u64 activeRecordNs = 0;
        FrameSnapshot* snapshot = nullptr;
        RenderSettings settings{};
        RenderCompletion completion{};
        RenderTimings timings{};
        std::unique_ptr<TextureHandle[]> exposureTextures;
        u32 exposureCount = 0;
        std::unique_ptr<StagedSceneTarget[]> stagedTargets;
        u32 stagedCount = 0;
    } previewGraph_;
    [[nodiscard]] Status finish_render(FrameSnapshot&, const RenderSettings&, const OffscreenTarget*,
        Status, bool frameOpen, RenderCompletion&, FrameStats&, RenderTimings&) noexcept;
    void release_preview_graph_scope() noexcept;
    void collect_resources(u64 frameNumber) noexcept;
    void read_timings(RenderTimings& t) noexcept;

    GPUBackend* backend_ = nullptr;
    void shutdown_impl(bool gpuDone) noexcept;
    void release_project_resources_impl(bool gpuDone) noexcept;
    const EffectRegistry* effects_ = nullptr;
    EffectTypeId posterizeType_ = 0, echoType_ = 0, rgbTimeType_ = 0, lowFpsType_ = 0;
    EffectTypeId randomEditsType_ = 0;   ///< Edições aleatórias: trechos embaralhados (0.2.7)
    EffectTypeId motionDetectType_ = 0;   ///< Detectar movimento: a fonte em t − atraso
    EffectTypeId datamoshType_ = 0;       ///< Datamosh: a fonte presa por "Quadros segurados"
    EffectTypeId timeSliceType_ = 0;      ///< TimeSlice: uma faixa por instante
    EffectTypeId wideTimeType_ = 0;       ///< Tempo amplo: média de instantes (0.3.0)
    /// TimeSlice em vídeo: o quadro de cada faixa já convertido, por (camada,
    /// instante da fonte). O decoder segura poucos quadros ao mesmo tempo; as
    /// faixas precisam de até 64. De um quadro para o seguinte quase todas se
    /// repetem, então em regime só um quadro novo é decodificado por quadro.
    struct SliceTexture {
        u64 layer = 0; i64 us = 0; TextureHandle tex{}; u32 w = 0, h = 0; u64 lastFrame = 0;
        FGTexture fg{}; u64 fgFrame = ~0ull;   ///< importada neste quadro do grafo
        u64 created = 0;                        ///< quadro em que a conversão foi gravada
    };
    std::vector<SliceTexture> sliceCache_;
    mutable std::mutex sliceMutex_;
    u64 sliceGraphFrame_ = ~0ull;   ///< quadro do grafo que gravou as faixas mais novas
    u64 sliceSalt_ = 0;             ///< planos de CPU próprios para a conversão de uma faixa
    bool sliceApprox_ = false;      ///< este quadro desenhou faixa aproximada (o instante ainda não chegou)
    [[nodiscard]] bool has_slice_texture(u64 layer, i64 us) const noexcept;
    void release_slice_cache(bool destroy) noexcept;
    /// Aprimorar: a textura do vizinho `slot` de uma camada de vídeo no tamanho
    /// `w`×`h` — o quadro exato vira textura persistente no MESMO cache das
    /// faixas do TimeSlice (chave camada + instante), o aproximado é transitório.
    [[nodiscard]] FGTexture neighbor_video_texture(const RenderLayer& layer, u32 layerIndex,
                                                   const RenderLayer::NeighborSample& n, u32 slot, u32 w, u32 h,
                                                   u64 frameNumber) noexcept;
    /// Flow Retime "Substituir quadros duplicados": a assinatura de luma 32×18
    /// de cada quadro da fonte (soma de 2×2 amostras do plano Y por célula, em
    /// quartos de código), calculada uma vez por quadro e guardada por asset +
    /// instante.
    static constexpr u32 kSignatureCells = 32 * 18;
    struct FrameSignature {
        i64 pts = 0;
        u64 used = 0;
        u16 luma[kSignatureCells]{};
    };
    struct SignatureTrack {
        std::unordered_map<i64, FrameSignature> frames;   ///< por instante (referências estáveis)
        u64 clock = 0;
    };
    std::unordered_map<u64, SignatureTrack> signatures_;
    /// A assinatura do quadro (calcula e guarda se ainda não existe); nula se
    /// o quadro não tem pixels legíveis na CPU.
    [[nodiscard]] const FrameSignature* frame_signature(u64 asset, i64 pts, DecodedFrame* frame) noexcept;
    [[nodiscard]] const FrameSignature* find_signature(u64 asset, i64 pts) noexcept;
    /// Dois quadros iguais para a sensibilidade (0..100) do Flow Retime.
    [[nodiscard]] static bool signatures_match(const FrameSignature& a, const FrameSignature& b, f32 sensitivity) noexcept;
    ShaderLibrary shaders_;
    scene3d::SceneRenderer scene3d_;
    scene3d::PoseWorkspace poseWorkspace_;
    struct StaticPoseEntry {
        std::weak_ptr<const scene3d::SceneAsset> owner;
        std::shared_ptr<const scene3d::SceneInstance> pose;
        u64 bytes = 0, used = 0;
    };
    std::unordered_map<const scene3d::SceneAsset*, StaticPoseEntry> staticPoses_;
    u64 staticPoseBytes_ = 0, staticPoseUse_ = 0;
    std::shared_ptr<const scene3d::SceneInstance> static_model_pose(const std::shared_ptr<const scene3d::SceneAsset>& asset);
    ModelLookup modelLookup_ = nullptr;
    HdriLookup hdriLookup_ = nullptr;
    CubeLookup cubeLookup_ = nullptr;
    void* cubeCtx_ = nullptr;
    void* hdriCtx_ = nullptr;
    void* modelCtx_ = nullptr;
    FrameGraph graph_;
    TransientTexturePool pool_;
    u64 failedGraphFrame_ = 0;
    Arena arena_{64 * 1024};

    std::vector<CompositeDraw> draws_;
    std::vector<FrameRef> framesInFlight_;
    /// Percurso dos snapshots (raiz + pré-composições) nos uploads do quadro:
    /// listas reaproveitadas — eram 6 alocações por quadro (Fase 8C).
    std::vector<FrameSnapshot*> snapAll_, snapStack_;
    std::unordered_map<u64, PlanarTextures> planar_;   ///< por LayerId empacotado
    // Texto na GPU: atlas de glifos (R8) e o buffer de glifos do quadro (anel).
    TextureHandle glyphAtlas_{};
    u64 glyphAtlasGen_ = 0;
    static constexpr u32 kGlyphRing = 4;
    BufferHandle glyphBuf_[kGlyphRing]{};
    usize glyphCap_[kGlyphRing]{};
    u32 glyphSlot_ = 0;
    BufferHandle glyphFrameBuf_{};
    void upload_glyphs(FrameSnapshot& snap) noexcept;
    // Máscaras: o buffer de arestas do quadro (anel) e a cobertura em cache por
    // camada — duas texturas alternadas, como o optical flow.
    BufferHandle maskBuf_[kGlyphRing]{};
    usize maskCap_[kGlyphRing]{};
    u32 maskSlot_ = 0;
    BufferHandle maskFrameBuf_{};
    void upload_masks(FrameSnapshot& snap) noexcept;
    struct MaskCache {
        TextureHandle tex[2]{};
        u64 key[2]{0, 0};
        u32 width = 0, height = 0;
        u32 next = 0;
        u64 lastFrame = 0;
    };
    std::unordered_map<u64, MaskCache> maskCache_;
    u32 maskHits_ = 0, maskMisses_ = 0;
    // Malhas vetoriais: o mesmo anel de buffers do quadro que os glifos.
    BufferHandle vecBuf_[kGlyphRing]{};
    usize vecCap_[kGlyphRing]{};
    u32 vecSlot_ = 0;
    BufferHandle vecFrameBuf_{};
    void upload_vectors(FrameSnapshot& snap) noexcept;
    // Partículas (8.2): o histórico do quadro (binding 16), no mesmo anel.
    BufferHandle particleBuf_[kGlyphRing]{};
    usize particleCap_[kGlyphRing]{};
    u32 particleSlot_ = 0;
    BufferHandle particleFrameBuf_{};
    void upload_particle_history(FrameSnapshot& snap) noexcept;
    /// Push do particles.vert para a subamostra `sub` da camada.
    [[nodiscard]] ParticlePush particle_push(const RenderLayer& layer, u32 sub, f32 weight) const noexcept;
    /// Desenhos das partículas 3D do grupo no passe da cena, com a câmera e o
    /// deslocamento de tempo do (sub)quadro `frame`. Memória da arena.
    [[nodiscard]] u32 scene_particle_draws(const scene3d::SceneFrame& group, const scene3d::SceneFrame& frame,
                                           scene3d::SceneParticleDraw*& out) noexcept;
    /// Monta `rl.particle` e o bloco do histórico em `out.particleData`
    /// (render/ParticleScene.cpp). Sem cena 3D, espaço mundo, emissão animada
    /// nem desfoque: flags = 0 e nada muda no 2D de sempre.
    void prepare_particle_space(const Composition& comp, const Layer& l, FrameIndex local, const RenderSettings& settings,
                                bool in3d, RenderLayer& rl, FrameSnapshot& out) const noexcept;
    /// O quad indexado das partículas (6 índices u16), criado uma vez.
    [[nodiscard]] bool particle_quad_ready() noexcept;
    /// Malha vetorial por camada: refeita só quando a chave (grupos avaliados +
    /// densidade) muda — camada parada não retriangula a cada quadro.
    struct VectorCacheEntry { u64 key = 0; u64 lastFrame = 0; std::vector<Vec4> verts, paints; Vec2 min{}, max{}; };
    std::unordered_map<u64, VectorCacheEntry> vectorCache_;
    std::atomic<u64> rigSetupLayer_{0};
    /// Cache do optical flow por camada: duas texturas alternadas (a que um
    /// quadro em voo lê nunca é a que o próximo escreve), cada uma com o par
    /// de quadros da fonte que a gerou.
    struct FlowCache {
        TextureHandle tex[2]{};
        u64 key[2]{0, 0};
        u32 width = 0, height = 0;
        u32 next = 0;
        u64 lastFrame = 0;
    };
    std::unordered_map<u64, FlowCache> flowCache_;
    u32 flowHits_ = 0, flowMisses_ = 0;
    f32 heavyScale_ = 1.0f;
    PreviewQuality quality_{};
    HeavyQuality heavyQ_{};
    HeavyStats heavyStats_{};
    u32 glyphRasterSeen_ = 0, glyphResetSeen_ = 0;   ///< leitura anterior de text::glyph_atlas_stats
    BufferHandle particleQuad_{};                      ///< 6 índices u16 do quad (partículas indexadas)
    // --- Aurea Particular 8.2: dados extras (render/ParticleExtras) -----------
    // Isolado do caminho 3D/histórico: o prepare monta o cabeçalho e o
    // desenho 2D sobe o buffer da camada (binding AUREA_DATA).
    particles::StaticCache particleStatics_;
    particles::GpuBuffers particleExtraBufs_;
    /// Prepare: o cabeçalho do quadro da camada de partículas (e a textura
    /// da partícula na GPU, se houver). Nada muda sem recurso em uso.
    void prepare_particle_extras(const Composition& comp, const Layer& layer, const ParticleData& pd, FrameIndex time,
                                 const ImagePixels* (*imageLookup)(void*, AssetId), void* imageCtx, u64 frameNumber,
                                 LayerId rid, LayerSource& src) noexcept;
    /// Desenho: sobe o cabeçalho (e o estático, se mudou) e devolve o que o
    /// passe liga. `on` falso = o shader fica no caminho de antes.
    struct ParticleExtrasBind {
        bool on = false;
        BufferHandle buffer{};
        u32 headerBase = 0;
        TextureHandle texture{};   ///< imagem da partícula (RGBA8 sRGB, alfa reto)
        bool mesh = false;
        u32 meshVertices = 0;
    };
    [[nodiscard]] ParticleExtrasBind particle_extras_bind(const LayerSource& src, u64 frameNumber) noexcept;
    /// Escreve o bloco 19 (extras) do bloco de parâmetros a partir do bind.
    static void particle_extras_params(const ParticleExtrasBind& b, Vec4* params) noexcept;
    /// Orçamento do cache do optical flow (texturas residentes, todas as camadas).
    u64 flowCacheBudget_ = 48ull << 20;
    void trim_flow_cache(u64 keepLayer, u64 incomingBytes) noexcept;
    /// Planos de cada grupo 3D do snapshot sendo composto (camadas 2D na cena).
    std::vector<std::vector<scene3d::ScenePlane>> groupPlanes_;
    std::vector<scene3d::ScenePlane> blurPlanes_;   ///< planos de um sub-quadro do desfoque 3D
    bool flowCacheEnabled_ = true;   ///< do quadro sendo renderizado (RenderSettings::heavyScale)
    bool motionBlurCropEnabled_ = true;
    bool boundedTextSurfaceEnabled_ = true;
    [[nodiscard]] FGTexture video_flow(u64 layerKey, u64 pairKey, FGTexture a, FGTexture b, u32 w, u32 h, f32 baseSide, u32& baseW, u32& baseH,
                                       u64 frameNumber) noexcept;
public:
    /// Acertos/erros do cache do optical flow (testes e HUD).
    void flow_cache_stats(u32& hits, u32& misses) const noexcept { hits = flowHits_; misses = flowMisses_; }
    /// Benchmark: sem cache, todo quadro calcula o fluxo.
    void set_flow_cache_enabled(bool on) noexcept { flowCacheEnabled_ = on; }
    /// Optimization toggle for pixel-equivalence tests and memory benchmarks.
    void set_motion_blur_crop_enabled(bool on) noexcept { motionBlurCropEnabled_ = on; }
    void set_bounded_text_surface_enabled(bool on) noexcept { boundedTextSurfaceEnabled_ = on; }
    /// Coberturas de máscara reaproveitadas / rasterizadas (testes e HUD).
    void mask_cache_stats(u32& hits, u32& misses) const noexcept { hits = maskHits_; misses = maskMisses_; }
private:
    std::unordered_map<u64, ImageTexture> images_;     ///< por AssetId empacotado
    /// Imagens do "Trocar textura 3D" na GPU (sRGB com mips, o formato das
    /// texturas de cor dos modelos), por AssetId empacotado.
    std::unordered_map<u64, ImageTexture> sceneImages_;
    /// A imagem `key` pronta para cobrir uma parte 3D (sobe na 1ª vez).
    [[nodiscard]] TextureHandle scene_image_texture(u64 key, const ImagePixels* px, u64 frameNumber) noexcept;
    std::unordered_map<u64, LutTexture> luts_;         ///< por hash da curva
    std::vector<u64> meshDataKeys_;                    ///< data_texture em luts_, da mais velha à mais nova
    // --- Espectro de áudio (EffectResources::audio_spectrum) ----------------
    // A textura de um quadro é função pura de (asset, amostra central, faixas,
    // ganho): a chave é isso. Blocos decodificados na hora, num cache próprio
    // (o do playback pertence ao motor e anda com o alto-falante).
    std::unordered_map<u64, LutTexture> spectra_;
    std::unique_ptr<audio::AudioBlockCache> spectrumBlocks_;
    VideoSourceFactory* spectrumFactory_ = nullptr;
    // --- Forma de onda / Espectro de áudio (EffectResources::audio_analysis) --
    // Texturas em `spectra_` (a mesma vida). O fluxo dos efeitos de áudio de
    // cada instância continua de um quadro para o seguinte (sem pré-rolagem
    // a cada quadro tocando); preview e export em estados separados.
    audio::AudioBlockCache* sharedAudio_ = nullptr;
    std::string (*audioResolve_)(void*, const std::string&) = nullptr;
    void* audioResolveCtx_ = nullptr;
    std::unique_ptr<audio::AudioBlockCache> ownAudio_;
    VideoSourceFactory* ownAudioFactory_ = nullptr;
    struct AudioVizState {
        std::shared_ptr<audio::MixState> mix;
        u64 lastFrame = 0;
    };
    std::unordered_map<u64, AudioVizState> audioViz_;
    std::unordered_map<u64, f32> audioPeaks_;   ///< faixa mais forte de cada espectro guardado
    // --- Mapa de profundidade (EffectResources::depth_map) ------------------
    // O serviço calcula uma vez por quadro-fonte; aqui ficam a textura R16F
    // 256×256 de cada quadro (como os espectros) e, por instância do efeito,
    // os limites suavizados no tempo (preview e export em estados separados).
    struct DepthState {
        u64 frameKey = 0;       ///< quadro-fonte que o estado já absorveu
        u64 asset = 0;
        i64 frame = 0;
        i64 sourceTimeUs = -1;
        f32 lo = 0.0f, hi = 1.0f;         ///< limites crus, suavizados
        f32 texLo = 0.0f, texHi = 1.0f;   ///< os mesmos, no espaço da textura de `frameKey`
        u64 lastFrame = 0;
    };
    std::unique_ptr<ai::DepthMapService> depth_;
    std::unique_ptr<ai::DepthMapService> foreground_;
    std::shared_ptr<ai::RotoService> roto_;   ///< shared_ptr: tipo incompleto aqui
    [[nodiscard]] DepthMapResult roto_map(const DepthMapRequest& request,
                                          const std::vector<ai::RotoStroke>& strokes) noexcept;
    std::string foregroundModelDirectory_;
    std::unordered_map<u64, LutTexture> depthTex_;     ///< por quadro-fonte
    /// Recortes refinados do Roto (R8/RGBA8 na resolução do quadro, até 8 MB
    /// cada em 1080p): poucos vivos — o do quadro e os vizinhos de agora.
    std::unordered_map<u64, LutTexture> rotoRefinedTex_;
    static constexpr usize kRotoRefinedTexturesKept = 3;
    std::unordered_map<u64, DepthState> depthState_;   ///< por (camada, efeito, export?)
    void release_depth(bool destroyTextures) noexcept;
    [[nodiscard]] DepthMapResult depth_map_preview() noexcept;
    /// Falso durante um quadro de prévia que não é a foto parada (ver depth_map_preview).
    bool previewDepthFromPhoto_ = true;
    /// Um quadro de prévia de efeito. O padrão é a prévia estática de sempre.
    struct PreviewFrameSpec {
        bool comparison = false;
        bool allowTemporal = false;  ///< sequência: temporal/global entram
        bool sourceOnly = false;     ///< devolve só a cena (captura da cartela/foto)
        bool forcePlate = false;     ///< ignora a foto: a cartela do shader
        const u8* source = nullptr;  ///< cena da CPU (RGBA8 sRGB, width×height); nula = foto/cartela
        const u8* history = nullptr; ///< a cena num instante passado (efeito com histórico)
        const u8* result = nullptr;  ///< resultado temporal pronto (o efeito não roda)
        i64 localTime = 0;
        f32 clipProgress = 0.0f;
        f32 sweep = std::numeric_limits<f32>::quiet_NaN();   ///< 0..1 da varredura; NaN = sem
        /// Os valores do preset no lugar dos de demonstração (miniatura de look).
        const EffectPreset* preset = nullptr;
    };
    [[nodiscard]] Status render_effect_preview_impl(const EffectRegistry& effects, EffectTypeId type, u32 width,
                                                    u32 height, const PreviewFrameSpec& spec,
                                                    std::vector<u8>& outRgba) noexcept;
    u32 collect_depth(u64 frameNumber, u64 idleFrames) noexcept;
    /// O `prepare` em curso: de onde vêm os pixels das imagens e se é export.
    const ImagePixels* (*planImageLookup_)(void*, AssetId) = nullptr;
    void* planImageCtx_ = nullptr;
    bool planFinal_ = false;
    bool planPlayback_ = false;      ///< prepare() em DecodeMode::Playback (tocando)
    bool planDeferLocalAi_ = false;
    u32 localAiPending_ = 0;
    /// A composição, o projeto, a mídia e o instante do `prepare` em curso
    /// (aninhado numa pré-composição, os dela). Só valem durante o prepare.
    const Composition* planComp_ = nullptr;
    const Project* planProject_ = nullptr;
    MediaManager* planMedia_ = nullptr;
    FrameIndex planTime_{0};
    /// Copiar fundo: a camada cujo plano do FUNDO (os efeitos depois do Copiar
    /// fundo) está sendo planejado agora. Como numa camada de ajuste, o plano
    /// é o da composição (Repetição no caminho, mapa de profundidade...).
    const Layer* compSpaceHost_ = nullptr;
    std::vector<PendingUpload> uploads_;
    std::unordered_map<u64, u64> textKeys_;   ///< chave sintética da camada de texto → chave dos pixels
    bool incomplete_ = false;   ///< o último quadro deixou camada de fora (recurso pendente)
    const char* incompleteFile_ = nullptr;
    u32 incompleteLine_ = 0;
    u32 exportGpuStep_ = 0;
    u32 exportBlurCap_ = 0;
    u32 exportBlurHardCap_ = 0;   ///< degrau da escada (0 = nenhum; 1 = sem desfoque)
    u32 exportSceneBlurCap_ = 0;  ///< amostras de CENA 3D no export (0 = nenhum)
    /// shutter_sample_count com o teto de amostras do export em GPU fraca.
    [[nodiscard]] u32 capped_shutter_samples(const MotionBlurSettings& settings, bool finalQuality, f32 quality,
            f64 pathPixels, bool changing = false, u32 resourceLimit = ShutterPlan::kHardLimit) const noexcept {
        // 0.2.1 (beta 0.2.0: "na prévia o motion blur 3D está normal, no
        // export sai outro"): o teto adaptativo do celular cortava o rastro
        // para 16 amostras (e até 2), enquanto a prévia usa uma amostra a
        // cada ~0,75 px da PRÉVIA (≈1,5 px do export, a prévia é meia
        // resolução). Rastro longo virava cópias separadas no arquivo. O teto
        // agora só tira o excesso acima da densidade da prévia, e nunca desce
        // abaixo das amostras escolhidas pela pessoa.
        if (exportBlurCap_ >= 2) {
            const u32 ceiling = std::clamp(settings.adaptiveLimit, 2u, ShutterPlan::kHardLimit);
            const u32 chosen = std::min(ceiling, std::clamp(settings.samples, 2u, 64u));
            const f64 previewDensity = std::isfinite(pathPixels)
                ? std::ceil(std::max(0.0, pathPixels) / 1.5) + 1.0 : static_cast<f64>(ceiling);
            const u32 floor = std::max(chosen, static_cast<u32>(std::min<f64>(previewDensity, ceiling)));
            resourceLimit = std::min(resourceLimit, std::max(exportBlurCap_, floor));
        }
        // Degrau da escada (0.2.7): teto RÍGIDO, acima do piso da prévia e das
        // amostras escolhidas — é a última chance de o export sair.
        if (finalQuality && exportBlurHardCap_ >= 1) resourceLimit = std::min(resourceLimit, exportBlurHardCap_);
        return shutter_sample_count(settings, finalQuality, quality, pathPixels, changing, resourceLimit);
    }
    void mark_incomplete(const char* file, u32 line) noexcept { incomplete_ = true; incompleteFile_ = file; incompleteLine_ = line; }
    bool videoSourceFailed_ = false; ///< principal source could not supply GPU pixels for this frame
    struct PreviewFrame { i64 time = -1; TextureHandle texture{}; bool complete = false; u64 used = 0, gpuFrame = 0; };
    std::vector<PreviewFrame> previewFrames_;
    u64 previewCacheBudget_ = 0, previewCacheKey_ = 0, previewCacheUse_ = 0;
    bool lowMemoryDevice_ = false;   ///< set_low_memory_device
    u64 previewCacheRevision_ = 0, previewCacheComposition_ = 0;
    u32 previewCacheCapacity_ = 0;
    bool previewCacheHit_ = false;
    void publish_preview_buffer_ranges() noexcept;
    mutable std::mutex previewRangesMutex_;
    std::array<i64, kPreviewCacheMaxFrames * 2> previewRangePairs_{};
    u32 previewRangeCount_ = 0;
    u64 previewRangeRevision_ = 0, previewRangeComposition_ = 0;
    std::vector<GpuTiming> timingScratch_;
    u32 lastGpuPasses_ = 0;

    const std::vector<scene3d::SceneFrame>* currentScenes_ = nullptr;
    std::span<const TextureHandle> boundedSceneTextures_{};
    std::span<const StagedSceneTarget> stagedSceneTargets_{};
    const FrameSnapshot* currentSnap_ = nullptr;   ///< dono das pré-composições da composição em curso
    u32 prepareDepth_ = 0;                          ///< aninhamento do prepare (guarda de recursão)
    u64 exposureBytesRemaining_ = 0;                ///< one frame budget shared by text, 3D and nested prepares
    u32 nestSalt_ = 0;                              ///< ≠ 0 dentro de uma pré-composição (ids únicos)
    // Exact instance paths, not a truncated hash: duplicated compositions can
    // run at different source times and must not share decoders/GPU uploads.
    std::vector<u64> renderInstancePath_;
    std::map<std::vector<u64>, u32> nestedNamespaces_;
    // O fundo de uma camada de ajuste em OUTRO instante (efeitos de tempo):
    // o próximo `prepare` desenha só as camadas da pilha em [from, limit)
    // (ou só as `only`, escopo 2) sobre fundo transparente se `bare`. Lidos
    // e zerados na entrada do prepare — a filha de uma pré-composição dentro
    // dele não herda o recorte.
    u32 backdropLimit_ = ~0u, backdropFrom_ = 0;
    std::vector<LayerId> backdropOnly_;
    bool backdropBare_ = false;
    u32 backdropBudget_ = 0;   ///< fundos re-renderizados restantes neste quadro (raiz)
    u32 compTargetW_ = 0, compTargetH_ = 0;
    u32 prewarmed_ = 0;
    // --- Pré-aquecimento preguiçoso (Fase 8I, §60–62) ------------------------
    // A abertura só compila o que todo projeto usa. Fora do playback, cada
    // quadro preparado varre o projeto (efeitos e 3D de TODAS as camadas, não
    // só as do instante) e junta o que falta; o render compila antes do grafo.
    void scan_for_warmup(const Project& project) noexcept;
    void warm_scene_pipelines(const FrameSnapshot& snapshot) noexcept;
    void flush_warmup() noexcept;
    std::vector<PipelineKey> warmPending_;
    std::vector<EffectTypeId> warmedEffects_;   ///< ordenado; tipos já pedidos
    u32 warmSceneQualities_ = 0;   ///< qualities used by all compositions, including future/nested layers
    u32 warmedSceneSamples_ = 0;  ///< actual GPU sample counts already queued
    u32 warmedForProject_ = 0;
    u32 framesRendered_ = 0;
    u64 frameNumber_ = 0;
    u64 renderFrameNumber_ = 0;   ///< quadro do backend em render() (buffers extras do Particular)
    bool lastZeroCopy_ = false;
    u64 videoPlaneUploadBytes_ = 0;
    f32 videoUploadMs_ = 0;
};

} // namespace aurea
