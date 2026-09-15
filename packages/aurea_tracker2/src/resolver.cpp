// O RESOLVEDOR DO MOTOR 2.0 — dos rastros a camera, sem inventar.
//
// O caminho: escolher um PAR INICIAL com paralaxe de verdade; descobrir a
// focal numa varredura de tres vistas (duas nao bastam: e a ressecao da
// terceira que denuncia a focal errada); montar a cena incremental
// (essencial -> triangulacao -> PnP -> triangulacao de novo); e fechar com
// um AJUSTE DE FEIXES de verdade — Huber contra pontos errados, focal E
// distorcao radial k1 livres, pontos eliminados por Schur. No fim, pose de
// TODO quadro por ressecao contra a nuvem pronta.
//
// O que ele NAO faz e devolver cena sem lastro: tripe vira rotacao pura
// (detectado sozinho pela homografia), pouca textura e pouca paralaxe
// viram CODIGO DE ERRO, e um ajuste que nao fecha diz que nao fechou.
#include "motor.h"

#include <array>
#include <cstring>
#include <map>
#include <vector>

#include "nucleo.hpp"

#include <cstdio>
#include <cstdlib>

// Diagnostico ligado por fora (AT2_DEBUG=1): e a lanterna que faltou ao
// motor 1 no aparelho — quando o rastreio sair estranho NO CELULAR, isto
// conta o que o motor viu, passo a passo, no logcat.
static bool at2Debug() {
  static const bool ligado = std::getenv("AT2_DEBUG") != nullptr;
  return ligado;
}
#define AT2_LOG(...) do { if (at2Debug()) { std::fprintf(stderr, "[at2] " __VA_ARGS__); std::fputc(10, stderr); std::fflush(stderr); } } while (0)

using namespace at2;

namespace {

constexpr uint64_t kSemente = 20260914u;

struct Obs {
  int quadro;
  double x, y;  // pixels
};

struct Trilha {
  int id = 0;
  std::vector<Obs> obs;
  V3 X{};
  bool temX = false;
  const Obs* em(int quadro) const {
    for (const auto& o : obs)
      if (o.quadro == quadro) return &o;
    return nullptr;
  }
};

struct Pose {
  M3 R;
  V3 t;
  bool ok = false;
};

// ------------------------------------------------------------- camera

struct Camera {
  double cx = 0, cy = 0, f = 1, k1 = 0;

  // pixel -> normalizado SEM distorcao (inverte k1 por ponto fixo).
  void normalizar(double px, double py, double* nx, double* ny) const {
    double dx = (px - cx) / f, dy = (py - cy) / f;
    double ux = dx, uy = dy;
    for (int i = 0; i < 4; i++) {
      const double r2 = ux * ux + uy * uy;
      const double d = 1 + k1 * r2;
      if (std::fabs(d) < 1e-6) break;
      ux = dx / d;
      uy = dy / d;
    }
    *nx = ux;
    *ny = uy;
  }

