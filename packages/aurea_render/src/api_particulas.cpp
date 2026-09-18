// A PORTA DO MOTOR DE PARTICULAS PARA O FLUTTER.
//
// AS DUAS REGRAS DE `api.cpp` VALEM AQUI INTEIRAS: nenhuma excecao
// atravessa o FFI (todo simbolo fecha o corpo num `catch (...)` e devolve
// 0 ou -1), e nada do lado do Dart aponta para memoria viva do C++ depois
// da chamada — o lote e do CHAMADOR, e quem o mantem vivo e ele.
//
// POR QUE O LOTE ATRAVESSA A FRONTEIRA, e o que isso custa:
//
// O lote sao 512 instancias x 60 bytes = 30 KB por quadro. Um quadro RGBA
// da composicao, para comparar, sao 8 MB. Mandar o LOTE e mandar 0,4% do
// que custaria mandar pixels — e e o mesmo buffer que uma chamada de
// desenho instanciada consumiria de qualquer forma. O que NAO se faz aqui
// e trazer pixel de particula pronto para o Dart: isso seria compor a
// nuvem duas vezes, uma em cada lado.
#include "particulas.h"

#include <cstdint>
#include <cstring>

using namespace aurea::render;

#if defined(_WIN32)
#define AUREA_API __declspec(dllexport)
#else
#define AUREA_API __attribute__((visibility("default")))
#endif

