// ALGEBRA DO RASTREADOR: o minimo de matriz que um rastreio de camera pede,
// escrito aqui para o pacote nao depender de nada (Eigen, Ceres) — compila
// igual no Android, no iOS e no PC.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace att {

struct V3 {
  double x = 0, y = 0, z = 0;
  V3() = default;
  V3(double a, double b, double c) : x(a), y(b), z(c) {}
  V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  V3 operator*(double k) const { return {x * k, y * k, z * k}; }
  double dot(const V3& o) const { return x * o.x + y * o.y + z * o.z; }
  V3 cross(const V3& o) const {
    return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
  }
  double norm() const { return std::sqrt(dot(*this)); }
  V3 unit() const {
    const double n = norm();
    return n > 1e-300 ? *this * (1.0 / n) : V3{0, 0, 1};
  }
};

struct M3 {
  std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};
  double& operator()(int r, int c) { return m[r * 3 + c]; }
  double operator()(int r, int c) const { return m[r * 3 + c]; }
  static M3 zero() {
    M3 z;
    z.m.fill(0);
    return z;
  }
  V3 operator*(const V3& v) const {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z,
            m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z};
  }
  M3 operator*(const M3& o) const {
    M3 r = zero();
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        for (int k = 0; k < 3; k++) r(i, j) += (*this)(i, k) * o(k, j);
    return r;
  }
  M3 t() const {
    M3 r;
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) r(i, j) = (*this)(j, i);
    return r;
  }
  double det() const {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) -
           m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
  }
};

inline M3 skew(const V3& v) {
  M3 s = M3::zero();
  s(0, 1) = -v.z;
  s(0, 2) = v.y;
  s(1, 0) = v.z;
  s(1, 2) = -v.x;
  s(2, 0) = -v.y;
  s(2, 1) = v.x;
  return s;
}

// Rodrigues: vetor de rotacao -> matriz.
inline M3 expSO3(const V3& w) {
  const double th = w.norm();
  const M3 k = skew(w);
  M3 r;
  if (th < 1e-12) {
    for (int i = 0; i < 9; i++) r.m[i] += k.m[i];
    return r;
  }
  const double a = std::sin(th) / th, b = (1 - std::cos(th)) / (th * th);
  const M3 k2 = k * k;
  for (int i = 0; i < 9; i++) r.m[i] += a * k.m[i] + b * k2.m[i];
  return r;
}

inline V3 logSO3(const M3& r) {
  const double c = std::clamp((r(0, 0) + r(1, 1) + r(2, 2) - 1) * .5, -1.0, 1.0);
  const double th = std::acos(c);
  V3 v{r(2, 1) - r(1, 2), r(0, 2) - r(2, 0), r(1, 0) - r(0, 1)};
  if (th < 1e-9) return v * .5;
  if (3.141592653589793 - th < 1e-6) {
    // Meia volta: o eixo sai da diagonal.
    V3 a{std::sqrt(std::max(0.0, (r(0, 0) + 1) / 2)),
         std::sqrt(std::max(0.0, (r(1, 1) + 1) / 2)),
         std::sqrt(std::max(0.0, (r(2, 2) + 1) / 2))};
    if (r(0, 1) < 0) a.y = -a.y;
    if (r(0, 2) < 0) a.z = -a.z;
    return a.unit() * th;
  }
  return v * (th / (2 * std::sin(th)));
}

// Jacobi para matriz simetrica n x n (n pequeno): autovalores crescentes em
// `val`, autovetores nas COLUNAS de `vec`.
inline void eigenSym(std::vector<double> a, int n, std::vector<double>& val,
                     std::vector<double>& vec) {
  vec.assign(n * n, 0);
  for (int i = 0; i < n; i++) vec[i * n + i] = 1;
  for (int sweep = 0; sweep < 60; sweep++) {
    double off = 0;
    for (int p = 0; p < n; p++)
      for (int q = p + 1; q < n; q++) off += a[p * n + q] * a[p * n + q];
    if (off < 1e-26) break;
    for (int p = 0; p < n; p++) {
      for (int q = p + 1; q < n; q++) {
        const double apq = a[p * n + q];
        if (std::fabs(apq) < 1e-300) continue;
        const double app = a[p * n + p], aqq = a[q * n + q];
        const double theta = (aqq - app) / (2 * apq);
        const double t = (theta >= 0 ? 1 : -1) /
                         (std::fabs(theta) + std::sqrt(theta * theta + 1));
        const double c = 1 / std::sqrt(t * t + 1), s = t * c;
        for (int k = 0; k < n; k++) {
          const double akp = a[k * n + p], akq = a[k * n + q];
          a[k * n + p] = c * akp - s * akq;
          a[k * n + q] = s * akp + c * akq;
        }
        for (int k = 0; k < n; k++) {
          const double apk = a[p * n + k], aqk = a[q * n + k];
          a[p * n + k] = c * apk - s * aqk;
          a[q * n + k] = s * apk + c * aqk;
        }
        for (int k = 0; k < n; k++) {
          const double vkp = vec[k * n + p], vkq = vec[k * n + q];
          vec[k * n + p] = c * vkp - s * vkq;
          vec[k * n + q] = s * vkp + c * vkq;
        }
      }
    }
  }
  std::vector<int> ordem(n);
  for (int i = 0; i < n; i++) ordem[i] = i;
  std::sort(ordem.begin(), ordem.end(),
            [&](int i, int j) { return a[i * n + i] < a[j * n + j]; });
  std::vector<double> v2(n * n);
  val.assign(n, 0);
  for (int c = 0; c < n; c++) {
    val[c] = a[ordem[c] * n + ordem[c]];
    for (int r = 0; r < n; r++) v2[r * n + c] = vec[r * n + ordem[c]];
  }
  vec.swap(v2);
}

