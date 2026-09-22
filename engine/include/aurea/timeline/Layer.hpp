// =============================================================================
//  Aurea / timeline / Layer.hpp
//
//  A camada. Uma só struct para todos os tipos — vídeo, texto, shape, null,
//  câmera, luz, modelo 3D, partículas, composição aninhada.
//
//  Por que não herança: a timeline precisa ordenar, mover, agrupar e duplicar
//  QUALQUER layer junto com as outras. Com hierarquia de classes, "mover 8
//  layers de tipos diferentes" viraria 8 caminhos de código, e cada tipo novo
//  obrigaria a revisitar todos. Com uma struct só, a operação é a mesma e o
//  campo que não se aplica fica no valor padrão.
//
//  O que é específico de tipo mora em `source` (AssetId) mais o bloco
//  `specific`, que é pequeno e POD. O que é universal — transform, tempo,
//  blend, tracks, efeitos, máscaras, parenting — mora direto na Layer.
//
//  Memória: uma Layer tem ~1 KB parada. 200 layers = 200 KB. O que cresce é o
//  TrackSet e as máscaras, e cresce só onde o usuário animou de verdade.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Handle.hpp"
#include "aurea/core/Math.hpp"
#include "aurea/animation/Curve.hpp"
#include "aurea/effects/Parameter.hpp"

#include <cmath>
#include <string>
#include <vector>

namespace aurea {

/// Transform de uma layer. Sempre guardado como valor estático aqui; quando
/// animado, o TrackSet sobrepõe. Manter os dois evita caso especial: uma layer
/// nunca animada lê direto daqui sem passar por avaliação.
struct Transform {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f};      ///< Euler ZYX, graus — como a UI mostra
    Vec3 anchor{0.0f, 0.0f, 0.0f};        ///< ponto de pivô, em espaço da layer
    f32  opacity = 1.0f;
    f32  skewX   = 0.0f;
    f32  skewY   = 0.0f;

    /// Motion blur por layer. Multiplicado pelo global da composição.
    f32  motionBlurAmount = 1.0f;
    bool motionBlurEnabled = false;

    /// Matriz local já resolvida para o frame atual. Preenchida pela avaliação;
    /// o renderer só lê.
    Mat4 localMatrix = Mat4::identity();

    [[nodiscard]] bool operator==(const Transform& o) const noexcept {
        return position == o.position && scale == o.scale && rotation == o.rotation
            && anchor == o.anchor && opacity == o.opacity
            && skewX == o.skewX && skewY == o.skewY;
    }
};

// Efeitos: a instância (`EffectInstance`) e os parâmetros genéricos vivem em
// effects/Parameter.hpp. A layer só guarda a lista, na ordem de aplicação.

/// Máscara. Os pontos são animáveis: o path vive aqui, a animação nos tracks.
struct MaskPoint {
    Vec2 position{0.0f, 0.0f};
    /// Tangentes de bezier, relativas à posição.
    Vec2 inTangent{0.0f, 0.0f};
    Vec2 outTangent{0.0f, 0.0f};
};

struct Mask {
    /// Id local à layer, estável enquanto a máscara existir. Mesma razão do
    /// Effect::id: a UI fala de "esta máscara", não de "a terceira do vetor".
    u32           id = kInvalidIndex;
    std::string   name;
    MaskOperation operation = MaskOperation::Add;
    bool          inverted = false;
    f32           feather  = 0.0f;     ///< em pixels de composição
    f32           expansion = 0.0f;    ///< dilata/erode o path
    f32           opacity  = 1.0f;
    bool          closed   = true;

    std::vector<MaskPoint> points;

    /// Quantos pontos o preview pode usar. Máscaras com centenas de pontos
    /// entram no modo adaptativo com uma versão simplificada.
    u32           previewPointLimit = 0;   ///< 0 = sem limite

    /// Bitmap da máscara em cache, com a chave do estado que o gerou. Enquanto
    /// a chave bate, não há re-rasterização.
    u64           cacheKey = 0;
};