  // mundo -> pixel. Falso quando o ponto esta atras da camera.
  bool projetar(const M3& R, V3 t, V3 X, double* px, double* py) const {
    const V3 c = R.aplicar(X) + t;
    if (c.z < 1e-6) return false;
    const double x = c.x / c.z, y = c.y / c.z;
    const double d = 1 + k1 * (x * x + y * y);
    *px = cx + f * x * d;
    *py = cy + f * y * d;
    return true;
  }
};

// --------------------------------------------------------------- SVD 3x3

// U S V^T de uma 3x3, por Jacobi em A^T A (V) e recomposicao (U).
void svd3(const M3& a, M3* u, double s[3], M3* v) {
  std::vector<std::vector<double>> ata(3, std::vector<double>(3, 0));
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      for (int k = 0; k < 3; k++) ata[i][j] += a.at(k, i) * a.at(k, j);
  std::vector<double> val;
  std::vector<std::vector<double>> vet;
  jacobiSimetrica(ata, val, vet);
  // Ordem DECRESCENTE de valor singular.
  for (int i = 0; i < 3; i++) {
    const int k = 2 - i;
    s[i] = std::sqrt(std::max(0.0, val[k]));
    for (int j = 0; j < 3; j++) v->at(j, i) = vet[k][j];
  }
  // U = A V / s (coluna a coluna; s ~ 0 vira produto vetorial das outras).
  for (int i = 0; i < 3; i++) {
    V3 col{v->at(0, i), v->at(1, i), v->at(2, i)};
    V3 av = a.aplicar(col);
    if (s[i] > 1e-12) {
      av = (1.0 / s[i]) * av;
    } else {
      const V3 u0{u->at(0, 0), u->at(1, 0), u->at(2, 0)};
      const V3 u1{u->at(0, 1), u->at(1, 1), u->at(2, 1)};
      av = unitario(cruz(u0, u1));
    }
    u->at(0, i) = av.x;
    u->at(1, i) = av.y;
    u->at(2, i) = av.z;
  }
}

// ------------------------------------------------------------ essencial

double sampson(const M3& e, double ax, double ay, double bx, double by) {
  const V3 a{ax, ay, 1}, b{bx, by, 1};
  const V3 ea = e.aplicar(a);
  const V3 etb = e.transposta().aplicar(b);
  const double num = dot(b, ea);
  const double den = ea.x * ea.x + ea.y * ea.y + etb.x * etb.x + etb.y * etb.y;
  return den < 1e-18 ? 1e18 : std::fabs(num) / std::sqrt(den);
}

// 8 pontos: nucleo do sistema, depois projecao no cone essencial (valores
// singulares 1, 1, 0).
bool essencialDe(const std::vector<std::pair<int, int>>& pares,
                 const std::vector<double>& ax, const std::vector<double>& ay,
                 const std::vector<double>& bx, const std::vector<double>& by,
                 const std::vector<int>& usar, M3* e) {
  std::vector<std::vector<double>> linhas;
  for (const int i : usar) {
    const double x1 = ax[i], y1 = ay[i], x2 = bx[i], y2 = by[i];
    linhas.push_back({x2 * x1, x2 * y1, x2, y2 * x1, y2 * y1, y2, x1, y1, 1});
  }
  const auto n = nucleoDe(linhas, 9);
  M3 bruto;
  for (int i = 0; i < 9; i++) bruto.m[i] = n[i];
  M3 u, v;
  double s[3];
  svd3(bruto, &u, s, &v);
  // Projeta: S = diag(1, 1, 0).
  M3 sig;
  sig.m[0] = 1; sig.m[4] = 1; sig.m[8] = 0;
  sig.m[1] = sig.m[2] = sig.m[3] = sig.m[5] = sig.m[6] = sig.m[7] = 0;
  *e = u * sig * v.transposta();
  return true;
}

struct DuasVistas {
  M3 e;
  std::vector<int> inliers;
};

bool essencialRansac(const std::vector<double>& ax,
                     const std::vector<double>& ay,
                     const std::vector<double>& bx,
                     const std::vector<double>& by, double limiar,
                     uint64_t salt, DuasVistas* saida) {
  const int n = (int)ax.size();
  if (n < 12) return false;
  Sorteio rng(kSemente + salt);
  std::vector<int> melhor;
  M3 melhorE;
  const int tentativas = 320;
  for (int t = 0; t < tentativas; t++) {
    std::vector<int> usar;
    while ((int)usar.size() < 8) {
      const int i = rng.ate(n);
      bool tem = false;
      for (const int j : usar) tem = tem || j == i;
      if (!tem) usar.push_back(i);
    }
    M3 e;
    if (!essencialDe({}, ax, ay, bx, by, usar, &e)) continue;
    std::vector<int> dentro;
    for (int i = 0; i < n; i++)
      if (sampson(e, ax[i], ay[i], bx[i], by[i]) < limiar) dentro.push_back(i);
    if (dentro.size() > melhor.size()) {
      melhor = dentro;
      melhorE = e;
    }
  }
  if ((int)melhor.size() < 12) return false;
  // Reajusta com o consenso inteiro.
  essencialDe({}, ax, ay, bx, by, melhor, &melhorE);
  saida->inliers.clear();
  for (int i = 0; i < n; i++)
    if (sampson(melhorE, ax[i], ay[i], bx[i], by[i]) < limiar)
      saida->inliers.push_back(i);
  saida->e = melhorE;
  return true;
}

// Duas rotacoes e uma direcao saem da essencial; a cheiralidade escolhe.
void posesDaEssencial(const M3& e, M3 r[2], V3* t) {
  M3 u, v;
  double s[3];
  svd3(e, &u, s, &v);
  if (u.determinante() < 0)
    for (int i = 0; i < 3; i++) u.at(i, 2) = -u.at(i, 2);
  if (v.determinante() < 0)
    for (int i = 0; i < 3; i++) v.at(i, 2) = -v.at(i, 2);
  M3 w;
  w.m[0] = 0; w.m[1] = -1; w.m[2] = 0;
  w.m[3] = 1; w.m[4] = 0; w.m[5] = 0;
  w.m[6] = 0; w.m[7] = 0; w.m[8] = 1;
  r[0] = rotacaoMaisProxima(u * w * v.transposta());
  r[1] = rotacaoMaisProxima(u * w.transposta() * v.transposta());
  *t = {u.at(0, 2), u.at(1, 2), u.at(2, 2)};
}

// A HOMOGRAFIA DECOMPOSTA (Faugeras): de H calibrada saem R, t/d e a
// normal do plano. E o unico caminho certo quando a cena e PLANA — o
// chao filmado de lado — porque ali a essencial degenera de proposito:
// pontos coplanares admitem uma familia inteira de essenciais.
struct SolucaoH {
  M3 r;
  V3 t;  // t/d, com d a distancia do plano
  V3 n;
};

// Devolve 1 quando H e (quase) uma rotacao pura — o caso tripe — e 0 no
// caso geral, com as quatro solucoes candidatas em `saida`.
int decomporHomografia(const M3& hNorm, std::vector<SolucaoH>* saida) {
  M3 u, v;
  double d[3];
  svd3(hNorm, &u, d, &v);
  const double d1 = d[0], d2 = d[1], d3 = d[2];
  if (d3 < 1e-12 || d1 / d3 < 1.01) return 1;
  const double s = u.determinante() * v.determinante();
  const double x1 =
      std::sqrt(std::max(0.0, (d1 * d1 - d2 * d2) / (d1 * d1 - d3 * d3)));
  const double x3 =
      std::sqrt(std::max(0.0, (d2 * d2 - d3 * d3) / (d1 * d1 - d3 * d3)));
  const double sinT =
      std::sqrt(std::max(0.0, (d1 * d1 - d2 * d2) * (d2 * d2 - d3 * d3))) /
      ((d1 + d3) * d2);
  const double cosT = (d2 * d2 + d1 * d3) / ((d1 + d3) * d2);
  for (int e1 = -1; e1 <= 1; e1 += 2)
    for (int e3 = -1; e3 <= 1; e3 += 2) {
      const double st = e1 * e3 * sinT;
      M3 rp;
      rp.m[0] = cosT; rp.m[1] = 0; rp.m[2] = -st;
      rp.m[3] = 0;    rp.m[4] = 1; rp.m[5] = 0;
      rp.m[6] = st;   rp.m[7] = 0; rp.m[8] = cosT;
      SolucaoH sol;
      sol.r = u * rp * v.transposta();
      if (s < 0)
        for (int k = 0; k < 9; k++) sol.r.m[k] = -sol.r.m[k];
      sol.r = rotacaoMaisProxima(sol.r);
      const V3 tp{(d1 - d3) * e1 * x1, 0, -(d1 - d3) * e3 * x3};
      const V3 np{e1 * x1, 0, e3 * x3};
      sol.t = u.aplicar(tp);
      sol.n = v.aplicar(np);
      saida->push_back(sol);
    }
  return 0;
}

// Triangulacao DLT de um ponto visto por varias cameras (normalizado).
bool triangular(const std::vector<const Pose*>& vistas,
                const std::vector<std::pair<double, double>>& xy, V3* X) {
  if (vistas.size() < 2) return false;
  std::vector<std::vector<double>> linhas;
  for (size_t i = 0; i < vistas.size(); i++) {
    const M3& R = vistas[i]->R;
    const V3& t = vistas[i]->t;
    const double x = xy[i].first, y = xy[i].second;
    linhas.push_back({x * R.at(2, 0) - R.at(0, 0), x * R.at(2, 1) - R.at(0, 1),
                      x * R.at(2, 2) - R.at(0, 2), x * t.z - t.x});
    linhas.push_back({y * R.at(2, 0) - R.at(1, 0), y * R.at(2, 1) - R.at(1, 1),
                      y * R.at(2, 2) - R.at(1, 2), y * t.z - t.y});
  }
  const auto v = nucleoDe(linhas, 4);
  if (std::fabs(v[3]) < 1e-12) return false;
  *X = {v[0] / v[3], v[1] / v[3], v[2] / v[3]};
  return true;
}

double profundidade(const Pose& p, V3 X) { return (p.R.aplicar(X) + p.t).z; }

// ----------------------------------------------------------- homografia

bool homografiaDe(const std::vector<double>& ax, const std::vector<double>& ay,
                  const std::vector<double>& bx, const std::vector<double>& by,
                  const std::vector<int>& usar, M3* h) {
  std::vector<std::vector<double>> linhas;
  for (const int i : usar) {
    const double x = ax[i], y = ay[i], X = bx[i], Y = by[i];
    linhas.push_back({-x, -y, -1, 0, 0, 0, X * x, X * y, X});
    linhas.push_back({0, 0, 0, -x, -y, -1, Y * x, Y * y, Y});
  }
  const auto n = nucleoDe(linhas, 9);
  for (int i = 0; i < 9; i++) h->m[i] = n[i];
  // O nucleo devolve H a menos de SINAL, e o sinal importa para quem
  // decompoe: uma homografia de movimento real tem determinante
  // positivo, e -H viraria uma "rotacao" de determinante -1 la na frente.
  if (h->determinante() < 0)
    for (int i = 0; i < 9; i++) h->m[i] = -h->m[i];
  return std::fabs(h->m[8]) > 1e-12;
}

double erroDeH(const M3& h, double ax, double ay, double bx, double by) {
  const V3 p = h.aplicar({ax, ay, 1});
  if (std::fabs(p.z) < 1e-12) return 1e18;
  const double dx = p.x / p.z - bx, dy = p.y / p.z - by;
  return std::sqrt(dx * dx + dy * dy);
}

bool homografiaRansac(const std::vector<double>& ax,
                      const std::vector<double>& ay,
                      const std::vector<double>& bx,
                      const std::vector<double>& by, double limiar,
                      uint64_t salt, M3* h, std::vector<int>* inliers) {
  const int n = (int)ax.size();
  if (n < 8) return false;
  Sorteio rng(kSemente + salt * 7 + 1);
  std::vector<int> melhor;
  M3 melhorH;
  for (int t = 0; t < 200; t++) {
    std::vector<int> usar;
    while ((int)usar.size() < 4) {
      const int i = rng.ate(n);
      bool tem = false;
      for (const int j : usar) tem = tem || j == i;
      if (!tem) usar.push_back(i);
    }
    M3 cand;
    if (!homografiaDe(ax, ay, bx, by, usar, &cand)) continue;
    std::vector<int> dentro;
    for (int i = 0; i < n; i++)
      if (erroDeH(cand, ax[i], ay[i], bx[i], by[i]) < limiar)
        dentro.push_back(i);
    if (dentro.size() > melhor.size()) {
      melhor = dentro;
      melhorH = cand;
    }
  }
  if ((int)melhor.size() < 8) return false;
  homografiaDe(ax, ay, bx, by, melhor, &melhorH);
  if (inliers) {
    inliers->clear();
    for (int i = 0; i < n; i++)
      if (erroDeH(melhorH, ax[i], ay[i], bx[i], by[i]) < limiar)
        inliers->push_back(i);
  }
  *h = melhorH;
  return true;
}

// -------------------------------------------------------- pose por LM

// Refina uma pose contra a nuvem (so a pose; a nuvem fica quieta). Huber
// em pixels. Devolve o RMS robusto final.
double refinarPose(const Camera& cam, const std::vector<V3>& X,
                   const std::vector<std::pair<double, double>>& px, Pose* p,
                   double huber = 2.0) {
  const int n = (int)X.size();
  if (n < 4) return 1e18;
  double lambda = 1e-3;
  auto custo = [&](const Pose& q) {
    double soma = 0;
    for (int i = 0; i < n; i++) {
      double u, v;
      if (!cam.projetar(q.R, q.t, X[i], &u, &v)) {
        soma += 100;
        continue;
      }
      const double dx = u - px[i].first, dy = v - px[i].second;
      const double r = std::sqrt(dx * dx + dy * dy);
      const double w = pesoHuber(r, huber);
      soma += w * r * r;
    }
    return soma;
  };
  double atual = custo(*p);
  for (int it = 0; it < 20; it++) {
    // Jacobiano numerico 2n x 6.
    std::vector<double> H(36, 0), g(6, 0);
    const double eps = 1e-5;
    for (int i = 0; i < n; i++) {
      double u0, v0;
      if (!cam.projetar(p->R, p->t, X[i], &u0, &v0)) continue;
      const double rx = u0 - px[i].first, ry = v0 - px[i].second;
      const double rn = std::sqrt(rx * rx + ry * ry);
      const double w = pesoHuber(rn, huber);
      double J[2][6];
      for (int k = 0; k < 6; k++) {
        Pose q = *p;
        if (k < 3) {
          V3 d{0, 0, 0};
          (&d.x)[k] = eps;
          q.R = rodrigues(d) * p->R;
        } else {
          (&q.t.x)[k - 3] += eps;
        }
        double u1, v1;
        if (!cam.projetar(q.R, q.t, X[i], &u1, &v1)) {
          u1 = u0;
          v1 = v0;
        }
        J[0][k] = (u1 - u0) / eps;
        J[1][k] = (v1 - v0) / eps;
      }
      for (int a = 0; a < 6; a++) {
        g[a] += w * (J[0][a] * rx + J[1][a] * ry);
        for (int b = a; b < 6; b++)
          H[a * 6 + b] += w * (J[0][a] * J[0][b] + J[1][a] * J[1][b]);
      }
    }
    for (int a = 0; a < 6; a++)
      for (int b = 0; b < a; b++) H[a * 6 + b] = H[b * 6 + a];
    std::vector<std::vector<double>> A(6, std::vector<double>(6));
    std::vector<double> B(6);
    for (int a = 0; a < 6; a++) {
      for (int b = 0; b < 6; b++) A[a][b] = H[a * 6 + b];
      A[a][a] *= (1 + lambda);
      B[a] = -g[a];
    }
    std::vector<double> d;
    if (!resolverSistema(A, B, d)) break;
    Pose q = *p;
    q.R = rotacaoMaisProxima(rodrigues({d[0], d[1], d[2]}) * p->R);
    q.t = q.t + V3{d[3], d[4], d[5]};
    const double novo = custo(q);
    if (novo < atual) {
      *p = q;
      const bool parou = (atual - novo) < 1e-8 * (atual + 1e-12);
      atual = novo;
      lambda = std::max(1e-7, lambda / 3);
      if (parou) break;
    } else {
      lambda *= 4;
      if (lambda > 1e6) break;
    }
  }
  return std::sqrt(atual / std::max(1, n));
}

// A POSE DE UMA NUVEM PLANA sai da homografia plano -> imagem (metodo
// das colunas): H = [r1 r2 t] quando o plano e z = 0. O DLT de 6 pontos
// DEGENERA com pontos coplanares — e um chao filmado e exatamente isso —
// entao o PnP escolhe o gerador de hipoteses pela forma da nuvem.
bool poseDoPlano(const Camera& cam, const std::vector<V3>& X,
                 const std::vector<std::pair<double, double>>& px,
                 const std::vector<int>& usar, Pose* saida) {
  // Base do plano dos pontos usados.
  V3 c{0, 0, 0};
  for (const int i : usar) c = c + X[i];
  c = (1.0 / usar.size()) * c;
  std::vector<std::vector<double>> cov(3, std::vector<double>(3, 0));
  for (const int i : usar) {
    const V3 d = X[i] - c;
    const double v[3] = {d.x, d.y, d.z};
    for (int a = 0; a < 3; a++)
      for (int b = 0; b < 3; b++) cov[a][b] += v[a] * v[b];
  }
  std::vector<double> val;
  std::vector<std::vector<double>> vet;
  jacobiSimetrica(cov, val, vet);
  const V3 e1{vet[2][0], vet[2][1], vet[2][2]};
  const V3 e2{vet[1][0], vet[1][1], vet[1][2]};

  // Homografia (u, v) do plano -> imagem normalizada.
  std::vector<double> pu, pv, nx, ny;
  for (const int i : usar) {
    const V3 d = X[i] - c;
    pu.push_back(dot(d, e1));
    pv.push_back(dot(d, e2));
    double ax, ay;
    cam.normalizar(px[i].first, px[i].second, &ax, &ay);
    nx.push_back(ax);
    ny.push_back(ay);
  }
  std::vector<int> todos;
  for (int i = 0; i < (int)usar.size(); i++) todos.push_back(i);
  M3 h;
  if (!homografiaDe(pu, pv, nx, ny, todos, &h)) return false;

  // Colunas: r1 = l h1, r2 = l h2, r3 = r1 x r2, t = l h3.
  const V3 h1{h.at(0, 0), h.at(1, 0), h.at(2, 0)};
  const V3 h2{h.at(0, 1), h.at(1, 1), h.at(2, 1)};
  const V3 h3{h.at(0, 2), h.at(1, 2), h.at(2, 2)};
  const double n1 = norma(h1), n2 = norma(h2);
  if (n1 < 1e-12 || n2 < 1e-12) return false;
  double l = 2.0 / (n1 + n2);
  // O sinal: o plano tem de ficar na FRENTE da camera (t_z > 0).
  if (h3.z * l < 0) l = -l;
  V3 r1 = l * h1;
  V3 r2 = l * h2;
  r2 = unitario(r2 - dot(r1, r2) * r1);
  r1 = unitario(r1);
  const V3 r3 = cruz(r1, r2);
  M3 rp;
  rp.m[0] = r1.x; rp.m[1] = r2.x; rp.m[2] = r3.x;
  rp.m[3] = r1.y; rp.m[4] = r2.y; rp.m[5] = r3.y;
  rp.m[6] = r1.z; rp.m[7] = r2.z; rp.m[8] = r3.z;
  rp = rotacaoMaisProxima(rp);
  const V3 tp = l * h3;

  // Do referencial do plano para o mundo: Xc = Rp * B^T (X - c) + tp.
  const V3 e3 = cruz(e1, e2);
  M3 bT;
  bT.m[0] = e1.x; bT.m[1] = e1.y; bT.m[2] = e1.z;
  bT.m[3] = e2.x; bT.m[4] = e2.y; bT.m[5] = e2.z;
  bT.m[6] = e3.x; bT.m[7] = e3.y; bT.m[8] = e3.z;
  saida->R = rotacaoMaisProxima(rp * bT);
  saida->t = tp - saida->R.aplicar(c);
  saida->ok = true;
  return true;
}

// PnP com RANSAC: hipoteses por DLT de 6 pontos OU pela homografia do
// plano (nuvem chata), consenso, refino final por LM.
bool pnp(const Camera& cam, const std::vector<V3>& X,
         const std::vector<std::pair<double, double>>& px, uint64_t salt,
         Pose* p, std::vector<int>* inliers, double limiar = 4.0) {
  const int n = (int)X.size();
  if (n < 6) return false;

  // A forma da nuvem decide o gerador: espalhamento minimo pequeno
  // diante do maximo = todo mundo num plano.
  bool plana = false;
  {
    V3 c{0, 0, 0};
    for (const auto& v : X) c = c + v;
    c = (1.0 / n) * c;
    std::vector<std::vector<double>> cov(3, std::vector<double>(3, 0));
    for (const auto& q : X) {
      const V3 d = q - c;
      const double v[3] = {d.x, d.y, d.z};
      for (int a = 0; a < 3; a++)
        for (int b = 0; b < 3; b++) cov[a][b] += v[a] * v[b];
    }
    std::vector<double> val;
    std::vector<std::vector<double>> vet;
    jacobiSimetrica(cov, val, vet);
    plana = val[0] < 0.01 * val[2];
  }

  Sorteio rng(kSemente + salt * 13 + 5);
  std::vector<int> melhor;
  Pose melhorPose;
  for (int t = 0; t < 200; t++) {
    std::vector<int> usar;
    while ((int)usar.size() < 6) {
      const int i = rng.ate(n);
      bool tem = false;
      for (const int j : usar) tem = tem || j == i;
      if (!tem) usar.push_back(i);
    }
    if (plana) {
      Pose cand;
      if (!poseDoPlano(cam, X, px, usar, &cand)) continue;
      int naFrente = 0;
      for (const int i : usar)
        if (profundidade(cand, X[i]) > 0) naFrente++;
      if (naFrente < 5) continue;
      std::vector<int> dentro;
      for (int i = 0; i < n; i++) {
        double u, w;
        if (!cam.projetar(cand.R, cand.t, X[i], &u, &w)) continue;
        const double dx = u - px[i].first, dy = w - px[i].second;
        if (dx * dx + dy * dy < limiar * limiar) dentro.push_back(i);
      }
      if (dentro.size() > melhor.size()) {
        melhor = dentro;
        melhorPose = cand;
      }
      continue;
    }
    // DLT de P = [R | t] em coordenadas normalizadas (sem distorcao).
    std::vector<std::vector<double>> linhas;
    for (const int i : usar) {
      double nx, ny;
      cam.normalizar(px[i].first, px[i].second, &nx, &ny);
      const V3& q = X[i];
      linhas.push_back({q.x, q.y, q.z, 1, 0, 0, 0, 0, -nx * q.x, -nx * q.y,
                        -nx * q.z, -nx});
      linhas.push_back({0, 0, 0, 0, q.x, q.y, q.z, 1, -ny * q.x, -ny * q.y,
                        -ny * q.z, -ny});
    }
    const auto v = nucleoDe(linhas, 12);
    M3 R;
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) R.at(i, j) = v[i * 4 + j];
    // Escala pela norma media das linhas; sinal pela profundidade.
    double escala = 0;
    for (int i = 0; i < 3; i++)
      escala += norma({R.at(i, 0), R.at(i, 1), R.at(i, 2)});
    escala /= 3;
    if (escala < 1e-12) continue;
    Pose cand;
    for (int i = 0; i < 9; i++) cand.R.m[i] = R.m[i] / escala;
    cand.t = {v[3] / escala, v[7] / escala, v[11] / escala};
    if (cand.R.determinante() < 0) {
      for (int i = 0; i < 9; i++) cand.R.m[i] = -cand.R.m[i];
      cand.t = -1.0 * cand.t;
    }
    cand.R = rotacaoMaisProxima(cand.R);
    int naFrente = 0;
    for (const int i : usar)
      if (profundidade(cand, X[i]) > 0) naFrente++;
    if (naFrente < 5) continue;
    std::vector<int> dentro;
    for (int i = 0; i < n; i++) {
      double u, w;
      if (!cam.projetar(cand.R, cand.t, X[i], &u, &w)) continue;
      const double dx = u - px[i].first, dy = w - px[i].second;
      if (dx * dx + dy * dy < limiar * limiar) dentro.push_back(i);
    }
    if (dentro.size() > melhor.size()) {
      melhor = dentro;
      melhorPose = cand;
    }
  }
  if ((int)melhor.size() < 6) return false;
  std::vector<V3> Xin;
  std::vector<std::pair<double, double>> pin;
  for (const int i : melhor) {
    Xin.push_back(X[i]);
    pin.push_back(px[i]);
  }
  refinarPose(cam, Xin, pin, &melhorPose);
  *p = melhorPose;
  if (inliers) *inliers = melhor;
  return true;
}

