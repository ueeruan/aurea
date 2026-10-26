# Export Engine 2.0 (0.3.2): auditoria e plano

Fase 1 da missão "Export Engine 2.0". Auditoria só de leitura do código em
`claude/release-2194` depois da 0.3.1 (commit `a7d9ef07`), em cinco frentes:
núcleo C++ do export, Android, iOS, motion blur 3D e FFmpeg 9. Cada item diz
**[código]** quando foi lido no código e **[inferido]** quando é dedução ainda
não provada. Nada aqui foi validado em aparelho físico.

## 1. Causas encontradas

### 1.1 O export morre ao sair do app (Android e iOS) [código]
- Android: `MainActivity.onStop` → `EditorStore.onEnterBackground` →
  `engine.suspend()` → `cancel_export()` e `join` da thread
  (`MainActivity.kt:220`, `EditorStore.kt:1186-1198`, `Engine.cpp:1044-1062`).
  Não há serviço em primeiro plano. O resultado é `Cancelled`, que a escada
  trata como "nada ajuda" (`ExportAttempts.hpp:116`).
- iOS: `enterBackground` faz o mesmo (`AureaModel.swift:1531-1542`) e nada
  mantém a tela acesa (`isIdleTimerDisabled` não aparece): o bloqueio
  automático encerra exports longos.
- O comentário que promete retomar do trecho salvo (`EditorStore.kt:1160`) e a
  chave `enableResumableExport` do iOS (`AureaBridge.mm:173-175`) não fazem
  nada: o motor não lê a chave, `packetLogPath` nunca é preenchido e
  `enableExportRecovery` é falso nas duas plataformas (`Engine.hpp:233`).

### 1.2 O vídeo exportado depende da carga do aparelho [código]
- O teto adaptativo de motion blur (0.3.1) mede `renderCpuNs + gpuWaitNs` por
  quadro (`Engine.cpp:14713`). Acima de 1,5 s corta o teto 64 → 32 → 16 → 8;
  depois de 8 quadros leves sobe de novo (`Renderer.hpp:789-807`). A espera do
  decoder e do mutex entram na conta: o mesmo projeto sai com desfoque
  diferente conforme o aparelho estiver ocupado, e o número de amostras pode
  mudar entre quadros vizinhos.
- O "degrau de GPU" só sobe (`Renderer.hpp:783`): uma vez acionado, sombra,
  MSAA e mapa de sombra ficam reduzidos até o fim do export.
- O HDRI entra ou não conforme a memória livre na hora (`Engine.cpp:13758,
  14438-14446`).
- Prova: `ExportV2Gpu.FullHdAnimatedTextMotionBlurWithinTrackedBudget` deu
  pixels diferentes rodando junto com um build de APK e passa sozinho.

### 1.3 Reduções silenciosas [código]
Sem `ExportFlag` nem aviso, só log: teto de resolução do aparelho
(`Engine.cpp:13865-13888`), teto de 8/16/32 amostras de cena 3D por aparelho
(`14675`), HDRI trocado por ambiente neutro (`14438-14446`), modelo "Original"
trocado pelo de prévia (`786-812`), grupo 3D sem orçamento renderizado sem
desfoque (`Renderer.cpp:4907-4920`), conjuntos de glifos cortados
(`Renderer.cpp:3232`) e o teto adaptativo. Só os degraus da escada aparecem na
tela. Os testes `MotionBlur.*`/`BoundedSceneExposure` que falham no HEAD
esperam o contrato antigo (recusar); o código passou a cortar e só registrar.

### 1.4 Lentidão tratada como falha [código]
- Um quadro tem 120 s e falha com 30 s "sem progresso"
  (`OfflineFrameRenderer.cpp:161-164`); progresso é só decoder, compilação de
  shader, upload e ambiente. IA/Roto lentos contam como travado.
- O `Timeout` vira `GpuTime` (`ExportAttempts.hpp:100-101`): a tela recomeça
  do quadro 0 com menos desfoque e depois menos pixels.
- O encoder de hardware tem 5 s para o primeiro pacote (`FFmpegExportSink.cpp:80`):
  um encoder só lento para abrir cai para o próximo.
- [inferido] `av_write_trailer` com `+faststart` reescreve o arquivo inteiro sem
  heartbeat (`:682, 2016`); num arquivo grande em armazenamento lento pode
  passar dos 45 s do watchdog e virar `EncoderStalled`.

### 1.5 Uma falha no meio recomeça do quadro 0 [código]
Depois do primeiro pacote não há troca de encoder (`FFmpegExportSink.cpp:1114-1169`);
pacote fora da ordem exata de PTS falha a tentativa (`:1484-1485, 1668-1670`).
A escada recomeça do início. O log de pacotes com retomada (`ExportPacketLog.cpp`,
`resumeFrame`, `begin_assembly`) existe e não é usado.

