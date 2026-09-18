// ESTAGIO V1 — SUPERFICIE, SWAPCHAIN E APRESENTACAO.
//
// ================================ O QUE ISTO E =========================
// O caminho COMPLETO de um quadro ate a tela do Android: a janela nativa
// que o `SurfaceProducer` do Flutter entrega, a `VkSurfaceKHR`, a
// swapchain, as imagens, a barreira de layout, o comando de limpeza, o
// semaforo de pronto e o `vkQueuePresentKHR`.
//
// O CONTEUDO DO QUADRO E UMA COR SOLIDA, e de proposito: o V1 existe para
// provar que a TUBULACAO funciona — superficie, swapchain, sincronizacao,
// apresentacao e recriacao. O compositor em shader e o V2. Misturar os
// dois agora deixaria a tela preta sem dizer se o erro foi a cor errada ou
// a swapchain recusada.
//
// ================================ O QUE ELE TRATA =====================
//   VK_ERROR_OUT_OF_DATE_KHR   a swapchain morreu (rotacao, resize, outra
//                              janela na frente): refaz e NAO apresenta.
//   VK_SUBOPTIMAL_KHR          a swapchain ainda serve, mas o tamanho ja
//                              nao bate: APRESENTA e marca para refazer.
//                              Tratar os dois igual perde um quadro bom.
//   superficie destruida       o Flutter avisa antes de a janela sumir
//                              (background); solta a swapchain e espera.
//
// ================================ POSSE ===============================
// TUDO O QUE ESTA CLASSE CRIA, ELA DESTROI — na ordem inversa da criacao e
// com `vkDeviceWaitIdle` antes de soltar a swapchain. Imagem de swapchain
// NAO e destruida: quem e dono dela e a swapchain, e destruir a mao e o
// erro classico que so aparece no fechamento do app.
#ifndef AUREA_RENDER_VULKAN_SUPERFICIE_H
#define AUREA_RENDER_VULKAN_SUPERFICIE_H

#include <cstdint>
#include <string>

#include "backend_vulkan.h"

namespace aurea::render {

enum class EstadoDaSuperficie : std::int32_t {
  sem_superficie = 0,
  pronta = 1,
  erro = 2,
};

struct EstatisticasDaSuperficie {
  std::uint32_t largura = 0;
  std::uint32_t altura = 0;
  std::uint32_t formato = 0;
  std::uint32_t modo_de_apresentacao = 0;
  std::uint32_t imagens = 0;
  std::uint32_t quadros_apresentados = 0;
  std::uint32_t recriacoes = 0;
  std::uint32_t out_of_date = 0;
  std::uint32_t suboptimal = 0;
  std::uint32_t falhas = 0;
  std::uint32_t quadros_descartados = 0;  // adquiridos e nao apresentados
};

class SuperficieVulkan {
 public:
  explicit SuperficieVulkan(DispositivoVulkan& dispositivo) noexcept;
  ~SuperficieVulkan();

  SuperficieVulkan(const SuperficieVulkan&) = delete;
  SuperficieVulkan& operator=(const SuperficieVulkan&) = delete;

  /// ANEXA A JANELA NATIVA E CRIA TUDO. Devolve o motivo, ou string vazia.
  /// [janela] e o `ANativeWindow*` — quem o obtem e a ponte JNI, e o
  /// ponteiro NAO pertence a esta classe: quem o solta e quem o pegou.
  [[nodiscard]] std::string anexar(void* janela, std::uint32_t largura,
                                   std::uint32_t altura) noexcept;

  /// SOLTA A SWAPCHAIN E A SUPERFICIE. Chamado quando o Flutter avisa que
  /// a janela vai sumir (background) — a partir daqui [apresentar] recusa.
  void desanexar() noexcept;

  /// APRESENTA UM QUADRO DE COR SOLIDA. Devolve 0 quando apresentou.
  ///
  /// NEGATIVO NAO E "TRAGEDIA": `-2` significa que a swapchain foi refeita
  /// e o quadro foi descartado — a resposta certa para uma rotacao no meio
  /// do play e desenhar o proximo, e nao derrubar o app.
  [[nodiscard]] std::int32_t apresentar(std::uint32_t cor_argb) noexcept;

