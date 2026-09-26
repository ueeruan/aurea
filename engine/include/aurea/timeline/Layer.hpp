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
#include "aurea/vector/VectorData.hpp"
#include "aurea/text/Captions.hpp"

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

/// Máscara: caminho bezier em px da camada (0,0 = canto superior esquerdo da
/// fonte). Rasterizada na GPU (render/MaskRaster.hpp) em cobertura
/// antisserrilhada que multiplica o alfa da camada ANTES dos efeitos.
struct MaskPoint {
    Vec2 position{0.0f, 0.0f};
    /// Tangentes de bezier, relativas à posição.
    Vec2 inTangent{0.0f, 0.0f};
    Vec2 outTangent{0.0f, 0.0f};
};

/// Forma do caminho num keyframe (tempo LOCAL da camada). Entre dois keys com
/// o mesmo número de pontos, cada ponto e as tangentes interpolam; com número
/// diferente, a forma segura até o próximo key.
struct MaskPathKey {
    i64 frame = 0;
    u8  interp = 1;      ///< 0 segura, 1 linear, 2 suave (ease in-out)
    std::vector<MaskPoint> points;
};

struct Mask {
    /// Id local à layer, estável enquanto a máscara existir. Mesma razão do
    /// Effect::id: a UI fala de "esta máscara", não de "a terceira do vetor".
    u32           id = kInvalidIndex;
    std::string   name;
    MaskOperation operation = MaskOperation::Add;
    bool          inverted = false;
    f32           feather  = 0.0f;     ///< px da camada: largura da rampa (±2σ do gaussiano)
    f32           expansion = 0.0f;    ///< px da camada: dilata (+) / erode (−) o path
    f32           opacity  = 1.0f;
    bool          closed   = true;

    std::vector<MaskPoint> points;
    /// Caminho animado: vazio = `points` parado; senão os keys mandam (e
    /// `points` guarda a última forma editada fora deles).
    std::vector<MaskPathKey> pathKeys;

    /// Quantos pontos o preview pode usar. Máscaras com centenas de pontos
    /// entram no modo adaptativo com uma versão simplificada.
    u32           previewPointLimit = 0;   ///< 0 = sem limite

    /// Bitmap da máscara em cache, com a chave do estado que o gerou. Enquanto
    /// a chave bate, não há re-rasterização.
    u64           cacheKey = 0;
};

/// Blocos de dado específico de tipo. Mantidos pequenos e POD.
/// Seletor do animador de texto: quanto cada unidade está "dentro".
struct TextSelector {
    u8   basedOn = 0;       ///< 0 caractere, 1 palavra, 2 linha
    u8   type = 0;          ///< 0 intervalo, 1 wiggly, 2 intervalo AE (rampas saturadas)
    u8   shape = 0;         ///< 0 quadrado, 1 rampa sobe, 2 rampa desce, 3 triângulo, 4 redondo, 5 suave
    bool randomOrder = false;
    u32  seed = 1;
    f32  start = 0.0f, end = 100.0f, offset = 0.0f;   ///< %
    f32  amount = 100.0f, easeHigh = 0.0f, easeLow = 0.0f;
    f32  wiggleRate = 2.0f;                           ///< variações por segundo
};

enum TextAnimProp : u32 {
    kTextPropPosition = 1u << 0, kTextPropScale = 1u << 1, kTextPropRotation = 1u << 2, kTextPropOpacity = 1u << 3,
    kTextPropTracking = 1u << 4, kTextPropBlur = 1u << 5, kTextPropSkew = 1u << 6, kTextPropStrokeWidth = 1u << 7,
    kTextPropCharOffset = 1u << 8, kTextPropFill = 1u << 9, kTextPropStroke = 1u << 10,
};

