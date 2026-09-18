#include "compositor.h"

#include <algorithm>
#include <cmath>

namespace aurea::render {

namespace {
constexpr float kEpsilonDeEscala = 1e-6F;

/// A FUNCAO B DE CADA MODO, em cores NAO-PREMULTIPLICADAS.
///
/// `b` e o fundo e `s` a fonte — a ordem importa: o sobrepor e o
/// hardlight com as camadas trocadas, e escrever ao contrario daria a
/// curva espelhada (o erro classico de quem implementa os dois de
/// memoria).
[[nodiscard]] float funcao_b(Mistura modo, float b, float s) noexcept {
  switch (modo) {
    case Mistura::normal:
      return s;
    case Mistura::multiplicar:
      return b * s;
    case Mistura::tela:
      return b + s - b * s;
    case Mistura::sobrepor:
      // Overlay = HardLight(fonte, fundo).
      return s <= 0.5F ? 2.0F * b * s : 1.0F - 2.0F * (1.0F - b) * (1.0F - s);
    case Mistura::somar:
      return std::min(1.0F, b + s);
    case Mistura::escurecer:
      return std::min(b, s);
    case Mistura::clarear:
      return std::max(b, s);
    case Mistura::diferenca:
      return std::fabs(b - s);
  }
  return s;
}

[[nodiscard]] float de_byte(std::uint8_t v) noexcept {
  return static_cast<float>(v) * (1.0F / 255.0F);
}

[[nodiscard]] std::uint8_t para_byte(float v) noexcept {
  const float c = prender(v, 0.0F, 1.0F) * 255.0F + 0.5F;
  return static_cast<std::uint8_t>(c);
}

/// AMOSTRA BILINEAR DE UMA TEXTURA, ja premultiplicada.
///
/// A MULTIPLICACAO VEM ANTES DA INTERPOLACAO. Interpolar cor e alfa
/// separados e multiplicar depois faz a borda de um recorte sangrar a cor
/// de dentro do pixel transparente — o halo escuro classico.
[[nodiscard]] void amostrar_textura(const CargaDeTextura& t, float u, float v,
                                    float* saida) noexcept {
  const float fx = u * static_cast<float>(t.largura) - 0.5F;
  const float fy = v * static_cast<float>(t.altura) - 0.5F;
  const int x0 = static_cast<int>(std::floor(fx));
  const int y0 = static_cast<int>(std::floor(fy));
  const float tx = fx - static_cast<float>(x0);
  const float ty = fy - static_cast<float>(y0);

  const auto texel = [&](int x, int y, float* out) noexcept {
    const int cx = std::clamp(x, 0, static_cast<int>(t.largura) - 1);
    const int cy = std::clamp(y, 0, static_cast<int>(t.altura) - 1);
    const std::size_t i =
        (static_cast<std::size_t>(cy) * t.largura + static_cast<std::size_t>(cx)) * 4;
    const float a = de_byte(t.rgba[i + 3]);
    out[0] = de_byte(t.rgba[i + 0]) * a;
    out[1] = de_byte(t.rgba[i + 1]) * a;
    out[2] = de_byte(t.rgba[i + 2]) * a;
    out[3] = a;
  };

  float p00[4], p10[4], p01[4], p11[4];
  texel(x0, y0, p00);
  texel(x0 + 1, y0, p10);
  texel(x0, y0 + 1, p01);
  texel(x0 + 1, y0 + 1, p11);
  for (int k = 0; k < 4; ++k) {
    const float cima = p00[k] + (p10[k] - p00[k]) * tx;
    const float baixo = p01[k] + (p11[k] - p01[k]) * tx;
    saida[k] = cima + (baixo - cima) * ty;
  }
}
}  // namespace

Cor desmultiplicar(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                   std::uint8_t a) noexcept {
  if (a == 0) return Cor{0, 0, 0, 0};
  const float inv = 255.0F / static_cast<float>(a);
  return Cor{para_byte(de_byte(r) * inv), para_byte(de_byte(g) * inv),
             para_byte(de_byte(b) * inv), a};
}

void misturar_pixel(float* destino, const float* fonte,
                    Mistura modo) noexcept {
  const float as = fonte[3];
  if (as <= 0.0F) return;  // nada a fazer: nem o alfa muda
  const float ab = destino[3];

  const float inv_as = as > 0.0F ? 1.0F / as : 0.0F;
  const float inv_ab = ab > 0.0F ? 1.0F / ab : 0.0F;

  const float cs[3] = {fonte[0] * inv_as, fonte[1] * inv_as, fonte[2] * inv_as};
  const float cb[3] = {destino[0] * inv_ab, destino[1] * inv_ab,
                       destino[2] * inv_ab};

  const float peso_fonte = as * (1.0F - ab);
  const float peso_misto = as * ab;
  const float peso_fundo = (1.0F - as) * ab;

  for (int k = 0; k < 3; ++k) {
    destino[k] = peso_fonte * cs[k] + peso_misto * funcao_b(modo, cb[k], cs[k]) +
                 peso_fundo * cb[k];
  }
  destino[3] = as + ab * (1.0F - as);
}

Resulta<std::uint32_t> Compositor::desenhar(CargaDeAlvo& alvo,
                                            std::span<const Camada> camadas,
                                            std::uint32_t amostras) {
  if (alvo.largura == 0 || alvo.altura == 0) return Erro::argumento;
  if (alvo.pixels.size() <
      static_cast<std::size_t>(alvo.largura) * alvo.altura * 4) {
    return Erro::estado_invalido;
  }

  const std::uint32_t n = std::clamp<std::uint32_t>(amostras, 1, 4);
  stats_ = EstatisticasDoCompositor{};
  stats_.amostras_por_pixel = n * n;

  // AS CAMADAS VALIDAS SAO RESOLVIDAS ANTES DO LACO DE PIXELS.
  //
  // Procurar o recurso de textura dentro do laco de pixels seria um
  // `unordered_map::find` por amostra — dois milhoes deles num quadro de
  // 1080p com quatro amostras. Aqui a busca acontece uma vez por camada,
  // e as alcas ficam vivas ate o fim do desenho (senao o gerenciador
  // poderia despejar a textura no meio do quadro).
  struct Item {
    const Camada* camada = nullptr;
    const CargaDeTextura* textura = nullptr;
  };
  std::vector<AlcaDeRecurso> alcas;
  std::vector<Item> pilha;
  alcas.reserve(camadas.size());
  pilha.reserve(camadas.size());

  for (const Camada& c : camadas) {
    if (c.tipo == TipoDeCamada::vazia) continue;
    if (c.largura <= 0.0F || c.altura <= 0.0F ||
        std::fabs(c.escala_x) < kEpsilonDeEscala ||
        std::fabs(c.escala_y) < kEpsilonDeEscala || c.opacidade <= 0.0F) {
      ++stats_.camadas_invalidas;
      continue;
    }
    Item item;
    item.camada = &c;
    if (c.tipo == TipoDeCamada::textura) {
      AlcaDeRecurso r = recursos_.usar(c.textura);
      if (!r.viva()) {
        ++stats_.camadas_invalidas;
        continue;
      }
      item.textura = dynamic_cast<const CargaDeTextura*>(r.carga());
      if (item.textura == nullptr || !item.textura->valida()) {
        ++stats_.camadas_invalidas;
        continue;
      }
      alcas.push_back(std::move(r));
    }
    pilha.push_back(item);
  }

  const float passo = 1.0F / static_cast<float>(n);
  const float peso = 1.0F / static_cast<float>(n * n);

  for (std::uint32_t py = 0; py < alvo.altura; ++py) {
    for (std::uint32_t px = 0; px < alvo.largura; ++px) {
      float acumulado[4] = {0.0F, 0.0F, 0.0F, 0.0F};

      for (std::uint32_t sy = 0; sy < n; ++sy) {
        for (std::uint32_t sx = 0; sx < n; ++sx) {
          const float ponto_x =
              static_cast<float>(px) + (static_cast<float>(sx) + 0.5F) * passo;
          const float ponto_y =
              static_cast<float>(py) + (static_cast<float>(sy) + 0.5F) * passo;
          float composicao[4] = {0.0F, 0.0F, 0.0F, 0.0F};

          for (const Item& item : pilha) {
            const Camada& c = *item.camada;

            // A VOLTA PARA O ESPACO LOCAL: desfaz a translacao para a
            // ancora, gira ao contrario e desfaz a escala. A ordem e a
            // inversa da montagem (ancora, escala, rotacao, posicao).
            const float dx = ponto_x - c.x;
            const float dy = ponto_y - c.y;
            const float rad =
                -c.rotacao_graus * (3.14159265358979323846F / 180.0F);
            const float co = std::cos(rad);
            const float si = std::sin(rad);
            const float girado_x = dx * co - dy * si;
            const float girado_y = dx * si + dy * co;
            const float local_x =
                girado_x / c.escala_x + c.ancora_x * c.largura;
            const float local_y =
                girado_y / c.escala_y + c.ancora_y * c.altura;

            if (local_x < 0.0F || local_y < 0.0F || local_x >= c.largura ||
                local_y >= c.altura) {
              continue;
            }

            float fonte[4];
            if (item.textura != nullptr) {
              amostrar_textura(*item.textura, local_x / c.largura,
                               local_y / c.altura, fonte);
              // A OPACIDADE DA CAMADA ENTRA NO ALFA, uma vez so.
              for (int k = 0; k < 4; ++k) fonte[k] *= c.opacidade;
            } else {
              const float a = de_byte(c.cor.a) * c.opacidade;
              fonte[0] = de_byte(c.cor.r) * a;
              fonte[1] = de_byte(c.cor.g) * a;
              fonte[2] = de_byte(c.cor.b) * a;
              fonte[3] = a;
            }
            misturar_pixel(composicao, fonte, c.mistura);
          }

          for (int k = 0; k < 4; ++k) acumulado[k] += composicao[k];
        }
      }

      const std::size_t i =
          (static_cast<std::size_t>(py) * alvo.largura + px) * 4;
      for (int k = 0; k < 4; ++k) {
        alvo.pixels[i + static_cast<std::size_t>(k)] =
            para_byte(acumulado[k] * peso);
      }
      ++stats_.pixels_escritos;
    }
  }

  stats_.camadas_desenhadas = static_cast<std::uint32_t>(pilha.size());
  const auto total = static_cast<std::int64_t>(camadas.size());
  stats_.camadas_fora = static_cast<std::uint32_t>(
      std::max<std::int64_t>(0, total - stats_.camadas_desenhadas -
                                    stats_.camadas_invalidas));
  return stats_.camadas_desenhadas;
}

}  // namespace aurea::render
