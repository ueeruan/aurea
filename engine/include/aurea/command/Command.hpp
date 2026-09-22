// =============================================================================
//  Aurea / command / Command.hpp
//
//  Command queue — a ÚNICA forma de a UI mexer no motor.
//
//  O problema que isto resolve: a UI roda a 60/90/120 Hz e mexe em dezenas de
//  propriedades por frame durante um arrasto. Se cada `setX()` atravessasse a
//  bridge (JNI / ObjC++) individualmente, seriam centenas de chamadas nativas
//  por frame, cada uma com custo fixo de marshalling, cada uma pegando um lock.
//
//  A solução: a UI escreve comandos num bloco contíguo e envia o bloco. Uma
//  chamada de fronteira por frame, independente de quantas layers o usuário
//  está mexendo. O motor drena o bloco na thread de render.
//
//  Consequência de projeto: comandos são POD. Sem ponteiros, sem std::string,
//  sem alocação. Isso permite validar o bloco inteiro antes de aplicar (um
//  comando inválido no meio não deixa o projeto em estado meio-aplicado) e
//  permite gravar o bloco como undo/redo sem tradução.
// =============================================================================
#pragma once

#include "aurea/core/Types.hpp"
#include "aurea/core/Handle.hpp"
#include "aurea/core/Math.hpp"
#include "aurea/core/Result.hpp"
#include "aurea/bridge/BridgePods.hpp"

#include <cstddef>
#include <cstring>
#include <type_traits>

namespace aurea {

enum class CommandType : u16 {
    Nop = 0,

    // --- Timeline -----------------------------------------------------------
    LayerCreate,
    LayerDelete,
    LayerDuplicate,
    LayerReorder,          ///< muda a ordem vertical (z-order)
    LayerSetKind,
    LayerSetName,
    LayerSetTimeRange,     ///< trim: move início/fim no tempo
    LayerSplit,            ///< divide em duas no playhead
    LayerSetVisible,
    LayerSetLocked,
    LayerSetParent,        ///< parenting / null object
    LayerSetBlendMode,
    LayerSetComposition,   ///< qual composição a layer referencia (pre-comp)

    // --- Transform ----------------------------------------------------------
    LayerSetTransform,     ///< x,y,z,scale,rot,anchor,opacity,skew — tudo de uma vez
    LayerSetAnchor,
    LayerSetOpacity,
    LayerSetSkew,
    LayerSetScale,
    LayerSetRotation,
    LayerSetPosition,

    // --- Keyframes ----------------------------------------------------------
    KeyframeInsert,
    KeyframeDelete,
    KeyframeMove,          ///< muda o tempo
    KeyframeSetValue,
    KeyframeSetInterpolation,
    KeyframeSetBezier,
    KeyframeSetEasing,

    // --- Máscaras -----------------------------------------------------------
    MaskCreate,
    MaskDelete,
    MaskSetOperation,
    MaskSetFeather,
    MaskSetExpansion,
    MaskSetOpacity,
    /// Move um ponto da máscara. A UI emite um destes por ponto arrastado —
    /// um arrasto de 3 pontos são 3 comandos, não um por frame por ponto.
    MaskSetPath,
    /// Fecha a edição do caminho: aplica o número de pontos e o flag de
    /// fechado. Sem este comando a máscara ficaria num estado intermediário em
    /// que o número de pontos não bate com o que o usuário desenhou.
    MaskSetPathCommit,

    // --- Efeitos ------------------------------------------------------------
    EffectAdd,
    EffectRemove,
    EffectReorder,
    EffectSetEnabled,
    EffectSetParam,        ///< paramIndex + valor
    EffectSetColorParam,

    // --- Áudio --------------------------------------------------------------
    AudioSetGain,
    AudioSetMuted,
    AudioSetSolo,
    AudioSetFadeIn,
    AudioSetFadeOut,

    // --- Texto --------------------------------------------------------------
    TextSetContent,        ///< o conteúdo vem por payload de string
    TextSetFont,
    TextSetSize,
    TextSetColor,
    TextSetAlignment,
    TextSetStrokeWidth,
    TextSetStrokeColor,

