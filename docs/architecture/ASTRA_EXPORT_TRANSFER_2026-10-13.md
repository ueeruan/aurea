# ASTRA — frames, GPU e formatos de exportação

Data: 13/10/2026. Esta revisão usa o código disponível do AUREA. Os testes
controlados abaixo não comprovam compatibilidade com OPPO, Vivo ou Realme;
não há um desses aparelhos conectado. Os relatos recebidos não identificam
encoder, formato negociado nem estágio GPU suficiente para atribuir uma causa.

## Caminhos existentes

| Etapa | Implementação disponível | Limite da conclusão |
|---|---|---|
| Composição | `OfflineFrameRenderer` possui compositor, fontes e histórico temporal próprios; usa a implementação compartilhada `Renderer` com qualidade final | Não altera o renderer de preview |
| Saída de cor | RGBA16F linear → shader SDR BT.709 limitado → plano R8 Y e plano RG8 CbCr | A exportação de vídeo atual é NV12 de 8 bits; não é HDR/P010 |
| Vulkan | Cópia imagem→buffer, barreira TRANSFER_WRITE→HOST_READ, fence, invalidação de memória não coerente ao mapear | API que não retorna não é preemptível pelo laço de polling do motor |
| OpenGL ES | Readback RGBA/UNSIGNED_BYTE para PBO, fence e map; cópia R/RG para espelho CPU compacto | Backend já existente; nenhum fallback GPU novo foi acrescentado |
| Metal/iOS | Buffer de readback, conclusão do command buffer, ponteiro CPU | Sem compilação ou execução iOS nesta máquina Windows |
| Entrada do sink | Ponteiros CPU NV12, Y stride=`width`, UV stride=`width`, cada linha útil copiada separadamente | Não é um input Surface do encoder |
| Codec Android | FFmpeg `h264_mediacodec`/`hevc_mediacodec`, NDK MediaCodec em buffers CPU; alternativas software compiladas | Capacidade anunciada não comprova que o encoder gere o primeiro pacote |
| Contêiner | FFmpeg libavformat MP4/MOV, publicação temporária e validação independente | O exportador atual não usa Android `MediaMuxer` |

O Surface/ImageReader/AHardwareBuffer observado em `MediaCodecSource` pertence
à **decodificação**. A fonte pode entregar imagem externa ou compactar
`YUV_420_888` para I420 respeitando row stride e pixel stride. Não se deve
confundir esse Surface com entrada do encoder ou modificar o decoder
compartilhado para corrigir uma hipótese exclusiva da exportação.

## Transferência e propriedade dos frames

`Engine::export_thread_main` entrega o slot somente depois de
`wait_export_gpu(slot.gpuFrame)` e de mapear novamente os dois planos. O sink
mantém o slot até terminar a escrita; a fila só então permite reutilizá-lo.
O compositor libera as imagens externas do decoder após a conclusão GPU.

`copy_nv12_plane` copia apenas a largura útil de cada linha. O teste existente
`ExportPixelCopy` cobre planos compactos e strides com padding, incluindo
854×482 e 1920×1080; as guardas verificam que padding e fonte permanecem
intactos. Um teste Vulkan independente mede a coluna de divisão dos quadrantes
em 1080×1920/1152, 1920×1080/2048 e 1080×1080/1152. Não foi identificado um
motivo para alterar a conversão de cor, shader, preview ou regra de stride.

## Esperas e falhas controladas

Os testes novos `ExportTransferDeadline` injetam o retorno documentado
`Timeout` de um backend de teste no primeiro fence. Verificam término sem
codificar um frame incompleto, retenção dos recursos pendentes e nova tentativa
após sinalização. O caso positivo aceita um primeiro fence atrasado antes do
prazo e preserva os timestamps de todos os frames. Isso não simula uma chamada
Vulkan/GLES que nunca retorna nem comprova recuperação de um driver OEM.

Os testes existentes `ExportPendingGpu` verificam cancelamento, bloqueio de nova
admissão com fence pendente e retomada após sua conclusão. Não se pode destruir
ou reutilizar esses buffers somente porque o prazo expirou.

O polling de `Engine::wait_export_gpu` e de `OfflineFrameRenderer::wait_gpu` usa
fatias de 100 ms com cancelamento e prazo finito. Ainda existe um limite de
observabilidade para chamadas de driver que não retornam. Submissões Vulkan
intermediárias offscreen também possuem espera finita de até 120 s; nenhum
ajuste genérico por fabricante foi aplicado sem evidência.

