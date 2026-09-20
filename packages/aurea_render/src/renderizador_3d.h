// O RENDERIZADOR 3D — DILIGENT, E UM SO PARA O PREVIEW E A EXPORTACAO.
//
// ============================ O QUE ISTO E =============================
// Ele recebe o `Quadro3D` (a cena ja resolvida pela timeline) e o
// `AcervoDeModelos` (o dado puro que saiu do importador), desenha na GPU
// fora da tela e devolve os pixels. Quem compoe esses pixels com o video, o
// texto e os efeitos e o compositor 2D que ja existia — a 3D NAO tem um
// caminho proprio de saida (§14, §33).
//
// ============================ AS TRES DECISOES =========================
//
//  1. NAO HA CAMERA DE EDITOR AQUI. A camera que desenha e a de composicao,
//     que e uma camada da timeline (§8). Uma segunda camera de bancada
//     exigiria uma segunda avaliacao da cena, e a partir dai o preview e a
//     exportacao deixariam de ser o mesmo desenho — que e o defeito que
//     este modulo existe para nao ter.
//
//  2. A GEOMETRIA E IMUTAVEL DEPOIS DE SUBIR. Um modelo vira buffers de
//     GPU uma vez e fica. Nao ha envio por quadro de vertice (§5, §24):
//     o que muda por quadro e a matriz, e a matriz e um bloco de uniformes.
//     A unica excecao e o bloco de ossos, que muda com a pose, e por isso
//     ele e do tamanho do esqueleto e nao do numero de vertices.
//
//  3. O QUE MUDA POR QUADRO VAI NUM BLOCO SO. As luzes, a camera e as
//     matrizes de osso sobem uma vez por quadro cada; por desenho sobe um
//     bloco pequeno. A ponte com o C++ (§39) existe para isto: nenhuma
//     chamada por vertice, por osso ou por objeto.
//
// ============================ O QUE ELE NAO FAZ ========================
// Nao carrega modelo (isso e do importador, em thread de fundo, §26), nao
// decide o que aparece (isso e da timeline) e nao guarda estado entre
// quadros alem do que a GPU exige (buffers, texturas, pipelines).
#ifndef AUREA_RENDER_RENDERIZADOR_3D_H
#define AUREA_RENDER_RENDERIZADOR_3D_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base.h"
#include "cena_3d.h"

namespace aurea::render::tresd {

// ------------------------------------------------------- o dispositivo

/// ABRE O DISPOSITIVO DILIGENT. 1 se subiu, 0 se nao.
///
/// E UMA VEZ SO PARA O PROCESSO INTEIRO, e nao uma por renderizador: abrir
/// um dispositivo Vulkan custa dezenas de milissegundos e leva memoria que
/// nao se recupera. O segundo chamador recebe a resposta guardada, e nao um
/// segundo dispositivo — dois dispositivos Vulkan no mesmo processo nao
/// compartilham textura nenhuma, e o resultado seria a cena ser desenhada
/// num contexto que ninguem le.
int preparar();

[[nodiscard]] bool pronto();

/// POR QUE NAO SUBIU. Ponteiro para string estatica DENTRO da biblioteca:
/// nao pertence a quem chama e nao pode ser liberado.
[[nodiscard]] const char* motivo();

/// O NOME DO RENDERIZADOR EM USO ("Diligent/Vulkan").
[[nodiscard]] const char* backend();

// ----------------------------------------------------- o que foi medido

/// OS NUMEROS DO ULTIMO QUADRO. Nao ha estimativa aqui: e o que a GPU
/// recebeu, contado no caminho. A barra de estado e o teste de estresse
/// (§45) leem daqui.
struct Estatisticas3D {
  std::uint32_t desenhos = 0;
  std::uint32_t desenhos_esqueleticos = 0;
  std::uint32_t triangulos = 0;
  std::uint32_t instancias = 0;
  std::uint32_t malhas_na_gpu = 0;
  std::uint32_t texturas_na_gpu = 0;
  std::uint32_t pipelines = 0;
  std::uint32_t quadros = 0;
  /// Quanto o ultimo `desenhar` levou, em milissegundos. Inclui o envio dos
  /// blocos, os passes e a releitura dos pixels.
  double ultimo_desenho_ms = 0.0;
  /// Medido na GPU, e nao estimado: a soma dos bytes dos buffers e texturas
  /// que este renderizador criou (§21).
  std::uint64_t bytes_de_gpu = 0;
  std::uint32_t lado_da_sombra = 0;
  bool sombra_ligada = false;
  bool antisserrilhado = false;
};

// ------------------------------------------------------- o renderizador

/// O RENDERIZADOR. Um por processo, criado na primeira cena que precise
/// dele.
///
/// ELE NAO TEM ESTADO DE CENA. Tudo o que ele sabe entre dois quadros e o
/// que a GPU guarda: buffers, texturas e pipelines. Trocar de projeto,
/// voltar um instante ou exportar outro trecho nao deixa residuo nenhum —
/// o que existe e um cache do que ja foi subido, e nao a memoria do que
/// aconteceu (§34).
class Renderizador3D {
 public:
  /// CRIA. Devolve nulo com o motivo em `erro` quando o dispositivo nao
  /// subiu — e ai a camada 3D nao desenha, mas o resto do app continua.
  [[nodiscard]] static std::unique_ptr<Renderizador3D> criar(std::string& erro);

