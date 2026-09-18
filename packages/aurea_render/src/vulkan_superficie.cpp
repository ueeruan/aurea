#include "vulkan_superficie.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <vector>

#if defined(__ANDROID__)
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

#include <android/log.h>
#include <android/native_window.h>
#endif

namespace aurea::render {

std::uint32_t cor_de_teste(std::uint32_t semente) noexcept {
  // CANAIS DIFERENTES EM CADA COMPONENTE, para uma troca de R e B nao
  // passar despercebida: uma cor cinza pareceria certa com os canais
  // invertidos, e uma cor com os quatro valores distintos nao.
  const std::uint32_t r = (semente * 37U) & 0xFFU;
  const std::uint32_t g = 0x40U + ((semente * 11U) & 0x7FU);
  const std::uint32_t b = 0xFFU - ((semente * 53U) & 0x7FU);
  return 0xFF000000U | (r << 16) | (g << 8) | b;
}

PreviewVulkan& PreviewVulkan::instancia() noexcept {
  // FUNCAO ESTATICA LOCAL: a construcao e garantidamente unica e
  // thread-safe na primeira chamada, sem ordem de inicializacao global
  // para dar errado.
  static PreviewVulkan unico;
  return unico;
}

std::string PreviewVulkan::anexar(void* janela, std::uint32_t largura,
                                  std::uint32_t altura) noexcept {
  if (!dispositivo_aberto_) {
    const std::string erro = dispositivo_.abrir();
    if (!erro.empty()) return erro;
    dispositivo_aberto_ = true;
  }
  return superficie_.anexar(janela, largura, altura);
}

void PreviewVulkan::desanexar() noexcept { superficie_.desanexar(); }

std::int32_t PreviewVulkan::apresentar(std::uint32_t cor) noexcept {
  return superficie_.apresentar(cor);
}

std::int32_t PreviewVulkan::apresentar_imagem(const std::uint8_t* rgba,
                                              std::uint32_t l,
                                              std::uint32_t a) noexcept {
  if (!dispositivo_aberto_) return -1;
  return superficie_.apresentar_imagem(rgba, l, a);
}

std::int32_t PreviewVulkan::redimensionar(std::uint32_t l,
                                          std::uint32_t a) noexcept {
  return superficie_.redimensionar(l, a);
}

std::int32_t PreviewVulkan::estado() const noexcept {
  return static_cast<std::int32_t>(superficie_.estado());
}

std::string PreviewVulkan::motivo() const noexcept {
  const std::string& m = superficie_.motivo();
  return m.empty() ? std::string("ok") : m;
}

EstatisticasDaSuperficie PreviewVulkan::estatisticas() const noexcept {
  return superficie_.estatisticas();
}

#if defined(__ANDROID__)
namespace {

constexpr const char* kEtiqueta = "AureaVulkan";

/// QUANTOS QUADROS PODEM ESTAR EM VOO. UM.
///
/// Um quadro em voo e o suficiente para o V1 e e o que menos memoria e
/// menos sincronizacao exige. Subir para dois ou tres e o que se faz
/// quando o custo de compor passa a importar — e no V1 nao passa: o
/// quadro e uma limpeza de cor.
constexpr std::uint32_t kQuadrosEmVoo = 1;

const char* nome_do_formato(VkFormat f) noexcept {
  switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: return "RGBA8";
    case VK_FORMAT_R8G8B8A8_SRGB: return "RGBA8 sRGB";
    case VK_FORMAT_B8G8R8A8_UNORM: return "BGRA8";
    case VK_FORMAT_B8G8R8A8_SRGB: return "BGRA8 sRGB";
    case VK_FORMAT_R5G6B5_UNORM_PACK16: return "RGB565";
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "RGB10A2";
    default: return "outro";
  }
}

/// A FORMA DE APRESENTAR. Sem VSync (`IMMEDIATE`) entrega o quadro o mais
/// rapido possivel e rasga; com FIFO espera o retorno do painel.
///
/// O V1 ESCOLHE `FIFO` QUANDO EXISTE: o objetivo do Aurea e gastar o
/// MINIMO de GPU, e apresentar a 200 Hz num painel de 60 so aquece. O
/// `MAILBOX` fica em segundo porque descarta quadros velhos em vez de
/// esperar — melhor latencia, mesmo custo.
VkPresentModeKHR escolher_modo(const std::vector<VkPresentModeKHR>& modos) noexcept {
  for (const auto preferido : {VK_PRESENT_MODE_MAILBOX_KHR,
                               VK_PRESENT_MODE_FIFO_KHR}) {
    if (std::find(modos.begin(), modos.end(), preferido) != modos.end()) {
      return preferido;
    }
  }
  // FIFO E O UNICO QUE A ESPECIFICACAO OBRIGA A EXISTIR; se nem ele
  // apareceu, o aparelho esta fora da especificacao e o primeiro da lista
  // e a resposta menos ruim.
  return modos.empty() ? VK_PRESENT_MODE_FIFO_KHR : modos.front();
}
}  // namespace

SuperficieVulkan::SuperficieVulkan(DispositivoVulkan& d) noexcept
    : dispositivo_(d) {}

SuperficieVulkan::~SuperficieVulkan() { desanexar(); }

void SuperficieVulkan::anotar_motivo(const std::string& texto) noexcept {
  motivo_ = texto;
  if (!texto.empty()) {
    __android_log_print(ANDROID_LOG_WARN, kEtiqueta, "%s", texto.c_str());
  }
}