O resultado da matriz principal é **25 testes únicos, 1.729 verificações e
0 falhas**, sem casos pulados. Houve 27 execuções: os dois testes que comparam
serial e paralelo foram repetidos após ajustar somente sua fixture. A política
existente de pressão serializa exportações curtas por 20 s; a rodada paralela
agora espera essa política expirar, com teto de 25 s, e continua exigindo
profundidade maior que 1 e igualdade de pixels/PTS/áudio. As duas falhas
originais desse requisito de fixture estão preservadas; a política de produção
e o preview não foram alterados para fazê-los passar.

| Validação host executada | Resultado observado |
|---|---|
| Primeiro fence sem sinal, prazo configurado de 200 ms | Término com `Timeout`, nenhum frame entregue ao encoder; nova tentativa só após sinalização |
| Primeiro fence atrasado válido | Todos os frames e seus timestamps aceitos antes do prazo |
| NV12 compacto/padded e export 1080 | Guardas intactas, borda vertical sem deslocamento de linha, tags BT.709 limitadas |
| Multicamadas/efeitos/3D/motion blur | Multicamadas/efeitos: todos os bytes/PTS/áudio iguais em serial e paralelo; 3D com blur: hashes de todos os planos NV12 iguais; texto e máscaras reabertos preservados |
| Composição → MP4 → decodificação independente | 30 frames 2D com efeitos e estéreo; 12 frames 3D com motion blur e estéreo; 3.600 frames em 120 s e 5.760.000 amostras lógicas de áudio |
| Projeto histórico real timeline v34 | 300 frames decodificados, animação 3D visível; arquivo .aurea original intacto e MP4 preservado após shutdown |
| H.264/HEVC software e relógio fracionário | Arquivos decodificados por uma segunda instância independente; contagem/ordem/timestamps/áudio verificados |

O host usa FFmpeg 8.1.2, enquanto a dependência Android fixada é 8.0.3. Essa
matriz não valida os codecs MediaCodec dos fabricantes. Identidades do
executável, bibliotecas FFmpeg, fontes antes/depois e resultados estão em
`build/validation/astra-export-20261013/host/gpu-transfer-validation.json`.
As fontes de produção permaneceram iguais entre os dois contextos; somente a
fixture de paralelismo mudou. Um log de build interrompido não é teste aprovado.

Depois da correção posterior de propriedade do diagnóstico concorrente em
`Engine.cpp`, a revalidação dirigida no novo executável repetiu
`ExportTransferDeadline` (2 casos/30 verificações), `ExportLegacyNativeGpu`
(1/27) e `OfflineNativeGpu` (1/52): **4 casos, 109 verificações, 0 falhas**.
Essa rodada confirmou novamente o prazo do primeiro fence, o projeto histórico
de 300 frames e as composições de 30/12/3.600 frames com decodificação
independente. Nenhum caso foi pulado; fontes e executável permaneceram estáveis
antes/depois da execução. A correção não mudou o renderer ou os headers GPU.

O manifesto desta rodada é
`build/validation/astra-export-20261013/host/gpu-transfer-owner-final-summary.json`.
O executável tem 102.842.880 bytes e SHA-256
`5c1b01dbb756b7f92dfd623fb9cd24691eca4f83a6d5854be99eadde18da8533`;
o `Engine.cpp` correspondente tem SHA-256
`4859b9faa09c45bdeae030535573662189607193248f296fcaa33f49aee5fad7`.
A matriz de 25 casos permanece como evidência do contexto anterior; os quatro
casos repetidos não são somados ao seu total nem às suítes sobrepostas de
startup/diagnóstico. Esta revalidação também é host Vulkan com codec software,
não uma confirmação em OPPO/Vivo/Realme ou uma execução nativa iOS.

## Encerramento offline que escapava da espera finita

A CI compartilhada `38466789159` falhou na asserção final de
`CancellationKeepsSourceTexturesUntilFenceThenRestoresPreviewProxyWithoutWaitingIdle`.
A asserção anterior, enquanto o fence estava pendente, passou. A reprodução
local isolada confirmou a mesma falha: depois de sinalizar o fence e chamar
`render_frame`, a destruição do renderer offline executava **três `wait_idle`**
na thread chamadora. Não era limpeza de outro teste ou de uma sessão anterior.
Com uma espera injetada de 700 ms em uma dessas chamadas, o caller levou
704,1–709,2 ms. A falha da CI e os logs anteriores continuam preservados.

