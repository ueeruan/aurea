#include "backend_vulkan.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#if defined(__ANDROID__)
// O CABECALHO DO VULKAN VEM DO NDK. `VK_USE_PLATFORM_ANDROID_KHR` liga as
// partes especificas do Android (superficie a partir de `ANativeWindow`) e
// tem de vir ANTES do `vulkan.h`, senao a declaracao nao existe e o erro
// que aparece e "unknown type" numa struct que a plataforma nao tem.
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

#include <android/log.h>
#endif

namespace aurea::render {

#if defined(__ANDROID__)
namespace {

constexpr const char* kEtiqueta = "AureaVulkan";

/// OS NOMES QUE INTERESSAM, para o resumo caber numa linha.
constexpr const char* kExtensoesDeInteresse[] = {
    "VK_KHR_android_surface",
    "VK_ANDROID_external_memory_android_hardware_buffer",
    "VK_KHR_swapchain",
};

bool tem_extensao(const std::vector<VkExtensionProperties>& lista,
                  const char* nome) noexcept {
  return std::any_of(lista.begin(), lista.end(), [nome](const auto& e) {
    return std::string_view(e.extensionName) == nome;
  });
}

/// A INSTANCIA COM POSSE. Criada por `vkCreateInstance`, destruida no
/// destrutor — e nao ha caminho em que alguem esqueca de destruir, que e
/// o que uma instancia de Vulkan vazada significa (o driver fica carregado
/// ate o processo morrer).
class Instancia {
 public:
  Instancia() = default;
  ~Instancia() { destruir(); }
  Instancia(const Instancia&) = delete;
  Instancia& operator=(const Instancia&) = delete;

  [[nodiscard]] bool criar() noexcept {
    std::uint32_t quantas = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &quantas, nullptr) !=
            VK_SUCCESS ||
        quantas == 0) {
      return false;
    }
    std::vector<VkExtensionProperties> disponiveis(quantas);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &quantas,
                                               disponiveis.data()) !=
        VK_SUCCESS) {
      return false;
    }

    // SO AS EXTENSOES QUE EXISTEM. Pedir uma que o aparelho nao tem faz
    // `vkCreateInstance` devolver VK_ERROR_EXTENSION_NOT_PRESENT e a
    // sonda inteira morre por causa de um extra opcional.
    std::vector<const char*> pedidas;
    for (const char* nome : kExtensoesDeInteresse) {
      if (tem_extensao(disponiveis, nome)) pedidas.push_back(nome);
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Aurea";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "Aurea RenderCore";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = static_cast<std::uint32_t>(pedidas.size());
    info.ppEnabledExtensionNames = pedidas.empty() ? nullptr : pedidas.data();

    if (vkCreateInstance(&info, nullptr, &instancia_) != VK_SUCCESS) {
      instancia_ = VK_NULL_HANDLE;
      return false;
    }
    return true;
  }

  void destruir() noexcept {
    if (instancia_ != VK_NULL_HANDLE) {
      vkDestroyInstance(instancia_, nullptr);
      instancia_ = VK_NULL_HANDLE;
    }
  }

  [[nodiscard]] VkInstance valor() const noexcept { return instancia_; }

 private:
  VkInstance instancia_ = VK_NULL_HANDLE;
};

/// O DISPOSITOR LOGICO COM POSSE. Destroi a fila logica junto — a fila
/// nao tem destruicao propria, ela morre com o dispositivo.
class Dispositivo {
 public:
  Dispositivo() = default;
  ~Dispositivo() { destruir(); }
  Dispositivo(const Dispositivo&) = delete;
  Dispositivo& operator=(const Dispositivo&) = delete;