std::string SuperficieVulkan::anexar(void* janela, std::uint32_t largura,
                                     std::uint32_t altura) noexcept {
  if (janela == nullptr) return "janela nativa nula";
  if (largura == 0 || altura == 0) return "tamanho zero";
  if (!dispositivo_.viva()) return "dispositivo Vulkan nao esta vivo";

  // TROCAR DE JANELA E UM CASO REAL: o Flutter destroi e recria a
  // superficie ao voltar do segundo plano. Soltar a antiga antes de criar
  // a nova e o que impede duas superficies vivas para a mesma janela.
  desanexar();

  try {
    const auto instancia = deVoid<VkInstance>(dispositivo_.instancia());
    VkAndroidSurfaceCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    info.window = static_cast<ANativeWindow*>(janela);

    // O `vndk` DA JANELA: `ANativeWindow_fromSurface` ja tomou uma
    // referencia a mais (ver `jni_android.cpp`). A superficie Vulkan usa a
    // janela enquanto existir, e quem chama `desanexar` e quem solta a
    // referencia — a ordem esta escrita la.
    VkSurfaceKHR superficie = VK_NULL_HANDLE;
    const VkResult r = vkCreateAndroidSurfaceKHR(instancia, &info, nullptr,
                                                 &superficie);
    if (r != VK_SUCCESS || superficie == VK_NULL_HANDLE) {
      anotar_motivo("vkCreateAndroidSurfaceKHR falhou");
      estado_ = EstadoDaSuperficie::erro;
      return motivo_;
    }
    superficie_ = paraVoid(superficie);
    largura_ = largura;
    altura_ = altura;

    // A SUPERFICIE ESTA DE PE. A SWAPCHAIN VEM DEPOIS, quando houver
    // tamanho — ver `garantir_swapchain`.
    estado_ = EstadoDaSuperficie::pronta;
    anotar_motivo("");
    return {};
  } catch (const std::exception& e) {
    anotar_motivo(std::string("excecao ao anexar: ") + e.what());
    estado_ = EstadoDaSuperficie::erro;
    return motivo_;
  } catch (...) {
    anotar_motivo("excecao desconhecida ao anexar");
    estado_ = EstadoDaSuperficie::erro;
    return motivo_;
  }
}

void SuperficieVulkan::desanexar() noexcept {
  destruir_swapchain();
  // A ORIGEM E NOSSA, NAO DA SWAPCHAIN. Ela sobrevive a uma troca de
  // tamanho (e por isso o buffer de staging nao e alocado por quadro) e
  // some junto com a superficie, nao com a swapchain.
  destruir_origem();
  if (superficie_ != nullptr && dispositivo_.viva()) {
    vkDestroySurfaceKHR(deVoid<VkInstance>(dispositivo_.instancia()),
                        deVoid<VkSurfaceKHR>(superficie_), nullptr);
  }
  superficie_ = paraVoid(nullptr);
  estado_ = EstadoDaSuperficie::sem_superficie;
  precisa_refazer_ = false;
}

void SuperficieVulkan::destruir_swapchain() noexcept {
  if (!dispositivo_.viva()) {
    swapchain_ = paraVoid(nullptr);
    imagens_ = nullptr;
    vistas_ = nullptr;
    pool_ = paraVoid(nullptr);
    comandos_ = nullptr;
    return;
  }
  const auto dev = deVoid<VkDevice>(dispositivo_.dispositivo());

  // ESPERA A GPU PARAR ANTES DE SOLTAR QUALQUER COISA. Sem isto, um
  // `vkDestroySwapchainKHR` pode cair no meio de um quadro que a fila
  // ainda esta lendo — e o resultado e corrompido ou um erro de driver,
  // dependendo da sorte.
  vkDeviceWaitIdle(dev);

  destruir_sincronizacao();

  if (comandos_ != nullptr) {
    delete static_cast<std::vector<VkCommandBuffer>*>(comandos_);
    comandos_ = nullptr;
  }
  if (pool_ != nullptr) {
    vkDestroyCommandPool(dev, deVoid<VkCommandPool>(pool_), nullptr);
    pool_ = paraVoid(nullptr);
  }
  // AS VISTAS SAO NOSSAS; AS IMAGENS NAO. A imagem pertence a swapchain —
  // destruir a mao e o erro classico que so aparece no fechamento do app.
  if (vistas_ != nullptr) {
    auto* vistas = static_cast<std::vector<VkImageView>*>(vistas_);
    for (const VkImageView v : *vistas) vkDestroyImageView(dev, v, nullptr);
    delete vistas;
    vistas_ = nullptr;
  }
  if (imagens_ != nullptr) {
    delete static_cast<std::vector<VkImage>*>(imagens_);
    imagens_ = nullptr;
  }
  if (swapchain_ != nullptr) {
    vkDestroySwapchainKHR(dev, deVoid<VkSwapchainKHR>(swapchain_),
                          nullptr);
    swapchain_ = paraVoid(nullptr);
  }
}

void SuperficieVulkan::destruir_sincronizacao() noexcept {
  if (!dispositivo_.viva()) return;
  const auto dev = deVoid<VkDevice>(dispositivo_.dispositivo());
  if (cerca_ != nullptr) {
    vkDestroyFence(dev, deVoid<VkFence>(cerca_), nullptr);
    cerca_ = paraVoid(nullptr);
  }
  for (void** sem : {&semaforo_pronto_, &semaforo_imagem_}) {
    if (*sem != nullptr) {
      vkDestroySemaphore(dev, deVoid<VkSemaphore>(*sem), nullptr);
      *sem = nullptr;
    }
  }
}

std::string SuperficieVulkan::criar_sincronizacao() noexcept {
  const auto dev = deVoid<VkDevice>(dispositivo_.dispositivo());
  VkSemaphoreCreateInfo s{};
  s.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo f{};
  f.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  // A CERCA NASCE SINALIZADA. Sem isto, o primeiro `vkWaitForFences` do
  // primeiro quadro espera por um quadro que nunca foi submetido — o app
  // congela na abertura, e so na abertura, que e o pior lugar para
  // depurar.
  f.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  VkSemaphore img = VK_NULL_HANDLE, pronto = VK_NULL_HANDLE;
  VkFence cerca = VK_NULL_HANDLE;
  if (vkCreateSemaphore(dev, &s, nullptr, &img) != VK_SUCCESS ||
      vkCreateSemaphore(dev, &s, nullptr, &pronto) != VK_SUCCESS ||
      vkCreateFence(dev, &f, nullptr, &cerca) != VK_SUCCESS) {
    if (img != VK_NULL_HANDLE) vkDestroySemaphore(dev, img, nullptr);
    if (pronto != VK_NULL_HANDLE) vkDestroySemaphore(dev, pronto, nullptr);
    if (cerca != VK_NULL_HANDLE) vkDestroyFence(dev, cerca, nullptr);
    return "sincronizacao recusada";
  }
  semaforo_imagem_ = paraVoid(img);
  semaforo_pronto_ = paraVoid(pronto);
  cerca_ = paraVoid(cerca);
  return {};
}

