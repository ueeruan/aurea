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
//  Convenção: câmera olhando para +Z, X para a direita, Y para baixo (a mesma
//  da câmera do Aurea). Pose = mundo → câmera: Xc = R·Xw + t. Escala
//  relativa (a base inicial vale 1): sem referência não há escala absoluta.
// =============================================================================
#pragma once

#include "aurea/core/Math.hpp"
#include "aurea/core/Types.hpp"
#include "aurea/tracking/PointTracker.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace aurea::tracking {

/// Rastros 2D: pos[rastro][quadro] em px da análise; NaN = ausente.
struct Tracks2D {
    u32 frames = 0;
    u32 width = 0, height = 0;
    std::vector<std::vector<Vec2>> pos;
    [[nodiscard]] static bool present(Vec2 p) noexcept { return p.x == p.x; }
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

struct CameraSolution {
    bool ok = false;
    bool rotationOnly = false;       ///< tripé: sem profundidade, só giro
    f32  fovY = 0.0f;                ///< radianos, vertical
    std::vector<CameraPose> poses;   ///< um por quadro
    std::vector<Vec3> points;        ///< pontos 3D reconstruídos
    // Qualidade (nunca maquiada: solve ruim aparece ruim).
    u32 tracks = 0;                  ///< rastros 2D com pelo menos 2 quadros
    u32 inliers = 0;                 ///< pontos 3D usados no solve final
    u32 framesSolved = 0;
    f32 rmsError = 0.0f;             ///< px da análise
    f32 confidence = 0.0f;           ///< 0..1
    std::string failure;             ///< por que não resolveu
};

struct SolveOptions {
    TrackMode mode = TrackMode::Balanced;
    f32 fovMinDeg = 25.0f, fovMaxDeg = 100.0f;
    f32 knownFovDeg = 0.0f;          ///< > 0: FOV conhecida (sem busca)
};

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
