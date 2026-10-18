# ASTRA — investigação da exportação

13/10/2026. Escopo: exportação e suas interfaces necessárias. A investigação
não autoriza atribuir aos aparelhos OPPO, Vivo, Realme, MediaTek, Snapdragon ou
Unisoc uma causa que ainda não aparece nos relatos recebidos. Não há aparelho
afetado conectado; o usuário recebe relatos de terceiros. A validação em
emulador cobre a integração Android e seu codec, não esses fabricantes.

## Evidências e correções

| Defeito demonstrado | Evidência antes | Correção e evidência depois |
|---|---|---|
| Encerramento nativo escapava do prazo de abertura | `open` falhava; `abort` de 700 ms fazia o chamador esperar cerca de 710 ms apesar do prazo de 100 ms | Limpeza e destrutor ficam no worker que possui o sink; caller retorna dentro do limite com tolerância de agendamento, preservando o erro original |
| Limpeza de um sink já aberto bloqueava o chamador | Abort/destrutor do proxy esperavam aproximadamente 705–713 ms | Monitor existente assume o encerramento; espera do caller limitada a 100 ms, recursos e permissão retidos até o retorno real |
| Retry de hardware impedia software | Duas sessões nativas travadas preenchiam a quota; software recebia `BudgetExceeded` antes de abrir | Timeout de abertura é `EncoderStalled`; falha por travamento vai diretamente ao nível 2. Quota continua em duas sessões |
| Falhas diferentes da GPU viravam falta de memória | Injeção de `NotSupported`, `DeviceLost`, `OutOfDeviceMemory` e `BudgetExceeded` retornava `OutOfMemory` em 4/4 casos | Primeiro código e detalhe do backend são copiados antes de destruir recursos. Probe de código exato preserva 4/4 códigos e detalhes; testes do motor cobrem texturas e readback |
| Capacidade do buffer de entrada não era validada na dependência FFmpeg | Frame de 384 bytes em capacidade declarada de 100 sobrescrevia 284 bytes de guarda | Patch versionado valida capacidade/layout antes da cópia |
| Erro de `queueInputBuffer` era ignorado | Retorno injetado −777 era apresentado como sucesso | Patch propaga a falha do envio |
| Último pacote com EOS era perdido e seu buffer não liberado | Saída de 4 bytes + EOS retornava EOF, zero pacote e zero release | Patch entrega o último pacote e libera o buffer; bateria controlada antes/depois cobre as três falhas anteriores |
| Encoder usado para extradata permanecia no fim do fluxo | Codec2 `c2.android.avc.encoder` aceitava a abertura com GLOBAL_HEADER, mas encerrava cedo: fonte original isolada produziu zero pacotes; versão com as correções de buffers/EOS produziu só um dos 12 frames, com PTS incorreto | Nova instância do mesmo codec após a sondagem de extradata produz 12/12 frames, PTS exatos e EOS correto em NV12 e I420. Reconfigurar o mesmo objeto foi testado e falhou; essa tentativa não integra a correção final |
| Consulta de bitrate abria outro codec fora do gate | Kotlin chamava `createEncoderByType` e `release` antes de `startExport` | Consulta ao catálogo `MediaCodecList` sem instanciar encoder; abertura/limpeza ficam no caminho protegido do motor. Não é uma causa atribuída a OEM |
| Falha de admissão podia reutilizar resultado antigo | C++ pode retornar antes de substituir a sessão; Kotlin aceitava flags antigos quando o código coincidia | Android e iOS comparam identidade da sessão; token antigo rejeita retry/causa antiga, token novo aceita falha idêntica. Sem token há comparação de snapshot imutável. Três regressões JVM aprovadas; harness Swift disponível para host com compilador |
| Diagnóstico terminal continuava contando espera | Depois de encerrar, espera registrada crescia de cerca de 30 para 140 ms | Snapshot terminal congela tempos, metadata e contadores; callbacks tardios não mudam o relatório |
| Recusa concorrente encerrava diagnóstico de outra chamada | Teste controlado: exportação principal ativa, segunda chamada recusada com código 5; seu resultado congelava a sessão principal como `failed` | Finalização passou para a seção protegida de admissão e usa somente o objeto criado pelo próprio chamador, inclusive na recuperação. Regressão antes: 1 caso/12 checks/2 falhas; depois preserva sessão ativa e passa. Manifesto `diagnostic-owned-after-summary.json` |