/// Animador de texto: seletor + propriedades (valor com a unidade toda dentro).
struct TextAnimator {
    std::string  name;
    bool         enabled = true;
    TextSelector selector;
    u32  props = 0;                        ///< TextAnimProp
    Vec3 position{0.0f, 0.0f, 0.0f};       ///< px (Z = profundidade por caractere)
    Vec2 scale{100.0f, 100.0f};            ///< %
    Vec3 rotation{0.0f, 0.0f, 0.0f};       ///< graus (X/Y = 3D por caractere)
    f32  opacity = 100.0f;                 ///< %
    f32  tracking = 0.0f;                  ///< px entre caracteres
    f32  blur = 0.0f, skew = 0.0f, strokeWidth = 0.0f, charOffset = 0.0f;
    Vec4 fill{1.0f, 1.0f, 1.0f, 1.0f};     ///< sRGB
    Vec4 stroke{0.0f, 0.0f, 0.0f, 1.0f};
};

/// Trecho com estilo próprio (rich text): caracteres [start, end) do texto.
struct TextSpan {
    u32  start = 0, end = 0;
    bool hasColor = false;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    u16  weight = 0;        ///< 0 = o do texto; 700 = negrito…
    f32  scale = 1.0f;      ///< tamanho relativo ao do texto
};

struct TextData {
    std::string content  = "Texto";
    FontId      font{};
    /// Fonte: família + peso + itálico (fontes do aparelho, portável entre
    /// aparelhos) e, para fonte importada, o arquivo no projeto ("docs:…").
    /// Vazio = fonte padrão do aparelho.
    std::string fontFamily;
    u16         fontWeight = 400;
    bool        fontItalic = false;
    std::string fontPath;
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
    /// Caixa: 0 texto de ponto (largura automática), 1 parágrafo (quebra na
    /// largura, altura automática), 2 caixa fixa (quebra e corta), 3 caixa fixa
    /// que encolhe o texto para caber.
    u32         boxMode = 0;
    std::vector<TextSpan> spans;
    /// Fundo atrás do texto (caixa arredondada) e sombra projetada.
    bool        background = false;
    Vec4        backgroundColor{0.0f, 0.0f, 0.0f, 0.6f};
    f32         backgroundPadding = 14.0f;
    f32         backgroundRadius = 10.0f;
    bool        shadow = false;
    Vec4        shadowColor{0.0f, 0.0f, 0.0f, 0.6f};
    Vec2        shadowOffset{4.0f, 6.0f};
    f32         shadowBlur = 6.0f;

    /// Text Animator: pilha de animadores (os valores animados moram na
    /// TrackSet da camada como TrackProperty::TextAnimParam).
    std::vector<TextAnimator> animators;

    /// Legenda gerada da fala de outra camada (id empacotado; 0 = texto comum).
    /// Gerar de novo substitui as legendas daquela camada.
    u64         captionSource = 0;

    /// Texto no caminho: camada vetorial-guia (id empacotado; 0 = linha reta).
    /// O primeiro caminho dela, no espaço da composição, conduz a linha de
    /// base; `pathOffset` = margem inicial (px ao longo do caminho);
    /// `pathPerpendicular` gira cada letra pela tangente; `pathReverse`
    /// percorre o caminho ao contrário.
    u64         pathLayer = 0;
    f32         pathOffset = 0.0f;
    bool        pathPerpendicular = true;
    bool        pathReverse = false;
};