namespace {

/// OS PARAMETROS, COMO O DART OS MONTA.
///
/// TUDO DE 4 BYTES — inclusive o que e booleano no C++, que aqui vira
/// `uint32_t`. Um `bool` de um byte no meio de floats obrigaria o Dart a
/// declarar o mesmo preenchimento, e um alinhamento diferente entre os
/// dois compiladores deslocaria TODOS os campos seguintes. Foi o que a
/// `CamadaC` de `api.cpp` ja resolvia, pelo mesmo motivo.
struct ParticulasC {
  std::uint32_t emissor;
  float centro_x, centro_y, centro_z;
  float largura, altura, profundidade, raio;
  float linha_x, linha_y, linha_z;
  float taxa_de_nascimento;
  float vida_s;
  float vida_variacao;
  std::uint32_t maximo;
  std::uint32_t semente;
  float velocidade;
  float direcao_graus;
  float abertura_graus;
  std::uint32_t modo_de_emissao;
  float gravidade;
  float vento_x, vento_y, vento_z;
  float arrasto;
  float turbulencia;
  float turbulencia_escala;
  float turbulencia_velocidade;
  float atracao;
  float atracao_x, atracao_y, atracao_z;
  float tamanho;
  float tamanho_variacao;
  std::uint32_t tamanho_na_vida;
  float opacidade;
  float opacidade_variacao;
  std::uint32_t opacidade_na_vida;
  std::uint32_t cor_inicio;
  std::uint32_t cor_fim;
  std::uint32_t tem_cor_fim;
  std::uint32_t forma;
  float giro_graus_s;
  float brilho;
  std::uint32_t cintilar;
  float rastro;
  std::uint32_t faiscas;
  float faisca_vida_s;
  float faisca_heranca;
  float faisca_velocidade;
  float faisca_tamanho;
  float faisca_inicio;
  float focal;
  float rotacao_x_graus, rotacao_y_graus, rotacao_z_graus;
};

static_assert(sizeof(ParticulasC) % 4 == 0, "ParticulasC com buraco");

[[nodiscard]] ParametrosDeParticulas converter(const ParticulasC& c) noexcept {
  ParametrosDeParticulas p;
  p.emissor = static_cast<EmissorDeParticulas>(c.emissor);
  p.centro_x = c.centro_x;
  p.centro_y = c.centro_y;
  p.centro_z = c.centro_z;
  p.largura = c.largura;
  p.altura = c.altura;
  p.profundidade = c.profundidade;
  p.raio = c.raio;
  p.linha_x = c.linha_x;
  p.linha_y = c.linha_y;
  p.linha_z = c.linha_z;
  p.taxa_de_nascimento = c.taxa_de_nascimento;
  p.vida_s = c.vida_s;
  p.vida_variacao = c.vida_variacao;
  p.maximo = c.maximo;
  p.semente = c.semente;
  p.velocidade = c.velocidade;
  p.direcao_graus = c.direcao_graus;
  p.abertura_graus = c.abertura_graus;
  p.modo_de_emissao = static_cast<ModoDeEmissao>(c.modo_de_emissao);
  p.gravidade = c.gravidade;
  p.vento_x = c.vento_x;
  p.vento_y = c.vento_y;
  p.vento_z = c.vento_z;
  p.arrasto = c.arrasto;
  p.turbulencia = c.turbulencia;
  p.turbulencia_escala = c.turbulencia_escala;
  p.turbulencia_velocidade = c.turbulencia_velocidade;
  p.atracao = c.atracao;
  p.atracao_x = c.atracao_x;
  p.atracao_y = c.atracao_y;
  p.atracao_z = c.atracao_z;
  p.tamanho = c.tamanho;
  p.tamanho_variacao = c.tamanho_variacao;
  p.tamanho_na_vida = static_cast<TamanhoNaVida>(c.tamanho_na_vida);
  p.opacidade = c.opacidade;
  p.opacidade_variacao = c.opacidade_variacao;
  p.opacidade_na_vida = static_cast<OpacidadeNaVida>(c.opacidade_na_vida);
  p.cor_inicio = c.cor_inicio;
  p.cor_fim = c.cor_fim;
  p.tem_cor_fim = c.tem_cor_fim != 0U;
  p.forma = static_cast<FormaDaParticula>(c.forma);
  p.giro_graus_s = c.giro_graus_s;
  p.brilho = c.brilho;
  p.cintilar = c.cintilar != 0U;
  p.rastro = c.rastro;
  p.faiscas = c.faiscas;
  p.faisca_vida_s = c.faisca_vida_s;
  p.faisca_heranca = c.faisca_heranca;
  p.faisca_velocidade = c.faisca_velocidade;
  p.faisca_tamanho = c.faisca_tamanho;
  p.faisca_inicio = c.faisca_inicio;
  p.focal = c.focal;
  p.rotacao_x_graus = c.rotacao_x_graus;
  p.rotacao_y_graus = c.rotacao_y_graus;
  p.rotacao_z_graus = c.rotacao_z_graus;
  return p;
}

void preencher(const ParametrosDeParticulas& p, ParticulasC& c) noexcept {
  std::memset(&c, 0, sizeof(c));
  c.emissor = static_cast<std::uint32_t>(p.emissor);
  c.centro_x = p.centro_x;
  c.centro_y = p.centro_y;
  c.centro_z = p.centro_z;
  c.largura = p.largura;
  c.altura = p.altura;
  c.profundidade = p.profundidade;
  c.raio = p.raio;
  c.linha_x = p.linha_x;
  c.linha_y = p.linha_y;
  c.linha_z = p.linha_z;
  c.taxa_de_nascimento = p.taxa_de_nascimento;
  c.vida_s = p.vida_s;
  c.vida_variacao = p.vida_variacao;
  c.maximo = p.maximo;
  c.semente = p.semente;
  c.velocidade = p.velocidade;
  c.direcao_graus = p.direcao_graus;
  c.abertura_graus = p.abertura_graus;
  c.modo_de_emissao = static_cast<std::uint32_t>(p.modo_de_emissao);
  c.gravidade = p.gravidade;
  c.vento_x = p.vento_x;
  c.vento_y = p.vento_y;
  c.vento_z = p.vento_z;
  c.arrasto = p.arrasto;
  c.turbulencia = p.turbulencia;
  c.turbulencia_escala = p.turbulencia_escala;
  c.turbulencia_velocidade = p.turbulencia_velocidade;
  c.atracao = p.atracao;
  c.atracao_x = p.atracao_x;
  c.atracao_y = p.atracao_y;
  c.atracao_z = p.atracao_z;
  c.tamanho = p.tamanho;
  c.tamanho_variacao = p.tamanho_variacao;
  c.tamanho_na_vida = static_cast<std::uint32_t>(p.tamanho_na_vida);
  c.opacidade = p.opacidade;
  c.opacidade_variacao = p.opacidade_variacao;
  c.opacidade_na_vida = static_cast<std::uint32_t>(p.opacidade_na_vida);
  c.cor_inicio = p.cor_inicio;
  c.cor_fim = p.cor_fim;
  c.tem_cor_fim = p.tem_cor_fim ? 1U : 0U;
  c.forma = static_cast<std::uint32_t>(p.forma);
  c.giro_graus_s = p.giro_graus_s;
  c.brilho = p.brilho;
  c.cintilar = p.cintilar ? 1U : 0U;
  c.rastro = p.rastro;
  c.faiscas = p.faiscas;
  c.faisca_vida_s = p.faisca_vida_s;
  c.faisca_heranca = p.faisca_heranca;
  c.faisca_velocidade = p.faisca_velocidade;
  c.faisca_tamanho = p.faisca_tamanho;
  c.faisca_inicio = p.faisca_inicio;
  c.focal = p.focal;
  c.rotacao_x_graus = p.rotacao_x_graus;
  c.rotacao_y_graus = p.rotacao_y_graus;
  c.rotacao_z_graus = p.rotacao_z_graus;
}

}  // namespace

