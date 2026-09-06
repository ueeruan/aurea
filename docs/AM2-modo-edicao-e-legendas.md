# AM2 STUDIO — MODO EDIÇÃO (NLE) + LEGENDAS AUTOMÁTICAS
### Especificação: editor de vídeo rápido no mesmo app do compositor, com Whisper on-device
**Base:** `AM2_STUDIO.md`, `AM2-auditoria-e-correcoes.md`, `AM2-motor-de-texto.md`.
**Pressuposto:** as fases 0 a 4 da auditoria já entraram (testes, bugs do preview, mixagem de áudio, matte, grupo, controles de efeito).

---

## 0. A DECISÃO DE ARQUITETURA

Um compositor e um editor de vídeo são motores com objetivos opostos:

| | Compositor (AE / o que você tem) | NLE (CapCut / o que falta) |
|---|---|---|
| Unidade | Camada empilhada, todas ativas ao mesmo tempo | Clipe sequencial, um por vez na trilha |
| Render | Grafo completo por frame | Passthrough do stream, grafo só quando há efeito |
| Seek | Recalcula tudo | Precisa ser instantâneo |
| Gargalo | GPU | Decoder de vídeo e I/O |
| Timeline | Vertical, poucas camadas, muitas propriedades | Horizontal, muitos clipes, poucas propriedades |

**Não force os dois na mesma tela.** A saída é:

```
Documento único (project.json)
├── Sequence          ← MODO EDIÇÃO (novo). Trilhas, clipes, corte, legendas
│   └── clipe pode referenciar uma Scene inteira
└── Scene             ← MODO COMPOSIÇÃO (o que você já tem). Camadas, keyframes, efeitos
```

Regras:
- Um projeto abre em **Modo Edição** por padrão quando começa por mídia, e em **Modo Composição** quando começa em branco.
- Tocar duas vezes num clipe → entra em Composição **daquele clipe**. Voltar → retorna à sequência. É o "abrir precomp" do AE, com cara de CapCut.
- Uma `Scene` usada como clipe é renderizada uma vez e cacheada; não reprocessa a cada frame da sequência.
- **Um documento, dois modos.** Nada de dois formatos de arquivo — esse erro já existe entre o Android e o protótipo Web e não pode se repetir aqui.

---

## 1. POR QUE TRAVA HOJE

Diagnóstico do pipeline de vídeo atual, a partir do que o seu documento descreve.

| # | Situação atual | Consequência |
|---|---|---|
| L1 | **Sem proxy.** `proxy persistente ou cache de vídeo editável` está em "Ausente" | Cada scrub num 4K H.265 decodifica desde o keyframe anterior. É a causa nº1 do lag |
| L2 | **Um `VideoSource` por camada**, mesmo apontando para o mesmo arquivo | `MediaCodec` de hardware é recurso escasso. Timeline com 15 clipes estoura o limite e cai para software ou falha |
| L3 | **Sem cache de frames decodificados** | Voltar 1 frame custa o mesmo que um seek longo |
| L4 | **Sem filmstrip na timeline** | Não dá para achar o corte olhando; só tateando |
| L5 | **Export com seek bloqueante por frame** | Num vídeo sequencial de 10 min isso é ordens de grandeza mais lento que decodificar em sequência |
| L6 | **Sem prefetch** durante playback | Engasgo em cada troca de clipe |
| L7 | Grafo do compositor roda mesmo em clipe sem efeito | Custo de GPU desnecessário no caso mais comum de um NLE |

L1, L2 e L5 são estruturais. Os outros são consequência.

---

## 2. MÍDIA OTIMIZADA — A BASE DE TUDO

### 2.1 Proxy na importação
Ao importar mídia, gerar em segundo plano uma versão de edição:

| Parâmetro | Valor |
|---|---|
| Resolução | eixo longo 960 px (config: 540 / 960 / 1280) |
| Codec | H.264 baseline/main |
| **GOP** | **keyframe a cada 6 frames** — este é o ponto inteiro |
| Bitrate | ~4 Mbps |
| Áudio | AAC 128k, e **PCM 16 kHz mono separado** para waveform e Whisper |
| Local | `filesDir/proxies/<sha1-do-uri+mtime>.mp4` |

