# Aurea V2 — Fase 8: relatório de performance

(Cada frente escreve só a sua seção.)

## 8C — Render / preview

### Ambiente da medição (declarado, não é celular)
- **Host:** Windows 11 Pro 22631, AMD Ryzen 5 5500, 32 GB, **NVIDIA GeForce RTX 3050** (Vulkan 1.4.351), build Release MSVC.
- **Projeto:** 1920×1080 e 3840×2160, 30 fps. **Preview:** FULL (alvo fora da tela do tamanho da composição). Orçamento: 16,67 ms (60) / 33,33 ms (30).
- **FPS/quadros perdidos de aparelho:** não medidos — sem aparelho Android físico na bancada e sem Mac (§ limites da SPEC). Os números abaixo são do motor no host; não são "de celular".
- **Cena:** N imagens 480×270 espalhadas (escala ~0,45 da largura, rotação, opacidade 0,85); "fx" = exposição + brilho/contraste + saturação + tingir + blur 6 px em todas, glow em 1 de cada 5.
- **CPU** = backend falso (só o custo do motor, sem driver). **GPU** = timestamps Vulkan, mediana da 2ª metade de 60 quadros em sequência (2 quadros em voo).
- Reproduzir: `AUREA_BENCH=1 engine/build/host/tests/Release/aurea_tests.exe Perf8C`.

### Qual estágio estoura o orçamento (§5)
No host, **nenhum** cenário passa de 16,67 ms. Pior caso, 4K + 50 camadas com efeitos: GPU 5,65 ms (composição 3,65 + efeitos 1,99), CPU prepare 0,05 ms + gravação 0,16 ms. O estágio dominante é **GPU**: composição (sobreposição de camadas grandes em RGBA16F) no 4K, efeitos no 1080p. A CPU só pesava com muitas camadas — pela compilação quadrática do FrameGraph (corrigida abaixo). Em celular a GPU é a primeira a estourar; é ela que a escada de resolução do AUTO ataca primeiro.

### Antes → depois (medido, mesmo host, mesma cena)

| Cenário | Antes | Depois |
|---|---|---|
| CPU gravação, 1080p 50 camadas fx | 0,646 ms | 0,165 ms |
| CPU gravação, 1080p 200 camadas fx (§127) | 7,931 ms | 0,847 ms |
| CPU prepare, 1080p 200 camadas fx | 0,428 ms | 0,348 ms |
| Alocações por quadro no caminho quente (prepare+render) | 6 (7 com 200 camadas) | **0** |
| Passes, 50 imagens sem efeito | 51 | 1 |
| Passes, 50 imagens fx / 200 fx | 241 / 961 | 191 / 761 |
| GPU 1080p 50 camadas / fx | 1,39 / 3,57 ms | 0,87 / 3,09 ms |
| GPU 4K 50 camadas / fx | 3,39 / 6,14 ms | 2,89 / 5,65 ms |
| GPU 1080p 10 camadas / 4K 10 | 0,28 / 0,61 ms | 0,18 / 0,51 ms |
| Intervalo entre quadros, desvio / p99 — 1080p 50 fx | 0,59 / 4,80 ms | 0,16 / 3,54 ms |
| Intervalo entre quadros, desvio / p99 — 4K 50 fx | 0,72 / 7,49 ms | 0,25 / 6,23 ms |
| Preview AUTO, série FULL=25 ms a 60 fps (3000 quadros) | 34 trocas (FULL↔1/2) | **1** troca |
| Preview AUTO, 6000 quadros, subidas que falham (ruído 10%) | 78 trocas | 13 trocas, 6 subidas, espera 32× |

Frame pacing (§145): o intervalo é a cadência de submissão fora da tela, sem vsync (o host não tem swapchain nos testes) — mede a regularidade do motor, não a do display.

### O que mudou
1. **FrameGraph linear (§24–25):** a busca do produtor varria todos os acessos por acesso (O(n²), com trecho O(n³)). Agora: acessos por recurso em CSR, busca binária nos escritores, fila de Kahn em heap, nascimento/morte de transitórios por posição, ordenação por contagem (o `stable_sort` alocava a cada quadro). Mesma ordem, mesmos aliasings (todos os testes do grafo passam).
2. **Imagem convertida uma vez:** a conversão sRGB→linear rodava a cada quadro para cada camada de imagem (o comentário do shader dizia o contrário). Agora a versão linear na densidade da camada fica em cache (dois tamanhos por imagem, despejo após 240 quadros sem uso, destruição na troca de projeto/dispositivo). Teste na GPU real: **0 texels diferentes** entre o quadro que converte e o que reaproveita; 2 camadas da mesma imagem = 1 conversão.
3. **Zero alocação por quadro (§26–28):** medido com `operator new` instrumentado (só a thread do render conta). Os 6 vinham de listas locais nos uploads de glifos/máscaras/vetores (trocadas por listas do renderer — duas linhas em cada, sem mudar a lógica da 8E).
4. **Preview AUTO 2.0 (§6–11):** decisão por métrica — GPU, CPU, decode, quadros perdidos, pressão de memória, calor e complexidade (troca grande de passes/camadas recomeça a média). Duas escadas: resolução FULL→1/2→1/4→1/8 e reduções (desfoque de movimento, flow, SSAO, sombra, partículas, blur, LOD: 1 → ½ → ¼). CPU no gargalo corta reduções antes da resolução; decode no gargalo **não** derruba resolução (não adiantaria) e fica registrado. Descer: 3 quadros acima de 85% do orçamento na média móvel. Subir: a **previsão** do degrau de cima (razão de custo medida na última descida; 3× se não medida) abaixo de 65% por 60 quadros × espera; subida desfeita em menos de 180 quadros dobra a espera (até 32×), estabilidade por uma janela a reduz. Pisos térmico (quente: reduções ½; crítico: 1/4 + ¼) e de aparelho (`set_quality_floor`, para a 8H) valem na hora.
   - Botões expostos em `RenderSettings::quality` / `Renderer::preview_quality()`. Desfoque de movimento, partículas e flow já respondem (pelo `heavyScale`, que agora é o menor entre calor e AUTO). SSAO, sombra, amostras de blur e LOD estão expostos; **quem os aplica é a 8E/8H**.
   - Telemetria (§164): escala em uso segue no `PerfPOD` (renderScaleNum/Den); `EngineTelemetry` ganhou escala, degrau de reduções, gargalo, espera de subida e camadas podadas. O `PerfPOD` (ABI de 256 bytes, HUD da 8A) não foi mexido.
