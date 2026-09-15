// A ALGEBRA do motor 2.0 — o minimo que a geometria de cameras precisa,
// escrito uma vez e usado pelo seguidor e pelo resolvedor.
//
// Nada aqui depende de biblioteca de fora: o motor inteiro e C++17 puro,
// deterministico (o sorteio e um xorshift com semente fixa) e igual em
// todo aparelho — a mesma filmagem da a mesma cena, sempre.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace at2 {

// ------------------------------------------------------------- vetores

struct V3 {
  double x = 0, y = 0, z = 0;
};

inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(double s, V3 a) { return {s * a.x, s * a.y, s * a.z}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cruz(V3 a, V3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline double norma(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 unitario(V3 a) {
  const double n = norma(a);
  return n < 1e-12 ? V3{0, 0, 0} : (1.0 / n) * a;
}

// ------------------------------------------------------------ matriz 3x3

struct M3 {
  // Linha a linha.
  double m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

  double at(int i, int j) const { return m[i * 3 + j]; }
  double& at(int i, int j) { return m[i * 3 + j]; }

  V3 aplicar(V3 v) const {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z,
            m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z};
  }

  M3 transposta() const {
    M3 t;
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) t.at(i, j) = at(j, i);
    return t;
  }

  M3 operator*(const M3& o) const {
    M3 r;
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) {
        double s = 0;
        for (int k = 0; k < 3; k++) s += at(i, k) * o.at(k, j);
        r.at(i, j) = s;
      }
    return r;
  }

  double determinante() const {
    return m[0] * (m[4] * m[8] - m[5] * m[7]) -
           m[1] * (m[3] * m[8] - m[5] * m[6]) +
           m[2] * (m[3] * m[7] - m[4] * m[6]);
  }
};

// Rodrigues: vetor de rotacao -> matriz.
inline M3 rodrigues(V3 w) {
  const double t = norma(w);
  M3 r;
  if (t < 1e-12) {
    // Perto de zero a serie e mais estavel que a formula com seno.
    r.m[0] = 1; r.m[1] = -w.z; r.m[2] = w.y;
    r.m[3] = w.z; r.m[4] = 1; r.m[5] = -w.x;
    r.m[6] = -w.y; r.m[7] = w.x; r.m[8] = 1;
    return r;
  }
  const V3 a = (1.0 / t) * w;
  const double c = std::cos(t), s = std::sin(t), u = 1 - c;
  r.m[0] = c + a.x * a.x * u;
  r.m[1] = a.x * a.y * u - a.z * s;
  r.m[2] = a.x * a.z * u + a.y * s;
  r.m[3] = a.y * a.x * u + a.z * s;
  r.m[4] = c + a.y * a.y * u;
  r.m[5] = a.y * a.z * u - a.x * s;
  r.m[6] = a.z * a.x * u - a.y * s;
  r.m[7] = a.z * a.y * u + a.x * s;
  r.m[8] = c + a.z * a.z * u;
  return r;
}

// Matriz -> vetor de rotacao (o inverso do Rodrigues).
inline V3 vetorDeRotacao(const M3& r) {
  const double tr = r.m[0] + r.m[4] + r.m[8];
  const double c = std::max(-1.0, std::min(1.0, (tr - 1) / 2));
  const double ang = std::acos(c);
  if (ang < 1e-9) return {0, 0, 0};
  V3 eixo{r.at(2, 1) - r.at(1, 2), r.at(0, 2) - r.at(2, 0),
          r.at(1, 0) - r.at(0, 1)};
  const double n = norma(eixo);
  if (n < 1e-9) {
    // 180 graus: o eixo sai da diagonal.
    int k = 0;
    if (r.m[4] > r.m[0]) k = 1;
    if (r.m[8] > r.at(k, k)) k = 2;
    V3 e{0, 0, 0};
    double* pe = k == 0 ? &e.x : k == 1 ? &e.y : &e.z;
    *pe = std::sqrt(std::max(0.0, (r.at(k, k) + 1) / 2));
    return ang * unitario(e);
  }
  return (ang / n) * eixo;
}

