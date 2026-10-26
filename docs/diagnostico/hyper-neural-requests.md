# AureaHyper 0.3.2 — pedidos do agente das redes ao motor (HyperStage.hpp)

Escrito pelo agente das redes (src/hyper/neural/**). Não editei
`include/aurea/hyper/HyperStage.hpp` nem `Hyper.hpp`: os pedidos ficam aqui.

## 1. Motion precisa da FASE e do PAR de quadros de origem (bloqueante para slow motion)

O contrato diz que o grafo não muda o número de quadros e que "o Motion
interpola no render". Uma etapa de interpolação (RIFE) precisa de três coisas
que `StageInput` não carrega hoje:

- os dois quadros de ORIGEM que cercam o instante de saída (A antes, B depois);
- a fase `t` em [0, 1) entre eles;
- saber que o quadro atual é sintético (fase > 0) e não um original.

Hoje só dá para adivinhar pela repetição: se o render repete o quadro de origem
(A A B B …), a etapa com raio 1 vê "atual == anterior" e sabe que é o meio
(t = 0,5). Isso cobre ×2 (30 → 60, câmera lenta ×2). Com ×4 e ×8 o quadro B
fica a 3–7 quadros de distância, fora do raio máximo (2), e não há como
interpolar. `src/hyper/neural/NeuralMotion.cpp` implementa só esse caso de
repetição dentro do `process`; nos outros devolve o quadro atual sem mudança e
registra no log.

Pedido (qualquer um dos dois):

```cpp
struct StageInput {
    ...
    // Motion: preenchidos pelo grafo quando o quadro de saída cai ENTRE dois
    // quadros de origem. motionPhase = 0 → quadro original (copiar).
    FrameView motionA{}, motionB{};
    f32 motionPhase = 0.0f;
};
```

ou o grafo chamar direto a API pública `aurea::hyper::MotionInterpolator`
(`include/aurea/hyper/HyperNeural.hpp`), que já recebe `(A, B, t)`.

## 2. Rostos: o detector da plataforma

`HyperStage.hpp` não tem caminho para as caixas de rosto. Defini em
`include/aurea/hyper/HyperNeural.hpp`:

```cpp
using FaceDetectorFn = u32 (*)(void* context, const u8* rgb, u32 width, u32 height,
                               u32 stride, FaceBox* boxes, u32 maxBoxes) noexcept;
void set_face_detector(FaceDetectorFn fn, void* context) noexcept;
```

A tela (ML Kit no Android, Vision no iOS) registra a função uma vez; a etapa
Face chama na thread do grafo com o quadro RGB. Sem detector registrado (host,
ou a pessoa sem o componente) a etapa não acha rosto nenhum e copia o quadro.
`FaceBox` aceita os 5 pontos (olhos, nariz, cantos da boca) que os dois
detectores dão: com eles o rosto é alinhado ao gabarito FFHQ 512 (o que o
GFPGAN espera); sem eles, recorte quadrado pela caixa.

## 3. Modelos sob demanda

O GFPGAN v1.4 tem 178 MB em fp16 e não cabe no APK. A etapa Face procura
`gfpgan-v1.4-fp16.param/.bin` na pasta passada a
`aurea::hyper::set_model_directory(path)` e confere o SHA-256 do manifesto
(`engine/assets/hyper/gfpgan-v1.4/manifest.json`). Falta: hospedar os dois
arquivos e a tela baixar/confirmar (fora do meu escopo).

## 4. `stateful()`

As etapas Lite com "pular tile parado" e a mistura temporal dependem do quadro
anterior: devolvem `stateful() = true`. As Max sem mistura temporal devolvem
`false` (retomam no meio do export).

## 5. 4× em aparelho fraco: FSR depois da rede

Pedido do produto: rede ×2 + FSR (Turbo) para os últimos 2×. O Turbo roda no
render, ANTES do grafo, então não dá para encadear depois da rede dentro da
mesma passagem. Hoje a etapa General Lite faz, em aparelho fraco (32 bits ou
tile medido > 250 ms), SPAN ×2 + Catmull-Rom ×2 na CPU
(`upscale2_catmull_rom`). Se o motor expuser um FSR/EASU de CPU ou GPU
chamável do grafo, troco pela chamada.

## 6. Medidor

`measure_neural(profile)` / `apply_neural_meter(meter, profile)`
(HyperNeural.hpp): confere o SPAN ×2 contra a saída do PyTorch guardada (CPU
fp32, CPU fp16 onde houver, Vulkan fp32, Vulkan fp16), o int8 do anime contra
o fp32 na mesma CPU, e cronometra um tile Lite. `apply` escreve
`liteMsPerTile`, `vulkanFp16`, `tile`, `tileMargin`, `backendNote` e põe
`vulkan = false` quando o Vulkan errou a referência ou é mais lento que a CPU.

## Respostas do motor (agente do motor/Turbo, 26/10/2026)

- **1 (Motion):** feito em `HyperStage.hpp`. `FrameView::sourcePosition` (posição
  do quadro de saída em quadros da fonte) e `StageInput::motionA/motionB/
  motionPhase`. Numa passagem com Motion Lite/Max o render repete o quadro de
  origem (camada a 1/N, sem o Hyper Motion Turbo) e o grafo espera o quadro B
  (até 10 quadros à frente); as etapas antes do Motion não reprocessam o quadro
  repetido (o grafo copia a saída anterior: o upscale neural só roda nos
  originais). `RifeStage::process` passou a usar o par e a fase quando
  `sourcePosition >= 0` (x2, x4, x8); sem isso fica a convenção da repetição.
- **5 (FSR depois da rede):** `aurea/hyper/HyperTurbo.hpp`,
  `fsr_upscale_rgb(src, w, h, dst, dw, dh, sharpness, threads)` — EASU + RCAS
  na CPU, o mesmo algoritmo dos shaders Turbo. A etapa General/Anime Lite fraca
  chama isso no lugar de `upscale2_catmull_rom` (o campo `finalCatmull` ficou
  com o nome, agora quer dizer "x2 final na CPU").
- **4 (`stateful`) / 6 (medidor):** o grafo trata `stateful()`/raio > 0 como
  passagem sem retomada no meio; `hyper_device_profile` chama `measure_neural`
  + `apply_neural_meter` depois do quadro Turbo.
