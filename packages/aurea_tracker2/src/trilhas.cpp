// O SEGUIDOR DO MOTOR 2.0 — Lucas-Kanade piramidal com tres mortes.
//
// O motor 1 seguia por busca de molde (NCC numa janela) com refino de
// parabola. Funciona, mas escorrega: meia janela de busca e o teto do
// movimento, e um ponto que desliza devagar passa na semelhanca por
// muito tempo. Este seguidor e o classico que os rastreadores de verdade
// usam, com as tres checagens que matam um ponto ANTES de ele envenenar
// a cena:
//
//   1. IDA E VOLTA: seguir o ponto para frente e depois de volta tem de
//      cair onde comecou. Oclusao e borda reprovam aqui.
//   2. TRAVA DE DERIVA: o ponto e conferido por NCC contra o molde do
//      quadro em que NASCEU, todo quadro. Deriva acumulada — o fantasma
//      classico — reprova aqui, porque o ponto ja nao se parece consigo.
//   3. CONTRASTE: janela sem textura nao prende o LK em nada; o ponto
//      morre em vez de flutuar.
//
// Multi-thread por ponto (os pontos sao independentes) e deterministico:
// nenhuma thread muda o resultado, so o relogio.
#include "motor.h"

#include <cstring>
#include <thread>
#include <vector>

#include "nucleo.hpp"

namespace {

constexpr int kNiveis = 4;       // piramide: 640 -> 320 -> 160 -> 80
constexpr int kRaioLk = 7;       // janela 15x15 do LK
constexpr int kRaioMolde = 6;    // janela 13x13 da trava de deriva
constexpr int kIteracoesLk = 12;
constexpr double kConvergiuPx = 0.03;
constexpr double kIdaEVoltaPx = 1.2;   // no nivel 0
constexpr double kDerivaNccMinima = 0.5;
constexpr double kContrasteMinimo = 3.0;
constexpr int kDistanciaMinima = 12;   // entre pontos, ao semear

struct Nivel {
  int w = 0, h = 0;
  std::vector<float> px;

  float em(int x, int y) const {
    x = std::max(0, std::min(w - 1, x));
    y = std::max(0, std::min(h - 1, y));
    return px[y * w + x];
  }

  // Bilinear com borda presa.
  float amostra(double x, double y) const {
    const int xi = (int)std::floor(x), yi = (int)std::floor(y);
    const double ax = x - xi, ay = y - yi;
    const double a = em(xi, yi), b = em(xi + 1, yi);
    const double c = em(xi, yi + 1), d = em(xi + 1, yi + 1);
    return (float)((1 - ay) * ((1 - ax) * a + ax * b) +
                   ay * ((1 - ax) * c + ax * d));
  }
};

struct Piramide {
  Nivel nivel[kNiveis];

