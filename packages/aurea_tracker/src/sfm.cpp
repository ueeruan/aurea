// O SOLVER DE CAMERA DO RASTREIO 3D.
//
// Troca o solver em Dart (8 pontos, sem ajuste de feixes, focal em grade,
// pose a 8 quadros/s) por uma reconstrucao incremental de verdade:
//   1. quadros-chave pela paralaxe dos rastros;
//   2. par inicial pela essencial (8 pontos + RANSAC) OU pela homografia
//      decomposta — e a homografia que resolve o chao plano, o caso em que
//      o 8 pontos degenera e o solver antigo recusava;
//   3. triangulacao com angulo minimo e cheiralidade;
//   4. registro dos quadros-chave seguintes por PnP (DLT + RANSAC e
//      Gauss-Newton robusto);
//   5. AJUSTE DE FEIXES (Levenberg-Marquardt com complemento de Schur) com a
//      FOCAL livre, local durante a reconstrucao e global no fim;
//   6. pose de TODO quadro analisado (nao so dos quadros-chave);
//   7. tripe (gira sem andar): rotacao por quadro e focal pela homografia de
//      rotacao pura, em vez de recusar.
// Convencoes (as do app): Xcam = R*Xmundo + t, camera olhando +Z, Y para
// baixo na imagem, focal em px do quadro analisado, centro no meio.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "algebra.hpp"
#include "tracker.h"