### 1.6 Esperas longas sob trava [código]
`deliver_oldest` segura `renderMutex_` durante a espera da fence (até 120 s,
`Engine.cpp:14687-14692`); `render_export_frame` segura o mesmo mutex no laço
de novas tentativas com sleeps (`14557`, `OfflineFrameRenderer.cpp:75-76`);
`gpu_->wait_idle()` sem limite no trim (`14755, 14758`). A fase "Preparando"
soma esperas de 30 s, 45 s, 45 s, 120 s e 120 s na thread que chamou
(`13636-14231`).

### 1.7 Encoders [código]
- Android: caminho legado do FFmpeg `h264_mediacodec` supõe stride = largura
  alinhada a 16 e não lê `stride`/`slice-height` do encoder (patch, linhas
  15-45); o degrau ByteBuffer enfileira a capacidade inteira do buffer, não o
  tamanho preenchido (`AndroidVideoEncoder.cpp:923`). Não há lista de
  fabricantes no encode. Só o primeiro quadro é conferido, e só nos degraus
  nativos/legado do Android.
- iOS: VideoToolbox via FFmpeg; até 5 cópias por quadro (RGBA16F → R8/RG8 →
  buffer compartilhado → AVFrame → CVPixelBuffer do VT); sem IOSurface nem pool
  próprio no encode; sem conferência do primeiro quadro; a lista de degraus
  que falharam não vale no iOS, então cada export paga a espera do VT de novo.
- Cancelar logo depois do commit relata OK com fase Cancelled
  (`OfflineFrameQueue.cpp:99`).

### 1.8 iOS sem sinais que o Android tem [código]
Temperatura (`setThermalLevel` existe e ninguém chama), marca de "morreu no
export" que acusa também o fechamento normal pelo sistema
(`AureaModel.swift:4281-4293`), salvamento ao sair na thread principal
(`:1519-1526`).

### 1.9 Motion blur 3D [código]
Força bruta: cada amostra renderiza a cena 3D inteira (sombra, PBR com MSAA,
reflexo, DOF), até 128 amostras (`Renderer.cpp:4782-5020, 5816-5925`). Não há
velocity buffer nem transformações anteriores. Todas as funções de tempo
(`world_3d_frac`, `comp_view_projection_frac`, `place_model`) aceitam tempo
fracionário, então o "antes" e o "depois" saem sem histórico. O caminho em
lotes com fence (`render_bounded_scene_exposures`) está pronto e desligado
(`Engine.hpp:206`).

**Caminho rápido (0.3.2, `MotionBlurSettings::vectorBlur`).** Com a opção
"Motion blur 3D rápido (vetores)" ligada, o grupo 3D é desenhado UMA vez no
centro do obturador; um passe só de geometria (`shaders/scene3d/motion/
vectors.*`, push de 2 mat4 = 128 B com as posições de clip do começo e do fim,
bloco de uniform com a matriz do centro, profundidade própria de 1 amostra)
grava a velocidade em px; bloco com a maior velocidade (2 passadas
separáveis), vizinhança 3×3 e a reconstrução (`reconstruct.frag`, pesos de
profundidade: o que anda borra sobre o fundo, o fundo não vaza sobre o objeto
parado na frente) rodam antes do bloom/tone map. Mesmas amostras (≤ 32) no
preview e no export. O grupo volta à acumulação (qualidade maior, sem aviso de
redução) com deformação (animação, skin/morph animados, material animado,
texto/forma animados, efeito de partes), partículas, planos 2D na cena,
transparente, recorte entre grupos, chão com câmera andando, trajeto curvo
(trajeto > 1,15 × a reta + 1 px) ou rastro maior que 2× o bloco
(`velocity_max_length`). Projetos antigos: o campo sempre foi gravado e sempre
`false` (não havia controle) — continuam na acumulação, sem mudar o visual.

### 1.10 FFmpeg 9.0.2 [verificado]
- `ffmpeg-9.0.2.tar.xz`: SHA-256
  `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`, assinatura
  PGP válida da chave de release (`FCF9 86EA 15E6 E293 A564 4F10 B432 2F04 D676 58D8`).
- `mediacodecenc.c` do 9.0.2 é idêntico ao 8.0.3 sem patch: nenhuma das nossas
  correções entrou no upstream; o patch aplica igual.
- Nenhuma API removida no 9.0 é usada pelo nosso C++.
- O 9.0 liga `unstable` por padrão e, em arm64, compila e roda um gerador no
  host (`libswscale/aarch64/ops_asmgen`) para um backend que não usamos.
  **Não dá para desligar**: com `--disable-unstable` o 9.0.2 não linka
  (`format.c` define `ff_sws_chroma_pos` só dentro de `#if CONFIG_UNSTABLE` e
  `graph.c` chama sempre — "undefined symbol: ff_sws_chroma_pos" no
  `libaurea.so`). Fica a configuração padrão.