  void montar(const uint8_t* cinza, int w, int h) {
    nivel[0].w = w;
    nivel[0].h = h;
    nivel[0].px.resize((size_t)w * h);
    for (int i = 0; i < w * h; i++) nivel[0].px[i] = cinza[i];
    for (int n = 1; n < kNiveis; n++) {
      const Nivel& de = nivel[n - 1];
      Nivel& para = nivel[n];
      para.w = std::max(1, de.w / 2);
      para.h = std::max(1, de.h / 2);
      para.px.resize((size_t)para.w * para.h);
      for (int y = 0; y < para.h; y++)
        for (int x = 0; x < para.w; x++) {
          const int x2 = x * 2, y2 = y * 2;
          para.px[y * para.w + x] =
              (de.em(x2, y2) + de.em(x2 + 1, y2) + de.em(x2, y2 + 1) +
               de.em(x2 + 1, y2 + 1)) /
              4.0f;
        }
    }
  }
};

struct Ponto {
  int32_t id = 0;
  int32_t nasceu = 0;
  double x = 0, y = 0;  // posicao atual (nivel 0)
  bool vivo = true;
  // O molde de nascimento, ja com media zero, e a sua norma — a trava de
  // deriva compara com ISTO, nunca com o quadro anterior.
  float molde[(2 * kRaioMolde + 1) * (2 * kRaioMolde + 1)];
  double normaDoMolde = 0;
};

// LK de um ponto num nivel: resolve o deslocamento que casa a janela do
// quadro anterior com o atual, iterando ate parar de andar.
bool lkNoNivel(const Nivel& antes, const Nivel& agora, double ax, double ay,
               double* gx_, double* gy_) {
  double gx = *gx_, gy = *gy_;
  // Gradientes e Hessiana da janela do quadro ANTERIOR (fixos na iteracao).
  constexpr int lado = 2 * kRaioLk + 1;
  double ix[lado * lado], iy[lado * lado], iv[lado * lado];
  double sxx = 0, sxy = 0, syy = 0;
  int k = 0;
  for (int dy = -kRaioLk; dy <= kRaioLk; dy++)
    for (int dx = -kRaioLk; dx <= kRaioLk; dx++, k++) {
      const double px = ax + dx, py = ay + dy;
      iv[k] = antes.amostra(px, py);
      ix[k] = (antes.amostra(px + 1, py) - antes.amostra(px - 1, py)) / 2;
      iy[k] = (antes.amostra(px, py + 1) - antes.amostra(px, py - 1)) / 2;
      sxx += ix[k] * ix[k];
      sxy += ix[k] * iy[k];
      syy += iy[k] * iy[k];
    }
  const double det = sxx * syy - sxy * sxy;
  if (det < 1e-6 || sxx + syy < kContrasteMinimo) return false;

  for (int it = 0; it < kIteracoesLk; it++) {
    double bx = 0, by = 0;
    k = 0;
    for (int dy = -kRaioLk; dy <= kRaioLk; dy++)
      for (int dx = -kRaioLk; dx <= kRaioLk; dx++, k++) {
        const double d = agora.amostra(ax + gx + dx, ay + gy + dy) - iv[k];
        bx += d * ix[k];
        by += d * iy[k];
      }
    const double ux = -(syy * bx - sxy * by) / det;
    const double uy = -(-sxy * bx + sxx * by) / det;
    gx += ux;
    gy += uy;
    if (ux * ux + uy * uy < kConvergiuPx * kConvergiuPx) break;
  }
  *gx_ = gx;
  *gy_ = gy;
  return true;
}

// A piramide inteira: comeca no nivel grosso e refina descendo.
bool lk(const Piramide& antes, const Piramide& agora, double x0, double y0,
        double* x1, double* y1) {
  double gx = 0, gy = 0;
  for (int n = kNiveis - 1; n >= 0; n--) {
    const double escala = 1.0 / (1 << n);
    if (!lkNoNivel(antes.nivel[n], agora.nivel[n], x0 * escala, y0 * escala,
                   &gx, &gy)) {
      if (n == 0) return false;
      // Nivel grosso sem textura ainda pode casar no fino.
      gx *= 2;
      gy *= 2;
      continue;
    }
    if (n > 0) {
      gx *= 2;
      gy *= 2;
    }
  }
  *x1 = x0 + gx;
  *y1 = y0 + gy;
  return true;
}

// NCC entre o molde de nascimento e a janela atual.
double nccDoMolde(const Ponto& p, const Nivel& agora) {
  constexpr int lado = 2 * kRaioMolde + 1;
  double atual[lado * lado];
  double media = 0;
  int k = 0;
  for (int dy = -kRaioMolde; dy <= kRaioMolde; dy++)
    for (int dx = -kRaioMolde; dx <= kRaioMolde; dx++, k++) {
      atual[k] = agora.amostra(p.x + dx, p.y + dy);
      media += atual[k];
    }
  media /= lado * lado;
  double soma = 0, norma2 = 0;
  for (k = 0; k < lado * lado; k++) {
    const double v = atual[k] - media;
    soma += v * p.molde[k];
    norma2 += v * v;
  }
  const double n = std::sqrt(norma2) * p.normaDoMolde;
  return n < 1e-9 ? 0 : soma / n;
}

void guardarMolde(Ponto& p, const Nivel& quadro) {
  constexpr int lado = 2 * kRaioMolde + 1;
  double media = 0;
  int k = 0;
  for (int dy = -kRaioMolde; dy <= kRaioMolde; dy++)
    for (int dx = -kRaioMolde; dx <= kRaioMolde; dx++, k++) {
      p.molde[k] = quadro.amostra(p.x + dx, p.y + dy);
      media += p.molde[k];
    }
  media /= lado * lado;
  double norma2 = 0;
  for (k = 0; k < lado * lado; k++) {
    p.molde[k] -= (float)media;
    norma2 += p.molde[k] * p.molde[k];
  }
  p.normaDoMolde = std::sqrt(norma2);
}

}  // namespace

struct at2_seguidor {
  int w = 0, h = 0, maximo = 0;
  Piramide antes, agora;
  bool temAntes = false;
  std::vector<Ponto> pontos;
  std::vector<double> obs;  // [id, quadro, x, y]
  int32_t proximoId = 1;

  int vivos() const {
    int n = 0;
    for (const auto& p : pontos)
      if (p.vivo) n++;
    return n;
  }