// ------------------------------------------------------------------ cena

struct QuadroDoPar {
  int a = 0, b = 0;
  int comuns = 0;
  double paralaxe = 0;  // residuo da homografia, em px
};

}  // namespace

struct at2_cena {
  int w = 0, h = 0, quadros = 0, fps = 0;
  std::map<int, Trilha> trilhas;

  Camera cam;
  bool tripe = false;
  double erro = 0;
  int codigo = AT2_ERR_NAO_CONVERGIU;
  std::vector<std::pair<int, Pose>> posesPorQuadro;
  struct PontoSaida {
    int id;
    V3 X;
    double erro;
    int vistas;
  };
  std::vector<PontoSaida> saida;

  at2_progresso progresso = nullptr;
  void* alvo = nullptr;
  void dizer(double fracao, int a, int b) {
    if (progresso) progresso(2, fracao, a, b, alvo);
  }

  // Pares de observacoes de dois quadros, em pixels.
  int paresDe(int qa, int qb, std::vector<int>* ids, std::vector<double>* ax,
              std::vector<double>* ay, std::vector<double>* bx,
              std::vector<double>* by) const {
    ids->clear();
    ax->clear(); ay->clear(); bx->clear(); by->clear();
    for (const auto& [id, t] : trilhas) {
      const Obs* a = t.em(qa);
      const Obs* b = t.em(qb);
      if (!a || !b) continue;
      ids->push_back(id);
      ax->push_back(a->x);
      ay->push_back(a->y);
      bx->push_back(b->x);
      by->push_back(b->y);
    }
    return (int)ids->size();
  }

  int resolver(double focalPx, int modo);
  int resolverTripe(const std::vector<int>& marcos);
  void posesDeTodosOsQuadros(const std::map<int, Pose>& chaves);
  // intrinsecos: 0 = nada, 1 = focal livre, 2 = focal e k1 livres.
  void ajusteDeFeixes(std::map<int, Pose>& kfs, int intrinsecos,
                      int iteracoes);
  double erroGlobal();
  void fichaDosPontos();
};

// ------------------------------------------------- ajuste de feixes (BA)

