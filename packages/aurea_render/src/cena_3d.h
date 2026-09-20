// A CENA 3D DO AUREA — O ESTADO, E NAO UM LUGAR.
//
// ============================ O QUE ISTO E =============================
// Um modelo 3D no Aurea e UMA CAMADA DA TIMELINE, igual a um video, a um
// texto ou a uma imagem. Nao existe uma "tela de cena 3D" para o usuario
// entrar, com a propria timeline e o proprio tempo: isso foi descartado.
// A timeline e a UNICA fonte da verdade, e o tempo que vale e o dela.
//
// Isso decide o desenho deste modulo: aqui nao ha relogio, nao ha estado
// acumulado e nao ha "delta desde o ultimo quadro". Ha uma lista de
// camadas com o estado JA RESOLVIDO pela timeline — posicao, rotacao,
// escala, qual clipe de animacao toca e em que instante daquele clipe — e
// uma funcao que transforma isso em desenhos concretos. O mesmo instante
// produz o mesmo quadro, sempre (§34), e o quadro exportado e o quadro
// que se viu no preview porque e a mesma avaliacao (§33).
//
// ============================= O QUE ELE NAO E =========================
// Nao e um motor de jogos: nao ha gravidade, colisao, script por quadro
// nem estado entre quadros. O que precisa de continuidade (uma particula
// que cai, um rastro) e problema de outra camada, e nao deste modulo.
#ifndef AUREA_RENDER_CENA_3D_H
#define AUREA_RENDER_CENA_3D_H

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "base.h"
#include "importador.h"
#include "modelo.h"

namespace aurea::render::tresd {

/// A COR DO PAINEL VIRA LINEAR, AQUI E UMA VEZ POR DESENHO.
///
/// A textura de cor ja chega na GPU marcada como sRGB e o driver a converte
/// ao amostrar (§6). A cor que o dono escolhe no painel — e a do ceu e a do
/// chao — nao passa por textura nenhuma e precisa ser convertida em algum
/// lugar: e o lugar e este, e nao o shader. Um `pow` por fragmento repetiria
/// em milhoes de pixels uma conta que nao muda dentro do desenho.
///
/// A CURVA E A sRGB DE VERDADE, e nao um `pow(x, 2.2)`: as duas quase
/// coincidem no meio e divergem nas pontas, e a diferenca apareceria como um
/// material um pouco mais escuro do que o mesmo material com a cor vinda da
/// textura. A volta (linear para sRGB) mora no `pbr.frag`, e as duas tem de
/// continuar inversas uma da outra.
[[nodiscard]] inline float canal_para_linear(std::uint8_t v) noexcept {
  const float s = static_cast<float>(v) * (1.0F / 255.0F);
  return s <= 0.04045F ? s * (1.0F / 12.92F)
                       : std::pow((s + 0.055F) * (1.0F / 1.055F), 2.4F);
}

}  // namespace aurea::render::tresd
#include "vetor.h"

namespace aurea::render::tresd {

// ------------------------------------------------------------------ luzes

/// OS TRES TIPOS QUE EXISTEM (§9). Nao ha area light: ela existe no papel
/// e nao no orcamento de um celular, e um tipo que so funciona em cena
/// pequena e pior do que um tipo que nao existe.
enum class TipoDeLuz : std::uint32_t {
  direcional = 0,
  pontual = 1,
  holofote = 2,
};

/// UMA LUZ. A COR E EM PONTO FLUTUANTE, e nao em `Cor` de 8 bits: uma luz
/// com intensidade 4 e cor (2, 1, 0.5) precisa de valores acima de 1 para
/// estourar de proposito, e o 8 bits cortaria justamente o estouro que da
/// o brilho.
struct Luz {
  TipoDeLuz tipo = TipoDeLuz::direcional;

  Vec3 posicao{0.0F, 3.0F, 0.0F};
  /// Para a direcional e a holofote: para ONDE a luz aponta.
  Vec3 direcao{0.0F, -1.0F, 0.0F};

  float vermelho = 1.0F, verde = 1.0F, azul = 1.0F;
  float intensidade = 1.0F;