/// Blocos de dado específico de tipo. Mantidos pequenos e POD.
struct TextData {
    std::string content  = "Texto";
    FontId      font{};
    f32         size     = 72.0f;
    Vec4        color{1.0f, 1.0f, 1.0f, 1.0f};
    f32         strokeWidth = 0.0f;
    Vec4        strokeColor{0.0f, 0.0f, 0.0f, 1.0f};
    u32         alignment = 0;         ///< 0 esquerda, 1 centro, 2 direita
    f32         lineHeight = 1.2f;
    f32         tracking   = 0.0f;
    bool        rtl        = false;
    bool        autoSize   = true;
    Rect        box{0.0f, 0.0f, 800.0f, 200.0f};   ///< quando autoSize == false

    /// Text Animator: seletores (char/word/line) e animadores por seletor.
    /// O conteúdo é resolvido no shape de texto, não aqui.
    u32         animatorCount = 0;
};

struct ShapeData {
    /// 0 retângulo (cantos arredondados), 1 elipse, 2 caminho, 3 polígono
    /// regular, 4 estrela, 5 cruz, 6 anel, 7 fatia, 8 flor, 9 seta,
    /// 10 triângulo retângulo (shaders/shape/shape.frag).
    u32  shapeType = 0;
    Rect bounds{0.0f, 0.0f, 200.0f, 200.0f};
    f32  cornerRadius = 0.0f;
    f32  points = 5.0f;        ///< estrela/polígono
    f32  innerRadius = 0.5f;
    Vec4 fillColor{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 strokeColor{0.0f, 0.0f, 0.0f, 0.0f};
    f32  strokeWidth = 0.0f;
    bool filled = true;
    /// Trim path: fração do comprimento desenhada.
    f32  trimStart = 0.0f;
    f32  trimEnd   = 1.0f;
    f32  trimOffset = 0.0f;
    bool trimEnabled = false;
    /// Pontos livres para shapeType == path.
    std::vector<Vec2> path;
};

struct CameraData {
    f32 fov           = 50.0f;      ///< graus
    f32 focalLength   = 35.0f;      ///< mm — sincronizado com fov
    f32 nearPlane     = 1.0f;
    f32 farPlane      = 10000.0f;
    f32 focusDistance = 1000.0f;
    f32 aperture      = 2.8f;       ///< f-stop
    /// Câmera ativa da composição. Só uma por vez.
    bool active = true;
};

enum class LightKind : u8 { Directional = 0, Point, Spot, Ambient };

struct LightData {
    LightKind kind = LightKind::Directional;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    f32  intensity  = 1.0f;
    f32  range      = 1000.0f;
    f32  coneAngle  = 45.0f;
    f32  penumbra   = 0.2f;
    bool castShadows = false;
    f32  shadowBias = 0.001f;
};

struct Model3DData {
    AssetId scene{};
    /// Metros do modelo → pixels da composição, e o centro da caixa do modelo
    /// (pivô). Definidos no import para o modelo APARECER enquadrado; a escala
    /// da layer continua 100% para o usuário.
    f32     unitScale = 1.0f;
    Vec3    pivot{0.0f, 0.0f, 0.0f};
    i32     animationClip = -1;
    f32     timeScale = 1.0f;
    bool    castShadows = true;
    bool    receiveShadows = true;
    /// Índices de LOD forçado, ou -1 para automático por tamanho na tela.
    i32     forcedLod = -1;
};

struct ParticleData {
    u32  emitterType = 0;
    f32  rate = 100.0f;
    f32  lifetime = 2.0f;
    Vec3 gravity{0.0f, -980.0f, 0.0f};
    f32  startSize = 20.0f, endSize = 0.0f;
    f32  startOpacity = 1.0f, endOpacity = 0.0f;
    f32  speed = 200.0f;
    f32  spread = 45.0f;
    u32  maxParticles = 10000;
    u32  blendMode = 1;          ///< aditivo por padrão
    bool collideEnvironment = false;
    // v7: aparência e emissor (partículas analíticas na GPU)
    Vec4 startColor{1.0f, 0.85f, 0.45f, 1.0f};   ///< sRGB, reta
    Vec4 endColor{1.0f, 0.35f, 0.10f, 1.0f};
    f32  direction = -90.0f;     ///< graus; −90 = para cima (y da tela para baixo)
    u32  seed = 1;
    Vec2 emitterSize{20.0f, 20.0f};
    Vec2 emitterOffset{0.0f, 0.0f};   ///< do centro da camada (px)
};

struct CompositionRef {
    CompositionId composition{};
    bool          collapsed = false;   ///< exibição colapsada na timeline
};

struct Layer {
    LayerKind kind = LayerKind::Unknown;
    std::string name;

