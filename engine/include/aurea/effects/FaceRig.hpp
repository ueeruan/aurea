#pragma once
// =============================================================================
//  Aurea / effects / FaceRig.hpp
//
//  "Rig 2D de rosto" (`aurea.rig.face`): o rosto de QUALQUER imagem (foto ou
//  anime) anima por parâmetros, como um rig de parâmetros 2D — virar, acenar,
//  inclinar, abrir/fechar cada olho, abrir a boca, sorrir, sobrancelhas, corpo
//  e respiração. Sem IA: os pontos (âncoras) saem dos pixels da camada
//  (`auto_place`) ou da mão do usuário ("Ajustar pontos").
//
//  A deformação é um campo analítico (`displace`) avaliado nos vértices de uma
//  grade densa que segue as linhas dos olhos e da boca; a GPU desenha a grade
//  num passe só (shaders/effects/face_rig.*). O mesmo cálculo serve aos testes
//  e ao palco. Tudo em px da camada; as âncoras ficam nos parâmetros ocultos em
//  FRAÇÃO da caixa da camada (a mesma pose em qualquer resolução).
// =============================================================================
#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"

#include <vector>

namespace aurea::face_rig {

inline constexpr const char* kFaceRigKey = "aurea.rig.face";

/// Índices dos parâmetros (ordem gravada no projeto: só cresce no fim).
enum Param : u32 {
    kTurn = 0,        ///< virar a cabeça (graus, −30..30; + = para a direita da imagem)
    kNod,             ///< acenar (graus; + = olhar para baixo)
    kTilt,            ///< inclinar (graus; + = horário na tela)
    kEyeOpenLeft,     ///< olho do lado ESQUERDO da imagem, 0..1
    kEyeOpenRight,    ///< olho do lado direito da imagem, 0..1
    kMouthOpen,       ///< 0..1
    kSmile,           ///< −1..1
    kBrowLeft,        ///< −1..1 (+ = sobe)
    kBrowRight,
    kBodySway,        ///< graus, −10..10
    kBreathing,       ///< quantidade 0..1
    kBreathingRate,   ///< respirações por minuto
    // --- Âncoras (ocultas, fração da camada) ---------------------------------
    kFaceCenter,
    kFaceSize,        ///< raios (x, y) da elipse do rosto
    kEyeLeft,
    kEyeLeftSize,     ///< meia largura, meia altura
    kEyeRight,
    kEyeRightSize,
    kMouth,
    kMouthSize,
    kNeck,            ///< queixo/pescoço: pivô da inclinação
    kHairTop,         ///< topo do cabelo
    kPlaced,          ///< 0 = padrão, 1 = só olhos achados, 2 = olhos e boca, 3 = ajustado à mão
    // --- v2: partes separadas (FaceRigParts.hpp) ---------------------------------
    kHairPhysics,     ///< "Física do cabelo" 0..1 (só no Controle do rig)
    kPartsRig,        ///< oculto: 1 = este efeito é o Controle do rig das partes
    kParamCount,
};

inline constexpr u32 kFirstAnchor = kFaceCenter;
inline constexpr u32 kAnchorCount = kHairTop - kFaceCenter + 1;   // 10 pontos
/// Alças do palco: o centro/canto de cada parte (fração da camada).
enum Handle : u32 {
    kHandleFace = 0,      ///< centro do rosto
    kHandleFaceEdge,      ///< canto da elipse (centro + raios)
    kHandleEyeLeft,
    kHandleEyeLeftEdge,
    kHandleEyeRight,
    kHandleEyeRightEdge,
    kHandleMouth,
    kHandleMouthEdge,
    kHandleNeck,
    kHandleHairTop,
    kHandleCount,
};

/// O rig em px da camada.
struct Rig {
    Vec2 face{}, faceR{};
    Vec2 eye[2]{}, eyeR[2]{};    ///< 0 = esquerda da imagem, 1 = direita
    Vec2 mouth{}, mouthR{};
    Vec2 neck{}, hairTop{};
    f32 width = 1.0f, height = 1.0f;   ///< caixa da camada
};

/// Pose no instante (ângulos em radianos; respiração já avaliada no tempo).
struct Pose {
    f32 turn = 0.0f, nod = 0.0f, tilt = 0.0f;
    f32 eyeOpen[2] = {1.0f, 1.0f};
    f32 mouthOpen = 0.0f, smile = 0.0f;
    f32 brow[2] = {0.0f, 0.0f};
    f32 sway = 0.0f;
    f32 breath = 0.0f;   ///< deslocamento da respiração agora, em fração do raio y do rosto
};

/// Âncoras em fração da camada (o que os parâmetros guardam).
struct Anchors {
    Vec2 face{0.5f, 0.45f}, faceR{0.30f, 0.33f};
    Vec2 eye[2]{{0.38f, 0.40f}, {0.62f, 0.40f}};
    Vec2 eyeR[2]{{0.060f, 0.045f}, {0.060f, 0.045f}};
    Vec2 mouth{0.5f, 0.58f}, mouthR{0.050f, 0.016f};
    Vec2 neck{0.5f, 0.80f}, hairTop{0.5f, 0.04f};
};

/// Âncoras proporcionais de uma caixa de rosto (fração): olhos a 42 % da altura
/// e a 30 %/70 % da largura, boca a 70 %. É o padrão e o plano B da busca.
[[nodiscard]] Anchors proportional_anchors(f32 x0, f32 y0, f32 x1, f32 y1) noexcept;
[[nodiscard]] Rig rig_of(const Anchors& a, f32 width, f32 height) noexcept;
[[nodiscard]] bool is_neutral(const Pose& p) noexcept;
/// Respiração no instante (`seconds`): 0..amount, começando em 0.
[[nodiscard]] f32 breath_at(f32 amount, f32 perMinute, f64 seconds) noexcept;

/// Ponto deformado do ponto de repouso `p` (px da camada). Pose neutra = `p`.
[[nodiscard]] Vec2 displace(const Rig& rig, const Pose& pose, Vec2 p) noexcept;

// --- Peças do campo, também usadas pelas partes separadas (FaceRigParts) ----
/// Linhas do olho em repouso: faixa de pele acima, topo (cílios), linha de baixo.
struct EyeLines {
    f32 band, top, low;
};
[[nodiscard]] EyeLines eye_lines(Vec2 center, Vec2 radius) noexcept;
/// Profundidade (fração do raio do rosto) da superfície em `p` (virar/acenar).
[[nodiscard]] f32 depth_at(const Rig& rig, Vec2 p) noexcept;
/// Deslocamento vertical da boca (abrir e sorrir) no ponto `p`.
[[nodiscard]] f32 mouth_dy(const Rig& rig, f32 open, f32 smile, Vec2 p) noexcept;
/// Deslocamento vertical da sobrancelha do olho `eye` no ponto `p`.
[[nodiscard]] f32 brow_dy(const Rig& rig, u32 eye, f32 brow, Vec2 p) noexcept;

/// Grade de repouso: linhas uniformes mais as linhas-chave dos olhos, da
/// sobrancelha e da boca (a dobra do campo cai em cima de uma linha da grade —
/// olho pequeno de foto também fecha certo). `xs`/`ys` em px, crescentes.
void grid_lines(const Rig& rig, u32 uniform, std::vector<f32>& xs, std::vector<f32>& ys);

/// Busca das âncoras nos pixels (RGBA8 sem pré-multiplicar, `w`×`h`). Sem IA:
/// silhueta pelo alfa, pele, o par de manchas escuras mais simétrico no alto do
/// rosto (olhos) e uma mancha escura/vermelha horizontal abaixo (boca).
/// Devolve 2 = olhos e boca, 1 = só olhos, 0 = proporções (plano B).
u32 auto_place(const u8* rgba, u32 w, u32 h, Anchors& out);

} // namespace aurea::face_rig
