// =============================================================================
//  Aurea / tracking / CameraTracker.hpp
//
//  Rastreio de câmera 3D a partir do vídeo:
//
//    quadros (cinza, resolução de análise)
//      → pontos (Shi-Tomasi, espalhados por grade)
//      → rastreio quadro a quadro (Lucas-Kanade em pirâmide, ida e volta)
//      → par inicial (matriz essencial com RANSAC, escolha pela paralaxe)
//      → triangulação + pose de cada quadro (Gauss-Newton robusto)
//      → refinamento alternado (pontos ↔ poses)
//      → FOV por busca (o que dá o menor erro de reprojeção)
//
//  Câmera no tripé (só gira): sem paralaxe não há profundidade — o solve
//  vira "só rotação" (Kabsch com RANSAC entre quadros) e diz isso.
//
//  Opções do After Effects (Shot Type / Solve Method, observadas nos textos
//  do 3D Camera Tracker; o algoritmo é o SfM publicado):
//    - Tipo de tomada: FOV fixa (busca), ZOOM VARIÁVEL (focal por quadro no
//      bundle adjustment, com prior de suavidade na 2ª diferença; no tripé,
//      a focal de cada quadro junto com o giro) ou FOV informada.
//    - Método: automático, típico (matriz essencial), cena quase plana
//      (homografia decomposta — Ma, Soatto, Košecká e Sastry, "An Invitation
//      to 3-D Vision", §5.3 — onde a essencial é degenerada) ou tripé.
//    - Distorção da lente: radial k1 (e k2 quando estável), Brown–Conrady em
//      coordenadas normalizadas pela MEIA ALTURA do quadro (independe da focal
//      e da resolução): p_d = p_u·(1 + k1·r² + k2·r⁴). A câmera resolvida é a
//      sem distorção; o efeito "Distorção da lente" usa o mesmo modelo.
//
//  Convenção: câmera olhando para +Z, X para a direita, Y para baixo (a mesma
//  da câmera do Aurea). Pose = mundo → câmera: Xc = R·Xw + t. Escala
//  relativa (a base inicial vale 1): sem referência não há escala absoluta.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/tracking/PointTracker.hpp"

#include <atomic>
#include <cmath>
#include <string>
#include <vector>