- Licença continua LGPL-2.1+ com as nossas flags.

### 1.11 Qualcomm OMX descarta quadros em VBR [medido no Moto g52]
- Aparelho: Moto g52 (Snapdragon 680, Adreno 610, Android 13),
  `OMX.qcom.video.encoder.avc`. Teste `ExportV2DeviceTest.sixtySecondsThreeVideoClipsWithTimeRemap`
  (3 clipes com remapeamento de tempo, 1800 quadros, 480×320, 8 Mbps).
- Sintoma: o encoder confirma o 1º pacote e, 1–3 s depois, para de devolver
  pacotes. O log do driver mostra `avg. fps 50–77, bitrate 0`: ele continua
  recebendo quadros e não gera bytes. A Surface trava no quadro ~123 (limite de
  quadros em voo) e o buffer em ~316 (fila de 256 cheia), 30 s depois `Timeout`.
- Experimento no aparelho com o mesmo teste (propriedade de debug, já removida):
  VBR (padrão) trava; sem `bitrate-mode` trava; VBR + `priority=1` +
  `operating-rate` trava; CQ o configure recusa (-10000); **CBR passa** (1800
  quadros, Surface do hardware, 1ª tentativa). Nenhum buffer vazio chegou ao app:
  o MediaCodec não repassa saídas de 0 bytes, então o quadro descartado só some.
- Inferido (não comprovado no código do fabricante): o HAL OMX da Qualcomm
  mapeia `BITRATE_MODE_VBR` para VBR com quadros variáveis (VBR_VFR), que
  descarta quadros quando a entrada chega mais rápido que o tempo real.
- O g52 aceita CBR **sem anunciá-lo** (`isBitrateModeSupported(CBR)` falso).
- Correção: `android_codec::vbr_drops_frames` — encoders `OMX.qcom.*` recebem
  CBR na mesma taxa alvo (nativo e FFmpeg+MediaCodec); se o configure recusar,
  o encoder abre de novo no modo pedido. Codec2 (`c2.qti.*`) e outros
  fabricantes não mudam (não testados).