// LM com Schur: os pontos sao eliminados e o sistema que sobra e o das
// cameras (6 por quadro-chave) + 2 intrinsecos (f, k1). Huber em pixels.
void at2_cena::ajusteDeFeixes(std::map<int, Pose>& kfs, int intrinsecos,
                              int iteracoes) {
  const double huber = 2.0;
  std::vector<int> ordem;
  for (auto& [q, p] : kfs) ordem.push_back(q);
  std::map<int, int> indice;
  for (int i = 0; i < (int)ordem.size(); i++) indice[ordem[i]] = i;
  const int K = (int)ordem.size();
  const int NG = intrinsecos;
  const int NC = 6 * K + NG;

  std::vector<Trilha*> pts;
  for (auto& [id, t] : trilhas)
    if (t.temX) pts.push_back(&t);

  auto custoTotal = [&]() {
    double soma = 0;
    int n = 0;
    for (const auto* t : pts)
      for (const auto& o : t->obs) {
        const auto it = kfs.find(o.quadro);
        if (it == kfs.end()) continue;
        double u, v;
        if (!cam.projetar(it->second.R, it->second.t, t->X, &u, &v)) {
          soma += 100;
          n++;
          continue;
        }
        const double dx = u - o.x, dy = v - o.y;
        const double r = std::sqrt(dx * dx + dy * dy);
        soma += pesoHuber(r, huber) * r * r;
        n++;
      }
    return std::pair<double, int>(soma, n);
  };

  double lambda = 1e-4;
  auto [atual, nres] = custoTotal();
  if (nres < 30) return;

  for (int it = 0; it < iteracoes; it++) {
    // Blocos: por ponto, Hpp (3x3), bp (3), e Hpc por camera envolvida.
    std::vector<double> Hcc((size_t)NC * NC, 0), bc(NC, 0);
    struct BlocoPonto {
      double Hpp[9] = {0};
      double bp[3] = {0};
      std::map<int, std::array<double, 18>> Hcp;  // indice de kf -> 6x3
    };
    std::vector<BlocoPonto> blocos(pts.size());

    const double eps = 1e-5;
    for (size_t pi = 0; pi < pts.size(); pi++) {
      Trilha* t = pts[pi];
      BlocoPonto& B = blocos[pi];
      for (const auto& o : t->obs) {
        const auto itk = kfs.find(o.quadro);
        if (itk == kfs.end()) continue;
        Pose& P = itk->second;
        double u0, v0;
        if (!cam.projetar(P.R, P.t, t->X, &u0, &v0)) continue;
        const double rx = u0 - o.x, ry = v0 - o.y;
        const double rn = std::sqrt(rx * rx + ry * ry);
        const double wgt = pesoHuber(rn, huber);

        // Jacobianos numericos: 6 da camera, 3 do ponto, NG globais.
        double Jc[2][6], Jp[2][3], Jg[2][2] = {{0, 0}, {0, 0}};
        for (int k = 0; k < 6; k++) {
          Pose q = P;
          if (k < 3) {
            V3 d{0, 0, 0};
            (&d.x)[k] = eps;
            q.R = rodrigues(d) * P.R;
          } else {
            (&q.t.x)[k - 3] += eps;
          }
          double u1, v1;
          if (!cam.projetar(q.R, q.t, t->X, &u1, &v1)) {
            u1 = u0;
            v1 = v0;
          }
          Jc[0][k] = (u1 - u0) / eps;
          Jc[1][k] = (v1 - v0) / eps;
        }
        for (int k = 0; k < 3; k++) {
          V3 X = t->X;
          (&X.x)[k] += eps;
          double u1, v1;
          if (!cam.projetar(P.R, P.t, X, &u1, &v1)) {
            u1 = u0;
            v1 = v0;
          }
          Jp[0][k] = (u1 - u0) / eps;
          Jp[1][k] = (v1 - v0) / eps;
        }
        if (NG > 0) {
          Camera c2 = cam;
          c2.f += cam.f * 1e-5;
          double u1, v1;
          if (!c2.projetar(P.R, P.t, t->X, &u1, &v1)) {
            u1 = u0;
            v1 = v0;
          }
          Jg[0][0] = (u1 - u0) / (cam.f * 1e-5);
          Jg[1][0] = (v1 - v0) / (cam.f * 1e-5);
          Camera c3 = cam;
          c3.k1 += 1e-6;
          if (!c3.projetar(P.R, P.t, t->X, &u1, &v1)) {
            u1 = u0;
            v1 = v0;
          }
          Jg[0][1] = (u1 - u0) / 1e-6;
          Jg[1][1] = (v1 - v0) / 1e-6;
        }

        const int ci = indice[o.quadro] * 6;
        // Hcc e bc (camera x camera, camera x globais, globais x globais).
        for (int a = 0; a < 6; a++) {
          bc[ci + a] += wgt * (Jc[0][a] * rx + Jc[1][a] * ry);
          for (int b = a; b < 6; b++)
            Hcc[(size_t)(ci + a) * NC + (ci + b)] +=
                wgt * (Jc[0][a] * Jc[0][b] + Jc[1][a] * Jc[1][b]);
          for (int g = 0; g < NG; g++)
            Hcc[(size_t)(ci + a) * NC + (6 * K + g)] +=
                wgt * (Jc[0][a] * Jg[0][g] + Jc[1][a] * Jg[1][g]);
        }
        for (int g = 0; g < NG; g++) {
          bc[6 * K + g] += wgt * (Jg[0][g] * rx + Jg[1][g] * ry);
          for (int g2 = g; g2 < NG; g2++)
            Hcc[(size_t)(6 * K + g) * NC + (6 * K + g2)] +=
                wgt * (Jg[0][g] * Jg[0][g2] + Jg[1][g] * Jg[1][g2]);
        }
        // Hpp, bp, Hcp.
        for (int a = 0; a < 3; a++) {
          B.bp[a] += wgt * (Jp[0][a] * rx + Jp[1][a] * ry);
          for (int b = 0; b < 3; b++)
            B.Hpp[a * 3 + b] +=
                wgt * (Jp[0][a] * Jp[0][b] + Jp[1][a] * Jp[1][b]);
        }
        auto& Hcp = B.Hcp[indice[o.quadro]];
        for (int a = 0; a < 6; a++)
          for (int b = 0; b < 3; b++)
            Hcp[a * 3 + b] += wgt * (Jc[0][a] * Jp[0][b] + Jc[1][a] * Jp[1][b]);
      }
    }

    // OS GLOBAIS TAMBEM ACOPLAM COM OS PONTOS. Para nao complicar o Schur,
    // o acoplamento global-ponto e dobrado no proprio laco acima usando o
    // truque de tratar os globais como "camera K": aqui ele e refeito
    // explicitamente, somando Hgp por ponto.
    std::vector<std::array<double, 6>> Hgp_por_ponto;
    if (NG > 0) {
      Hgp_por_ponto.assign(pts.size(), {0, 0, 0, 0, 0, 0});
      for (size_t pi = 0; pi < pts.size(); pi++) {
        Trilha* t = pts[pi];
        for (const auto& o : t->obs) {
          const auto itk = kfs.find(o.quadro);
          if (itk == kfs.end()) continue;
          Pose& P = itk->second;
          double u0, v0;
          if (!cam.projetar(P.R, P.t, t->X, &u0, &v0)) continue;
          const double rx = u0 - o.x, ry = v0 - o.y;
          const double wgt = pesoHuber(std::sqrt(rx * rx + ry * ry), huber);
          double Jp[2][3], Jg[2][2];
          for (int k = 0; k < 3; k++) {
            V3 X = t->X;
            (&X.x)[k] += eps;
            double u1, v1;
            if (!cam.projetar(P.R, P.t, X, &u1, &v1)) {
              u1 = u0;
              v1 = v0;
            }
            Jp[0][k] = (u1 - u0) / eps;
            Jp[1][k] = (v1 - v0) / eps;
          }
          Camera c2 = cam;
          c2.f += cam.f * 1e-5;
          double u1, v1;
          if (!c2.projetar(P.R, P.t, t->X, &u1, &v1)) {
            u1 = u0;
            v1 = v0;
          }
          Jg[0][0] = (u1 - u0) / (cam.f * 1e-5);
          Jg[1][0] = (v1 - v0) / (cam.f * 1e-5);
          Camera c3 = cam;
          c3.k1 += 1e-6;
          if (!c3.projetar(P.R, P.t, t->X, &u1, &v1)) {
            u1 = u0;
            v1 = v0;
          }
          Jg[0][1] = (u1 - u0) / 1e-6;
          Jg[1][1] = (v1 - v0) / 1e-6;
          for (int g = 0; g < NG; g++)
            for (int b = 0; b < 3; b++)
              Hgp_por_ponto[pi][g * 3 + b] +=
                  wgt * (Jg[0][g] * Jp[0][b] + Jg[1][g] * Jp[1][b]);
        }
      }
    }

    // Espelha a metade de baixo de Hcc.
    for (int a = 0; a < NC; a++)
      for (int b = 0; b < a; b++)
        Hcc[(size_t)a * NC + b] = Hcc[(size_t)b * NC + a];

    // Amortece e faz o Schur: Hcc - sum_p Hcp Hpp^-1 Hpc.
    std::vector<double> S = Hcc, rhs = bc;
    for (int a = 0; a < NC; a++) S[(size_t)a * NC + a] *= (1 + lambda);

    std::vector<std::array<double, 9>> HppInv(pts.size());
    bool degenerou = false;
    for (size_t pi = 0; pi < pts.size(); pi++) {
      double A[9];
      std::memcpy(A, blocos[pi].Hpp, sizeof(A));
      for (int a = 0; a < 3; a++) A[a * 3 + a] *= (1 + lambda);
      // Inversa 3x3.
      const double det =
          A[0] * (A[4] * A[8] - A[5] * A[7]) -
          A[1] * (A[3] * A[8] - A[5] * A[6]) +
          A[2] * (A[3] * A[7] - A[4] * A[6]);
      if (std::fabs(det) < 1e-12) {
        std::memset(HppInv[pi].data(), 0, sizeof(double) * 9);
        continue;
      }
      auto& I = HppInv[pi];
      I[0] = (A[4] * A[8] - A[5] * A[7]) / det;
      I[1] = (A[2] * A[7] - A[1] * A[8]) / det;
      I[2] = (A[1] * A[5] - A[2] * A[4]) / det;
      I[3] = (A[5] * A[6] - A[3] * A[8]) / det;
      I[4] = (A[0] * A[8] - A[2] * A[6]) / det;
      I[5] = (A[2] * A[3] - A[0] * A[5]) / det;
      I[6] = (A[3] * A[7] - A[4] * A[6]) / det;
      I[7] = (A[1] * A[6] - A[0] * A[7]) / det;
      I[8] = (A[0] * A[4] - A[1] * A[3]) / det;
    }
    (void)degenerou;

    for (size_t pi = 0; pi < pts.size(); pi++) {
      const auto& B = blocos[pi];
      const auto& I = HppInv[pi];
      // rhs_c -= Hcp Hpp^-1 bp ; S -= Hcp Hpp^-1 Hpc (por pares de blocos).
      std::vector<std::pair<int, const std::array<double, 18>*>> cams;
      for (const auto& [ci, Hcp] : B.Hcp) cams.push_back({ci, &Hcp});
      auto linhaGlobal = [&](int g, int b) {
        return NG > 0 ? Hgp_por_ponto[pi][g * 3 + b] : 0.0;
      };
      // Hpp^-1 bp:
      double ib[3] = {
          I[0] * B.bp[0] + I[1] * B.bp[1] + I[2] * B.bp[2],
          I[3] * B.bp[0] + I[4] * B.bp[1] + I[5] * B.bp[2],
          I[6] * B.bp[0] + I[7] * B.bp[1] + I[8] * B.bp[2],
      };
      for (const auto& [ci, Hcp] : cams) {
        for (int a = 0; a < 6; a++) {
          double s = 0;
          for (int b = 0; b < 3; b++) s += (*Hcp)[a * 3 + b] * ib[b];
          rhs[ci * 6 + a] -= s;
        }
      }
      for (int g = 0; g < NG; g++) {
        double s = 0;
        for (int b = 0; b < 3; b++) s += linhaGlobal(g, b) * ib[b];
        rhs[6 * K + g] -= s;
      }
      // S: blocos camera x camera.
      for (const auto& [ci, Hci] : cams) {
        // W_i = Hcp_i * Hpp^-1  (6x3)
        double Wi[18];
        for (int a = 0; a < 6; a++)
          for (int b = 0; b < 3; b++) {
            double s = 0;
            for (int k = 0; k < 3; k++) s += (*Hci)[a * 3 + k] * I[k * 3 + b];
            Wi[a * 3 + b] = s;
          }
        for (const auto& [cj, Hcj] : cams) {
          for (int a = 0; a < 6; a++)
            for (int b = 0; b < 6; b++) {
              double s = 0;
              for (int k = 0; k < 3; k++) s += Wi[a * 3 + k] * (*Hcj)[b * 3 + k];
              S[(size_t)(ci * 6 + a) * NC + (cj * 6 + b)] -= s;
            }
        }
        if (NG > 0)
          for (int a = 0; a < 6; a++)
            for (int g = 0; g < NG; g++) {
              double s = 0;
              for (int k = 0; k < 3; k++) s += Wi[a * 3 + k] * linhaGlobal(g, k);
              S[(size_t)(ci * 6 + a) * NC + (6 * K + g)] -= s;
              S[(size_t)(6 * K + g) * NC + (ci * 6 + a)] -= s;
            }
      }
      if (NG > 0) {
        double Wg[6];
        for (int g = 0; g < NG; g++)
          for (int b = 0; b < 3; b++) {
            double s = 0;
            for (int k = 0; k < 3; k++) s += linhaGlobal(g, k) * I[k * 3 + b];
            Wg[g * 3 + b] = s;
          }
        for (int g = 0; g < NG; g++)
          for (int g2 = 0; g2 < NG; g2++) {
            double s = 0;
            for (int k = 0; k < 3; k++) s += Wg[g * 3 + k] * linhaGlobal(g2, k);
            S[(size_t)(6 * K + g) * NC + (6 * K + g2)] -= s;
          }
      }
    }

    // A PRIMEIRA CAMERA E O CALIBRE: fica presa (senao o sistema inteiro
    // flutua — a origem e a escala do mundo sao livres por natureza).
    for (int a = 0; a < 6; a++) {
      for (int b = 0; b < NC; b++) {
        S[(size_t)a * NC + b] = 0;
        S[(size_t)b * NC + a] = 0;
      }
      S[(size_t)a * NC + a] = 1;
      rhs[a] = 0;
    }
    // E a DISTANCIA da segunda prende a escala: o eixo dominante da
    // translacao dela fica quieto.
    if (K > 1) {
      int eixo = 3;
      double maior = 0;
      const Pose& p1 = kfs[ordem[1]];
      const double vs[3] = {p1.t.x, p1.t.y, p1.t.z};
      for (int k = 0; k < 3; k++)
        if (std::fabs(vs[k]) > maior) {
          maior = std::fabs(vs[k]);
          eixo = 3 + k;
        }
      const int a = 6 + eixo;
      for (int b = 0; b < NC; b++) {
        S[(size_t)a * NC + b] = 0;
        S[(size_t)b * NC + a] = 0;
      }
      S[(size_t)a * NC + a] = 1;
      rhs[a] = 0;
    }

    std::vector<double> dc = rhs;
    std::vector<double> Scopia = S;
    if (!resolverSimetrico(Scopia, dc, NC)) {
      lambda *= 10;
      if (lambda > 1e8) break;
      continue;
    }

    // Passo dos pontos: dp = Hpp^-1 (bp - Hpc dc)  (com o sinal do LM:
    // resolve-se H d = -g, entao o que esta em dc ja e -delta... aqui o
    // rhs foi montado com +g, logo dc = -delta; aplica-se com sinal
    // trocado dos dois lados).
    std::map<int, Pose> novasKfs = kfs;
    for (int i = 0; i < K; i++) {
      Pose& p = novasKfs[ordem[i]];
      const double* d = &dc[i * 6];
      p.R = rotacaoMaisProxima(rodrigues({-d[0], -d[1], -d[2]}) * p.R);
      p.t = p.t + V3{-d[3], -d[4], -d[5]};
    }
    Camera novaCam = cam;
    if (NG > 0)
      novaCam.f = std::max(w * 0.3, std::min(w * 4.0, cam.f - dc[6 * K + 0]));
    if (NG > 1)
      novaCam.k1 = std::max(-0.5, std::min(0.5, cam.k1 - dc[6 * K + 1]));
    std::vector<V3> novosX(pts.size());
    for (size_t pi = 0; pi < pts.size(); pi++) {
      const auto& B = blocos[pi];
      const auto& I = HppInv[pi];
      double r[3] = {B.bp[0], B.bp[1], B.bp[2]};
      for (const auto& [ci, Hcp] : B.Hcp)
        for (int b = 0; b < 3; b++)
          for (int a = 0; a < 6; a++)
            r[b] -= Hcp[a * 3 + b] * (-dc[ci * 6 + a]);
      for (int b = 0; b < 3; b++)
        for (int g = 0; g < NG; g++)
          r[b] -= Hgp_por_ponto[pi][g * 3 + b] * (-dc[6 * K + g]);
      const double dp[3] = {
          I[0] * r[0] + I[1] * r[1] + I[2] * r[2],
          I[3] * r[0] + I[4] * r[1] + I[5] * r[2],
          I[6] * r[0] + I[7] * r[1] + I[8] * r[2],
      };
      novosX[pi] = pts[pi]->X - V3{dp[0], dp[1], dp[2]};
    }

    // Aceita ou rejeita o passo pelo custo de verdade.
    const Camera camAntes = cam;
    std::vector<V3> XAntes(pts.size());
    for (size_t pi = 0; pi < pts.size(); pi++) XAntes[pi] = pts[pi]->X;
    std::map<int, Pose> kfsAntes = kfs;

    cam = novaCam;
    for (size_t pi = 0; pi < pts.size(); pi++) pts[pi]->X = novosX[pi];
    kfs = novasKfs;
    const auto [novo, n2] = custoTotal();
    (void)n2;
    if (novo < atual) {
      const bool parou = (atual - novo) < 1e-7 * (atual + 1e-12);
      atual = novo;
      lambda = std::max(1e-8, lambda / 3);
      dizer(0.55 + 0.25 * (it + 1.0) / iteracoes, it + 1, iteracoes);
      if (parou) break;
    } else {
      cam = camAntes;
      for (size_t pi = 0; pi < pts.size(); pi++) pts[pi]->X = XAntes[pi];
      kfs = kfsAntes;
      lambda *= 6;
      if (lambda > 1e8) break;
    }
  }
}