namespace att {
namespace {

struct Pose {
  M3 R;
  V3 t;
  bool ok = false;
};

struct Obs {
  int frame;
  double x, y;
};

struct Track {
  int id;
  std::vector<Obs> obs;  // em ordem de quadro
  V3 X;
  bool ok = false;
  double err = 0;
  int views = 0;
};

inline V3 bearing(double x, double y, double f, double cx, double cy) {
  return {(x - cx) / f, (y - cy) / f, 1.0};
}

inline bool reproj(const Pose& p, const V3& X, double f, double cx, double cy,
                   double u, double v, double& ex, double& ey) {
  const V3 c = p.R * X + p.t;
  if (c.z <= 1e-6) return false;
  ex = cx + f * c.x / c.z - u;
  ey = cy + f * c.y / c.z - v;
  return true;
}

// Triangulacao linear em N vistas (coordenadas normalizadas).
bool triangulate(const std::vector<const Pose*>& ps, const std::vector<V3>& bs,
                 V3& out) {
  const int n = static_cast<int>(ps.size());
  if (n < 2) return false;
  std::vector<double> a(n * 2 * 4);
  for (int i = 0; i < n; i++) {
    const M3& R = ps[i]->R;
    const V3& t = ps[i]->t;
    const double P[3][4] = {{R(0, 0), R(0, 1), R(0, 2), t.x},
                            {R(1, 0), R(1, 1), R(1, 2), t.y},
                            {R(2, 0), R(2, 1), R(2, 2), t.z}};
    for (int k = 0; k < 4; k++) {
      a[(i * 2) * 4 + k] = bs[i].x * P[2][k] - P[0][k];
      a[(i * 2 + 1) * 4 + k] = bs[i].y * P[2][k] - P[1][k];
    }
  }
  const auto h = nullVector(a, n * 2, 4);
  if (std::fabs(h[3]) < 1e-12) return false;
  out = {h[0] / h[3], h[1] / h[3], h[2] / h[3]};
  return true;
}

// Angulo entre os raios de dois centros ate o ponto (graus).
double parallaxDeg(const Pose& a, const Pose& b, const V3& X) {
  const V3 ca = a.R.t() * (a.t * -1.0), cb = b.R.t() * (b.t * -1.0);
  const V3 ra = (X - ca).unit(), rb = (X - cb).unit();
  return std::acos(std::clamp(ra.dot(rb), -1.0, 1.0)) * 57.29577951308232;
}

M3 essential8(const std::vector<V3>& a, const std::vector<V3>& b,
              const int* idx, int n) {
  std::vector<double> A(n * 9);
  for (int k = 0; k < n; k++) {
    const V3& p = a[idx[k]];
    const V3& q = b[idx[k]];
    const double row[9] = {q.x * p.x, q.x * p.y, q.x, q.y * p.x, q.y * p.y,
                           q.y,       p.x,       p.y, 1};
    std::copy(row, row + 9, A.begin() + k * 9);
  }
  const auto e = nullVector(A, n, 9);
  M3 E;
  for (int i = 0; i < 9; i++) E.m[i] = e[i];
  M3 u, v;
  V3 s;
  svd3(E, u, s, v);
  M3 d = M3::zero();
  d(0, 0) = d(1, 1) = 1;
  return u * d * v.t();
}

double sampson(const M3& E, const V3& a, const V3& b) {
  const V3 Ea = E * a, Etb = E.t() * b;
  const double num = b.dot(Ea);
  const double den =
      Ea.x * Ea.x + Ea.y * Ea.y + Etb.x * Etb.x + Etb.y * Etb.y;
  return num * num / std::max(den, 1e-30);
}

M3 homography4(const std::vector<V3>& a, const std::vector<V3>& b,
               const int* idx, int n) {
  std::vector<double> A(n * 2 * 9, 0);
  for (int k = 0; k < n; k++) {
    const V3& p = a[idx[k]];
    const V3& q = b[idx[k]];
    const double r1[9] = {-p.x, -p.y, -1, 0, 0, 0, q.x * p.x, q.x * p.y, q.x};
    const double r2[9] = {0, 0, 0, -p.x, -p.y, -1, q.y * p.x, q.y * p.y, q.y};
    std::copy(r1, r1 + 9, A.begin() + (2 * k) * 9);
    std::copy(r2, r2 + 9, A.begin() + (2 * k + 1) * 9);
  }
  const auto h = nullVector(A, n * 2, 9);
  M3 H;
  for (int i = 0; i < 9; i++) H.m[i] = h[i];
  return H;
}

double transferErr(const M3& H, const V3& a, const V3& b) {
  const V3 m = H * a;
  if (std::fabs(m.z) < 1e-12) return 1e9;
  const double dx = m.x / m.z - b.x, dy = m.y / m.z - b.y;
  return dx * dx + dy * dy;
}

// Candidatos (R, t) de uma homografia em coordenadas normalizadas
// (Faugeras e Lustman): oito solucoes, a cheiralidade escolhe.
void decomposeH(const M3& Hn, std::vector<std::pair<M3, V3>>& out) {
  M3 U, V;
  V3 s;
  svd3(Hn, U, s, V);
  if (s.y < 1e-12) return;
  const double d1 = s.x / s.y, d3 = s.z / s.y;
  if (d1 - d3 < 1e-5) return;  // rotacao pura: sem translacao a medir
  const double x1 = std::sqrt(std::max(0.0, (d1 * d1 - 1) / (d1 * d1 - d3 * d3)));
  const double x3 = std::sqrt(std::max(0.0, (1 - d3 * d3) / (d1 * d1 - d3 * d3)));
  const double e1s[4] = {1, 1, -1, -1}, e3s[4] = {1, -1, 1, -1};
  const double sgn = U.det() * V.det();
  // d' > 0
  const double sinT = std::sqrt(std::max(0.0, (d1 * d1 - 1) * (1 - d3 * d3))) / (d1 + d3);
  const double cosT = (d3 * d1 + 1) / (d1 + d3);
  for (int k = 0; k < 4; k++) {
    M3 Rp;
    Rp(0, 0) = cosT;
    Rp(0, 2) = -e1s[k] * e3s[k] * sinT;
    Rp(2, 0) = e1s[k] * e3s[k] * sinT;
    Rp(2, 2) = cosT;
    const V3 tp = V3{e1s[k] * x1, 0, -e3s[k] * x3} * (d1 - d3);
    M3 R = U * Rp * V.t();
    for (double& x : R.m) x *= sgn;
    out.push_back({nearestRotation(R), U * tp});
  }
  // d' < 0
  const double sinP = std::sqrt(std::max(0.0, (d1 * d1 - 1) * (1 - d3 * d3))) / (d1 - d3);
  const double cosP = (d1 * d3 - 1) / (d1 - d3);
  for (int k = 0; k < 4; k++) {
    M3 Rp = M3::zero();
    Rp(0, 0) = cosP;
    Rp(0, 2) = e1s[k] * e3s[k] * sinP;
    Rp(1, 1) = -1;
    Rp(2, 0) = e1s[k] * e3s[k] * sinP;
    Rp(2, 2) = -cosP;
    const V3 tp = V3{e1s[k] * x1, 0, e3s[k] * x3} * (d1 + d3);
    M3 R = U * Rp * V.t();
    for (double& x : R.m) x *= sgn;
    out.push_back({nearestRotation(R), U * tp});
  }
}

// Pose so (6 parametros) por Gauss-Newton com Huber, a partir de `p`.
void refinePose(Pose& p, const std::vector<V3>& X,
                const std::vector<std::array<double, 2>>& uv, double f,
                double cx, double cy, int iters, double huber) {
  for (int it = 0; it < iters; it++) {
    std::vector<double> H(36, 0), g(6, 0);
    for (size_t i = 0; i < X.size(); i++) {
      const V3 rx = p.R * X[i];
      const V3 c = rx + p.t;
      if (c.z <= 1e-6) continue;
      const double iz = 1 / c.z;
      const double rx_ = cx + f * c.x * iz - uv[i][0];
      const double ry_ = cy + f * c.y * iz - uv[i][1];
      const double e = std::sqrt(rx_ * rx_ + ry_ * ry_);
      const double w = e <= huber ? 1 : huber / e;
      const double jp[2][3] = {{f * iz, 0, -f * c.x * iz * iz},
                               {0, f * iz, -f * c.y * iz * iz}};
      const M3 sk = skew(rx);
      double J[2][6];
      for (int r = 0; r < 2; r++) {
        for (int k = 0; k < 3; k++) {
          double dw = 0;
          for (int m = 0; m < 3; m++) dw += jp[r][m] * -sk(m, k);
          J[r][k] = dw;
          J[r][3 + k] = jp[r][k];
        }
      }
      const double res[2] = {rx_, ry_};
      for (int r = 0; r < 2; r++)
        for (int a = 0; a < 6; a++) {
          g[a] += w * J[r][a] * res[r];
          for (int b = 0; b < 6; b++) H[a * 6 + b] += w * J[r][a] * J[r][b];
        }
    }
    for (int a = 0; a < 6; a++) H[a * 6 + a] *= 1.0001, H[a * 6 + a] += 1e-9;
    std::vector<double> d(6);
    for (int a = 0; a < 6; a++) d[a] = -g[a];
    if (!choleskySolve(H, 6, d)) return;
    p.R = expSO3({d[0], d[1], d[2]}) * p.R;
    p.t = p.t + V3{d[3], d[4], d[5]};
    if (std::fabs(d[0]) + std::fabs(d[1]) + std::fabs(d[2]) +
            std::fabs(d[3]) + std::fabs(d[4]) + std::fabs(d[5]) <
        1e-10)
      return;
  }
}

// PnP linear (DLT, >= 6 pontos) em coordenadas normalizadas.
bool pnpDLT(const std::vector<V3>& X, const std::vector<V3>& b, const int* idx,
            int n, Pose& out) {
  std::vector<double> A(n * 2 * 12, 0);
  for (int k = 0; k < n; k++) {
    const V3& P = X[idx[k]];
    const V3& q = b[idx[k]];
    const double r1[12] = {P.x, P.y, P.z, 1, 0, 0, 0, 0,
                           -q.x * P.x, -q.x * P.y, -q.x * P.z, -q.x};
    const double r2[12] = {0, 0, 0, 0, P.x, P.y, P.z, 1,
                           -q.y * P.x, -q.y * P.y, -q.y * P.z, -q.y};
    std::copy(r1, r1 + 12, A.begin() + (2 * k) * 12);
    std::copy(r2, r2 + 12, A.begin() + (2 * k + 1) * 12);
  }
  const auto h = nullVector(A, n * 2, 12);
  M3 M;
  M(0, 0) = h[0], M(0, 1) = h[1], M(0, 2) = h[2];
  M(1, 0) = h[4], M(1, 1) = h[5], M(1, 2) = h[6];
  M(2, 0) = h[8], M(2, 1) = h[9], M(2, 2) = h[10];
  V3 tt{h[3], h[7], h[11]};
  double det = M.det();
  if (std::fabs(det) < 1e-18) return false;
  if (det < 0) {
    for (double& x : M.m) x = -x;
    tt = tt * -1.0;
    det = -det;
  }
  const double escala = std::cbrt(det);
  M3 Ms = M;
  for (double& x : Ms.m) x /= escala;
  out.R = nearestRotation(Ms);
  out.t = tt * (1 / escala);
  out.ok = true;
  return true;
}

class Solver {
 public:
  int W, H, F, fps;
  double cx, cy;
  std::vector<Track> tracks;
  std::vector<std::vector<std::pair<int, int>>> porQuadro;  // (track, obs)
  // Saida
  double focal = 0, erro = 0;
  bool tripe = false;
  std::vector<Pose> poses;

  Solver(int w, int h, int frames, int fps_)
      : W(w), H(h), F(frames), fps(fps_), cx(w / 2.0), cy(h / 2.0) {}

