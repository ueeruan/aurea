#pragma once
// =============================================================================
//  Aurea / effects / FaceRigParts.hpp
//
//  Rig 2D de rosto v2 — "separar em partes": como um artista de rig 2D, a
//  imagem da camada vira uma camada por parte (cabelo de trás, corpo,
//  pescoço, rosto, olhos em branco/íris/cílios, sobrancelhas, nariz, boca,
//  franja, mechas, óculos), com o que fica ESCONDIDO atrás de cada parte
//  preenchido (pele sob os olhos e a franja, cabelo atrás do rosto, roupa sob
//  as mechas). Sem IA: cores (Lab) + as âncoras do v1 + topologia.
//
//  As partes somam de volta a imagem: compor todas, de trás para a frente, na
//  pose de repouso, reproduz a entrada (o teste mede). A animação usa os mesmos
//  parâmetros do `aurea.rig.face` (lidos do "Controle do rig"), cada parte com
//  a sua profundidade, mais a física do cabelo (mola amortecida avaliada numa
//  janela fixa de tempo: o mesmo quadro dá sempre o mesmo resultado).
// =============================================================================
#include "aurea/effects/FaceRig.hpp"

#include <vector>

namespace aurea::face_rig {

inline constexpr const char* kFaceRigPartKey = "aurea.rig.part";

/// Papel de uma parte. A ORDEM é a de desenho (de trás para a frente) e é
/// gravada no projeto (parâmetro "role" do aurea.rig.part): só cresce no fim.
enum PartRole : u32 {
    kPartBackground = 0,   ///< fundo de foto sem transparência
    kPartHairBack,
    kPartBody,
    kPartNeck,
    kPartFace,             ///< pele do rosto
    kPartEars,
    kPartEyeWhiteL,        ///< L = lado esquerdo da IMAGEM (como face_rig::kEyeOpenLeft)
    kPartEyeWhiteR,
    kPartIrisL,
    kPartIrisR,
    kPartLashL,            ///< cílios / pálpebra de cima
    kPartLashR,
    kPartBrowL,
    kPartBrowR,
    kPartNose,
    kPartMouth,            ///< lábios + miolo
    kPartHairFront,        ///< franja
    kPartSideLocks,        ///< mechas na frente do rosto e dos ombros
    kPartGlasses,
    kPartRoleCount,
};

/// Parâmetros do `aurea.rig.part` (cada camada de parte). A ordem é gravada
/// no projeto: só cresce no fim.
enum PartParam : u32 {
    kPartParamRole = 0,      ///< oculto: PartRole
    kPartParamDepth,         ///< profundidade (paralaxe), animável
    kPartParamController,    ///< camada "Controle do rig" (nulo com o aurea.rig.face)
    kPartParamOrigin,        ///< oculto: canto do recorte na imagem original (px)
    kPartParamFullSize,      ///< oculto: tamanho da imagem original (px)
    kPartParamLid,           ///< oculto: abertura medida do olho (PartSet::lid)
    kPartParamCount,
};

/// Nome da parte em português (nome da camada criada).
[[nodiscard]] const char* part_name(u32 role) noexcept;
/// Profundidade padrão da parte (paralaxe ao virar/acenar; em raios do rosto).
[[nodiscard]] f32 part_default_depth(u32 role) noexcept;
[[nodiscard]] bool part_is_hair(u32 role) noexcept;
/// Parte da cabeça (gira com "Inclinar" em volta do pescoço)?
[[nodiscard]] bool part_in_head(u32 role) noexcept;

/// Uma parte: recorte RGBA8 de alfa reto, na caixa (x0, y0, w, h) da imagem.
struct Part {
    u32 role = kPartFace;
    u32 x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<u8> rgba;
};

struct PartSet {
    u32 width = 0, height = 0;     ///< tamanho da imagem original
    std::vector<Part> parts;       ///< só as partes não vazias, na ordem de desenho
    /// Papel dominante de cada pixel (diagnóstico/testes; 255 = transparente).
    std::vector<u8> labels;
    bool glasses = false;
    /// Abertura medida de cada olho (topo e linha de baixo do branco + íris), em
    /// frações do raio y do olho a partir do centro: o "lid" das partes do olho.
    Vec2 lid[2]{{-0.6f, 0.7f}, {-0.6f, 0.7f}};
};

/// Andamento (0..1) para a barra da UI; pode ser nulo.
using SplitProgress = void (*)(void* ctx, f32 done);

/// Separa a imagem (RGBA8 de alfa reto, `w`×`h`) em partes, com o fundo de
/// cada uma preenchido onde outra a cobre. Determinístico. false = imagem
/// vazia/pequena demais.
bool split_parts(const u8* rgba, u32 w, u32 h, const Anchors& anchors, PartSet& out,
                 SplitProgress progress = nullptr, void* progressCtx = nullptr);

/// Composição das partes (sobre, de trás para a frente) na pose de repouso,
/// em luz LINEAR como o renderer: RGBA linear de alfa PRÉ-multiplicado (0..1),
/// `width`×`height`×4.
void composite_parts(const PartSet& set, std::vector<f32>& premultiplied);

/// Preenchimento "de artista" (CPU, determinístico): os pixels com
/// `known[i] == 0` dentro de `region[i] != 0` recebem a cor que continua a do
/// redor (pirâmide empurra-puxa + difusão de membrana). `rgb` = 3 floats por
/// pixel, alterados só na região.
void membrane_fill(std::vector<f32>& rgb, const std::vector<u8>& known, const std::vector<u8>& region, u32 w, u32 h);

// -----------------------------------------------------------------------------
//  Movimento das partes
// -----------------------------------------------------------------------------

/// Ponto de REPOUSO `p` (px da imagem original) da parte `role` → deformado.
/// `swing` = deslocamento da física do cabelo (px) já avaliado no instante;
/// `lid` = abertura medida do olho (PartSet::lid) para os cílios.
[[nodiscard]] Vec2 part_displace(const Rig& rig, const Pose& pose, u32 role, f32 depth, Vec2 swing, Vec2 lid, Vec2 p) noexcept;
/// Quanto do balanço do cabelo o ponto recebe (0 na raiz, 1 na ponta).
[[nodiscard]] f32 hair_hang(const Rig& rig, u32 role, Vec2 p) noexcept;
/// Linha da pálpebra do olho `eye` no x de repouso, com fechamento `k` (0..1):
/// o branco e a íris só aparecem ABAIXO dela (cortados, não achatados).
[[nodiscard]] f32 lid_line(const Rig& rig, u32 eye, f32 k, f32 x, Vec2 lid) noexcept;
/// Onde a raiz do cabelo vai com a pose (px): é o que puxa a mola.
[[nodiscard]] Vec2 head_drive(const Rig& rig, const Pose& pose) noexcept;

/// Física do cabelo: mola amortecida que segue `drive(t)` (px, t em s) e
/// devolve quanto a ponta fica para trás no instante `now`. Simula sempre a
/// mesma janela antes de `now` partindo do repouso: sem estado entre quadros
/// (preview = export, ir e voltar dá o mesmo). `amount` 0..1.
template <class Drive>
[[nodiscard]] Vec2 hair_swing(Drive&& drive, f64 now, f32 amount) noexcept;

inline constexpr f64 kHairLookbackSeconds = 1.6;
inline constexpr f64 kHairStepSeconds = 1.0 / 60.0;
inline constexpr f32 kHairFrequency = 1.3f;   ///< Hz
inline constexpr f32 kHairDamping = 0.32f;    ///< razão de amortecimento

template <class Drive>
Vec2 hair_swing(Drive&& drive, f64 now, f32 amount) noexcept {
    if (!(amount > 0.0f) || !std::isfinite(now)) return Vec2{};
    const f32 w = 2.0f * kPi * kHairFrequency;
    const u32 steps = static_cast<u32>(kHairLookbackSeconds / kHairStepSeconds + 0.5);
    const f32 dt = static_cast<f32>(kHairStepSeconds);
    // Tempo absoluto das amostras alinhado à grade de passos: o mesmo `now`
    // sempre passa pelos mesmos instantes.
    const f64 t0 = now - static_cast<f64>(steps) * kHairStepSeconds;
    Vec2 u = drive(t0);
    Vec2 x = u, v{};
    for (u32 i = 1; i <= steps; ++i) {
        u = drive(t0 + static_cast<f64>(i) * kHairStepSeconds);
        // Euler semi-implícito (estável neste passo).
        const Vec2 a = (u - x) * (w * w) - v * (2.0f * kHairDamping * w);
        v += a * dt;
        x += v * dt;
    }
    Vec2 lag = (x - u) * clampf(amount, 0.0f, 1.0f) * 1.6f;
    lag.y *= 0.35f;   // cabelo estica pouco: o balanço é mais de lado
    if (!std::isfinite(lag.x) || !std::isfinite(lag.y)) return Vec2{};
    return lag;
}

} // namespace aurea::face_rig