struct ShapeData {
    /// 0 retângulo (cantos arredondados), 1 elipse, 2 caminho, 3 polígono
    /// regular, 4 estrela, 5 cruz, 6 anel, 7 fatia, 8 flor, 9 seta,
    /// 10 triângulo retângulo (shaders/shape/shape.frag), 11 vetorial
    /// (kShapeVector: grupos em `vector`, vector/Vector.hpp).
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
    /// Camada vetorial (shapeType == kShapeVector).
    VectorData vector;
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

// Per-layer factors over the immutable imported material. Mask bits select
// R/G/B/A/metallic/roughness; unselected components retain the asset value.
struct MaterialOverride {
    u32 materialIndex = 0;
    u32 mask = 0;
    Vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    f32 metallic = 1.0f;
    f32 roughness = 1.0f;
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
    std::vector<MaterialOverride> materials;
};

/// Forma do emissor (Aurea Particular). Todas amostradas em forma fechada no
/// shader: nenhuma precisa de malha, de textura ou de estado de simulação.
enum class ParticleEmitter : u32 {
    Point = 0,    ///< um ponto (o centro do emissor)
    Box,          ///< caixa: largura × altura × profundidade
    Sphere,       ///< esfera oca; `emitFill` enche o volume
    Disc,         ///< disco no plano XY
    Line,         ///< segmento orientado por `emitterRotation`
    Grid,         ///< grade `gridX` × `gridY` de pontos
    // 8.2 — emissores a partir de outra coisa da cena (`emitterSource`):
    Layer,        ///< pixels opacos de uma camada (superfície ou borda, `emitFrom`)
    Text,         ///< os glifos de uma camada de texto
    Path,         ///< o caminho da 1ª máscara (ou do vetor) de uma camada
    Mesh,         ///< vértices / superfície / arestas de um modelo 3D
    WorldExplosive, ///< spherical producer, isotropic 3D velocity
    WorldJet,       ///< spherical producer, directional 3D cone
    WorldVortex,    ///< spherical producer, rotating 3D flow
    WorldBox,       ///< box producer with scattered light trails
    Count,
};

/// Como a partícula é desenhada.
enum class ParticleShape : u32 {
    Circle = 0,   ///< disco macio
    Square,       ///< quadrado
    Streak,       ///< esticada no sentido da velocidade (rastro de faísca)
    Soft,         ///< brilho radial bem suave (poeira, luz)
    Texture,      ///< imagem do projeto (`textureAsset`)
    Mesh,         ///< malha 3D instanciada (`meshSource`)
    Count,
};

/// O que a partícula faz ao encontrar o plano de colisão.
enum class ParticleCollision : u32 {
    None = 0,
    Plane,        ///< plano horizontal em `collisionY`
    Sphere,       ///< esfera em `collisionCenter`, raio `collisionRadius` (quica por fora)
    Box,          ///< caixa em `collisionCenter`, tamanho `collisionBox` (quica por fora)
    Count,
};

/// Os parâmetros que a UI pode escrever, um a um.
///
/// O NÚMERO É CONTRATO com o Kotlin e com os presets salvos: reordenar a lista
/// sem renumerar os dois lados faz um projeto antigo escrever no parâmetro
/// errado, em silêncio. Por isso cada valor está escrito à mão.
enum class ParticleParam : u32 {
    // Emissor
    EmitterType = 0, EmitterWidth, EmitterHeight, EmitterRadius, EmitterRotation,
    EmitterDepth, GridX, GridY, EmitFill, EmitterOffsetX, EmitterOffsetY,
    // Emissão
    Rate, Burst, Lifetime, LifeRandom, Speed, SpeedRandom, Direction, Spread,
    InheritVelocity, Seed,
    // Partícula
    ParticleType, Softness, Rotation, RotationRandom, Spin,
    // Ao longo da vida
    StartSize, EndSize, StartOpacity, EndOpacity,
    // Física
    GravityX, GravityY, GravityZ, Drag, WindX, WindY,
    Turbulence, TurbulenceScale, TurbulenceSpeed, Vortex, Attractor,
    // Rastro
    TrailLength, TrailTaper,
    // Aux
    AuxCount, AuxAt, AuxLife, AuxSpeed, AuxSize, AuxSpread,
    // Colisão
    Collision, CollisionY, CollisionBounce,
    // Render
    BlendMode, MaxParticles,
    // 8.2 (v21) — só no fim: o número é contrato.
    EmitterSpace, EmitFrom, AuxProbability, TrailWidth, TrailOpacity,
    SizeRandom, OpacityRandom, ColorRandom,
    CollisionX, CollisionZ, CollisionRadius, CollisionWidth, CollisionHeight, CollisionDepth,
    MeshScale, MeshLit,
    Count,
};

/// Sistema de partículas (Aurea Particular).
///
/// A SIMULAÇÃO É ANALÍTICA: posição, velocidade, tamanho, cor e opacidade saem
/// de uma conta fechada sobre (semente, slot, geração, tempo). Nada de estado
/// por partícula na GPU. As consequências são as que o dono exigiu:
/// determinístico (prévia = export), seek instantâneo em qualquer ponto e
/// nenhum checkpoint para guardar — não existe estado para reconstruir.
///
/// As forças que entram têm forma fechada: gravidade e vento integram direto;
/// o arrasto é exponencial; o vórtice é uma rotação; o atrator é uma mola; e a
/// turbulência é a SOMA DE SENOIDES de três eixos, que é fechada e dá o visual
/// de campo contínuo sem precisar integrar ruído.
struct ParticleData {
    // --- Emissor -------------------------------------------------------------
    u32  emitterType = 0;                     ///< ParticleEmitter
    Vec2 emitterSize{20.0f, 20.0f};           ///< largura × altura (px)
    Vec2 emitterOffset{0.0f, 0.0f};           ///< do centro da camada (px)
    f32  emitterRadius = 40.0f;               ///< esfera e disco
    f32  emitterRotation = 0.0f;              ///< graus (linha e disco)
    f32  emitterDepth = 0.0f;                 ///< espessura em Z (caixa)
    u32  gridX = 4, gridY = 4;
    bool emitFill = false;                    ///< esfera cheia em vez de casca