  void indexar() {
    porQuadro.assign(F, {});
    for (int ti = 0; ti < static_cast<int>(tracks.size()); ti++) {
      auto& o = tracks[ti].obs;
      std::sort(o.begin(), o.end(),
                [](const Obs& a, const Obs& b) { return a.frame < b.frame; });
      for (int k = 0; k < static_cast<int>(o.size()); k++)
        if (o[k].frame >= 0 && o[k].frame < F)
          porQuadro[o[k].frame].push_back({ti, k});
    }
  }

  const Obs* obsEm(int ti, int frame) const {
    const auto& o = tracks[ti].obs;
    auto it = std::lower_bound(o.begin(), o.end(), frame,
                               [](const Obs& a, int f) { return a.frame < f; });
    return it != o.end() && it->frame == frame ? &*it : nullptr;
  }

  // Quadros-chave: anda enquanto a paralaxe media dos rastros comuns com o
  // ultimo chave e pequena, com teto de distancia.
  std::vector<int> quadrosChave() {
    std::vector<int> k;
    int ultimo = -1;
    for (int f = 0; f < F; f++) {
      if (porQuadro[f].size() < 12) continue;
      if (ultimo < 0) {
        k.push_back(f);
        ultimo = f;
        continue;
      }
      std::vector<double> desl;
      for (auto [ti, oi] : porQuadro[f]) {
        const Obs* a = obsEm(ti, ultimo);
        if (!a) continue;
        const Obs& b = tracks[ti].obs[oi];
        desl.push_back(std::hypot(b.x - a->x, b.y - a->y));
      }
      const int comuns = static_cast<int>(desl.size());
      double med = 0;
      if (!desl.empty()) {
        std::nth_element(desl.begin(), desl.begin() + comuns / 2, desl.end());
        med = desl[comuns / 2];
      }
      const bool longe = med > 0.025 * W;
      const bool perdendo = comuns < 0.55 * porQuadro[ultimo].size();
      if (longe || perdendo || f - ultimo >= 12 || f == F - 1) {
        if (comuns < 10 && f != F - 1) {
          // Corte ou borrao: ancora aqui para nao atravessar o buraco.
          k.push_back(f);
        } else {
          k.push_back(f);
        }
        ultimo = f;
      }
    }
    return k;
  }

  struct Par {
    std::vector<int> ti;
    std::vector<V3> a, b;
  };

  Par par(int fa, int fb, double f) const {
    Par p;
    for (auto [ti, oi] : porQuadro[fb]) {
      const Obs* o = obsEm(ti, fa);
      if (!o) continue;
      const Obs& q = tracks[ti].obs[oi];
      p.ti.push_back(ti);
      p.a.push_back(bearing(o->x, o->y, f, cx, cy));
      p.b.push_back(bearing(q.x, q.y, f, cx, cy));
    }
    return p;
  }

  // Inicializa com (fa, fb): devolve pontos triangulados (ou 0).
  int inicializar(int fa, int fb, double f, Pose& pa, Pose& pb,
                  double& paralaxeMediana) {
    const Par p = par(fa, fb, f);
    const int n = static_cast<int>(p.ti.size());
    if (n < 15) return 0;
    Rng rng(static_cast<unsigned long long>(fa * 7919 + fb * 104729 + 17));
    const double thr = std::pow(1.5 / f, 2);
    M3 bestE;
    int bestE_n = -1;
    M3 bestH;
    int bestH_n = -1;
    int idx[8];
    for (int it = 0; it < 350; it++) {
      for (int k = 0; k < 8; k++) idx[k] = rng.below(n);
      const M3 E = essential8(p.a, p.b, idx, 8);
      int c = 0;
      for (int i = 0; i < n; i++) c += sampson(E, p.a[i], p.b[i]) < thr;
      if (c > bestE_n) bestE_n = c, bestE = E;
      const M3 Hm = homography4(p.a, p.b, idx, 4);
      int ch = 0;
      for (int i = 0; i < n; i++) ch += transferErr(Hm, p.a[i], p.b[i]) < thr * 4;
      if (ch > bestH_n) bestH_n = ch, bestH = Hm;
    }
    // Refaz cada modelo com os proprios inliers.
    std::vector<int> inE, inH;
    for (int i = 0; i < n; i++) {
      if (sampson(bestE, p.a[i], p.b[i]) < thr) inE.push_back(i);
      if (transferErr(bestH, p.a[i], p.b[i]) < thr * 4) inH.push_back(i);
    }
    if (inE.size() >= 8) bestE = essential8(p.a, p.b, inE.data(), (int)inE.size());
    if (inH.size() >= 4) bestH = homography4(p.a, p.b, inH.data(), (int)inH.size());

    // ROTACAO PURA explica tanto quanto a essencial? Entao nao ha paralaxe
    // neste par: o que parecia profundidade era ruido (tripe).
    {
      M3 Hs = M3::zero();
      for (int i : inE) {
        const V3 a = p.a[i].unit(), b = p.b[i].unit();
        const double va[3] = {a.x, a.y, a.z}, vb[3] = {b.x, b.y, b.z};
        for (int r = 0; r < 3; r++)
          for (int c = 0; c < 3; c++) Hs(r, c) += vb[r] * va[c];
      }
      const M3 Rr = nearestRotation(Hs);
      int rot = 0;
      for (int i : inE) {
        const V3 q = Rr * p.a[i];
        if (q.z <= 1e-9) continue;
        const double dx = (q.x / q.z - p.b[i].x) * f, dy = (q.y / q.z - p.b[i].y) * f;
        rot += dx * dx + dy * dy < 1.5 * 1.5;
      }
      if (rot >= 0.92 * inE.size()) return 0;
    }

    std::vector<std::pair<M3, V3>> cands;
    {
      M3 U, V;
      V3 s;
      svd3(bestE, U, s, V);
      M3 Wm = M3::zero();
      Wm(0, 1) = -1, Wm(1, 0) = 1, Wm(2, 2) = 1;
      M3 R1 = U * Wm * V.t(), R2 = U * Wm.t() * V.t();
      if (R1.det() < 0) for (double& x : R1.m) x = -x;
      if (R2.det() < 0) for (double& x : R2.m) x = -x;
      V3 u3{U(0, 2), U(1, 2), U(2, 2)};
      for (const M3& R : {R1, R2})
        for (double sg : {1.0, -1.0}) cands.push_back({R, u3 * sg});
    }
    // O CHAO PLANO: quando a homografia explica quase tudo, as solucoes
    // dela entram na disputa (o 8 pontos degenera com pontos coplanares).
    if (bestH_n >= 0.8 * bestE_n) decomposeH(bestH, cands);

    int melhor = 0;
    double melhorScore = -1;
    for (const auto& [R, t] : cands) {
      Pose A;
      A.ok = true;
      Pose B;
      B.R = R;
      B.t = t.unit();
      B.ok = true;
      int bons = 0;
      std::vector<double> angs;
      for (int i = 0; i < n; i++) {
        V3 X;
        if (!triangulate({&A, &B}, {p.a[i], p.b[i]}, X)) continue;
        const V3 c2 = B.R * X + B.t;
        if (X.z <= 0 || c2.z <= 0) continue;
        double ex, ey;
        if (!reproj(B, X, f, cx, cy, cx + f * p.b[i].x, cy + f * p.b[i].y, ex, ey)) continue;
        if (ex * ex + ey * ey > 16) continue;
        bons++;
        angs.push_back(parallaxDeg(A, B, X));
      }
      if (bons < 12) continue;
      std::sort(angs.begin(), angs.end());
      const double med = angs[angs.size() / 2];
      const double score = bons * std::min(med, 6.0);
      if (score > melhorScore) {
        melhorScore = score;
        melhor = bons;
        pa = A;
        pb = B;
        paralaxeMediana = med;
      }
    }
    return melhorScore < 0 ? 0 : melhor;
  }