// ------------------------------------------------ o resto da reconstrucao

void at2_cena::posesDeTodosOsQuadros(const std::map<int, Pose>& chaves) {
  posesPorQuadro.clear();
  std::vector<V3> X;
  std::vector<std::pair<double, double>> px;
  Pose ultima;
  bool temUltima = false;
  std::vector<int> semPose;
  for (int q = 0; q < quadros; q++) {
    X.clear();
    px.clear();
    for (const auto& [id, t] : trilhas) {
      if (!t.temX) continue;
      const Obs* o = t.em(q);
      if (o) {
        X.push_back(t.X);
        px.push_back({o->x, o->y});
      }
    }
    Pose p;
    bool ok = false;
    const auto ja = chaves.find(q);
    if (ja != chaves.end()) {
      p = ja->second;
      ok = true;
    } else if ((int)X.size() >= 6) {
      if (temUltima) {
        // Parte da pose vizinha: refino local e mais estavel que PnP puro
        // e nunca "pula" para um minimo errado do outro lado da cena.
        p = ultima;
        const double rms = refinarPose(cam, X, px, &p);
        ok = rms < 12;
      }
      if (!ok) ok = pnp(cam, X, px, (uint64_t)q, &p, nullptr);
    }
    if (ok) {
      posesPorQuadro.push_back({q, p});
      ultima = p;
      temUltima = true;
    } else {
      semPose.push_back(q);
      posesPorQuadro.push_back({q, temUltima ? ultima : Pose{}});
    }
    if ((q & 15) == 0) dizer(0.8 + 0.15 * (q + 1.0) / quadros, q + 1, quadros);
  }
  // Buracos: interpola entre os vizinhos resolvidos (giro pelo vetor de
  // rotacao relativo, posicao linear) — melhor que congelar a camera.
  for (const int q : semPose) {
    int antes = -1, depois = -1;
    for (int i = q - 1; i >= 0; i--)
      if (std::find(semPose.begin(), semPose.end(), i) == semPose.end()) {
        antes = i;
        break;
      }
    for (int i = q + 1; i < quadros; i++)
      if (std::find(semPose.begin(), semPose.end(), i) == semPose.end()) {
        depois = i;
        break;
      }
    if (antes < 0 && depois < 0) continue;
    if (antes < 0) {
      posesPorQuadro[q].second = posesPorQuadro[depois].second;
      continue;
    }
    if (depois < 0) {
      posesPorQuadro[q].second = posesPorQuadro[antes].second;
      continue;
    }
    const Pose& a = posesPorQuadro[antes].second;
    const Pose& b = posesPorQuadro[depois].second;
    const double u = (double)(q - antes) / (depois - antes);
    const M3 rel = b.R * a.R.transposta();
    const V3 w = vetorDeRotacao(rel);
    Pose p;
    p.R = rodrigues(u * w) * a.R;
    // Interpola o CENTRO (nao o t): e o centro que anda em linha.
    const V3 ca = -1.0 * a.R.transposta().aplicar(a.t);
    const V3 cb = -1.0 * b.R.transposta().aplicar(b.t);
    const V3 c = ca + u * (cb - ca);
    p.t = -1.0 * p.R.aplicar(c);
    posesPorQuadro[q].second = p;
  }
}

double at2_cena::erroGlobal() {
  double soma = 0;
  int n = 0;
  for (const auto& [id, t] : trilhas) {
    if (!t.temX) continue;
    for (const auto& o : t.obs) {
      const auto& p = posesPorQuadro[o.quadro].second;
      double u, v;
      if (!cam.projetar(p.R, p.t, t.X, &u, &v)) continue;
      const double dx = u - o.x, dy = v - o.y;
      soma += dx * dx + dy * dy;
      n++;
    }
  }
  return n == 0 ? 1e9 : std::sqrt(soma / n);
}

void at2_cena::fichaDosPontos() {
  saida.clear();
  for (const auto& [id, t] : trilhas) {
    if (!t.temX) continue;
    double soma = 0;
    int n = 0;
    for (const auto& o : t.obs) {
      const auto& p = posesPorQuadro[o.quadro].second;
      double u, v;
      if (!cam.projetar(p.R, p.t, t.X, &u, &v)) continue;
      const double dx = u - o.x, dy = v - o.y;
      soma += dx * dx + dy * dy;
      n++;
    }
    if (n < 2) continue;
    const double erroDoPonto = std::sqrt(soma / n);
    if (erroDoPonto > 30) continue;  // lixo nao entra na nuvem final
    saida.push_back({id, t.X, erroDoPonto, (int)t.obs.size()});
  }
}

int at2_cena::resolverTripe(const std::vector<int>& marcos) {
  // ROTACAO POR PAR DE MARCOS via homografia: com t = 0 vale
  // H = K R K^-1, logo R = K^-1 H K (limpa para a rotacao mais proxima).
  // A focal certa e a que faz esse R explicar os pares — a varredura
  // escolhe pelo erro de transferencia.
  std::vector<M3> hs;
  std::vector<int> ids;
  std::vector<double> ax, ay, bx, by;
  for (size_t i = 0; i + 1 < marcos.size(); i++) {
    if (paresDe(marcos[i], marcos[i + 1], &ids, &ax, &ay, &bx, &by) < 8)
      return AT2_ERR_POUCOS_PONTOS;
    M3 h;
    if (!homografiaRansac(ax, ay, bx, by, 1.5, (uint64_t)i, &h, nullptr))
      return AT2_ERR_NAO_CONVERGIU;
    hs.push_back(h);
  }

  auto rotacaoDe = [&](const M3& h, double f) {
    M3 kInv, k;
    k.m[0] = f; k.m[1] = 0; k.m[2] = cam.cx;
    k.m[3] = 0; k.m[4] = f; k.m[5] = cam.cy;
    k.m[6] = 0; k.m[7] = 0; k.m[8] = 1;
    kInv.m[0] = 1 / f; kInv.m[1] = 0; kInv.m[2] = -cam.cx / f;
    kInv.m[3] = 0; kInv.m[4] = 1 / f; kInv.m[5] = -cam.cy / f;
    kInv.m[6] = 0; kInv.m[7] = 0; kInv.m[8] = 1;
    return rotacaoMaisProxima(kInv * h * k);
  };

  auto erroComF = [&](double f) {
    double soma = 0;
    int n = 0;
    for (size_t i = 0; i + 1 < marcos.size(); i++) {
      if (paresDe(marcos[i], marcos[i + 1], &ids, &ax, &ay, &bx, &by) < 8)
        continue;
      const M3 r = rotacaoDe(hs[i], f);
      for (size_t j = 0; j < ax.size(); j++) {
        const V3 raio{(ax[j] - cam.cx) / f, (ay[j] - cam.cy) / f, 1};
        const V3 gira = r.aplicar(raio);
        if (gira.z < 1e-9) continue;
        const double px = cam.cx + f * gira.x / gira.z;
        const double py = cam.cy + f * gira.y / gira.z;
        const double dx = px - bx[j], dy = py - by[j];
        soma += std::sqrt(dx * dx + dy * dy);
        n++;
      }
    }
    return n == 0 ? 1e18 : soma / n;
  };

  if (cam.f <= 0) {
    double melhorF = w * 1.2, melhorErro = 1e18;
    for (double m = 0.6; m <= 3.01; m += 0.15) {
      const double e = erroComF(w * m);
      if (e < melhorErro) {
        melhorErro = e;
        melhorF = w * m;
      }
    }
    // Refina em volta do melhor.
    for (double m = -0.12; m <= 0.121; m += 0.03) {
      const double f = melhorF + w * m;
      if (f < w * 0.4) continue;
      const double e = erroComF(f);
      if (e < melhorErro) {
        melhorErro = e;
        melhorF = f;
      }
    }
    cam.f = melhorF;
  }

  // Encadeia as rotacoes dos marcos e resolve cada quadro contra o marco
  // mais proximo (homografia direta, mais curta = mais firme).
  std::map<int, Pose> chaves;
  M3 acumulada;  // identidade no primeiro marco
  chaves[marcos[0]] = {acumulada, {0, 0, 0}, true};
  for (size_t i = 0; i + 1 < marcos.size(); i++) {
    acumulada = rotacaoDe(hs[i], cam.f) * acumulada;
    chaves[marcos[i + 1]] = {acumulada, {0, 0, 0}, true};
  }

  posesPorQuadro.clear();
  for (int q = 0; q < quadros; q++) {
    // Marco mais proximo.
    int marco = marcos[0];
    for (const int m : marcos)
      if (std::abs(m - q) < std::abs(marco - q)) marco = m;
    Pose p = chaves[marco];
    if (q != marco) {
      if (paresDe(marco, q, &ids, &ax, &ay, &bx, &by) >= 8) {
        M3 h;
        if (homografiaRansac(ax, ay, bx, by, 1.5, (uint64_t)(q + 9000), &h,
                             nullptr)) {
          p.R = rotacaoMaisProxima(rotacaoDe(h, cam.f) * chaves[marco].R);
        }
      }
    }
    posesPorQuadro.push_back({q, p});
    if ((q & 15) == 0) dizer(0.5 + 0.4 * (q + 1.0) / quadros, q + 1, quadros);
  }

  // A "nuvem" do tripe: raios do primeiro quadro em profundidade fixa —
  // nao ha paralaxe para medir profundidade, e fingir que ha seria mentira.
  // Serve para ancorar overlays que giram junto.
  const double raio = 600;
  for (auto& [id, t] : trilhas) {
    const Obs* o = t.obs.empty() ? nullptr : &t.obs.front();
    if (!o) continue;
    const auto& p = posesPorQuadro[o->quadro].second;
    const V3 dir = unitario(
        {(o->x - cam.cx) / cam.f, (o->y - cam.cy) / cam.f, 1});
    t.X = p.R.transposta().aplicar(raio * dir - p.t);
    t.temX = true;
  }
  fichaDosPontos();
  erro = erroGlobal();
  tripe = true;
  return erro < 8 ? AT2_OK : AT2_ERR_NAO_CONVERGIU;
}