Retirar essas esperas exige comprovar mais que o último frame normal. Um teste
Vulkan real mostrou que `wait_frame(last_submitted_frame(), 0)` pode retornar
sucesso com um upload auxiliar posterior ainda pendente; também reproduziu
uploads de inicialização sem qualquer frame normal. O novo `poll_export_work`
consulta todas as submissões normais e auxiliares com prazo zero. Vulkan e Metal
verificam seus fences/completion states; OpenGL ES insere e consulta um fence
que cobre comandos fora do frame. A implementação padrão não fornece prova e
retém os recursos. Nenhum timeout autoriza liberar recursos ainda usados.

Sob o mutex de render do motor, a comprovação é seguida imediatamente pela
aposentadoria dos recursos GPU privados do renderer offline. O caminho novo
não chama `trim_external_images` global, que poderia atingir imports retidos
pelo preview. O renderer offline perde seu backend e rejeita comandos tardios.
Os callbacks das fontes que continuam fechando apenas sinalizam prontidão CPU;
a sessão/factory permanece viva até esses workers retornarem. Assim o preview
pode avançar enquanto a fonte CPU termina, sem guardar uma comprovação antiga
para liberar recursos GPU depois de novos frames do preview. O shutdown normal
do preview conserva seu comportamento anterior; o offline aposentado tem
destruição idempotente. Vulkan drena as liberações privadas já comprovadas com
`wait_frame(frame, 0)`, sem uma nova espera global.

Depois da correção, a asserção original foi preservada e passou. No mesmo
probe, o caller levou **1,4 ms, zero `wait_idle`**, e não acionou o atraso
injetado. A primeira rodada AFTER passou **18 casos/400 verificações** nos filtros
`ExportGpuRetirement`, `OriginalModelExport`, `ExportPendingGpu` e
`ExportSourceStartupGpu`. Ela cobre propriedade dos recursos até o fence,
auxiliares posteriores, setup sem frame, preview ativo com fonte CPU retida,
callback tardio sem submissão GPU e duplo shutdown. A revalidação nativa de
transferência/projeto v34/MP4 acrescentou **4 casos/109 verificações** distintos:
**22 casos/509 verificações, zero falhas**, sem skips nem fontes alteradas
durante as execuções. Esses casos não são somados às matrizes históricas acima.

O manifesto é
`build/validation/astra-export-20261013/host/export-retirement-validation.json`.
O executável host correspondente tem 102.907.904 bytes e SHA-256
`11ef72e40ec3dd732da3d98b5bb57c0ba667c58a04b5870d76e05782442ba879`.
O BEFORE da nova retenção tem 4 execuções/88 verificações/18 falhas preservadas;
os dois casos de callback tardio/duplo shutdown foram acrescentados depois e
não são apresentados como reproduções anteriores. A CI original continua
marcada como falha; a aprovação local não a substitui por uma CI corrigida.

Vulkan foi compilado/executado neste host; Metal e OpenGL ES estão implementados
e ainda requerem seus builds/execuções nativos nesta versão. O teste de 700 ms
é injeção determinística, não captura de hang em telefone OEM. Uma API nativa
de driver que nunca retorna ainda não pode ser interrompida dentro do processo.
Nenhuma blacklist por fabricante foi criada. Essa correção resolve o caminho
de bloqueio comprovado no encerramento da exportação; não confirma compatibilidade
física com OPPO, Vivo ou Realme.

### Falhas terminais Metal preservadas durante a aposentadoria

Uma revisão complementar reproduziu dois erros na nova consulta Metal, sem
atribuir causa a um aparelho. Primeiro, um auxiliar que saiu de `Timeout` para
`MTLCommandBufferStatusError` retornava sempre o mesmo erro e nunca saía da
lista de submissões. O harness MSVC que compila o loop exato com estados
retornáveis simulados registrou **16 verificações/12 falhas antes** e **16/0
depois**. O status de conclusão segura agora é separado do erro original:
um comando terminal pode ser retirado, mas o erro 20/5 e seu detalhe continuam
na sessão. Estado não terminal e `DeviceLost` não fornecem essa comprovação.

O motor copia o detalhe e, se a falha já é conhecida antes da finalização,
publica resultado/diagnóstico/recuperação com falha. Não transforma o arquivo
em exportação bem-sucedida só porque os recursos podem ser retirados. A nova
regressão no motor confirmou os dois códigos, a mensagem após o provider mudar
sua memória, diagnóstico `failed`, pacote de recuperação `Failed` e retirada
dos recursos sem `wait_idle`. Falhas de cleanup que só chegarem depois da
publicação terminal são registradas no dono da sessão/log; o snapshot terminal
imutável anterior não é reescrito.