  int triangularNovos(const std::vector<int>& chaves, double f, double minAng) {
    int novos = 0;
    for (auto& tr : tracks) {
      if (tr.ok) continue;
      std::vector<const Pose*> ps;
      std::vector<V3> bs;
      for (int k : chaves) {
        if (!poses[k].ok) continue;
        auto it = std::lower_bound(tr.obs.begin(), tr.obs.end(), k,
                                   [](const Obs& a, int fr) { return a.frame < fr; });
        if (it == tr.obs.end() || it->frame != k) continue;
        ps.push_back(&poses[k]);
        bs.push_back(bearing(it->x, it->y, f, cx, cy));
      }
      if (ps.size() < 2) continue;
      V3 X;
      if (!triangulate(ps, bs, X)) continue;
      double maxAng = 0;
      bool bom = true;
      for (size_t i = 0; i < ps.size() && bom; i++) {
        const V3 c = ps[i]->R * X + ps[i]->t;
        if (c.z <= 1e-6) bom = false;
        double ex, ey;
        if (bom && reproj(*ps[i], X, f, cx, cy, cx + f * bs[i].x, cy + f * bs[i].y, ex, ey) &&
            ex * ex + ey * ey > 16)
          bom = false;
        if (i > 0) maxAng = std::max(maxAng, parallaxDeg(*ps[0], *ps[i], X));
      }
      if (!bom || maxAng < minAng) continue;
      tr.X = X;
      tr.ok = true;
      novos++;
    }
    return novos;
  }

  bool registrar(int frame, double f, const Pose* palpite, double limiarPx) {
    std::vector<V3> X, bs;
    std::vector<std::array<double, 2>> uv;
    for (auto [ti, oi] : porQuadro[frame]) {
      if (!tracks[ti].ok) continue;
      const Obs& o = tracks[ti].obs[oi];
      X.push_back(tracks[ti].X);
      bs.push_back(bearing(o.x, o.y, f, cx, cy));
      uv.push_back({o.x, o.y});
    }
    const int n = static_cast<int>(X.size());
    if (n < 8) return false;
    auto inliers = [&](const Pose& p, std::vector<int>* lista) {
      int c = 0;
      for (int i = 0; i < n; i++) {
        double ex, ey;
        if (reproj(p, X[i], f, cx, cy, uv[i][0], uv[i][1], ex, ey) &&
            ex * ex + ey * ey < limiarPx * limiarPx) {
          c++;
          if (lista) lista->push_back(i);
        }
      }
      return c;
    };
    Pose melhor;
    int melhor_n = -1;
    if (palpite && palpite->ok) {
      Pose p = *palpite;
      refinePose(p, X, uv, f, cx, cy, 12, 2.0);
      melhor_n = inliers(p, nullptr);
      melhor = p;
    }
    if (melhor_n < 0.7 * n && n >= 8) {
      Rng rng(static_cast<unsigned long long>(frame * 2654435761ULL + 3));
      int idx[6];
      for (int it = 0; it < 200; it++) {
        for (int k = 0; k < 6; k++) idx[k] = rng.below(n);
        Pose p;
        if (!pnpDLT(X, bs, idx, 6, p)) continue;
        const int c = inliers(p, nullptr);
        if (c > melhor_n) melhor_n = c, melhor = p;
      }
    }
    if (melhor_n < std::max(8, n / 3)) return false;
    std::vector<int> in;
    inliers(melhor, &in);
    std::vector<V3> Xi;
    std::vector<std::array<double, 2>> uvi;
    for (int i : in) Xi.push_back(X[i]), uvi.push_back(uv[i]);
    refinePose(melhor, Xi, uvi, f, cx, cy, 15, 1.5);
    melhor.ok = true;
    poses[frame] = melhor;
    return true;
  }

