#include "particulas.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurea::render {

// ==================== O LAYOUT E CONTRATO ====================
//
// Se um campo novo entrar no meio de `InstanciaDeParticula`, o Dart le o
// vizinho e a nuvem sai com a cor do tamanho. O `static_assert` e o unico
// aviso que existe — nao ha ABI checada em tempo de execucao.
static_assert(sizeof(InstanciaDeParticula) == kFlutuantesDaInstancia * 4,
              "InstanciaDeParticula mudou de tamanho: o Dart precisa saber");

namespace {

constexpr float kPi = 3.14159265358979323846F;

[[nodiscard]] float prender_f(float v, float lo, float hi) noexcept {
  return v < lo ? lo : (v > hi ? hi : v);
}

/// O SORTEIO. xorshift32 de (semente, indice, canal): a mesma semente e o
/// mesmo indice devolvem SEMPRE o mesmo numero, que e o que faz o scrub
/// voltar ao mesmo quadro. A mistura de avalanche existe porque um XOR
/// linear de canal produzia faixas diagonais visiveis no campo — o defeito
/// classico de um gerador correlacionado entre eixos.
[[nodiscard]] float aleatorio(std::uint32_t semente, std::uint32_t i,
                              std::uint32_t canal) noexcept {
  std::uint32_t s = (semente * 0x9E3779B9U) ^ ((i + 1U) * 0x85EBCA6BU) ^
                    ((canal + 1U) * 0xC2B2AE35U);
  s ^= s >> 16;
  s *= 0x7FEB352DU;
  s ^= s >> 15;
  s *= 0x846CA68BU;
  s ^= s >> 16;
  return static_cast<float>(s & 0xFFFFFFU) / 16777216.0F;
}

[[nodiscard]] double aleatorio_d(std::uint32_t semente, std::uint32_t i,
                                 std::uint32_t canal) noexcept {
  return static_cast<double>(aleatorio(semente, i, canal));
}

/// O RUIDO DE VALOR 3D, suavizado — o campo da turbulencia.
///
/// Ruido de VALOR (e nao de gradiente) de proposito: ele vale zero em
/// media, custa quatro leituras por eixo e nao precisa de derivada. A
/// turbulencia do Aurea nao empurra um fluido, empurra a nuvem — e o olho
/// nao distingue os dois.
[[nodiscard]] double hash3(int x, int y, int z, std::uint32_t semente) noexcept {
  std::uint32_t h = (static_cast<std::uint32_t>(x) * 0x27D4EB2DU) ^
                    (static_cast<std::uint32_t>(y) * 0x165667B1U) ^
                    (static_cast<std::uint32_t>(z) * 0x9E3779B1U) ^
                    (semente * 0x85EBCA6BU);
  h ^= h >> 15;
  h *= 0x2C1B3C6DU;
  h ^= h >> 12;
  h *= 0x297A2D39U;
  h ^= h >> 15;
  return static_cast<double>(h & 0xFFFFFFU) / 16777216.0;
}

[[nodiscard]] double ruido3(double x, double y, double z,
                            std::uint32_t semente) noexcept {
  const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
  const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy),
            z0 = static_cast<int>(fz);
  const double tx = x - fx, ty = y - fy, tz = z - fz;
  const double ux = tx * tx * (3.0 - 2.0 * tx);
  const double uy = ty * ty * (3.0 - 2.0 * ty);
  const double uz = tz * tz * (3.0 - 2.0 * tz);
  const auto l = [](double a, double b, double t) { return a + (b - a) * t; };
  const double c00 =
      l(hash3(x0, y0, z0, semente), hash3(x0 + 1, y0, z0, semente), ux);
  const double c10 =
      l(hash3(x0, y0 + 1, z0, semente), hash3(x0 + 1, y0 + 1, z0, semente), ux);
  const double c01 =
      l(hash3(x0, y0, z0 + 1, semente), hash3(x0 + 1, y0, z0 + 1, semente), ux);
  const double c11 = l(hash3(x0, y0 + 1, z0 + 1, semente),
                       hash3(x0 + 1, y0 + 1, z0 + 1, semente), ux);
  return l(l(c00, c10, uy), l(c01, c11, uy), uz);
}

/// ================= A TRAJETORIA, EM FORMA FECHADA =====================
///
/// Resolve `u'' + k u' + s u = g` com `u(0) = u0` e `u'(0) = v0`.
///
/// POR QUE NAO INTEGRAR PASSO A PASSO: o numero de passos dependeria de
/// quantos quadros caberam, e a nuvem mudaria de forma entre o preview e a
/// exportacao — o mesmo projeto sairia com duas caras. A solucao analitica
/// nao tem esse problema, e ainda custa uma exponencial em vez de centenas
/// de iteracoes.
///
///   * `s = 0` e o caso de sempre (gravidade + arrasto), e a formula
///     reduz exatamente ao que o motor ja fazia — projeto antigo nao
///     muda de forma;
///   * `s > 0` e a ATRACAO (mola);
///   * `s < 0` e a REPULSAO: as raizes reais trocam de sinal e a solucao
///     vira hiperbolica sozinha, sem ramo separado.
[[nodiscard]] double deslocamento(double u0, double v0, double g, double k,
                                  double s, double t) noexcept {
  if (t <= 0.0) return u0;
  constexpr double kSemMola = 1e-6;
  constexpr double kRaizDupla = 1e-9;

  if (std::fabs(s) < kSemMola) {
    // SEM MOLA. Com arrasto, a velocidade terminal aparece sozinha; sem
    // arrasto, e a queda livre de sempre.
    if (k < kSemMola) return u0 + v0 * t + 0.5 * g * t * t;
    const double e = (1.0 - std::exp(-k * t)) / k;
    return u0 + v0 * e + g * (t - e) / k;
  }

  const double particular = g / s;
  const double w = u0 - particular;
  const double disc = k * k - 4.0 * s;

  if (disc < -kRaizDupla) {
    // RAIZES COMPLEXAS: oscila e decai. O caso da atracao fraca.
    const double alfa = -k / 2.0;
    const double beta = std::sqrt(-disc) / 2.0;
    const double a = w;
    const double b = (v0 - alfa * w) / beta;
    return std::exp(alfa * t) * (a * std::cos(beta * t) + b * std::sin(beta * t)) +
           particular;
  }

  const double raiz = std::sqrt(std::max(0.0, disc));
  const double r1 = (-k + raiz) / 2.0;
  const double r2 = (-k - raiz) / 2.0;

  if (raiz < kRaizDupla) {
    // RAIZ DUPLA: o amortecimento critico. A solucao tem um `t` a mais.
    return (w + (v0 - r1 * w) * t) * std::exp(r1 * t) + particular;
  }

  const double a = (v0 - r2 * w) / (r1 - r2);
  const double b = w - a;
  return a * std::exp(r1 * t) + b * std::exp(r2 * t) + particular;
}