GOP curto é o que transforma seek de "decodificar 60 frames" em "decodificar 3". É exatamente o que Premiere e CapCut fazem, e é a diferença entre scrub fluido e scrub travado.

Regras:
- Gerar com `MediaCodec` + `Surface`, em `WorkManager` ou serviço em primeiro plano, com progresso visível e cancelável.
- **Edição não espera o proxy.** Enquanto não existe, usa o original e mostra selo "otimizando".
- Alternar proxy/original por botão, e **sempre usar o original no export**.
- Pular proxy se o original já for pequeno (≤1080p, ≤10 Mbps, GOP curto).
- Cache com teto configurável e limpeza LRU. Mostrar quanto ocupa nas preferências.

### 2.2 Filmstrip
Na importação, extrair miniaturas a cada N frames (N conforme o zoom da timeline), 96 px de altura, JPEG q70, em `filesDir/filmstrips/<hash>/`. A timeline desenha a tira dentro da barra do clipe. Sem isso não existe decupagem visual.

### 2.3 Pool de decoders
```kotlin
val max = MediaCodecList(REGULAR_CODECS)
    .findDecoderForFormat(fmt)
    .let { codecInfo.getCapabilitiesForType(mime).maxSupportedInstances }
```
- Teto real do aparelho, com piso de segurança (assuma 4 se a consulta falhar).
- LRU: só os clipes na janela `[playhead - 1s, playhead + 3s]` seguram decoder.
- Clipe fora da janela mostra o frame congelado do cache ou a miniatura do filmstrip.
- Dois clipes do mesmo arquivo com faixas de tempo próximas **compartilham** o decoder.
- Ao estourar o limite: liberar o menos recente, nunca falhar.

### 2.4 Cache de frames
- Anel de texturas em volta do cabeçote, orçamento em MB, não em quantidade.
- Durante playback: prefetch de N frames à frente numa thread própria.
- Durante scrub: `SEEK_TO_CLOSEST_SYNC` (só keyframes) enquanto o dedo está na tela; ao soltar, decodifica o frame exato. Isso é o segredo do scrub fluido.
- Ao pausar, decodificar o frame exato e substituir o aproximado.

### 2.5 Caminho rápido de render
Se o clipe tem transform identidade ou simples, sem efeito, sem máscara, sem blend diferente de Normal e sem opacidade animada: **blit direto da textura OES para a tela**, pulando o grafo. Marque a condição num flag recalculado quando a camada muda, não por frame.

### 2.6 Export sequencial
Reescrever a exportação para NLE: por clipe, decodificar **em sequência do in ao out**, sem seek por frame. Seek uma vez, no início do clipe. Ganho tipicamente de uma ordem de grandeza em vídeo longo.

Bônus: se o clipe não tem nenhum efeito, mesma resolução/codec do destino e não é retimado, considerar **remux** — copiar os pacotes comprimidos sem reencodar. Corte puro fica quase instantâneo.

---

## 3. MODO DECUPAGEM

O pedido "assistir e decupar vídeos rapidamente" é uma tela própria, não um uso da timeline.

```
┌──────────────────────────────┐
│                              │
│      PLAYER EM TELA CHEIA    │
│                              │
├──────────────────────────────┤
│ ▶ 1.5×  [pular silêncio ✓]   │
├──────────────────────────────┤
│ ~~~ waveform + marcas ~~~    │
├──────────────────────────────┤
│  [I] marcar in  [O] marcar out│
│  [★] favoritar  [#] etiqueta  │
├──────────────────────────────┤
│ Trechos marcados (12)        │
│ ▸ 00:04–00:11  ★ "abertura"  │
│ ▸ 00:32–00:47    "take bom"  │
└──────────────────────────────┘
```

