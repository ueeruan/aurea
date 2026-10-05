# Motion Tile e estresse pesado — 05/10/2026

## Escopo e reprodução

O relato esclarecido pelo usuário é: ao adicionar outro efeito depois de Motion
Tile, a repetição desaparece ou deixa de espelhar. A imagem anexada é somente um
quadro; os testes usam imagens procedurais próprias e verificações de pixels.

Referência de comportamento: [Adobe, Motion Tile](https://helpx.adobe.com/after-effects/desktop/apply-effects-and-animation-presets/list-of-effects/stylize-effects.html#motion-tile-effect).
Nenhum código, shader ou mídia de outro aplicativo foi incorporado.

## Falhas reproduzidas antes das correções

- Motion Tile padrão seguido de Gaussian Blur 12: 831 pixels escuros indevidos.
- Transform 50% antes de Motion Tile padrão: 2.720 pixels escuros indevidos.
- Motion Tile espelhado, Transform 10%/25% e Blur 24: 1.206/738 pixels escuros
  sem rotação; 110/40 com rotação de 33 graus.
- Android, moto g52, Android 13, aproximadamente 4 GB de RAM: a bateria original
  de 96 camadas, 176 efeitos e 768 keyframes foi encerrada pelo sistema antes de
  terminar playback. `ApplicationExitInfo` registrou `LOW_MEMORY` às 00:28:51,
  PID 7238. Não houve exceção nativa registrada nessa rodada.

O teste inicial de contraste de uma imagem assimétrica teve cinco falsos
positivos por amostrar somente uma região escura. Essa métrica foi corrigida;
as seis falhas de cobertura acima são independentes dela.

## Implementação e validação em andamento

O motor compartilhado passou a compor pilhas Normal em blocos de quatro
desenhos, liberando intermediários de camadas anteriores. Pilhas com ajustes
direcionados ou modos de mistura especiais mantêm o caminho existente. HDRs
ociosos de pré-comps e objetos são liberados sob pressão crítica e reconstruídos
quando voltam a ser necessários.

O Android passou a considerar a memória disponível acima do limite de pressão
do sistema, além da folga do heap Java. Reservas de 128/64 MiB antecipam os níveis
moderado/crítico, com cooldown e escalada imediata. O iOS já usa reservas de
128/64 MiB sobre o limite de alocação do próprio processo. As duas métricas não
são intercambiáveis: a [documentação Android](https://developer.android.com/reference/android/app/ActivityManager.MemoryInfo)
define `availMem` como memória do sistema e `threshold` como o limite de pressão.

Primeira validação da correção de memória:

- Motor Windows/Vulkan: 3 testes `MemoryPressure`, 715 verificações, nenhuma
  falha. Inclui 8/96 camadas, reutilização estável e comparação de pixels com o
  compositor anterior.
- Android: compilação de UiTest/instrumentação e 363 testes JVM aprovados.
- Android físico: a carga original avançou o playhead, seeks, trim e reabertura.
  Essa primeira bateria não verificava a mudança de pixels na superfície;
  portanto, o avanço do playhead não comprova reprodução visual correta.
  O trim recuperou 26.874.464 bytes. A exportação 720p ficou no primeiro quadro
  com `VK_TIMEOUT` repetido no upload do ambiente 3D. Após preservar registros,
  o pacote isolado foi encerrado manualmente; o `Process crashed` final dessa
  rodada resulta desse encerramento. A bateria **não foi aprovada**.
- iOS: auditoria estática de memória aprovada. Swift/Xcode e aparelho Apple
  indisponíveis nesta máquina; compilação remota preparada, ainda pendente.

## Evidências

Resultados brutos em `build/reference/heavy-stress-20261005/`:

- `android-heavy-baseline.log`, `baseline-progress.txt`, `exit-info-baseline.txt`;
- `host-motiontile-baseline.log`, `host-memory.log`;
- `android-memory-build.log`, `android-heavy-memory-fix.log`;
- `memory-fix-progress.txt`, `memory-fix-logcat.txt`, `android-heavy-memory.png`;
- `ios-memory-audit.log`.

A nova bateria sustentada usa quatro vídeos H.264 Full HD independentes,
48 camadas e 92 efeitos, com quatro minutos úteis, recriação da superfície,
salvar/reabrir, edição, pressão de memória e verificações de pixels. Sua
implementação não constitui aprovação: a execução física será registrada aqui
depois de concluída.

Primeira execução da bateria sustentada na versão com correção de memória:
o MP4 Full HD foi gerado, os quatro vídeos importados e as 48 camadas montadas.
A captura do quadro 0 entregou 230.400 bytes em 1.871 ms; após seek para o quadro
45, a captura expirou em 4.233 ms. A interface respondeu ao watchdog em 1 ms
nessa etapa. Os registros mostram a captura do teste e uma miniatura automática
concorrentes, além de reinícios repetidos dos decodificadores. A captura usava
`mediaGeneration=0`, enquanto a prévia usava a geração atual após o seek. A
correção desse desencontro foi implementada no motor compartilhado. Capturas
agora usam a geração de mídia atual e são canceladas quando um novo seek muda
a geração de reprodução. Três testes Vulkan reais validam captura após seek,
cancelamento de captura antiga e continuidade durante reprodução. Essa execução
anterior no Android também **não
foi aprovada** (`android-soak-memory-fix.log`, `soak-memory-fix-progress.txt`,
`soak-memory-fix-logcat.txt`).

A sequência simples de espelhamento no Android passou: adicionar Gaussian,
Glow e Transform, editar um efeito anterior, remover Transform e salvar/reabrir
mantiveram a simetria e reproduziram pixels idênticos após a reabertura.
`android-motion-stack-baseline.log`: 1 teste, 6,015 s. Isso não elimina as falhas
de cobertura demonstradas pelas combinações específicas do motor.

## Validação posterior e bloqueio encontrado

As correções de Motion Tile preservam a extensão da fonte antes da repetição,
propagam as margens dos efeitos seguintes através das transformações e corrigem
a densidade após redução de escala. As seis combinações de cobertura passaram
a registrar zero pixels escuros indevidos. O teste de deslocamento de um período
espelhado após blur antes do Tile também passou, com erro máximo zero.

Motor Windows/Vulkan, rodada de aceitação: MotionTile 49 testes/45.093 checks;
UploadLifetimeGpu 3/40; CaptureEpochGpu 3/102; MemoryPressure 3/715;
SceneCuts 8/184. Nenhuma falha. Esses resultados não equivalem à execução iOS.
No Android físico de 64 bits, oito testes de Motion Tile, margens, câmera,
ambiente e iluminação passaram em 48,671 segundos.

A repetição da carga de 96 camadas com as correções encontrou outro bloqueio:
o processo 32649 ficou aguardando indefinidamente um fence da GPU na apresentação
da superfície. A pilha nativa coletada e simbolizada confirma
`Backend::end_frame → Vulkan → Surface::queueBuffer →
BufferQueueProducer::queueBuffer → Fence::waitForever → poll`.
As threads de decodificação estavam aguardando condição, sem espera em
`glFinish`. O consumo permaneceu aproximadamente estável, com PSS de 452 MB;
essa ocorrência não tem evidência de encerramento por falta de memória.
A origem do fence não sinalizado ainda está em investigação. A bateria de
96 camadas e a bateria sustentada **continuam sem aprovação**.

Uploads em Vulkan e Metal agora reutilizam o comando do frame quando aberto;
uploads grandes temporários são mantidos até o fence. No Vulkan, um timeout
de upload imediato conserva os recursos ainda em uso e limita novas alocações
até a conclusão real. Os testes verificam essa retenção e recuperação; isso
não comprova, por si só, a causa do bloqueio de apresentação observado.

Compilaram os candidatos APK release de 32 e 64 bits. Ainda precisam da
qualificação final. O snapshot das fontes para IPA foi criado sem alterar
HEAD, branch nem índice principal. O envio à branch nova no GitHub público
foi bloqueado pela revisão automática; depois, o usuário autorizou explicitamente
a publicação no repositório público. O snapshot será atualizado com as últimas
correções antes da compilação. Nenhum IPA com essas fontes foi compilado até
este registro.

Evidências adicionais: `android-motion-scene-final.log`,
`android-heavy-final.log`, `heavy-final-lldb-stacks.txt`,
`heavy-final-proc-maps.txt`, `heavy-final-stall-logcat.txt`,
`host-*-acceptance.log`, `android-release-arm64-build.log`,
`android-release-arm32-build.log`, `ipa-snapshot-result.json`.

## Diagnóstico de sincronização Vulkan

Foi carregada temporariamente no pacote isolado a camada oficial Khronos
1.4.363.0, com validação de sincronização ativada. Ela não integra os APKs de
release. O procedimento segue a [documentação Android](https://developer.android.com/ndk/guides/graphics/validation-layer)
e o [guia de SyncVal](https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/vulkan-sdk-1.4.363.0/docs/syncval_usage.md).

Erros concretos encontrados, inclusive em testes pequenos cujos pixels passaram:

- A primeira transição de layout do swapchain partia de `TOP_OF_PIPE`, enquanto
  a aquisição aguardava em `COLOR_ATTACHMENT_OUTPUT`, permitindo disputa com a
  apresentação anterior. A transição foi ligada ao estágio correto conforme
  [VkSubmitInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkSubmitInfo.html).
- O buffer vazio de fallback era usado como uniform dinâmico, mas não declarava
  uso `Uniform`. A declaração foi corrigida, preservando os bindings.
- A cena 3D apontou texturas de ambiente 2D nos bindings 5/6, cujos shaders
  exigem cubemaps. A origem e a correção estão em investigação.

Esses erros não eram detectados pelos testes anteriores sem a camada instalada.
A aprovação exige repetir a validação e a carga no aparelho após as correções;
o simples desaparecimento de um erro estático não comprova estabilidade.