  /// Ate onde a pontual e a holofote alcancam. Zero desliga o limite (a
  /// luz vale em toda a cena), que e o que uma luz de preenchimento
  /// artistica costuma querer.
  float alcance = 0.0F;

  /// O CONE DA HOLOFOTE, em graus, medido do eixo: o interno e o nucleo
  /// aceso, o externo onde a luz morre. Entre os dois ha a penumbra — e e
  /// ela que separa uma holofote de um circulo recortado a faca.
  float angulo_interno_graus = 20.0F;
  float angulo_externo_graus = 35.0F;

  bool ligada = true;
};

// ----------------------------------------------------------------- câmera

/// A CAMERA DE COMPOSICAO (§8). Nao e a camera do editor — a do editor
/// orbita e nao vai para a exportacao; esta vai, e por isso e uma camada
/// da timeline como qualquer outra.
struct Camera {
  Vec3 posicao{0.0F, 0.0F, 5.0F};
  /// O PONTO PARA ONDE OLHA. Quando `usar_rotacao` esta ligado, quem manda
  /// e a rotacao e o alvo e derivado — sao dois jeitos de dizer a mesma
  /// coisa, e aceitar os dois sem escolher um seria uma ambiguidade que
  /// aparece como camera torta.
  Vec3 alvo{0.0F, 0.0F, 0.0F};
  Vec3 rotacao_graus{0.0F, 0.0F, 0.0F};
  bool usar_rotacao = false;
  Vec3 cima{0.0F, 1.0F, 0.0F};

  float fov_graus = 45.0F;
  float perto = 0.05F;
  float longe = 500.0F;
  bool ortografica = false;
  /// Na ortografica: quantas unidades cabem na altura da composicao.
  float altura_ortografica = 4.0F;
};

// ------------------------------------------------------------------ camada

/// A SOBRESCRITA DE MATERIAL DE UMA CAMADA.
///
/// Valores em -1 significam "o que o modelo traz" — e nao zero, que e uma
/// escolha artistica legitima (metalico 0 e um plastico). Sem essa
/// distincao, mexer num controle apagaria o material do arquivo, e voltar
/// ao original seria impossivel sem recarregar.
struct MaterialDaCamada {
  bool ligado = false;
  Cor cor_base{255, 255, 255, 255};
  float metalico = -1.0F;
  float rugosidade = -1.0F;
  float forca_emissiva = -1.0F;
  Cor emissivo{0, 0, 0, 255};
  /// -1 = do modelo; 0 opaco, 1 mascarado, 2 transparente.
  int modo = -1;
  bool face_dupla = false;
  float alfa_corte = -1.0F;
  /// Apaga a textura de cor sem apagar o material (§52 "mudar material").
  bool sem_textura_de_cor = false;
};

/// UMA CAMADA 3D, JA RESOLVIDA PELA TIMELINE.
struct Camada3D {
  /// A identificacao que o Dart usa. E o que liga esta camada a camada
  /// homonima da timeline — sem isso, a selecao no palco nao teria como
  /// saber qual objeto esta debaixo do dedo.
  std::uint64_t alca = 0;

  /// O indice do modelo no acervo, ou -1 quando a camada ainda nao tem
  /// geometria (o modelo esta carregando, ou falhou). UMA CAMADA SEM
  /// MODELO NAO E UM ERRO (§26): ela existe, aparece na timeline, e nao
  /// desenha nada enquanto o modelo nao chega.
  std::int32_t modelo = -1;

  /// Qual clipe toca, e em que instante DELE. O tempo ja vem resolvido
  /// pela timeline (§12): uma camada com Time Remap a 50% nao muda nada
  /// aqui, porque quem desacelera e a timeline, e nao o amostrador.
  std::int32_t animacao = -1;
  double tempo_da_animacao = 0.0;

  bool visivel = true;
  Vec3 posicao{0.0F, 0.0F, 0.0F};
  Vec3 rotacao_graus{0.0F, 0.0F, 0.0F};
  Vec3 escala{1.0F, 1.0F, 1.0F};
  /// O PIVO DENTRO DA CAIXA DO MODELO, em 0..1 por eixo. (0,5;0,5;0,5) e o
  /// centro. O pivo do Aurea e o mesmo das outras camadas, so que com um
  /// eixo a mais — e mexer nele move o modelo em volta do ponto escolhido
  /// sem mexer na posicao.
  Vec3 ancora{0.5F, 0.5F, 0.5F};

