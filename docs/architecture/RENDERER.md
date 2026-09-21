# Renderer

## A regra

Nada acima de `GPUBackend` menciona Vulkan ou Metal. O compositor, o grafo de
efeitos, o motor 3D e o export falam em `TextureHandle`, `PipelineHandle` e
`CommandList`.

Consequência: o MESMO código de composição roda no preview Android (Vulkan) e no
preview iOS (Metal). Preview e export serem visualmente idênticos deixa de
depender de disciplina e passa a ser consequência de arquitetura — só existe uma
implementação.

```
        ┌────────────────────────────┐
        │      Compositor            │
        │  FrameGraph · EffectGraph  │   ← não conhece API gráfica
        └─────────────┬──────────────┘
                      │  GPUBackend (interface pura)
          ┌───────────┴───────────┐
          │                       │
     VulkanBackend           MetalBackend
      SPIR-V                  MSL (de SPIR-V)
```

## Zero-copy

O ponto crítico do pipeline. Um frame decodificado pelo hardware chega como
`AHardwareBuffer` (Android) ou `CVPixelBuffer` (iOS). A ponte o importa como
textura amostrável **sem cópia de CPU**.

```
Android:
  MediaCodec → Surface / AHardwareBuffer → Vulkan → FrameGraph → Display

iOS:
  VideoToolbox → CVPixelBuffer → CVMetalTexture → Metal → FrameGraph → Display
```

A sincronização é responsabilidade do backend: o decoder sinaliza um fence, o
backend o espera antes de amostrar. Devolver a textura antes do fence é o bug
clássico de frame rasgado — por isso a interface só expõe
`import_external_image`, que já cuida disso.

## Fonte única de shader

O shader é escrito **uma vez**, em GLSL, em `engine/include/aurea/shaders/`.
Android recebe SPIR-V. iOS recebe MSL gerado de SPIR-V no build (SPIRV-Cross),
não escrito à mão.

Convenções:

- espaço de cor **linear**. A conversão de sRGB acontece na importação; a de
  saída, no passe final. Um shader de efeito nunca converte sozinho;
- sem ramificação dependente de dado quando a mesma conta sai por aritmética.
  Em GPU mobile um `if` divergente custa mais que uma multiplicação por zero.

## Cache de pipeline

Compilar um pipeline custa de 5 a 200 ms. Três defesas:

1. **cache por chave estrutural** — `(shader, blend, formats, samples)`. Duas
   camadas com o mesmo blend compartilham o pipeline;
2. **pré-aquecimento** ao abrir o projeto e antes de exportar. Nenhum pipeline
   novo durante o playback nem no meio da exportação;
3. **invalidação sem perda** — perder o dispositivo apaga os handles do driver
   mas mantém as chaves. O cache do motor continua válido.

Se um pipeline não está em cache e não compila, o passe é **pulado** e o fato
aparece na telemetria. Nunca se desenha com pipeline inválido, e nunca se finge
que o efeito rodou.

## Orçamento de frame

O orçamento vem da cadência REAL do display:

| Display | Orçamento |
| --- | --- |
| 60 Hz | 16,67 ms |
| 90 Hz | 11,11 ms |
| 120 Hz | 8,33 ms |

Divisão: decode 15%, apresentação 10%, reserva 20%, render 55%. A reserva existe
porque um frame que usa 100% do orçamento já está perdido — a variação normal o
empurra para fora.

## Preview adaptativo

Ordem de degradação, sempre nesta ordem:

```
1. sobra tempo?              não faz nada
2. passou do orçamento?      reduz a resolução um degrau (1 → 1/2 → 1/4 → 1/8)
3. ainda passa?              reduz efeitos caros para a versão de preview
4. aqueceu?                  reduz mais cedo, antes de o aparelho travar
```

Anti-oscilação: descer exige 3 frames seguidos acima do orçamento; subir exige
90 frames confortavelmente abaixo. Todo degrau trava por 30 (descer) ou 60
(subir) frames. Sem isso a resolução pisca, o que é pior do que ficar baixo.

A escolha manual do usuário **nunca** é sobreposta pelo automático. Ele mandou;
ele vê o resultado — e a UI avisa se ficar lento, em vez de desobedecer.

**O export nunca degrada.** Preview e export compartilham timeline, compositor,
shaders, pipeline de cor, 3D, máscaras e avaliação de animação. O que muda é só
o quanto o preview degrada.

## Estado atual

Nenhum backend existe. `GPUBackend::create_default()` devolve `nullptr`, e o
`Engine` trata isso: registra um aviso, desativa o preview e segue de pé. Não é
um stub fingindo funcionar — é a resposta correta para "que backend usar?".

O que falta, em ordem:

1. `VulkanBackend` — device, swapchain, import de `AHardwareBuffer`,
   compilador de SPIR-V em runtime para as variantes;
2. passes de composição no FrameGraph;
3. `MetalBackend` — device, `CAMetalLayer`, import de `CVPixelBuffer`,
   carregamento do MSL traduzido no build.