    // --- Composição / projeto ----------------------------------------------
    CompositionCreate,
    CompositionDelete,
    CompositionSetSize,
    CompositionSetFps,
    CompositionSetDuration,
    CompositionSetBackground,
    ProjectSetCurrentComposition,

    // --- 3D -----------------------------------------------------------------
    SceneLoadModel,
    SceneSetCamera,
    SceneAddLight,
    SceneSetLightParam,
    SceneSetModelTransform,
    SceneSetAnimationClip,
    SceneSetMaterialParam,
    SceneSetEnvironment,

    // --- Câmera / visualização ---------------------------------------------
    ViewportSetZoom,
    ViewportSetPan,
    ViewportSetRotation,
    ViewportSetPreviewScale,   ///< AUTO / FULL / 1/2 / 1/4 / 1/8

    // --- Reprodução ---------------------------------------------------------
    PlaybackPlay,
    PlaybackPause,
    PlaybackSeek,
    PlaybackSetLoop,
    PlaybackSetSpeed,

    // --- Histórico ----------------------------------------------------------
    Undo,
    Redo,
    UndoBeginGroup,        ///< agrupa N comandos como uma única ação de undo
    UndoEndGroup,

    // --- Export -------------------------------------------------------------
    ExportRequest,
    ExportCancel,

    // --- Reprodução (fase de preview real) ------------------------------------
    // No FIM do enum: os números anteriores já estão na UI e em journals de
    // recuperação. Nada é reordenado.
    PlaybackToggle,
    PlaybackScrubBegin,    ///< o dedo encostou na régua
    PlaybackScrub,         ///< o dedo moveu (SeekPayload): coalescido no decode
    PlaybackScrubEnd,
    PlaybackStep,          ///< avança/recua N frames (StepPayload)

    // --- Áudio (fase 4) ---------------------------------------------------------
    AudioSetVolume,        ///< volume parado (AudioGainPayload, linear); animado = keyframes de AudioVolume
    AudioSetPan,           ///< balanço −1..1 (AudioGainPayload)

    // --- Tempo do clipe (fase 6) -----------------------------------------------
    LayerSetSpeed,         ///< velocidade (AudioGainPayload: layer + f32); a duração acompanha
    LayerSetReversed,      ///< reverso (AudioFlagPayload)