5. **Pular trabalho (§19–22):** o plano de efeitos passou para antes do decode; camada 2D inteira fora da composição, só com efeitos de cor (sem desfoque/eco/RGB no tempo/perspectiva), não abre decoder, não sobe textura e não gera passe — vale para matte (matte fora da tela recorta igual a matte ausente). Oculta e opacidade 0 já pulavam; efeito desligado e identidade (blur raio 0, exposição 0) já saíam do grafo — agora com teste que conta.
6. **Fusão (§23):** já existia (`color_stack`, até 12 operações, 1 LUT por passe). Verificado: exposição + brilho/contraste + saturação + tingir + matriz de cor + níveis = **1 passe** (sem fusão seriam 6).
7. **Tipos consultados por camada** (posterizar tempo, eco, RGB no tempo) resolvidos no `initialize` (eram 3 buscas lineares por camada por quadro).
8. **Timers de GPU:** 64 → 512 por quadro; acima de 64 passes o detalhe por etapa saía errado (composição 0 ms com 191 passes).

### Verificações (testes, sempre rodam — `Perf8C`)
- AUTO determinístico: mesma série → mesma sequência de escalas; FULL caro assenta em 1/2 com 1 troca; carga no limite com ±25% de ruído: ≤ 1 troca; intervalo entre tentativas de subida nunca encolhe; CPU no gargalo mantém FULL e reduz; decode no gargalo mantém FULL; cena mais leve volta a FULL; memória ≥ 0,9 desce; pisos térmico/aparelho.
- Relógio (§9): quadros de 100 ms a 30 fps → após 3 s a timeline está no quadro 90 (2 derrubados por quadro), nunca atrasada; com relógio de áudio mestre o vídeo segue o áudio.
- Export (§8): com AUTO no degrau mínimo e `heavyScale` 0,25, o export tem 1000 slots de partícula (o preview 250) e `effective_quality` volta tudo a 1.
- Decode: 5 vídeos (visível, oculto, opacidade 0, fora da tela com cor, fora com blur) → **2 decoders abertos**, 1 camada podada. (Antes desta frente a camada fora da tela abria decoder — por construção, não havia poda; não medido separadamente.)
- Regime com 50 camadas fx: 0 texturas criadas por quadro, 190 transitórias em 92 físicas (98 reaproveitadas), 0 alocações.
- Render sob demanda (§39): parado, 20 chamadas + 1,2 s de thread de render ociosa = **0** apresentações; um seek = exatamente 1.
- Suíte: 443 → 460 testes, 0 falhas. Android `assembleDebug` x86_64 compila.

### Lock do modelo (§29–30)
50 camadas com efeitos, render em laço + UI lendo `read_status` a cada 4 ms: espera **p50 0,002 ms**; máximo 5,2 ms, sempre no 1º quadro — o `prepare` copia os pixels de cada imagem nova para o upload sob o lock (50 × 480×270). Em regime o lock só cobre o prepare (0,03–0,35 ms). `submit_commands` é lock-free. O `renderMutex_` fica preso durante a apresentação (espera do vsync): só a prévia de efeito e a captura disputam com ele. **Pendente:** tirar a cópia de pixels do lock exige mudar a posse de `Engine::images_`.

### Vulkan (§140–142)
- **Camada de validação ausente neste host** (sem Vulkan SDK; sem downloads): não verificável aqui. O backend agora conta erros/avisos da validação (`vk::Backend::validation_errors()`) e o teste `VulkanCachedImageIsPixelExactAndValidationClean` exige zero quando a camada existir.
- `vkDeviceWaitIdle` só em `wait_idle` (desligar, recuperar dispositivo, captura fora da tela), `detach_surface` e recriação do swapchain. Nenhum no playback: `begin_frame` espera só a cerca do quadro N−2 (2 em voo) → CPU e GPU sobrepostas; uploads no quadro vão pelo anel de staging.

### Android
- `RenderLoop.kt` **não** dispara render: é o laço de estado da UI (Choreographer). O motor tem thread própria, acordada por comando/decoder, que dorme parado (acorda no máximo a cada 500 ms e não desenha). Achado para a 8D/8H: esse laço de estado chama `readStatus` (JNI + lock do modelo) a cada vsync mesmo com o editor parado.

### Pendências honestas
- Números em aparelho real (FPS, quadros perdidos, térmico) — sem aparelho.
- SSAO, resolução de sombra, amostras de blur e LOD: botões prontos, sistemas (8E) ainda não leem.
- Cópia de pixels de imagem nova sob o lock do modelo (1º quadro).