/// A JANELA AINDA TEM O TAMANHO DA SWAPCHAIN?
///
/// `VK_SUBOPTIMAL_KHR` quer dizer "ainda serve, mas o tamanho ja nao
/// bate". O rotulo e honesto e a acao obvia — refazer —, mas o driver
/// repete o aviso ENQUANTO os tamanhos nao batem, e cada refazer custa
/// `vkDeviceWaitIdle` mais uma swapchain nova. Com o aviso virado em
/// "refaz sempre", o preview entra numa tempestade: sete recriacoes em
/// dois quadros, e algumas delas caindo no meio de uma mudanca de
/// janela — falhando.
///
/// A pergunta certa nao e "o driver reclamou?", e "ha o que consertar?".
/// Se o tamanho ja bate, o aviso e ruido e a swapchain fica.
bool SuperficieVulkan::tamanho_mudou() const noexcept {
  if (superficie_ == nullptr || !dispositivo_.viva()) return false;
  VkSurfaceCapabilitiesKHR capacidades{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          deVoid<VkPhysicalDevice>(dispositivo_.fisico()),
          deVoid<VkSurfaceKHR>(superficie_), &capacidades) != VK_SUCCESS) {
    return false;
  }
  // `0xFFFFFFFF` e "escolha voce": o tamanho e o que esta la, e nao ha
  // divergencia a corrigir.
  if (capacidades.currentExtent.width == 0xFFFFFFFFU) return false;
  return capacidades.currentExtent.width != largura_ ||
         capacidades.currentExtent.height != altura_;
}

std::string SuperficieVulkan::garantir_swapchain() noexcept {
  if (swapchain_ != nullptr) return {};
  if (superficie_ == nullptr) return "sem superficie";
  const std::string erro = criar_swapchain();
  if (!erro.empty()) return erro;
  __android_log_print(ANDROID_LOG_INFO, kEtiqueta,
                      "swapchain %ux%u %s (%u imagens, modo %d)", largura_,
                      altura_, nome_do_formato(static_cast<VkFormat>(formato_)),
                      stats_.imagens, static_cast<int>(modo_));
  return {};
}