Funções:
- **Velocidade 1×, 1.25×, 1.5×, 2×** com pitch preservado. Você já tem isso.
- **Pular silêncio ao assistir** — reaproveita seu detector (-40 dB, 0,35 s, padding 0,08 s) como modo de reprodução, não só como comando destrutivo. Assistir bruto em metade do tempo.
- **Marcar in/out** com toque, gerando subclipes.
- **Etiquetas e favorito** por trecho, filtráveis.
- **Detecção de cena**: diferença de histograma em frames de baixa resolução do proxy, marcando cortes automáticos. Rodar no mesmo passo do filmstrip, praticamente de graça.
- **Enviar selecionados para a timeline**, na ordem marcada.
- Gestos: arrastar horizontal = shuttle; duplo toque nas laterais = ±10 s; segurar = 2×.

**E o mais rápido de todos: decupagem por transcrição.** Depois do §8, o texto do Whisper vira a interface de corte — apagar uma frase no transcript apaga o trecho correspondente. É como o Descript funciona, é o método mais rápido que existe para material falado, e no seu caso sai quase de graça porque a transcrição já vai estar lá.

---

## 4. TIMELINE DE EDIÇÃO

Trilhas horizontais, magnéticas por padrão.

| Trilha | Conteúdo |
|---|---|
| V1..Vn | Vídeo, imagem, cenas de composição |
| A1..An | Áudio |
| T | Texto e legendas |
| S | Stickers e overlays |

### Gestos
| Gesto | Ação |
|---|---|
| Arrastar clipe | Move; com trilha magnética, empurra os vizinhos |
| Arrastar borda | Trim, com ripple opcional |
| Pinça | Zoom temporal |
| Toque no clipe | Seleciona e abre o inspetor daquele clipe |
| Toque longo | Menu do clipe |
| Arrastar vertical | Troca de trilha |
| Duplo toque | Abre em Modo Composição |

### Ferramentas
- **Dividir no cabeçote** — você já tem.
- **Ripple delete** (apaga e fecha o buraco) e **lift** (apaga e deixa o buraco).
- **Fechar todos os buracos** da trilha.
- **Inserir** e **sobrescrever** ao arrastar da biblioteca.
- **Trim em rolagem** entre dois clipes adjacentes.
- **Snap** em: frame, cabeçote, bordas de clipe de qualquer trilha, marcadores, batidas detectadas.
- **Seleção múltipla** e mover em bloco.
- **Copiar/colar clipe** com todos os atributos.
- **Substituir clipe** mantendo duração e atributos.

### Velocidade
- Constante de 0,1× a 100× (o teto atual de 4× é baixo para timelapse).
- **Curva de velocidade** com keyframes, reusando seu editor de curvas. Presets: montagem, herói, bala, salto, flash.
- **Congelar frame** no cabeçote, com duração ajustável.
- **Reverso** (exige indexar keyframes do clipe; para material longo, só com proxy).
- Opção "manter tom do áudio" ao mudar velocidade.

### Transições
Entre clipes adjacentes, com alça arrastável para a duração: dissolve, dip to black/white, wipe direcional, whip pan, zoom, glitch. Implementadas como efeito de dois entrados no seu grafo, o que você já suporta.

---

## 5. ÁUDIO

Pré-requisito já apontado como **P0** na auditoria e agora bloqueante também para legendas:

- **Mixagem multitrilha real no export**, com ganho, mute, fade e recorte por camada.
- **Ducking automático**: música abaixa sob a fala, dirigido pelo envelope de voz. Você já extrai waveform, então é aplicar um seguidor de envelope com attack/release.
- **Detecção de batida** para sincronizar cortes; marcar as batidas na régua.
- **Fade in/out** por clipe com alça na timeline.
- **Normalização** para -14 LUFS (destino de rede social) ou -16 LUFS.
- **Redução de ruído** simples: subtração espectral com perfil dos primeiros 500 ms de silêncio detectado.
- **Separar áudio do vídeo** com um toque.

---

## 6. LEGENDAS AUTOMÁTICAS COM WHISPER

### 6.1 Runtime e modelos

**Escolha: `whisper.cpp` via NDK + JNI.** Roda offline, aceita modelos quantizados, sem dependência de Python nem de servidor.