    // --- Emissão -------------------------------------------------------------
    f32  rate = 100.0f;                       ///< partículas por segundo
    u32  burst = 0;                           ///< extras no primeiro instante
    f32  lifetime = 2.0f;                     ///< segundos
    f32  lifeRandom = 0.25f;                  ///< ±fração da vida
    f32  speed = 200.0f;                      ///< px/s
    f32  speedRandom = 0.3f;                  ///< ±fração da velocidade
    f32  direction = -90.0f;                  ///< graus; −90 = para cima
    f32  spread = 45.0f;                      ///< graus, abertura do cone
    f32  inheritVelocity = 0.0f;              ///< 0..1 da velocidade da camada
    u32  seed = 1;

    // --- Partícula -----------------------------------------------------------
    u32  particleType = 0;                    ///< ParticleShape
    f32  softness = 0.7f;                     ///< 0 = borda dura, 1 = bem macia (0,7 = a borda de antes deste controle existir)
    f32  rotation = 0.0f;                     ///< graus
    f32  rotationRandom = 0.0f;               ///< ±graus
    f32  spin = 0.0f;                         ///< graus/s ao longo da vida

    // --- Ao longo da vida ----------------------------------------------------
    f32  startSize = 20.0f, endSize = 0.0f;
    f32  startOpacity = 1.0f, endOpacity = 0.0f;
    Vec4 startColor{1.0f, 0.85f, 0.45f, 1.0f};   ///< sRGB, reta
    Vec4 endColor{1.0f, 0.35f, 0.10f, 1.0f};

    // --- Física --------------------------------------------------------------
    Vec3 gravity{0.0f, -980.0f, 0.0f};
    f32  drag = 0.0f;                         ///< 1/s; 0 = sem arrasto
    Vec3 wind{0.0f, 0.0f, 0.0f};              ///< px/s²
    f32  turbulence = 0.0f;                   ///< px/s² de amplitude
    f32  turbulenceScale = 1.0f;              ///< 1 = uma ondulação por 200 px
    f32  turbulenceSpeed = 1.0f;              ///< multiplica o tempo
    f32  vortex = 0.0f;                       ///< graus/s em torno do emissor
    f32  attractor = 0.0f;                    ///< mola para o emissor; <0 = repulsor

    // --- Rastro --------------------------------------------------------------
    f32  trailLength = 0.0f;                  ///< segundos de rastro (Streak)
    f32  trailTaper = 1.0f;                   ///< 0 = largura constante