/// O TETO DE SEGURANCA. Uma repulsao forte somada a uma vida longa leva a
/// exponencial a estourar, e um `inf` numa instancia envenena o lote
/// inteiro (o pintor escreveria `nan` em cima da composicao). Nenhuma
/// nuvem util chega perto deste numero: 1e6 px e mil vezes a largura de
/// um 8K.
constexpr double kLimiteDoMundo = 1.0e6;

/// QUANTAS PARTICULAS O CAMPO TEM.
///
/// COM TAXA: quantas ficam vivas ao mesmo tempo (`taxa x vida`). Esse e o
/// numero que a pessoa ve na tela, e nao um total de nascimentos.
/// SEM TAXA: o teto, com as idades espalhadas — o regime de PRE-ROLL, em
/// que o campo ja nasce cheio.
[[nodiscard]] std::uint32_t quantas_vivas(const ParametrosDeParticulas& p)
    noexcept {
  const std::uint32_t teto = std::max<std::uint32_t>(1, p.maximo);
  if (p.taxa_de_nascimento <= 0.0F) return teto;
  const double vivas =
      static_cast<double>(p.taxa_de_nascimento) *
      std::max(0.05, static_cast<double>(p.vida_s));
  if (!(vivas > 0.0)) return teto;
  return std::min<std::uint32_t>(
      teto, static_cast<std::uint32_t>(std::min(vivas, 1.0e7)));
}

[[nodiscard]] std::uint32_t quantos_rastros(const ParametrosDeParticulas& p)
    noexcept {
  if (p.rastro <= 0.001F) return 0;
  return static_cast<std::uint32_t>(
      std::ceil(std::min(p.rastro, 1.0F) * 6.0F));
}

[[nodiscard]] std::uint32_t quantas_faiscas(const ParametrosDeParticulas& p)
    noexcept {
  return std::min<std::uint32_t>(p.faiscas, 24);
}

/// O FATOR DE TAMANHO ao longo da vida.
[[nodiscard]] double fator_de_tamanho(TamanhoNaVida modo, double u) noexcept {
  switch (modo) {
    case TamanhoNaVida::cresce:
      return 0.15 + 0.85 * u;
    case TamanhoNaVida::encolhe:
      return 1.0 - 0.85 * u;
    case TamanhoNaVida::sobeEDesce:
      return 0.15 + 0.85 * std::sin(u * 3.14159265358979323846);
    case TamanhoNaVida::fixo:
      break;
  }
  return 1.0;
}

/// O ALFA ao longo da vida. `entraESai` e a curva de sempre: entra em 8%
/// da vida e sai nos ultimos 35%.
[[nodiscard]] double alfa_na_vida(OpacidadeNaVida modo, double u) noexcept {
  switch (modo) {
    case OpacidadeNaVida::some:
      return (u < 0.04 ? u / 0.04 : 1.0) * (1.0 - u);
    case OpacidadeNaVida::aparece:
      return u;
    case OpacidadeNaVida::fixa:
      return 1.0;
    case OpacidadeNaVida::entraESai:
      break;
  }
  return (u / 0.08 < 1.0 ? u / 0.08 : 1.0) *
         ((1.0 - u) / 0.35 < 1.0 ? (1.0 - u) / 0.35 : 1.0);
}

[[nodiscard]] float canal(std::uint32_t cor, int deslocamento) noexcept {
  return static_cast<float>((cor >> deslocamento) & 0xFFU) / 255.0F;
}

}  // namespace

// ==================== O LOTE ====================

std::size_t tamanho_do_lote(const ParametrosDeParticulas& p) noexcept {
  const std::size_t base = quantas_vivas(p);
  const std::size_t porParticula = 1U + quantos_rastros(p) + quantas_faiscas(p);
  return base * porParticula;
}

std::vector<InstanciaDeParticula> reservar_lote(
    const ParametrosDeParticulas& p) {
  return std::vector<InstanciaDeParticula>(tamanho_do_lote(p));
}

std::uint32_t teto_de_particulas(std::uint32_t nivel,
                                 std::uint32_t pedido) noexcept {
  // O NIVEL VEM DA QUALIDADE ADAPTATIVA (0 = minimo, 3 = cheio). O TETO E
  // UM TETO: um campo de cem particulas continua com cem em qualquer
  // nivel — o que cai e o que passou do orcamento.
  static constexpr std::uint32_t kTeto[] = {256U, 640U, 1536U, 4096U};
  const std::uint32_t n = std::min<std::uint32_t>(nivel, 3U);
  return std::min(pedido, kTeto[n]);
}

// ==================== A SIMULACAO ====================