- Compilar com NEON. Avaliar o backend GPU (Vulkan) da versão do whisper.cpp usada, medindo antes de adotar — em vários aparelhos a CPU com 4 threads ainda ganha.
- Modelos `ggml-*.bin`, quantizados. Tamanhos aproximados:

| Modelo | q5_1 aprox. | Uso |
|---|---|---|
| tiny | ~31 MB | Rascunho, tempo real |
| **base** | **~57 MB** | **Padrão recomendado para PT-BR** |
| small | ~181 MB | Alta qualidade, opt-in |
| medium | ~514 MB | Só em aparelho topo, com aviso |

- **Multilíngue obrigatório** para português. Os modelos `.en` não servem.
- **Não embarque o modelo no APK.** Baixar no primeiro uso, verificar hash, guardar em `filesDir/models/`, mostrar tamanho e permitir apagar nas preferências.
- **Não invente número de velocidade.** Meça no seu conjunto de aparelhos alvo e mostre uma estimativa real na UI antes de começar ("~2 min para 10 min de vídeo neste aparelho").

### 6.2 Pipeline de áudio
Whisper exige **PCM float 16 kHz mono**. Nada disso é opcional.

```
clipe/sequência
  → MediaExtractor + MediaCodec (decode)
  → downmix para mono
  → resample para 16 kHz            ← use um resampler de qualidade, não decimação
  → normaliza para float [-1, 1]
  → VAD segmenta em trechos de fala
  → whisper.cpp por trecho
  → segmentos + palavras com tempo
```

O PCM 16 kHz já é gerado no passo do proxy (§2.1). Aproveite: transcrever não deve custar uma segunda decodificação.

### 6.3 VAD antes do Whisper — não é opcional
Dois motivos:

1. **Velocidade.** Pular silêncio corta o tempo de transcrição proporcionalmente. Em entrevista bruta, cai pela metade.
2. **Alucinação.** Whisper alucina em silêncio, e em português costuma inventar frases de crédito de legendagem e repetições em laço. É comportamento conhecido do modelo, não bug seu.

Mitigações combinadas:
- VAD na frente (o seu detector de silêncio já serve; Silero VAD é melhor se couber).
- `no_speech_threshold` e `logprob_threshold` ativos, descartando segmento abaixo do limiar.
- `condition_on_previous_text = false` — é o que trava o laço de repetição.
- Descartar segmento cujo texto repete o anterior mais de duas vezes.
- Descartar segmento com duração incompatível com a contagem de caracteres.

### 6.4 Timestamps por palavra
Ligar `token_timestamps`. Onde a versão do whisper.cpp oferecer alinhamento DTW com *aheads* do modelo, use — o tempo por palavra fica bem melhor. Isto é o que habilita legenda karaokê.

Saída canônica:
```json
{
  "language": "pt",
  "segments": [
    { "start": 1.24, "end": 3.02, "text": "isso muda tudo",
      "confidence": 0.91,
      "words": [
        {"w": "isso",  "s": 1.24, "e": 1.41},
        {"w": "muda",  "s": 1.41, "e": 1.78},
        {"w": "tudo",  "s": 1.78, "e": 3.02}
      ] }
  ]
}
```

### 6.5 Modelo de dados — camada de legenda

**Não crie uma camada de texto por fala.** Um vídeo de 5 minutos gera facilmente 300 falas, e 300 camadas destroem a timeline, o undo e o desempenho.

```text
CaptionLayer : Layer
  cues: Cue[]
  style: CaptionStyle
  wordHighlight: WordHighlightSpec?
  maxCharsPerLine: Int = 42
  maxLines: Int = 2
  minCueDuration: Float = 0.8
  language: String

Cue
  start, end: Float
  text: String
  words: Word[]
  locked: Boolean          // não reescrever se o usuário editou
```

Uma camada, muitos cues. A timeline desenha os cues como marcas dentro da barra da camada. Renderiza só o cue ativo no tempo atual.

Segmentação em cues:
- Quebrar por pontuação primeiro, depois por pausa maior que 0,4 s, depois por limite de caracteres.
- Nunca quebrar dentro de palavra.
- Duração mínima de 0,8 s; unir cues curtos vizinhos.
- Máximo 2 linhas, ~42 caracteres por linha (ajustável).