  /// APRESENTA UM QUADRO COMPOSTO PELO MOTOR, em RGBA8 premultiplicado.
  ///
  /// E O PASSO QUE FAZ O COMPOSITOR C++ APARECER. `apresentar(cor)` prova
  /// a tubulacao — superficie, swapchain, sincronizacao —, mas o que chega
  /// a tela e uma constante que o Dart escolheu. Aqui o que chega sao os
  /// PIXELS que o `Nucleo` escreveu: eles sobem por um buffer de
  /// transferencia (staging), sao copiados para uma imagem de origem e
  /// ampliados para a swapchain com `vkCmdBlitImage`.
  ///
  /// O BUFFER E A IMAGEM SAO REAPROVEITADOS. Trocar o tamanho do quadro
  /// recria os dois; o mesmo tamanho nao aloca nada. Sem isso seria uma
  /// alocacao de GPU por quadro — o caminho mais rapido para o driver
  /// passar a fragmentar memoria.
  [[nodiscard]] std::int32_t apresentar_imagem(const std::uint8_t* rgba,
                                               std::uint32_t largura,
                                               std::uint32_t altura) noexcept;

  /// REDIMENSIONA. O tamanho vem do `SurfaceProducer`; um valor diferente
  /// do atual marca a swapchain para refazer.
  [[nodiscard]] std::int32_t redimensionar(std::uint32_t largura,
                                           std::uint32_t altura) noexcept;

  [[nodiscard]] EstadoDaSuperficie estado() const noexcept { return estado_; }
  [[nodiscard]] bool pronta() const noexcept {
    return estado_ == EstadoDaSuperficie::pronta;
  }
  [[nodiscard]] const std::string& motivo() const noexcept { return motivo_; }
  [[nodiscard]] const EstatisticasDaSuperficie& estatisticas() const noexcept {
    return stats_;
  }

 private:
  [[nodiscard]] std::string criar_swapchain() noexcept;

  /// CRIA A SWAPCHAIN SE ELA AINDA NAO EXISTE, e devolve o motivo quando
  /// nao da — inclusive quando a resposta e "a superficie ainda nao tem
  /// tamanho".
  ///
  /// A SUPERFICIE DO ANDROID NASCE SEM TAMANHO. No `onSurfaceCreated` a
  /// janela ja existe, mas quem diz o tamanho e o layout, e ele vem
  /// DEPOIS. Criar a swapchain ali dava uma de 0x0 — e o driver de
  /// software chegou a aceitar, com DEZESSETE imagens de zero pixel, o
  /// que so aparece quando alguem le o numero. Por isso a criacao e
  /// PREGUICOSA: quem chama apresentar e que a dispara, quando o tamanho
  /// ja existe.
  /// A janela ainda tem o tamanho da swapchain? Ver o comentario longo no
  /// `.cpp`: e o que impede a tempestade de recriacoes que o
  /// `VK_SUBOPTIMAL_KHR` repetido provoca.
  [[nodiscard]] bool tamanho_mudou() const noexcept;
  [[nodiscard]] std::string garantir_swapchain() noexcept;
  /// CRIA (OU REAPROVEITA) O BUFFER E A IMAGEM DE ORIGEM do quadro
  /// composto. Devolve o motivo, ou string vazia.
  [[nodiscard]] std::string garantir_origem(std::uint32_t largura,
                                            std::uint32_t altura) noexcept;
  void destruir_origem() noexcept;
  void destruir_swapchain() noexcept;
  [[nodiscard]] std::string criar_sincronizacao() noexcept;
  void destruir_sincronizacao() noexcept;
  void anotar_motivo(const std::string& texto) noexcept;

  DispositivoVulkan& dispositivo_;
  EstadoDaSuperficie estado_ = EstadoDaSuperficie::sem_superficie;
  std::string motivo_;