Segundo, os slots de frames não estão em ordem cronológica. Consultar primeiro
um slot recente `Completed` por `wait_frame` retirava um slot antigo `Error`
antes de registrar seu erro. O probe do loop exato, com frame 40 no slot 0 e
frame 29 no slot 1, registrou **13 verificações/10 falhas antes** e **13/0 depois**.
A consulta export-only agora observa todos os comandos antes de retirar qualquer
slot e só executa callbacks após comprovar também os auxiliares. O helper de
aposentadoria do preview conserva a política anterior. A regressão Apple recebeu
15 novas asserções para conclusão, erro, ordem dos slots e estado pendente.

O contexto host com o out-status passou **23 casos únicos/545 verificações,
zero falhas**, nos mesmos filtros acima mais transferência/projeto antigo/MP4
nativo. O caller do cancelamento levou **1,0 ms, zero `wait_idle`**, sem acionar
a injeção de 700 ms. O executável tem 102.933.504 bytes e SHA-256
`f43fc794e2deef768515ab3042f81da67861ece24155c8bf1156ef79056b9897`.
Depois dessa rodada os fontes Metal e sua fixture Apple mudaram para preservar
o erro de slots fora da ordem. Os probes portáteis do Metal final passaram **13/0** e
**16/0**. São contagens separadas, não somadas aos 23 casos ou às matrizes
históricas. O manifesto desse contexto registra fontes, binário e escopo
em `build/validation/astra-export-20261013/host/export-retirement-status-validation.json`.

Os harnesses substituem tipos Objective-C por estados portáteis; não comprovam
compilação ou execução Apple. A tradução MSL Windows dos dois novos shaders
Motion Tile passou no tradutor e checker existentes: UBO 752 B, push 112 B,
texturas/samplers 0..11, uniforms 12, push 30, MRT 0/1 e FragDepth. A variante
`fs_main_c0` preserva cor 0/profundidade e elimina cor 1. UBO/push cabem nos limites
compartilhados 1.024/128 B. O tradutor foi compilado somente na configuração
host separada com SPIRV-Cross em cache, sem instalar SDK. Seu manifesto é
`build/validation/astra-export-20261013/host/motion-tile-msl/summary.json`.
Tradução MSL/checagem de ABI no Windows é distinta de `xcrun metal`/`metallib` e
execução Metal Apple; estas permanecem pendentes para este snapshot.

Após a correção complementar comprovada de minificação/emenda não espelhada
do Motion Tile, os fontes `Renderer.cpp` (somente seu grupo de planos),
`MotionTileEffect.cpp` e `plane_tile.frag` também mudaram. Um novo build comum
foi compilado antes da revalidação dirigida da exportação. No contexto cumulativo
final, os filtros `ExportGpuRetirement`, `OriginalModelExport`,
`ExportTransferDeadline`, `ExportLegacyNativeGpu` e `OfflineNativeGpu` passaram
**15 casos únicos/308 verificações, zero falhas**, sem skips; o caller da
restauração levou **1,1 ms, zero `wait_idle`**. Fontes e executável permaneceram
idênticos durante a rodada e no fechamento do manifesto. O executável atual
tem 102.939.136 bytes e SHA-256
`c97e9a02ffc9fa8f028d1c737bf291947ccd82ca0e9b964953f844f57aee4281`.
As contagens se sobrepõem ao contexto anterior de 23 casos e não são somadas.

O manifesto cumulativo é
`build/validation/astra-export-20261013/host/export-retirement-cumulative-validation.json`.
Ele registra o fonte Metal final e os probes 13/0 e 16/0, cuja compilação Apple
permanece pendente; o host executado continua sendo Vulkan/FFmpeg software.
A tradução MSL foi repetida sobre o fragmento Motion Tile final e conservou
todos os contratos de UBO/push/bindings/MRT/depth já listados, com zero problemas
no checker. A identidade atual está em
`build/validation/astra-export-20261013/host/motion-tile-msl-final/summary.json`.
Nenhuma compatibilidade física OEM é inferida dessas rodadas.

## Erros de setup que eram confundidos com falta de memória

O setup de texturas/readbacks substituía os erros originais por `OutOfMemory`.
Um harness com o fragmento exato de alocação reproduziu quatro causas
(`NotSupported`, `DeviceLost`, `OutOfDeviceMemory`, `BudgetExceeded`) retornando
todas como `OutOfMemory`. Depois da correção restrita à exportação, as quatro
retornaram seus códigos e detalhes reais. O teste do motor completo confirmou
o mesmo para **8 combinações entre textura e readback**, 40 verificações.