// A rotacao MAIS PROXIMA de uma matriz suja (ortogonaliza por Gram-Schmidt
// simetrizado — bastante para limpar o ruido numerico de um produto longo).
inline M3 rotacaoMaisProxima(const M3& a) {
  // Iteracao de Higham: R <- (R + R^-T) / 2 converge para o fator ortogonal.
  M3 r = a;
  for (int it = 0; it < 12; it++) {
    // R^-T via adjunta / det.
    const double d = r.determinante();
    if (std::fabs(d) < 1e-12) return a;
    M3 inv;
    inv.m[0] = (r.m[4] * r.m[8] - r.m[5] * r.m[7]) / d;
    inv.m[1] = (r.m[2] * r.m[7] - r.m[1] * r.m[8]) / d;
    inv.m[2] = (r.m[1] * r.m[5] - r.m[2] * r.m[4]) / d;
    inv.m[3] = (r.m[5] * r.m[6] - r.m[3] * r.m[8]) / d;
    inv.m[4] = (r.m[0] * r.m[8] - r.m[2] * r.m[6]) / d;
    inv.m[5] = (r.m[2] * r.m[3] - r.m[0] * r.m[5]) / d;
    inv.m[6] = (r.m[3] * r.m[7] - r.m[4] * r.m[6]) / d;
    inv.m[7] = (r.m[1] * r.m[6] - r.m[0] * r.m[7]) / d;
    inv.m[8] = (r.m[0] * r.m[4] - r.m[1] * r.m[3]) / d;
    // inv e a inversa; a inversa-transposta e a transposta dela.
    M3 invT = inv.transposta();
    M3 prox;
    double delta = 0;
    for (int i = 0; i < 9; i++) {
      prox.m[i] = (r.m[i] + invT.m[i]) / 2;
      delta = std::max(delta, std::fabs(prox.m[i] - r.m[i]));
    }
    r = prox;
    if (delta < 1e-14) break;
  }
  return r;
}

// ------------------------------------------------ sistemas e autovalores

// Resolve A x = b (quadrada, pequena) por eliminacao com pivo. Devolve
// false quando o sistema e singular.
inline bool resolverSistema(std::vector<std::vector<double>> a,
                            std::vector<double> b, std::vector<double>& x) {
  const int n = (int)a.size();
  for (int col = 0; col < n; col++) {
    int piv = col;
    for (int i = col + 1; i < n; i++)
      if (std::fabs(a[i][col]) > std::fabs(a[piv][col])) piv = i;
    if (std::fabs(a[piv][col]) < 1e-12) return false;
    std::swap(a[piv], a[col]);
    std::swap(b[piv], b[col]);
    for (int i = col + 1; i < n; i++) {
      const double f = a[i][col] / a[col][col];
      for (int j = col; j < n; j++) a[i][j] -= f * a[col][j];
      b[i] -= f * b[col];
    }
  }
  x.assign(n, 0);
  for (int i = n - 1; i >= 0; i--) {
    double s = b[i];
    for (int j = i + 1; j < n; j++) s -= a[i][j] * x[j];
    x[i] = s / a[i][i];
  }
  return true;
}

// Cholesky (LDL^T sem pivo) para o sistema NORMAL do ajuste de feixes:
// simetrico e quase sempre positivo-definido depois do amortecimento LM.
inline bool resolverSimetrico(std::vector<double>& a, std::vector<double>& b,
                              int n) {
  // a: n*n linha a linha (so a metade de cima e lida), b: n. Resposta em b.
  for (int j = 0; j < n; j++) {
    double d = a[j * n + j];
    for (int k = 0; k < j; k++) d -= a[k * n + j] * a[k * n + j] * a[k * n + k];
    if (d < 1e-12) return false;
    a[j * n + j] = d;
    for (int i = j + 1; i < n; i++) {
      double s = a[j * n + i];
      for (int k = 0; k < j; k++)
        s -= a[k * n + i] * a[k * n + j] * a[k * n + k];
      a[j * n + i] = s / d;
    }
  }
  // L^T D L x = b: frente...
  for (int i = 0; i < n; i++) {
    double s = b[i];
    for (int k = 0; k < i; k++) s -= a[k * n + i] * b[k];
    b[i] = s;
  }
  for (int i = 0; i < n; i++) b[i] /= a[i * n + i];
  // ...e volta.
  for (int i = n - 1; i >= 0; i--) {
    double s = b[i];
    for (int k = i + 1; k < n; k++) s -= a[i * n + k] * b[k];
    b[i] = s;
  }
  return true;
}