    // --- Tempo ---------------------------------------------------------------
    FrameIndex start{0};
    FrameIndex end{0};
    FrameIndex offset{0};      ///< deslocamento do conteúdo dentro do tempo da layer

    /// Time remap: curva própria, separada dos tracks de transform, porque a
    /// avaliação dela acontece ANTES de tudo (decide qual frame decodificar).
    Track timeRemap;
    bool  timeRemapEnabled = false;

    /// Velocidade do conteúdo (quadros da fonte por quadro da timeline).
    /// 0 = quadro congelado. `reversed` toca do ponto de saída para o de
    /// entrada. `offset` continua sendo o ponto de entrada na fonte.
    f32   speed = 1.0f;
    bool  reversed = false;
    /// Desfoque de movimento desta camada (a composição define o obturador).
    bool  motionBlur = false;
    /// Vídeo fora da grade da fonte (câmera lenta, velocidade quebrada):
    /// 0 = quadro mais próximo (repete), 1 = mistura dos dois quadros vizinhos
    /// da fonte pelo tempo entre eles.
    u8    frameBlend = 0;
    /// Transições de entrada/saída (avaliadas no render, não viram keyframes):
    /// 0 nenhuma, 1 dissolver, 2 deslizar para cima, 3 deslizar da esquerda,
    /// 4 zoom, 5 girar. Duração em quadros.
    u8    transitionIn = 0, transitionOut = 0;
    u32   transitionInFrames = 0, transitionOutFrames = 0;
    /// Eco (rastro do movimento, operador "somar"): cópias em t − i·atraso com
    /// peso queda^i. RGB no tempo: vermelho em t, verde em t − d, azul em t − 2d.
    u32   echoCount = 0;
    f32   echoDelay = 2.0f;      ///< quadros
    f32   echoDecay = 0.6f;
    f32   rgbDelay = 0.0f;       ///< quadros (0 = desligado)

    // --- Hierarquia e composição --------------------------------------------
    LayerId  parent{};
    u32      zOrder = 0;       ///< posição vertical; maior = na frente
    BlendMode blendMode = BlendMode::Normal;
    bool     visible = true;
    bool     locked  = false;
    bool     solo    = false;
    bool     threeD  = false;  ///< participa da cena 3D da composição

    // --- Conteúdo ------------------------------------------------------------
    AssetId source{};              ///< vídeo, imagem, áudio ou modelo
    CompositionRef nested{};       ///< quando kind == Composition

    // --- Universal -----------------------------------------------------------
    Transform transform;
    TrackSet  tracks;

    std::vector<EffectInstance> effects;
    std::vector<Mask>   masks;

    // --- Específico de tipo --------------------------------------------------
    TextData      text;
    ShapeData     shape;
    CameraData    camera;
    LightData     light;
    Model3DData   model;
    ParticleData  particles;

    // --- Áudio (também presente em layers de vídeo com trilha) ---------------
    f32  gain     = 1.0f;
    f32  pan      = 0.0f;
    bool muted    = false;
    FrameIndex fadeIn{0};
    FrameIndex fadeOut{0};