  float opacidade = 1.0F;
  /// Tinge o modelo inteiro. Branco opaco nao muda nada.
  Cor cor{255, 255, 255, 255};

  MaterialDaCamada material{};

  /// A ORDEM NA TIMELINE. Uma camada de cima desenha por cima; para os
  /// solidos isso quase nao importa (o teste de profundidade decide), mas
  /// para os transparentes e o que separa "o vidro na frente" de "o vidro
  /// atras".
  float camada_z = 0.0F;
};

// ------------------------------------------------------------------- cena

/// QUANTO DE SOMBRA (§28). Desligar nao e so uma questao de gosto: num
/// celular, o mapa de sombra e um passe a mais por luz.
enum class QualidadeDaSombra : std::uint32_t {
  desligada = 0,
  baixa = 1,
  media = 2,
  alta = 3,
};

struct Cena3D {
  std::vector<Camada3D> camadas;
  Camera camera;
  std::vector<Luz> luzes;

  /// A LUZ QUE VEM DE TODO LADO. Sem ela, o lado escuro de um objeto fica
  /// preto absoluto — o que nao existe nem no espaco, e faz o modelo
  /// parecer recortado em papel em vez de solido.
  float ambiente_vermelho = 0.18F;
  float ambiente_verde = 0.18F;
  float ambiente_azul = 0.20F;

  /// O AMBIENTE COM DIRECAO, em linear: a cor que vem de cima e a que vem
  /// de baixo. O ambiente plano nao faz metal — um metal nao tem difusa, e
  /// refletindo uma cor unica ele vira uma cor chapada. Neutro (1,1,1 nas
  /// duas) devolve exatamente o comportamento do ambiente plano.
  float ceu_vermelho = 1.0F;
  float ceu_verde = 1.0F;
  float ceu_azul = 1.0F;
  float chao_vermelho = 1.0F;
  float chao_verde = 1.0F;
  float chao_azul = 1.0F;

  /// QUANTO DO AMBIENTE VOLTA NO REFLEXO ESPELHADO (0..1). Sem ele, o
  /// metal so mostra o realce da luz direta e o resto da superficie fica
  /// com a cor da reflexao ambiente chapada.
  float reflexo_do_ambiente = 0.0F;

  QualidadeDaSombra sombra = QualidadeDaSombra::desligada;
  /// Amostras por eixo do antisserrilhado da 3D (§29). 1 = sem.
  std::uint32_t amostras = 1;

  /// O TAMANHO DO ALVO, em pixels. O mesmo da composicao: a 3D nao tem
  /// resolucao propria, porque ela e uma camada e nao uma janela.
  std::uint32_t largura = 0;
  std::uint32_t altura = 0;
};

// ------------------------------------------------- o quadro ja resolvido

/// UMA GEOMETRIA PRONTA PARA DESENHAR.
struct Desenho {
  /// O MODELO DE ONDE SAI A GEOMETRIA. `malha` sozinho nao basta: ele e um
  /// indice DENTRO de um modelo, e o renderizador precisa saber de qual —
  /// sem isto, o mesmo indice em dois modelos apontaria para a malha errada
  /// e o sintoma seria uma peca de outro objeto aparecendo na cena.
  std::int32_t modelo = -1;
  std::int32_t malha = -1;

  /// O PEDACO DA MALHA QUE ESTE DESENHO DESENHA.
  ///
  /// Uma malha com varias primitivas — que e o caso COMUM de um GLB, e nao
  /// a excecao — tem um material por pedaco. Desenhar a malha inteira com o
  /// material do primeiro pedaco pintaria o objeto todo com a cor errada, e
  /// o defeito apareceria so em arquivo com mais de um material: o tipo de
  /// coisa que passa num teste de cubo e chega na mao do dono.
  ///
  /// Cada pedaco e um `DrawIndexed`: o material, onde comeca e quantos.
  std::uint32_t primeiro_indice = 0;
  std::uint32_t quantidade_indices = 0;
  std::uint32_t base_do_vertice = 0;