    // --- Forma (fase 6) -----------------------------------------------------------
    ShapeSetFill,          ///< cor de preenchimento sRGB + alfa (TextColorPayload); alfa 0 = sem preenchimento
    ShapeSetStroke,        ///< cor do contorno (TextColorPayload)
    ShapeSetParam,         ///< ShapeParamPayload: 0 tipo, 1 canto, 2 pontas, 3 raio interno, 4 contorno, 5 largura, 6 altura
};

/// Alvo de um comando que mexe em uma propriedade animável.
struct TrackRef {
    LayerId      layer{};
    TrackProperty property = TrackProperty::Opacity;
    u32          effectIndex = kInvalidIndex;  ///< só para TrackProperty::EffectParam
    u32          effectParamIndex = 0;
};


// -----------------------------------------------------------------------------
// Payloads dos comandos.
//
// Cada comando tem o seu proprio arranjo de campos, e a UI nativa escreve
// direto nestes offsets. Sao tipos NOMEADOS (nao structs anonimos dentro da
// uniao) porque o padrao C++ nao permite struct anonimo em uniao anonima — e
// depender de uma extensao do compilador num contrato de ABI seria fragil.
// -----------------------------------------------------------------------------
struct LayerCreatePayload { LayerId layer; LayerKind kind; };
struct LayerRefPayload { LayerId layer; };
struct LayerReorderPayload { LayerId layer; u32 newIndex; };
/// Trim/mover no tempo. Com `setOffset`, o deslocamento do conteúdo também
/// muda: é o trim do INÍCIO, em que o conteúdo fica parado no tempo e só a
/// borda anda (sem ele, cortar o começo de um vídeo o empurraria).
struct LayerRangePayload { LayerId layer; FrameIndex start; FrameIndex end; FrameIndex offset; u32 setOffset; };
struct LayerSplitPayload { LayerId layer; FrameIndex at; LayerId outSecond; };
struct LayerParentPayload { LayerId layer; LayerId parent; };
struct LayerBlendPayload { LayerId layer; BlendMode mode; };
struct LayerCompPayload { LayerId layer; CompositionId comp; };
struct LayerVisiblePayload { LayerId layer; bool visible; };
struct LayerLockedPayload { LayerId layer; bool locked; };
struct PositionPayload { LayerId layer; f32 x, y, z; };
struct ScalePayload { LayerId layer; f32 sx, sy, sz; };
struct RotationPayload { LayerId layer; f32 rx, ry, rz; };
struct AnchorPayload { LayerId layer; f32 ax, ay, az; };
struct OpacityPayload { LayerId layer; f32 opacity; };
struct SkewPayload { LayerId layer; f32 skewX, skewY; };
struct TransformPayload { LayerId layer; f32 x,y,z, sx,sy,sz, rx,ry,rz, ax,ay,az, opacity; };
struct KeyframePayload { TrackRef track; FrameIndex time; f32 value; };
struct KeyframeMovePayload { TrackRef track; FrameIndex fromTime; FrameIndex toTime; };
struct KeyframeInterpPayload { TrackRef track; FrameIndex time; Interpolation interp; f32 bx1, by1, bx2, by2; };
struct MaskOpPayload { LayerId layer; MaskId mask; MaskOperation op; };
struct MaskScalarPayload { LayerId layer; MaskId mask; f32 value; };
struct MaskPointPayload { LayerId layer; MaskId mask; u32 pointIndex; f32 x, y; f32 inX, inY, outX, outY; };
struct MaskCommitPayload { LayerId layer; MaskId mask; u32 pointCount; bool closed; };
struct EffectAddPayload { LayerId layer; u32 effectType; u32 index; };
struct EffectRefPayload { LayerId layer; EffectId effect; };
struct EffectReorderPayload { LayerId layer; EffectId effect; u32 newIndex; };
struct EffectParamPayload { LayerId layer; EffectId effect; u32 paramIndex; f32 value; };
struct EffectColorPayload { LayerId layer; EffectId effect; u32 paramIndex; f32 r, g, b, a; };
struct EffectEnabledPayload { LayerId layer; EffectId effect; bool enabled; };
struct AudioGainPayload { LayerId layer; f32 gain; };
struct AudioFlagPayload { LayerId layer; bool flag; };
struct AudioFadePayload { LayerId layer; FrameIndex duration; };
struct ShapeParamPayload { LayerId layer; u32 param; f32 value; };
struct TextSizePayload { LayerId layer; f32 size; };
struct TextColorPayload { LayerId layer; f32 r, g, b, a; };
struct TextAlignPayload { LayerId layer; u32 alignment; };
struct TextStrokeWidthPayload { LayerId layer; f32 width; };
struct CompRefPayload { CompositionId comp; };
struct CompSizePayload { CompositionId comp; u32 width, height; };
struct CompFpsPayload { CompositionId comp; f64 fps; };
struct CompDurationPayload { CompositionId comp; FrameIndex duration; };
struct CompBackgroundPayload { CompositionId comp; f32 r, g, b, a; };
struct SceneModelPayload { Scene3DId scene; AssetId asset; };
struct SceneCameraPayload { Scene3DId scene; LayerId camera; };
struct SceneLightPayload { Scene3DId scene; LayerId light; u32 lightType; };
struct SceneLightParamPayload { Scene3DId scene; LightId light; u32 param; f32 value; };
struct SceneModelTransformPayload { Scene3DId scene; u32 modelIndex; f32 x,y,z, sx,sy,sz, rx,ry,rz; };
struct SceneClipPayload { Scene3DId scene; u32 modelIndex; i32 clipIndex; f32 time; };
struct SceneMaterialPayload { Scene3DId scene; u32 modelIndex; u32 materialIndex; u32 param; f32 value; };
struct SceneEnvironmentPayload { Scene3DId scene; AssetId environment; };
struct ViewportZoomPayload { f32 zoom; };
struct ViewportPanPayload { f32 x, y; };
struct ViewportRotationPayload { f32 rx, ry, rz; };
struct PreviewScalePayload { u32 scaleNumerator; u32 scaleDenominator; bool automatic; };
struct SeekPayload { TickNs time; };
struct LoopPayload { bool loop; };
struct SpeedPayload { f32 speed; };
struct StepPayload { i32 frames; };
struct ExportRequestPayload { u32 codec; u32 width; u32 height; f64 fps; u32 bitrate; u32 audioBitrate; };

/// Comando. União de campos por tipo — `union` porque não há construtor nem
/// destrutor a rodar, e o bloco inteiro é memcpy-ável pela bridge.
struct Command {
    /// Tudo zerado, payload inteiro incluído. O `raw = 0` da união só zera 8
    /// dos 64 bytes: um comando montado em C++ campo a campo (motor, testes,
    /// recuperação) levava o resto do payload com lixo da pilha — um `z` de
    /// posição que ninguém escreveu virava NaN no transform (achado pelo fuzz
    /// da Fase 8, §134). Continua trivialmente copiável (memcpy pela bridge).
    Command() noexcept { std::memset(static_cast<void*>(this), 0, sizeof(*this)); }