  // AJUSTE DE FEIXES: poses dos quadros de `livres` (a primeira fica fixa
  // como referencia), pontos vistos por elas, e a focal se `focalLivre`.
  double ajustarFeixes(const std::vector<int>& quadros, bool focalLivre,
                       int iteracoes, double& f) {
    if (quadros.size() < 2) return 0;
    std::vector<int> cams;  // quadros com pose
    for (int q : quadros)
      if (poses[q].ok) cams.push_back(q);
    if (cams.size() < 2) return 0;
    const int nc = static_cast<int>(cams.size());
    std::unordered_map<int, int> camIdx;
    for (int i = 0; i < nc; i++) camIdx[cams[i]] = i;
    // Pontos observados em >= 2 cameras do conjunto.
    struct ObsBA { int cam; double u, v; };
    std::vector<int> pts;
    std::vector<std::vector<ObsBA>> obsPt;
    for (int ti = 0; ti < static_cast<int>(tracks.size()); ti++) {
      if (!tracks[ti].ok) continue;
      std::vector<ObsBA> o;
      for (const Obs& ob : tracks[ti].obs) {
        auto it = camIdx.find(ob.frame);
        if (it != camIdx.end()) o.push_back({it->second, ob.x, ob.y});
      }
      if (o.size() < 2) continue;
      pts.push_back(ti);
      obsPt.push_back(std::move(o));
    }
    const int np = static_cast<int>(pts.size());
    if (np < 8) return 0;
    // Parametros: camera 0 fixa; cameras 1..nc-1 com 6 cada; focal no fim.
    const int ncp = (nc - 1) * 6 + (focalLivre ? 1 : 0);
    auto custo = [&](double fv) {
      double c = 0;
      for (int j = 0; j < np; j++)
        for (const auto& o : obsPt[j]) {
          double ex, ey;
          if (!reproj(poses[cams[o.cam]], tracks[pts[j]].X, fv, cx, cy, o.u, o.v, ex, ey)) {
            c += 100;
            continue;
          }
          const double e = std::sqrt(ex * ex + ey * ey);
          c += e <= 2.0 ? e * e : 2 * 2.0 * e - 4.0;
        }
      return c;
    };
    double lambda = 1e-3;
    double atual = custo(f);
    for (int it = 0; it < iteracoes; it++) {
      std::vector<double> S(ncp * ncp, 0), gc(ncp, 0);
      std::vector<std::array<double, 9>> Pinv(np);
      std::vector<std::array<double, 3>> gp(np);
      // Por ponto: blocos W (ncp x 3) esparsos por camera + focal.
      struct Wb { int col; double w[6][3]; };
      std::vector<std::vector<Wb>> Wpt(np);
      std::vector<std::array<double, 3>> Wf(np);
      for (int j = 0; j < np; j++) {
        std::array<double, 9> Hpp{};
        std::array<double, 3> g{};
        std::array<double, 3> wf{};
        const V3& X = tracks[pts[j]].X;
        for (const auto& o : obsPt[j]) {
          const Pose& P = poses[cams[o.cam]];
          const V3 rx = P.R * X;
          const V3 c = rx + P.t;
          if (c.z <= 1e-6) continue;
          const double iz = 1 / c.z;
          const double ex = cx + f * c.x * iz - o.u, ey = cy + f * c.y * iz - o.v;
          const double e = std::sqrt(ex * ex + ey * ey);
          const double w = e <= 2.0 ? 1 : 2.0 / e;
          const double jp[2][3] = {{f * iz, 0, -f * c.x * iz * iz},
                                   {0, f * iz, -f * c.y * iz * iz}};
          double JX[2][3];
          for (int r = 0; r < 2; r++)
            for (int k = 0; k < 3; k++) {
              double s = 0;
              for (int m = 0; m < 3; m++) s += jp[r][m] * P.R(m, k);
              JX[r][k] = s;
            }
          const double Jf[2] = {c.x * iz, c.y * iz};
          const double res[2] = {ex, ey};
          for (int r = 0; r < 2; r++) {
            for (int a = 0; a < 3; a++) {
              g[a] += w * JX[r][a] * res[r];
              for (int b = 0; b < 3; b++) Hpp[a * 3 + b] += w * JX[r][a] * JX[r][b];
              if (focalLivre) wf[a] += w * Jf[r] * JX[r][a];
            }
          }
          if (focalLivre) {
            const int fi = ncp - 1;
            for (int r = 0; r < 2; r++) {
              S[fi * ncp + fi] += w * Jf[r] * Jf[r];
              gc[fi] += w * Jf[r] * res[r];
            }
          }
          if (o.cam == 0) continue;
          const int col = (o.cam - 1) * 6;
          const M3 sk = skew(rx);
          double Jc[2][6];
          for (int r = 0; r < 2; r++)
            for (int k = 0; k < 3; k++) {
              double dw = 0;
              for (int m = 0; m < 3; m++) dw += jp[r][m] * -sk(m, k);
              Jc[r][k] = dw;
              Jc[r][3 + k] = jp[r][k];
            }
          Wb wb;
          wb.col = col;
          for (int a = 0; a < 6; a++)
            for (int b = 0; b < 3; b++) {
              double s = 0;
              for (int r = 0; r < 2; r++) s += w * Jc[r][a] * JX[r][b];
              wb.w[a][b] = s;
            }
          for (int r = 0; r < 2; r++)
            for (int a = 0; a < 6; a++) {
              gc[col + a] += w * Jc[r][a] * res[r];
              for (int b = 0; b < 6; b++)
                S[(col + a) * ncp + col + b] += w * Jc[r][a] * Jc[r][b];
              if (focalLivre) {
                const int fi = ncp - 1;
                S[(col + a) * ncp + fi] += w * Jc[r][a] * Jf[r];
                S[fi * ncp + col + a] += w * Jc[r][a] * Jf[r];
              }
            }
          Wpt[j].push_back(wb);
        }
        for (int a = 0; a < 3; a++) Hpp[a * 3 + a] *= (1 + lambda);
        for (int a = 0; a < 3; a++) Hpp[a * 3 + a] += 1e-12;
        if (!inv3(Hpp, Pinv[j])) Pinv[j].fill(0);
        gp[j] = g;
        Wf[j] = wf;
      }
      // Complemento de Schur.
      for (int j = 0; j < np; j++) {
        const auto& Pi = Pinv[j];
        struct Lin { int col; int len; double v[6][3]; };
        std::vector<Lin> blocos;
        for (const auto& wb : Wpt[j]) {
          Lin l;
          l.col = wb.col;
          l.len = 6;
          for (int a = 0; a < 6; a++)
            for (int b = 0; b < 3; b++) l.v[a][b] = wb.w[a][b];
          blocos.push_back(l);
        }
        if (focalLivre) {
          Lin l;
          l.col = ncp - 1;
          l.len = 1;
          for (int b = 0; b < 3; b++) l.v[0][b] = Wf[j][b];
          blocos.push_back(l);
        }
        // W P^-1
        std::vector<std::array<std::array<double, 3>, 6>> WP(blocos.size());
        for (size_t q = 0; q < blocos.size(); q++)
          for (int a = 0; a < blocos[q].len; a++)
            for (int b = 0; b < 3; b++) {
              double s = 0;
              for (int m = 0; m < 3; m++) s += blocos[q].v[a][m] * Pi[m * 3 + b];
              WP[q][a][b] = s;
            }
        for (size_t q = 0; q < blocos.size(); q++) {
          for (int a = 0; a < blocos[q].len; a++) {
            double s = 0;
            for (int m = 0; m < 3; m++) s += WP[q][a][m] * gp[j][m];
            gc[blocos[q].col + a] -= s;
          }
          for (size_t r = 0; r < blocos.size(); r++)
            for (int a = 0; a < blocos[q].len; a++)
              for (int b = 0; b < blocos[r].len; b++) {
                double s = 0;
                for (int m = 0; m < 3; m++) s += WP[q][a][m] * blocos[r].v[b][m];
                S[(blocos[q].col + a) * ncp + blocos[r].col + b] -= s;
              }
        }
      }
      for (int a = 0; a < ncp; a++) S[a * ncp + a] *= (1 + lambda), S[a * ncp + a] += 1e-9;
      std::vector<double> dc(ncp);
      for (int a = 0; a < ncp; a++) dc[a] = -gc[a];
      std::vector<double> Sfat = S;
      if (!choleskySolve(Sfat, ncp, dc)) {
        lambda *= 10;
        continue;
      }
      // Guarda o estado e aplica.
      std::vector<Pose> guardadas;
      for (int q : cams) guardadas.push_back(poses[q]);
      std::vector<V3> Xg(np);
      for (int j = 0; j < np; j++) Xg[j] = tracks[pts[j]].X;
      const double fg = f;
      for (int i = 1; i < nc; i++) {
        const int col = (i - 1) * 6;
        Pose& P = poses[cams[i]];
        P.R = expSO3({dc[col], dc[col + 1], dc[col + 2]}) * P.R;
        P.t = P.t + V3{dc[col + 3], dc[col + 4], dc[col + 5]};
      }
      if (focalLivre) f = std::clamp(f + dc[ncp - 1], 0.2 * W, 6.0 * W);
      for (int j = 0; j < np; j++) {
        std::array<double, 3> rhs = gp[j];
        for (const auto& wb : Wpt[j])
          for (int b = 0; b < 3; b++)
            for (int a = 0; a < 6; a++) rhs[b] += wb.w[a][b] * dc[wb.col + a];
        if (focalLivre)
          for (int b = 0; b < 3; b++) rhs[b] += Wf[j][b] * dc[ncp - 1];
        V3 d;
        for (int b = 0; b < 3; b++) {
          double s = 0;
          for (int m = 0; m < 3; m++) s += Pinv[j][b * 3 + m] * rhs[m];
          (b == 0 ? d.x : (b == 1 ? d.y : d.z)) = -s;
        }
        tracks[pts[j]].X = tracks[pts[j]].X + d;
      }
      const double novo = custo(f);
      if (novo < atual) {
        const bool parou = (atual - novo) < 1e-6 * atual;
        atual = novo;
        lambda = std::max(1e-7, lambda / 3);
        if (parou) break;
      } else {
        for (int i = 0; i < nc; i++) poses[cams[i]] = guardadas[i];
        for (int j = 0; j < np; j++) tracks[pts[j]].X = Xg[j];
        f = fg;
        lambda *= 5;
        if (lambda > 1e6) break;
      }
    }
    return atual;
  }