std::size_t MotorDeParticulas::gerar(
    const ParametrosDeParticulas& p, double tempo_s,
    std::span<InstanciaDeParticula> saida) const {
  quadros_ += 1;
  ultimo_ = 0;
  if (saida.empty()) return 0;

  const std::size_t n = std::min<std::size_t>(quantas_vivas(p), saida.size());
  const std::uint32_t nRastros = quantos_rastros(p);
  const std::uint32_t nFaiscas = quantas_faiscas(p);

  const double vida = std::max(0.05, static_cast<double>(p.vida_s));
  const double vidaFaisca = std::max(0.05, static_cast<double>(p.faisca_vida_s));
  const double k = std::max(0.0, static_cast<double>(p.arrasto));
  const double s = static_cast<double>(p.atracao);
  const double turb = std::max(0.0, static_cast<double>(p.turbulencia));
  const double turbF = 1.0 / std::max(8.0, static_cast<double>(p.turbulencia_escala));
  const double turbT = tempo_s * static_cast<double>(p.turbulencia_velocidade) * 0.35;
  const double rastroGap = 0.05 * (0.5 + std::min(1.0, static_cast<double>(p.rastro)));
  const bool semRastro = nRastros == 0;

  const double rx = -static_cast<double>(p.rotacao_x_graus) * kPi / 180.0;
  const double ry = -static_cast<double>(p.rotacao_y_graus) * kPi / 180.0;
  const double rz = static_cast<double>(p.rotacao_z_graus) * kPi / 180.0;
  const double cxr = std::cos(rx), sxr = std::sin(rx);
  const double cyr = std::cos(ry), syr = std::sin(ry);
  const double czr = std::cos(rz), szr = std::sin(rz);

  const double focal = (std::isfinite(p.focal) && p.focal > 60.0F)
                           ? static_cast<double>(p.focal)
                           : 1200.0;

  const float cr0 = canal(p.cor_inicio, 24), cg0 = canal(p.cor_inicio, 16),
              cb0 = canal(p.cor_inicio, 8);
  const float cr1 = canal(p.cor_fim, 24), cg1 = canal(p.cor_fim, 16),
              cb1 = canal(p.cor_fim, 8);

  const double gravidadeY = static_cast<double>(p.gravidade);
  const double ventoX = static_cast<double>(p.vento_x);
  const double ventoY = static_cast<double>(p.vento_y);
  const double ventoZ = static_cast<double>(p.vento_z);

  const std::uint32_t semente = p.semente;
  std::size_t escritas = 0;

  /// A TRAJETORIA de um nascimento e uma velocidade quaisquer. A particula
  /// usa a dela; a faisca usa a sua, nascida do pai. Uma funcao so, para os
  /// dois nao divergirem.
  const auto trajetoria = [&](double ox, double oy, double oz, double ux,
                              double uy, double uz, double a, double* saida3) {
    double px = deslocamento(ox - p.atracao_x, ux, 0.0, k, s, a) + p.atracao_x +
                ventoX * a;
    double py = deslocamento(oy - p.atracao_y, uy, gravidadeY, k, s, a) +
                p.atracao_y + ventoY * a;
    double pz = deslocamento(oz - p.atracao_z, uz, 0.0, k, s, a) + p.atracao_z +
                ventoZ * a;
    if (turb > 0.0) {
      // RAMPA DE ENTRADA: sem ela a particula nasceria ja empurrada, e o
      // emissor ficaria com um borrao em volta em vez de um ponto.
      const double rampa = prender_f(static_cast<float>(a / 0.6), 0.0F, 1.0F) *
                           static_cast<double>(turb);
      const double nx = px * turbF, ny = py * turbF, nz = pz * turbF + turbT;
      px += (ruido3(nx, ny, nz, semente) - 0.5) * 2.0 * rampa;
      py += (ruido3(nx + 31.7, ny, nz, semente + 1U) - 0.5) * 2.0 * rampa;
      pz += (ruido3(nx, ny + 47.3, nz, semente + 2U) - 0.5) * 2.0 * rampa;
    }
    px = prender_f(static_cast<float>(px), -1.0e6F, 1.0e6F);
    py = prender_f(static_cast<float>(py), -1.0e6F, 1.0e6F);
    pz = prender_f(static_cast<float>(pz), -1.0e6F, 1.0e6F);
    saida3[0] = px;
    saida3[1] = py;
    saida3[2] = pz;
  };

  const auto projetar = [&](const double* m, float* destino) {
    const double y1 = m[1] * cxr - m[2] * sxr;
    const double z1 = m[1] * sxr + m[2] * cxr;
    const double x1 = m[0] * cyr + z1 * syr;
    const double z2 = -m[0] * syr + z1 * cyr;
    const double wx = x1 * czr - y1 * szr;
    const double wy = x1 * szr + y1 * czr;
    if (focal + z2 < 60.0) return false;
    const double proj =
        prender_f(static_cast<float>(focal / (focal + z2)), 0.02F, 6.0F);
    destino[0] = static_cast<float>(p.centro_x + wx * proj);
    destino[1] = static_cast<float>(p.centro_y + wy * proj);
    destino[2] = static_cast<float>(proj);
    destino[3] = static_cast<float>(z2);
    return true;
  };

  for (std::size_t i = 0; i < n; ++i) {
    const std::uint32_t ii = static_cast<std::uint32_t>(i);

    // VIDA PROPRIA: parte das particulas vive menos (Life Random).
    const double vidaI =
        vida * (1.0 - prender_f(p.vida_variacao, 0.0F, 1.0F) *
                           aleatorio_d(semente, ii, 14U) * 0.8);

    // A IDADE. O modulo POSITIVO e o pre-roll: o sistema ja rodava antes do
    // quadro zero, e cada particula esta na fase em que estaria.
    const double nascimento = p.taxa_de_nascimento > 0.0F
                                  ? (static_cast<double>(i) +
                                     aleatorio_d(semente, ii, 0U)) /
                                        static_cast<double>(p.taxa_de_nascimento)
                                  : aleatorio_d(semente, ii, 0U) * vidaI;
    double age = std::fmod(tempo_s - nascimento, vidaI);
    if (age < 0.0) age += vidaI;

    // ---- NASCIMENTO ----
    double bx = 0.0, by = 0.0, bz = 0.0;
    switch (p.emissor) {
      case EmissorDeParticulas::ponto:
        break;
      case EmissorDeParticulas::esfera: {
        const double cz = 2.0 * aleatorio_d(semente, ii, 15U) - 1.0;
        const double ph = 2.0 * kPi * aleatorio_d(semente, ii, 16U);
        const double rr = std::sqrt(std::max(0.0, 1.0 - cz * cz));
        const double rad = static_cast<double>(p.raio) *
                           std::cbrt(aleatorio_d(semente, ii, 19U));
        bx = rr * std::cos(ph) * rad;
        by = rr * std::sin(ph) * rad;
        bz = cz * rad;
        break;
      }
      case EmissorDeParticulas::linha: {
        const double t = aleatorio_d(semente, ii, 17U) - 0.5;
        const double len = std::sqrt(static_cast<double>(p.linha_x) * p.linha_x +
                                     static_cast<double>(p.linha_y) * p.linha_y +
                                     static_cast<double>(p.linha_z) * p.linha_z);
        const double e = len < 1e-6 ? 1.0 : static_cast<double>(p.raio);
        bx = static_cast<double>(p.linha_x) / (len < 1e-6 ? 1.0 : len) * e * t;
        by = static_cast<double>(p.linha_y) / (len < 1e-6 ? 1.0 : len) * e * t;
        bz = static_cast<double>(p.linha_z) / (len < 1e-6 ? 1.0 : len) * e * t;
        break;
      }
      case EmissorDeParticulas::anel: {
        const double ang = 2.0 * kPi * aleatorio_d(semente, ii, 18U);
        bx = std::cos(ang) * static_cast<double>(p.raio);
        by = std::sin(ang) * static_cast<double>(p.raio);
        bz = (aleatorio_d(semente, ii, 3U) - 0.5) * p.profundidade;
        break;
      }
      case EmissorDeParticulas::caixa:
        bx = (aleatorio_d(semente, ii, 6U) - 0.5) * p.largura;
        by = (aleatorio_d(semente, ii, 7U) - 0.5) * p.altura;
        bz = (aleatorio_d(semente, ii, 3U) - 0.5) * p.profundidade;
        break;
    }

    // ---- VELOCIDADE INICIAL ----
    const double v0 = static_cast<double>(p.velocidade) *
                      (0.5 + aleatorio_d(semente, ii, 2U));
    double vx = 0.0, vy = 0.0, vz = 0.0;
    switch (p.modo_de_emissao) {
      case ModoDeEmissao::esfera: {
        const double cz = 2.0 * aleatorio_d(semente, ii, 15U) - 1.0;
        const double ph = 2.0 * kPi * aleatorio_d(semente, ii, 16U);
        const double rr = std::sqrt(std::max(0.0, 1.0 - cz * cz));
        vx = rr * std::cos(ph) * v0;
        vy = rr * std::sin(ph) * v0;
        vz = cz * v0;
        break;
      }
      case ModoDeEmissao::radial: {
        const double len = std::sqrt(bx * bx + by * by + bz * bz);
        if (len < 1e-3) {
          const double cz = 2.0 * aleatorio_d(semente, ii, 15U) - 1.0;
          const double ph = 2.0 * kPi * aleatorio_d(semente, ii, 16U);
          const double rr = std::sqrt(std::max(0.0, 1.0 - cz * cz));
          vx = rr * std::cos(ph) * v0;
          vy = rr * std::sin(ph) * v0;
          vz = cz * v0;
        } else {
          vx = bx / len * v0;
          vy = by / len * v0;
          vz = bz / len * v0;
        }
        break;
      }
      case ModoDeEmissao::cone: {
        const double dir =
            (static_cast<double>(p.direcao_graus) +
             (aleatorio_d(semente, ii, 1U) - 0.5) *
                 static_cast<double>(p.abertura_graus)) *
            kPi / 180.0;
        vx = std::cos(dir) * v0;
        vy = std::sin(dir) * v0;
        vz = (aleatorio_d(semente, ii, 10U) - 0.5) *
             static_cast<double>(p.velocidade);
        break;
      }
    }

    /// PROJETA UMA IDADE E ESCREVE UMA INSTANCIA.
    const auto escrever = [&](double a, double alfaExtra, double raioExtra,
                              std::uint32_t canalVariacao,
                              std::uint32_t canalAngulo,
                              bool comCauda, double idadeDaCauda,
                              std::uint32_t canalTurb) {
      if (escritas >= saida.size()) return;
      double m[3];
      trajetoria(bx, by, bz, vx, vy, vz, a, m);
      float pr[4];
      if (!projetar(m, pr)) return;
      const double uu = prender_f(static_cast<float>(a / vidaI), 0.0F, 1.0F);
      (void)canalTurb;

      double alfa = alfa_na_vida(p.opacidade_na_vida, uu);
      alfa *= 1.0 - prender_f(p.opacidade_variacao, 0.0F, 1.0F) *
                        aleatorio_d(semente, ii, 12U);
      if (p.cintilar) {
        const double tw =
            0.5 + 0.5 * std::sin((tempo_s * (0.7 + aleatorio_d(semente, ii, 8U) * 1.5) +
                                  aleatorio_d(semente, ii, 9U)) * 2.0 * kPi);
        alfa *= 0.30 + 0.70 * tw;
      }
      alfa *= prender_f(p.opacidade, 0.0F, 1.0F) * alfaExtra;
      if (alfa <= 0.004) return;

      const double vida3 =
          fator_de_tamanho(p.tamanho_na_vida, uu) * raioExtra;
      const double espalha =
          1.0 + (aleatorio_d(semente, ii, 4U) - 0.5) *
                    static_cast<double>(p.tamanho_variacao) * 1.8;
      const double raio = static_cast<double>(p.tamanho) * espalha * vida3 *
                          pr[2] * 0.5;
      if (raio < 0.35) return;

      InstanciaDeParticula& inst = saida[escritas];
      inst = InstanciaDeParticula{};
      inst.x = pr[0];
      inst.y = pr[1];
      inst.tamanho = static_cast<float>(raio);
      inst.angulo = static_cast<float>(
          (static_cast<double>(p.giro_graus_s) * a +
           aleatorio_d(semente, ii, canalAngulo) * 360.0) *
          kPi / 180.0);
      // A COR: a final so entra quando ela foi pedida. Uma cor fixa com
      // interpolacao ligada daria o dobro do trabalho por zero de imagem.
      const float t = p.tem_cor_fim ? static_cast<float>(uu) : 0.0F;
      const float cc = misturar(cr0, cr1, t);
      const float cg = misturar(cg0, cg1, t);
      const float cb = misturar(cb0, cb1, t);
      const float a8 = prender_f(static_cast<float>(alfa), 0.0F, 1.0F);
      inst.r = cc;
      inst.g = cg;
      inst.b = cb;
      inst.a = a8;
      inst.profundidade = pr[3];
      inst.forma = static_cast<float>(p.forma);
      inst.u = static_cast<float>(uu);
      inst.brilho = prender_f(p.brilho, 0.0F, 1.0F);
      inst.variacao = aleatorio(semente, ii, canalVariacao);

      if (comCauda) {
        double atras[3];
        trajetoria(bx, by, bz, vx, vy, vz, idadeDaCauda, atras);
        float pr2[4];
        if (projetar(atras, pr2)) {
          inst.cauda_x = pr2[0];
          inst.cauda_y = pr2[1];
        } else {
          inst.cauda_x = pr[0];
          inst.cauda_y = pr[1];
        }
      } else {
        inst.cauda_x = pr[0];
        inst.cauda_y = pr[1];
      }
      ++escritas;
    };

    escrever(age, 1.0, 1.0, 5U, 13U, true, std::max(0.0, age - 0.045), 0U);

    // RASTRO: amostras da MESMA trajetoria em idades anteriores.
    if (!semRastro) {
      for (std::uint32_t g = 1; g <= nRastros; ++g) {
        const double a = age - static_cast<double>(g) * rastroGap;
        if (a < 0.0) break;
        const double f =
            1.0 - static_cast<double>(g) / static_cast<double>(nRastros + 1);
        escrever(a, 0.6 * f, 0.5 + 0.5 * f, 5U, 13U, false, 0.0, 0U);
      }
    }

    // AS FAISCAS: cada particula solta as suas ao longo do caminho.
    if (nFaiscas > 0) {
      const double inicioAux =
          vidaI * static_cast<double>(prender_f(p.faisca_inicio, 0.0F, 0.95F));
      for (std::uint32_t f = 0; f < nFaiscas; ++f) {
        const double fatia =
            (static_cast<double>(f) + aleatorio_d(semente, ii, 40U + f)) /
            static_cast<double>(nFaiscas);
        const double ta = inicioAux + (vidaI - inicioAux) * fatia;
        const double idade = age - ta;
        if (idade < 0.0 || idade > vidaFaisca) continue;

        double n0[3];
        trajetoria(bx, by, bz, vx, vy, vz, ta, n0);
        constexpr double h = 1.0 / 90.0;
        double n1[3];
        trajetoria(bx, by, bz, vx, vy, vz, ta + h, n1);
        const double heranca = static_cast<double>(p.faisca_heranca);
        const double hx = (n1[0] - n0[0]) / h * heranca;
        const double hy = (n1[1] - n0[1]) / h * heranca;
        const double hz = (n1[2] - n0[2]) / h * heranca;

        const double cz = 2.0 * aleatorio_d(semente, ii, 60U + f) - 1.0;
        const double ph = 2.0 * kPi * aleatorio_d(semente, ii, 80U + f);
        const double rr = std::sqrt(std::max(0.0, 1.0 - cz * cz));
        const double vv = static_cast<double>(p.faisca_velocidade) *
                          (0.4 + 0.6 * aleatorio_d(semente, ii, 100U + f));

        // A FAISCA TEM NASCIMENTO PROPRIO: por isso ela nao usa o
        // `escrever` da particula, e sim uma trajetoria com a origem e a
        // velocidade dela.
        if (escritas >= saida.size()) break;
        const double uf = prender_f(static_cast<float>(idade / vidaFaisca),
                                    0.0F, 1.0F);
        const double alfaF =
            prender_f(static_cast<float>(uf / 0.12), 0.0F, 1.0F) *
            (1.0 - uf) * (1.0 - uf) * prender_f(p.opacidade, 0.0F, 1.0F);
        if (alfaF <= 0.004) continue;

        double mf[3];
        trajetoria(n0[0], n0[1], n0[2], hx + rr * std::cos(ph) * vv,
                   hy + rr * std::sin(ph) * vv, hz + cz * vv, idade, mf);
        float prf[4];
        if (!projetar(mf, prf)) continue;
        const double raioF = static_cast<double>(p.tamanho) *
                             static_cast<double>(p.faisca_tamanho) *
                             (1.0 - 0.5 * uf) * prf[2] * 0.5;
        if (raioF < 0.35) continue;

        InstanciaDeParticula& inst = saida[escritas];
        inst = InstanciaDeParticula{};
        inst.x = prf[0];
        inst.y = prf[1];
        inst.tamanho = static_cast<float>(raioF);
        inst.angulo = static_cast<float>(
            (static_cast<double>(p.giro_graus_s) * idade +
             aleatorio_d(semente, ii, 140U + f) * 360.0) *
            kPi / 180.0);
        const float t = p.tem_cor_fim ? static_cast<float>(uf) : 0.0F;
        inst.r = misturar(cr0, cr1, t);
        inst.g = misturar(cg0, cg1, t);
        inst.b = misturar(cb0, cb1, t);
        inst.a = prender_f(static_cast<float>(alfaF), 0.0F, 1.0F);
        inst.profundidade = prf[3];
        inst.forma = static_cast<float>(p.forma);
        inst.u = static_cast<float>(uf);
        inst.brilho = prender_f(p.brilho, 0.0F, 1.0F);
        inst.variacao = aleatorio(semente, ii, 120U + f);
        inst.cauda_x = prf[0];
        inst.cauda_y = prf[1];
        ++escritas;
      }
    }
  }

  ultimo_ = escritas;
  return escritas;
}