    CommandType type = CommandType::Nop;

    /// Payload de string: deslocamento no blob do bloco, não ponteiro.
    /// (nome de layer, conteúdo de texto, caminho de asset). O blob vem logo
    /// depois do array de comandos na mesma mensagem.
    u32 stringOffset = 0;
    u32 stringLength = 0;

    union {
        // Sinais: cada campo abaixo ja foi um `struct { ... }` anonimo dentro da
        // uniao. Eles viraram TIPOS NOMEADOS por dois motivos:
        //
        //   1. `struct` anonimo em uniao anonima e uma EXTENSAO do compilador, nao
        //      C++ padrao. Compila nos tres alvos, mas e uma extensao — e o aviso
        //      -Wnested-anon-types aparece em todo build, treinando quem le a
        //      ignorar avisos.
        //
        //   2. `offsetof` sobre um TIPO nomeado documenta o contrato melhor do que
        //      um tipo sem nome: `sizeof(LayerCreatePayload)` diz o que e.
        //
        // A ORDEM E O LAYOUT NAO MUDARAM. Os static_assert de offset em
        // `cmd_layout` continuam valendo campo a campo.
        LayerCreatePayload layer_create;
        LayerRefPayload layer_ref;
        LayerReorderPayload layer_reorder;
        LayerRangePayload layer_range;
        LayerSplitPayload layer_split;
        LayerParentPayload layer_parent;
        LayerBlendPayload layer_blend;
        LayerCompPayload layer_comp;
        LayerVisiblePayload layer_visible;
        LayerLockedPayload layer_locked;
        PositionPayload position;
        ScalePayload scale;
        RotationPayload rotation;
        AnchorPayload anchor;
        OpacityPayload opacity;
        SkewPayload skew;
        TransformPayload transform;
        KeyframePayload keyframe;
        KeyframeMovePayload keyframe_move;
        KeyframeInterpPayload keyframe_interp;
        MaskOpPayload mask_op;
        MaskScalarPayload mask_scalar;
        MaskPointPayload mask_point;
        MaskCommitPayload mask_commit;
        EffectAddPayload effect_add;
        EffectRefPayload effect_ref;
        EffectReorderPayload effect_reorder;
        EffectParamPayload effect_param;
        EffectColorPayload effect_color;
        EffectEnabledPayload effect_enabled;
        AudioGainPayload audio_gain;
        AudioFlagPayload audio_flag;
        AudioFadePayload audio_fade;
        ShapeParamPayload shape_param;
        TextSizePayload text_size;
        TextColorPayload text_color;
        TextAlignPayload text_align;
        TextStrokeWidthPayload text_stroke_width;
        CompRefPayload comp_ref;
        CompSizePayload comp_size;
        CompFpsPayload comp_fps;
        CompDurationPayload comp_duration;
        CompBackgroundPayload comp_background;
        SceneModelPayload scene_model;
        SceneCameraPayload scene_camera;
        SceneLightPayload scene_light;
        SceneLightParamPayload scene_light_param;
        SceneModelTransformPayload scene_model_transform;
        SceneClipPayload scene_clip;
        SceneMaterialPayload scene_material;
        SceneEnvironmentPayload scene_environment;
        ViewportZoomPayload viewport_zoom;
        ViewportPanPayload viewport_pan;
        ViewportRotationPayload viewport_rotation;
        PreviewScalePayload preview_scale;
        SeekPayload seek;
        LoopPayload loop;
        SpeedPayload speed;
        StepPayload step;
        ExportRequestPayload export_request;