std::string SuperficieVulkan::criar_swapchain() noexcept {
  const auto dev = deVoid<VkDevice>(dispositivo_.dispositivo());
  const auto fisico = deVoid<VkPhysicalDevice>(dispositivo_.fisico());
  const auto superficie = deVoid<VkSurfaceKHR>(superficie_);

  VkSurfaceCapabilitiesKHR capacidades{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(fisico, superficie,
                                                &capacidades) != VK_SUCCESS) {
    anotar_motivo("vkGetPhysicalDeviceSurfaceCapabilitiesKHR falhou");
    return motivo_;
  }

  // O TAMANHO VEM DA SUPERFICIE QUANDO ELA MANDA. `currentExtent` igual a
  // 0xFFFFFFFF significa "voce escolhe"; qualquer outro valor e o tamanho
  // real da janela, e usar o do `SurfaceProducer` ali daria uma imagem
  // esticada em aparelho com barra de navegacao.
  //
  // E O TAMANHO E SEMPRE PRESO AOS LIMITES. Um `currentExtent` de zero
  // chegou a ser aceito pelo driver de software, que criou uma swapchain
  // de 0x0 com dezessete imagens — um objeto legal e inutil, que nao
  // aparecia como erro em lugar nenhum. Zero aqui nao e "escolha o que
  // quiser": e "a janela ainda nao tem tamanho", e a resposta certa e
  // RECUSAR e tentar de novo no proximo quadro.
  VkExtent2D pedido = capacidades.currentExtent;
  if (pedido.width == 0xFFFFFFFFU) {
    pedido = VkExtent2D{largura_, altura_};
  }
  const VkExtent2D extensao{
      std::clamp(pedido.width, capacidades.minImageExtent.width,
                 capacidades.maxImageExtent.width),
      std::clamp(pedido.height, capacidades.minImageExtent.height,
                 capacidades.maxImageExtent.height)};
  if (extensao.width == 0 || extensao.height == 0) {
    // NAO E ERRO: e cedo. A janela ganha tamanho no proximo layout, e
    // quem chamou tenta de novo.
    return "";
  }

  std::uint32_t quantos_formatos = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(fisico, superficie, &quantos_formatos,
                                       nullptr);
  if (quantos_formatos == 0) {
    anotar_motivo("a superficie nao oferece formato nenhum");
    return motivo_;
  }
  std::vector<VkSurfaceFormatKHR> formatos(quantos_formatos);
  vkGetPhysicalDeviceSurfaceFormatsKHR(fisico, superficie, &quantos_formatos,
                                       formatos.data());

  // O FORMATO: o primeiro de 8 bits por canal, e nao o primeiro da lista.
  // O primeiro costuma ser BGRA8, e o compositor do V2 vai escrever RGBA —
  // escolher o que casa evita uma troca de canais que so aparece como cor
  // errada na tela.
  VkSurfaceFormatKHR escolhido = formatos.front();
  for (const auto& f : formatos) {
    if (f.format == VK_FORMAT_R8G8B8A8_UNORM ||
        f.format == VK_FORMAT_R8G8B8A8_SRGB) {
      escolhido = f;
      break;
    }
  }
  for (const auto& f : formatos) {
    if (f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      escolhido.colorSpace = f.colorSpace;
      break;
    }
  }

  std::uint32_t quantos_modos = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(fisico, superficie, &quantos_modos,
                                            nullptr);
  std::vector<VkPresentModeKHR> modos(quantos_modos);
  if (quantos_modos > 0) {
    vkGetPhysicalDeviceSurfacePresentModesKHR(fisico, superficie,
                                              &quantos_modos, modos.data());
  }
  const VkPresentModeKHR modo = escolher_modo(modos);

  // UMA A MAIS QUE O MINIMO, E NUNCA ABAIXO DE DOIS. Com uma imagem so, a
  // GPU fica esperando a tela devolver antes de comecar o proximo quadro.
  std::uint32_t quantas_imagens = capacidades.minImageCount + 1;
  if (capacidades.maxImageCount > 0 &&
      quantas_imagens > capacidades.maxImageCount) {
    quantas_imagens = capacidades.maxImageCount;
  }
  quantas_imagens = std::max(quantas_imagens, 2U);

  VkSwapchainCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface = superficie;
  info.minImageCount = quantas_imagens;
  info.imageFormat = escolhido.format;
  info.imageColorSpace = escolhido.colorSpace;
  info.imageExtent = extensao;
  info.imageArrayLayers = 1;
  // `TRANSFER_DST` E OBRIGATORIO NO V2, quando o compositor copiar uma
  // imagem renderizada para a da swapchain, e nao custa nada reservar
  // agora. `COLOR_ATTACHMENT` e o que o `vkCmdClearColorImage` do V1
  // exige.
  info.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.preTransform = capacidades.currentTransform;
  // SEM CANAL ALFA NA COMPOSICAO: o que esta atras do preview e a UI do
  // Flutter, e uma swapchain com alfa faria o painel aparecer por buraco.
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = modo;
  info.clipped = VK_TRUE;
  info.oldSwapchain = VK_NULL_HANDLE;

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  const VkResult r = vkCreateSwapchainKHR(dev, &info, nullptr, &swapchain);
  if (r != VK_SUCCESS) {
    anotar_motivo("vkCreateSwapchainKHR recusou (codigo " +
                  std::to_string(static_cast<int>(r)) + ")");
    return motivo_;
  }
  swapchain_ = paraVoid(swapchain);
  stats_.recriacoes++;
  formato_ = escolhido.format;
  stats_.formato = formato_;
  // AS ESTATISTICAS SAO PREENCHIDAS AQUI, e nao lidas de `largura_` na
  // hora de responder: o placar e um retrato do que foi CRIADO. Sem estas
  // quatro linhas o relatorio dizia "0x0 formato 0" com uma swapchain
  // viva de 640x360 — e um numero zerado num relatorio parece um defeito
  // do motor, nao do relatorio.
  stats_.largura = extensao.width;
  stats_.altura = extensao.height;
  modo_ = static_cast<std::uint32_t>(modo);
  stats_.modo_de_apresentacao = modo_;
  largura_ = extensao.width;
  altura_ = extensao.height;

  // ------------------------------- as imagens e as vistas
  std::uint32_t quantas = 0;
  if (vkGetSwapchainImagesKHR(dev, swapchain, &quantas, nullptr) !=
          VK_SUCCESS ||
      quantas == 0) {
    anotar_motivo("vkGetSwapchainImagesKHR nao devolveu imagens");
    return motivo_;
  }
  auto* imagens = new std::vector<VkImage>(quantas);
  if (vkGetSwapchainImagesKHR(dev, swapchain, &quantas, imagens->data()) !=
      VK_SUCCESS) {
    delete imagens;
    anotar_motivo("vkGetSwapchainImagesKHR falhou na segunda chamada");
    return motivo_;
  }
  imagens_ = imagens;
  stats_.imagens = quantas;

  auto* vistas = new std::vector<VkImageView>();
  vistas->reserve(quantas);
  for (const VkImage img : *imagens) {
    VkImageViewCreateInfo v{};
    v.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    v.image = img;
    v.viewType = VK_IMAGE_VIEW_TYPE_2D;
    v.format = escolhido.format;
    v.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY,
                    VK_COMPONENT_SWIZZLE_IDENTITY};
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView vista = VK_NULL_HANDLE;
    if (vkCreateImageView(dev, &v, nullptr, &vista) != VK_SUCCESS) {
      for (const VkImageView feitas : *vistas) {
        vkDestroyImageView(dev, feitas, nullptr);
      }
      delete vistas;
      anotar_motivo("vkCreateImageView falhou");
      return motivo_;
    }
    vistas->push_back(vista);
  }
  vistas_ = vistas;

  // ------------------------------- o pool e o comando
  VkCommandPoolCreateInfo p{};
  p.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  // O POOL E POR FAMILIA DE FILA, e o comando tem de ser gravado de novo a
  // cada quadro: o alvo muda (a imagem adquirida) e a barreira de layout
  // muda junto.
  p.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
  p.queueFamilyIndex = dispositivo_.familia_da_fila();
  VkCommandPool pool = VK_NULL_HANDLE;
  if (vkCreateCommandPool(dev, &p, nullptr, &pool) != VK_SUCCESS) {
    anotar_motivo("vkCreateCommandPool falhou");
    return motivo_;
  }
  pool_ = paraVoid(pool);

  auto* comandos = new std::vector<VkCommandBuffer>(quantas, VK_NULL_HANDLE);
  VkCommandBufferAllocateInfo a{};
  a.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  a.commandPool = pool;
  a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  a.commandBufferCount = quantas;
  if (vkAllocateCommandBuffers(dev, &a, comandos->data()) != VK_SUCCESS) {
    delete comandos;
    anotar_motivo("vkAllocateCommandBuffers falhou");
    return motivo_;
  }
  comandos_ = comandos;

  const std::string erro = criar_sincronizacao();
  if (!erro.empty()) {
    anotar_motivo(erro);
    return motivo_;
  }

  // A SWAPCHAIN NASCE APONTANDO PARA UM QUADRO QUE AINDA NAO FOI DESENHADO.
  // O `vkAcquireNextImageKHR` do primeiro quadro sinaliza um semaforo que
  // ninguem consumiu, e alguns drivers reclamam. Marcar como "precisa
  // refazer" faz o primeiro `apresentar` adquirir do zero, sem semaforo
  // pendurado.
  precisa_refazer_ = false;
  return {};
}