  // SEMEIA cantos Shi-Tomasi espalhados, longe dos que ja vivem.
  void semear(int quadro) {
    const Nivel& q = agora.nivel[0];
    const int alvo = maximo - vivos();
    if (alvo <= 0) return;

    // Gradientes uma vez; somas de janela por imagens integrais.
    std::vector<double> gx((size_t)w * h, 0), gy((size_t)w * h, 0);
    for (int y = 1; y < h - 1; y++)
      for (int x = 1; x < w - 1; x++) {
        const int i = y * w + x;
        gx[i] = (q.px[i + 1] - q.px[i - 1]) / 2.0;
        gy[i] = (q.px[i + w] - q.px[i - w]) / 2.0;
      }
    const int W = w + 1;
    std::vector<double> ixx((size_t)W * (h + 1), 0), iyy((size_t)W * (h + 1), 0),
        ixy((size_t)W * (h + 1), 0);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        const int i = y * w + x;
        ixx[(y + 1) * W + (x + 1)] = gx[i] * gx[i] + ixx[y * W + (x + 1)] +
                                     ixx[(y + 1) * W + x] - ixx[y * W + x];
        iyy[(y + 1) * W + (x + 1)] = gy[i] * gy[i] + iyy[y * W + (x + 1)] +
                                     iyy[(y + 1) * W + x] - iyy[y * W + x];
        ixy[(y + 1) * W + (x + 1)] = gx[i] * gy[i] + ixy[y * W + (x + 1)] +
                                     ixy[(y + 1) * W + x] - ixy[y * W + x];
      }
    auto janela = [&](const std::vector<double>& s, int x0, int y0, int x1,
                      int y1) {
      return s[y1 * W + x1] - s[y0 * W + x1] - s[y1 * W + x0] + s[y0 * W + x0];
    };

    // A forca de canto (menor autovalor) so nas celulas de uma grade — e
    // dentro de cada celula fica O MELHOR pixel. Espalhar importa tanto
    // quanto a forca: cinquenta cantos na mesma quina dizem menos sobre a
    // camera do que dez pelo quadro inteiro.
    const int celula = std::max(14, std::min(w, h) / 18);
    const int margem = kRaioLk + 3;
    const int raio = 3;

