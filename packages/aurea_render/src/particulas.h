// O MOTOR DE PARTICULAS — a simulacao e o lote de instancias.
//
// ================================ POR QUE AQUI =========================
// A nuvem de particulas do Aurea era desenhada por um pintor de Dart: a
// simulacao rodava na thread da UI, a cada quadro, junto com o resto da
// arvore de widgets. Num campo de mil e duzentas particulas isso e trabalho
// de milissegundos por quadro disputando com o gesto — e a exportacao
// repetia a MESMA conta num segundo lugar, com a chance de os dois
// discordarem. Uma simulacao so, em C++, serve preview e exportacao.
//
// ================================ O QUE ELE E ==========================
// SIMULACAO PURA. Cada particula e funcao de (semente, indice, tempo):
// nada acumula de um quadro para o outro. Arrastar o cabecote para tras
// devolve exatamente o mesmo quadro, e nao uma aproximacao — que e o que
// torna o scrub utilizavel (mesmo invariante do motor de texto).
//
// A TRAJETORIA TEM FORMA FECHADA, e nao integracao passo a passo. A forca
// constante (gravidade), o vento e a resistencia do ar saem da solucao
// analitica de `u'' + k u' + s u = g` — que cobre tambem a atracao e a
// repulsao (s positivo e negativo). Integrar numericamente daria um
// resultado que depende de quantos passos couberam no quadro: a nuvem
// mudaria de forma conforme o aparelho, e o mesmo projeto sairia
// diferente no preview e na exportacao.
//
// ================================ O LOTE ===============================
// O QUE SAI DAQUI NAO E PIXEL: E UM LOTE DE INSTANCIAS.
//
// Cada instancia e um quad com posicao, tamanho, giro, cor e forma — o
// formato que uma chamada de desenho INSTANCIADA consome (um
// `vkCmdDrawIndexedIndirect` com 512 instancias e UMA chamada). Quem
// desenha decide: o rasterizador de referencia logo abaixo, no mesmo
// arquivo, ou o backend de GPU quando ele existir. O buffer e do
// chamador e se repete entre quadros: `reservar` uma vez, `gerar` sem
// alocar nunca.
#ifndef AUREA_RENDER_PARTICULAS_H
#define AUREA_RENDER_PARTICULAS_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "base.h"
#include "compositor.h"

namespace aurea::render {

/// DE ONDE AS PARTICULAS NASCEM. Os numeros sao CONTRATO com o Dart.
enum class EmissorDeParticulas : std::uint32_t {
  caixa = 0,
  ponto = 1,
  esfera = 2,
  linha = 3,
  /// ANEL no plano XY, de raio [raio]. E o emissor que os projetos
  /// anteriores ao motor novo ja usavam — manter o numero dele evita que
  /// um projeto salvo abra com as particulas nascendo noutro lugar.
  anel = 4,
};

/// A FORMA DESENHADA. A mascara e procedural (sem textura) e a mesma nos
/// dois caminhos — a nuvem nao muda de cara quando o aparelho troca de
/// backend.
enum class FormaDaParticula : std::uint32_t {
  esfera = 0,
  estrela = 1,
  risco = 2,
  nuvem = 3,
  quadrado = 4,
  anel = 5,
};

/// PARA ONDE A VELOCIDADE INICIAL APONTA.
enum class ModoDeEmissao : std::uint32_t {
  cone = 0,    // direcao + abertura, no plano XY, com espalhamento em Z
  esfera = 1,  // todas as direcoes
  radial = 2,  // para fora do centro do emissor
};

/// A CURVA DO TAMANHO AO LONGO DA VIDA.
enum class TamanhoNaVida : std::uint32_t {
  fixo = 0,
  cresce = 1,
  encolhe = 2,
  sobeEDesce = 3,
};

/// A CURVA DA OPACIDADE AO LONGO DA VIDA.
enum class OpacidadeNaVida : std::uint32_t {
  entraESai = 0,
  some = 1,
  aparece = 2,
  fixa = 3,
};

/// TUDO O QUE O EMISSOR E A FISICA PRECISAM.
///
/// POD DE PROPOSITO: atravessa o FFI como memoria crua, sem alocacao e sem
/// campo de tamanho variavel. Um `std::string` aqui obrigaria a um
/// protocolo de serializacao so para dizer "Fogo".
struct ParametrosDeParticulas {
  // ---- emissor ----
  EmissorDeParticulas emissor = EmissorDeParticulas::caixa;
  float centro_x = 0.0F;
  float centro_y = 0.0F;
  float centro_z = 0.0F;
  float largura = 980.0F;
  float altura = 980.0F;
  float profundidade = 1400.0F;
  float raio = 490.0F;
  /// A direcao do emissor em LINHA, em unidades de mundo.
  float linha_x = 1.0F;
  float linha_y = 0.0F;
  float linha_z = 0.0F;