// ==================== O RASTERIZADOR DE REFERENCIA ====================

namespace {

/// A MASCARA DA FORMA, em coordenadas locais normalizadas pelo raio.
/// Devolve (cobertura, quanto de branco) — o branco esta na ponta da
/// estrela, que e o nucleo claro que da o brilho de lente.
struct Mascara {
  float cobertura = 0.0F;
  float branco = 0.0F;
};

[[nodiscard]] float disco(float d, float raio) noexcept {
  // ANTISSERRILHADO ANALITICO: um pixel e a media da cobertura na sua
  // area. A largura da transicao e meio pixel, que e o que o olho espera.
  if (raio <= 0.0F) return 0.0F;
  const float borda = 0.5F;
  return prender_f((raio + borda - d) / (2.0F * borda), 0.0F, 1.0F);
}

[[nodiscard]] float losango(float lx, float ly, float meiaH, float meiaV)
    noexcept {
  // |x|/a + |y|/b <= 1, com borda de meio pixel no eixo maior.
  if (meiaH <= 0.0F || meiaV <= 0.0F) return 0.0F;
  const float d = std::fabs(lx) / meiaH + std::fabs(ly) / meiaV;
  const float escala = std::min(meiaH, meiaV);
  const float borda = 0.5F / std::max(1.0F, escala);
  return prender_f((1.0F + borda - d) / (2.0F * borda), 0.0F, 1.0F);
}

/// A COBERTURA DA INSTANCIA NUM PONTO (px, py) DA COMPOSICAO, com o
/// espalhamento [alcance] do halo.
[[nodiscard]] Mascara mascara_da(const InstanciaDeParticula& in, float px,
                                 float py, float folga) noexcept {
  const float r = in.tamanho;
  if (r <= 0.0F) return {};
  const float dx = px - in.x;
  const float dy = py - in.y;
  const auto forma = static_cast<FormaDaParticula>(
      static_cast<std::uint32_t>(in.forma + 0.5F));

  switch (forma) {
    case FormaDaParticula::esfera:
      return {disco(std::sqrt(dx * dx + dy * dy), r), 0.0F};

    case FormaDaParticula::estrela: {
      // A CRUZ DE QUATRO PONTAS, girada pelo angulo proprio. Desfazer o
      // giro e mais barato do que girar o ponto de teste duas vezes.
      const float a = -in.angulo * kPi / 180.0F;
      const float ca = std::cos(a), sa = std::sin(a);
      const float lx = dx * ca - dy * sa;
      const float ly = dx * sa + dy * ca;
      const float len = r * (2.4F + in.variacao * 1.6F);
      const float lenH = len * 0.72F;
      const float w = r * 0.40F;
      float cob = losango(lx, ly, w, len);
      cob = std::max(cob, losango(lx, ly, lenH, w));
      const float nucleo = disco(std::sqrt(lx * lx + ly * ly), w * 0.95F);
      return {std::max(cob, nucleo), nucleo};
    }

    case FormaDaParticula::risco: {
      // O RISCO VAI DO PONTO ATE A CAUDA — a direcao do movimento.
      const float cx0 = in.cauda_x, cy0 = in.cauda_y;
      const float rx0 = in.x - cx0, ry0 = in.y - cy0;
      const float len = std::sqrt(rx0 * rx0 + ry0 * ry0);
      float ax = in.x, ay = in.y - r * 2.0F;
      if (len >= 0.5F) {
        const float escala = std::max(r * 2.5F, len * 3.0F) / len;
        ax = in.x - rx0 * escala;
        ay = in.y - ry0 * escala;
      }
      const float sx = ax - in.x, sy = ay - in.y;
      const float l2 = sx * sx + sy * sy;
      float t = l2 > 1e-6F ? ((px - in.x) * sx + (py - in.y) * sy) / l2 : 0.0F;
      t = prender_f(t, 0.0F, 1.0F);
      const float qx = in.x + sx * t, qy = in.y + sy * t;
      const float meia = std::max(0.4F, r * 0.45F);
      const float cobLinha = disco(std::sqrt((px - qx) * (px - qx) +
                                              (py - qy) * (py - qy)),
                                   meia);
      const float cobPonta = disco(std::sqrt(dx * dx + dy * dy), r * 0.55F);
      return {std::max(cobLinha, cobPonta), 0.0F};
    }

    case FormaDaParticula::nuvem: {
      const float d = std::sqrt(dx * dx + dy * dy);
      const float c1 = disco(d, r * 2.6F) * 0.10F;
      const float c2 = disco(d, r * 1.8F) * 0.16F;
      const float c3 = disco(d, r * 1.1F) * 0.28F;
      return {prender_f(c1 + c2 + c3, 0.0F, 1.0F), 0.0F};
    }

    case FormaDaParticula::quadrado: {
      const float a = -in.angulo * kPi / 180.0F;
      const float ca = std::cos(a), sa = std::sin(a);
      const float lx = std::fabs(dx * ca - dy * sa);
      const float ly = std::fabs(dx * sa + dy * ca);
      const float m = std::max(lx, ly);
      return {prender_f((r + 0.5F - m), 0.0F, 1.0F), 0.0F};
    }

    case FormaDaParticula::anel: {
      const float d = std::sqrt(dx * dx + dy * dy);
      const float meia = std::max(0.4F, r * 0.175F);
      const float fora = disco(d, r + meia);
      const float dentro = disco(d, r - meia);
      return {prender_f(fora - dentro, 0.0F, 1.0F), 0.0F};
    }
  }
  (void)folga;
  return {};
}

}  // namespace