  void descartarRuins(const std::vector<int>& chaves, double f, double limiar) {
    for (auto& tr : tracks) {
      if (!tr.ok) continue;
      int ruins = 0, vistas = 0;
      for (const Obs& o : tr.obs) {
        if (o.frame >= F || !poses[o.frame].ok) continue;
        if (!std::binary_search(chaves.begin(), chaves.end(), o.frame)) continue;
        vistas++;
        double ex, ey;
        if (!reproj(poses[o.frame], tr.X, f, cx, cy, o.x, o.y, ex, ey) ||
            ex * ex + ey * ey > limiar * limiar)
          ruins++;
      }
      if (vistas < 2 || ruins * 2 > vistas) tr.ok = false;
    }
  }

  // Reconstrucao completa com uma focal inicial. Devolve o erro medio (px)
  // nos quadros-chave, ou -1.
  double reconstruir(double f0, const std::vector<int>& chaves, int& codigo,
                     double& fFinal) {
    poses.assign(F, Pose{});
    for (auto& t : tracks) t.ok = false;
    double f = f0;
    // Par inicial: o melhor entre os primeiros chaves, sem exigir o zero.
    int ia = -1, ib = -1;
    Pose pa, pb;
    double melhorScore = -1, melhorAng = 0;
    const int K = static_cast<int>(chaves.size());
    for (int a = 0; a < std::min(K, 6); a++)
      for (int b = a + 1; b < std::min(K, a + 8); b++) {
        Pose A, B;
        double ang = 0;
        const int n = inicializar(chaves[a], chaves[b], f, A, B, ang);
        if (n < 15 || ang < 0.8) continue;
        const double score = n * std::min(ang, 5.0);
        if (score > melhorScore) {
          melhorScore = score, ia = a, ib = b, pa = A, pb = B, melhorAng = ang;
        }
      }
    if (ia < 0) {
      codigo = ATT_ERR_NO_PARALLAX;
      return -1;
    }
    poses[chaves[ia]] = pa;
    poses[chaves[ib]] = pb;
    std::vector<int> feitos = {chaves[ia], chaves[ib]};
    std::sort(feitos.begin(), feitos.end());
    triangularNovos(feitos, f, 0.8);
    ajustarFeixes(feitos, false, 10, f);
    // Cresce para os dois lados, sempre pelo chave vizinho de um ja feito.
    std::vector<char> tentado(K, 0);
    tentado[ia] = tentado[ib] = 1;
    int desdeAjuste = 0;
    for (int passo = 0; passo < 3 * K; passo++) {
      int escolhido = -1, palpite = -1;
      for (int k = 0; k < K; k++) {
        if (tentado[k]) continue;
        for (int d : {-1, 1}) {
          const int v = k + d;
          if (v >= 0 && v < K && poses[chaves[v]].ok) {
            escolhido = k;
            palpite = v;
            break;
          }
        }
        if (escolhido >= 0) break;
      }
      if (escolhido < 0) break;
      tentado[escolhido] = 1;
      if (!registrar(chaves[escolhido], f, &poses[chaves[palpite]], 4.0)) continue;
      feitos.push_back(chaves[escolhido]);
      std::sort(feitos.begin(), feitos.end());
      triangularNovos(feitos, f, 1.0);
      if (++desdeAjuste >= 4) {
        desdeAjuste = 0;
        // Ajuste local: os ultimos 12 feitos em volta do novo.
        std::vector<int> janela;
        const auto pos = std::find(feitos.begin(), feitos.end(), chaves[escolhido]);
        const int centro = static_cast<int>(pos - feitos.begin());
        for (int i = std::max(0, centro - 8); i < std::min((int)feitos.size(), centro + 5); i++)
          janela.push_back(feitos[i]);
        ajustarFeixes(janela, false, 6, f);
      }
    }
    if (feitos.size() < std::max<size_t>(3, chaves.size() / 2)) {
      codigo = ATT_ERR_DIVERGED;
      return -1;
    }
    // Ajuste global com a focal livre, limpa os pontos ruins e ajusta de novo.
    ajustarFeixes(feitos, true, 25, f);
    descartarRuins(feitos, f, 3.0);
    triangularNovos(feitos, f, 1.0);
    ajustarFeixes(feitos, true, 20, f);
    fFinal = f;
    // Erro medio nos chaves.
    double soma = 0;
    int n = 0;
    for (const auto& tr : tracks) {
      if (!tr.ok) continue;
      for (const Obs& o : tr.obs) {
        if (o.frame >= F || !poses[o.frame].ok) continue;
        if (!std::binary_search(feitos.begin(), feitos.end(), o.frame)) continue;
        double ex, ey;
        if (reproj(poses[o.frame], tr.X, f, cx, cy, o.x, o.y, ex, ey)) {
          soma += std::sqrt(ex * ex + ey * ey);
          n++;
        }
      }
    }
    (void)melhorAng;
    codigo = ATT_OK;
    return n > 0 ? soma / n : -1;
  }