    // Ocupacao dos vivos, para nao nascer em cima de ninguem.
    const int gradeW = (w + kDistanciaMinima - 1) / kDistanciaMinima;
    const int gradeH = (h + kDistanciaMinima - 1) / kDistanciaMinima;
    std::vector<uint8_t> ocupado((size_t)gradeW * gradeH, 0);
    auto marcar = [&](double x, double y) {
      const int cx = (int)(x / kDistanciaMinima), cy = (int)(y / kDistanciaMinima);
      if (cx >= 0 && cy >= 0 && cx < gradeW && cy < gradeH)
        ocupado[cy * gradeW + cx] = 1;
    };
    auto livre = [&](double x, double y) {
      const int cx = (int)(x / kDistanciaMinima), cy = (int)(y / kDistanciaMinima);
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          const int nx = cx + dx, ny = cy + dy;
          if (nx >= 0 && ny >= 0 && nx < gradeW && ny < gradeH &&
              ocupado[ny * gradeW + nx])
            return false;
        }
      return true;
    };
    for (const auto& p : pontos)
      if (p.vivo) marcar(p.x, p.y);

    struct Canto {
      double forca;
      int x, y;
    };
    std::vector<Canto> cantos;
    double maiorForca = 0;
    for (int cy = margem; cy + celula < h - margem; cy += celula)
      for (int cx = margem; cx + celula < w - margem; cx += celula) {
        double melhor = 0;
        int mx = -1, my = -1;
        for (int y = cy; y < cy + celula; y += 2)
          for (int x = cx; x < cx + celula; x += 2) {
            const double sxx = janela(ixx, x - raio, y - raio, x + raio + 1,
                                      y + raio + 1);
            const double syy = janela(iyy, x - raio, y - raio, x + raio + 1,
                                      y + raio + 1);
            const double sxy = janela(ixy, x - raio, y - raio, x + raio + 1,
                                      y + raio + 1);
            const double meio = (sxx + syy) / 2;
            const double raiz =
                std::sqrt(std::max(0.0, meio * meio - (sxx * syy - sxy * sxy)));
            const double f = meio - raiz;
            if (f > melhor) {
              melhor = f;
              mx = x;
              my = y;
            }
          }
        if (mx >= 0) {
          cantos.push_back({melhor, mx, my});
          maiorForca = std::max(maiorForca, melhor);
        }
      }
    if (maiorForca <= 0) return;
    std::sort(cantos.begin(), cantos.end(),
              [](const Canto& a, const Canto& b) { return a.forca > b.forca; });
    // O corte e RELATIVO ao melhor canto do quadro: limiar absoluto acharia
    // tudo num quadro contrastado e nada num quadro de neblina.
    const double corte = maiorForca * 0.004;
    int nasceram = 0;
    for (const auto& c : cantos) {
      if (nasceram >= alvo) break;
      if (c.forca < corte) break;
      if (!livre(c.x, c.y)) continue;
      Ponto p;
      p.id = proximoId++;
      p.nasceu = quadro;
      p.x = c.x;
      p.y = c.y;
      guardarMolde(p, agora.nivel[0]);
      if (p.normaDoMolde < kContrasteMinimo) continue;
      marcar(p.x, p.y);
      obs.insert(obs.end(),
                 {(double)p.id, (double)quadro, p.x, p.y});
      pontos.push_back(p);
      nasceram++;
    }
  }

  void empurrar(const uint8_t* cinza, int32_t quadro) {
    agora.montar(cinza, w, h);
    if (!temAntes) {
      temAntes = true;
      semear(quadro);
      std::swap(antes, agora);
      return;
    }

    // Segue todos os vivos, em paralelo (cada ponto escreve so no proprio
    // lugar; o resultado independe de quantas threads ha).
    std::vector<int> indices;
    for (int i = 0; i < (int)pontos.size(); i++)
      if (pontos[i].vivo) indices.push_back(i);

    auto seguirFaixa = [&](int de, int ate) {
      for (int k = de; k < ate; k++) {
        Ponto& p = pontos[indices[k]];
        double nx, ny;
        if (!lk(antes, agora, p.x, p.y, &nx, &ny)) {
          p.vivo = false;
          continue;
        }
        // Perto da borda o molde ja esta metade fora da imagem.
        if (nx < kRaioLk + 2 || ny < kRaioLk + 2 || nx > w - kRaioLk - 3 ||
            ny > h - kRaioLk - 3) {
          p.vivo = false;
          continue;
        }
        // IDA E VOLTA.
        double vx, vy;
        if (!lk(agora, antes, nx, ny, &vx, &vy) ||
            (vx - p.x) * (vx - p.x) + (vy - p.y) * (vy - p.y) >
                kIdaEVoltaPx * kIdaEVoltaPx) {
          p.vivo = false;
          continue;
        }
        p.x = nx;
        p.y = ny;
        // TRAVA DE DERIVA: o ponto tem de continuar parecendo com quem
        // nasceu. E esta checagem que impede o rastreio fantasma.
        if (nccDoMolde(p, agora.nivel[0]) < kDerivaNccMinima) {
          p.vivo = false;
          continue;
        }
      }
    };

    const int n = (int)indices.size();
    const int nucleos =
        std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    if (n > 120 && nucleos > 1) {
      std::vector<std::thread> ts;
      const int passo = (n + nucleos - 1) / nucleos;
      for (int t = 0; t < nucleos; t++) {
        const int de = t * passo, ate = std::min(n, de + passo);
        if (de < ate) ts.emplace_back(seguirFaixa, de, ate);
      }
      for (auto& t : ts) t.join();
    } else {
      seguirFaixa(0, n);
    }

    for (const int i : indices) {
      const Ponto& p = pontos[i];
      if (p.vivo)
        obs.insert(obs.end(), {(double)p.id, (double)quadro, p.x, p.y});
    }

    // Repoe quando o time encolheu de verdade.
    if (vivos() < (maximo * 3) / 4) semear(quadro);
    std::swap(antes, agora);
  }
};

extern "C" {

AT2_API const char* at2_versao(void) { return "2.0.0"; }

AT2_API at2_seguidor* at2_seguidor_criar(int32_t largura, int32_t altura,
                                         int32_t maximo_de_pontos) {
  if (largura < 32 || altura < 32 || maximo_de_pontos < 8) return nullptr;
  auto* s = new at2_seguidor();
  s->w = largura;
  s->h = altura;
  s->maximo = maximo_de_pontos;
  return s;
}

AT2_API void at2_seguidor_destruir(at2_seguidor* s) { delete s; }

AT2_API int32_t at2_seguidor_empurrar(at2_seguidor* s, const uint8_t* cinza,
                                      int32_t quadro) {
  if (!s || !cinza) return AT2_ERR_ARGS;
  s->empurrar(cinza, quadro);
  return s->vivos();
}

AT2_API int32_t at2_seguidor_quantas(at2_seguidor* s) {
  return s ? (int32_t)(s->obs.size() / 4) : 0;
}

AT2_API int32_t at2_seguidor_observacoes(at2_seguidor* s, double* saida,
                                         int32_t maximo) {
  if (!s || !saida) return 0;
  const int32_t n = std::min<int32_t>(maximo, (int32_t)(s->obs.size() / 4));
  std::memcpy(saida, s->obs.data(), (size_t)n * 4 * sizeof(double));
  return n;
}

}  // extern "C"