    // --- Aux (partículas secundárias) ---------------------------------------
    u32  auxCount = 0;                        ///< 0 = desligado
    f32  auxAt = 0.6f;                        ///< fração da vida em que nascem
    f32  auxLife = 0.5f, auxSpeed = 120.0f, auxSize = 4.0f, auxSpread = 180.0f;
    Vec4 auxColor{1.0f, 0.7f, 0.3f, 1.0f};

    // --- Colisão -------------------------------------------------------------
    u32  collision = 0;                       ///< ParticleCollision
    f32  collisionY = 0.0f;                   ///< plano em Y (px da camada)
    f32  collisionBounce = 0.4f;              ///< 0 = gruda, 1 = quica igual

    // --- Render --------------------------------------------------------------
    u32  maxParticles = 10000;
    u32  blendMode = 1;                       ///< aditivo por padrão
    bool collideEnvironment = false;

    // --- 8.2 (v21) -------------------------------------------------------------
    u32  emitterSpace = 0;                    ///< 0 = local (anda com a camada), 1 = mundo (fica onde nasceu)
    u32  emitFrom = 1;                        ///< camada/malha: 0 vértices, 1 superfície, 2 bordas/arestas
    u64  emitterSource = 0;                   ///< LayerId (pack) da camada que emite (Layer/Text/Path/Mesh)
    u64  textureAsset = 0;                    ///< AssetId (pack) da imagem da partícula (Texture)
    u64  meshSource = 0;                      ///< LayerId (pack) da camada de modelo 3D (partícula Mesh)
    f32  auxProbability = 1.0f;               ///< 0..1: chance de cada primária gerar o aux
    f32  trailWidth = 1.0f;                   ///< × tamanho da partícula
    f32  trailOpacity = 1.0f;
    f32  sizeRandom = 0.0f, opacityRandom = 0.0f, colorRandom = 0.0f;   ///< ±fração, por partícula
    Vec3 collisionCenter{0.0f, 0.0f, 0.0f};   ///< esfera/caixa (px, a partir do centro do emissor)
    f32  collisionRadius = 100.0f;
    Vec3 collisionBox{200.0f, 200.0f, 200.0f};
    f32  meshScale = 1.0f;
    bool meshLit = true;                      ///< partícula de malha com PBR/luzes da cena

    /// Ao longo da vida (0 pontos = as pontas início/fim de cima). Cor: (posição
    /// 0..1, r, g, b) sRGB reta; tamanho e opacidade: (posição 0..1, multiplicador).
    static constexpr u32 kMaxLifeStops = 8;
    u32  colorStopCount = 0;
    Vec4 colorStops[kMaxLifeStops]{};
    u32  sizeCurveCount = 0;
    Vec2 sizeCurve[kMaxLifeStops]{};
    u32  opacityCurveCount = 0;
    Vec2 opacityCurve[kMaxLifeStops]{};

};

struct CompositionRef {
    CompositionId composition{};
    bool          collapsed = false;   ///< exibição colapsada na timeline
};

/// Etiquetas de cor: 0 = nenhuma + as 12 cores da paleta da UI (4 bits nas
/// flags da linha da timeline).
inline constexpr u8 kLayerLabelCount = 13;


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
    // Recovery metadata for pre-canonical remap tracks. Never evaluated or shown
    // as active keyframes; copied with the layer/history and persisted losslessly.
    std::vector<Track> timeRemapLegacyTracks;
    bool timeRemapLegacyMigrated = false;

