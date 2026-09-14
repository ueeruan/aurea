// O SEGUIDOR DE PONTOS: Lucas-Kanade piramidal com checagem de ida e volta.
//
// O antigo seguia com NCC num molde fixo a 240 px, arredondando a posicao a
// cada quadro (o subpixel se perdia) e matando o ponto quando o molde
// deformava — justamente nos movimentos que dao paralaxe. Aqui: 3 niveis de
// piramide, janela 15x15 com interpolacao bilinear, e so fica o ponto que
// volta para onde saiu (erro de ida e volta abaixo de 1 px).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "tracker.h"

namespace {

struct Imagem {
  int w = 0, h = 0;
  std::vector<float> px, gx, gy;
  float at(float x, float y) const {
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1.001));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1.001));
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const float ax = x - x0, ay = y - y0;
    const float* p = &px[y0 * w + x0];
    return (1 - ay) * ((1 - ax) * p[0] + ax * p[1]) +
           ay * ((1 - ax) * p[w] + ax * p[w + 1]);
  }
  float amostra(const std::vector<float>& v, float x, float y) const {
    x = std::clamp(x, 0.0f, static_cast<float>(w - 1.001));
    y = std::clamp(y, 0.0f, static_cast<float>(h - 1.001));
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const float ax = x - x0, ay = y - y0;
    const float* p = &v[y0 * w + x0];
    return (1 - ay) * ((1 - ax) * p[0] + ax * p[1]) +
           ay * ((1 - ax) * p[w] + ax * p[w + 1]);
  }
  void gradientes() {
    gx.assign(w * h, 0);
    gy.assign(w * h, 0);
    for (int y = 1; y < h - 1; y++)
      for (int x = 1; x < w - 1; x++) {
        const int i = y * w + x;
        // Scharr: menos erro de direcao que a diferenca central.
        gx[i] = (3 * (px[i - w + 1] - px[i - w - 1]) +
                 10 * (px[i + 1] - px[i - 1]) +
                 3 * (px[i + w + 1] - px[i + w - 1])) / 32.0f;
        gy[i] = (3 * (px[i + w - 1] - px[i - w - 1]) +
                 10 * (px[i + w] - px[i - w]) +
                 3 * (px[i + w + 1] - px[i - w + 1])) / 32.0f;
      }
  }
};

using Piramide = std::vector<Imagem>;

Piramide montar(const uint8_t* gray, int w, int h, int niveis) {
  Piramide p(niveis);
  p[0].w = w;
  p[0].h = h;
  p[0].px.resize(w * h);
  for (int i = 0; i < w * h; i++) p[0].px[i] = gray[i];
  for (int l = 1; l < niveis; l++) {
    const Imagem& a = p[l - 1];
    Imagem& b = p[l];
    b.w = std::max(8, a.w / 2);
    b.h = std::max(8, a.h / 2);
    b.px.resize(b.w * b.h);
    for (int y = 0; y < b.h; y++)
      for (int x = 0; x < b.w; x++) {
        // 1-2-1 em cada eixo: sem aliasing que confunde o nivel grosso.
        float s = 0, pesos = 0;
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) {
            const int sx = std::clamp(2 * x + dx, 0, a.w - 1);
            const int sy = std::clamp(2 * y + dy, 0, a.h - 1);
            const float wgt = (dx == 0 ? 2.0f : 1.0f) * (dy == 0 ? 2.0f : 1.0f);
            s += wgt * a.px[sy * a.w + sx];
            pesos += wgt;
          }
        b.px[y * b.w + x] = s / pesos;
      }
  }
  for (auto& im : p) im.gradientes();
  return p;
}

// Segue um ponto de A para B. Devolve falso se sair ou nao convergir.
bool seguir(const Piramide& A, const Piramide& B, float x, float y, float& ox,
            float& oy) {
  const int niveis = static_cast<int>(A.size());
  const int meia = 7;
  float gxAcc = 0, gyAcc = 0;  // deslocamento no nivel atual
  for (int l = niveis - 1; l >= 0; l--) {
    const float esc = 1.0f / static_cast<float>(1 << l);
    const float px = x * esc, py = y * esc;
    const Imagem& I = A[l];
    const Imagem& J = B[l];
    if (px < meia + 1 || py < meia + 1 || px > I.w - meia - 2 || py > I.h - meia - 2) {
      if (l == 0) return false;
      gxAcc *= 2;
      gyAcc *= 2;
      continue;
    }
    // Matriz de estrutura do molde.
    double gxx = 0, gxy = 0, gyy = 0;
    const int lado = 2 * meia + 1;
    std::vector<float> tI(lado * lado), tGx(lado * lado), tGy(lado * lado);
    int k = 0;
    for (int dy = -meia; dy <= meia; dy++)
      for (int dx = -meia; dx <= meia; dx++, k++) {
        const float sx = px + dx, sy = py + dy;
        tI[k] = I.at(sx, sy);
        tGx[k] = I.amostra(I.gx, sx, sy);
        tGy[k] = I.amostra(I.gy, sx, sy);
        gxx += tGx[k] * tGx[k];
        gxy += tGx[k] * tGy[k];
        gyy += tGy[k] * tGy[k];
      }
    const double det = gxx * gyy - gxy * gxy;
    if (det < 1e-3 * lado * lado) return false;
    float vx = 0, vy = 0;
    for (int it = 0; it < 12; it++) {
      double bx = 0, by = 0;
      k = 0;
      for (int dy = -meia; dy <= meia; dy++)
        for (int dx = -meia; dx <= meia; dx++, k++) {
          const float d = tI[k] - J.at(px + dx + gxAcc + vx, py + dy + gyAcc + vy);
          bx += d * tGx[k];
          by += d * tGy[k];
        }
      const float ux = static_cast<float>((gyy * bx - gxy * by) / det);
      const float uy = static_cast<float>((gxx * by - gxy * bx) / det);
      vx += ux;
      vy += uy;
      if (ux * ux + uy * uy < 1e-4f) break;
    }
    gxAcc += vx;
    gyAcc += vy;
    if (l > 0) {
      gxAcc *= 2;
      gyAcc *= 2;
    }
  }
  ox = x + gxAcc;
  oy = y + gyAcc;
  return ox >= 4 && oy >= 4 && ox <= A[0].w - 5 && oy <= A[0].h - 5;
}