  /// O material depois da sobrescrita da camada — o renderizador nao
  /// precisa saber que existe sobrescrita.
  Material material{};
  /// Onde este desenho vai parar no mundo.
  Mat4 mundo = Mat4::identidade();
  /// O CONJUNTO DE MATRIZES DE OSSO, no acervo de pele do quadro. -1
  /// quando a malha nao e esqueletica.
  std::int32_t pele = -1;
  float opacidade = 1.0F;
  Cor cor{255, 255, 255, 255};
  /// A distancia ao olho, para ordenar os transparentes do mais longe para
  /// o mais perto. Nos opacos ela nao e usada — o teste de profundidade
  /// resolve, e resolve melhor.
  float distancia = 0.0F;
  std::uint64_t alca = 0;
};

/// O QUE O RENDERIZADOR RECEBE. Tudo o que depende do tempo ja foi
/// resolvido: nao ha mais nenhuma consulta a chave, a clipe ou a curva
/// daqui para a frente.
struct Quadro3D {
  Camera camera;
  Mat4 vista = Mat4::identidade();
  Mat4 projecao = Mat4::identidade();
  /// A camera de onde se olha, para o corte de frustum — guardada em
  /// separado porque tirar os seis planos da matriz a cada consulta de
  /// caixa custaria mais do que comparar contra isto.
  Vec3 olho{0.0F, 0.0F, 0.0F};

  std::vector<Luz> luzes;
  float ambiente[3] = {0.18F, 0.18F, 0.20F};
  /// O ceu e o chao ja lineares, e a forca do reflexo do ambiente.
  float ceu[3] = {1.0F, 1.0F, 1.0F};
  float chao[3] = {1.0F, 1.0F, 1.0F};
  float reflexo_do_ambiente = 0.0F;
  QualidadeDaSombra sombra = QualidadeDaSombra::desligada;
  std::uint32_t amostras = 1;
  std::uint32_t largura = 0;
  std::uint32_t altura = 0;

  /// Os desenhos OPACOS primeiro, e depois os transparentes do mais longe
  /// para o mais perto. A ordem e o resultado da avaliacao, e nao uma
  /// decisao do renderizador: quem escolhe e quem sabe das camadas.
  std::vector<Desenho> opacos;
  std::vector<Desenho> transparentes;

  /// AS MATRIZES DE PELE DO QUADRO, num bloco so. Cada desenho esqueletico
  /// aponta para uma fatia dele (`Desenho::pele` e o primeiro osso).
  /// Empacotar assim e o que permite um envio por quadro em vez de um por
  /// osso (§39) e o que mantem os buffers de GPU reaproveitados.
  std::vector<Mat4> pele;
  std::vector<std::uint32_t> ossos_por_pele;

  geo::Caixa limites{};
  std::uint32_t triangulos = 0;
  std::uint32_t vertices = 0;
  /// Quantas camadas existiam e quantas desenharam. A diferenca e o que a
  /// barra de estado mostra quando o dono pergunta "por que sumiu".
  std::uint32_t camadas = 0;
  std::uint32_t camadas_desenhadas = 0;
  std::uint32_t camadas_fora_do_campo = 0;
  std::uint32_t camadas_sem_modelo = 0;

  /// MUDA QUANDO A IMAGEM MUDA. O compositor 2D ja reaproveita o quadro
  /// quando a `impressao` da cena nao muda; esta e a mesma ideia para a
  /// 3D, e evita redesenhar e reler a mesma imagem a cada quadro.
  std::uint64_t impressao = 0;
};

// ------------------------------------------------------------------ acervo

/// OS MODELOS CARREGADOS, COM POSSE (§20).
///
/// O modelo e o dado PURO que saiu do importador: vertices, materiais,
/// texturas, ossos, clipes. Nao ha nada de GPU aqui — subir para a placa e
/// do renderizador, e essa separacao e o que permite importar num thread
/// de fundo sem tocar em contexto nenhum (§26).
class AcervoDeModelos {
 public:
  AcervoDeModelos() = default;
  AcervoDeModelos(const AcervoDeModelos&) = delete;
  AcervoDeModelos& operator=(const AcervoDeModelos&) = delete;