    /// Velocidade do conteúdo (quadros da fonte por quadro da timeline).
    /// 0 = quadro congelado. `reversed` toca do ponto de saída para o de
    /// entrada. `offset` continua sendo o ponto de entrada na fonte.
    f32   speed = 1.0f;
    bool  reversed = false;
    /// Desfoque de movimento desta camada (a composição define o obturador).
    bool  motionBlur = false;
    /// Vídeo fora da grade da fonte (câmera lenta, velocidade quebrada):
    /// 0 = quadro mais próximo (repete), 1 = mistura dos dois quadros vizinhos
    /// da fonte pelo tempo entre eles, 2 = movimento de pixels (optical flow:
    /// o quadro intermediário é deformado pelo fluxo entre os dois).
    u8    frameBlend = 0;
    /// Desfoque pelo movimento do PRÓPRIO vídeo (vetores do optical flow, à
    /// la RSMB): 0 desligado; 1 = o obturador da composição. Borra o que se
    /// mexe dentro do quadro, não o transform da camada.
    f32   vectorBlur = 0.0f;
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
    /// Solo: com QUALQUER camada em solo, o preview só desenha as que estão
    /// (o áudio já seguia a mesma chave). O export ignora o solo.
    bool     solo    = false;
    bool     threeD  = false;  ///< participa da cena 3D da composição
    /// Camada de ajuste: não desenha conteúdo próprio; os efeitos dela valem
    /// para a composição de TUDO o que está abaixo (no trecho de tempo dela),
    /// no quadro inteiro, misturados pela opacidade da camada.
    bool     adjustment = false;
    /// Guia: aparece no preview do editor e nunca sai no export.
    bool     guide = false;
    /// Etiqueta de cor da camada (0 = nenhuma; 1..kLayerLabelCount-1 = paleta
    /// fixa da UI). Só organização: não muda o render.
    u8       label = 0;
    /// Track matte: camada cujo alfa/luma recorta esta (inválido = nenhuma).
    /// A matte deixa de ser desenhada por conta própria enquanto é usada.
    LayerId   matteSource{};
    MatteMode matteMode = MatteMode::None;

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
    std::vector<text::CaptionSegment> captions;
    text::CaptionOptions captionOptions;
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