A correção copia o detalhe antes de outra chamada ou destruição do provider,
preserva a primeira causa e aplica o mesmo tratamento à exportação de imagens.
Falta de memória/orçamento ainda permite reduzir a quantidade de slots quando
há um slot válido; erros de dispositivo/formato encerram o setup. Não há
alteração da resolução ou do compositor de preview. Os harnesses before/after
estão em `build/validation/astra-export-20261013/host/setup-error-before/` e
`setup-error-after/`. A mensagem enganosa foi comprovada no código/testes;
não foi identificada como causa específica dos relatos Infinix/OPPO/Vivo/Realme.

Uma falha do novo diagnóstico foi reproduzida antes da correção: o relógio
continuava avançando depois de concluir/falhar/cancelar. Após uma pausa de
100 ms, uma exportação encerrada aparecia com 30→140 ms de espera. Isso
distorceria um relatório recebido mais tarde. A correção congela o snapshot
terminal, inclusive contadores e primeiro input/pacote; callbacks tardios não
o reescrevem. O teste isolado MSVC registrou 2 casos/16 verificações/9 falhas
antes e 2 casos/19 verificações/0 falhas depois; as 3 novas verificações cobrem
callbacks tardios. A espera ativa continua avançando. Identidades e resultados
estão em `build/validation/astra-export-20261013/host/diagnostics-timing-before/`
e `diagnostics-timing-after/`. Esse bug do diagnóstico não é uma causa atribuída
aos travamentos OEM.

A tentativa REA `capture_process_scenario` recusou a execução nativa Windows:
o adaptador não verifica a limpeza de processos descendentes nessa plataforma.
Nenhum teste foi executado pelo REA nessa tentativa. A limitação está em
`build/reference/astra-export-20261013/gpu/rea-native-process-unavailable.json`;
os testes do runner regular são evidência complementar, identificada como tal.

## Dependência FFmpeg inspecionada

No fonte local fixado FFmpeg 8.0.3, `libavcodec/mediacodecenc.c`:

- A entrada NV12 utiliza `COLOR_FormatYUV420SemiPlanar`; o modo Surface usa
  `AV_PIX_FMT_MEDIACODEC` e não é o caminho atual do AUREA.
- O armazenamento CPU H.264/HEVC é alinhado a 16; cropping de bitstream mantém
  o tamanho visível solicitado.
- A função original `copy_frame_to_buffer` recebia capacidade, mas não a
  validava antes da cópia. O código também não verificava o retorno de
  `queueInputBuffer`. Esses achados foram encaminhados à investigação de
  encoders para reprodução e correção da dependência. Não são prova de uma
  causa em um telefone específico.

O fonte de referência e o fixado devem ser distinguidos: consultar o
[wrapper oficial FFmpeg](https://ffmpeg.org/doxygen/8.1/mediacodecenc_8c_source.html)
explica seus caminhos, mas não demonstra quais bytes estão no APK instalado.
A identidade de binários e execuções analisadas deve passar pelo REA e ficar
registrada em `build/reference/`.

## Documentação e implementações públicas

A documentação Android explica que buffers retidos podem impedir a produção
do codec de modo dependente do aparelho; YUV flexível requer respeitar o layout
dos planos. Stride/slice height brutos não permitem adivinhar um layout vendor
com segurança. São critérios de investigação, não diagnósticos já comprovados
no AUREA. [MediaCodec](https://developer.android.com/reference/android/media/MediaCodec).

A visibilidade da cópia GPU para o host requer a ordem de sincronização e, para
memória não coerente, invalidação. O caminho Vulkan disponível faz ambas após
o fence. [Sincronização Vulkan](https://docs.vulkan.org/spec/latest/chapters/synchronization.html).

Media3 Transformer usa seleção por capacidades, entrada Surface e ajustes
restritos por versão/chipset. Seu código possui uma restrição de operating
rate para determinados SoCs Snapdragon e Unisoc no Android 31–34. O AUREA não
solicita o operating rate extremo dessa implementação; copiar a lista de
modelos não corrigiria uma causa demonstrada. As estratégias informam o
diagnóstico, sem importar código ou mudar o compositor.
[DefaultEncoderFactory](https://github.com/androidx/media/blob/release/libraries/transformer/src/main/java/androidx/media3/transformer/DefaultEncoderFactory.java).

## O que continua desconhecido

Nenhum log recebido estabelece qual encoder OPPO/Vivo/Realme foi escolhido,
quais buffers foram negociados ou onde parou o primeiro frame. Fabricante não
é uma prova de chipset/driver. A confirmação requer relatório por sessão com
modelo/SoC/API, codec e formato, primeiro frame/pacote, estágio e tempo de
espera; esse diagnóstico não deve conter caminhos de mídias ou conteúdo dos
projetos. A exportação simples, multicamada, com efeitos, motion blur, 3D e
projetos reabertos deve ser validada também nos aparelhos afetados.