ResultadoDoPintor pintar_particulas(
    CargaDeAlvo& alvo, std::span<const InstanciaDeParticula> lote,
    float opacidade_da_camada) {
  ResultadoDoPintor r{};
  if (alvo.largura == 0 || alvo.altura == 0) return r;
  if (alvo.pixels.size() <
      static_cast<std::size_t>(alvo.largura) * alvo.altura * 4) {
    return r;
  }
  if (lote.empty()) return r;

  // A ORDEM: LONGE PRIMEIRO. Perto cobre longe, como no espaco.
  //
  // A ORDENACAO E SOBRE INDICES, e nao sobre as instancias: 4 bytes contra
  // 60, e a mesma ideia que faz o desenho na GPU mandar o indice no
  // vertice em vez de reordenar os vertices. O vetor vive entre chamadas
  // (thread_local) para nao alocar a cada quadro — o caminho quente nao
  // paga por uma ordenacao.
  static thread_local std::vector<std::uint32_t> ordem;
  ordem.resize(lote.size());
  for (std::size_t i = 0; i < lote.size(); ++i) {
    ordem[i] = static_cast<std::uint32_t>(i);
  }
  std::sort(ordem.begin(), ordem.end(),
            [&](std::uint32_t a, std::uint32_t b) {
              const float za = lote[a].profundidade;
              const float zb = lote[b].profundidade;
              if (za != zb) return za > zb;
              return a < b;  // desempate estavel: a nuvem nao pisca
            });

  const float opac = prender_f(opacidade_da_camada, 0.0F, 1.0F);

  for (const std::uint32_t idx : ordem) {
    const InstanciaDeParticula& in = lote[idx];
    if (!(in.tamanho > 0.0F) || !(in.a > 0.0F)) continue;
    if (!std::isfinite(in.x) || !std::isfinite(in.y)) continue;

    const auto forma = static_cast<FormaDaParticula>(
        static_cast<std::uint32_t>(in.forma + 0.5F));
    // A CAIXA QUE A INSTANCIA PODE TOCAR. Cada forma tem o seu alcance, e
    // o halo entra por cima: uma caixa justa demais corta a estrela no
    // meio e a nuvem na borda.
    float alcance = 2.0F;
    switch (forma) {
      case FormaDaParticula::estrela:
        alcance = 2.4F + in.variacao * 1.6F;
        break;
      case FormaDaParticula::nuvem:
        alcance = 2.6F;
        break;
      case FormaDaParticula::risco: {
        const float rx0 = in.x - in.cauda_x, ry0 = in.y - in.cauda_y;
        const float len = std::sqrt(rx0 * rx0 + ry0 * ry0);
        alcance = len < 0.5F ? 2.0F : std::max(2.5F, len * 3.0F / in.tamanho);
        break;
      }
      case FormaDaParticula::anel:
        alcance = 1.2F;
        break;
      case FormaDaParticula::quadrado:
        alcance = 1.5F;
        break;
      case FormaDaParticula::esfera:
        alcance = 1.1F;
        break;
    }
    const float haloAlcance = 1.8F + in.brilho * 1.5F;
    const float raio = in.tamanho * std::max(alcance, haloAlcance) + 2.0F;

    const int x0 = std::max(0, static_cast<int>(std::floor(in.x - raio)));
    const int y0 = std::max(0, static_cast<int>(std::floor(in.y - raio)));
    const int x1 = std::min(static_cast<int>(alvo.largura) - 1,
                            static_cast<int>(std::ceil(in.x + raio)));
    const int y1 = std::min(static_cast<int>(alvo.altura) - 1,
                            static_cast<int>(std::ceil(in.y + raio)));
    if (x1 < x0 || y1 < y0) {
      ++r.instancias_fora;
      continue;
    }

    const float vidro = prender_f(in.brilho, 0.0F, 1.0F);
    for (int py = y0; py <= y1; ++py) {
      for (int px = x0; px <= x1; ++px) {
        const float fx = static_cast<float>(px) + 0.5F;
        const float fy = static_cast<float>(py) + 0.5F;
        const Mascara m = mascara_da(in, fx, fy, raio);

        float cob = m.cobertura;
        if (vidro > 0.001F) {
          const float dx = fx - in.x, dy = fy - in.y;
          const float d = std::sqrt(dx * dx + dy * dy);
          const float dHalo = disco(d, in.tamanho * (1.8F + vidro * 1.5F));
          cob = prender_f(cob + dHalo * 0.9F * vidro, 0.0F, 1.0F);
        }
        if (cob <= 0.0015F) continue;

        const float a = in.a * cob * opac;
        if (a <= 0.0015F) continue;

        // A PONTA DA ESTRELA E BRANCA. Misturar aqui, e nao em duas
        // passadas, mantem UMA escrita por pixel.
        const float cr = misturar(in.r, 1.0F, m.branco);
        const float cg = misturar(in.g, 1.0F, m.branco);
        const float cb = misturar(in.b, 1.0F, m.branco);

        const std::size_t i =
            (static_cast<std::size_t>(py) * alvo.largura +
             static_cast<std::size_t>(px)) * 4;
        const float ab = static_cast<float>(alvo.pixels[i + 3]) / 255.0F;
        const float fora = 1.0F - ab;
        // PREMULTIPLICADO, srcOver: a fonte entra multiplicada pelo
        // proprio alfa e o fundo pelo que sobrou.
        alvo.pixels[i + 0] = static_cast<std::uint8_t>(
            prender_f(static_cast<float>(alvo.pixels[i + 0]) / 255.0F * fora +
                          cr * a, 0.0F, 1.0F) * 255.0F + 0.5F);
        alvo.pixels[i + 1] = static_cast<std::uint8_t>(
            prender_f(static_cast<float>(alvo.pixels[i + 1]) / 255.0F * fora +
                          cg * a, 0.0F, 1.0F) * 255.0F + 0.5F);
        alvo.pixels[i + 2] = static_cast<std::uint8_t>(
            prender_f(static_cast<float>(alvo.pixels[i + 2]) / 255.0F * fora +
                          cb * a, 0.0F, 1.0F) * 255.0F + 0.5F);
        alvo.pixels[i + 3] = static_cast<std::uint8_t>(
            prender_f(ab + a * fora, 0.0F, 1.0F) * 255.0F + 0.5F);
        ++r.pixels_tocados;
      }
    }
    ++r.instancias_pintadas;
  }

  return r;
}