  // ---- fluxo ----
  /// PARTICULAS POR SEGUNDO. Zero cai no regime de PRE-ROLL: o campo ja
  /// nasce cheio, como se o sistema rodasse desde sempre — que e o que um
  /// projeto antigo espera ao abrir no meio da cena.
  float taxa_de_nascimento = 0.0F;
  float vida_s = 4.0F;
  /// 0..1: parte das particulas vive menos.
  float vida_variacao = 0.0F;
  /// O TETO de vidas simultaneas. E ele que a qualidade adaptativa gira.
  std::uint32_t maximo = 512;
  /// A semente: muda o sorteio inteiro, e o mesmo valor devolve sempre a
  /// mesma nuvem.
  std::uint32_t semente = 7;

  // ---- velocidade ----
  float velocidade = 40.0F;
  float direcao_graus = -90.0F;
  float abertura_graus = 360.0F;
  ModoDeEmissao modo_de_emissao = ModoDeEmissao::cone;

  // ---- forcas ----
  /// Gravidade em px/s^2, POSITIVA PARA BAIXO (o +y da composicao).
  float gravidade = 0.0F;
  /// Vento em px/s: uma deriva constante, somada ao movimento.
  float vento_x = 0.0F;
  float vento_y = 0.0F;
  float vento_z = 0.0F;
  /// Resistencia do ar (1/s). Com gravidade, da a velocidade terminal.
  float arrasto = 0.0F;
  /// Turbulencia: amplitude (px) e o tamanho do detalhe (px).
  float turbulencia = 0.0F;
  float turbulencia_escala = 300.0F;
  float turbulencia_velocidade = 1.0F;
  /// A ATRACAO (positiva) ou REPULSAO (negativa) em volta do ponto
  /// [atracao_x, atracao_y, atracao_z], em rad/s. E a pulsacao do sistema
  /// massa-mola: quanto maior, mais rapido o campo puxa de volta.
  float atracao = 0.0F;
  float atracao_x = 0.0F;
  float atracao_y = 0.0F;
  float atracao_z = 0.0F;

  // ---- aparencia ----
  float tamanho = 26.0F;
  /// 0..1: espalha o tamanho entre as particulas.
  float tamanho_variacao = 0.5F;
  TamanhoNaVida tamanho_na_vida = TamanhoNaVida::fixo;
  float opacidade = 1.0F;
  float opacidade_variacao = 0.0F;
  OpacidadeNaVida opacidade_na_vida = OpacidadeNaVida::entraESai;
  std::uint32_t cor_inicio = 0xFFFF3B52U;  // RGBA
  std::uint32_t cor_fim = 0xFFFF3B52U;
  /// Pintar a cor final ao longo da vida? Falso deixa [cor_inicio] fixa.
  bool tem_cor_fim = false;
  FormaDaParticula forma = FormaDaParticula::estrela;
  /// Giro proprio, em graus por segundo.
  float giro_graus_s = 0.0F;
  /// Halo atras da particula, 0..1.
  float brilho = 0.25F;
  /// Cintilar: o alfa oscila com fase e frequencia proprias.
  bool cintilar = true;