### 6.6 Estilo e presets
`CaptionStyle` reaproveita tudo do §10 do motor de texto: fonte, tamanho, cor, contorno, sombra, caixa de fundo com raio e opacidade, posição segura, margem.

Presets: Limpo, Contorno grosso, Caixa preta, Podcast, Karaokê, Neon, Legenda de cinema.
**Aplicar estilo a todos os cues** é um botão só.

### 6.7 Karaokê — onde os dois sistemas se encontram
Esta é a parte elegante. Os tempos por palavra do Whisper alimentam diretamente o **seletor de intervalo em unidades INDEX** do motor de texto:

```
para o cue ativo:
  seletor.units    = INDEX
  seletor.basedOn  = WORDS
  seletor.start    = índice da palavra atual
  seletor.end      = índice da palavra atual + 1
  animador.fillColor = cor de destaque
  animador.scale     = 108%
```

Arrastar o `offset` no tempo dá o efeito de preenchimento progressivo. Modos: palavra por palavra, preenchimento contínuo, pop, e revelar acumulado.

**O motor de texto que você vai construir paga por si mesmo aqui.** Nenhum código novo de animação para karaokê.

### 6.8 Edição das legendas
Tela dedicada, lista de cues:
- Editar texto no lugar, com teclado.
- **Unir** e **dividir** cue.
- Arrastar as bordas de tempo, com snap na palavra.
- Marcar cue como `locked` — regerar a transcrição não sobrescreve o que foi editado à mão.
- Buscar e substituir em todos os cues (nome de marca escrito errado, resolvido de uma vez).
- Realçar em amarelo os cues com confiança baixa, para revisão dirigida.
- **Modo transcrição:** o texto corrido inteiro; apagar uma frase apaga o trecho do vídeo. Decupagem por texto, conforme o §3.

### 6.9 Importar e exportar
- Exportar **SRT** e **VTT**.
- Importar SRT/VTT sobre a camada de legenda.
- No export de vídeo: legenda queimada (padrão) ou **faixa de legenda separada** no MP4 (`mp4v` timed text via `MediaMuxer.addTrack` com `application/x-subrip`) — útil para quem sobe no YouTube.

### 6.10 Comportamento do trabalho
- Roda em **serviço de primeiro plano** com notificação, progresso e cancelar.
- **Não bloqueia a edição.** Enquanto transcreve, dá para continuar cortando.
- Pausar e retomar; sobreviver a rotação e a app em segundo plano.
- Ao terminar: resumo com quantidade de cues, idioma detectado, tempo gasto e quantos cues ficaram com confiança baixa.
- Escopo: sequência inteira, clipe selecionado, ou intervalo in/out.

### 6.11 Privacidade e limites
- **Tudo roda no aparelho.** Nenhum áudio sai do telefone. Diga isso na tela — é uma vantagem real sobre o CapCut e ponto de venda legítimo.
- Whisper **não separa quem falou.** Diarização exige outro modelo e não é realista on-device hoje. Não prometa "identificar falantes"; ofereça rótulo manual por cue.
- Qualidade cai com ruído, sobreposição de vozes e sotaque carregado. Mostre a confiança em vez de fingir precisão.

---

## 7. EXPORT

| Item | Requisito |
|---|---|
| Resolução | 480p a 4K, com "igual ao projeto" |
| FPS | 24 / 25 / 30 / 50 / 60 |
| Bitrate | Slider com estimativa de tamanho em MB, e modo "recomendado" |
| Codec | H.264 sempre; HEVC quando o aparelho suportar |
| Áudio | AAC 128–320 kbps, mixado de verdade |
| Legenda | Queimada ou faixa separada |
| Alfa | Sequência PNG ou WebM VP9 |
| **Remux** | Corte puro sem efeito não reencoda |
| Presets | TikTok/Reels, YouTube, WhatsApp, Máxima qualidade |

---

## 8. ORÇAMENTO DE DESEMPENHO

Metas mensuráveis. Sem número, "sem lag" não é verificável.

