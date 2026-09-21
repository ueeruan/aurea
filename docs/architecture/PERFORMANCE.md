# Performance

## O princípio

```
CPU organiza.  GPU processa.  Hardware decodifica.  Hardware codifica.
UI apenas controla.
```

As três coisas proibidas:

- `GPU → CPU → GPU` — ler o frame de volta entre passes;
- `decode → bitmap → Swift/Kotlin → UI → GPU`;
- qualquer processamento pesado na thread da UI.

## Onde o tempo vai, e como cada item é atacado

| Custo | Atacado por |
| --- | --- |
| Alocação por frame | arena de bump; buffers reusados; nenhuma alocação no caminho de frame |
| Travessias de bridge | um lote de comandos por frame; status POD (3 chamadas no total) |
| Compilação de pipeline | cache por chave estrutural + pré-aquecimento |
| Banda de memória dos efeitos | fusão de efeitos (N passes virando 1) |
| Pico de memória dos intermediários | aliasing no FrameGraph |
| Decode repetido | cache de frames com chave de conteúdo + prefetch |
| Resolução | preview adaptativo com orçamento medido por display |
| Recompilação do grafo | revisão estrutural — só recompila quando a topologia muda |

## Orçamento de frame

```
60 Hz  → 16,67 ms
90 Hz  → 11,11 ms
120 Hz →  8,33 ms
```

Divisão do orçamento: decode 15%, apresentação 10%, reserva 20%, render 55%.

A reserva não é folga: um frame que usa 100% do orçamento já está perdido,
porque a variação normal o empurra para fora. O limiar de conforto é 80%.

## Threads

| Thread | O quê | Nunca faz |
| --- | --- | --- |
| UI | gestos, painéis, escrita de comandos | processar frame, esperar o motor |
| Render | fila, animação, grafo, submissão | esperar a GPU terminar o frame anterior |
| Workers | decode, proxy, miniatura, waveform, geometria | bloquear esperando outro worker |
| Áudio | mixer — o master clock | esperar o vídeo |

### Dimensionamento do pool

`recommended_worker_count()` usa núcleos de **performance**, não o total, e
deixa dois de folga (um para a thread de render, um para o sistema).

Um pool que ocupa os 8 núcleos de um big.LITTLE deixa a thread de render sem
CPU — o scheduler do sistema a tira no meio do frame, e o ganho dos núcleos
extras some. O teto também é limitado por memória disponível: mais workers é
menos memória para cache, e menos cache é mais decode repetido.

`decode_parallelism()` é limitado pelo número de **instâncias de decoder de
hardware**. Pedir 8 decodes a um aparelho com 2 instâncias faz 6 esperarem —
mais lento do que pedir 2.

### `wait()` que não bloqueia

Se quem espera é um worker, ele **trabalha** em vez de bloquear. Bloquear um
worker esperando outro é como um pool de N threads vira pool de 1 — ou trava de
vez quando todos esperam.

## Memória

Orçamento por categoria, derivado da RAM medida do aparelho, não de uma
constante escolhida no escuro:

| Categoria | Fatia | Ordem de descarte |
| --- | --- | --- |
| Thumbnails | 6% | 1ª |
| Proxies | 10% | 7ª |
| DecodedFrames | 24% | 3ª |
| RenderedFrames | 16% | 2ª |
| GpuTextures | 20% | 5ª |
| GpuGeometry | 10% | 4ª |
| Audio | 4% | 6ª |
| Assets | 8% | — |
| Persistent | 2% | **nunca** |

A ordem de descarte é por custo de refazer: uma miniatura custa um decode
pequeno; um frame decodificado custa um decode grande; geometria custa um
re-upload.

`Persistent` (o projeto aberto) **nunca** é recusado nem descartado. Se ele não
couber, o problema é o teto — e falhar aqui perderia trabalho do usuário.

Antes de recusar uma reserva, o gerenciador **pede** aos caches da categoria que
liberem. Recusar sem tentar seria deixar memória ocupada por cache enquanto o
trabalho real falha.

## Cache

Sete níveis: `DecodedFrameCache`, `RenderedFrameCache`, `ThumbnailCache`,
`AudioCache`, `AssetCache`, `ShaderCache`, `DiskCache`.

A chave do cache de frames combina **tudo** que influencia o frame: fonte, tempo,
transform avaliado, propriedades de efeito, máscaras, dependências. Se nada
mudou, não se renderiza de novo — é o que faz arrastar um slider parado não
custar nada, e o que faz o scrubbing para trás ser rápido.

## Prefetch

| Situação | Janela |
| --- | --- |
| Parado | simétrica curta (o usuário pode ir para qualquer lado) |
| Playback / scrub para frente | 1 atrás, `n+2` à frente |
| Scrub para trás | `n+2` atrás, 1 à frente |

Pré-decodificar para trás durante um arrasto para a frente é trabalho jogado
fora — o usuário nunca vai ver aqueles frames.

## Térmico

Monitorado por `ThermalState`, informado pela plataforma (`PowerManager` no
Android, `ProcessInfo` no iOS).

| Estado | Ação |
| --- | --- |
| Nominal / Fair | nada |
| Serious | para de subir a resolução |
| Critical / Emergency | desce mesmo com o frame dentro do orçamento |

Descer no nível crítico acontece **antes** de o frame estourar: o aparelho VAI
estrangular, e esperar o estrangulamento produzir engasgo.

A ordem de degradação nunca mata o playback: reduz preview, mantém a UI fluida,
e só o export nunca degrada.

## Telemetria

Painel de debug com: FPS, tempo de frame, tempo de GPU, tempo de CPU, decode,
frames perdidos, RAM, memória de GPU, taxa de acerto de cache, passes, draw
calls, triângulos, efeitos ativos, camadas ativas.

Sem timestamp query disponível, o painel mostra **"não medido"** em vez de zero —
um número inventado seria pior do que a ausência do número.

## O que falta medir

O motor roda hoje headless: sem backend gráfico, sem decode, sem composição. Os
números de frame time e de banda acima são **orçamentos de projeto**, derivados
de medição de aparelho para este tipo de pipeline — não medições do Aurea.

A primeira tarefa depois do backend Vulkan é instrumentar e substituir estes
números pelos reais.