  Renderizador3D(const Renderizador3D&) = delete;
  Renderizador3D& operator=(const Renderizador3D&) = delete;
  ~Renderizador3D();

  /// DESENHA O QUADRO. Devolve falso quando nao deu — e `ultimo_erro` diz o
  /// que foi. Um quadro que nao desenha NAO derruba o app (§43): o alvo
  /// fica com o que tinha, e a composicao segue.
  bool desenhar(const Quadro3D& quadro, const AcervoDeModelos& acervo);

  /// OS PIXELS DO ULTIMO QUADRO, em RGBA8, na ordem de linhas de cima para
  /// baixo. O ponteiro pertence ao renderizador e vale ate o proximo
  /// `desenhar`.
  [[nodiscard]] const std::uint8_t* pixels() const noexcept;
  [[nodiscard]] std::uint32_t largura() const noexcept;
  [[nodiscard]] std::uint32_t altura() const noexcept;

  /// SOBE PARA A PLACA O QUE AINDA NAO SUBIU. Chamado do thread de fundo
  /// logo depois de uma importacao (§26), para que o primeiro quadro com o
  /// modelo nao pague a subida — o defeito classico e o app travar no
  /// instante em que o modelo aparece.
  ///
  /// ELE NAO DESENHA E NAO PRECISA DE QUADRO NENHUM. Devolve quantas malhas
  /// subiram.
  std::uint32_t aquecer(const AcervoDeModelos& acervo);

  /// ESQUECE O QUE NINGUEM MAIS USA. Recebe os modelos que ainda existem no
  /// acervo e apaga da GPU todo o resto. E o que impede a memoria de crescer
  /// para sempre quando o dono abre e fecha projetos (§21, §49).
  std::uint32_t limpar(const AcervoDeModelos& acervo);

  /// ESQUECE TUDO. Chamado quando o app vai fechar ou trocar de projeto.
  void liberar() noexcept;

  /// A GEOMETRIA DESTE MODELO JA ESTA NA PLACA? E a resposta que a ficha
  /// da camada mostra — e ela e por IDENTIDADE, e nao por alca: o acervo
  /// recicla o numero, e responder pela alca diria "esta" para a geometria
  /// de um modelo que ja morreu.
  [[nodiscard]] bool modelo_na_gpu(std::int32_t alca,
                                   const AcervoDeModelos& acervo) const noexcept;

  [[nodiscard]] const char* ultimo_erro() const noexcept;
  [[nodiscard]] Estatisticas3D estatisticas() const noexcept;

  /// QUANTO DE MEMORIA DE GPU O APARELHO TEM, em bytes. O acervo consulta
  /// antes de aceitar um modelo grande (§21).
  ///
  /// E O TOTAL DA PLACA, E NAO O QUE ESTA LIVRE: perguntar o que esta livre
  /// nao tem resposta confiavel na Vulkan e na Metal, e um numero inventado
  /// seria pior do que nenhum. Zero quando o aparelho nao informa — e ai
  /// quem chama decide pelo proprio teto.
  [[nodiscard]] std::uint64_t memoria_de_gpu() const noexcept;

 private:
  Renderizador3D();
  struct Interno;
  std::unique_ptr<Interno> interno_;
};

}  // namespace aurea::render::tresd

#endif  // AUREA_RENDER_RENDERIZADOR_3D_H