        /// Leitura crua do payload. Usado para zerar o comando inteiro.
        u64 raw = 0;
    };

    /// Identificador gerado pela UI para casar com o resultado assíncrono
    /// (ex.: o id real de uma layer criada). 0 = sem correlação.
    u64 correlationId = 0;

    /// Preenchimento explícito até 128 bytes. Existe por dois motivos:
    ///   1. cada slot da CommandQueue ocupa linhas de cache inteiras, então
    ///      não há desperdício nem falso compartilhamento na fronteira;
    ///   2. o tamanho vira contrato de ABI entre a UI e o motor — acrescentar
    ///      um campo ao payload sem revisar isto falha na compilação.
    u64 reserved_[5]{};

    [[nodiscard]] bool valid() const noexcept { return type != CommandType::Nop; }

    /// Payload da união zerado. Usado quando o comando só preenche parte dos
    /// campos e o resto precisa ser determinístico.
    void clear_payload() noexcept { raw = 0; }
};

static_assert(sizeof(Command) == 128, "Command deve ocupar exatamente 2 linhas de cache");
static_assert(alignof(Command) == 8, "alinhamento de Command e contrato da bridge");
static_assert(std::is_trivially_copyable_v<Command>, "Command precisa ser memcpy-avel pela bridge");

// O layout congelado em bridge/BridgePods.hpp é o que a UI nativa usa para
// ESCREVER comandos. Um campo fora de lugar aqui produziria comandos com
// valores trocados — sem erro, sem crash, e com o projeto corrompido em
// silêncio. Estas asserções fazem a mudança quebrar a compilação.
static_assert(offsetof(Command, type) == bridge::command_layout::kOffsetType);
static_assert(offsetof(Command, stringOffset) == bridge::command_layout::kOffsetStrOff);
static_assert(offsetof(Command, stringLength) == bridge::command_layout::kOffsetStrLen);
static_assert(offsetof(Command, raw) == bridge::command_layout::kOffsetPayload);
static_assert(offsetof(Command, correlationId) == bridge::command_layout::kOffsetCorrId);
static_assert(sizeof(Command) == bridge::command_layout::kSize);

// -----------------------------------------------------------------------------
// Offsets do PAYLOAD, congelados.
//
// A UI nativa escreve estes campos direto no buffer, por offset. Não há como
// derivá-los em Kotlin a partir do C++ — então eles são declarados nos dois
// lados, e estas asserções são o que garante que os dois lados concordam.
//
// Sem elas, acrescentar um campo no meio de um payload deslocaria todos os
// seguintes: a UI continuaria escrevendo nos offsets antigos e o motor leria
// valores trocados. Sem crash, sem erro — só um projeto silenciosamente errado.
//
// Ao acrescentar um campo: ACRESCENTE NO FIM do struct membro, atualize os
// números aqui e no CommandBatch.kt, e deixe a asserção falhar até os dois
// estarem de acordo.
// -----------------------------------------------------------------------------
namespace cmd_layout {
    // Campos comuns aos comandos de uma camada.
    inline constexpr usize kLayerPayload      = 16;   // LayerId
    inline constexpr usize kLayerPayload2     = 24;   // LayerId, u32, u16 ou f32