std::int32_t SuperficieVulkan::apresentar(std::uint32_t cor_argb) noexcept {
  if (estado_ != EstadoDaSuperficie::pronta || superficie_ == nullptr) {
    return -1;  // sem superficie: nao ha o que apresentar
  }
  // A SWAPCHAIN NASCE AQUI NA PRIMEIRA VEZ, quando a janela ja tem
  // tamanho. Recusa silenciosa (-1) enquanto nao tiver.
  if (swapchain_ == nullptr && !garantir_swapchain().empty()) {
    ++stats_.quadros_descartados;
    return -1;
  }
  // A SWAPCHAIN PEDIU PARA SER REFAZIDA (rotacao, resize, a janela voltou
  // do segundo plano). REFAZ AQUI, e nao no proximo `apresentar`: deixar a
  // marcacao para depois faria todo quadro seguinte devolver OUT_OF_DATE
  // em cascata, e o placar de falhas subiria sem haver falha nenhuma.
  if (precisa_refazer_) {
    precisa_refazer_ = false;
    destruir_swapchain();
    const std::string erro = garantir_swapchain();
    if (!erro.empty() || swapchain_ == nullptr) {
      ++stats_.quadros_descartados;
      return -1;
    }
  }
  const auto dev = deVoid<VkDevice>(dispositivo_.dispositivo());
  const auto fila = deVoid<VkQueue>(dispositivo_.fila());

  try {
    // 1. ESPERA O QUADRO ANTERIOR. A cerca nasce sinalizada, entao o
    //    primeiro quadro nao espera por nada.
    const VkFence cerca = deVoid<VkFence>(cerca_);
    vkWaitForFences(dev, 1, &cerca, VK_TRUE, UINT64_MAX);

    // 2. A IMAGEM. `OUT_OF_DATE` aqui e rotacao, resize ou a janela indo
    //    para tras: refaz a swapchain e DESCARTA o quadro — nao ha o que
    //    apresentar, e insistir daria erro em cascata.
    std::uint32_t indice = 0;
    const VkResult adquiriu = vkAcquireNextImageKHR(
        dev, deVoid<VkSwapchainKHR>(swapchain_), UINT64_MAX,
        deVoid<VkSemaphore>(semaforo_imagem_), VK_NULL_HANDLE, &indice);
    if (adquiriu == VK_ERROR_OUT_OF_DATE_KHR) {
      ++stats_.out_of_date;
      stats_.quadros_descartados++;
      precisa_refazer_ = true;
      return -2;
    }
    if (adquiriu == VK_SUBOPTIMAL_KHR) {
      // AINDA SERVE: descartar aqui perderia um quadro bom por causa de
      // um aviso.
      ++stats_.suboptimal;
      // SO REFAZ SE HOUVER O QUE REFAZER. O aviso se repete
      // enquanto o tamanho nao bate, e cada refazer custa um
      // `vkDeviceWaitIdle` mais uma swapchain nova — a tempestade
      // de recriacoes que este `if` evita. Ver [tamanho_mudou].
      if (tamanho_mudou()) precisa_refazer_ = true;
    } else if (adquiriu != VK_SUCCESS) {
      ++stats_.falhas;
      return -3;
    }

    // 3. SO AGORA A CERCA E REARMADA. Rearmar antes do `acquire` deixaria
    //    uma espera por um quadro que nunca foi submetido.
    vkResetFences(dev, 1, &cerca);

    auto* imagens = static_cast<std::vector<VkImage>*>(imagens_);
    auto* comandos = static_cast<std::vector<VkCommandBuffer>*>(comandos_);
    if (indice >= imagens->size() || indice >= comandos->size()) {
      ++stats_.falhas;
      return -4;
    }
    const VkCommandBuffer cmd = (*comandos)[indice];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo b{};
    b.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &b) != VK_SUCCESS) {
      ++stats_.falhas;
      return -5;
    }

    // 4. A BARREIRA DE ENTRADA. A imagem vem de `PRESENT_SRC` (ou
    //    `UNDEFINED`, na primeira vez) e vai para anexo de cor. Sem a
    //    barreira, o conteudo antigo ainda esta sendo lido pela tela
    //    quando a limpeza comeca — e o rasgo aparece so em aparelho rapido.
    VkImageMemoryBarrier antes{};
    antes.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    antes.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    antes.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    antes.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    antes.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    antes.image = (*imagens)[indice];
    antes.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    antes.srcAccessMask = 0;
    antes.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &antes);

    // 5. A LIMPEZA. `UNDEFINED` no layout antigo e o certo aqui: a
    //    limpeza escreve TODOS os pixels, entao nao ha conteudo a
    //    preservar — e declarar isso deixa o driver descartar a leitura.
    VkClearColorValue cor{};
    cor.float32[0] = static_cast<float>((cor_argb >> 16) & 0xFFU) / 255.0F;
    cor.float32[1] = static_cast<float>((cor_argb >> 8) & 0xFFU) / 255.0F;
    cor.float32[2] = static_cast<float>(cor_argb & 0xFFU) / 255.0F;
    cor.float32[3] = static_cast<float>((cor_argb >> 24) & 0xFFU) / 255.0F;
    VkImageSubresourceRange faixa{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cmd, (*imagens)[indice],
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cor, 1,
                         &faixa);

    // 6. A BARREIRA DE SAIDA: de destino de transferencia para
    //    apresentacao. Esta e a que a especificacao exige antes do
    //    `present`, e a que se esquece.
    VkImageMemoryBarrier depois = antes;
    depois.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    depois.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    depois.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    depois.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &depois);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
      ++stats_.falhas;
      return -6;
    }

    VkSubmitInfo sub{};
    sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const VkSemaphore esperar[] = {deVoid<VkSemaphore>(semaforo_imagem_)};
    const VkPipelineStageFlags estagios[] = {
        VK_PIPELINE_STAGE_TRANSFER_BIT};
    sub.waitSemaphoreCount = 1;
    sub.pWaitSemaphores = esperar;
    sub.pWaitDstStageMask = estagios;
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    const VkSemaphore sinalizar[] = {deVoid<VkSemaphore>(semaforo_pronto_)};
    sub.signalSemaphoreCount = 1;
    sub.pSignalSemaphores = sinalizar;
    if (vkQueueSubmit(fila, 1, &sub, cerca) != VK_SUCCESS) {
      ++stats_.falhas;
      return -7;
    }

    VkPresentInfoKHR ap{};
    ap.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    ap.waitSemaphoreCount = 1;
    ap.pWaitSemaphores = sinalizar;
    const VkSwapchainKHR alvo = deVoid<VkSwapchainKHR>(swapchain_);
    ap.swapchainCount = 1;
    ap.pSwapchains = &alvo;
    ap.pImageIndices = &indice;

    const VkResult apresentou = vkQueuePresentKHR(fila, &ap);
    if (apresentou == VK_ERROR_OUT_OF_DATE_KHR) {
      ++stats_.out_of_date;
      precisa_refazer_ = true;
      return -2;
    }
    if (apresentou == VK_SUBOPTIMAL_KHR) {
      ++stats_.suboptimal;
      // SO REFAZ SE HOUVER O QUE REFAZER. O aviso se repete
      // enquanto o tamanho nao bate, e cada refazer custa um
      // `vkDeviceWaitIdle` mais uma swapchain nova — a tempestade
      // de recriacoes que este `if` evita. Ver [tamanho_mudou].
      if (tamanho_mudou()) precisa_refazer_ = true;
    } else if (apresentou != VK_SUCCESS) {
      ++stats_.falhas;
      return -8;
    }

    ++stats_.quadros_apresentados;
    return 0;
  } catch (const std::exception&) {
    ++stats_.falhas;
    return -9;
  } catch (...) {
    ++stats_.falhas;
    return -10;
  }
}