    /// Source distance traversed during a centered shutter, in composition
    /// frames. Sampling both halves preserves motion at a direction reversal;
    /// a frozen remap produces zero regardless of the clip's stored speed.
    [[nodiscard]] f64 source_shutter_travel(f64 timelineTime, f64 shutterFrames) const noexcept {
        if (!std::isfinite(timelineTime) || !std::isfinite(shutterFrames) || shutterFrames <= 0.0) return 0.0;
        const f64 center = source_frame_f(timelineTime);
        const f64 before = source_frame_f(timelineTime - shutterFrames * 0.5);
        const f64 after = source_frame_f(timelineTime + shutterFrames * 0.5);
        const f64 travel = std::fabs(center - before) + std::fabs(after - center);
        return std::isfinite(travel) ? travel : 0.0;
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

    // --- Ambiente por objeto (v22, Fase 9) -------------------------------------
    //
    // Cada objeto 3D escolhe de ONDE vem a luz do ambiente: a do projeto
    // (`EnvironmentSource::Scene`) ou a PRÓPRIA (`Custom`). O estado é deste
    // objeto; os MAPAS na GPU são compartilhados por asset — dois objetos com o
    // mesmo HDRI usam a mesma textura, e mudar um não mexe no outro.
    enum class EnvironmentSource : u32 { Scene = 0, Custom = 1 };
    u32  environmentSource = static_cast<u32>(EnvironmentSource::Scene);
    u64  environmentAsset = 0;                ///< AssetId (pack) do HDRI deste objeto
    f32  environmentIntensity = 1.0f;
    f32  environmentExposure = 1.0f;
    f32  environmentRotation = 0.0f;          ///< graus
    bool environmentBackground = false;       ///< mostra o HDRI como fundo deste objeto
};

/// Os parâmetros da camada de partículas com os keyframes aplicados.
///
/// Cada `ParticleParam` pode ter a própria trilha (`TrackProperty::ParticleParam`,
/// `paramIndex` = o valor do enum). Sem trilha, vale o campo parado — por isso a
/// cópia sai igual à de antes deste controle existir.
[[nodiscard]] inline ParticleData sampled_particles(const Layer& l, FrameIndex local) noexcept {
    ParticleData pd = l.particles;
    auto sample = [&](ParticleParam p, f32 fallback) {
        if (!l.tracks.find(TrackProperty::ParticleParam, kInvalidIndex, static_cast<u32>(p))) return fallback;
        return l.tracks.sample_or(TrackProperty::ParticleParam, local, fallback, kInvalidIndex, static_cast<u32>(p));
    };
    auto count = [&](ParticleParam p, u32 fallback) {
        return static_cast<u32>(std::max(0.0f, std::round(sample(p, static_cast<f32>(fallback)))));
    };
    pd.emitterType = count(ParticleParam::EmitterType, pd.emitterType);
    pd.emitterSize.x = sample(ParticleParam::EmitterWidth, pd.emitterSize.x);
    pd.emitterSize.y = sample(ParticleParam::EmitterHeight, pd.emitterSize.y);
    pd.emitterRadius = sample(ParticleParam::EmitterRadius, pd.emitterRadius);
    pd.emitterRotation = sample(ParticleParam::EmitterRotation, pd.emitterRotation);
    pd.emitterDepth = sample(ParticleParam::EmitterDepth, pd.emitterDepth);
    pd.gridX = count(ParticleParam::GridX, pd.gridX);
    pd.gridY = count(ParticleParam::GridY, pd.gridY);
    pd.emitFill = sample(ParticleParam::EmitFill, pd.emitFill ? 1.0f : 0.0f) > 0.5f;
    pd.emitterOffset.x = sample(ParticleParam::EmitterOffsetX, pd.emitterOffset.x);
    pd.emitterOffset.y = sample(ParticleParam::EmitterOffsetY, pd.emitterOffset.y);
    pd.rate = sample(ParticleParam::Rate, pd.rate);
    pd.burst = count(ParticleParam::Burst, pd.burst);
    pd.lifetime = sample(ParticleParam::Lifetime, pd.lifetime);
    pd.lifeRandom = sample(ParticleParam::LifeRandom, pd.lifeRandom);
    pd.speed = sample(ParticleParam::Speed, pd.speed);
    pd.speedRandom = sample(ParticleParam::SpeedRandom, pd.speedRandom);
    pd.direction = sample(ParticleParam::Direction, pd.direction);
    pd.spread = sample(ParticleParam::Spread, pd.spread);
    pd.inheritVelocity = sample(ParticleParam::InheritVelocity, pd.inheritVelocity);
    pd.seed = count(ParticleParam::Seed, pd.seed);
    pd.particleType = count(ParticleParam::ParticleType, pd.particleType);
    pd.softness = sample(ParticleParam::Softness, pd.softness);
    pd.rotation = sample(ParticleParam::Rotation, pd.rotation);
    pd.rotationRandom = sample(ParticleParam::RotationRandom, pd.rotationRandom);
    pd.spin = sample(ParticleParam::Spin, pd.spin);
    pd.startSize = sample(ParticleParam::StartSize, pd.startSize);
    pd.endSize = sample(ParticleParam::EndSize, pd.endSize);
    pd.startOpacity = sample(ParticleParam::StartOpacity, pd.startOpacity);
    pd.endOpacity = sample(ParticleParam::EndOpacity, pd.endOpacity);
    pd.gravity.x = sample(ParticleParam::GravityX, pd.gravity.x);
    pd.gravity.y = sample(ParticleParam::GravityY, pd.gravity.y);
    pd.gravity.z = sample(ParticleParam::GravityZ, pd.gravity.z);
    pd.drag = sample(ParticleParam::Drag, pd.drag);
    pd.wind.x = sample(ParticleParam::WindX, pd.wind.x);
    pd.wind.y = sample(ParticleParam::WindY, pd.wind.y);
    pd.turbulence = sample(ParticleParam::Turbulence, pd.turbulence);
    pd.turbulenceScale = sample(ParticleParam::TurbulenceScale, pd.turbulenceScale);
    pd.turbulenceSpeed = sample(ParticleParam::TurbulenceSpeed, pd.turbulenceSpeed);
    pd.vortex = sample(ParticleParam::Vortex, pd.vortex);
    pd.attractor = sample(ParticleParam::Attractor, pd.attractor);
    pd.trailLength = sample(ParticleParam::TrailLength, pd.trailLength);
    pd.trailTaper = sample(ParticleParam::TrailTaper, pd.trailTaper);
    pd.auxCount = std::min<u32>(count(ParticleParam::AuxCount, pd.auxCount), 16u);
    pd.auxAt = sample(ParticleParam::AuxAt, pd.auxAt);
    pd.auxLife = sample(ParticleParam::AuxLife, pd.auxLife);
    pd.auxSpeed = sample(ParticleParam::AuxSpeed, pd.auxSpeed);
    pd.auxSize = sample(ParticleParam::AuxSize, pd.auxSize);
    pd.auxSpread = sample(ParticleParam::AuxSpread, pd.auxSpread);
    pd.collision = count(ParticleParam::Collision, pd.collision);
    pd.collisionY = sample(ParticleParam::CollisionY, pd.collisionY);
    pd.collisionBounce = sample(ParticleParam::CollisionBounce, pd.collisionBounce);
    pd.blendMode = count(ParticleParam::BlendMode, pd.blendMode);
    pd.maxParticles = std::max<u32>(1u, count(ParticleParam::MaxParticles, pd.maxParticles));
    pd.emitterSpace = count(ParticleParam::EmitterSpace, pd.emitterSpace);
    pd.emitFrom = count(ParticleParam::EmitFrom, pd.emitFrom);
    pd.auxProbability = sample(ParticleParam::AuxProbability, pd.auxProbability);
    pd.trailWidth = sample(ParticleParam::TrailWidth, pd.trailWidth);
    pd.trailOpacity = sample(ParticleParam::TrailOpacity, pd.trailOpacity);
    pd.sizeRandom = sample(ParticleParam::SizeRandom, pd.sizeRandom);
    pd.opacityRandom = sample(ParticleParam::OpacityRandom, pd.opacityRandom);
    pd.colorRandom = sample(ParticleParam::ColorRandom, pd.colorRandom);
    pd.collisionCenter.x = sample(ParticleParam::CollisionX, pd.collisionCenter.x);
    pd.collisionCenter.z = sample(ParticleParam::CollisionZ, pd.collisionCenter.z);
    pd.collisionRadius = sample(ParticleParam::CollisionRadius, pd.collisionRadius);
    pd.collisionBox.x = sample(ParticleParam::CollisionWidth, pd.collisionBox.x);
    pd.collisionBox.y = sample(ParticleParam::CollisionHeight, pd.collisionBox.y);
    pd.collisionBox.z = sample(ParticleParam::CollisionDepth, pd.collisionBox.z);
    pd.meshScale = sample(ParticleParam::MeshScale, pd.meshScale);
    pd.meshLit = sample(ParticleParam::MeshLit, pd.meshLit ? 1.0f : 0.0f) > 0.5f;
    return pd;
}

/// A ponte dos projetos antigos: Faíscas, Neve e Poeira de luz.
///
/// Os três emitiam SEMPRE de uma caixa — o emissor era o retângulo da camada e
/// o campo `emitterType` nem chegava a ser escrito (ficava no zero). Lido como
/// está, um projeto anterior à v20 reabriria emitindo de um PONTO e a neve
/// viraria um borrifo no centro. Aqui ele volta a ser caixa, que é o que o
/// shader antigo fazia com aquele `emitterSize`.
inline void migrate_legacy_particles(ParticleData& p, u32 timelineVersion) noexcept {
    if (timelineVersion < 20) p.emitterType = static_cast<u32>(ParticleEmitter::Box);
}

} // namespace aurea