int at2_cena::resolver(double focalPx, int modo) {
  cam.cx = w / 2.0;
  cam.cy = h / 2.0;
  cam.f = focalPx > 0 ? focalPx : 0;
  cam.k1 = 0;

  if (quadros < 16) return AT2_ERR_POUCOS_QUADROS;

  // Trilhas curtas nao dizem nada sobre a camera.
  int uteis = 0;
  for (const auto& [id, t] : trilhas)
    if ((int)t.obs.size() >= 5) uteis++;
  if (uteis < 30) return AT2_ERR_POUCOS_PONTOS;

  dizer(0.02, 0, 1);

  // MARCOS (quadros-chave): ~24 espalhados pelo trecho.
  std::vector<int> marcos;
  const int passo = std::max(1, quadros / 24);
  for (int q = 0; q < quadros; q += passo) marcos.push_back(q);
  if (marcos.back() != quadros - 1) marcos.push_back(quadros - 1);

  if (modo == AT2_MODO_TRIPE) return resolverTripe(marcos);

  // PAR INICIAL: entre pares de marcos afastados, o que tem MAIS pares,
  // MAIS paralaxe (o residuo da homografia: o quanto do movimento uma
  // transformacao plana NAO explica) e MAIS linha de base — o
  // deslocamento entra no desempate porque numa cena PLANA o residuo de
  // H e ~ zero para todo par, e ainda assim o par largo e o melhor.
  std::vector<int> ids;
  std::vector<double> ax, ay, bx, by;
  QuadroDoPar melhorPar;
  double melhorNota = 0;
  for (size_t i = 0; i < marcos.size(); i++)
    for (size_t j = i + 2; j < marcos.size(); j++) {
      const int n = paresDe(marcos[i], marcos[j], &ids, &ax, &ay, &bx, &by);
      if (n < 30) continue;
      M3 h;
      std::vector<int> dentroH;
      if (!homografiaRansac(ax, ay, bx, by, 2.0, i * 100 + j, &h, &dentroH))
        continue;
      double residuo = 0, desloc = 0;
      for (int k = 0; k < n; k++) {
        residuo += std::min(30.0, erroDeH(h, ax[k], ay[k], bx[k], by[k]));
        const double dx = bx[k] - ax[k], dy = by[k] - ay[k];
        desloc += std::sqrt(dx * dx + dy * dy);
      }
      residuo /= n;
      desloc /= n;
      const double nota = std::sqrt((double)n) * (residuo + 0.02 * desloc);
      if (nota > melhorNota) {
        melhorNota = nota;
        melhorPar = {marcos[i], marcos[j], n, residuo};
      }
    }
  if (melhorPar.comuns < 30) return AT2_ERR_POUCOS_PONTOS;

  dizer(0.08, 0, 1);

  // FOCAL POR QUATRO VISTAS: para cada candidata, resolve o par inicial e
  // RESSECA dois marcos de apoio no meio do caminho; a focal errada ate
  // fecha o par, mas nao fecha as vistas de apoio. Dentro do mesmo passo
  // o par escolhe o MODELO: homografia (cena plana ou giro puro,
  // decomposta por Faugeras) ou essencial (cena com volume).
  auto marcoPerto = [&](double alvo) {
    int m = marcos[0];
    for (const int mm : marcos)
      if (std::fabs(mm - alvo) < std::fabs(m - alvo)) m = mm;
    return m;
  };
  // Apoios pelo CLIPE INTEIRO, nao so entre o par: a focal e o que faz o
  // COMECO e o FIM do caminho fecharem com o meio — encurtar o alcance
  // achata o minimo (foi o que deixou a orbita escolher focal errada).
  std::vector<int> candidatosDeApoio;
  for (const int m : marcos)
    if (m != melhorPar.a && m != melhorPar.b) candidatosDeApoio.push_back(m);
  std::vector<int> apoiosEscolhidos;
  if ((int)candidatosDeApoio.size() <= 8) {
    apoiosEscolhidos = candidatosDeApoio;
  } else {
    for (int k = 0; k < 8; k++)
      apoiosEscolhidos.push_back(
          candidatosDeApoio[(size_t)(k * (candidatosDeApoio.size() - 1) / 7)]);
  }
  if (apoiosEscolhidos.empty())
    apoiosEscolhidos.push_back(
        marcoPerto(melhorPar.a + (melhorPar.b - melhorPar.a) * 0.5));

  paresDe(melhorPar.a, melhorPar.b, &ids, &ax, &ay, &bx, &by);

  // O TESTE DO GIRO VEM ANTES DE TUDO — e e um teste de Occam. Rotacao
  // pura numa focal errada fabrica uma cena 3D FALSA e consistente (a
  // paralaxe aparece do nada e a reprojecao fecha com decimos de pixel):
  // e assim que um tracker alucina. O que a alucinacao nao consegue e o
  // contrario: se existe UMA focal em que a rotacao pura sozinha explica
  // o melhor par com erro de ruido, entao a paralaxe nao esta na
  // filmagem, e a resposta honesta e TRIPE.
  {
    M3 hDoPar;
    double melhorGiro = 1e18;
    const bool temHdoPar =
        homografiaRansac(ax, ay, bx, by, 2.0, 4242, &hDoPar, nullptr);
    AT2_LOG("teste do giro: n=%d temH=%d", (int)ax.size(), temHdoPar ? 1 : 0);
    double fDoGiro = 0;
    auto medirGiro = [&](double f) {
        M3 kInv, k;
        k.m[0] = f; k.m[1] = 0; k.m[2] = cam.cx;
        k.m[3] = 0; k.m[4] = f; k.m[5] = cam.cy;
        k.m[6] = 0; k.m[7] = 0; k.m[8] = 1;
        kInv.m[0] = 1 / f; kInv.m[1] = 0; kInv.m[2] = -cam.cx / f;
        kInv.m[3] = 0; kInv.m[4] = 1 / f; kInv.m[5] = -cam.cy / f;
        kInv.m[6] = 0; kInv.m[7] = 0; kInv.m[8] = 1;
        const M3 r = rotacaoMaisProxima(kInv * hDoPar * k);
        double soma = 0;
        int quantos = 0;
        for (size_t j = 0; j < ax.size(); j++) {
          const V3 raio{(ax[j] - cam.cx) / f, (ay[j] - cam.cy) / f, 1};
          const V3 gira = r.aplicar(raio);
          if (gira.z < 1e-9) continue;
          const double px = cam.cx + f * gira.x / gira.z;
          const double py = cam.cy + f * gira.y / gira.z;
          const double dx = px - bx[j], dy = py - by[j];
          soma += std::sqrt(dx * dx + dy * dy);
          quantos++;
        }
        if (quantos * 2 >= (int)ax.size() && soma / quantos < melhorGiro) {
          melhorGiro = soma / quantos;
          fDoGiro = f;
        }
    };
    if (temHdoPar) {
      // Grosso, depois fino: a conjugacao K^-1 H K e sensivel a focal
      // (1% de erro em f vira pixels de transferencia num giro largo).
      for (double m = 0.5; m <= 3.01; m += 0.1) medirGiro(w * m);
      if (fDoGiro > 0) {
        const double base = fDoGiro;
        for (double dm = -0.09; dm <= 0.091; dm += 0.015)
          medirGiro(base + w * dm);
        const double base2 = fDoGiro;
        for (double dm = -0.012; dm <= 0.0121; dm += 0.003)
          medirGiro(base2 + w * dm);
      }
    }
    AT2_LOG("teste do giro: melhor explicacao por rotacao pura = %.3f px",
            melhorGiro);
    if (melhorGiro < 1.5) {
      cam.f = focalPx > 0 ? focalPx : 0;
      const int r = resolverTripe(marcos);
      return r == AT2_OK ? AT2_OK : AT2_ERR_SEM_PARALAXE;
    }
  }

  // Devolve 0 = nao fechou, 1 = fechou (erro em erroFora), 2 = giro puro.
  double k1Varredura = 0;
  auto tentarFocal = [&](double f, double* erroFora,
                         std::map<int, Pose>* chavesFora) -> int {
    Camera c;
    c.cx = cam.cx;
    c.cy = cam.cy;
    c.f = f;
    c.k1 = k1Varredura;
    const int n = (int)ids.size();
    std::vector<double> nax(n), nay(n), nbx(n), nby(n);
    for (int k = 0; k < n; k++) {
      c.normalizar(ax[k], ay[k], &nax[k], &nay[k]);
      c.normalizar(bx[k], by[k], &nbx[k], &nby[k]);
    }

    // Os dois modelos disputam o par.
    M3 h;
    std::vector<int> inlH;
    const bool temH =
        homografiaRansac(nax, nay, nbx, nby, 2.0 / f, 991, &h, &inlH);
    DuasVistas dv;
    const bool temE = essencialRansac(nax, nay, nbx, nby, 2.0 / f, 77, &dv);
    AT2_LOG("f=%.0f temH=%d inlH=%d temE=%d inlE=%d", f, temH ? 1 : 0,
            (int)inlH.size(), temE ? 1 : 0, (int)dv.inliers.size());
    if (!temH && !temE) return 0;
    const bool plana =
        temH && (!temE || (int)inlH.size() >= (int)(0.90 * dv.inliers.size()));

    Pose A;
    A.ok = true;
    Pose B;
    B.ok = true;
    std::vector<int> doModelo;

    if (plana) {
      // O sinal de H importa para a decomposicao: (H xa) do lado de xb.
      int pos = 0, neg = 0;
      for (const int i : inlH) {
        const V3 p = h.aplicar({nax[i], nay[i], 1});
        (p.z > 0 ? pos : neg)++;
      }
      M3 hs = h;
      if (neg > pos)
        for (int k = 0; k < 9; k++) hs.m[k] = -hs.m[k];
      std::vector<SolucaoH> cands;
      if (decomporHomografia(hs, &cands) == 1) {
        AT2_LOG("f=%.0f plana: giro puro pela decomposicao", f);
        return 2;
      }
      int melhorFrente = -1;
      V3 melhorT{0, 0, 0};
      for (const auto& sh : cands) {
        Pose Bq;
        Bq.R = sh.r;
        Bq.t = sh.t;
        Bq.ok = true;
        int naFrente = 0;
        for (size_t k = 0; k < inlH.size(); k += 3) {
          const int i = inlH[k];
          V3 X;
          if (!triangular({&A, &Bq}, {{nax[i], nay[i]}, {nbx[i], nby[i]}}, &X))
            continue;
          if (profundidade(A, X) > 0 && profundidade(Bq, X) > 0) naFrente++;
        }
        if (naFrente > melhorFrente) {
          melhorFrente = naFrente;
          B = Bq;
          melhorT = sh.t;
        }
      }
      if (melhorFrente < 8) return 0;
      // Translacao desprezivel diante da distancia do plano = tripe.
      if (norma(melhorT) < 0.05) return 2;
      B.t = unitario(B.t);  // a escala do mundo e livre; |t| = 1 e a regra
      doModelo = inlH;
    } else {
      M3 rs[2];
      V3 tdir;
      posesDaEssencial(dv.e, rs, &tdir);
      int melhorFrente = -1, melhorTrianguladas = 0;
      for (int ri = 0; ri < 2; ri++)
        for (int si = 0; si < 2; si++) {
          Pose Bq;
          Bq.R = rs[ri];
          Bq.t = (si == 0 ? 1.0 : -1.0) * tdir;
          Bq.ok = true;
          int naFrente = 0, trianguladas = 0;
          for (const int i : dv.inliers) {
            V3 X;
            if (!triangular({&A, &Bq},
                            {{nax[i], nay[i]}, {nbx[i], nby[i]}}, &X))
              continue;
            trianguladas++;
            if (profundidade(A, X) > 0 && profundidade(Bq, X) > 0) naFrente++;
          }
          if (naFrente > melhorFrente) {
            melhorFrente = naFrente;
            melhorTrianguladas = trianguladas;
            B = Bq;
          }
        }
      // MAIORIA entre o que TRIANGULA, nao pluralidade: num f degenerado
      // as quatro combinacoes empatam baixo, e a "vencedora" e lixo.
      if (melhorFrente < 8 ||
          melhorFrente * 10 < melhorTrianguladas * 6)
        return 0;
      doModelo = dv.inliers;
    }

    // A GUARDA DO GIRO: a paralaxe angular DOS RAIOS, compensada a
    // rotacao. Rotacao pura satisfaz a essencial com QUALQUER translacao
    // (a conta fecha e o erro sai pequeno), e a triangulacao de raios
    // paralelos inventa pontos-lixo perto das cameras — entao a unica
    // medida honesta e a que nao depende de nada disso: girar o raio da
    // segunda vista de volta e medir o angulo que sobra. Sem angulo, nao
    // ha profundidade, e a resposta certa e TRIPE.
    {
      std::vector<double> angulos;
      for (const int i : doModelo) {
        const V3 a = unitario({nax[i], nay[i], 1});
        const V3 b = unitario({nbx[i], nby[i], 1});
        const V3 volta = B.R.transposta().aplicar(b);
        const double co = std::max(-1.0, std::min(1.0, dot(a, volta)));
        angulos.push_back(std::acos(co));
      }
      std::sort(angulos.begin(), angulos.end());
      const double mediano = angulos.empty() ? 0 : angulos[angulos.size() / 2];
      AT2_LOG("f=%.0f paralaxe angular mediana=%.5f rad", f, mediano);
      if (angulos.empty()) return 0;
      if (mediano < 0.004) {
        // CONTRAPROVA: giro de verdade explica os pixels do par com a
        // propria rotacao. Um ramo degenerado da essencial tambem zera o
        // angulo — mas nao explica pixel nenhum, e a resposta certa para
        // ele e "falhou", nunca "tripe".
        double soma = 0;
        int quantos = 0;
        for (const int i : doModelo) {
          const V3 raio{nax[i], nay[i], 1};
          const V3 gira = B.R.aplicar(raio);
          if (gira.z < 1e-9) continue;
          const double dx = f * (gira.x / gira.z - nbx[i]);
          const double dy = f * (gira.y / gira.z - nby[i]);
          soma += std::sqrt(dx * dx + dy * dy);
          quantos++;
        }
        const double transfer = quantos == 0 ? 1e18 : soma / quantos;
        AT2_LOG("f=%.0f giro? transferencia=%.3f px", f, transfer);
        return transfer < 2.0 ? 2 : 0;
      }
    }

    // Triangula os inliers do modelo vencedor...
    std::map<int, V3> nuvemLocal;
    for (const int i : doModelo) {
      V3 X;
      if (!triangular({&A, &B}, {{nax[i], nay[i]}, {nbx[i], nby[i]}}, &X))
        continue;
      if (profundidade(A, X) <= 0 || profundidade(B, X) <= 0) continue;
      nuvemLocal[ids[i]] = X;
    }
    if ((int)nuvemLocal.size() < 20) return 0;

    // ...e resseca os marcos de apoio.
    std::vector<std::pair<int, Pose>> apoios;
    for (const int marco : apoiosEscolhidos) {
      if (marco == melhorPar.a || marco == melhorPar.b) continue;
      bool ja = false;
      for (const auto& [m, p] : apoios) ja = ja || m == marco;
      if (ja) continue;
      std::vector<V3> Xm;
      std::vector<std::pair<double, double>> pm;
      for (const auto& [id, X] : nuvemLocal) {
        const Obs* o = trilhas[id].em(marco);
        if (!o) continue;
        Xm.push_back(X);
        pm.push_back({o->x, o->y});
      }
      if ((int)Xm.size() < 12) continue;
      Pose pose;
      if (pnp(c, Xm, pm, 31 + (uint64_t)marco, &pose, nullptr, 6.0))
        apoios.push_back({marco, pose});
    }
    if ((int)apoios.size() < 2) return 0;

    // REFINO ALTERNADO antes de dar a nota: cada vista acerta a pose
    // contra a nuvem, a nuvem re-triangula com todas as vistas, duas
    // vezes. Sem isso o minimo em f e raso — toda focal parece igual —
    // e a varredura escolhe qualquer coisa.
    std::vector<std::pair<int, Pose*>> vistasDoPar;
    vistasDoPar.push_back({melhorPar.a, &A});
    vistasDoPar.push_back({melhorPar.b, &B});
    for (auto& [m, p] : apoios) vistasDoPar.push_back({m, &p});
    for (int rodada = 0; rodada < 2; rodada++) {
      for (auto& [quadro, pose] : vistasDoPar) {
        if (quadro == melhorPar.a) continue;  // a primeira e o calibre
        std::vector<V3> Xs;
        std::vector<std::pair<double, double>> ps;
        for (const auto& [id, X] : nuvemLocal) {
          const Obs* o = trilhas[id].em(quadro);
          if (!o) continue;
          Xs.push_back(X);
          ps.push_back({o->x, o->y});
        }
        if ((int)Xs.size() >= 6) refinarPose(c, Xs, ps, pose);
      }
      for (auto& [id, X] : nuvemLocal) {
        std::vector<const Pose*> vs;
        std::vector<std::pair<double, double>> xy;
        for (const auto& [quadro, pose] : vistasDoPar) {
          const Obs* o = trilhas[id].em(quadro);
          if (!o) continue;
          double ux, uy;
          c.normalizar(o->x, o->y, &ux, &uy);
          vs.push_back(pose);
          xy.push_back({ux, uy});
        }
        V3 novo;
        if ((int)vs.size() >= 2 && triangular(vs, xy, &novo)) {
          bool frente = true;
          for (const auto* v : vs) frente = frente && profundidade(*v, novo) > 0;
          if (frente) X = novo;
        }
      }
    }

    // O erro que decide: reprojecao no par e nos apoios.
    double soma = 0;
    int quantos = 0;
    auto acumular = [&](const Pose& P, int quadro) {
      for (const auto& [id, X] : nuvemLocal) {
        const Obs* o = trilhas[id].em(quadro);
        if (!o) continue;
        double u, v;
        if (!c.projetar(P.R, P.t, X, &u, &v)) continue;
        const double dx = u - o->x, dy = v - o->y;
        soma += std::min(100.0, dx * dx + dy * dy);
        quantos++;
      }
    };
    acumular(A, melhorPar.a);
    acumular(B, melhorPar.b);
    for (const auto& [m, p] : apoios) acumular(p, m);
    if (quantos < 30) return 0;
    *erroFora = std::sqrt(soma / quantos);
    AT2_LOG("f=%.0f fechou: erro=%.3f px em %d obs (%d apoios)", f, *erroFora,
            quantos, (int)apoios.size());
    if (chavesFora) {
      (*chavesFora)[melhorPar.a] = A;
      (*chavesFora)[melhorPar.b] = B;
      for (const auto& [m, p] : apoios) (*chavesFora)[m] = p;
      for (auto& [id, t] : trilhas) t.temX = false;
      for (const auto& [id, X] : nuvemLocal) {
        trilhas[id].X = X;
        trilhas[id].temX = true;
      }
    }
    return 1;
  };

  // A VARREDURA E CONJUNTA em (k1, f): o par sozinho nao separa os dois
  // (r^3 e r andam quase juntos neste campo de visao), mas a regua de
  // muitas vistas separa — para cada k1 candidato, o f e varrido
  // inteiro, e o MINIMO GLOBAL leva os dois.
  double melhorF = 0, melhorErroF = 1e18;
  double k1Escolhido = 0;
  int girosVistos = 0;
  auto provar = [&](double f) {
    double e;
    const int r = tentarFocal(f, &e, nullptr);
    if (r == 2) girosVistos++;
    if (r == 1 && e < melhorErroF) {
      melhorErroF = e;
      melhorF = f;
      k1Escolhido = k1Varredura;
    }
  };
  // A NAVALHA DO K1: modelar distorcao so quando ela paga a passagem.
  // Um k1 falso compra ~10% de erro por conformacao (troca com f e
  // profundidade); um k1 de verdade derruba o erro de 25% para cima.
  // Entao o modelo simples (k1 = 0) so perde se o composto vencer COM
  // FOLGA — e o mesmo principio do teste do giro.
  const double k1Candidatos[] = {0.0, -0.05, -0.10, -0.15};
  double erroComK1Zero = 1e18, fComK1Zero = 0;
  if (cam.f > 0) {
    melhorF = cam.f;
    double soDoK = 1e18;
    for (const double k : k1Candidatos) {
      k1Varredura = k;
      double e;
      if (tentarFocal(cam.f, &e, nullptr) == 1 && e < soDoK) {
        if (k != 0.0 && e > 0.8 * soDoK) continue;
        soDoK = e;
        k1Escolhido = k;
      }
    }
  } else {
    for (const double k : k1Candidatos) {
      k1Varredura = k;
      for (double m = 0.6; m <= 3.01; m += 0.2) provar(w * m);
      if (k == 0.0) {
        erroComK1Zero = melhorErroF;
        fComK1Zero = melhorF;
      }
      AT2_LOG("varrido k1=%.2f: melhor global f=%.0f erro=%.4f (k1=%.2f)", k,
              melhorF, melhorErroF, k1Escolhido);
    }
    if (k1Escolhido != 0.0 && fComK1Zero > 0 &&
        melhorErroF > 0.8 * erroComK1Zero) {
      k1Escolhido = 0;
      melhorF = fComK1Zero;
      melhorErroF = erroComK1Zero;
      AT2_LOG("navalha: k1 nao paga a passagem; fica k1=0 f=%.0f", melhorF);
    }
    if (melhorF > 0) {
      // Passo fino em volta da vencedora, ja com o k1 dela.
      k1Varredura = k1Escolhido;
      const double base = melhorF;
      for (double dm = -0.15; dm <= 0.151; dm += 0.05) {
        if (std::fabs(dm) < 1e-9) continue;
        const double f = base + w * dm;
        if (f > w * 0.4) provar(f);
      }
    }
    if (melhorF <= 0) {
      if (girosVistos >= 3) {
        cam.f = focalPx > 0 ? focalPx : 0;  // o tripe varre a propria focal
        const int r = resolverTripe(marcos);
        return r == AT2_OK ? AT2_OK : AT2_ERR_SEM_PARALAXE;
      }
      return AT2_ERR_SEM_PARALAXE;
    }
  }
  k1Varredura = k1Escolhido;
  AT2_LOG("varredura: f=%.0f k1=%.2f giros=%d", melhorF, k1Escolhido,
          girosVistos);

  dizer(0.25, 0, 1);

  std::map<int, Pose> chaves;

  auto triangularNovas = [&]() {
    for (auto& [id, t] : trilhas) {
      if (t.temX || (int)t.obs.size() < 4) continue;
      std::vector<const Pose*> vistas;
      std::vector<std::pair<double, double>> xy;
      std::vector<const Obs*> fontes;
      for (const auto& o : t.obs) {
        const auto it = chaves.find(o.quadro);
        if (it == chaves.end()) continue;
        double nx, ny;
        cam.normalizar(o.x, o.y, &nx, &ny);
        vistas.push_back(&it->second);
        xy.push_back({nx, ny});
        fontes.push_back(&o);
      }
      if ((int)vistas.size() < 2) continue;
      V3 X;
      if (!triangular(vistas, xy, &X)) continue;
      bool ok = true;
      double pior = 0;
      for (size_t i = 0; i < vistas.size(); i++) {
        if (profundidade(*vistas[i], X) <= 0) {
          ok = false;
          break;
        }
        double u, v;
        if (!cam.projetar(vistas[i]->R, vistas[i]->t, X, &u, &v)) {
          ok = false;
          break;
        }
        const double ddx = u - fontes[i]->x, ddy = v - fontes[i]->y;
        pior = std::max(pior, std::sqrt(ddx * ddx + ddy * ddy));
      }
      if (ok && pior < 8) {
        t.X = X;
        t.temX = true;
      }
    }
  };

  // MONTA A CENA INTEIRA para uma focal: par inicial, registro
  // incremental dos marcos, triangulacao e ajuste. E a unidade da
  // LARGADA TRIPLA logo abaixo.
  auto montarCena = [&](double f) -> bool {
    chaves.clear();
    for (auto& [id, t] : trilhas) t.temX = false;
    cam.f = f;
    cam.k1 = k1Escolhido;
    double erroInicial;
    if (tentarFocal(f, &erroInicial, &chaves) != 1) return false;
    triangularNovas();

    std::vector<int> fila;
    for (const int m : marcos)
      if (chaves.find(m) == chaves.end()) fila.push_back(m);

    int registrados = (int)chaves.size();
    int passoReg = 0;
    while (!fila.empty()) {
      // Sempre o marco mais perto de alguem ja registrado.
      std::sort(fila.begin(), fila.end(), [&](int a, int b) {
        auto dist = [&](int q) {
          int d = 1 << 30;
          for (const auto& [k, p] : chaves) d = std::min(d, std::abs(k - q));
          return d;
        };
        return dist(a) > dist(b);
      });
      const int q = fila.back();
      fila.pop_back();

      std::vector<V3> X;
      std::vector<std::pair<double, double>> px;
      for (const auto& [id, t] : trilhas) {
        if (!t.temX) continue;
        const Obs* o = t.em(q);
        if (o) {
          X.push_back(t.X);
          px.push_back({o->x, o->y});
        }
      }
      Pose p;
      if ((int)X.size() >= 10 &&
          pnp(cam, X, px, (uint64_t)q + 500, &p, nullptr)) {
        chaves[q] = p;
        registrados++;
        triangularNovas();
        // Ajuste local de vez em quando segura o desvio acumulado.
        if ((registrados % 5) == 0) ajusteDeFeixes(chaves, 0, 4);
      }
      passoReg++;
      dizer(0.25 + 0.3 * (double)passoReg / std::max(1, (int)marcos.size()),
            registrados, (int)marcos.size());
    }
    if (registrados < (int)marcos.size() * 3 / 5) return false;

    ajusteDeFeixes(chaves, 0, 8);
    if (focalPx <= 0) ajusteDeFeixes(chaves, 1, 12);
    ajusteDeFeixes(chaves, 2, 10);
    return true;
  };

  // AS LARGADAS SAO RESERVAS, nao um plebiscito: quem decidiu f e k1 foi
  // a regua meio-convergida da varredura — o custo da cena conformada e
  // PLANO no vale do bas-relief e re-decidir por ele desfazia o acerto.
  // Se a primeira largada nao monta, as vizinhas tentam.
  bool alguma = false;
  const std::vector<double> largadas = focalPx > 0
      ? std::vector<double>{focalPx}
      : std::vector<double>{melhorF, 0.85 * melhorF, 1.2 * melhorF};
  for (const double f : largadas) {
    if (f < w * 0.4 || f > w * 4.0) continue;
    if (montarCena(f)) {
      alguma = true;
      AT2_LOG("largada montada: f=%.1f k1=%.3f", cam.f, cam.k1);
      break;
    }
  }
  if (!alguma) return AT2_ERR_NAO_CONVERGIU;

  ajusteDeFeixes(chaves, 2, 20);
  AT2_LOG("apos a corrida: f=%.1f k1=%.4f", cam.f, cam.k1);

  // Poda: ponto com residuo alto em qualquer chave sai da estrutura.
  for (auto& [id, t] : trilhas) {
    if (!t.temX) continue;
    double soma = 0;
    int n = 0;
    for (const auto& o : t.obs) {
      const auto it = chaves.find(o.quadro);
      if (it == chaves.end()) continue;
      double u, v;
      if (!cam.projetar(it->second.R, it->second.t, t.X, &u, &v)) continue;
      const double dx = u - o.x, dy = v - o.y;
      soma += dx * dx + dy * dy;
      n++;
    }
    if (n < 2 || std::sqrt(soma / std::max(1, n)) > 6) t.temX = false;
  }
  int sobraram = 0;
  for (const auto& [id, t] : trilhas)
    if (t.temX) sobraram++;
  if (sobraram < 20) return AT2_ERR_NAO_CONVERGIU;
  ajusteDeFeixes(chaves, 2, 20);

  // POSE DE TODO QUADRO contra a nuvem pronta.
  posesDeTodosOsQuadros(chaves);
  fichaDosPontos();
  erro = erroGlobal();
  tripe = false;
  dizer(1.0, quadros, quadros);
  if (erro > 8) return AT2_ERR_NAO_CONVERGIU;
  return AT2_OK;
}