// ==================== OS PRESETS ====================

namespace {
struct ReceitaDePreset {
  const char* nome;
  void (*aplicar)(ParametrosDeParticulas&);
};

void base_fogo(ParametrosDeParticulas& p) {
  p.emissor = EmissorDeParticulas::caixa;
  p.largura = 90.0F;
  p.altura = 40.0F;
  p.profundidade = 120.0F;
  p.taxa_de_nascimento = 140.0F;
  p.vida_s = 1.5F;
  p.vida_variacao = 0.5F;
  p.velocidade = 70.0F;
  p.direcao_graus = -90.0F;
  p.abertura_graus = 50.0F;
  p.modo_de_emissao = ModoDeEmissao::cone;
  p.gravidade = -30.0F;
  p.arrasto = 1.2F;
  p.turbulencia = 26.0F;
  p.turbulencia_escala = 90.0F;
  p.turbulencia_velocidade = 2.0F;
  p.atracao = 0.0F;
  p.tamanho = 48.0F;
  p.tamanho_variacao = 0.6F;
  p.tamanho_na_vida = TamanhoNaVida::encolhe;
  p.opacidade_na_vida = OpacidadeNaVida::entraESai;
  p.opacidade_variacao = 0.2F;
  p.cor_inicio = 0xFFF2A03DU;
  p.cor_fim = 0xFF6E1F12U;
  p.tem_cor_fim = true;
  p.forma = FormaDaParticula::nuvem;
  p.brilho = 0.45F;
  p.maximo = 700U;
}
}  // namespace