| Métrica | Meta | Aparelho de referência |
|---|---|---|
| Scrub → primeiro frame na tela | **< 80 ms** | médio de 2023 |
| Playback 1080p30, até 3 camadas | **0 frames perdidos** em 60 s | médio |
| Scroll e zoom da timeline | 60 fps constantes | médio |
| Abrir projeto de 100 clipes | < 1,5 s | médio |
| Proxy de 10 min de 4K | < 3 min em segundo plano | médio |
| Export 1080p30 de 5 min | ≥ 1× tempo real | médio |
| Corte puro com remux | ≥ 10× tempo real | qualquer |
| Pico de RAM em projeto de 30 clipes | < 500 MB | médio |
| Instâncias de `MediaCodec` vivas | ≤ limite do aparelho, sempre | qualquer |

Instrumente com trace e falhe o CI se uma meta regredir.

---

## 9. TESTES

**Vídeo**
- Seek aleatório 200× num clipe de 10 min: nenhum crash, p95 abaixo da meta.
- Timeline com 40 clipes do mesmo arquivo: nunca ultrapassa o limite de decoders.
- Scrub para trás 500 frames: sem vazamento de memória.
- Export com e sem remux produzem a mesma duração e o mesmo áudio.
- Frame exato: pedir o frame 1237 direto e após reproduzir dá bit a bit o mesmo (I1).

**Legendas**
- Áudio de referência de 60 s em PT-BR: WER abaixo do limite acordado, medido contra transcrição humana. Fixe o número quando medir a primeira vez e trate regressão como bug.
- Áudio **só com silêncio**: resultado tem **zero** cues. Este é o teste anti-alucinação e vai falhar sem VAD.
- Cue com `locked = true` sobrevive a uma nova transcrição.
- Round-trip SRT: exportar e importar dá cues idênticos.
- Karaokê: no meio de uma palavra, o seletor destaca exatamente aquela palavra.
- Serviço morto pelo sistema no meio: retoma sem duplicar cues.

---

## 10. PLANO DE PRs

**PR-V1 — Proxy e filmstrip.** Geração em segundo plano, cache com LRU, alternar proxy/original, PCM 16 kHz mono como subproduto. Filmstrip desenhado na timeline.

**PR-V2 — Pool de decoders e cache de frames.** Limite real do aparelho, LRU por janela do cabeçote, compartilhamento entre clipes do mesmo arquivo, anel de frames, prefetch, scrub por keyframe. Metas do §8 medidas no CI.

**PR-V3 — Modelo Sequence e Modo Edição.** Trilhas, clipes, magnetismo, ripple, snap, inserir/sobrescrever, dividir, seleção múltipla. Duplo toque abre o clipe em Composição.

**PR-V4 — Caminho rápido e export sequencial.** Blit direto para clipe sem efeito; export decodificando em sequência; remux quando aplicável.

**PR-V5 — Modo Decupagem.** Player, velocidade com pitch, pular silêncio ao assistir, marcar in/out, etiquetas, detecção de cena, enviar para a timeline.

**PR-V6 — Velocidade, congelar, reverso, transições.**

**PR-V7 — Áudio completo.** Mixagem real, ducking, batidas, fade, normalização.

**PR-W1 — Whisper: infra.** NDK + JNI do whisper.cpp, download e verificação do modelo, serviço de primeiro plano, progresso, cancelar. Sem UI de legenda ainda: entrega JSON de transcrição.

**PR-W2 — Pipeline de áudio e VAD.** Decode, downmix, resample 16 kHz, VAD, limiares anti-alucinação. **Teste do áudio silencioso obrigatório.**

**PR-W3 — CaptionLayer.** Modelo de cues, segmentação, render do cue ativo, estilos e presets.

**PR-W4 — Edição de legendas.** Lista de cues, unir/dividir, arrastar tempo, lock, buscar e substituir, realce de baixa confiança.

**PR-W5 — Karaokê.** Palavras do Whisper alimentando o seletor INDEX/WORDS do motor de texto. Nenhum código novo de animação.

**PR-W6 — SRT/VTT** import/export e faixa de legenda no MP4.

**PR-W7 — Decupagem por transcrição.** Apagar frase no texto apaga o trecho na timeline.

