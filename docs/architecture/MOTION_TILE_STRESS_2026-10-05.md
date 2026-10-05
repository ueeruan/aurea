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
A instrumentação adicional encontrou os erros descritos abaixo. A bateria de
96 camadas e a bateria sustentada **continuam sem aprovação** até a repetição
completa depois das correções.

Uploads em Vulkan e Metal agora reutilizam o comando do frame quando aberto;
uploads grandes temporários são mantidos até o fence. No Vulkan, um timeout
de upload imediato conserva os recursos ainda em uso e limita novas alocações
até a conclusão real. Os testes verificam essa retenção e recuperação; isso
não comprova, por si só, a causa do bloqueio de apresentação observado.

Compilaram novamente os candidatos APK release de 32 e 64 bits, incluindo as
correções encontradas pelo validador. Ainda precisam da qualificação final.
O snapshot das fontes para IPA foi criado sem alterar HEAD, branch nem índice
principal. O envio à branch nova no GitHub público foi inicialmente bloqueado
pela revisão automática; depois, o usuário autorizou explicitamente a publicação.
O commit `901b9dab91eddbbfad22420881792712e63d621b` foi publicado em
`codex/motion-tile-stress-2139-20261005` e a
[compilação iOS 37262805922](https://github.com/ueeruan/aurea/actions/runs/37262805922)
iniciou com esse SHA. Os resultados da compilação, simulador e aparelho Apple
serão registrados separadamente; iniciar a CI não equivale a passar nos testes.

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
  exigem cubemaps. `release_environment` destruía o cubemap preto de reserva
  ao trocar de projeto, sem recriá-lo. O motor compartilhado agora preserva
  esse recurso global até encerrar o renderer. Isso corrige também o Metal.

Esses erros não eram detectados pelos testes anteriores sem a camada instalada.
Após a correção, sete testes nativos no moto g52 passaram com Khronos e SyncVal
carregados, sem erros VUID/SYNC: MotionTileMargins (seis cenas em três testes)
e SceneCut (quatro testes). Duração: 50,896 s. As seis imagens uniformes do Tile
mantiveram todos os pixels em 200, sem bordas escuras ou transparentes.

O contrato Vulkan de fallback uniform/storage e três trocas reais de projeto
com cubo PBR também passou no Windows: dois testes, 27 verificações, zero erros
de validação. As três capturas tinham cobertura visível de 17,4%. Há avisos
não fatais de saída de fragmento sem attachment em variantes de pipeline;
zero erros não significa zero avisos. SceneCuts: nove testes/352 verificações.

A aprovação da estabilidade ainda exige terminar as duas baterias pesadas.

## Ciclo de projeto e pressão sustentada

A troca de projeto fechava fontes de vídeo antes de concluir as leituras da GPU
e sem manter a exclusão da thread de renderização durante toda a troca. O motor
compartilhado agora drena a GPU e solta o snapshot antes de fechar os decoders,
sob o mesmo lock, ao criar, carregar, suspender e encerrar. O carregador de
imagens é chamado depois de liberar o lock, preservando importações reentrantes.
No Windows passaram ProjectLifecycle (4 testes/57 verificações) e
ImportStability (3/68). As regressões GPU de captura, upload e descritores
também passaram com validação. Essa correção de disputa não resolveu sozinha o
bloqueio da carga de 96 camadas.

Na repetição de 48 camadas com quatro vídeos Full HD, as capturas após seek e
três reaberturas passaram com diferença máxima zero em 230.400 canais. Duas
recriações de superfície e o watchdog da interface (0–1 ms) também passaram.
Porém, a rodada foi encerrada pelo Android por LOW_MEMORY antes de completar
os quatro minutos: PID 13778, 01:22:06. Após voltar à prévia Full HD e reproduzir,
o consumo de GPU cresceu de aproximadamente 389 MB para 937 MB, com 54 texturas
lógicas vivas e 188 alocações; PSS chegou a 1.116 MB. A memória nativa de CPU
permaneceu em aproximadamente 56 MB. A bateria continua reprovada nessa versão.

O pool descartava dimensões antigas, mas a liberação física era adiada até o
frame novo que estava sendo gravado. Vulkan e Metal passaram a aposentar essas
texturas pelo último frame que realmente as usou, mantendo-as vivas até seu
fence e recolhendo liberações já concluídas antes das novas alocações. Não há
redução de resolução, de camadas ou de amostras nessa mudança. A redução real
de pico ainda depende de repetir a bateria no aparelho.

## Isolamento do bloqueio pesado

As variantes abaixo são diagnósticas, não substituem a bateria original:

- Sem vídeo: preservou objetos 3D, 176 efeitos, 768 keyframes e Motion Blur
  de 360 graus. Avançou reprodução/seeks, mas bloqueou ao aguardar a GPU no
  trim. PSS aproximadamente 433 MB; GPU sem progresso nas amostras coletadas.
- Sem objetos 3D: preservou oito vídeos, Vector Blur, os mesmos efeitos,
  animações e Motion Blur. Passou reprodução/seeks, trim (26.006.720 bytes),
  reabertura e captura com conteúdo visível em 28,151 s. PSS entre
  aproximadamente 435 e 451 MB nas etapas finais. Não incluiu exportação.

Esses resultados direcionam a investigação para o caminho 3D ou sua interação
com a carga. Não demonstram ainda qual operação de GPU causa o bloqueio.
Evidências: `android-isolation-no-video-*`, `android-isolation-no-3d-*`,
`soak-validation-fixes-*`, `host-*-platform-final.log`.

Diagnósticos posteriores, no mesmo APK com aposentadoria por fence:

| Variante da carga original | Resultado diagnóstico |
| --- | --- |
| Sem Vector Blur dos vídeos | Bloqueio ao iniciar reprodução |
| Motion Blur desligado somente nos oito objetos 3D | Passou em 27,475 s, com vídeos e demais efeitos ativos |
| FBX substituído por primitiva, mantendo oito objetos com blur | Bloqueio ao iniciar reprodução |
| Qualidade 3D Low, uma amostra de rasterização + FXAA | Bloqueio ao iniciar reprodução |
| Oito objetos com sombras desativadas e blur ativo | Bloqueio ao iniciar reprodução |
| Uma primitiva com blur, sete objetos substituídos por formas 2D | Passou reprodução, seeks, trim, reabertura e captura |
| Oito objetos com blur colocados contíguos no mesmo grupo 3D | Bloqueio ao iniciar reprodução, contiguidade verificada antes/depois da pré-comp |

Esses testes não incluem as exportações de aceitação. A variante Low também
altera IBL/bloom e não deve ser interpretada como alteração isolada de MSAA.
Os resultados apontam para a carga de vários objetos 3D com Motion Blur; a
última variante mostra que vários grupos separados não são necessários para
o bloqueio. A auditoria de ponteiros e a validação GPU no Windows não
reproduziram o bloqueio do Adreno do aparelho.

## Captura completa e aparência após reabrir

O soak com aposentadoria por fence encontrou diferença estável entre as
capturas antes e depois de reabrir: máximo 34/255, com 48.473 de 230.400 canais
diferindo mais de quatro níveis. Duas capturas consecutivas antes são iguais,
assim como duas depois. Os RGBA e PNG estão em `soak-rgba-fixture/`. A rodada
foi interrompida por essa falha; não comprova quatro minutos sem crescimento
de memória. Uma recriação de superfície também apresentou vídeos ausentes
na prévia, embora a captura posterior tivesse conteúdo.

O diagnóstico Android isolado com um vídeo Full HD sem efeitos comparou
decoder GL de hardware e planos de software: repetições idênticas, diferença
máxima de um nível e nenhum canal acima da tolerância de quatro. O arquivo
declara BT.709, faixa limitada e transferência SDR. A repetição com a cadeia
do soak (escala 0,105, rotação 90 graus, Motion Tile 150% espelhado com escala
uniforme 70%, Gaussian 3 e Glow 20) também passou: máximo dois níveis,
nenhum canal acima de quatro. Hardware e software foram confirmados nos
metadados do teste. A diferença de 34 níveis exige outro fator da cena.

Duas falhas adicionais foram reproduzidas no motor: uma captura final
disputava com a prévia entre proxy e original (353 aberturas do original e
335 do proxy durante uma captura de quatro segundos); uploads de imagem
sempre falhando ainda permitiam que a captura retornasse sucesso. A correção
mantém uma reserva contada das fontes originais durante a captura e exige
renderização completa antes de retornar sucesso, com prazo absoluto e
cancelamento ao mudar projeto, posição ou GPU. Após corrigir: uma abertura de
cada fonte e captura correta (CaptureProxyGpu, 1 teste/55 verificações),
recuperação transitória em 41 ms, timeout permanente em 4,015 s e cancelamento
sem prender a interface (CaptureResources, 3/22). A reserva com duas capturas
concorrentes e outras razões de pausa passou em 1/33.

O cache de Optical Flow também reutilizava o resultado calculado sobre o
proxy ao abrir o original com os mesmos timestamps e tamanho de saída. A
chave agora inclui a identidade dos dois quadros decodificados. No teste GPU,
a troca passou de um acerto indevido de cache e erro máximo 0,375488 linear
(3.696 canais divergentes) para um recálculo e diferença zero. Repetir os
mesmos quadros continua reutilizando o cache.

Falhas de upload no segundo quadro do Vector Blur/mesclagem, nos canais de
RGB no tempo e no histórico de detecção de movimento também retornavam
sucesso com efeito incompleto. Os fallbacks agora sinalizam o quadro
incompleto, conservam a imagem disponível na prévia e permitem que captura e
exportação aguardem recuperação. Os três casos foram reproduzidos antes da
mudança. Após corrigir, CaptureResources passou 6 testes/47 verificações;
TimeWarpRGB, MotionDetect e VectorBlur mantiveram os pixels esperados.
Ao todo, esta frente passou 27 testes e 497 verificações no motor, sem erros
VUID/SyncVal nas execuções GPU. A validação física Android e a nova CI iOS
continuam separadas.

## Diagnóstico prolongado de vídeo e memória

No APK `capture-lease`, a carga original de 48 camadas passou pelas três
primeiras reaberturas sem diferença, mas falhou na quarta, aos 138 segundos:
máximo 34 e os mesmos 48.473 canais divergentes. Os arquivos RGBA são
idênticos aos estados antes/depois da falha anterior. Sentinelas de cor
confirmaram vídeos presentes, portanto a comparação não aceitou apenas as
formas do projeto. O PSS chegou a aproximadamente 1,26 GB, sem ANR ou morte
por memória nesta rodada, que terminou pela diferença visual.

No mesmo APK, a variante diagnóstica com quatro decodificadores em planos
de software terminou em 317,992 s, incluindo 256,289 s de exercício útil:
11 ciclos, 11 edições, 41 capturas, sete reaberturas, sete recriações reais de
superfície e 81 amostras de reprodução. Todas as reaberturas e repetições
tiveram diferença zero, com sentinelas de vídeo presentes e sem ANR/crash.
PSS mediano inicial/final: 411/400 MB; memória nativa: 56,4/56,1 MB; reserva
GPU: 364/376 MB. É um diagnóstico do caminho de vídeo, não substitui a
aceitação do caminho padrão nem justifica desativar aceleração globalmente.

Foi implementada liberação dos imports nativos de vídeo ao trocar projeto,
suspender e receber pressão crítica de memória. Esses buffers estavam fora
do orçamento comum de texturas e podiam manter pools de decodificadores já
fechados. Vulkan e Metal removem a entrada de cache entre frames e retêm o
buffer até o fence do último uso; quadros ainda pertencentes ao decoder
podem ser importados novamente. A compilação e validação dessa mudança são
registradas separadamente dos números anteriores.

A compilação Windows passou. ProjectLifecycle passou 5 testes/71
verificações, incluindo liberação de imports em reload, suspensão, pressão
crítica e novo projeto após drenar a GPU. UploadLifetimeGpu,
MemoryPressureGpu, VulkanDescriptorsGpu, CaptureResources e CaptureProxyGpu
também passaram: total desta rodada 20 testes/415 verificações, sem erros
VUID/SyncVal registrados. O teste AHardwareBuffer real foi compilado para
Android separadamente; sua execução física permanece pendente nesta etapa.

O teste GPU de 96 camadas no Windows percorreu 18 quadros reais: três
disposições de objetos, duas resoluções e três posições da timeline, com
176 efeitos e Motion Blur de 360 graus. Passou 459 verificações sem erros
VUID/SyncVal. No quadro 45 em 960×540, oito grupos separados produziram
1.344 passes/1.440 draws/128 amostras 3D; um grupo com oito objetos produziu
804 passes/893 draws/16 amostras. Os três arranjos chegaram ao limite de
512 medições GPU. Isso não reproduz nem comprova resolução do bloqueio no
Adreno do aparelho Android.

## Compilação iOS intermediária

O job de IPA do run 37262805922 passou: compilação nativa Release, verificações
Foundation, compilador Metal e validação do pacote 2139. O IPA sem assinatura
foi baixado para `ipa-intermediate-901b9d/` dentro da pasta de evidências,
SHA-256 `48c38369b65693d44336b4805681ff749e625e7b7b509176a961652585971c0e`.
É um artefato intermediário: não inclui as correções posteriores de ciclo de
projeto e aposentadoria de texturas. A entrega final exige nova compilação do
mesmo código dos APKs. Execução em aparelho Apple não foi realizada.

A suíte de gestos dessa CI terminou com 49/52 testes aprovados. Foram
corrigidos localmente o seletor que contava botão e rótulo do Motion Blur
como dois controles, a rolagem do eixo Y no teste de texto e uma regressão
real do menu compacto: o acesso a copiar/colar voltou no Android e no iOS.
Os testes continuam exigindo alteração de valores e desfazer. A verificação
das capturas também foi atualizada para a versão 6 dos dados de ambiente
HDR; passou ao reaplicar os PNGs reais da CI, incluindo três mutações
rejeitadas. Essas correções exigem nova execução nativa na CI final.

## Conteúdo acumulado dos pacotes

Os APKs e o IPA partem das mesmas fontes compartilhadas e incluem as mudanças
documentadas em [DEVICE_FIXES_2026-10-05.md](DEVICE_FIXES_2026-10-05.md) e
[COMMUNITY_FIXES_2026-10-05.md](COMMUNITY_FIXES_2026-10-05.md), além deste relatório:

- Buffer de reprodução, retenção e aquecimento de quadros;
- Depth Map e Roto Brush locais, com fila assíncrona e estados de interface;
- Câmera e ambiente HDR respeitando os cortes, iluminação de pré-comps;
- Motion Tile com escala, espelho, margens e pilhas de efeitos;
- Interface de edição compacta em Kotlin e Swift, com acessibilidade;
- Contorno de texto, bordas de máscara, opção de mostrar somente a sombra;
- Instâncias de pré-comps e captura coerente com projeto, tempo e geração;
- Uso de memória, validade de recursos 3D e sincronização de GPU;
- No iOS, identificadores próprios nos controles expandidos do Motion Blur.

O teste iOS de mover um Null foi atualizado para a regra já existente do motor:
Auto-Key não cria chaves em eixos sem alteração. Ele exige mudança real em X,
Y/Z e suas curvas preservados, pose inicial intacta e um único desfazer. Essa é
uma correção da expectativa do teste, separada da mudança de acessibilidade.