// ======================== V1.1: O QUADRO DO MOTOR ======================
//
// O QUE FALTAVA PARA O COMPOSITOR C++ APARECER. Ate aqui o quadro que
// chegava a tela era uma constante escolhida pelo Dart: a tubulacao
// (superficie, swapchain, sincronizacao, apresentacao) estava provada, e
// nada do motor. Aqui os PIXELS que o `Nucleo` escreveu sobem para a GPU.
//
// O CAMINHO, e por que ele e este:
//
//   memoria do motor
//     -> vkMapMemory + memcpy        um buffer de transferencia (staging)
//     -> vkCmdCopyBufferToImage      uma imagem de origem RGBA8
//     -> vkCmdBlitImage (linear)     a imagem da swapchain
//     -> vkQueuePresentKHR           a tela
//
// PODERIA SER sem o staging? Nao: buffer e imagem sao espacos diferentes,
// e a copia entre eles e o unico caminho. PODERIA SER `vkCmdCopyImage`
// direto? Nao: o tamanho do quadro do motor e o da superficie quase nunca
// coincidem, e o copy exige dimensoes iguais. E o BLIT que escala — e
// escalar e o comportamento certo para um preview que muda de tamanho com
// a rotacao.

std::string SuperficieVulkan::garantir_origem(std::uint32_t largura,
                                              std::uint32_t altura) noexcept {
  if (largura == 0 || altura == 0) return "quadro sem tamanho";
  if (origem_imagem_ != nullptr && origem_largura_ == largura &&
      origem_altura_ == altura) {
    return {};  // mesmo tamanho: nada a alocar
  }
  destruir_origem();
  if (!dispositivo_.viva()) return "dispositivo morto";
  const auto dev = deVoid<VkDevice>(dispositivo_.dispositivo());

  // A IMAGEM DE ORIGEM, em RGBA8 linear: e o que o compositor escreve, e
  // nao ha conversao no meio do caminho — converter aqui custaria uma
  // passada de GPU por quadro para corrigir um dado que ja esta certo.
  VkImageCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = VK_FORMAT_R8G8B8A8_UNORM;
  ci.extent = {largura, altura, 1};
  ci.mipLevels = 1;
  ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage imagem = VK_NULL_HANDLE;
  if (vkCreateImage(dev, &ci, nullptr, &imagem) != VK_SUCCESS) {
    return "imagem de origem recusada";
  }
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(dev, imagem, &req);
  VkMemoryAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  ai.allocationSize = req.size;
  ai.memoryTypeIndex = dispositivo_.tipo_de_memoria(
      req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  VkDeviceMemory memoria = VK_NULL_HANDLE;
  if (ai.memoryTypeIndex == kTipoDeMemoriaInvalido ||
      vkAllocateMemory(dev, &ai, nullptr, &memoria) != VK_SUCCESS) {
    vkDestroyImage(dev, imagem, nullptr);
    return "sem memoria de GPU para a imagem de origem";
  }
  if (vkBindImageMemory(dev, imagem, memoria, 0) != VK_SUCCESS) {
    vkDestroyImage(dev, imagem, nullptr);
    vkFreeMemory(dev, memoria, nullptr);
    return "nao consegui ligar a imagem de origem a memoria";
  }

  // O STAGING. Visivel pela CPU e pela GPU ao mesmo tempo, porque e por
  // ele que a memoria do motor entra. Escolher o tipo de memoria certo
  // aqui e o que evita uma copia extra feita pelo driver.
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(largura) * altura * 4U;
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = bytes;
  bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkBuffer buffer = VK_NULL_HANDLE;
  if (vkCreateBuffer(dev, &bi, nullptr, &buffer) != VK_SUCCESS) {
    vkDestroyImage(dev, imagem, nullptr);
    vkFreeMemory(dev, memoria, nullptr);
    return "staging recusado";
  }
  VkMemoryRequirements breq{};
  vkGetBufferMemoryRequirements(dev, buffer, &breq);
  VkMemoryAllocateInfo bai{};
  bai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  bai.allocationSize = breq.size;
  bai.memoryTypeIndex = dispositivo_.tipo_de_memoria(
      breq.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VkDeviceMemory bmem = VK_NULL_HANDLE;
  if (bai.memoryTypeIndex == kTipoDeMemoriaInvalido ||
      vkAllocateMemory(dev, &bai, nullptr, &bmem) != VK_SUCCESS) {
    vkDestroyBuffer(dev, buffer, nullptr);
    vkDestroyImage(dev, imagem, nullptr);
    vkFreeMemory(dev, memoria, nullptr);
    return "sem memoria visivel pela CPU";
  }
  if (vkBindBufferMemory(dev, buffer, bmem, 0) != VK_SUCCESS) {
    vkDestroyBuffer(dev, buffer, nullptr);
    vkFreeMemory(dev, bmem, nullptr);
    vkDestroyImage(dev, imagem, nullptr);
    vkFreeMemory(dev, memoria, nullptr);
    return "nao consegui ligar o buffer de transferencia a memoria";
  }

  origem_imagem_ = paraVoid(imagem);
  origem_memoria_ = paraVoid(memoria);
  origem_buffer_ = paraVoid(buffer);
  origem_buffer_memoria_ = paraVoid(bmem);
  origem_largura_ = largura;
  origem_altura_ = altura;
  __android_log_print(ANDROID_LOG_INFO, kEtiqueta,
                      "origem %ux%u (%llu bytes de staging)", largura, altura,
                      static_cast<unsigned long long>(bytes));
  return {};
}

void SuperficieVulkan::destruir_origem() noexcept {
  origem_largura_ = 0;
  origem_altura_ = 0;
  if (!dispositivo_.viva()) {
    origem_imagem_ = paraVoid(nullptr);
    origem_memoria_ = paraVoid(nullptr);
    origem_buffer_ = paraVoid(nullptr);
    origem_buffer_memoria_ = paraVoid(nullptr);
    return;
  }
  const auto dev = deVoid<VkDevice>(dispositivo_.dispositivo());
  // O BUFFER E A IMAGEM NAO PERTENCEM A SWAPCHAIN. E por isso que eles
  // sobrevivem a uma troca de tamanho de janela — e so sao soltos aqui,
  // ou quando o tamanho do QUADRO DO MOTOR muda. Confundir os dois donos e
  // o que deixa memoria de GPU presa depois de uma rotacao.
  if (origem_buffer_ != nullptr) {
    vkDestroyBuffer(dev, deVoid<VkBuffer>(origem_buffer_), nullptr);
    origem_buffer_ = paraVoid(nullptr);
  }
  if (origem_buffer_memoria_ != nullptr) {
    vkFreeMemory(dev, deVoid<VkDeviceMemory>(origem_buffer_memoria_),
                 nullptr);
    origem_buffer_memoria_ = paraVoid(nullptr);
  }
  if (origem_imagem_ != nullptr) {
    vkDestroyImage(dev, deVoid<VkImage>(origem_imagem_), nullptr);
    origem_imagem_ = paraVoid(nullptr);
  }
  if (origem_memoria_ != nullptr) {
    vkFreeMemory(dev, deVoid<VkDeviceMemory>(origem_memoria_), nullptr);
    origem_memoria_ = paraVoid(nullptr);
  }
}

std::int32_t SuperficieVulkan::apresentar_imagem(const std::uint8_t* rgba,
                                                 std::uint32_t largura,
                                                 std::uint32_t altura) noexcept {
  if (rgba == nullptr) return -1;
  if (estado_ != EstadoDaSuperficie::pronta || superficie_ == nullptr) {
    return -1;
  }
  if (swapchain_ == nullptr && !garantir_swapchain().empty()) {
    ++stats_.quadros_descartados;
    return -1;
  }
  if (precisa_refazer_) {
    precisa_refazer_ = false;
    destruir_swapchain();
    if (!garantir_swapchain().empty() || swapchain_ == nullptr) {
      ++stats_.quadros_descartados;
      return -1;
    }
  }
  if (!garantir_origem(largura, altura).empty()) {
    ++stats_.falhas;
    return -11;
  }

  const auto dev = deVoid<VkDevice>(dispositivo_.dispositivo());
  const auto fila = deVoid<VkQueue>(dispositivo_.fila());

  try {
    const VkFence cerca = deVoid<VkFence>(cerca_);
    vkWaitForFences(dev, 1, &cerca, VK_TRUE, UINT64_MAX);

    std::uint32_t indice = 0;
    const VkResult adquiriu = vkAcquireNextImageKHR(
        dev, deVoid<VkSwapchainKHR>(swapchain_), UINT64_MAX,
        deVoid<VkSemaphore>(semaforo_imagem_), VK_NULL_HANDLE, &indice);
    if (adquiriu == VK_ERROR_OUT_OF_DATE_KHR) {
      ++stats_.out_of_date;
      stats_.quadros_descartados++;
      precisa_refazer_ = true;
      return -2;
    }
    if (adquiriu == VK_SUBOPTIMAL_KHR) {
      ++stats_.suboptimal;
      // SO REFAZ SE HOUVER O QUE REFAZER. O aviso se repete
      // enquanto o tamanho nao bate, e cada refazer custa um
      // `vkDeviceWaitIdle` mais uma swapchain nova — a tempestade
      // de recriacoes que este `if` evita. Ver [tamanho_mudou].
      if (tamanho_mudou()) precisa_refazer_ = true;
    } else if (adquiriu != VK_SUCCESS) {
      ++stats_.falhas;
      return -3;
    }
    vkResetFences(dev, 1, &cerca);

    auto* imagens = static_cast<std::vector<VkImage>*>(imagens_);
    auto* comandos = static_cast<std::vector<VkCommandBuffer>*>(comandos_);
    if (indice >= imagens->size() || indice >= comandos->size()) {
      ++stats_.falhas;
      return -4;
    }

    // A SUBIDA. O `memcpy` acontece com a GPU parada — a cerca acabou de
    // esperar —, entao nao ha corrida entre a CPU escrevendo e a fila
    // lendo. O mapa e do MESMO buffer todas as vezes: nao ha alocacao por
    // quadro.
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(largura) * altura * 4U;
    void* destino = nullptr;
    if (vkMapMemory(dev, deVoid<VkDeviceMemory>(origem_buffer_memoria_), 0,
                    bytes, 0, &destino) != VK_SUCCESS) {
      ++stats_.falhas;
      return -12;
    }
    std::memcpy(destino, rgba, static_cast<std::size_t>(bytes));
    vkUnmapMemory(dev, deVoid<VkDeviceMemory>(origem_buffer_memoria_));

    const VkCommandBuffer cmd = (*comandos)[indice];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo b{};
    b.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    b.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &b) != VK_SUCCESS) {
      ++stats_.falhas;
      return -5;
    }

    const VkImageSubresourceRange faixa{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    // A ORIGEM VAI PARA DESTINO DE TRANSFERENCIA. `UNDEFINED` no layout
    // antigo: a copia escreve todos os pixels, nao ha conteudo a preservar
    // — e declarar isso deixa o driver descartar a leitura.
    VkImageMemoryBarrier paraDestino{};
    paraDestino.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    paraDestino.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    paraDestino.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    paraDestino.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    paraDestino.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    paraDestino.image = deVoid<VkImage>(origem_imagem_);
    paraDestino.subresourceRange = faixa;
    paraDestino.srcAccessMask = 0;
    paraDestino.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &paraDestino);

    // E A IMAGEM DA SWAPCHAIN TAMBEM.
    VkImageMemoryBarrier swapDestino = paraDestino;
    swapDestino.image = (*imagens)[indice];
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &swapDestino);

    // Buffer -> imagem de origem.
    VkBufferImageCopy regiao{};
    regiao.bufferOffset = 0;
    regiao.bufferRowLength = 0;
    regiao.bufferImageHeight = 0;
    regiao.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    regiao.imageOffset = {0, 0, 0};
    regiao.imageExtent = {largura, altura, 1};
    vkCmdCopyBufferToImage(cmd, deVoid<VkBuffer>(origem_buffer_),
                           deVoid<VkImage>(origem_imagem_),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &regiao);

    // A ORIGEM PASSA A SER FONTE. Sem esta barreira o blit pode ler antes
    // de a copia terminar, e o defeito e intermitente — o pior tipo.
    VkImageMemoryBarrier paraFonte = paraDestino;
    paraFonte.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    paraFonte.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    paraFonte.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    paraFonte.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &paraFonte);

    // O BLIT, com FILTRO LINEAR de proposito: o quadro do motor e o da
    // superficie quase nunca tem o mesmo tamanho, e um filtro NEAREST aqui
    // serrilha a previa inteira — o que se le como "a imagem perdeu
    // qualidade".
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<std::int32_t>(largura),
                          static_cast<std::int32_t>(altura), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<std::int32_t>(largura_),
                          static_cast<std::int32_t>(altura_), 1};
    vkCmdBlitImage(cmd, deVoid<VkImage>(origem_imagem_),
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, (*imagens)[indice],
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    // E a swapchain vai para apresentacao.
    VkImageMemoryBarrier depois = swapDestino;
    depois.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    depois.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    depois.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    depois.dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &depois);

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
      ++stats_.falhas;
      return -6;
    }

    VkSubmitInfo sub{};
    sub.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const VkSemaphore esperar[] = {deVoid<VkSemaphore>(semaforo_imagem_)};
    const VkPipelineStageFlags estagios[] = {VK_PIPELINE_STAGE_TRANSFER_BIT};
    sub.waitSemaphoreCount = 1;
    sub.pWaitSemaphores = esperar;
    sub.pWaitDstStageMask = estagios;
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &cmd;
    const VkSemaphore sinalizar[] = {
        deVoid<VkSemaphore>(semaforo_pronto_)};
    sub.signalSemaphoreCount = 1;
    sub.pSignalSemaphores = sinalizar;
    if (vkQueueSubmit(fila, 1, &sub, cerca) != VK_SUCCESS) {
      ++stats_.falhas;
      return -7;
    }

    VkPresentInfoKHR ap{};
    ap.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    ap.waitSemaphoreCount = 1;
    ap.pWaitSemaphores = sinalizar;
    const VkSwapchainKHR alvo = deVoid<VkSwapchainKHR>(swapchain_);
    ap.swapchainCount = 1;
    ap.pSwapchains = &alvo;
    ap.pImageIndices = &indice;
    const VkResult apresentou = vkQueuePresentKHR(fila, &ap);
    if (apresentou == VK_ERROR_OUT_OF_DATE_KHR) {
      ++stats_.out_of_date;
      precisa_refazer_ = true;
      return -2;
    }
    if (apresentou == VK_SUBOPTIMAL_KHR) {
      ++stats_.suboptimal;
      // SO REFAZ SE HOUVER O QUE REFAZER. O aviso se repete
      // enquanto o tamanho nao bate, e cada refazer custa um
      // `vkDeviceWaitIdle` mais uma swapchain nova — a tempestade
      // de recriacoes que este `if` evita. Ver [tamanho_mudou].
      if (tamanho_mudou()) precisa_refazer_ = true;
    } else if (apresentou != VK_SUCCESS) {
      ++stats_.falhas;
      return -8;
    }

    ++stats_.quadros_apresentados;
    return 0;
  } catch (const std::exception&) {
    ++stats_.falhas;
    return -9;
  } catch (...) {
    ++stats_.falhas;
    return -10;
  }
}