  /// GUARDA UM MODELO JA IMPORTADO. Devolve a alca.
  std::int32_t guardar(Modelo&& modelo);

  /// IMPORTA E GUARDA. Um arquivo que ja foi importado antes (mesmo
  /// caminho, mesmo tamanho e mesma data) devolve a MESMA alca, sem ler de
  /// novo — o caso de dez camadas do mesmo modelo, que e como se monta uma
  /// cena de verdade (§6, §19).
  Resulta<std::int32_t> importar_do_arquivo(const std::string& caminho,
                                            const OpcoesDeImportacao& opcoes,
                                            RelatoDaImportacao* relato = nullptr);

  Resulta<std::int32_t> importar_da_memoria(const std::uint8_t* bytes,
                                            std::size_t tamanho,
                                            const std::string& extensao,
                                            const OpcoesDeImportacao& opcoes,
                                            RelatoDaImportacao* relato = nullptr);

  [[nodiscard]] const Modelo* obter(std::int32_t alca) const noexcept;

  /// SEGURA E SOLTA. Uma camada que aponta para um modelo o segura
  /// enquanto existir; quando a ultima solta, o modelo pode sair (§49).
  void segurar(std::int32_t alca) noexcept;
  void soltar(std::int32_t alca) noexcept;

  /// APAGA O QUE NINGUEM SEGURA. Chamado quando a memoria aperta (§21).
  std::uint32_t limpar_desocupados() noexcept;

  void limpar() noexcept;

  [[nodiscard]] std::uint32_t quantidade() const noexcept {
    return static_cast<std::uint32_t>(fichas_.size());
  }
  [[nodiscard]] std::uint64_t bytes() const noexcept;

  // ---------------------------------------------------------- sob pressao

  /// QUANTO O ACERVO PODE OCUPAR. Estourar o teto nao recusa a importacao
  /// de um modelo so — ele existe justamente para carregar um arquivo
  /// grande (§21). O que o teto faz e impedir o SEGUNDO.
  std::uint64_t teto_de_bytes = 640ULL * 1024ULL * 1024ULL;

  /// DE QUANTO AINDA CABE. O importador consulta antes de aceitar (§26: um
  /// modelo de 1 GB nao pode derrubar o processo).
  [[nodiscard]] std::uint64_t espaco_livre() const noexcept {
    const std::uint64_t usado = bytes();
    return usado >= teto_de_bytes ? 0 : teto_de_bytes - usado;
  }

 private:
  struct Ficha {
    Modelo modelo;
    std::uint32_t usos = 0;
    /// A chave do arquivo de origem, para nao reimportar. Vazia quando o
    /// modelo veio da memoria.
    std::string origem;
  };

  std::vector<Ficha> fichas_;
  std::vector<std::int32_t> livres_;
};

// -------------------------------------------------------------- avaliacao

/// A CAMERA VIRA VISTA E PROJECAO. O aspecto sai do alvo, e nao de um
/// numero solto: uma camera com aspecto fixo entortaria o modelo quando a
/// composicao mudasse de formato.
void montar_camera(const Camera& camera, std::uint32_t largura,
                   std::uint32_t altura, Mat4& vista, Mat4& projecao,
                   Vec3& olho) noexcept;

/// A TRANSFORMACAO DE UMA CAMADA NO MUNDO. Junta posicao, rotacao, escala
/// e a ancora com a caixa do modelo — a ancora e um ponto DENTRO do
/// modelo, entao ela depende do tamanho dele e nao e um numero fixo.
[[nodiscard]] Mat4 mundo_da_camada(const Camada3D& camada,
                                   const Modelo& modelo) noexcept;

/// O MATERIAL DEPOIS DA SOBRESCRITA DA CAMADA.
[[nodiscard]] Material material_da_camada(const Material& original,
                                          const MaterialDaCamada& sobre) noexcept;

/// AVALIA A CENA NO INSTANTE PEDIDO. Sem relogio, sem estado: entra a
/// cena, sai o quadro (§34).
void avaliar(const Cena3D& cena, const AcervoDeModelos& acervo, Quadro3D& saida);

}  // namespace aurea::render::tresd

#endif  // AUREA_RENDER_CENA_3D_H
