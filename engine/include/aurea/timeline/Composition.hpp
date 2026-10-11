// =============================================================================
//  Aurea / timeline / Composition.hpp
//
//  Uma composição: um palco com tamanho, taxa e uma pilha de layers.
//
//  Tudo que é "um vídeo editável" é uma composição — inclusive uma composição
//  aninhada dentro de outra. Não existe tipo separado para "pre-comp": é a
//  MESMA estrutura, referenciada por um LayerId de tipo Composition. Isso faz
//  a recursão ser natural e o renderer não precisar de caso especial.
//
//  A ordem vertical das layers é explícita em `order` (um vetor de ids). Não é
//  derivada do zOrder nem da ordem de criação: arrastar uma layer para cima de
//  outra precisa ser uma operação O(1) de reordenação que não invalida handle
//  nenhum.
// =============================================================================
#pragma once

#include "aurea/timeline/Layer.hpp"
#include "aurea/core/Handle.hpp"

#include <memory>
#include <string>
#include <vector>

namespace aurea {

/// Configuração de sombras da composição. Separada porque é por cena, não por
/// luz: uma cena não precisa de dois orçamentos de shadow map.
struct ShadowSettings {
    bool  enabled = false;
    u32   mapResolution = 1024;
    u32   cascadeCount  = 1;      ///< preview mobile começa com 1
    f32   cascadeSplitLambda = 0.9f;
    f32   bias = 0.0015f;
    f32   normalBias = 0.02f;
    bool  softShadows = false;
    u32   pcfSamples  = 4;
};

/// Configuração de ambiente / IBL.
struct EnvironmentSettings {
    AssetId hdri{};
    f32     intensity = 1.0f;
    f32     rotation  = 0.0f;
    Color   ambientColor{0.05f, 0.05f, 0.06f, 1.0f};
    bool    showBackground = false;
    f32     backgroundBlur = 0.0f;
    /// v32: ambiente de estúdio procedural (StudioPreset: 0 nenhum, 1 estúdio
    /// escuro, 2 estúdio de produto, 3 céu e sol). Vale quando não há HDRI.
    u32     studioPreset = 0;
    /// Half-open interval of the visible panorama, in composition frames.
    /// -1 keeps the historical whole-composition end; lighting stays independent.
    FrameIndex backgroundStart{0};
    FrameIndex backgroundEnd{-1};
    [[nodiscard]] bool background_at(FrameIndex time) const noexcept {
        return showBackground && time >= backgroundStart && (backgroundEnd.value < 0 || time < backgroundEnd);
    }
};

/// v32: chão do grupo 3D (plano horizontal no ponto mais baixo dos modelos;
/// "para cima" no mundo é −Y). Modo 0 = sem chão, 1 = chão visível (PBR
/// dielétrico com IBL, sombra e reflexo planar), 2 = só sombra/reflexo
/// ("shadow catcher": transparente fora da sombra e do reflexo — compõe
/// sobre um fundo 2D).
struct FloorSettings {
    u32   mode = 0;
    Color color{0.5f, 0.5f, 0.5f, 1.0f};   ///< linear
    f32   roughness = 0.35f;               ///< 0..1: desfoque do reflexo
    f32   reflectivity = 0.5f;             ///< 0..1 (0 = sem passe de reflexo)
    f32   contactShadow = 0.8f;            ///< força da sombra de contato (0..1)
    f32   fade = 6.0f;                     ///< raio do desbotamento (× raio dos modelos)
};

/// Grafo de pós-processamento 3D habilitado na composição.
struct PostProcessSettings {
    bool ssao      = false;  f32 ssaoRadius = 0.5f;   f32 ssaoIntensity = 1.0f;
    /// Bloom HDR do grupo 3D. Padrão: limiar 1,25 (joelho suave de 20%) e
    /// soma de 6% — só a luz HDR de verdade (farol, emissivo, reflexo do sol)
    /// espalha; a cor difusa perto de 1 fica limpa. Limiar 0 = espalhamento
    /// conservador de tudo (~4%, estilo COD/Unreal). Projeto novo nasce com o
    /// bloom ligado; projeto antigo mantém o que gravou.
    bool bloom     = true;   f32 bloomThreshold = 1.25f; f32 bloomIntensity = 0.6f;
    bool dof       = false;  f32 dofAperture = 2.8f;
    bool fog       = false;  Vec4 fogColor{0.5f, 0.5f, 0.55f, 1.0f}; f32 fogDensity = 0.001f;
    bool vignette  = false;  f32 vignetteAmount = 0.3f;
    bool colorGrade = false;
    /// Índice do LUT aplicado (0 = nenhum).
    u32  lutIndex  = 0;
    // --- v31 -----------------------------------------------------------------
    /// Qualidade do 3D (AA + pós): `Scene3DQuality` (0 AUTO, 1 BAIXO, 2 MÉDIO,
    /// 3 ALTO, 4 ULTRA). O export nunca fica abaixo do ALTO.
    u32  quality3d = 0;
    /// Tone map do grupo 3D: 0 = Khronos PBR Neutral (cores de catálogo),
    /// 1 = AgX, 2 = AgX Punchy, 3 = AgX Golden, 4 = Uchimura, 5 = ACES SDR.
    u32  toneMapper = 0;
    /// Exposição do grupo 3D (multiplicador linear da luz da cena; 1 = neutro).
    /// Vale para modelos e céu; o 2D dentro da cena não muda.
    f32  exposure = 1.0f;
};

/// Motion blur da composição (o global; cada layer multiplica por seu próprio).
struct MotionBlurSettings {
    bool enabled = false;
    u32  samples = 16;        ///< minimum samples while moving (export)
    u32  previewSamples = 16; ///< preview (adaptive budget can reduce this)
    f32  shutterAngle = 180.0f;
    bool vectorBlur = false;  ///< blur baseado em vetores de movimento
    f32  shutterPhase = -90.0f; ///< exposure starts at frame + phase / 360
    u32  adaptiveLimit = 128;  ///< bounded adaptive sample ceiling (export)
};

/// Marca na régua da composição. `kind` 0 = marca da pessoa, 1 = batida
/// detectada (substituída em bloco quando a detecção roda de novo).
struct Marker {
    FrameIndex  frame{0};
    u32         color = 0xFFF7C34Fu;   ///< RGBA8 sRGB (r no byte baixo)
    u32         kind = 0;
    std::string label;
};
inline constexpr u32 kMarkerManual = 0;
inline constexpr u32 kMarkerBeat = 1;

/// Faixa de taxa da composição. "fps livre" na prática: qualquer valor real
/// nesta faixa (23,976, 29,97, 144…). O teto é o que os encoders de vídeo dos
/// aparelhos aceitam; o piso evita quadros de mais de um segundo.
inline constexpr f64 kMinCompositionFps = 1.0;
inline constexpr f64 kMaxCompositionFps = 240.0;

/// A taxa como o motor guarda: finita, dentro da faixa e, perto de uma taxa
/// NTSC (n·1000/1001: 23,976, 29,97, 47,952, 59,94, 119,88, 239,76), a razão
/// EXATA. As telas digitam "29,97" e o Android passa `float`; sem o encaixe,
/// 29,97 ≠ 30000/1001 e um vídeo de 29,97 importado andaria meio quadro por
/// minuto em relação à composição.
[[nodiscard]] inline f64 normalize_fps(f64 fps) noexcept {
    if (!(fps > 0.0) || fps != fps || fps > 1e9) return 30.0;
    for (const f64 n : {24.0, 30.0, 48.0, 60.0, 120.0, 240.0}) {
        const f64 ntsc = n * 1000.0 / 1001.0;
        const f64 d = fps - ntsc;
        if (d < 0.0015 && d > -0.0015) return ntsc;
    }
    const f64 whole = static_cast<f64>(static_cast<i64>(fps + 0.5));
    if (fps - whole < 1e-4 && whole - fps < 1e-4) fps = whole;
    return fps < kMinCompositionFps ? kMinCompositionFps : fps > kMaxCompositionFps ? kMaxCompositionFps : fps;
}

class Composition {
public:
    using LayerTable = SlotTable<Layer, LayerTag>;