// Autovalores e autovetores de uma simetrica pequena, por Jacobi. Sai em
// ordem CRESCENTE; os vetores nas linhas de `vet`.
inline void jacobiSimetrica(std::vector<std::vector<double>> a,
                            std::vector<double>& val,
                            std::vector<std::vector<double>>& vet) {
  const int n = (int)a.size();
  vet.assign(n, std::vector<double>(n, 0));
  for (int i = 0; i < n; i++) vet[i][i] = 1;
  for (int varre = 0; varre < 64; varre++) {
    double fora = 0;
    for (int i = 0; i < n; i++)
      for (int j = i + 1; j < n; j++) fora += a[i][j] * a[i][j];
    if (fora < 1e-22) break;
    for (int p = 0; p < n; p++)
      for (int q = p + 1; q < n; q++) {
        if (std::fabs(a[p][q]) < 1e-15) continue;
        const double theta = (a[q][q] - a[p][p]) / (2 * a[p][q]);
        const double t = (theta >= 0 ? 1.0 : -1.0) /
                         (std::fabs(theta) + std::sqrt(theta * theta + 1));
        const double c = 1 / std::sqrt(t * t + 1), s = t * c;
        for (int k = 0; k < n; k++) {
          const double akp = a[k][p], akq = a[k][q];
          a[k][p] = c * akp - s * akq;
          a[k][q] = s * akp + c * akq;
        }
        for (int k = 0; k < n; k++) {
          const double apk = a[p][k], aqk = a[q][k];
          a[p][k] = c * apk - s * aqk;
          a[q][k] = s * apk + c * aqk;
        }
        for (int k = 0; k < n; k++) {
          const double vpk = vet[p][k], vqk = vet[q][k];
          vet[p][k] = c * vpk - s * vqk;
          vet[q][k] = s * vpk + c * vqk;
        }
      }
  }
  val.resize(n);
  for (int i = 0; i < n; i++) val[i] = a[i][i];
  // Ordena crescente, carregando os vetores junto.
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++)
      if (val[j] < val[i]) {
        std::swap(val[i], val[j]);
        std::swap(vet[i], vet[j]);
      }
}

// O vetor unitario x que minimiza |A x| (o "nucleo" pratico): autovetor do
// menor autovalor de A^T A.
inline std::vector<double> nucleoDe(const std::vector<std::vector<double>>& a,
                                    int colunas) {
  std::vector<std::vector<double>> ata(colunas,
                                       std::vector<double>(colunas, 0));
  for (const auto& linha : a)
    for (int i = 0; i < colunas; i++)
      for (int j = 0; j < colunas; j++) ata[i][j] += linha[i] * linha[j];
  std::vector<double> val;
  std::vector<std::vector<double>> vet;
  jacobiSimetrica(ata, val, vet);
  return vet[0];
}

// --------------------------------------------------------------- sorteio

// xorshift64*: rapido, bom o bastante para RANSAC, e IGUAL em todo lugar.
struct Sorteio {
  uint64_t s;
  explicit Sorteio(uint64_t semente) : s(semente ? semente : 0x9E3779B97F4A7C15ull) {}
  uint64_t proximo() {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 0x2545F4914F6CDD1Dull;
  }
  int ate(int n) { return (int)(proximo() % (uint64_t)n); }
  double real() { return (proximo() >> 11) * (1.0 / 9007199254740992.0); }
};

// ----------------------------------------------------------------- Huber

// O peso de Huber para um residuo r com cotovelo delta: 1 dentro, delta/|r|
// fora. E o que deixa um ponto errado PESAR MENOS em vez de mandar no
// ajuste inteiro.
inline double pesoHuber(double r, double delta) {
  const double a = std::fabs(r);
  return a <= delta ? 1.0 : delta / a;
}

}  // namespace at2