namespace aurea::tracking {

/// Rastros 2D: pos[rastro][quadro] em px da análise; NaN = ausente.
struct Tracks2D {
    u32 frames = 0;
    u32 width = 0, height = 0;
    std::vector<std::vector<Vec2>> pos;
    [[nodiscard]] static bool present(Vec2 p) noexcept { return std::isfinite(p.x) && std::isfinite(p.y); }
};

/// FAST: menos pontos e menos análise; HIGH: mais pontos e mais refinamento.
enum class TrackMode : u8 { Fast = 0, Balanced, High };

/// Detecta e segue pontos quadro a quadro.
class FeatureTracker {
public:
    explicit FeatureTracker(TrackMode mode = TrackMode::Balanced) noexcept;
    void add_frame(const Gray& g);
    [[nodiscard]] const Tracks2D& tracks() const noexcept { return tracks_; }
    [[nodiscard]] u32 active() const noexcept { return static_cast<u32>(active_.size()); }

private:
    struct Active { u32 track; Vec2 pos; };
    void detect(const Gray& g);
    u32 maxFeatures_ = 400;
    f32 minDistance_ = 10.0f;
    std::vector<Gray> prev_;
    std::vector<Active> active_;
    Tracks2D tracks_;
};

struct CameraPose {
    f64 R[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   ///< mundo → câmera, linha a linha
    f64 t[3] = {0, 0, 0};
    bool valid = false;
    /// Centro da câmera no mundo (−Rᵀ·t).
    [[nodiscard]] Vec3 center() const noexcept;
};

/// Tipo de tomada (AE "Shot Type").
enum class ShotType : u8 { FixedFov = 0, VariableZoom = 1, SpecifyFov = 2 };
/// Método do solve (AE "Solve Method"); o número é o `SolveOptions::motion`.
enum class SolveMethod : u8 { Auto = 0, Typical = 1, Tripod = 2, FlatScene = 3 };

struct CameraSolution {
    bool ok = false;
    bool rotationOnly = false;       ///< tripé: sem profundidade, só giro
    f32  fovY = 0.0f;                ///< radianos, vertical (zoom variável: a do 1º quadro)
    std::vector<CameraPose> poses;   ///< um por quadro
    std::vector<Vec3> points;        ///< pontos 3D reconstruídos
    std::vector<u8> trackSolved;     ///< por rastro 2D: entrou no solve (1) ou foi rejeitado (0)
    // Qualidade (nunca maquiada: solve ruim aparece ruim).
    u32 tracks = 0;                  ///< rastros 2D com pelo menos 2 quadros
    u32 inliers = 0;                 ///< pontos 3D usados no solve final
    u32 framesSolved = 0;
    f32 rmsError = 0.0f;             ///< px da análise (raiz da média dos quadrados)
    f32 confidence = 0.0f;           ///< 0..1
    std::string failure;             ///< por que não resolveu
    // Tipo de tomada, método e lente (o que o solve USOU).
    u8 shotType = 0;                 ///< ShotType
    u8 method = 1;                   ///< SolveMethod resolvido: 1 típico, 2 tripé, 3 cena plana
    std::vector<f32> frameFovY;      ///< zoom variável: FOV vertical por quadro (rad); vazio = fovY em todos
    f32 k1 = 0.0f, k2 = 0.0f;        ///< distorção radial (meia altura = 1); 0 = sem distorção
    bool lensSolved = false;         ///< k1 estimado (k2 só quando estável)
    bool k2Solved = false;
    // "Average Error" do AE: média de |reprojeção| em px da FONTE, com a
    // distorção, sobre os pontos resolvidos; por quadro (−1 = sem pose/pontos).
    f32 averageErrorPx = 0.0f;
    std::vector<f32> frameErrorPx;
    [[nodiscard]] f32 fov_at(u32 frame) const noexcept {
        return frame < frameFovY.size() ? frameFovY[frame] : fovY;
    }
};

struct SolveOptions {
    u32 motion = 0;                 ///< SolveMethod: 0 auto, 1 típico (câmera livre), 2 tripé, 3 cena quase plana
    TrackMode mode = TrackMode::Balanced;
    f32 fovMinDeg = 25.0f, fovMaxDeg = 100.0f;
    f32 knownFovDeg = 0.0f;          ///< > 0: FOV conhecida (sem busca) — "Specify Angle of View"
    u32 shotType = 0;                ///< ShotType; 2 exige knownFovDeg > 0 (só knownFovDeg > 0 também vale)
    u32 lens = 0;                    ///< 0 sem distorção, 1 estimar k1 (k2 quando estável)
    f32 sourceScale = 1.0f;          ///< px da fonte por px da análise (erro em px da FONTE)
};

/// Distorção radial (Brown–Conrady) em coordenadas normalizadas pela meia
/// altura do quadro, com o centro óptico na origem: sem → com distorção.
[[nodiscard]] Vec2 lens_distort(Vec2 undistorted, f32 k1, f32 k2) noexcept;
/// O inverso (iteração de ponto fixo; converge para |k1·r²| < ~0,5).
[[nodiscard]] Vec2 lens_undistort(Vec2 distorted, f32 k1, f32 k2) noexcept;
/// Os rastros sem a distorção (px da análise, centro = meio do quadro).
[[nodiscard]] Tracks2D undistort_tracks(const Tracks2D& tracks, f32 k1, f32 k2);

/// Resolve a câmera a partir dos rastros. `cancel`/`progress` opcionais
/// (progress vai de 0 a 1 dentro do solve).
[[nodiscard]] CameraSolution solve_camera(const Tracks2D& tracks, const SolveOptions& opt,
                                          const std::atomic<bool>* cancel = nullptr,
                                          std::atomic<f32>* progress = nullptr);

/// Euler (rx, ry, rz, radianos) na convenção do Aurea (R = Rz·Ry·Rx) da
/// matriz câmera → mundo (colunas = eixos da câmera).
[[nodiscard]] Vec3 euler_zyx_from_matrix(const f64 m[9]) noexcept;

/// Plano dominante (RANSAC) dos pontos: centro e normal. Falso se nenhum
/// plano tem pelo menos `minShare` dos pontos.
bool dominant_plane(const std::vector<Vec3>& pts, f32 tolerance, f32 minShare, Vec3& centroid, Vec3& normal);

} // namespace aurea::tracking