---

## 11. PROMPT PARA O AGENTE DE CÓDIGO

> Implemente o Modo Edição (NLE) e as legendas automáticas do AM2 Studio conforme `AM2-modo-edicao-e-legendas.md`. Leia antes `AM2_STUDIO.md` e `AM2-auditoria-e-correcoes.md`. As fases 0 a 4 da auditoria são pré-requisito; se não estiverem prontas, diga e pare.
>
> **Antes de codar, responda com evidência do fonte:**
> 1. `VideoSource.kt` cria uma instância de `MediaCodec` por camada? Há algum limite ou pool? Cole o trecho.
> 2. Como é feito o seek no preview e no export? Cole os dois trechos e diga se o export faz seek por frame.
> 3. Existe algum cache de frames decodificados ou de miniaturas? Onde?
> 4. Onde o áudio é decodificado hoje, e em que taxa e número de canais?
>
> **Execute em ordem, um PR por item, sem misturar:**
>
> **PR-V1** — Geração de proxy na importação: eixo longo 960 px, H.264, **GOP de 6 frames**, ~4 Mbps, em `filesDir/proxies/<hash>.mp4`, com PCM 16 kHz mono como subproduto em `filesDir/audio/<hash>.pcm`. `WorkManager` ou serviço de primeiro plano, com progresso e cancelar. Edição nunca espera o proxy. Export sempre usa o original. Filmstrip de miniaturas desenhado na barra do clipe.
>
> **PR-V2** — Pool de decoders limitado por `maxSupportedInstances` do aparelho, com piso 4, LRU pela janela `[playhead-1s, playhead+3s]`, compartilhamento entre clipes do mesmo arquivo. Anel de texturas com orçamento em MB, prefetch em thread própria durante playback, e scrub usando `SEEK_TO_CLOSEST_SYNC` enquanto o dedo está na tela, com o frame exato ao soltar. Instrumente e faça o CI falhar se o scrub passar de 80 ms no p95.
>
> **PR-V3** — Modelo `Sequence` com trilhas e clipes no **mesmo** `project.json`, sem criar segundo formato. Timeline horizontal magnética, ripple delete, lift, fechar buracos, inserir, sobrescrever, trim em rolagem, snap, seleção múltipla. Duplo toque no clipe abre aquele clipe em Modo Composição e volta.
>
> **PR-V4** — Caminho rápido de render para clipe sem efeito, máscara, blend ou opacidade animada: blit direto sem passar pelo grafo. Export decodificando em sequência por clipe, com um seek só no início. Remux sem reencodar quando o clipe não tem efeito, não é retimado e casa com o formato de saída.
>
> **PR-W1 e PR-W2** — `whisper.cpp` via NDK/JNI, modelo baixado sob demanda com verificação de hash, serviço de primeiro plano. Pipeline de áudio: decode, downmix, **resample para 16 kHz mono float**, VAD antes do Whisper, `condition_on_previous_text = false`, `no_speech_threshold` e `logprob_threshold` ativos, descarte de repetição em laço. **Escreva primeiro o teste que transcreve 60 s de silêncio puro e exige zero cues** — ele deve falhar sem VAD e passar depois.
>
> **PR-W3 a PR-W5** — `CaptionLayer` com muitos cues numa camada só, nunca uma camada por fala. Segmentação por pontuação, pausa e limite de caracteres, com duração mínima de 0,8 s. Edição de cues com flag `locked` respeitada em nova transcrição. Karaokê alimentando o seletor `units=INDEX, basedOn=WORDS` do motor de texto, sem escrever animação nova.
>
> **Regras:**
> - Um documento, dois modos. Nenhum formato de arquivo novo.
> - Preview e export pelo mesmo grafo; só muda a resolução e a fonte (proxy vs original).
> - Nenhum trabalho pesado na thread de UI. Transcrição e proxy nunca bloqueiam a edição.
> - Todo áudio permanece no aparelho.
> - Cada PR entra com as metas de desempenho do §8 medidas.
> - Atualize `AM2_STUDIO.md` no mesmo PR que muda comportamento.
