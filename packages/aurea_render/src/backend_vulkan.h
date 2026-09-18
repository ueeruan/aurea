// O BACKEND VULKAN — ESTAGIO V0: O DISPOSITOR.
//
// ================================ O QUE ISTO E =========================
// A sonda que sobe o Vulkan de verdade, pergunta ao aparelho o que ele
// tem e DESCE. Nao compoe um pixel.
//
// POR QUE COMECAR PELO DISPOSITIVO, E POR QUE NAO DIZER QUE E O MOTOR:
// num celular, o Vulkan falha em metade dos casos antes de desenhar
// qualquer coisa — driver antigo, camada ausente, fila grafica que nao
// existe, `vkCreateDevice` recusado. Comecar pela swapchain deixaria
// tudo isso escondido atras de uma tela preta, e a tela preta nao diz se
// o erro foi a swapchain, o driver ou o shader. A sonda isola a primeira
// metade.
//
// E ELA NAO SE VESTE DE MAIS DO QUE E: `aurea_render_abrir` continua
// RECUSANDO o backend Vulkan. Um nucleo que se diz de GPU e compoe na
// CPU seria exatamente a "camada falsa sobre o renderizador antigo" que
// nao se quer. A sonda responde o que sabe; a promocao a motor acontece
// no estagio V2, quando o compositor rodar em shader.
//
// ================================ O QUE FALTA =========================
// V1: superficie a partir do `ANativeWindow` do `SurfaceProducer` e
//     swapchain. V2: o compositor em GPU (oito modos de mistura em
//     shader, subida de textura, transformacao na matriz).
#ifndef AUREA_RENDER_BACKEND_VULKAN_H
#define AUREA_RENDER_BACKEND_VULKAN_H

#include <cstdint>
#include <string>
#include <string_view>

namespace aurea::render {

/// O QUE A SONDA DESCOBRIU. Tudo opcional: um aparelho sem Vulkan
/// devolve [disponivel] falso e o resto vazio, e isso NAO e um erro —
/// e uma resposta.
struct SondaVulkan {
  bool disponivel = false;

  /// O motivo, quando nao ha Vulkan. Sempre preenchido se `!disponivel`.
  std::string motivo;

  std::uint32_t versao_da_instancia = 0;  // VK_MAKE_VERSION, cru
  std::string nome_do_dispositivo;
  std::uint32_t tipo_do_dispositivo = 0;  // VkPhysicalDeviceType
  std::uint32_t versao_do_driver = 0;
  std::uint32_t versao_da_api = 0;
  std::uint32_t dispositivos_encontrados = 0;

  bool tem_fila_grafica = false;

  /// O dispositivo logico subiu COM `VK_KHR_swapchain`. Sem ela o V1 nao
  /// tem como apresentar na tela — e saber disso agora e melhor do que
  /// descobrir com uma janela preta.
  bool tem_swapchain = false;

  /// O teto de textura que o aparelho garante. E o que decide se uma
  /// composicao 4K cabe — e a pergunta que mais custa descobrir errado
  /// depois.
  std::uint32_t textura_maxima = 0;

  /// Onde a memoria de imagem mora. A partir do Android 10 o caminho sem
  /// copia e um `AHardwareBuffer`; um aparelho que so oferece
  /// `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` obriga a copia e a promessa
  /// de zero-copy cai ali.
  bool memoria_local_do_dispositivo = false;
  bool memoria_visivel_ao_host = false;

  /// A extensao de buffer de hardware existe e esta ligada.
  bool extensao_hardware_buffer = false;

  /// O relatorio em uma linha, do jeito que vai para a tela ou para o
  /// relatorio de bug.
  [[nodiscard]] std::string resumo() const;
};

/// SOBE O VULKAN, PERGUNTA E DESCE.
///
/// Em plataforma sem Vulkan (o PC de desenvolvimento) devolve
/// `disponivel = false` com o motivo — e nao uma excecao, porque "nao ha
/// Vulkan aqui" e um resultado legitimo e nao uma falha.
[[nodiscard]] SondaVulkan sondar_vulkan() noexcept;

}  // namespace aurea::render

#endif  // AUREA_RENDER_BACKEND_VULKAN_H