// Nucleo de A (linhas x n): o autovetor de AtA com menor autovalor.
inline std::vector<double> nullVector(const std::vector<double>& a, int linhas,
                                      int n) {
  std::vector<double> ata(n * n, 0);
  for (int r = 0; r < linhas; r++)
    for (int i = 0; i < n; i++) {
      const double ai = a[r * n + i];
      if (ai == 0) continue;
      for (int j = i; j < n; j++) ata[i * n + j] += ai * a[r * n + j];
    }
  for (int i = 0; i < n; i++)
    for (int j = 0; j < i; j++) ata[i * n + j] = ata[j * n + i];
  std::vector<double> val, vec;
  eigenSym(ata, n, val, vec);
  std::vector<double> out(n);
  for (int r = 0; r < n; r++) out[r] = vec[r * n + 0];
  return out;
}

// SVD 3x3: A = U diag(s) Vt, s decrescente, U e V com det +1 quando der.
inline void svd3(const M3& a, M3& u, V3& s, M3& v) {
  const M3 ata = a.t() * a;
  std::vector<double> val, vec;
  eigenSym(std::vector<double>(ata.m.begin(), ata.m.end()), 3, val, vec);
  // Crescente -> decrescente.
  for (int c = 0; c < 3; c++)
    for (int r = 0; r < 3; r++) v(r, c) = vec[r * 3 + (2 - c)];
  s = {std::sqrt(std::max(0.0, val[2])), std::sqrt(std::max(0.0, val[1])),
       std::sqrt(std::max(0.0, val[0]))};
  if (v.det() < 0)
    for (int r = 0; r < 3; r++) v(r, 2) = -v(r, 2);
  V3 c0{v(0, 0), v(1, 0), v(2, 0)}, c1{v(0, 1), v(1, 1), v(2, 1)};
  V3 u0 = (a * c0).unit(), u1 = a * c1;
  u1 = (u1 - u0 * u1.dot(u0)).unit();
  V3 u2 = u0.cross(u1);
  for (int r = 0; r < 3; r++) {
    u(r, 0) = r == 0 ? u0.x : (r == 1 ? u0.y : u0.z);
    u(r, 1) = r == 0 ? u1.x : (r == 1 ? u1.y : u1.z);
    u(r, 2) = r == 0 ? u2.x : (r == 1 ? u2.y : u2.z);
  }
}

// A rotacao mais perto de uma matriz qualquer.
inline M3 nearestRotation(const M3& a) {
  M3 u, v;
  V3 s;
  svd3(a, u, s, v);
  M3 r = u * v.t();
  if (r.det() < 0) {
    for (int i = 0; i < 3; i++) u(i, 2) = -u(i, 2);
    r = u * v.t();
  }
  return r;
}

// Cholesky (LDLt) de uma matriz simetrica densa n x n, in place; resolve
// A x = b. Devolve falso se nao for positiva.
inline bool choleskySolve(std::vector<double>& a, int n, std::vector<double>& b) {
  for (int j = 0; j < n; j++) {
    double d = a[j * n + j];
    for (int k = 0; k < j; k++) d -= a[j * n + k] * a[j * n + k];
    if (d <= 1e-18) return false;
    d = std::sqrt(d);
    a[j * n + j] = d;
    for (int i = j + 1; i < n; i++) {
      double s = a[i * n + j];
      for (int k = 0; k < j; k++) s -= a[i * n + k] * a[j * n + k];
      a[i * n + j] = s / d;
    }
  }
  for (int i = 0; i < n; i++) {
    double s = b[i];
    for (int k = 0; k < i; k++) s -= a[i * n + k] * b[k];
    b[i] = s / a[i * n + i];
  }
  for (int i = n - 1; i >= 0; i--) {
    double s = b[i];
    for (int k = i + 1; k < n; k++) s -= a[k * n + i] * b[k];
    b[i] = s / a[i * n + i];
  }
  return true;
}

// Inversa de 3x3 simetrica positiva.
inline bool inv3(const std::array<double, 9>& a, std::array<double, 9>& out) {
  M3 m;
  m.m = a;
  const double d = m.det();
  if (std::fabs(d) < 1e-24) return false;
  out = {(a[4] * a[8] - a[5] * a[7]) / d, (a[2] * a[7] - a[1] * a[8]) / d,
         (a[1] * a[5] - a[2] * a[4]) / d, (a[5] * a[6] - a[3] * a[8]) / d,
         (a[0] * a[8] - a[2] * a[6]) / d, (a[2] * a[3] - a[0] * a[5]) / d,
         (a[3] * a[7] - a[4] * a[6]) / d, (a[1] * a[6] - a[0] * a[7]) / d,
         (a[0] * a[4] - a[1] * a[3]) / d};
  return true;
}

// Gerador deterministico (a mesma entrada da a mesma camera sempre).
struct Rng {
  unsigned long long s;
  explicit Rng(unsigned long long seed) : s(seed * 6364136223846793005ULL + 1) {}
  unsigned next() {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<unsigned>(s >> 33);
  }
  int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % n); }
};

}  // namespace att