    explicit Composition(std::string name = "Composição") : name_(std::move(name)) {}

    // --- Identidade -----------------------------------------------------------
    [[nodiscard]] CompositionId id() const noexcept { return id_; }
    void set_id(CompositionId id) noexcept { id_ = id; }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    void set_name(std::string n) { name_ = std::move(n); }

    // --- Formato --------------------------------------------------------------
    [[nodiscard]] u32 width()  const noexcept { return width_; }
    [[nodiscard]] u32 height() const noexcept { return height_; }
    [[nodiscard]] f64 fps()    const noexcept { return fps_; }

    void set_size(u32 w, u32 h) noexcept {
        width_  = w ? w : 1;
        height_ = h ? h : 1;
        ++formatRevision_;
    }
    /// Qualquer taxa real; passa por `normalize_fps` (faixa + razão NTSC exata).
    void set_fps(f64 fps) noexcept { fps_ = normalize_fps(fps); ++formatRevision_; }

    /// Resize the editing canvas and fit its existing content, keeping aspect
    /// ratio, hierarchy and animated transforms. set_size remains the raw loader API.
    void resize_content(u32 w, u32 h) noexcept;

    /// Troca a taxa PRESERVANDO os segundos: todo tempo guardado em frames
    /// (duração, início/fim/offset e fades das layers, keyframes e o remap)
    /// é reescalado. Sem isto, 30 → 60 fps deixaria o vídeo tocando na
    /// velocidade certa mas cortado na metade, porque o tempo da fonte é
    /// `frames locais / fps`.
    void retime(f64 fps) noexcept;