As reproduções controladas comprovam defeitos do código e da dependência
fixada, não a causa de cada relato antigo. Resultado, fonte e identidade dos
probes estão em `build/validation/astra-export-20261013/host/` e
`build/validation/astra-export-20261013/ffmpeg-adapter/`. Os resultados finais
devem usar uma compilação uniforme: binários produzidos enquanto um header
mudava não são considerados aprovados.

## Pipeline e formatos

O produto compõe offline em RGBA16F e converte para SDR BT.709 limitado, Y R8
e CbCr RG8. A transferência GPU→CPU entrega NV12 compacto à fila, com dono do
slot até o sink terminar. Vulkan usa fence, barreira para leitura do host e
invalidação de memória não coerente. GLES usa PBO/fence e compactação dos canais.
A auditoria e os testes de stride estão em
[ASTRA — transferência](ASTRA_EXPORT_TRANSFER_2026-10-13.md).

O Android exporta pelo FFmpeg 8.0.3, wrapper NDK MediaCodec com buffers CPU ou
encoders software compilados. MP4 é muxado por libavformat. Não há input
Surface do encoder, Java ByteBuffer de pixels nem Android MediaMuxer nesse
caminho. Surface/ImageReader do decoder são uma etapa diferente. NV12,
I420 e YUV flexível não são intercambiáveis por suposição; a seleção pede
explicitamente cor 21 ou 19. Cor negociada desconhecida continua `null` no
diagnóstico.

A seleção verifica a configuração efetivamente enviada pela dependência:
dimensões de armazenamento alinhadas a 16 (1920×1080 visível pode solicitar
1920×1088 ao codec), FPS, bitrate, perfil e modo CBR/VBR. Consulta de capacidade
serve para evitar configurações rejeitadas; só produzir e validar frames
comprova execução. A alternativa software existe no pipeline compartilhado.
O modo de segurança existente usa H.264 Baseline e arredonda os lados para
múltiplos de 16; preserva FPS e bitrate solicitados. Não foi criada lista
genérica de fabricantes proibidos.

## Prazos, recuperação e limites

A abertura possui prazo sem atividade e teto absoluto; limpeza não pode
prolongar esse retorno. Worker nativo abandonado mantém seus recursos e
permissão até realmente encerrar. Não se destrói um buffer que a GPU ou o
codec ainda usa. O fallback reinicia o arquivo com outro encoder e só publica
após conclusão e validação.

Polling de fences usa fatias de 100 ms. Testes injetam timeout, conclusão
atrasada válida e cancelamento; isso cobre um backend que devolve controle.
Uma chamada de driver, URI ou syscall que nunca retorna não se torna
preemptível por um timeout no caller. A preparação anterior ao retorno de
`startExport` ainda possui operações de provider que exigem evidência por
aparelho antes de uma correção específica. O diagnóstico paralelo permite
localizar a fase. Não se promete eliminar todo bloqueio possível de driver.

Armazenamento é verificado antes e durante a exportação; saída é criada em
arquivo temporário próprio e depois publicada. Android 10+ usa item pendente
de MediaStore e remove o item se a publicação falha. Android antigo usa
permissão pública ou diretório do app. Exportação atual é SDR de 8 bits;
HDR/P010 não é anunciado como saída. Recursos descartáveis de exportação são
liberados sem apagar projetos, mídias ou arquivos de uma sessão ativa.

O relato Infinix Android 13/build 2151 informa erro 26 e mensagem de memória,
mas não identifica codec/primeiro pacote. O ANR Xiaomi Android 15/build 2178
mostra a main thread esperando serviço Google; não demonstra um deadlock do
encoder. Sem APK/símbolos correspondentes e diagnóstico completo, esses
relatos não são prova da mesma causa das reproduções novas.

## Diagnóstico recebido dos usuários

Android e iOS coletam snapshot leve da sessão sem adquirir locks de modelo,
render ou codec. No Android, o registro privado em `noBackupFilesDir` usa
AtomicFile; no iOS fica em UserDefaults. Até três tentativas são mantidas,
com limite de 3.000 caracteres e inclusão no envio já existente de
“Relatar um problema”, iniciado pelo usuário. Não há envio novo automático.