    // Payloads maiores.
    inline constexpr usize kRangeStart        = 24;
    inline constexpr usize kRangeEnd          = 32;
    inline constexpr usize kRangeOffset       = 40;
    inline constexpr usize kRangeSetOffset    = 48;
    inline constexpr usize kSplitAt           = 24;
    inline constexpr usize kPositionX         = 24;
    inline constexpr usize kPositionY         = 28;
    inline constexpr usize kPositionZ         = 32;
    inline constexpr usize kScaleX            = 24;
    inline constexpr usize kScaleY            = 28;
    inline constexpr usize kScaleZ            = 32;
    inline constexpr usize kRotationX         = 24;
    inline constexpr usize kRotationY         = 28;
    inline constexpr usize kRotationZ         = 32;
    inline constexpr usize kOpacityValue      = 24;
    inline constexpr usize kGainValue         = 24;
    inline constexpr usize kTextSizeValue     = 24;
    inline constexpr usize kTextColorR        = 24;
    inline constexpr usize kTextColorG        = 28;
    inline constexpr usize kTextColorB        = 32;
    inline constexpr usize kTextColorA        = 36;
    inline constexpr usize kAlignmentValue    = 24;
    inline constexpr usize kCompWidth         = 24;
    inline constexpr usize kCompHeight        = 28;
    inline constexpr usize kCompFps           = 24;
    inline constexpr usize kSeekTime          = 16;
    inline constexpr usize kLoopValue         = 16;
    inline constexpr usize kZoomValue         = 16;
    inline constexpr usize kPreviewNumerator  = 16;
    inline constexpr usize kPreviewDenom      = 20;
    inline constexpr usize kPreviewAutomatic  = 24;

    // Transform completo: 13 floats a partir de +24.
    inline constexpr usize kTransformX        = 24;
    inline constexpr usize kTransformScaleX   = 36;
    inline constexpr usize kTransformRotX     = 48;
    inline constexpr usize kTransformAnchorX  = 60;
    inline constexpr usize kTransformOpacity  = 72;

    // Keyframe: TrackRef em +16 (24 bytes), tempo em +40, valor em +48.
    inline constexpr usize kKeyframeTrack     = 16;
    inline constexpr usize kKeyframeTime      = 40;
    inline constexpr usize kKeyframeValue     = 48;
    // TrackRef por dentro.
    inline constexpr usize kTrackLayer        = 0;
    inline constexpr usize kTrackProperty     = 8;
    inline constexpr usize kTrackEffectIndex  = 12;
    inline constexpr usize kTrackEffectParam  = 16;

