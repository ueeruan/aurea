#include "relogio_do_quadro.h"

#include <algorithm>
#include <span>

namespace aurea::render {

namespace {
/// A JANELA E COPIADA ANTES DE ORDENAR. Ordenar o buffer circular no
/// lugar embaralharia a ordem dos quadros, e a mediana do INTERVALO
/// depende dela.
void percentis(std::span<const double> amostras, double& mediana,
               double& p95, double& maximo) noexcept {
  if (amostras.empty()) {
    mediana = p95 = maximo = 0.0;
    return;
  }
  std::array<double, kJanelaDeQuadros> copia{};
  const std::size_t n = std::min(amostras.size(), copia.size());
  std::copy_n(amostras.begin(), n, copia.begin());
  std::sort(copia.begin(), copia.begin() + static_cast<std::ptrdiff_t>(n));

  mediana = copia[n / 2];
  // O p95 pelo indice mais proximo: com 120 amostras, o sexto pior.
  const std::size_t i95 = (n * 95) / 100;
  p95 = copia[std::min(i95, n - 1)];
  maximo = copia[n - 1];
}
}  // namespace

RelogioDoQuadro::RelogioDoQuadro(double orcamento_ms,
                                 std::size_t janela) noexcept
    : orcamento_ms_(orcamento_ms > 0.0 ? orcamento_ms : kOrcamentoPadraoMs),
      janela_(std::clamp<std::size_t>(janela, 1, kJanelaDeQuadros)) {}

double RelogioDoQuadro::em_ms(Relogio::duration d) noexcept {
  return std::chrono::duration<double, std::milli>(d).count();
}

void RelogioDoQuadro::comecar_quadro() noexcept {
  inicio_do_quadro_ = Relogio::now();
}

void RelogioDoQuadro::terminar_quadro() noexcept {
  const auto agora = Relogio::now();
  cpu_ultimo_ms_ = em_ms(agora - inicio_do_quadro_);

  cpu_ms_[escritos_ % janela_] = cpu_ultimo_ms_;
  if (tem_anterior_) {
    intervalo_ms_[escritos_ % janela_] = em_ms(agora - fim_do_quadro_anterior_);
  } else {
    // O PRIMEIRO QUADRO NAO TEM INTERVALO. Registrar zero puxaria a
    // mediana para baixo e o placar comecaria mentindo.
    intervalo_ms_[escritos_ % janela_] =
        intervalo_ms_[(escritos_ + 1) % janela_];
  }
  ++escritos_;

  if (cpu_ultimo_ms_ > orcamento_ms_) ++atrasados_;
  ++quadros_;
  fim_do_quadro_anterior_ = agora;
  tem_anterior_ = true;
}

void RelogioDoQuadro::registrar_gpu(double ms) noexcept {
  gpu_ultimo_ms_ = ms > 0.0 ? ms : 0.0;
}

void RelogioDoQuadro::registrar_pulo() noexcept {
  ++pulados_;
}

bool RelogioDoQuadro::orcamento_estourado() const noexcept {
  return em_ms(Relogio::now() - inicio_do_quadro_) > orcamento_ms_;
}

double RelogioDoQuadro::folga_ms() const noexcept {
  return orcamento_ms_ - em_ms(Relogio::now() - inicio_do_quadro_);
}

EstatisticasDoQuadro RelogioDoQuadro::estatisticas() const noexcept {
  EstatisticasDoQuadro e;
  e.cpu_ultimo_ms = cpu_ultimo_ms_;
  e.gpu_ultimo_ms = gpu_ultimo_ms_;
  e.quadros = quadros_;
  e.quadros_atrasados = atrasados_;
  e.quadros_pulados = pulados_;
  e.quadros_na_janela = static_cast<std::uint32_t>(
      std::min<std::size_t>(escritos_, janela_));

  if (e.quadros_na_janela == 0) return e;

  // Ordena a copia. O caminho e frio (o HUD pede uma vez por segundo),
  // entao a copia de 960 bytes nao aparece no perfil.
  std::array<double, kJanelaDeQuadros> cpu_copia{};
  std::array<double, kJanelaDeQuadros> int_copia{};
  std::size_t n = 0;
  if (escritos_ <= janela_) {
    n = escritos_;
    std::copy_n(cpu_ms_.begin(), n, cpu_copia.begin());
    std::copy_n(intervalo_ms_.begin(), n, int_copia.begin());
  } else {
    // DEU A VOLTA: reordena do mais antigo para o mais novo.
    const std::size_t inicio = escritos_ % janela_;
    for (std::size_t i = 0; i < janela_; ++i) {
      const std::size_t k = (inicio + i) % janela_;
      cpu_copia[i] = cpu_ms_[k];
      int_copia[i] = intervalo_ms_[k];
    }
    n = janela_;
  }

  double mediana = 0.0, p95 = 0.0, maximo = 0.0;
  percentis({cpu_copia.data(), n}, mediana, p95, maximo);
  e.cpu_mediana_ms = mediana;
  e.cpu_p95_ms = p95;
  e.cpu_max_ms = maximo;

  double i_mediana = 0.0, i_p95 = 0.0, i_max = 0.0;
  percentis({int_copia.data(), n}, i_mediana, i_p95, i_max);
  e.intervalo_mediana_ms = i_mediana;

  // O FPS SAI DO INTERVALO, e nao do tempo de CPU: um quadro barato que
  // chega tarde ao olho continua sendo um quadro atrasado.
  if (i_mediana > 0.05) e.fps_efetivo = 1000.0 / i_mediana;

  return e;
}

void RelogioDoQuadro::reiniciar() noexcept {
  escritos_ = 0;
  tem_anterior_ = false;
  cpu_ultimo_ms_ = 0.0;
  gpu_ultimo_ms_ = 0.0;
  quadros_ = 0;
  atrasados_ = 0;
  pulados_ = 0;
  cpu_ms_.fill(0.0);
  intervalo_ms_.fill(0.0);
}

}  // namespace aurea::render
