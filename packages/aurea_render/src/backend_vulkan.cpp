#include "backend_vulkan.h"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#if defined(__ANDROID__)
// O CABECALHO DO VULKAN VEM DO NDK. `VK_USE_PLATFORM_ANDROID_KHR` liga as
// partes especificas do Android (superficie a partir de `ANativeWindow`) e
// tem de vir ANTES do `vulkan.h`, senao a declaracao nao existe.
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

#include <android/log.h>
#endif

namespace aurea::render {

#if defined(__ANDROID__)
namespace {

constexpr const char* kEtiqueta = "AureaVulkan";

/// AS EXTENSOES DE INSTANCIA QUE A SUPERFICIE PRECISA.
constexpr const char* kExtensoesObrigatorias[] = {"VK_KHR_surface",
                                                  "VK_KHR_android_surface"};

bool tem_extensao_de_instancia(const char* nome) noexcept {
  std::uint32_t quantas = 0;
  if (vkEnumerateInstanceExtensionProperties(nullptr, &quantas, nullptr) !=
          VK_SUCCESS ||
      quantas == 0) {
    return false;
  }
  std::vector<VkExtensionProperties> lista(quantas);
  if (vkEnumerateInstanceExtensionProperties(nullptr, &quantas, lista.data()) !=
      VK_SUCCESS) {
    return false;
  }
  return std::any_of(lista.begin(), lista.end(), [nome](const auto& e) {
    return std::string_view(e.extensionName) == nome;
  });
}

bool tem_extensao_de_dispositivo(VkPhysicalDevice fisico,
                                 const char* nome) noexcept {
  std::uint32_t quantas = 0;
  if (vkEnumerateDeviceExtensionProperties(fisico, nullptr, &quantas, nullptr) !=
          VK_SUCCESS ||
      quantas == 0) {
    return false;
  }
  std::vector<VkExtensionProperties> lista(quantas);
  if (vkEnumerateDeviceExtensionProperties(fisico, nullptr, &quantas,
                                           lista.data()) != VK_SUCCESS) {
    return false;
  }
  return std::any_of(lista.begin(), lista.end(), [nome](const auto& e) {
    return std::string_view(e.extensionName) == nome;
  });
}

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

std::string DispositivoVulkan::abrir() noexcept {
  if (viva()) return {};
  try {
    // ------------------------------- a instancia
    //
    // AS DUAS EXTENSOES DE SUPERFICIE SAO OBRIGATORIAS PARA O V1. Sem
    // `VK_KHR_android_surface` nao ha o que apresentar — e o `vkCreateInstance`
    // devolve VK_ERROR_EXTENSION_NOT_PRESENT, que e um erro claro e
    // imediato. Falhar aqui e melhor do que falhar no primeiro quadro.
    for (const char* nome : kExtensoesObrigatorias) {
      if (!tem_extensao_de_instancia(nome)) {
        return std::string("extensao de instancia ausente: ") + nome;
      }
    }

    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Aurea";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "Aurea RenderCore";
    app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app.apiVersion = VK_API_VERSION_1_1;

    const char* pedidas[] = {"VK_KHR_surface", "VK_KHR_android_surface"};
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = 2;
    info.ppEnabledExtensionNames = pedidas;
    // A VARIAVEL LOCAL E TIPADA; O MEMBRO E `void*`.
    //
    // O cabecalho guarda os handles como `void*` para poder ser incluido
    // onde o `vulkan.h` nao existe (o PC). A conversao fica toda aqui, e o
    // compilador do Android NAO aceita `&void*` no lugar de `VkInstance*`
    // — o que e um favor: sem a variavel local, o erro apareceria longe
    // daqui.
    VkInstance instancia = VK_NULL_HANDLE;
    if (vkCreateInstance(&info, nullptr, &instancia) != VK_SUCCESS) {
      return "vkCreateInstance falhou";
    }
    instancia_ = paraVoid(instancia);

    // ------------------------------- o dispositivo fisico
    std::uint32_t quantos = 0;
    if (vkEnumeratePhysicalDevices(deVoid<VkInstance>(instancia_),
                                   &quantos, nullptr) != VK_SUCCESS ||
        quantos == 0) {
      fechar();
      return "nenhum dispositivo Vulkan";
    }
    std::vector<VkPhysicalDevice> fisicos(quantos);
    vkEnumeratePhysicalDevices(deVoid<VkInstance>(instancia_), &quantos,
                               fisicos.data());
    fisico_ = paraVoid(fisicos.front());
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(deVoid<VkPhysicalDevice>(fisico_),
                                  &props);
    nome_ = props.deviceName;

    if (!achar_familia_grafica(deVoid<VkPhysicalDevice>(fisico_),
                               familia_)) {
      fechar();
      return "nenhuma familia de fila grafica";
    }

    // ------------------------------- o dispositivo logico
    if (!tem_extensao_de_dispositivo(deVoid<VkPhysicalDevice>(fisico_),
                                     "VK_KHR_swapchain")) {
      fechar();
      return "VK_KHR_swapchain indisponivel";
    }

    const float prioridade = 1.0F;
    VkDeviceQueueCreateInfo fila{};
    fila.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    fila.queueFamilyIndex = familia_;
    fila.queueCount = 1;
    fila.pQueuePriorities = &prioridade;

    const char* extensoes[] = {"VK_KHR_swapchain"};
    VkDeviceCreateInfo dev{};
    dev.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev.queueCreateInfoCount = 1;
    dev.pQueueCreateInfos = &fila;
    dev.enabledExtensionCount = 1;
    dev.ppEnabledExtensionNames = extensoes;

    VkDevice criado = VK_NULL_HANDLE;
    if (vkCreateDevice(deVoid<VkPhysicalDevice>(fisico_), &dev, nullptr,
                       &criado) != VK_SUCCESS) {
      fechar();
      return "vkCreateDevice recusado";
    }
    dispositivo_ = paraVoid(criado);
    VkQueue q = VK_NULL_HANDLE;
    vkGetDeviceQueue(criado, familia_, 0, &q);
    fila_ = paraVoid(q);

    __android_log_print(ANDROID_LOG_INFO, kEtiqueta,
                        "dispositivo pronto: %s (familia %u)", nome_.c_str(),
                        familia_);
    return {};
  } catch (const std::exception& e) {
    fechar();
    return std::string("excecao: ") + e.what();
  } catch (...) {
    fechar();
    return "excecao desconhecida";
  }
}

void DispositivoVulkan::fechar() noexcept {
  // A ORDEM IMPORTA: o dispositivo logico morre ANTES da instancia. Ao
  // contrario, a instancia leva junto um dispositivo que ainda existe e o
  // driver reclama (ou pior, deixa memoria presa).
  if (dispositivo_ != nullptr) {    vkDestroyDevice(deVoid<VkDevice>(dispositivo_), nullptr);
    dispositivo_ = paraVoid(nullptr);
    fila_ = paraVoid(nullptr);
  }
  if (instancia_ != nullptr) {
    vkDestroyInstance(deVoid<VkInstance>(instancia_), nullptr);
    instancia_ = paraVoid(nullptr);
  }
  fisico_ = paraVoid(nullptr);
}

std::string SondaVulkan::resumo() const {
  if (!disponivel) return "Vulkan indisponivel: " + motivo;
  char texto[256];
  std::snprintf(texto, sizeof(texto),
                "Vulkan %u.%u.%u | %s | driver %u.%u.%u | %u dispositivo(s) | "
                "fila grafica: %s | swapchain: %s | textura max %u | "
                "AHardwareBuffer: %s",
                VK_VERSION_MAJOR(versao_da_api), VK_VERSION_MINOR(versao_da_api),
                VK_VERSION_PATCH(versao_da_api), nome_do_dispositivo.c_str(),
                VK_VERSION_MAJOR(versao_do_driver),
                VK_VERSION_MINOR(versao_do_driver),
                VK_VERSION_PATCH(versao_do_driver), dispositivos_encontrados,
                tem_fila_grafica ? "sim" : "NAO", tem_swapchain ? "sim" : "NAO",
                textura_maxima, extensao_hardware_buffer ? "sim" : "nao");
  return texto;
}

/// ESCOLHER O TIPO DE MEMORIA E OBRIGACAO, NAO PREFERENCIA.
///
/// O Vulkan nao aloca memoria "generica": da uma lista de tipos, cada um
/// com um conjunto de propriedades, e usar um tipo que nao as tem e falha
/// de validacao. O `bits` e quem diz quais servem para ESTE recurso.
std::uint32_t DispositivoVulkan::tipo_de_memoria(std::uint32_t bits,
                                                 std::uint32_t exigidas) const
    noexcept {
  VkPhysicalDeviceMemoryProperties props{};
  vkGetPhysicalDeviceMemoryProperties(deVoid<VkPhysicalDevice>(fisico_),
                                      &props);
  for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((bits & (1U << i)) == 0) continue;
    if ((props.memoryTypes[i].propertyFlags & exigidas) == exigidas) {
      return i;
    }
  }
  return kTipoDeMemoriaInvalido;
}

SondaVulkan sondar_vulkan() noexcept {
  SondaVulkan s;
  try {
    DispositivoVulkan d;
    const std::string erro = d.abrir();
    if (!erro.empty()) {
      s.motivo = erro;
      return s;
    }
    s.disponivel = true;
    s.tem_fila_grafica = true;
    s.tem_swapchain = true;
    s.nome_do_dispositivo = d.nome();

    const auto fisico = deVoid<VkPhysicalDevice>(d.fisico());
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(fisico, &props);
    s.versao_do_driver = props.driverVersion;
    s.versao_da_api = props.apiVersion;
    s.tipo_do_dispositivo = static_cast<std::uint32_t>(props.deviceType);
    s.textura_maxima = props.limits.maxImageDimension2D;

    std::uint32_t quantos = 0;
    if (vkEnumeratePhysicalDevices(deVoid<VkInstance>(d.instancia()),
                                   &quantos, nullptr) == VK_SUCCESS) {
      s.dispositivos_encontrados = quantos;
    }
    std::uint32_t versao = 0;
    if (vkEnumerateInstanceVersion(&versao) == VK_SUCCESS) {
      s.versao_da_instancia = versao;
    }

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
    s.extensao_hardware_buffer = tem_extensao_de_dispositivo(
        fisico, "VK_ANDROID_external_memory_android_hardware_buffer");

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

std::string DispositivoVulkan::abrir() noexcept {
  return "backend Vulkan so e compilado para Android";
}

void DispositivoVulkan::fechar() noexcept {}

std::uint32_t DispositivoVulkan::tipo_de_memoria(std::uint32_t,
                                                 std::uint32_t) const noexcept {
  return kTipoDeMemoriaInvalido;
}

std::string SondaVulkan::resumo() const {
  return "Vulkan indisponivel: " + motivo;
}

SondaVulkan sondar_vulkan() noexcept {
  SondaVulkan s;
  // NO PC NAO HA VULKAN NESTE BUILD, E ISSO NAO E FALHA. O backend e
  // compilado so para Android hoje; devolver "nao existe aqui" e a
  // resposta honesta, e o teste no PC cobra exatamente isso.
  s.disponivel = false;
  s.motivo = "backend Vulkan so e compilado para Android";
  return s;
}

#endif  // __ANDROID__

}  // namespace aurea::render