  // ---- rastro ----
  /// 0..1: quantas copias fantasmas em idades anteriores (a cauda).
  float rastro = 0.0F;

  // ---- faiscas ----
  /// Quantas particulas nascem de CADA particula ao longo do caminho.
  std::uint32_t faiscas = 0;
  float faisca_vida_s = 0.7F;
  /// Quanto da velocidade do pai a faisca leva (0..1).
  float faisca_heranca = 0.35F;
  float faisca_velocidade = 60.0F;
  float faisca_tamanho = 0.45F;
  /// A partir de que fracao da vida do pai as faiscas comecam a sair.
  float faisca_inicio = 0.0F;

  // ---- projecao ----
  /// A lente da composicao no instante (px). 1200 e a neutra.
  float focal = 1200.0F;
  /// A rotacao do SISTEMA (camada + o que veio do pai 3D), em graus.
  float rotacao_x_graus = 0.0F;
  float rotacao_y_graus = 0.0F;
  float rotacao_z_graus = 0.0F;
};

/// UMA INSTANCIA — o que uma chamada de desenho instanciada consome.
///
/// O LAYOUT E CONTRATO COM O DART e com o shader. Ordem dos campos e
/// tamanho mudam juntos; ha `static_assert` no `.cpp` para o tamanho.
struct InstanciaDeParticula {
  float x = 0.0F;
  float y = 0.0F;
  /// O RAIO em pixels da composicao, ja com a perspectiva aplicada.
  float tamanho = 0.0F;
  /// O giro proprio, em graus.
  float angulo = 0.0F;
  /// A cor FINAL, ja interpolada e com o alfa da vida — nao-premultiplicada.
  float r = 1.0F;
  float g = 1.0F;
  float b = 1.0F;
  float a = 1.0F;
  /// A profundidade projetada: ordena o desenho (longe primeiro) e serve
  /// de chave para um futuro descarte por z.
  float profundidade = 0.0F;
  /// A forma, para o shader escolher a mascara. Vem de
  /// [FormaDaParticula].
  float forma = 0.0F;
  /// O PONTO ANTERIOR da trajetoria: e o que da a direcao do risco.
  float cauda_x = 0.0F;
  float cauda_y = 0.0F;
  /// 0..1 da vida, para variacao por sprite.
  float u = 0.0F;
  /// O halo (0..1).
  float brilho = 0.0F;
  /// A variacao propria da particula (0..1) — a mesma que escolhe a ponta
  /// da estrela.
  float variacao = 0.0F;
};

/// 14 floats — o mesmo numero no `static_assert` do `.cpp` e no Dart.
inline constexpr std::size_t kFlutuantesDaInstancia = 15;

/// QUANTAS INSTANCIAS CABEM no lote destes parametros. E o tamanho que o
/// chamador reserva uma vez, e o teto que [MotorDeParticulas::gerar]
/// respeita.
[[nodiscard]] std::size_t tamanho_do_lote(
    const ParametrosDeParticulas& p) noexcept;

class MotorDeParticulas {
 public:
  MotorDeParticulas() = default;

  /// SIMULA E ESCREVE O LOTE. Devolve quantas instancias escreveu — que e
  /// o argumento de uma chamada de desenho instanciada.
  ///
  /// NAO ALOCA. O chamador passa o buffer, e o mesmo buffer serve todos os
  /// quadros (ver [reservar_lote]).
  [[nodiscard]] std::size_t gerar(const ParametrosDeParticulas& p,
                                  double tempo_s,
                                  std::span<InstanciaDeParticula> saida) const;

  /// QUANTOS QUADROS O MOTOR JA SIMULOU. Serve ao relatorio de bancada.
  [[nodiscard]] std::uint64_t quadros() const noexcept { return quadros_; }