    [[nodiscard]] f32 aspect() const noexcept {
        return static_cast<f32>(width_) / static_cast<f32>(height_);
    }

    /// Duração em frames. Sempre >= 1.
    [[nodiscard]] FrameIndex duration() const noexcept { return duration_; }
    /// End-exclusive export range starting at frame zero. Content mode keeps
    /// leading gaps and audio, but drops unused composition tail after clips.
    [[nodiscard]] FrameIndex export_duration(bool trimToContent) const noexcept;
    void set_duration(FrameIndex d) noexcept {
        duration_ = FrameIndex{d.value > 0 ? d.value : 1};
    }

    /// Duração em segundos — só para exibição na UI.
    [[nodiscard]] f64 duration_seconds() const noexcept {
        return static_cast<f64>(duration_.value) / fps_;
    }

    /// Cor de fundo em sRGB (o valor que a pessoa escolheu e vê), alfa reto.
    /// O renderer lineariza ao compor — ao contrário dos parâmetros de cor dos
    /// efeitos, que já são lineares.
    [[nodiscard]] Color background() const noexcept { return background_; }
    void set_background(Color c) noexcept { background_ = c; }

    [[nodiscard]] bool transparent_background() const noexcept { return transparent_; }
    void set_transparent_background(bool t) noexcept { transparent_ = t; }

    /// O fundo como as duas telas o escolhem (Ajustes do projeto e "Novo
    /// projeto"): alfa 0 = "Transparente" (sem fundo; o PNG, a sequência PNG e
    /// o GIF saem com alfa, o MP4 sai sobre preto); qualquer outro alfa = cor
    /// opaca. A cor RGB fica guardada mesmo transparente, para voltar a ela.
    void set_background_choice(Color c) noexcept {
        transparent_ = !(c.a > 0.5f);
        background_ = Color{c.r, c.g, c.b, 1.0f};
    }
    /// O inverso de `set_background_choice`: RGBA com alfa 0 quando transparente.
    [[nodiscard]] Color background_choice() const noexcept {
        return Color{background_.r, background_.g, background_.b, transparent_ ? 0.0f : 1.0f};
    }

    // --- Layers ---------------------------------------------------------------

    [[nodiscard]] LayerTable& layers() noexcept { return layers_; }
    [[nodiscard]] const LayerTable& layers() const noexcept { return layers_; }

    [[nodiscard]] LayerId add_layer(LayerKind kind, std::string name);
    [[nodiscard]] LayerId duplicate_layer(LayerId source, FrameIndex atTime);
    bool remove_layer(LayerId id) noexcept;