  [[nodiscard]] bool criar(VkPhysicalDevice fisico, std::uint32_t familia,
                           bool com_extensao_de_swapchain) noexcept {
    const float prioridade = 1.0F;
    VkDeviceQueueCreateInfo fila{};
    fila.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    fila.queueFamilyIndex = familia;
    fila.queueCount = 1;
    fila.pQueuePriorities = &prioridade;

    const char* extensoes[1] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &fila;
    // O V1 PRECISA DA SWAPCHAIN. Ela e pedida OPTATIVAMENTE: um aparelho
    // que ainda nao a oferece (raro, mas existe) nao pode impedir a sonda
    // de dizer o nome do dispositivo.
    info.enabledExtensionCount = com_extensao_de_swapchain ? 1 : 0;
    info.ppEnabledExtensionNames = com_extensao_de_swapchain ? extensoes
                                                             : nullptr;

    if (vkCreateDevice(fisico, &info, nullptr, &dispositivo_) != VK_SUCCESS) {
      dispositivo_ = VK_NULL_HANDLE;
      return false;
    }
    vkGetDeviceQueue(dispositivo_, familia, 0, &fila_);
    return true;
  }

  void destruir() noexcept {
    if (dispositivo_ != VK_NULL_HANDLE) {
      vkDestroyDevice(dispositivo_, nullptr);
      dispositivo_ = VK_NULL_HANDLE;
      fila_ = VK_NULL_HANDLE;
    }
  }

  [[nodiscard]] bool vivo() const noexcept {
    return dispositivo_ != VK_NULL_HANDLE;
  }

 private:
  VkDevice dispositivo_ = VK_NULL_HANDLE;
  VkQueue fila_ = VK_NULL_HANDLE;
};