  /// QUANTAS PARTICULAS + FAISCAS + RASTROS o ultimo lote escreveu. Serve
  /// para o teste conferir que o teto da qualidade foi respeitado.
  [[nodiscard]] std::size_t ultimo_lote() const noexcept { return ultimo_; }

 private:
  mutable std::uint64_t quadros_ = 0;
  mutable std::size_t ultimo_ = 0;
};

/// RESERVA O LOTE UMA VEZ, com folga para o maior caso. Devolve o buffer
/// pronto — e o mesmo objeto em todos os quadros.
[[nodiscard]] std::vector<InstanciaDeParticula> reservar_lote(
    const ParametrosDeParticulas& p);

/// ==================== O RASTERIZADOR DE REFERENCIA ====================
///
/// PINTA O LOTE NUM ALVO RGBA8 PREMULTIPLICADO, do fundo para a frente.
/// E o caminho de referencia: os pixels que ele escreve sao contra o que
/// um backend de GPU vai ser comparado, e e ele que a exportacao usa
/// enquanto nao ha backend.
///
/// POR QUE ELE ORDENA AQUI, E NAO NO LACO DE INSTANCIAS: a nuvem precisa
/// ser pintada de tras para frente. A ordenacao e sobre INDICES (8 bytes
/// cada) e nao sobre as instancias inteiras (60 bytes) — e o mesmo motivo
/// pelo qual quem desenha na GPU faz o `sort` na CPU e manda o indice no
/// vertice.
struct ResultadoDoPintor {
  std::uint32_t instancias_pintadas = 0;
  std::uint32_t instancias_fora = 0;  // a caixa nao tocou o alvo
  std::uint64_t pixels_tocados = 0;
};

[[nodiscard]] ResultadoDoPintor pintar_particulas(
    CargaDeAlvo& alvo, std::span<const InstanciaDeParticula> lote,
    float opacidade_da_camada = 1.0F);

/// ==================== OS PRESETS ====================
///
/// UMA RECEITA E UM PONTO DE PARTIDA, e nao uma camisa de forca: o preset
/// preenche os parametros e a pessoa mexe no que quiser depois. Os nomes
/// sao os que se procura ("Fogo", "Faiscas"), e nao uma descricao tecnica.
enum class PresetDeParticulas : std::uint32_t {
  fogo = 0,
  faiscas = 1,
  neve = 2,
  chuva = 3,
  estrelas = 4,
  fumaca = 5,
  magia = 6,
  confete = 7,
  poeira = 8,
  explosao = 9,
};

/// QUANTOS PRESETS existem. Contrato com o Dart — o cliente monta a lista
/// pelo nome, e nunca pelo indice do enum.
inline constexpr std::uint32_t kPresetsDeParticulas = 10;

[[nodiscard]] const char* nome_do_preset(PresetDeParticulas p) noexcept;

/// OS PARAMETROS DO PRESET, sobre a base atual. O que o preset nao mexe
/// fica como estava — a lente, o centro do emissor e as rotacoes sao de
/// quem chamou, e nao da receita.
void aplicar_preset(PresetDeParticulas preset,
                    ParametrosDeParticulas& destino) noexcept;

/// ==================== A QUALIDADE ADAPTATIVA ====================
///
/// O TETO DE PARTICULAS NAO E GOSTO, E ORCAMENTO. Num celular que esta
/// esquentando, um campo de duas mil particulas derruba o relogio, e o
/// resultado e 20 fps depois de dois minutos — pior do que 40 desde o
/// comeco. O nivel vem do mesmo lugar que a resolucao do quadro.
///
/// O numero NAO multiplica o que a pessoa pediu: e um TETO. Um campo de
/// cem particulas continua com cem em qualquer nivel.
[[nodiscard]] std::uint32_t teto_de_particulas(std::uint32_t nivel,
                                               std::uint32_t pedido) noexcept;

}  // namespace aurea::render

#endif  // AUREA_RENDER_PARTICULAS_H