    [[nodiscard]] Layer* layer(LayerId id) noexcept { return layers_.get(id); }
    [[nodiscard]] const Layer* layer(LayerId id) const noexcept { return layers_.get(id); }

    /// Move a layer para a posição vertical `targetIndex` (0 = fundo).
    bool reorder_layer(LayerId id, u32 targetIndex) noexcept;

    /// Ordem vertical, do fundo para a frente. O renderer desenha nesta ordem.
    [[nodiscard]] const OrderedIds<LayerId>& order() const noexcept { return order_; }
    [[nodiscard]] OrderedIds<LayerId>& order() noexcept { return order_; }

    /// Índice vertical da layer, ou -1.
    [[nodiscard]] i32 z_index_of(LayerId id) const noexcept { return order_.index_of(id); }

    /// Recalcula `zOrder` e `drawIndex` de todas as layers a partir de `order_`.
    /// Chamado depois de qualquer reordenação, antes do próximo frame.
    void rebuild_draw_order() noexcept;

    /// Layers visíveis que cobrem `t`, já em ordem de desenho. Preenche
    /// `out` e devolve quantas. Não aloca se `out` tiver capacidade.
    u32 collect_active(FrameIndex t, std::vector<LayerId>& out) const;

    // --- Câmera e 3D ----------------------------------------------------------
    [[nodiscard]] LayerId active_camera() const noexcept { return activeCamera_; }
    void set_active_camera(LayerId id) noexcept { activeCamera_ = id; }

    [[nodiscard]] ShadowSettings& shadows() noexcept { return shadows_; }
    [[nodiscard]] const ShadowSettings& shadows() const noexcept { return shadows_; }

    [[nodiscard]] EnvironmentSettings& environment() noexcept { return environment_; }
    [[nodiscard]] const EnvironmentSettings& environment() const noexcept { return environment_; }

    [[nodiscard]] FloorSettings& floor() noexcept { return floor_; }
    [[nodiscard]] const FloorSettings& floor() const noexcept { return floor_; }
    [[nodiscard]] PostProcessSettings& post_process() noexcept { return postProcess_; }
    [[nodiscard]] const PostProcessSettings& post_process() const noexcept { return postProcess_; }

    [[nodiscard]] MotionBlurSettings& motion_blur() noexcept { return motionBlur_; }
    [[nodiscard]] const MotionBlurSettings& motion_blur() const noexcept { return motionBlur_; }

    // --- Modo da timeline --------------------------------------------------------
    /// Modo Edição (ímã): aparar empurra/puxa as camadas seguintes e excluir
    /// fecha o buraco. Modo Composição (padrão): tudo livre, camada por camada.
    [[nodiscard]] bool edit_mode() const noexcept { return editMode_; }
    void set_edit_mode(bool on) noexcept { editMode_ = on; }

    /// Desloca no tempo toda camada que começa em `from` ou depois (menos
    /// `except`) e as marcas a partir de `from`. `delta` < 0 puxa para trás.
    void shift_from(FrameIndex from, i64 delta, LayerId except = LayerId{}) noexcept;

    /// Fecha os intervalos vazios (sem nenhuma camada) dentro de [from, to):
    /// quem vem depois de cada buraco anda para trás o tamanho dele. Devolve o
    /// total de frames removidos.
    i64 close_gaps(FrameIndex from, FrameIndex to) noexcept;

    // --- Ripple por LINHA (modo Edição de vídeo) ------------------------------
    /// Uma LINHA é o `zOrder`: os trechos que a pessoa vê lado a lado na mesma
    /// faixa da timeline. O ripple de edição de vídeo anda só dentro dela —
    /// texto, imagem e overlay em outras linhas ficam parados no tempo, que é
    /// o contrário do `shift_from`, que empurra a composição inteira.

    /// Desloca no tempo os trechos DA LINHA `track` que começam em `from` ou
    /// depois (menos `except`). Não toca em marcas nem em outras linhas.
    void shift_track(FrameIndex from, i64 delta, u32 track, LayerId except = LayerId{}) noexcept;

    /// A linha de um trecho (`Layer::trackId`). `kInvalidIndex` se ele não existe.
    [[nodiscard]] u32 track_of(LayerId id) const noexcept;