// ------------------------------------------------------------------ API

extern "C" {

AT2_API at2_cena* at2_cena_criar(int32_t largura, int32_t altura,
                                 int32_t quadros, int32_t fps) {
  if (largura < 32 || altura < 32 || quadros < 2) return nullptr;
  auto* c = new at2_cena();
  c->w = largura;
  c->h = altura;
  c->quadros = quadros;
  c->fps = fps;
  return c;
}

AT2_API void at2_cena_destruir(at2_cena* c) { delete c; }

AT2_API void at2_cena_observar(at2_cena* c, const double* obs,
                               int32_t quantas) {
  if (!c || !obs) return;
  for (int32_t i = 0; i < quantas; i++) {
    const int id = (int)obs[i * 4 + 0];
    const int quadro = (int)obs[i * 4 + 1];
    if (quadro < 0 || quadro >= c->quadros) continue;
    Trilha& t = c->trilhas[id];
    t.id = id;
    t.obs.push_back({quadro, obs[i * 4 + 2], obs[i * 4 + 3]});
  }
}

AT2_API int32_t at2_cena_resolver(at2_cena* c, double focal_px, int32_t modo,
                                  at2_progresso progresso, void* alvo) {
  if (!c) return AT2_ERR_ARGS;
  c->progresso = progresso;
  c->alvo = alvo;
  c->codigo = c->resolver(focal_px, modo);
  return c->codigo;
}

AT2_API double at2_cena_focal(at2_cena* c) { return c ? c->cam.f : 0; }
AT2_API double at2_cena_distorcao(at2_cena* c) { return c ? c->cam.k1 : 0; }
AT2_API double at2_cena_erro(at2_cena* c) { return c ? c->erro : 0; }
AT2_API int32_t at2_cena_tripe(at2_cena* c) { return c && c->tripe ? 1 : 0; }

AT2_API int32_t at2_cena_quantas_poses(at2_cena* c) {
  return c ? (int32_t)c->posesPorQuadro.size() : 0;
}

AT2_API int32_t at2_cena_poses(at2_cena* c, double* saida, int32_t maximo) {
  if (!c || !saida) return 0;
  const int32_t n =
      std::min<int32_t>(maximo, (int32_t)c->posesPorQuadro.size());
  for (int32_t i = 0; i < n; i++) {
    const auto& [q, p] = c->posesPorQuadro[i];
    double* d = saida + (size_t)i * 13;
    d[0] = q;
    for (int k = 0; k < 9; k++) d[1 + k] = p.R.m[k];
    d[10] = p.t.x;
    d[11] = p.t.y;
    d[12] = p.t.z;
  }
  return n;
}

AT2_API int32_t at2_cena_quantos_pontos(at2_cena* c) {
  return c ? (int32_t)c->saida.size() : 0;
}

AT2_API int32_t at2_cena_pontos(at2_cena* c, double* saida, int32_t maximo) {
  if (!c || !saida) return 0;
  const int32_t n = std::min<int32_t>(maximo, (int32_t)c->saida.size());
  for (int32_t i = 0; i < n; i++) {
    const auto& p = c->saida[i];
    double* d = saida + (size_t)i * 6;
    d[0] = p.id;
    d[1] = p.X.x;
    d[2] = p.X.y;
    d[3] = p.X.z;
    d[4] = p.erro;
    d[5] = p.vistas;
  }
  return n;
}

}  // extern "C"
