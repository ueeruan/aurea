// =============================================================================
//  Aurea / render / GPUBackend.cpp
//
//  Ponto de entrada do backend gráfico.
//
//  ESTADO ATUAL: nenhum backend está implementado.
//
//  `create_default()` devolve `nullptr`, e isso NÃO é um stub fingindo
//  funcionar — é a resposta correta e honesta para "que backend gráfico usar?".
//  O Engine trata o nullptr: registra um aviso, desativa o preview e segue de
//  pé. A timeline, a animação, os comandos e a serialização funcionam sem GPU,
//  e é justamente o que permite testá-los no CI sem aparelho na mão.
//
//  O que falta, em ordem de dependência:
//
//    1. `VulkanBackend` (Android)  — device, swapchain, import de
//       AHardwareBuffer como textura (zero-copy do decoder),
//       e o compilador de SPIR-V em runtime para as variantes de shader.
//
//    2. `MetalBackend` (iOS)       — device, CAMetalLayer, import de
//       CVPixelBuffer como CVMetalTexture (zero-copy do VideoToolbox),
//       e o carregamento do MSL já traduzido de SPIR-V no build.
//
//  O restante do motor — compositor, grafo de efeitos, cena 3D, export — escreve
//  contra `GPUBackend` e não muda quando um deles entrar. É essa indireção que
//  faz o trabalho ser aditivo em vez de uma reescrita.
//
//  Ver docs/architecture/RENDERER.md para a especificação de cada backend.
// =============================================================================
#include "aurea/render/GPUBackend.hpp"
#include "aurea/core/Log.hpp"

namespace aurea {

GPUBackend* GPUBackend::create_default() noexcept {
#if defined(AUREA_PLATFORM_ANDROID)
    // Android usará Vulkan. O backend ainda não existe; devolver nullptr faz o
    // Engine seguir sem preview, com aviso registrado — em vez de prometer uma
    // superfície que não desenha nada.
    AUREA_LOG_WARN("VulkanBackend ainda nao implementado: preview desativado");
    return nullptr;

#elif defined(AUREA_PLATFORM_IOS)
    // iOS usará Metal.
    AUREA_LOG_WARN("MetalBackend ainda nao implementado: preview desativado");
    return nullptr;

#else
    // No host não há backend gráfico, e não precisa haver: toda a lógica
    // testável do motor é independente de GPU.
    AUREA_LOG_INFO("host sem backend grafico: motor roda em modo headless");
    return nullptr;
#endif
}

} // namespace aurea