    /// Um número de LINHA que nenhuma camada desta composição usa. Quem traz
    /// camadas de outra composição (colar, desagrupar, tirar do grupo) troca a
    /// linha delas por uma destas — senão o número de lá cairia numa linha que
    /// já existe aqui e camadas sem relação passariam a andar juntas.
    [[nodiscard]] u32 new_track_id() noexcept;

    /// Fecha os buracos DENTRO DE UMA LINHA em [from, to). Os vizinhos de
    /// faixa andam para trás; o resto da composição não. Devolve os frames
    /// removidos.
    i64 close_gaps_in_track(FrameIndex from, FrameIndex to, u32 track) noexcept;

    // --- Marcas ----------------------------------------------------------------
    /// Sempre em ordem de frame; no máximo uma marca por frame.
    [[nodiscard]] const std::vector<Marker>& markers() const noexcept { return markers_; }
    /// Insere (ou troca a do mesmo frame). Devolve o índice.
    u32 put_marker(Marker m);
    bool remove_marker_at(FrameIndex f) noexcept;
    /// Remove todas as marcas do tipo dentro de [from, to).
    u32 remove_markers(u32 kind, FrameIndex from, FrameIndex to) noexcept;

    [[nodiscard]] Scene3DId scene() const noexcept { return scene_; }
    void set_scene(Scene3DId s) noexcept { scene_ = s; }

    // --- Revisões -------------------------------------------------------------
    //
    //  Contador que muda a cada alteração estrutural. É a chave grossa de
    //  invalidação de cache: enquanto não muda, nada precisa ser recompilado
    //  (grafo de efeitos, lista de draws, ordem). Alterar só um valor de
    //  keyframe NÃO muda a revisão — o cache de frame já cobre isso com a
    //  chave fina.
    [[nodiscard]] u64 revision() const noexcept { return revision_; }
    void touch() noexcept { ++revision_; }

    /// Revisão só do formato (tamanho/taxa). Separada porque mudar o tamanho
    /// invalida todos os render targets; mudar uma layer, não.
    [[nodiscard]] u64 format_revision() const noexcept { return formatRevision_; }

    /// Profundidade de aninhamento. Uma composição que se referencia direta ou
    /// indiretamente é rejeitada — sem isto, o renderer entraria em recursão
    /// infinita e derrubaria o app.
    [[nodiscard]] u32 nesting_depth() const noexcept { return nestingDepth_; }
    void set_nesting_depth(u32 d) noexcept { nestingDepth_ = d; }

    /// Checagem de ciclo antes de permitir aninhar `candidate`.
    [[nodiscard]] bool can_nest(const Composition& candidate) const noexcept;

    /// Cópia profunda para o histórico (ids preservados).
    [[nodiscard]] std::unique_ptr<Composition> clone() const;
    /// Volta ao estado de `snapshot`. As revisões AVANÇAM (nunca voltam): os
    /// caches que dependem delas precisam ver a mudança.
    void restore_from(const Composition& snapshot);

private:
    CompositionId id_{};
    std::string   name_;

    u32 width_  = 1920;
    u32 height_ = 1080;
    f64 fps_    = 60.0;
    FrameIndex duration_{600};
    Color background_{0.0f, 0.0f, 0.0f, 1.0f};
    bool  transparent_ = false;

    LayerTable          layers_;
    OrderedIds<LayerId> order_;
    LayerId             activeCamera_{};

    ShadowSettings      shadows_{};
    EnvironmentSettings environment_{};
    PostProcessSettings postProcess_{};
    FloorSettings floor_{};
    MotionBlurSettings  motionBlur_{};
    Scene3DId           scene_{};
    std::vector<Marker> markers_;
    bool                editMode_ = false;
    /// Próxima LINHA livre. Cresce e nunca é reaproveitada: um trecho que
    /// ficou sozinho na linha dele não passa a dividi-la com um trecho novo.
    u32                 nextTrackId_ = 1;

    u64 revision_       = 1;
    u64 formatRevision_ = 1;
    u32 nestingDepth_   = 0;
};

} // namespace aurea
