# Desempenho

Estado: **métricas implementadas**; números medidos listados abaixo com a
origem. Aparelho real ainda não medido (nenhum conectado até agora).

## Orçamentos

- Preview a 60 Hz: 16,67 ms por frame; a 30 Hz: 33,33 ms. O
  `AdaptiveResolutionController` (modo AUTO) usa o custo medido do frame para
  descer/subir a escala do preview (1/2, 1/4, 1/8) sem mudar coordenadas.
- O preview redesenha na taxa do CONTEÚDO (ver RENDERER.md, "ritmo"), não na do
  painel; a UI (Compose) roda na taxa do painel, independente do preview.
- Miniaturas em prioridade de fundo; preview sempre na frente; export (futuro)
  separado.

## O que é medido (`bridge::PerfPOD`, painel DEV)

Preview FPS e FPS da UI; CPU do frame (prepare + gravação); GPU do frame e por
estágio via timestamp queries (conversão de cor, efeitos, blur, glow,
composição, saída); decode médio; aquisição e present do swapchain; último seek;
frames perdidos (total e janela recente); escala do preview e resolução; frames e
bytes no cache de decode; RAM (DeviceCapabilities — no Android por
`/proc/meminfo`) e memória de GPU; passes executados/cortados; texturas físicas,
aliasadas e criadas no frame; pipelines totais e compilados "ao vivo" (depois do
estado estável); zero-copy ligado; decoder e se é hardware; seeks e pedidos
coalescidos; frames aproximados (scrub); camadas renderizadas; estado térmico.

O painel DEV do editor mostra esses campos por cima do preview (leitura a cada
250 ms, só com o painel aberto).

## Garantias com teste

- Playback estável não cria textura (TransientTexturePool) — `test_gpu.cpp`.
- Nenhum pipeline compilado durante o playback depois do pré-aquecimento (15
  pipelines na inicialização; compilação tardia gera aviso no log).
- Scrub de 12 posições em rajada faz no máximo 3 seeks (coalescência) —
  `test_gpu.cpp`/`test_media.cpp`.

## Números medidos

Emulador Android (API 34, x86_64, GPU do host — RTX 3050 — via gfxstream;
caminho de planos pela CPU porque o gfxstream não amostra YUV externo), vídeo
H.264 1280×720 30 fps, antes do ritmo por conteúdo:

| Métrica | Valor |
|---|---|
| Preview (apresentação) | 60 fps, 0 frames perdidos |
| CPU do frame | 9,6 ms (CPU x86 emulada) |
| GPU do frame | 0,4 ms |
| Decode médio | 2,2 ms/frame |
| Aquisição / present | 4,5 ms / 0,8 ms |
| Com desfoque gaussiano | 8 passes, blur 0,1 ms de GPU |
| Com desfoque + Motion Tile | 9 passes, 2 texturas aliasadas |
| Seek | 1,4 – 4,3 ms |
| Cor (barras do testsrc2 vs ffmpeg) | erro máximo 3/255 |

Host (Windows, RTX 3050): 243 testes do motor, 0 falhas, incluindo o backend
Vulkan real e os golden frames.

## Pendente

Medir em aparelho Android real (zero-copy, 1080p/4K, temperatura, memória) e
refazer a tabela com o ritmo por conteúdo ativo.