std::int32_t SuperficieVulkan::redimensionar(std::uint32_t largura,
                                             std::uint32_t altura) noexcept {
  if (largura == 0 || altura == 0) return -1;
  if (estado_ != EstadoDaSuperficie::pronta) return -1;
  if (largura == largura_ && altura == altura_) return 0;
  // NAO RECRIA AQUI. O tamanho real de uma swapchain vem da superficie, e
  // ela pode discordar do que o Flutter pediu (barra de navegacao, corte).
  // Guardar o pedido e deixar a proxima recriacao consultar a superficie e
  // o caminho que sempre funciona.
  largura_ = largura;
  altura_ = altura;
  precisa_refazer_ = true;
  return 0;
}

#else  // !__ANDROID__

SuperficieVulkan::SuperficieVulkan(DispositivoVulkan& d) noexcept
    : dispositivo_(d) {}
SuperficieVulkan::~SuperficieVulkan() { desanexar(); }

std::string SuperficieVulkan::anexar(void*, std::uint32_t, std::uint32_t) noexcept {
  estado_ = EstadoDaSuperficie::erro;
  motivo_ = "superficie Vulkan so existe no Android";
  return motivo_;
}
void SuperficieVulkan::desanexar() noexcept {}
void SuperficieVulkan::destruir_swapchain() noexcept {}
void SuperficieVulkan::destruir_sincronizacao() noexcept {}
void SuperficieVulkan::anotar_motivo(const std::string& texto) noexcept {
  motivo_ = texto;
}
std::string SuperficieVulkan::criar_swapchain() noexcept { return motivo_; }
std::string SuperficieVulkan::criar_sincronizacao() noexcept { return {}; }

std::int32_t SuperficieVulkan::apresentar(std::uint32_t) noexcept {
  ++stats_.falhas;
  return -1;
}

// O QUADRO DO MOTOR TAMBEM NAO EXISTE NO PC — e a AUSENCIA precisa ser uma
// recusa explicita, e nao um simbolo faltando: sem estas tres, quem
// compilasse o pacote no PC levava um erro de LINK (`LNK2019`) em vez de um
// `-1` que o Dart trata como "nao deu, siga pelo caminho antigo".
std::int32_t SuperficieVulkan::apresentar_imagem(const std::uint8_t*,
                                                 std::uint32_t,
                                                 std::uint32_t) noexcept {
  ++stats_.falhas;
  return -1;
}

std::string SuperficieVulkan::garantir_origem(std::uint32_t,
                                              std::uint32_t) noexcept {
  return "sem GPU: a origem do quadro so existe no Android";
}

void SuperficieVulkan::destruir_origem() noexcept {}

bool SuperficieVulkan::tamanho_mudou() const noexcept { return false; }

std::int32_t SuperficieVulkan::redimensionar(std::uint32_t, std::uint32_t) noexcept {
  return -1;
}

#endif  // __ANDROID__

}  // namespace aurea::render
