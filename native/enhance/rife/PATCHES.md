# RIFE vendorizado — origem e correções

Origem: [rife-ncnn-vulkan](https://github.com/nihui/rife-ncnn-vulkan) no commit
`a7532fc3f9f8f008cd6eecd6f2ffe2a9698e0cf7` (2022-10-23), licença MIT (ver `LICENSE`).
Copiados: `rife.cpp`, `rife.h`, `rife_ops.h`, `warp.cpp`, os 16 shaders `.comp` e
`generate_shader_comp_header.cmake`. O `main.cpp` (linha de comando) não entra: o app
usa `../aurea_rife.cpp`.

Todas as correções abaixo estão marcadas com `AUREA` no código.

1. **pack8 do `Warp` desligado** (`warp.cpp`). `Option::use_shader_pack8` saiu do ncnn
   no commit b5bc05e2 (2025-08-22) e elempack 8 nunca chega a esta camada no Vulkan.
   Sem isto não compila com o ncnn 20260526.
2. **Erro de leitura do modelo propagado** (`load_param_model`, `load`). O original
   chamava `load_param(NULL)`/`fclose(NULL)` quando o arquivo não abria e `load()`
   devolvia 0 sempre.
3. **Shaders compilados com as opções já limpas pelo ncnn** (`load`). `Net::load_param`
   desliga fp16/int8 que a GPU não tem em `flownet.opt`, e `process_v4` lê
   `flownet.opt`; os shaders de pré/pós-processamento eram compilados com a opção
   anterior à limpeza. Numa GPU sem int8 storage o pré-processamento esperava uint8 e
   recebia float.
4. **Pipeline que não cria vira erro** (`load`, pré, pós e timestep).
5. **Falha de inferência vira erro** (`process_v4` sem TTA e `process_v4_cpu` sem TTA,
   que são os caminhos do app). O original ignorava `extract` e `submit_and_wait` e
   devolvia 0 com o quadro de saída cheio de lixo.
6. **Clamp em ponto flutuante no pós-processamento** (`rife_postproc.comp`). Converter
   float negativo para `uint` é indefinido; em Adreno/Mali pode virar 0xFFFFFFFF, que o
   clamp transforma em 255: pontos brancos nas áreas escuras.
7. **CPU sem Vulkan não derruba o processo** (`load`). O ncnn novo lê `vkdev->info` em
   `Net::set_vulkan_device`; o original chamava com ponteiro nulo no modo CPU.

A regeneração é reprodutível a partir do clone original: cada troca é aplicada por
texto exato e falha se o trecho mudar.

## Verificado

- Host Windows (RTX 3050, ncnn 20260526 Windows): `test/rife_motor_nativo_test.dart`
  passa na GPU e na CPU. Janela de textura andando 24 px, 640x360: RIFE 44 dB contra
  13-15 dB da mistura e 12-15 dB de repetir o quadro. Os quadros t=0 e t=1 saem
  idênticos às entradas.
- Memória de GPU no host (fp16): modelo 77 MB; 720p +184 MB; 1080p +397 MB. Tudo é
  devolvido ao destruir o motor.
- Android arm64-v8a (NDK 28.2, `-std=c++20`, `c++_shared`): compila e linka; exporta
  `ar_create/ar_destroy/ar_info_get/ar_interpolate/ar_interpolate_png`; segmentos
  alinhados em 16 KB.

## Não verificado

Nenhum número de celular. Tempo por quadro, memória real e a lista de GPUs que carregam
o modelo precisam de medida no aparelho.