/// A FAMILIA DE FILA QUE COMPOE. Sem ela nao ha desenho nenhum, e o
/// numero dela muda de aparelho para aparelho — por isso se procura, e
/// nao se assume a zero.
bool achar_familia_grafica(VkPhysicalDevice fisico,
                           std::uint32_t& familia) noexcept {
  std::uint32_t quantas = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(fisico, &quantas, nullptr);
  if (quantas == 0) return false;
  std::vector<VkQueueFamilyProperties> familias(quantas);
  vkGetPhysicalDeviceQueueFamilyProperties(fisico, &quantas, familias.data());
  for (std::uint32_t i = 0; i < quantas; ++i) {
    if ((familias[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      familia = i;
      return true;
    }
  }
  return false;
}
}  // namespace

std::string SondaVulkan::resumo() const {
  if (!disponivel) {
    return "Vulkan indisponivel: " + motivo;
  }
  char texto[256];
  std::snprintf(texto, sizeof(texto),
                "Vulkan %u.%u.%u | %s | driver %u.%u.%u | %u dispositivo(s) | "
                "fila grafica: %s | swapchain: %s | textura max %u | AHardwareBuffer: %s",
                VK_VERSION_MAJOR(versao_da_api), VK_VERSION_MINOR(versao_da_api),
                VK_VERSION_PATCH(versao_da_api), nome_do_dispositivo.c_str(),
                VK_VERSION_MAJOR(versao_do_driver),
                VK_VERSION_MINOR(versao_do_driver),
                VK_VERSION_PATCH(versao_do_driver), dispositivos_encontrados,
                tem_fila_grafica ? "sim" : "NAO", tem_swapchain ? "sim" : "NAO",
                textura_maxima,
                extensao_hardware_buffer ? "sim" : "nao");
  return texto;
}

SondaVulkan sondar_vulkan() noexcept {
  SondaVulkan s;
  try {
    Instancia instancia;
    if (!instancia.criar()) {
      s.motivo = "vkCreateInstance falhou (driver ausente ou camada recusada)";
      return s;
    }

    std::uint32_t versao = 0;
    if (vkEnumerateInstanceVersion(&versao) == VK_SUCCESS) {
      s.versao_da_instancia = versao;
    }

    std::uint32_t quantos = 0;
    if (vkEnumeratePhysicalDevices(instancia.valor(), &quantos, nullptr) !=
            VK_SUCCESS ||
        quantos == 0) {
      s.motivo = "nenhum dispositivo Vulkan no aparelho";
      return s;
    }
    std::vector<VkPhysicalDevice> fisicos(quantos);
    if (vkEnumeratePhysicalDevices(instancia.valor(), &quantos,
                                   fisicos.data()) != VK_SUCCESS) {
      s.motivo = "vkEnumeratePhysicalDevices falhou";
      return s;
    }
    s.dispositivos_encontrados = quantos;

    // O PRIMEIRO, MAS COM AS PROPRIEDADES LIDAS DE VERDADE. Escolher
    // "o melhor" agora seria chute: com um dispositivo so (o caso de todo
    // celular) nao ha o que escolher, e com dois ainda nao se sabe qual
    // tem a superficie que o Flutter vai entregar.
    const VkPhysicalDevice fisico = fisicos.front();
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(fisico, &props);

    s.nome_do_dispositivo = props.deviceName;
    s.tipo_do_dispositivo = static_cast<std::uint32_t>(props.deviceType);
    s.versao_do_driver = props.driverVersion;
    s.versao_da_api = props.apiVersion;
    s.textura_maxima = props.limits.maxImageDimension2D;

    std::uint32_t familia = 0;
    s.tem_fila_grafica = achar_familia_grafica(fisico, familia);

    // A MEMORIA, POR TIPO. E o que decide se a imagem pode ficar na GPU e
    // ser entregue ao Flutter sem passar pela CPU.
    VkPhysicalDeviceMemoryProperties memoria{};
    vkGetPhysicalDeviceMemoryProperties(fisico, &memoria);
    for (std::uint32_t i = 0; i < memoria.memoryTypeCount; ++i) {
      const auto flags = memoria.memoryTypes[i].propertyFlags;
      if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0) {
        s.memoria_local_do_dispositivo = true;
      }
      if ((flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0) {
        s.memoria_visivel_ao_host = true;
      }
    }

    std::uint32_t quantas_ext = 0;
    if (vkEnumerateDeviceExtensionProperties(fisico, nullptr, &quantas_ext,
                                             nullptr) == VK_SUCCESS &&
        quantas_ext > 0) {
      std::vector<VkExtensionProperties> lista(quantas_ext);
      if (vkEnumerateDeviceExtensionProperties(fisico, nullptr, &quantas_ext,
                                               lista.data()) == VK_SUCCESS) {
        s.extensao_hardware_buffer = tem_extensao(
            lista, "VK_ANDROID_external_memory_android_hardware_buffer");
      }
    }

    // ================= O DISPOSITOR LOGICO E CRIADO E DESTRUIDO =========
    //
    // ESTE E O TESTE QUE IMPORTA. `vkCreateDevice` e onde o driver real
    // recusa: fila inexistente, extensao pedida sem suporte, limite
    // estourado. Uma sonda que para em `vkEnumeratePhysicalDevices` conta
    // metade da historia, e a metade facil.
    s.disponivel = true;
    if (s.tem_fila_grafica) {
      Dispositivo dispositivo;
      // A SWAPCHAIN PRIMEIRO, e sem ela depois: um aparelho que ainda nao
      // expoe `VK_KHR_swapchain` nao pode impedir a sonda de dizer o nome
      // do dispositivo — mas o fato fica registrado, porque sem swapchain
      // o V1 nao tem como apresentar.
      if (dispositivo.criar(fisico, familia, /*com_extensao_de_swapchain=*/true)) {
        s.tem_swapchain = true;
      } else if (!dispositivo.criar(fisico, familia,
                                    /*com_extensao_de_swapchain=*/false)) {
        s.tem_fila_grafica = false;
        s.motivo = "vkCreateDevice recusado";
      }
    }

    __android_log_print(ANDROID_LOG_INFO, kEtiqueta, "%s", s.resumo().c_str());
    return s;
  } catch (const std::exception& e) {
    s.disponivel = false;
    s.motivo = std::string("excecao na sonda: ") + e.what();
    return s;
  } catch (...) {
    s.disponivel = false;
    s.motivo = "excecao desconhecida na sonda";
    return s;
  }
}

#else  // !__ANDROID__

std::string SondaVulkan::resumo() const {
  return "Vulkan indisponivel: " + motivo;
}

SondaVulkan sondar_vulkan() noexcept {
  SondaVulkan s;
  // NO PC NAO HA VULKAN NESTE BUILD, E ISSO NAO E FALHA. O backend e
  // compilado so para Android hoje; devolver "nao existe aqui" e a
  // resposta honesta, e o teste no PC cobra exatamente isso — a sonda nao
  // pode dizer que subiu um Vulkan que nao existe.
  s.disponivel = false;
  s.motivo = "backend Vulkan so e compilado para Android";
  return s;
}

#endif  // __ANDROID__

}  // namespace aurea::render