  // Pose de todo quadro: registra pelos pontos e, sem pontos, interpola.
  void todosOsQuadros(double f) {
    std::vector<int> feitos;
    for (int q = 0; q < F; q++)
      if (poses[q].ok) feitos.push_back(q);
    if (feitos.empty()) return;
    for (int q = 0; q < F; q++) {
      if (poses[q].ok) continue;
      auto it = std::lower_bound(feitos.begin(), feitos.end(), q);
      int antes = it == feitos.begin() ? -1 : *(it - 1);
      int depois = it == feitos.end() ? -1 : *it;
      int perto = antes < 0 ? depois : (depois < 0 ? antes : (q - antes <= depois - q ? antes : depois));
      Pose palpite = poses[perto];
      if (antes >= 0 && depois >= 0) {
        const double u = double(q - antes) / double(depois - antes);
        const M3 dR = poses[depois].R * poses[antes].R.t();
        palpite.R = expSO3(logSO3(dR) * u) * poses[antes].R;
        palpite.t = poses[antes].t * (1 - u) + poses[depois].t * u;
        palpite.ok = true;
      }
      if (!registrar(q, f, &palpite, 3.0)) poses[q] = palpite;
    }
  }

  // TRIPE: gira sem andar. Rotacao de cada quadro em relacao ao primeiro
  // pelos raios (Kabsch), e a focal pela homografia de rotacao pura.
  bool resolverTripe(double fChute) {
    tripe = true;
    // Focal: a que deixa K^-1 H K mais perto de uma rotacao, em pares
    // espacados.
    double f = fChute;
    {
      double melhor = 1e18;
      for (double k = 0.45; k <= 3.2; k *= 1.04) {
        const double fc = W * k;
        double erroTotal = 0;
        int pares = 0;
        for (int a = 0; a + 6 < F && pares < 12; a += std::max(1, F / 12)) {
          const int b = std::min(F - 1, a + std::max(6, F / 6));
          const Par p = par(a, b, 1.0);
          if (p.ti.size() < 20) continue;
          std::vector<int> idx(p.ti.size());
          for (size_t i = 0; i < idx.size(); i++) idx[i] = static_cast<int>(i);
          M3 Hm = homography4(p.a, p.b, idx.data(), static_cast<int>(idx.size()));
          Hm(0, 2) /= fc, Hm(1, 2) /= fc, Hm(2, 0) *= fc, Hm(2, 1) *= fc;
          const double escala = std::cbrt(std::fabs(Hm.det()));
          if (escala < 1e-12) continue;
          for (double& x : Hm.m) x /= escala;
          if (Hm.det() < 0) for (double& x : Hm.m) x = -x;
          const M3 R = nearestRotation(Hm);
          double e = 0;
          for (int i = 0; i < 9; i++) e += (Hm.m[i] - R.m[i]) * (Hm.m[i] - R.m[i]);
          erroTotal += e;
          pares++;
        }
        if (pares > 0 && erroTotal / pares < melhor) melhor = erroTotal / pares, f = fc;
      }
    }
    focal = f;
    poses.assign(F, Pose{});
    int ref = -1;
    for (int q = 0; q < F; q++)
      if (porQuadro[q].size() >= 12) {
        ref = q;
        break;
      }
    if (ref < 0) return false;
    poses[ref].ok = true;
    for (int q = ref + 1; q < F; q++) {
      // Contra o quadro feito mais recente com rastros em comum.
      for (int back = q - 1; back >= ref; back--) {
        if (!poses[back].ok) continue;
        const Par p = par(back, q, f);
        if (p.ti.size() < 8) continue;
        M3 Hs = M3::zero();
        for (size_t i = 0; i < p.a.size(); i++) {
          const V3 a = p.a[i].unit(), b = p.b[i].unit();
          for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
              Hs(r, c) += (r == 0 ? b.x : r == 1 ? b.y : b.z) *
                          (c == 0 ? a.x : c == 1 ? a.y : a.z);
        }
        const M3 Rrel = nearestRotation(Hs);
        poses[q].R = Rrel * poses[back].R;
        poses[q].t = {0, 0, 0};
        poses[q].ok = true;
        break;
      }
    }
    for (int q = 0; q < ref; q++) poses[q] = poses[ref];
    // Pontos no infinito pratico (distancia fixa ao longo do raio).
    for (auto& tr : tracks) {
      tr.ok = false;
      for (const Obs& o : tr.obs) {
        if (o.frame >= F || !poses[o.frame].ok) continue;
        const V3 b = bearing(o.x, o.y, f, cx, cy).unit();
        tr.X = poses[o.frame].R.t() * (b * 1000.0);
        tr.ok = true;
        break;
      }
    }
    return true;
  }

  // PARECE TRIPE? Sem depender da focal: em rotacao pura a homografia (em px
  // centrados) explica todos os pontos E, para alguma focal, K^-1 H K e uma
  // rotacao. Chao plano com a camera andando tambem cabe numa homografia,
  // mas nenhuma focal a transforma em rotacao.
  bool pareceTripe() {
    int pares = 0, tripes = 0;
    for (int a = 0; a + 6 < F && pares < 8; a += std::max(1, F / 8)) {
      const int b = std::min(F - 1, a + std::max(6, F / 4));
      const Par p = par(a, b, 1.0);
      const int n = static_cast<int>(p.ti.size());
      if (n < 20) continue;
      Rng rng(static_cast<unsigned long long>(a * 31 + b * 131 + 5));
      M3 melhorH;
      int melhor_n = -1;
      int idx[4];
      for (int it = 0; it < 200; it++) {
        for (int k = 0; k < 4; k++) idx[k] = rng.below(n);
        const M3 Hm = homography4(p.a, p.b, idx, 4);
        int c = 0;
        for (int i = 0; i < n; i++) c += transferErr(Hm, p.a[i], p.b[i]) < 2.25;
        if (c > melhor_n) melhor_n = c, melhorH = Hm;
      }
      pares++;
      if (melhor_n < 0.95 * n) continue;
      std::vector<int> in;
      for (int i = 0; i < n; i++)
        if (transferErr(melhorH, p.a[i], p.b[i]) < 2.25) in.push_back(i);
      const M3 Hm = homography4(p.a, p.b, in.data(), static_cast<int>(in.size()));
      double menor = 1e18;
      for (double k = 0.3; k <= 4.0; k *= 1.02) {
        const double fc = k * W;
        M3 Hn = Hm;
        Hn(0, 2) /= fc, Hn(1, 2) /= fc, Hn(2, 0) *= fc, Hn(2, 1) *= fc;
        const double esc = std::cbrt(std::fabs(Hn.det()));
        if (esc < 1e-12) continue;
        for (double& x : Hn.m) x /= esc;
        if (Hn.det() < 0) for (double& x : Hn.m) x = -x;
        const M3 R = nearestRotation(Hn);
        double e = 0;
        for (int i = 0; i < 9; i++) e += (Hn.m[i] - R.m[i]) * (Hn.m[i] - R.m[i]);
        menor = std::min(menor, e);
      }
      if (menor < 1e-3) tripes++;
    }
    return pares > 0 && tripes * 5 >= pares * 3;
  }