extern "C" {

/// O TAMANHO DA STRUCT DE PARAMETROS. O Dart confere contra o dele antes
/// de mandar qualquer coisa — um campo novo de um lado so zera o do outro
/// em silencio, e o sintoma seria "a nuvem nao obedece ao tamanho".
AUREA_API std::uint32_t aurea_render_particulas_tamanho_parametros(void) {
  try {
    return static_cast<std::uint32_t>(sizeof(ParticulasC));
  } catch (...) {
    return 0;
  }
}

/// O TAMANHO DE UMA INSTANCIA. Mesmo motivo.
AUREA_API std::uint32_t aurea_render_particulas_tamanho_instancia(void) {
  try {
    return static_cast<std::uint32_t>(sizeof(InstanciaDeParticula));
  } catch (...) {
    return 0;
  }
}

/// QUANTAS INSTANCIAS CABEM no lote destes parametros — o tamanho que o
/// Dart reserva UMA vez e reaproveita em todos os quadros.
AUREA_API std::uint32_t aurea_render_particulas_tamanho_do_lote(
    const ParticulasC* p) {
  try {
    if (p == nullptr) return 0;
    const std::size_t n = tamanho_do_lote(converter(*p));
    return static_cast<std::uint32_t>(
        n > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : n);
  } catch (...) {
    return 0;
  }
}

/// SIMULA E ESCREVE O LOTE. Devolve quantas instancias escreveu.
AUREA_API std::uint32_t aurea_render_particulas_gerar(
    const ParticulasC* p, double tempo_s, InstanciaDeParticula* saida,
    std::uint32_t capacidade) {
  try {
    if (p == nullptr || saida == nullptr || capacidade == 0) return 0;
    static MotorDeParticulas motor;
    const std::size_t n = motor.gerar(converter(*p), tempo_s,
                                      std::span<InstanciaDeParticula>(
                                          saida, capacidade));
    return static_cast<std::uint32_t>(n);
  } catch (...) {
    return 0;
  }
}

/// PINTA O LOTE NUM ALVO RGBA8 PREMULTIPLICADO, do fundo para a frente.
/// Devolve quantos pixels foram tocados — zero significa "nao desenhou
/// nada", e nao "deu erro".
AUREA_API std::uint64_t aurea_render_particulas_pintar(
    std::uint8_t* alvo, std::uint32_t largura, std::uint32_t altura,
    const InstanciaDeParticula* lote, std::uint32_t quantas,
    float opacidade) {
  try {
    if (alvo == nullptr || lote == nullptr || largura == 0 || altura == 0) {
      return 0;
    }
    CargaDeAlvo carga(largura, altura);
    const std::size_t bytes = carga.pixels.size();
    std::memcpy(carga.pixels.data(), alvo, bytes);
    const ResultadoDoPintor r = pintar_particulas(
        carga, std::span<const InstanciaDeParticula>(lote, quantas),
        opacidade);
    std::memcpy(alvo, carga.pixels.data(), bytes);
    return r.pixels_tocados;
  } catch (...) {
    return 0;
  }
}

/// O TETO DA QUALIDADE ADAPTATIVA.
AUREA_API std::uint32_t aurea_render_particulas_teto(std::uint32_t nivel,
                                                     std::uint32_t pedido) {
  try {
    return teto_de_particulas(nivel, pedido);
  } catch (...) {
    return 0;
  }
}

/// QUANTOS PRESETS EXISTEM.
AUREA_API std::uint32_t aurea_render_particulas_quantos_presets(void) {
  try {
    return kPresetsDeParticulas;
  } catch (...) {
    return 0;
  }
}

/// O NOME DE UM PRESET. Vem por INDICE e e conferido pelo nome do lado do
/// Dart — indice de enum envelhece mal, nome nao.
AUREA_API const char* aurea_render_particulas_preset_nome(
    std::uint32_t indice) {
  try {
    if (indice >= kPresetsDeParticulas) return "";
    return nome_do_preset(static_cast<PresetDeParticulas>(indice));
  } catch (...) {
    return "";
  }
}

/// APLICA UM PRESET SOBRE O QUE JA ESTA EM `p` (centro, lente, rotacoes e
/// semente ficam; o resto e da receita).
AUREA_API std::int32_t aurea_render_particulas_preset(std::uint32_t indice,
                                                      ParticulasC* p) {
  try {
    if (p == nullptr || indice >= kPresetsDeParticulas) return -1;
    ParametrosDeParticulas atual = converter(*p);
    aplicar_preset(static_cast<PresetDeParticulas>(indice), atual);
    preencher(atual, *p);
    return 0;
  } catch (...) {
    return -1;
  }
}

/// OS PARAMETROS DE UM PRESET DO ZERO, com a lente neutra.
AUREA_API std::int32_t aurea_render_particulas_preset_novo(
    std::uint32_t indice, ParticulasC* p) {
  try {
    if (p == nullptr || indice >= kPresetsDeParticulas) return -1;
    ParametrosDeParticulas novo;
    aplicar_preset(static_cast<PresetDeParticulas>(indice), novo);
    preencher(novo, *p);
    return 0;
  } catch (...) {
    return -1;
  }
}

}  // extern "C"