    /// Payload do maior comando. Nada pode passar disto.
    inline constexpr usize kMaxPayload = bridge::command_layout::kPayloadSize;
} // namespace cmd_layout

static_assert(offsetof(Command, layer_create.layer) == cmd_layout::kLayerPayload);
static_assert(offsetof(Command, layer_create.kind) == cmd_layout::kLayerPayload2);
static_assert(offsetof(Command, layer_reorder.newIndex) == cmd_layout::kLayerPayload2);
static_assert(offsetof(Command, layer_range.start) == cmd_layout::kRangeStart);
static_assert(offsetof(Command, layer_range.end) == cmd_layout::kRangeEnd);
static_assert(offsetof(Command, layer_range.offset) == cmd_layout::kRangeOffset);
static_assert(offsetof(Command, layer_range.setOffset) == cmd_layout::kRangeSetOffset);
static_assert(offsetof(Command, layer_split.at) == cmd_layout::kSplitAt);
static_assert(offsetof(Command, layer_blend.mode) == cmd_layout::kLayerPayload2);
static_assert(offsetof(Command, layer_visible.visible) == cmd_layout::kLayerPayload2);
static_assert(offsetof(Command, layer_parent.parent) == cmd_layout::kLayerPayload2);
static_assert(offsetof(Command, layer_comp.comp) == cmd_layout::kLayerPayload2);

static_assert(offsetof(Command, position.x) == cmd_layout::kPositionX);
static_assert(offsetof(Command, position.y) == cmd_layout::kPositionY);
static_assert(offsetof(Command, position.z) == cmd_layout::kPositionZ);
static_assert(offsetof(Command, scale.sx) == cmd_layout::kScaleX);
static_assert(offsetof(Command, scale.sy) == cmd_layout::kScaleY);
static_assert(offsetof(Command, scale.sz) == cmd_layout::kScaleZ);
static_assert(offsetof(Command, rotation.rx) == cmd_layout::kRotationX);
static_assert(offsetof(Command, rotation.ry) == cmd_layout::kRotationY);
static_assert(offsetof(Command, rotation.rz) == cmd_layout::kRotationZ);
static_assert(offsetof(Command, opacity.opacity) == cmd_layout::kOpacityValue);

static_assert(offsetof(Command, transform.x) == cmd_layout::kTransformX);
static_assert(offsetof(Command, transform.sx) == cmd_layout::kTransformScaleX);
static_assert(offsetof(Command, transform.rx) == cmd_layout::kTransformRotX);
static_assert(offsetof(Command, transform.ax) == cmd_layout::kTransformAnchorX);
static_assert(offsetof(Command, transform.opacity) == cmd_layout::kTransformOpacity);

static_assert(offsetof(Command, keyframe.track) == cmd_layout::kKeyframeTrack);
static_assert(offsetof(Command, keyframe.time) == cmd_layout::kKeyframeTime);
static_assert(offsetof(Command, keyframe.value) == cmd_layout::kKeyframeValue);
static_assert(offsetof(Command, keyframe.track.layer) == cmd_layout::kKeyframeTrack + cmd_layout::kTrackLayer);
static_assert(offsetof(Command, keyframe.track.property) == cmd_layout::kKeyframeTrack + cmd_layout::kTrackProperty);
static_assert(offsetof(Command, keyframe.track.effectIndex) == cmd_layout::kKeyframeTrack + cmd_layout::kTrackEffectIndex);

static_assert(offsetof(Command, audio_gain.gain) == cmd_layout::kGainValue);
static_assert(offsetof(Command, text_size.size) == cmd_layout::kTextSizeValue);
static_assert(offsetof(Command, text_color.r) == cmd_layout::kTextColorR);
static_assert(offsetof(Command, text_color.g) == cmd_layout::kTextColorG);
static_assert(offsetof(Command, comp_size.width) == cmd_layout::kCompWidth);
static_assert(offsetof(Command, comp_size.height) == cmd_layout::kCompHeight);
static_assert(offsetof(Command, comp_fps.fps) == cmd_layout::kCompFps);
static_assert(offsetof(Command, seek.time) == cmd_layout::kSeekTime);
static_assert(offsetof(Command, loop.loop) == cmd_layout::kLoopValue);
static_assert(offsetof(Command, viewport_zoom.zoom) == cmd_layout::kZoomValue);
static_assert(offsetof(Command, preview_scale.scaleNumerator) == cmd_layout::kPreviewNumerator);
static_assert(offsetof(Command, preview_scale.scaleDenominator) == cmd_layout::kPreviewDenom);
static_assert(offsetof(Command, preview_scale.automatic) == cmd_layout::kPreviewAutomatic);

// Os comandos que a UI emite em maior volume precisam caber folgadamente. Se um
// deles crescer perto do teto da união, o próximo campo não caberia.
static_assert(sizeof(Command::transform) <= cmd_layout::kMaxPayload);
static_assert(sizeof(Command::keyframe_interp) <= cmd_layout::kMaxPayload);

/// Um bloco de comandos produzido pela UI em um frame.
struct CommandBatch {
    const Command* commands = nullptr;
    u32            count    = 0;
    const char*    stringBlob = nullptr;
    u32            stringBlobSize = 0;

    [[nodiscard]] bool empty() const noexcept { return count == 0; }
};

} // namespace aurea