  int resolver(double focalChute, int modo) {
    indexar();
    int uteis = 0;
    for (const auto& t : tracks) uteis += t.obs.size() >= 6;
    if (uteis < 12 || F < 4) return ATT_ERR_FEW_POINTS;
    if (modo == ATT_MODE_TRIPOD || pareceTripe()) {
      if (!resolverTripe(focalChute > 0 ? focalChute : 1.2 * W)) return ATT_ERR_FEW_POINTS;
      calcularErros();
      return ATT_OK;
    }
    const std::vector<int> chaves = quadrosChave();
    if (chaves.size() < 2) return ATT_ERR_FEW_POINTS;
    std::vector<double> chutes;
    if (focalChute > 0) chutes = {focalChute};
    else chutes = {0.8 * W, 1.2 * W, 1.9 * W};
    double melhorErro = 1e18, melhorF0 = 0;
    int codigo = ATT_ERR_NO_PARALLAX;
    for (double f0 : chutes) {
      int c = 0;
      double ff = f0;
      const double e = reconstruir(f0, chaves, c, ff);
      if (c == ATT_OK && e >= 0 && e < melhorErro) {
        melhorErro = e, melhorF0 = f0, codigo = ATT_OK;
      } else if (codigo != ATT_OK) {
        codigo = c;
      }
    }
    if (codigo != ATT_OK) {
      // Sem paralaxe para nada: pode ser tripe.
      if (codigo == ATT_ERR_NO_PARALLAX && resolverTripe(focalChute > 0 ? focalChute : 1.2 * W)) {
        calcularErros();
        return ATT_OK;
      }
      return codigo;
    }
    // Refaz com o MESMO chute que deu certo (deterministico: mesma resposta).
    int c = 0;
    double ff = melhorF0;
    erro = reconstruir(melhorF0, chaves, c, ff);
    if (c != ATT_OK) return c;
    focal = ff;
    todosOsQuadros(focal);
    calcularErros();
    return ATT_OK;
  }

  void calcularErros() {
    double soma = 0;
    int n = 0;
    for (auto& tr : tracks) {
      tr.err = 0;
      tr.views = 0;
      if (!tr.ok) continue;
      double s = 0;
      int v = 0;
      for (const Obs& o : tr.obs) {
        if (o.frame >= F || !poses[o.frame].ok) continue;
        double ex, ey;
        if (!reproj(poses[o.frame], tr.X, focal, cx, cy, o.x, o.y, ex, ey)) continue;
        s += std::sqrt(ex * ex + ey * ey);
        v++;
      }
      tr.views = v;
      tr.err = v > 0 ? s / v : 0;
      soma += s;
      n += v;
    }
    erro = n > 0 ? soma / n : 0;
  }
};

}  // namespace
}  // namespace att

struct att_sfm {
  att::Solver solver;
  std::unordered_map<int, int> idParaIndice;
  att_sfm(int w, int h, int f, int fps) : solver(w, h, f, fps) {}
};

extern "C" {

att_sfm* att_sfm_create(int32_t width, int32_t height, int32_t frames, int32_t fps) {
  if (width <= 0 || height <= 0 || frames <= 0) return nullptr;
  return new att_sfm(width, height, frames, fps);
}

void att_sfm_destroy(att_sfm* s) { delete s; }

void att_sfm_add(att_sfm* s, const double* obs, int32_t n) {
  if (!s || !obs) return;
  for (int i = 0; i < n; i++) {
    const int id = static_cast<int>(obs[i * 4]);
    auto it = s->idParaIndice.find(id);
    int idx;
    if (it == s->idParaIndice.end()) {
      idx = static_cast<int>(s->solver.tracks.size());
      s->idParaIndice[id] = idx;
      att::Track t;
      t.id = id;
      s->solver.tracks.push_back(t);
    } else {
      idx = it->second;
    }
    s->solver.tracks[idx].obs.push_back(
        {static_cast<int>(obs[i * 4 + 1]), obs[i * 4 + 2], obs[i * 4 + 3]});
  }
}

int32_t att_sfm_solve(att_sfm* s, double focal_guess, int32_t mode) {
  if (!s) return ATT_ERR_ARGS;
  return s->solver.resolver(focal_guess, mode);
}

double att_sfm_focal(att_sfm* s) { return s ? s->solver.focal : 0; }
double att_sfm_error(att_sfm* s) { return s ? s->solver.erro : 0; }
int32_t att_sfm_is_tripod(att_sfm* s) { return s && s->solver.tripe ? 1 : 0; }

int32_t att_sfm_pose_count(att_sfm* s) {
  if (!s) return 0;
  int n = 0;
  for (const auto& p : s->solver.poses) n += p.ok;
  return n;
}

int32_t att_sfm_poses(att_sfm* s, double* out, int32_t max) {
  if (!s || !out) return 0;
  int n = 0;
  for (int q = 0; q < static_cast<int>(s->solver.poses.size()) && n < max; q++) {
    const auto& p = s->solver.poses[q];
    if (!p.ok) continue;
    double* o = out + n * 13;
    o[0] = q;
    for (int i = 0; i < 9; i++) o[1 + i] = p.R.m[i];
    o[10] = p.t.x;
    o[11] = p.t.y;
    o[12] = p.t.z;
    n++;
  }
  return n;
}

int32_t att_sfm_point_count(att_sfm* s) {
  if (!s) return 0;
  int n = 0;
  for (const auto& t : s->solver.tracks) n += t.ok;
  return n;
}

int32_t att_sfm_points(att_sfm* s, double* out, int32_t max) {
  if (!s || !out) return 0;
  int n = 0;
  for (const auto& t : s->solver.tracks) {
    if (!t.ok || n >= max) continue;
    double* o = out + n * 6;
    o[0] = t.id;
    o[1] = t.X.x;
    o[2] = t.X.y;
    o[3] = t.X.z;
    o[4] = t.err;
    o[5] = t.views;
    n++;
  }
  return n;
}

}  // extern "C"