O registro contém fabricante/modelo, Android/API/ABI/processo 32/64 bits,
hardware/board e SoC quando o sistema disponibiliza; resolução desejada e
armazenamento codificado, FPS, bitrate/perfil/cor solicitados, encoder/backend,
primeiro input/pacote, frames/pacotes, estágio, tempo de espera e resultado.
Nome de mídia, caminhos, projeto, conta e conteúdo do vídeo não integram esse
snapshot. SoC ausente não é deduzido da marca.

Uma recusa anterior à criação da sessão nativa não possui novo token nem
encoder selecionado. Nesse caso o relato básico registra o código; o coletor
rejeita o diagnóstico antigo, em vez de atribuí-lo à tentativa recusada.

Após instalar a build corrigida, o usuário afetado deve tentar exportar o
projeto que falhava e usar “Relatar um problema”. O relatório permite comparar
uma tentativa simples com a tentativa original e separar abertura, primeiro
frame, GPU, codec, finalização e publicação. A confirmação OEM precisa desses
registros ou de testes no aparelho; um emulador não substitui essa etapa.

## Pesquisa primária

Android documenta estados do codec, EOS, layout de planos e que `flush` de
encoder tem comportamento indefinido. Isso sustenta investigar a reutilização
da instância, cuja falha foi depois reproduzida localmente.
[MediaCodec](https://developer.android.com/reference/android/media/MediaCodec).

Suporte depende de tamanho, taxa, alinhamento e configuração, e o desempenho
publicado é uma estimativa específica do dispositivo.
[VideoCapabilities](https://developer.android.com/reference/android/media/MediaCodecInfo.VideoCapabilities).

Stop/delete NDK chamam encerramento nativo síncrono; não oferecem prazo de
caller. O código explica por que limpeza precisa permanecer sob propriedade
do worker, sem provar que determinado HAL OEM trava.
[NdkMediaCodec](https://android.googlesource.com/platform/frameworks/av/+/refs/heads/main/media/ndk/NdkMediaCodec.cpp).

Media3 Transformer seleciona encoders/configurações por capacidades e tem
workarounds delimitados. Um relato público de exportação em Vivo/iQOO
Qualcomm mostra que fallback de configuração não garante recuperação de
falhas em execução; não prova a causa do AUREA nem justifica copiar blacklist.
[DefaultEncoderFactory](https://github.com/androidx/media/blob/release/libraries/transformer/src/main/java/androidx/media3/transformer/DefaultEncoderFactory.java),
[relato Media3 #2666](https://github.com/androidx/media/issues/2666).

Dois relatos públicos recentes reforçam a necessidade de diagnóstico exato:
Media3 1.11.0 recebeu FPS zero do decoder em modelos OPPO, Realme e Vivo e
falhou ao criar seu `Format`. O AUREA não usa essa classe nessa exportação;
`MediaCodecSource::frame_rate_of` já exige FPS positivo e deriva a taxa das
amostras quando necessário. Não foi aplicado workaround de preview/decoder
por marca. [Media3 #3399](https://github.com/androidx/media/issues/3399).

Outro caso envolve largura mínima do encoder Unisoc, com software disponível
mas não considerado pelo seletor. No AUREA, verificar a configuração real e
manter alternativas software evita depender de um único encoder anunciado.
O relato não prova a causa de um telefone AUREA.
[Media3 #3451](https://github.com/androidx/media/issues/3451).

## Validação

Matriz obrigatória: vídeo simples, múltiplas camadas, efeitos, motion blur,
3D e projeto antigo, verificando dimensões, todos os frames/PTS, conclusão,
decodificação independente e preservação dos originais. Resultados finais e
identidades da build são registradas nos manifestos por contexto. O motor final passou
em 59 casos únicos e 1.371 verificações, incluindo bloqueio de abertura,
cancelamento, quota, recuperação, diagnóstico e oito combinações de erro GPU.
Android JVM passou em 408 testes/77 suítes. A matriz nativa Android executou
sete exportações em API 31/x86_64, além de duas exportações 1080p. FFmpeg host
8.1.2 independente decodificou todos os frames dos sete cenários, dos probes
MediaCodec e das duas saídas 1080p, com dimensões, PTS e duração corretos.
Esse decoder é separado do exportador Android FFmpeg 8.0.3 fixado.

O probe real MediaCodec também passou com raster visível 192×100, que exige
armazenamento alinhado e crop: NV12 e I420 geraram 12 frames cada, decodificados
com tamanho visível exato e ordem/timestamps corretos. Essa verificação exercita
o padding/crop do wrapper, diferentemente de testar só dimensões múltiplas de 16.

A matriz host GPU tem resultado final 25 testes únicos/1.729 checks/zero
falhas; não é somada aos 59 anteriores porque existem casos compartilhados.
Dois asserts iniciais falharam porque a fixture declarava executar paralelismo
durante o cooldown que força profundidade 1. Somente a fixture paralela foi
ajustada para aguardar a política existente expirar; os asserts de profundidade,
pixels, PTS e áudio permaneceram. A repetição de dois casos passou; produção e
preview não mudaram. O manifesto preserva ambas as rodadas e não as conta como
testes distintos. Após a correção de concorrência, quatro casos GPU relevantes
foram reexecutados no motor final (109 verificações/zero falhas), incluindo
timeout de transferência, legado e exportação nativa. Essa repetição não é
somada aos totais anteriores. A rodada Android final reexecutou os nove cenários
no código congelado; incluindo quatro probes de formato MediaCodec, o decoder
independente verificou **156 frames em treze arquivos**, sem erro.
Implementação iOS é
mantida junto ao motor/ponte; nesta máquina Windows, as auditorias Swift são
estáticas, sem compilação Xcode nem validação em iPhone.

REA é usado para identidade de pacotes e inspeção/runtime de aplicativos;
código-fonte completo e testes próprios usam ferramentas regulares como
complemento. A capacidade REA de executar cenário nativo Windows não está
disponível neste provider e a limitação foi registrada em `build/reference/`.

## Entrega local final

[AUREA 0.1.8 BETA 3 — 2179 ARM64](C:/Users/Ruan/Documents/Aureabeta/entrega/AUREA-0.1.8-BETA3-2179-arm64-v8a.apk),
75.865.011 bytes. SHA-256:
`a97e1121590dfa3148a81b56e6e6763057605c72bf0ed0e6184d930e007014bc`.
Assinaturas v2/v3 verificadas, mesmo certificado do candidato anterior;
o candidato anterior não é a entrega válida. O APK contém somente ARM64.

O inventário REA do APK final tem evidência
`ev_5edb7d62f65b7f604fc415049e4b6955f431181cfce6bdc1505e18903cc51952`.
Inventário não é prova de execução nem de assinatura. Essas verificações
independentes e hashes das fontes ficam no
[manifesto final](C:/Users/Ruan/Documents/Aureabeta/build/validation/astra-export-20261013/production-final-release-manifest.json).

Build ID `7b084622af703132a048c191d738b9310bbd9505` coincide entre a biblioteca
do APK, biblioteca não removida de símbolos e símbolos preservados. Mapping
R8 comprimido, símbolos nativos e manifesto FFmpeg ficam em
`build/validation/astra-export-20261013/release-symbols/`, para interpretar
futuros crashes desta build. Isso não permite simbolizar os APKs antigos sem
seus símbolos correspondentes. Até essa entrega local não houve publicação
externa; não houve teste físico nos aparelhos afetados. No host Windows, iOS
teve implementação e quinze auditorias estáticas, sem Xcode ou execução nativa.

Na entrega posterior dos três APKs e do IPA, o usuário autorizou a publicação
de uma branch pública separada para compilar iOS. A seleção de 134 arquivos foi
enviada no commit `35bac8c052d2480c13162c7ab1b3c35b260891a2`, branch
`codex/beta3-2179-ios-20261013`. O workflow nativo
<https://github.com/ueeruan/aurea/actions/runs/38466795670> está em execução;
seu resultado e a validação do pacote serão registrados em
`entrega/ENTREGA-0.1.8-BETA3-2179.md`. Isso não muda a ausência de teste físico
nos dispositivos afetados.