  // HANDLES COMO `void*`, e nao como tipos do Vulkan: este cabecalho
  // precisa ser incluivel onde o `vulkan.h` nao existe (o PC), e o `api.cpp`
  // e portatil. A conversao acontece toda dentro do `.cpp`.
  void* superficie_ = nullptr;   // VkSurfaceKHR
  void* swapchain_ = nullptr;    // VkSwapchainKHR
  void* imagens_ = nullptr;      // std::vector<VkImage>*
  void* vistas_ = nullptr;       // std::vector<VkImageView>*
  void* pool_ = nullptr;         // VkCommandPool
  void* comandos_ = nullptr;     // std::vector<VkCommandBuffer>*
  void* semaforo_imagem_ = nullptr;   // VkSemaphore
  void* semaforo_pronto_ = nullptr;   // VkSemaphore
  void* cerca_ = nullptr;             // VkFence
  void* origem_imagem_ = nullptr;     // VkImage   — o quadro do motor
  void* origem_memoria_ = nullptr;    // VkDeviceMemory da imagem
  void* origem_buffer_ = nullptr;     // VkBuffer  — o staging
  void* origem_buffer_memoria_ = nullptr;  // VkDeviceMemory do staging
  std::uint32_t origem_largura_ = 0;
  std::uint32_t origem_altura_ = 0;

  std::uint32_t largura_ = 0;
  std::uint32_t altura_ = 0;
  std::uint32_t formato_ = 0;
  std::uint32_t modo_ = 0;
  bool precisa_refazer_ = false;
  EstatisticasDaSuperficie stats_{};
};

/// O PREVIEW VULKAN — UM SO PARA O APP INTEIRO.
///
/// SUBIR UM DISPOSITIVO VULKAN CUSTA DEZENAS DE MILISSEGUNDOS num celular,
/// e a superficie muda varias vezes na vida do app (background, rotacao). O
/// singleton existe para que o dispositivo suba UMA vez e a superficie seja
/// criada e destruida quantas vezes o Flutter pedir — e nao para que o
/// estado seja global por conveniencia.
///
/// O ACESSO E PELA THREAD DA UI. Os dois lados que chamam (o JNI, quando a
/// superficie nasce, e o Dart, a cada quadro) sao a mesma thread, e a
/// sincronizacao com a GPU e feita por cerca e semaforo do Vulkan, e nao
/// por trava de CPU.
class PreviewVulkan {
 public:
  [[nodiscard]] static PreviewVulkan& instancia() noexcept;

  /// ANEXA A JANELA. Devolve o motivo da falha, ou string vazia.
  [[nodiscard]] std::string anexar(void* janela, std::uint32_t largura,
                                   std::uint32_t altura) noexcept;
  void desanexar() noexcept;

  [[nodiscard]] std::int32_t apresentar(std::uint32_t cor_argb) noexcept;
  [[nodiscard]] std::int32_t apresentar_imagem(const std::uint8_t* rgba,
                                               std::uint32_t largura,
                                               std::uint32_t altura) noexcept;
  [[nodiscard]] std::int32_t redimensionar(std::uint32_t largura,
                                           std::uint32_t altura) noexcept;

  [[nodiscard]] std::int32_t estado() const noexcept;
  [[nodiscard]] std::string motivo() const noexcept;
  [[nodiscard]] EstatisticasDaSuperficie estatisticas() const noexcept;

 private:
  PreviewVulkan() = default;
  ~PreviewVulkan() = default;
  PreviewVulkan(const PreviewVulkan&) = delete;
  PreviewVulkan& operator=(const PreviewVulkan&) = delete;

  DispositivoVulkan dispositivo_;
  SuperficieVulkan superficie_{dispositivo_};
  bool dispositivo_aberto_ = false;
};

/// A COR SOLIDA DO V1, EM ARGB — a mesma ordem do `Color.value` do Flutter.
/// Existe para o teste provar que o canal certo chegou ao canal certo: uma
/// troca de canais aqui vira uma tela azul onde devia ser vermelha.
[[nodiscard]] std::uint32_t cor_de_teste(std::uint32_t semente) noexcept;

}  // namespace aurea::render

#endif  // AUREA_RENDER_VULKAN_SUPERFICIE_H