const char* nome_do_preset(PresetDeParticulas p) noexcept {
  switch (p) {
    case PresetDeParticulas::fogo:
      return "Fogo";
    case PresetDeParticulas::faiscas:
      return "Faiscas";
    case PresetDeParticulas::neve:
      return "Neve";
    case PresetDeParticulas::chuva:
      return "Chuva";
    case PresetDeParticulas::estrelas:
      return "Estrelas";
    case PresetDeParticulas::fumaca:
      return "Fumaca";
    case PresetDeParticulas::magia:
      return "Magia";
    case PresetDeParticulas::confete:
      return "Confete";
    case PresetDeParticulas::poeira:
      return "Poeira";
    case PresetDeParticulas::explosao:
      return "Explosao";
  }
  return "Particulas";
}

void aplicar_preset(PresetDeParticulas preset,
                    ParametrosDeParticulas& p) noexcept {
  // O QUE O PRESET NAO TOCA E DE QUEM CHAMOU: a lente, o centro do
  // emissor, as rotacoes e a semente. Trocar de receita nao pode jogar a
  // nuvem para outro lugar da composicao nem mudar a camera.
  const float cx = p.centro_x, cy = p.centro_y, cz = p.centro_z;
  const float focal = p.focal;
  const float rx = p.rotacao_x_graus, ry = p.rotacao_y_graus,
              rz = p.rotacao_z_graus;
  const std::uint32_t semente = p.semente;
  const std::uint32_t maximo = p.maximo;

  p = ParametrosDeParticulas{};
  p.centro_x = cx;
  p.centro_y = cy;
  p.centro_z = cz;
  p.focal = focal;
  p.rotacao_x_graus = rx;
  p.rotacao_y_graus = ry;
  p.rotacao_z_graus = rz;
  p.semente = semente;
  p.maximo = maximo;

  switch (preset) {
    case PresetDeParticulas::fogo:
      base_fogo(p);
      break;
    case PresetDeParticulas::faiscas:
      base_fogo(p);
      p.forma = FormaDaParticula::estrela;
      p.brilho = 0.7F;
      p.tamanho = 22.0F;
      p.taxa_de_nascimento = 90.0F;
      p.faiscas = 3U;
      p.faisca_velocidade = 120.0F;
      p.cor_inicio = 0xFFFFE082U;
      p.cor_fim = 0xFFFF6D00U;
      break;
    case PresetDeParticulas::neve:
      p.emissor = EmissorDeParticulas::caixa;
      p.largura = 2200.0F;
      p.altura = 60.0F;
      p.profundidade = 900.0F;
      p.taxa_de_nascimento = 90.0F;
      p.vida_s = 8.0F;
      p.vida_variacao = 0.3F;
      p.velocidade = 22.0F;
      p.direcao_graus = 90.0F;
      p.abertura_graus = 24.0F;
      p.vento_x = 18.0F;
      p.arrasto = 0.2F;
      p.turbulencia = 30.0F;
      p.turbulencia_escala = 220.0F;
      p.tamanho = 16.0F;
      p.tamanho_variacao = 0.7F;
      p.opacidade_na_vida = OpacidadeNaVida::entraESai;
      p.cor_inicio = 0xFFFFFFFFU;
      p.forma = FormaDaParticula::esfera;
      p.brilho = 0.2F;
      p.maximo = 900U;
      break;
    case PresetDeParticulas::chuva:
      p.emissor = EmissorDeParticulas::caixa;
      p.largura = 2400.0F;
      p.altura = 40.0F;
      p.profundidade = 1000.0F;
      p.taxa_de_nascimento = 220.0F;
      p.vida_s = 3.0F;
      p.velocidade = 620.0F;
      p.direcao_graus = 90.0F;
      p.abertura_graus = 4.0F;
      p.vento_x = 90.0F;
      p.tamanho = 26.0F;
      p.tamanho_variacao = 0.5F;
      p.tamanho_na_vida = TamanhoNaVida::fixo;
      p.cor_inicio = 0x99CFE8FFU;
      p.forma = FormaDaParticula::risco;
      p.brilho = 0.0F;
      p.cintilar = false;
      p.maximo = 900U;
      break;
    case PresetDeParticulas::estrelas:
      p.emissor = EmissorDeParticulas::caixa;
      p.largura = 2400.0F;
      p.altura = 1400.0F;
      p.profundidade = 1800.0F;
      p.taxa_de_nascimento = 0.0F;
      p.vida_s = 6.0F;
      p.velocidade = 4.0F;
      p.modo_de_emissao = ModoDeEmissao::esfera;
      p.tamanho = 14.0F;
      p.tamanho_variacao = 0.8F;
      p.opacidade_na_vida = OpacidadeNaVida::fixa;
      p.opacidade_variacao = 0.5F;
      p.cor_inicio = 0xFFFFFFFFU;
      p.forma = FormaDaParticula::estrela;
      p.brilho = 0.35F;
      p.cintilar = true;
      p.maximo = 260U;
      break;
    case PresetDeParticulas::fumaca:
      base_fogo(p);
      p.forma = FormaDaParticula::nuvem;
      p.tamanho = 120.0F;
      p.tamanho_na_vida = TamanhoNaVida::cresce;
      p.velocidade = 30.0F;
      p.gravidade = -12.0F;
      p.turbulencia = 40.0F;
      p.cor_inicio = 0xAA9AA0A6U;
      p.cor_fim = 0x22333333U;
      p.brilho = 0.0F;
      p.opacidade = 0.55F;
      p.maximo = 400U;
      break;
    case PresetDeParticulas::magia:
      p.emissor = EmissorDeParticulas::esfera;
      p.raio = 160.0F;
      p.taxa_de_nascimento = 200.0F;
      p.vida_s = 1.8F;
      p.velocidade = 120.0F;
      p.modo_de_emissao = ModoDeEmissao::esfera;
      p.arrasto = 2.4F;
      p.atracao = 2.2F;
      p.atracao_x = 0.0F;
      p.atracao_y = 0.0F;
      p.atracao_z = 0.0F;
      p.tamanho = 20.0F;
      p.tamanho_na_vida = TamanhoNaVida::sobeEDesce;
      p.cor_inicio = 0xFF8CE9FFU;
      p.cor_fim = 0xFF7A3BFFU;
      p.tem_cor_fim = true;
      p.forma = FormaDaParticula::estrela;
      p.brilho = 0.6F;
      p.maximo = 800U;
      break;
    case PresetDeParticulas::confete:
      p.emissor = EmissorDeParticulas::linha;
      p.raio = 500.0F;
      p.linha_x = 1.0F;
      p.taxa_de_nascimento = 120.0F;
      p.vida_s = 4.0F;
      p.velocidade = 240.0F;
      p.direcao_graus = -90.0F;
      p.abertura_graus = 60.0F;
      p.gravidade = 420.0F;
      p.arrasto = 0.6F;
      p.tamanho = 22.0F;
      p.tamanho_variacao = 0.7F;
      p.tamanho_na_vida = TamanhoNaVida::fixo;
      p.cor_inicio = 0xFFFFD54FU;
      p.cor_fim = 0xFFE91E63U;
      p.tem_cor_fim = true;
      p.forma = FormaDaParticula::quadrado;
      p.giro_graus_s = 220.0F;
      p.brilho = 0.0F;
      p.maximo = 600U;
      break;
    case PresetDeParticulas::poeira:
      p.emissor = EmissorDeParticulas::caixa;
      p.largura = 2200.0F;
      p.altura = 1300.0F;
      p.profundidade = 1400.0F;
      p.taxa_de_nascimento = 0.0F;
      p.vida_s = 7.0F;
      p.velocidade = 10.0F;
      p.modo_de_emissao = ModoDeEmissao::esfera;
      p.vento_x = 12.0F;
      p.vento_y = -6.0F;
      p.turbulencia = 22.0F;
      p.turbulencia_escala = 320.0F;
      p.tamanho = 9.0F;
      p.tamanho_variacao = 0.9F;
      p.opacidade_na_vida = OpacidadeNaVida::entraESai;
      p.opacidade_variacao = 0.6F;
      p.cor_inicio = 0xFFFFF3D6U;
      p.forma = FormaDaParticula::esfera;
      p.brilho = 0.3F;
      p.maximo = 500U;
      break;
    case PresetDeParticulas::explosao:
      p.emissor = EmissorDeParticulas::ponto;
      p.taxa_de_nascimento = 0.0F;
      p.vida_s = 1.6F;
      p.vida_variacao = 0.6F;
      p.velocidade = 520.0F;
      p.modo_de_emissao = ModoDeEmissao::esfera;
      p.arrasto = 2.8F;
      p.gravidade = 180.0F;
      p.tamanho = 26.0F;
      p.tamanho_variacao = 0.8F;
      p.tamanho_na_vida = TamanhoNaVida::encolhe;
      p.cor_inicio = 0xFFFFF0B0U;
      p.cor_fim = 0xFFFF3B00U;
      p.tem_cor_fim = true;
      p.forma = FormaDaParticula::esfera;
      p.brilho = 0.8F;
      p.rastro = 0.6F;
      p.faiscas = 4U;
      p.faisca_velocidade = 260.0F;
      p.maximo = 900U;
      break;
  }
}

}  // namespace aurea::render