- Duas correções de apoio vistas no mesmo teste: (1) a Surface parada no meio
  refaz igual pelo buffer do mesmo encoder (Busy) em vez de descer para
  software e menos pixels; (2) o checkpoint gravado por outro caminho do encoder
  não marca mais todos os degraus como falhos (terminava em "encoder
  indisponível", erro 31): refaz igual e recomeça do zero.

## 2. Arquitetura resultante (o que evolui, sem duplicar)

| Componente da missão | Já existe | Evolução |
| --- | --- | --- |
| ExportSupervisor | `OfflineFrameQueue` (fases), `ExportContext`, `ExportDiagnostics`, `ExportWatchdog.hpp` | Estados com causa (lento × travado × falhou), heartbeat em toda etapa longa, fim correto de cancelar × commit, sobreviver ao segundo plano |
| ResourceBudgeter | espalhado: `admission_limit`, profundidade por orçamento, `address_space::*` | Um plano de recursos feito **antes** do primeiro quadro, determinístico (projeto + aparelho); a carga muda só a concorrência, nunca o conteúdo |
| FrameScheduler | `AureaRenderScheduler::frame`, fila de slots | Texturas de composição/YUV por slot (pipeline real), espera da fence fora do `renderMutex_` |
| EncoderRecovery | escada `ExportAttempts`, probation, degraus lembrados | Troca de encoder na fronteira de segmento (sem recomeçar do 0), validação do layout do encoder, primeiro quadro conferido também no VT |
| CheckpointManager | `ExportPacketLog`, `export_recovery`, commit atômico | Segmentos por GOP com hash do projeto/configuração, retomada depois de fechar o app, montagem final segura |
| MotionBlurRenderer | acumulação por amostras, lotes com fence | Caminho rápido por velocity buffer + caminho de qualidade por amostragem; a mesma regra no preview e no export; redução sempre avisada |

## 3. Ordem de implementação (cada etapa com testes de regressão)

1. **FFmpeg 9.0.2** — versão e SHA, patch renomeado, pasta de objetos por
   versão, `--disable-unstable`; recompilar as 3 ABIs Android e o iOS no CI.
2. **Export determinístico e honesto** — tirar a carga das decisões de
   conteúdo (teto adaptativo e degrau de GPU passam a ser plano feito antes do
   export); toda redução vira `ExportFlag` e aviso nas duas interfaces; testes
   de motion blur no contrato "reduz e avisa".
3. **Lento × falha** — heartbeat nas etapas longas (IA, compilação, trailer),
   `Timeout` de etapa lenta não desce a escada.
4. **Segundo plano** — Android com serviço em primeiro plano durante o export;
   iOS com tela acesa e checkpoint ao sair; marca de morte e temperatura no iOS.
5. **Checkpoints** — segmentos validados, retomada a partir do último
   segmento consistente, troca de encoder na fronteira.
6. **Encoders** — tamanho preenchido no ByteBuffer, stride do encoder no
   legado, conferência do primeiro quadro no VT, degraus lembrados no iOS.
7. **Cópias e memória** — texturas por slot, pool de CVPixelBuffer/IOSurface
   no iOS, fence fora do mutex.
8. **Motion blur 3D** — velocity buffer (caminho rápido) e amostragem
   progressiva (qualidade), lotes com fence ligados.

## 4. Limites de validação
- Host: Windows + Vulkan (RTX 3050). Android: emulador x86_64. iOS: só o CI
  (compilação e checagens) e o simulador do CI.
- **Não testados**: ARM 32 bits físico, vivo, Oppo, MediaTek, PowerVR, iPhone
  real. Compilação não prova funcionamento em aparelho.

## 5. O que foi feito na 0.3.2 (estado em 26/10/2026)

| Etapa | Situação | Validação |
| --- | --- | --- |
| 1. FFmpeg 9.0.2 | feito (Android 3 ABIs; iOS pelo CI) | host + emulador x86_64; ARM físico não |
| 2. Export determinístico e honesto | feito | host (MotionBlur, ExportAttempts) |
| 3. Lento × falha | feito: espera ociosa no quadro, heartbeat no trailer | host |
| 4. Segundo plano | Android: serviço em primeiro plano; iOS: tela acesa, temperatura, `suspend` espera a fence | Kotlin compila; iOS só estático |
| 5. Checkpoints | feito (ExportCheckpoint, retomada por GOP, impressão limitada a 3 MiB por arquivo) | host + `ExportCheckpointDeviceTest` no emulador |
| 6. Encoders | ByteBuffer com tamanho preenchido; cancelar na validação; espera em fence inexistente | host |
| 7. Cópias e memória | **não feito**: IOSurface/CVPixelBufferPool no iOS, texturas por slot | — |
| 8. Motion blur 3D por vetores | feito (opção por composição; projetos antigos seguem em amostras) | host (7 testes GPU) |

### Números medidos (host, RTX 3050)
- Motion blur 3D 1080p, 6 cubos + chão, 32 amostras: acumulação 114 ms de GPU,
  vetores 4,8 ms (≈24×). Erro médio por pixel contra a acumulação de 64
  amostras ≈ 0,01 (sem desfoque: 0,05).
- Impressão das fontes para o checkpoint: arquivo de 512 MB em 4,1–4,5 ms
  (lê no máximo 3 MiB por arquivo; antes a recuperação lia o arquivo inteiro).
- Suíte completa do host: 121 → 79 falhas em relação à rodada da 0.3.1, nenhuma
  falha nova (as restantes já falhavam no código anterior; lista em
  `full033.log` da sessão).

### Ainda pendente / limites conhecidos
- Etapa 7 inteira (cópias por quadro no iOS, pipeline real com texturas por slot).
- Caminho legado `h264_mediacodec` ainda supõe stride alinhado a 16 (patch);
  primeiro quadro não é conferido no VideoToolbox; degraus que falharam não são
  lembrados no iOS.
- Troca de encoder no meio do vídeo recomeça do quadro 0 (fluxos de encoders
  diferentes não emendam numa trilha MP4).
- Motion blur por vetores: sombra/reflexo do objeto em movimento ficam nítidos;
  rastros acima de ~160 px em 1080p e trajetos curvos voltam para amostras.
- Testes `ExportV2DeviceTest.{galleryContentUri…, prepareInterruptedExport,
  resumeInterruptedExport}` testam o pacote de recuperação antigo
  (`enableExportRecovery`, desligado) e falham por isso; a retomada nova tem o
  próprio teste.
- Aparelho físico (26/10/2026): Moto g52 (Snapdragon 680, Adreno 610,
  Android 13, processo de 64 bits). `ExportV2DeviceTest` 7/10 (as 3 falhas são
  as do pacote de recuperação antigo, acima), `ExportCheckpointDeviceTest`,
  `HomeNavigationTest`, `HyperScreenDeviceTest` 5/5 e `HyperJobDeviceTest` 3/3
  passaram. Causa nova encontrada lá: 1.11. Não testados: 32 bits, vivo, Oppo,
  MediaTek, PowerVR, iPhone (o iOS compila no CI).