struct Ponto {
  int id;
  float x, y;
  bool vivo;
};

}  // namespace

struct att_tracker {
  int w, h, alvo;
  int proximoId = 0;
  Piramide anterior;
  std::vector<Ponto> pontos;
  std::vector<double> observacoes;  // id, quadro, x, y

  void semear(const Imagem& im, int quadro) {
    int vivos = 0;
    for (const auto& p : pontos) vivos += p.vivo;
    if (vivos >= alvo * 0.7) return;
    // Menor autovalor da matriz de estrutura em janela 5x5 (Shi-Tomasi).
    const int w_ = im.w, h_ = im.h;
    const int celula = std::max(12, std::min(w_, h_) / 18);
    const float margem = 10;
    std::vector<char> ocupado(((w_ / celula) + 1) * ((h_ / celula) + 1), 0);
    for (const auto& p : pontos)
      if (p.vivo)
        ocupado[static_cast<int>(p.y) / celula * ((w_ / celula) + 1) +
                static_cast<int>(p.x) / celula] = 1;
    struct Cand { float score, x, y; };
    std::vector<Cand> cands;
    for (int cy = 0; cy <= h_ / celula; cy++)
      for (int cx = 0; cx <= w_ / celula; cx++) {
        if (ocupado[cy * ((w_ / celula) + 1) + cx]) continue;
        Cand melhor{0, 0, 0};
        for (int y = cy * celula + 2; y < std::min(h_ - static_cast<int>(margem), (cy + 1) * celula); y += 2)
          for (int x = cx * celula + 2; x < std::min(w_ - static_cast<int>(margem), (cx + 1) * celula); x += 2) {
            if (x < margem || y < margem) continue;
            double a = 0, b = 0, c = 0;
            for (int dy = -2; dy <= 2; dy++)
              for (int dx = -2; dx <= 2; dx++) {
                const int i = (y + dy) * w_ + (x + dx);
                a += im.gx[i] * im.gx[i];
                b += im.gx[i] * im.gy[i];
                c += im.gy[i] * im.gy[i];
              }
            const float menor = static_cast<float>(
                (a + c) * 0.5 - std::sqrt((a - c) * (a - c) * 0.25 + b * b));
            if (menor > melhor.score) melhor = {menor, static_cast<float>(x), static_cast<float>(y)};
          }
        if (melhor.score > 0) cands.push_back(melhor);
      }
    if (cands.empty()) return;
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.score > b.score; });
    const float corte = cands.front().score * 0.02f;
    for (const auto& c : cands) {
      if (vivos >= alvo) break;
      if (c.score < corte) break;
      pontos.push_back({proximoId++, c.x, c.y, true});
      observacoes.insert(observacoes.end(), {static_cast<double>(pontos.back().id),
                                             static_cast<double>(quadro), c.x, c.y});
      vivos++;
    }
  }
};

extern "C" {

int32_t att_version(void) { return 1; }

att_tracker* att_tracker_create(int32_t width, int32_t height, int32_t max_points) {
  if (width < 32 || height < 32) return nullptr;
  auto* t = new att_tracker();
  t->w = width;
  t->h = height;
  t->alvo = std::max(20, max_points);
  return t;
}

void att_tracker_destroy(att_tracker* t) { delete t; }

int32_t att_tracker_push(att_tracker* t, const uint8_t* gray, int32_t frame) {
  if (!t || !gray) return 0;
  const int niveis = std::min(t->w, t->h) >= 240 ? 3 : 2;
  Piramide atual = montar(gray, t->w, t->h, niveis);
  if (!t->anterior.empty()) {
    for (auto& p : t->pontos) {
      if (!p.vivo) continue;
      float nx, ny, bx, by;
      if (!seguir(t->anterior, atual, p.x, p.y, nx, ny) ||
          !seguir(atual, t->anterior, nx, ny, bx, by) ||
          (bx - p.x) * (bx - p.x) + (by - p.y) * (by - p.y) > 1.0f) {
        p.vivo = false;
        continue;
      }
      p.x = nx;
      p.y = ny;
      t->observacoes.insert(t->observacoes.end(),
                            {static_cast<double>(p.id), static_cast<double>(frame), nx, ny});
    }
  }
  t->semear(atual[0], frame);
  t->anterior = std::move(atual);
  int vivos = 0;
  for (const auto& p : t->pontos) vivos += p.vivo;
  return vivos;
}

int32_t att_tracker_observation_count(att_tracker* t) {
  return t ? static_cast<int32_t>(t->observacoes.size() / 4) : 0;
}

int32_t att_tracker_observations(att_tracker* t, double* out, int32_t max) {
  if (!t || !out) return 0;
  const int n = std::min<int>(max, static_cast<int>(t->observacoes.size() / 4));
  std::memcpy(out, t->observacoes.data(), sizeof(double) * n * 4);
  return n;
}

}  // extern "C"