    /// Chave de cache desta layer no frame atual: combina transform avaliado,
    /// efeitos ativos e fonte. Se não muda de um frame para o outro, o
    /// compositor reaproveita o resultado anterior em vez de recompor.
    u64  cacheKey = 0;

    /// Índice estável de desenho, preenchido pelo avaliador. Ordena o desenho
    /// sem depender do zOrder (que pode estar desatualizado durante um arrasto).
    u32  drawIndex = 0;

    // --- Consultas -----------------------------------------------------------

    [[nodiscard]] bool contains_time(FrameIndex t) const noexcept {
        return t >= start && t < end;
    }
    [[nodiscard]] FrameIndex duration() const noexcept { return FrameIndex{end.value - start.value}; }
    [[nodiscard]] bool animated() const noexcept { return tracks.has_animation() || timeRemapEnabled; }

    /// Instante da FONTE (em quadros da composição, fracionário) que toca no
    /// frame `timelineTime` da timeline: ponto de entrada + tempo decorrido ×
    /// velocidade (de trás para a frente se `reversed`; parado se velocidade
    /// 0). É a ÚNICA função de tempo da fonte — vídeo, miniatura e áudio usam
    /// esta mesma conta.
    [[nodiscard]] f64 source_frame(FrameIndex timelineTime) const noexcept {
        return source_frame_f(static_cast<f64>(timelineTime.value));
    }

    /// `source_frame` num instante fracionário da timeline (áudio amostra a
    /// amostra). Com o remapeamento ligado, a curva manda: o VALOR da trilha é
    /// o quadro da fonte (entre quadros, interpolado linearmente).
    [[nodiscard]] f64 source_frame_f(f64 timelineTime) const noexcept {
        if (timeRemapEnabled && !timeRemap.keys.empty()) {
            const f64 local = timelineTime - static_cast<f64>(start.value) + static_cast<f64>(offset.value);
            const f64 f = std::floor(local);
            const f64 a = timeRemap.sample(FrameIndex{static_cast<i64>(f)});
            const f64 b = timeRemap.sample(FrameIndex{static_cast<i64>(f) + 1});
            return a + (b - a) * (local - f);
        }
        const f64 elapsed = reversed ? static_cast<f64>(end.value - 1) - timelineTime
                                     : timelineTime - static_cast<f64>(start.value);
        return static_cast<f64>(offset.value) + elapsed * static_cast<f64>(speed);
    }

    /// Tempo dentro da layer (0 = primeiro frame dela).
    [[nodiscard]] FrameIndex local_time(FrameIndex timelineTime) const noexcept {
        return FrameIndex{timelineTime.value - start.value + offset.value};
    }
    [[nodiscard]] FrameIndex timeline_time(FrameIndex localTime) const noexcept {
        return FrameIndex{localTime.value + start.value - offset.value};
    }

    [[nodiscard]] Mask* find_mask(MaskId id) noexcept {
        for (auto& m : masks) if (m.id == id.index) return &m;
        return nullptr;
    }
    [[nodiscard]] EffectInstance* find_effect(EffectId id) noexcept {
        for (auto& e : effects) if (e.id == id.index) return &e;
        return nullptr;
    }
    [[nodiscard]] u32 effect_index(EffectId id) const noexcept {
        for (u32 i = 0; i < effects.size(); ++i) if (effects[i].id == id.index) return i;
        return kInvalidIndex;
    }

    /// Próximo id local livre. Contadores ficam na layer, não num global, para
    /// que duplicar uma layer produza ids determinísticos e o projeto continue
    /// comparável byte a byte.
    u32 nextEffectId = 0;
    u32 nextMaskId   = 0;

    [[nodiscard]] u32 alloc_effect_id() noexcept { return nextEffectId++; }
    [[nodiscard]] u32 alloc_mask_id() noexcept { return nextMaskId++; }
};

} // namespace aurea
