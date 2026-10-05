# Auditoria ampliada do AUREA — 04/10/2026

## Estado deste registro

**Validação local da geração 18 concluída:** a suíte C++ completa passou com **1.361 testes, 6.929.148 verificações e zero falhas**, sem crash nessa execução. Os 11 filtros focados passaram; Android x86_64/ARM64, APK de instrumentação e Release compilaram, e a seleção final Android passou 15/15 após repetir integralmente uma fixture corrigida. As fontes iOS foram implementadas e verificadas estaticamente, mas não compiladas nem executadas com ferramentas Apple. O cenário extremo de desempenho permanece incompleto. Esses resultados não demonstram aplicativo livre de crashes em qualquer aparelho. A cronologia, inclusive regressões introduzidas e corrigidas durante a auditoria, está em `build/reference/full-audit-20261004/core/execution-ledger.txt`.

O trabalho amplia a investigação de exportação, transformação e buffering para o ciclo de vida do aplicativo, documentos, tarefas assíncronas, mídia, editor e serviços. A revisão percorreu subsistemas e contratos; não equivale a exercitar cada combinação de controle, shader, arquivo e aparelho. Alterações já presentes no checkout foram preservadas. Este relatório não atribui toda diferença do checkout à auditoria atual.

Os resultados brutos estão em `build/reference/full-audit-20261004/`; a investigação anterior está em `build/reference/stability-20261004/`. O Release 18 usa certificado local Android Debug. Identificação, hash e resultados finais estão registrados abaixo. Os APKs intermediários foram preservados como evidência, sem substituírem a entrega final.

## Implementação, compilação e execução

| Área | Implementação/revisão | Compilação/verificação estática | Execução nativa/local | Aparelho físico |
| --- | --- | --- | --- | --- |
| Motor C++ compartilhado | Correções de jobs, imports, mídia, memória, pacotes, histórico e transcrição; regressões adicionadas | Core/testes compilados na rodada 18 | Suíte completa 18: 1.361 testes/6.929.148 verificações/zero falhas, sem crash; 11 filtros focados aprovados | Não executado em celular físico |
| Render, preview e exportação | Cache real limitado, linha azul dos intervalos reais, recuperação de recursos gráficos e exportação; blur fracionário implementado | Host e Android x86_64/ARM64 compilados no freeze18 | MotionBlur 31/2216, TextOutline 2/26, serialização 33/66821, catálogo 3D 1/321, cancelamento, retomada e trim passaram nos focos 18 e na suíte completa | Não executado |
| Android | Correções de sessão, teardown, importação, salvamento, legendas, IA, comunidade, gestos e memória | Geração 18: x86_64/ARM64, APK uiTest/Release, 361/361 JVM; lint com 0 erros, 727 avisos e 28 sugestões | Seleção final de 15/15 aprovada no APK 18, com repetição integral do Manual; consolidado de 95/95 métodos normais entre gerações. HeavyStress interrompido por desempenho; exportação 720p incompleta, 1080p não iniciada | Não executado |
| iOS | Implementação Swift/ObjC++ correspondente, controles de novo blur e inventário das fontes/ponte | 14 verificadores estáticos passaram; três blocos aplicáveis do verificador legado passaram. Sem compilador Swift/Xcode | Nenhuma execução iOS; fixtures Swift/UI preparadas, partes compiladas explicitamente puladas | Não executado em iPhone |
| Servidor IA Python | Cancelamento, finalização e shutdown corrigidos | Testes locais e coleta final de resultados concluídos | Suíte final 101/101, zero falhas; filtro de 10 regressões de ciclo de vida também passou | Não se aplica; não houve geração remota real |
| Discovery, contas e comunidade | Contratos, armazenamento, autorização e fluxos locais revisados | Suíte Node concluída | 61/61 testes; integrações em Workers local com D1/R2 e presets de legendas passaram | Não houve validação de produção pelo app |
| Ferramentas e localização | Auditorias de fixtures, contratos e chaves; catálogos sincronizados | 7/7 testes de ferramentas e quatro novos testes do gerador; textos das mudanças também preenchidos em ES/RU/HI/AR/ID e catálogo Swift regenerado | Isso não substitui leitura visual por idioma nem teste de leitor de tela em aparelho | Pendente |

O alvo desktop opcional encontrou erro de ordem de includes entre VulkanLoader e headers do SDK Windows. A fonte foi corrigida e `aurea_desktop` compilou com código 0 após o freeze7 (`core/desktop-final-build.log`). Isso comprova compilação, sem execução da interface desktop nesta rodada.

Contagens de filtros, repetição de regressões e suítes sobrepostas não devem ser somadas como casos únicos. Lint sem erros não significa ausência de avisos; auditoria estática não comprova compilação nem execução nativa do iOS.

### APK de teste preservado

- Arquivo: `build/reference/full-audit-20261004/android/AUREA-2139-stability-motion-blur-arm64-freeze18.apk`.
- Pacote `com.aurea.aurea`, versão `2.0.0-beta2`, código 2139, ABI `arm64-v8a`, 60.385.803 bytes.
- SHA-256: `d564b6519e8568876a015fea9e27946e9165dcf3777c05fe02cfa11533a78316`.
- Assinaturas v2/v3 verificadas. Certificado **Android Debug**, SHA-256 `55bf3cc844050df48dff27e71a70cb54e5bca40a5c0db9e43f9d21cfff52b5c8`.
- É um artefato de teste com assinatura local, não uma publicação assinada pela loja. O Release ARM64 foi compilado e verificado; a execução instrumentada usa o APK uiTest x86_64 das mesmas fontes no emulador API35. Não houve execução deste Release em aparelho ARM64.
- Evidências: `android/freeze18-artifact.json` e `android/freeze18-apk-signature.txt`.

## Correções confirmadas

### Reconstrução do desfoque de movimento

O novo caminho substitui a interpolação de matrizes inteiras por avaliação em tempo fracionário: curvas, Hold, expressões, pais, câmera, animadores e deformação são avaliados no instante de cada amostra. A janela é controlada por ângulo e fase do obturador; centralizar aplica `fase = −ângulo/2`. Android e iOS expõem esses valores, amostras mínimas e limite adaptativo por uma única edição atômica do motor, com undo/redo e persistência v41. Projetos antigos recebem uma janela centralizada e limites finitos.

O comportamento é baseado na documentação pública do [After Effects](https://helpx.adobe.com/after-effects/desktop/animate-in-after-effects/assorted-animation-tools/assorted-animation-tools.html) e de [CompItem](https://developer.adobe.com/after-effects/uxp/after-effects-api/compitem). Isso não certifica igualdade pixel a pixel com um renderer proprietário, nem transforma amostragem temporal em custo zero. Movimento já gravado dentro de uma filmagem continua diferente de movimento de camada/câmera; o recurso opcional de vetores permanece separado.

Texto é composto por inteiro em cada instante antes de acumular a exposição, evitando multiplicação de alfa nos glifos sobrepostos e dupla integração do movimento. A cena 3D integra cor antes do pós-processamento; reflexos conservam a câmera de cada instância. Camadas com desfoque desligado mantêm seu instante central mesmo num grupo com câmera em movimento. Composições aninhadas resolvem os identificadores de renderização para suas camadas de origem.

A validação encontrou uma diferença de borda mesmo quando o movimento da letra cancelava exatamente o da camada. A suavização anterior usava a derivada da distância do glifo em blocos de 2×2 pixels, alterada por deslocamentos ímpares no raster intermediário. O gradiente centrado da geração 15 reduziu a diferença máxima de 66 para 1 nível em 255 e igualou a cobertura, mantendo a geometria e os limites originais do teste. O controle sem exposição e com compensação de 1 px deu diferença zero; escalas 0,5 e 1,5 também passaram. São quatro leituras adicionais do atlas por pixel de glifo normal visível, sem novas texturas ou alocações. Fundos arredondados usam gradiente analítico. Isso não comprova invariância sob toda perspectiva nem custo aceitável em todo aparelho.

Poses rígidas compartilham dados imutáveis. A admissão considera um único orçamento de 32 MiB por quadro preparado, somando texto, composições aninhadas, estado temporal, poses deformáveis, uploads de vértices/juntas e estado de desenho. A revisão final encontrou e corrigiu a reinicialização indevida do limite em cada composição, além de uma alocação mínima de glifos mesmo com saldo insuficiente. Prévia pode usar menos amostras; a exportação sinaliza insuficiência quando não cabe a qualidade mínima solicitada, em vez de aceitar silenciosamente o quadro incompleto. Valores não finitos da quantidade de desfoque por camada são normalizados para zero ao ler o projeto e antes da exposição; valores finitos ficam em 0..4. As regressões de orçamento conjunto, reinício por quadro, admissão zero e câmera com NaN/Inf passaram.

A suíte completa detectou ainda dois pixels de diferença no contorno quando o glifo ocupava uma posição distante da origem do atlas. A interpolação de UV absoluto perdia precisão. A geração 18 interpola coordenadas locais e soma a origem apenas na consulta da textura, preservando perspectiva e custo. O teste isolado passou a preencher o atlas com o mesmo prefixo de textos da sequência: o glifo V chegou ao mesmo U≈0,5200 da reprodução e a diferença caiu de 4 para 0, sem relaxar o limite 2.

A investigação do teste extremo também encontrou um ciclo vazio no worker de prévia: durante exportação, `render_frame` retornava cedo, mas um prazo de refino vencido fazia a thread voltar imediatamente à mesma chamada. A espera agora acompanha o fim/cancelamento da exportação e não é encurtada por callbacks de vsync/decoder. A regressão mantém um refino real pendente, envia callbacks durante a exportação e verifica a retomada da prévia.

Essa regressão passou no host com 13 verificações. No emulador Android 18, a medição nativa durante uma exportação menor registrou 0 ms de CPU da thread de prévia em 2.044 ms, enquanto o encoder avançou para 29 de 240 quadros (`android/export-preview-idle-freeze18.txt`). O resultado anterior 11 também foi preservado. Isso confirma a retirada do ciclo vazio nesse caso; não equivale à aprovação do cenário extremo descrito abaixo.

### Motor, tarefas e identidade de projeto

- `JobSystem` deixava workers destacados continuarem após dois segundos de shutdown, com possibilidade de acessar seu dono já destruído. O encerramento agora aguarda a conclusão do trabalho, com cancelamento cooperativo nos consumidores.
- A espera por um handle podia terminar pela conclusão de tarefas não relacionadas. A conclusão passa a ser acompanhada por identidade, com acerto da contabilização de submit/stop, falha parcial de `parallel_for` e vida útil dos slices descartados.
- Importação de vídeo, áudio, HDRI e modelo fazia trabalho lento fora do lock e podia aplicá-lo ao projeto/composição aberto depois. O commit verifica sessão e composição capturadas no início. A inserção de imagem é síncrona no motor; as interfaces protegem o decode anterior e callbacks atrasados.
- Transcrição verifica identidade da mídia/projeto e rejeita resultado vencido; troca de documento e shutdown sinalizam cancelamento. Uma segunda chamada não pode resetar o cancelamento da primeira. Conversão de PTS para amostras evita overflow.
- Detecção de batidas admite PCM e memória de análise antes de abrir o decoder, trata falha de alocação e só publica marcas na sessão/composição/revisão original. Android e iOS liberam o indicador de trabalho em erro e descartam callbacks antigos. O orçamento de análise é `min(32 MiB, orçamento do motor/8)`, incluindo cache e intermediários: comporta aproximadamente 158 s com teto de 32 MiB ou 31 s com 8 MiB. Trechos maiores são recusados com orientação para reduzir a duração; não são truncados. Um detector incremental para faixas longas permanece fora desta implementação.
- O histórico passa a contabilizar memória de rig, materiais, tracks legadas e animadores que não entravam no orçamento anterior.

### Documentos, importação e persistência

- Leitura de pacotes ZIP por streaming, validação de limites dos headers e CRC, preservação do destino existente e tratamento da publicação como commit. Nomes têm limite individual de 4096 bytes e limite agregado de 8 MiB antes da alocação; evita manter listas de nomes arbitrariamente grandes mesmo sem extrair os arquivos.
- O commit de pacote preserva o destino em falha. Descritor de arquivo é fechado também quando o flush falha. `ByteReader` compara comprimentos sem soma que possa transbordar; coordenadas relativas das máscaras PSD usam aritmética de 64 bits.
- As interfaces bloqueiam troca de projeto enquanto uma operação nativa que escreve nele está pendente; o token dura até o retorno, inclusive se a tela solicitou cancelamento.
- Falha no primeiro save de um projeto novo deixa o novo destino associado ao projeto novo, evitando que um save posterior sobrescreva o documento anterior. No iOS, renomeação malsucedida também deixa de mudar o destino do autosave.
- Operações de compartilhar/exportar pacote usam temporários exclusivos; seleção de mídia e criação pela Home possuem identidade de pedido/projeto, impedindo retorno tardio de importar no documento errado.
- SVG é lido com limite de 32 MiB e parse fora da interface; SRT tem limite de 4 MiB. Erros liberam estado de trabalho e recursos temporários; leitores validam metadados antes de converter para tipos inteiros.
- A reabertura limitada por referências preserva também a textura de partículas e o HDRI por objeto. A revisão encontrou que o filtro inicial ignorava essas referências indiretas, podendo omitir a textura depois de apagar sua camada de imagem original. Os dois testes de domínio de IDs e do fluxo real de salvar/reabrir passaram na rodada 8.

### Mídia, áudio, 3D e memória

- Timestamps de áudio esparsos podiam criar silêncio sem limite. O preenchimento agora respeita a janela solicitada e valida taxa, canais e posições.
- Cache de waveform corrige pin de entrada vinda do disco, contabilização dos níveis, publicação após relink e uso de iterador após possível remoção por orçamento. Duração e consultas também receberam validação de limites.
- Importação glTF passa a limitar leitura inicial e memória do parser/buffers externos antes da estimativa final; o pico transitório de cópia entra no orçamento. Caminhos existentes de FBX/HDRI foram revisados quanto aos seus limites.
- Amostragem de thumbnails valida crop, stride, layout de planos, formato e tamanho de saída. No iOS, cartões criam a miniatura diretamente pelo ImageIO com dimensão máxima de 2048 px e descartam conclusão antiga.
- O fallback da estimativa de profundidade deixa de reter mapas independentemente do cache limitado; remoção e publicação respeitam a política de retenção.
- `PreviewProxy` passou isoladamente e no build coerente da terceira rodada. Objetos compilados contra gerações diferentes da interface de exportação são uma hipótese para os crashes iniciais, não uma causa demonstrada. O registro de execução preserva essa distinção.

### Memória: causas, contenção e limites

Os achados concretos incluem picos transitórios de decodificação/cópia, dados de importações sem quota agregada, pools de upload que duplicavam memória e referências de thumbnails fora do cache limitado. O trabalho reduz essas retenções e recusa operações acima dos orçamentos; monitorar pressão não substitui limitar as alocações que a provocam.

As fontes CPU ainda necessárias ao histórico de desfazer são preservadas. A coleta é conservadora: uma fonte antiga pode continuar ocupando a quota mesmo após sair do histórico. Salvar e reabrir recarrega somente as fontes referenciadas pelo projeto, mas também reinicia o histórico. Portanto, a quota pode recusar uma nova importação após várias exclusões; não se liberam essas fontes indiscriminadamente, o que quebraria o desfazer.

PSD passa a preparar a importação antes de publicar a camada no projeto. Arquivos, composições e assets parciais têm rollback; o histórico só descarta refazer depois de preparar seu snapshot. Duas cópias temporárias da composição entram na admissão. A ponte do motor retorna erro de memória em vez de conservar o staging incompleto. O teste injeta falhas em oito pontos, incluindo arquivo aberto e staging completo, e verifica projeto, refazer e arquivos. O rollback libera suas imagens e snapshots antes de remover composições, mas a implementação de slots ainda constrói um estado vazio ao remover uma composição: a recuperação sob exaustão absoluta do processo não é garantida. A execução final dessas regressões é registrada na tabela de resultados.

| Camada | Causa identificada | Contenção implementada | Evidência atual |
| --- | --- | --- | --- |
| Motor/arquivos | Imagens, modelos e HDRI podiam consumir seus limites separadamente; capacidades de arrays, morphs, LODs e animações ficavam fora da estimativa | Quota CPU agregada de um terço do orçamento do processo, com teto de 256 MiB; reserva antes do histórico e parse/cópias serializados; imports pendentes usam saldo; reabertura carrega fontes referenciadas e valida sessão | Filtros finais ModelBudget, ImportStability e ProjectAssets passaram, inclusive na execução completa da rodada 9 |
| 3D/GPU/exportação | Uploads mantinham cópias/pools além da residência contada; cache de preview pendente competia com buffers de export | Residência inclui uploads; pools sem cópia redundante; trim/fechamento libera recursos sem uso; export drena liberações do preview antes da alocação; trim gráfico serializado também durante export | Filtros finais de pools/modelo, HDRI pendente e trim com encoder ativo passaram; aparelho pendente |
| Imagens/3D/PSD | Decodificar antes de conhecer dimensões e múltiplas cópias RGBA ampliavam o pico; fotos de partes 3D permaneciam retidas globalmente | Preflight de dimensões/orçamento, cache global fraco das fotos 3D, cálculo do pico de HDRI incluindo entrada/decode/ZIP; limites PSD e rollback transacional de OOM implementados | Psd 11 casos/838 verificações passou na rodada 8 e novamente na suíte da rodada 9; aparelho pendente |
| Android | Decoder alocava bitmap/RGBA antes de conter OOM; miniatura Home limitava somente largura; timeline retinha 240 bitmaps além do LRU | Amostragem canônica por arquivo/cap4096 para imagem do projeto; falta de orçamento recusa o decode sem mudar geometria. Forma até 2048, avatar512; Home limita ambos os eixos/área; referências fracas/épocas eliminam retenção extra; encoding de forma escreve no arquivo sem duplicar byte array | 361 testes JVM e ImageMemoryAudit 2/2 no emulador passaram; pressão real em aparelho pendente |
| iOS | ImageIO permitia imagem até 16384² e duplicava buffer RGBA; índice PTS crescia sem teto; falha de alocação podia escapar em caminho `noexcept` | Predecode canônico por arquivo/cap4096; orçamento de `(folga − 16 MiB) / 12` pixels admite ou recusa, sem mudar dimensões. RGBA convertido no mesmo buffer; textura de forma até 2048 px; índice PTS até 16 MiB e 1/16 da folga; `bad_alloc` libera sample/imagem e retorna erro; unwind habilitado no decoder da plataforma e nos arquivos do motor com fronteira de alocação protegida (Engine, Beats, AudioBlockCache e imports) | 14 verificadores estáticos passaram; frameworks/compilação Apple e falha real de alocação não executados |
| Caches das interfaces | Limpeza do cache principal não soltava referências de telas; tarefas antigas podiam repovoá-lo após trim | Home/efeitos/timeline invalidam referências e pedidos por época; trabalho opcional fica limitado enquanto há pressão; background também libera caches | Contratos estáticos iOS e testes JVM Android; pressão real em aparelho pendente |

No Android, o monitor consulta `ActivityManager.MemoryInfo` em worker até uma vez por segundo, independentemente do HUD. A folga do heap Java é `maxMemory − (totalMemory − freeMemory)`: solicita nível crítico 15 se o sistema informa `lowMemory` ou a folga fica em até `max(16 MiB, heap/20)`; nível 10 em até `max(32 MiB, heap/10)`. Repetições do mesmo nível têm intervalo de dez segundos; escalada é imediata. O heap Java não contabiliza integralmente recursos nativos/GPU, portanto o orçamento do motor continua necessário. A documentação Android registra que avisos como `TRIM_MEMORY_RUNNING_*` e `onLowMemory` deixaram de ser entregues desde API 34; depender apenas deles deixava a reação incompleta. [ComponentCallbacks2](https://developer.android.com/reference/android/content/ComponentCallbacks2)

No iOS, a medida é a folga de alocação do processo consultada por `os_proc_available_memory`, não a memória livre do sistema nem o heap Java. O monitor consulta até uma vez por segundo: abaixo de 128 MiB solicita nível 10, abaixo de 64 MiB nível 15, e retoma trabalho opcional a partir de 192 MiB. Zero também é tratado como crítico porque, dentro de um app, pode indicar que seu limite já foi excedido; novos decodes/índices não recebem um orçamento de fallback amplo. [os_proc_available_memory](https://developer.apple.com/documentation/os/os_proc_available_memory)

Há uma exceção exclusiva de `TARGET_OS_SIMULATOR`: se essa API retorna zero em um processo sem limite de app, a ponte mede RAM física e `resident_size` e calcula saldo de `min(RAM/8, 1 GiB) − residência`. É um orçamento conservador do simulador, não uma medição de jetsam. Falha de medição ou saldo esgotado continua retornando zero. O fallback não é compilado no app de aparelho e sua execução Apple permanece pendente.

O trim iOS ocorre fora da thread principal, uma execução nativa por vez. O intervalo de cinco segundos vale para repetição; uma escalada crítica ultrapassa o intervalo e, se já houver trim em voo, conserva o maior nível para executar logo depois. Avisos do sistema invalidam os caches imediatamente. Os testes preparados extraem os métodos Swift reais para exercitar essa fila e a escalada; aqui somente a verificação de fonte rodou. A Apple informa que o processo pode ser encerrado antes de receber o aviso de pouca memória, portanto esses mecanismos não garantem ausência de jetsam, crash ou lag em um aparelho. [Responding to low-memory warnings](https://developer.apple.com/documentation/xcode/responding-to-low-memory-warnings)

A resolução da imagem do projeto deve ser estável: o renderer usa suas dimensões para geometria, âncora e rig. Por isso as duas pontes calculam a redução apenas pelo arquivo e pelo teto de dimensão. Memória insuficiente produz recusa controlada, não uma imagem menor que mudaria a composição ao reabrir. Miniaturas da interface continuam podendo adaptar sua resolução.

Na reabertura, o motor também recusa pixels cujas dimensões divergem da largura/altura conhecidas nos metadados salvos. Conserva os dados da camada, âncoras, máscaras e rig, contabiliza a origem como ausente/incompatível e aciona o aviso existente de mídia para religar. Isso tem um limite de compatibilidade concreto: projetos antigos do iOS podiam guardar imagens acima de 4096 px, até o teto anterior de 16384. Essas imagens podem ficar sem renderização nesta versão mesmo com o arquivo original presente, pois o novo carregador não produz aquelas dimensões. Não há migração automática da geometria; a origem precisa ser religada/substituída deliberadamente por uma versão suportada, com revisão do enquadramento. O arquivo original e os metadados da camada não são redimensionados silenciosamente.

### Android e iOS: tarefas assíncronas, IA e comunidade

- Android mantém acessos nativos demorados protegidos durante teardown; cancelamento precede espera, e encerramento/save saem da thread principal. Callbacks de sistema atrasados são descartados. Imports e operações em IO encerram estado de trabalho em `finally`.
- As duas interfaces validam sessão e tentativa em ticket, anúncio, recompensa, retry, acompanhamento e persistência de IA. Uma conclusão antiga não limpa o estado da tentativa nova nem inicia geração na sessão substituída. Cancelamento de transporte é preservado.
- Download de IA no iOS limita a leitura do corpo de erro, verifica cancelamento antes da publicação e remove temporários em falha.
- Comunidade invalida ações/respostas quando conta ou perfil muda. Somente a operação atual publica dados ou encerra o indicador de trabalho; Downloads locais são limpos em falha/cancelamento.
- Legendas descartam estado e callbacks do projeto anterior, propagam cancelamento para preparação/download/transcrição e mantêm a retenção do projeto até a chamada nativa terminar.

### Servidor, download e subprocessos

- A fila de trabalhos trata stop de forma idempotente e respeita cancelamento e espera pelos workers durante shutdown.
- Cancelamento durante download não promove o trabalho a `completed`. A publicação só ocorre para a operação ainda válida.
- FFmpeg cancelado é terminado e aguardado, evitando subprocesso órfão e recursos pendentes. A finalização de jobs deixa de confundir conclusão tardia com sucesso após cancelamento.
- Testes exercitam inicialização/encerramento, cancelamento e respostas atrasadas localmente; não acionam um provedor remoto de geração ou anúncios reais.

## Correções anteriores preservadas nesta auditoria

O detalhamento e os números daquela etapa estão em [STABILITY_EXPORT_PREVIEW_2026-10-04.md](STABILITY_EXPORT_PREVIEW_2026-10-04.md).

- Exportação: vida útil das mensagens de erro, validação de entradas e planos de GPU, filas/pools limitados, erro quando mídia essencial está ausente, repetição limitada de quadros gráficos incompletos, cancelamento durante espera e encerramento de decoders após o export worker terminar.
- Android: publicação transacional na galeria, recusa de arquivo vazio, URI de conteúdo para compartilhar em Android antigo e prazos do codificador que consideram progresso/software.
- iOS: limite de quadros em voo e pool VideoToolbox, validação de planos e cancelamento, distinção entre inatividade transitória e background, suspensão em fila serial.
- Preview: composição renderizada em cache de memória limitado, preparação antes de reproduzir, cancelamento por pausa/seek/edição e intervalos reais mostrados por linha azul de 3 dp/pt na régua. Quadros incompletos não são anunciados como prontos; invalidação e substituição atualizam os trechos.
- Transform: detalhe acompanha confirmação nativa de seek; propriedades controladas por expressão explicam o bloqueio e preservam a fórmula. O vídeo enviado mostra esse caminho de transformação, sem mensagem de erro de exportação.
- Modelos 3D: aviso nas duas plataformas sobre possível falta de memória, aquecimento, lentidão e fechamento do app, com uso por conta e risco.

## Inventário da revisão

| Subsistema | Fontes/contratos percorridos | Forma de evidência e limite |
| --- | --- | --- |
| Motor/editor/comandos | `Engine`, `JobSystem`, histórico, seleção, sessão, queue e entradas JNI/ObjC++ | Revisão de concorrência, cancelamento e identidade; regressões e suíte completa 18 aprovadas. Rodadas intermediárias com falhas permanecem no registro |
| Timeline/animação/keyframes | UI de timeline e gráfico, gestos, detalhe de camada, auto-key, ranges do cache, tracks e histórico | Testes focados anteriores e instrumentação atual; combinações completas de propriedade/expressão não enumeradas |
| Render/efeitos/GPU | Renderer, render scheduler, FrameGraph, EffectGraph, recursos temporários, upload/cache e caminhos de efeitos | Falhas controladas e casos GPU no host; sem alegação de revisão linha a linha de cada shader |
| Texto/vetor/3D | Texto e contorno, painéis/vetor, GltfImporter/Ufbx/HDRI, orçamento de modelo, cena, profundidade e transformação | Casos de render e importação, paridade das pontes; hardware móvel e modelos arbitrários permanecem fora da amostra |
| Vídeo/preview/exportação | MediaManager/VideoSource, caches, PreviewProxy, thumbnails, export engine, MediaCodec e VideoToolbox | Exportações e lifecycle passaram no emulador Android 18; suíte C++18 completa aprovada; VideoToolbox sem execução Apple |
| Áudio/legendas/tracking | AudioBlockCache/Waveform, mixer/fx/conversões, Beats/Spectrum, LocalWhisper e telas | Revisão de entradas/loops/vida útil, regressões adicionadas; execução Apple indisponível |
| Projeto/arquivos/persistência | FileIO/ProjectPackage/Serialization/PSD/History, Home, metadados, renomeação, mídia e seletores | Casos de falha e arquivos malformados; filesystem de iOS/iCloud exige teste nativo |
| Android | 192 fontes Kotlin: Home/editor/ferramentas, exportação, imports, IA, comunidade, diagnóstico, permissões e teardown | 361 JVM e lint; inventário de 43 classes/96 métodos instrumentados (95 normais e um Heavy opt-in). Bateria anterior e repetições discriminadas abaixo; sem aparelho físico |
| iOS | 55 Swift, 6 ObjC++ e cabeçalhos; 65 fontes no inventário bruto | 54 Swift ativos e um adaptador Google desativado; API, ponte, catálogo, assets, parâmetros e layout conferidos estaticamente |
| Contas/comunidade/IA backend | Discovery Worker, perfis/sessões/autorização, presets, D1/R2, fila Python e subprocessos | 61 Node, 101 Python e integrações Workers locais; não houve migração/deploy remoto |
| Diagnósticos/localização/build | ExitDiagnostics/crash reporting, permissões por API, manifests, catálogos Android/Swift, verificadores e workflows | Compatibilidade e chaves verificadas; telemetria de produção, anúncios reais e matriz completa de idiomas não executados |

Detalhes das correções, fontes e limitações iOS estão em [FULL_AUDIT_IOS_2026-10-04.md](FULL_AUDIT_IOS_2026-10-04.md).

## Resultados e evidências da rodada atual

| Execução | Resultado registrado até esta versão | Evidência em `build/reference/full-audit-20261004/` |
| --- | --- | --- |
| Motor completo final 18 | **1.361 testes, 6.929.148 verificações, zero falhas; exit0, sem crash.** Execução de 01:48:31 a 01:58:10 local em cópia imutável | `core/host-all-round18.log`, `core/round18-results.txt`, `core/round18-snapshot/sha256.txt` |
| Filtros finais 18 | 11 filtros aprovados. Incluem MotionBlur 31/2216, TextOutline 2/26, Serialization 33/66821, ShutterSubframe 5/43, Effect3D 1/321, cancelamento, preview em export, Beats, I18n, Rotobrush e trim crítico. São subconjuntos/sobreposições da suíte, não casos adicionais | `core/round18-results.txt` e `core/round18-*.log` |
| Motor completo anterior ao novo blur | Rodada 9 terminou sem crash: 1.331 testes, 6.927.197 verificações e três asserções falhas. Não é aprovação da nova geração | `core/execution-ledger.txt`, `core/round9-results.txt`, `core/host-all-round9.log` |
| Regressões de jobs/import | Três casos Jobs e `ImportStability.MediaProbeCannotCommitIntoAReplacedProjectOrComposition` passaram na rodada parcial | `core/host-all-round1.log` |
| Filtros C++ da terceira rodada | Thumbnail 13 casos/149 verificações; Stability 31/4600; ProjectPackage 6/2929; Psd 7/711 passaram. Precedem as mudanças finais de memória/PSD/trim e não as validam | `core/execution-ledger.txt` e logs da terceira rodada |
| Filtros finais de quotas/persistência | ModelBudget 14 casos/141 verificações; ImportStability 3/68; ProjectAssets 2/22; Psd 11/838: zero falhas | `core/round8-ModelBudget.log`, `core/round8-ImportStability.log`, `core/round8-ProjectAssets.log`, `core/round8-Psd.log` |
| Filtros finais de render/export | HDRI pendente 1 caso/5 verificações; glTF espelhado/base64 1/11; catálogo de efeitos 1/569; trim crítico durante export 1/10: zero falhas | `core/round8-PendingCompositionAndObjectHdriKeepTheFrameIncomplete.log`, `core/round8-Scene3DMirroredNormalMapMatchesBakedReflection.log`, `core/round8-EveryCatalogEffectChangesTheProjectFrame.log`, `core/round8-CriticalMemoryTrimReleasesCachesWhileEncoderKeepsItsFrame.log` |
| Android JVM/lint | 361 testes sem falha; lint com 0 erros, 727 avisos e 28 sugestões. Gradle 18 reutiliza os resultados válidos quando as fontes Kotlin não mudaram | `android/freeze18-validation-counts.json`, `android/freeze18-x86-kotlin-package-lint.log` |
| Android build 18 | x86_64/ARM64 ligados; APK uiTest, assembleRelease e lint concluídos; Release preservado e assinatura v2/v3 verificada | `android/freeze18-artifact.json`, `android/freeze18-apk-signature.txt` e logs `freeze18-*` |
| Android instrumentado | Seleção final 18: 15/15 aprovados, sendo 14 na primeira execução e Manual aprovado na repetição integral após reparo de fixture. Consolidado de 95/95 métodos normais únicos pelas últimas execuções entre baselines 7/9/11/13/18; Heavy separado. Não somar rodadas sobrepostas nem afirmar que os 95 rodaram no último APK | `android/final-coverage-inventory.json`, `android/final-instrumentation-summary.txt` e logs por suíte |
| iOS | 14 verificadores com código 0; contratos aplicáveis de texto/símbolos/tipos passaram; sem Swift/Xcode | `ios/results.json`, `ios/source-inventory.json`, `ios/*.log` |
| iOS legado | 325 literais de medida/cor e 27 mapeamentos antigos, total 352 apontamentos, mantidos como pendência de estilo/contrato legado | `ios/ios_contract_legacy_advisory.log` |
| Python servidor | 101/101 na suíte; 10/10 regressões finais de ciclo de vida. A repetição está contida/sobreposta à suíte, não são 111 casos únicos | `backend/server-final.xml`, `backend/server-final.log`, `backend/lifecycle-final.xml`, `backend/lifecycle-final.log` |
| Node/Discovery | 61/61, zero falhas ou skips | `backend/discovery-final.log` |
| Workers local | Presets/legendas e comunidade/contas passaram com D1/R2 locais | `backend/caption-integration.log`, `backend/community-integration.log`, `backend/worker-8794.log` |
| Ferramentas | 7/7 anteriores e quatro testes novos do gerador de localização aprovados | `tool-audits.log`, `i18n/generator-tests.log` |

A suíte Python final de 101 casos passou após o último ajuste de compatibilidade de cancelamento, com zero falhas, erros ou skips.

### Falhas do produto e expectativas de teste

Publicação tardia de imports/IA, retenção indevida de memória, perda de deltas de gesto e caminhos de salvamento incorretos são problemas de implementação corrigidos. Os crashes iniciais de proxy não receberam atribuição definitiva; a corrupção de heap da terceira rodada foi introduzida pelo allocator novo desta auditoria e não explica retroativamente relatos do produto. Uma falha de asserção não recebeu automaticamente a classificação de bug funcional.

Na terceira rodada, o allocator glTF passou a prefixar um header, mas a imagem base64 ainda liberava o ponteiro interior com `std::free`, causando `0xC0000374`. A correção usa o deleter correspondente; o caso que reproduzia o crash passou na rodada 8. Builds intermediários também falharam: retornos `Errc` sem `Status`, um argumento ausente numa fixture e, no build6, escrita de `incomplete_` em helper HDRI declarado `const`. Os reparos foram recompilados no freeze7; nenhum executável antigo foi contado como validação desses builds falhos.

A execução completa da rodada 8 encontrou `0xc0000005` em `Gpu.NewToolsRotobrushRealMaskAndControls`, com stack em `Backend::upload_texture` e `Renderer::flush_uploads`. O caso isolado passava, mas a sequência reproduziu a falha: uma instância iniciada em 256 px era reutilizada após mudança para modo que pede máscara de 320 px; o upload lia além do buffer anterior. O serviço agora é recriado quando sua configuração muda e todos os uploads enfileirados validam extensão, stride e tamanho. A regressão e a suíte completa da rodada 9 passaram por esse ponto sem crash. Isso sustenta essa correção específica, sem atribuir todos os relatos de produção à mesma causa.

O primeiro teste novo de reabertura das partículas reutilizava um `LayerId` de antes do save. A serialização compacta os slots e reconstrói esses IDs, portanto as três asserções finais apontavam para uma camada ausente, embora a textura já tivesse sido carregada. A fixture agora resolve emissor e asset por tipo/nome/origem; os dois casos ProjectAssets passaram. Essa falha de fixture não exigiu outra mudança no produto depois do freeze7.

`CommunityRegressionTest` e `GizmoToolsTest` esperavam Z armazenado absoluto após ajuste/pinça/escala uniforme. O contrato atual do motor grava Z de conteúdo relativo a X, com profundidade efetiva `X × Z`; câmera e luz têm contrato absoluto. As duas interfaces já chamavam o helper compartilhado correto. Os testes foram corrigidos para verificar Z relativo preservado e profundidade efetiva proporcional; não se alterou o produto 3D para satisfazer uma expectativa antiga. A fixture iOS já seguia esse contrato e ganhou uma asserção da profundidade efetiva. As repetições Android Community/Gizmo passaram e constam do consolidado baseline9; isso não executa a fixture Swift.

As fixtures Home, GraphGestures e ManualEditingWorkflow usavam seletores/tags, coordenadas e texto PT fixo incompatíveis com a UI atual em inglês. SceneKeyframe usava um dedo para uma translação que hoje exige dois; a repetição também revelou que esperava uma chave Y redundante num arrasto que altera somente X/Z. O contrato `onlyIfChanged` preserva Y sem nova chave. A fixture passou a verificar X/Z, Y e as três chaves iniciais preservadas e foi aprovada no emulador. Stagger e os três casos GraphGestures também passaram. ManualEditingWorkflow passou no APK13 após corrigir tags/rolagem e reconhecer o fim natural somente no último quadro: salvar, reabrir e exportar 480 quadros com áudio foram verificados. A repetição 18 revelou uma corrida da fixture entre localizar Pausar e clicar, enquanto a reprodução chegava ao fim. O reparo só aceita o desaparecimento se o motor estiver parado no último quadro; qualquer interrupção antecipada continua falhando. O fluxo integral passou novamente em 86,12 s, sem alterar o produto/APK. Logs e capturas originais permanecem como evidência.

Depois de corrigido o seletor da Home, o teste de 320 dp/texto 150% revelou defeito visual real: o botão Criar estava em x426 px quando o centro era x440 px. O menu de 48 dp ocupava largura fixa, enquanto os outros três slots laterais eram flexíveis. Android e Swift agora dão ao menu o mesmo slot flexível, preservando seu alvo de 48 dp/pt e a ordem de navegação. Os dois casos Home passaram no APK freeze9, com captura visual revisada; no iOS, apenas os contratos estáticos foram conferidos, sem renderização SwiftUI.

SceneDrag revelou também defeito real no wrapper incremental: passos recebidos antes do motor publicar seu estado sobrescreviam alvos calculados a partir de uma posição antiga. Android e Swift agora acumulam deltas sobre a base compartilhada capturada no início do gesto, mantendo o último alvo enviado para o retorno à origem. As duas regressões SceneDragRate passaram no emulador da repetição final; a fixture iOS de fila atrasada, ida/volta e troca de sessão permanece sem execução.

A asserção GPU que marcou 13 efeitos como inertes usava imagem estática de 96 px sem parte do contexto exigido: LUT ausente, efeito de luz em camada de imagem, rotobrush sem máscara/modelo, metadados de grid, repetição fora da tela ou sem guia, matte invisível e parâmetros/tempo de identidade. Nove casos raster receberam entradas observáveis e adequadas; quatro casos de contexto diferente são verificados por testes dedicados de grid/cena/rotobrush. A fixture de catálogo corrigida passou com 569 verificações na rodada 8; a execução completa da rodada 8 encontrou o crash de upload Rotobrush descrito acima; a rodada 9 passou por esse ponto após a correção.

### Ensaio extremo interrompido

O cenário `HeavyStress` montou 97 camadas, 176 efeitos, 768 chaves e modelo FBX no emulador Android API35/x86_64, quatro CPUs virtuais e SwiftShader. Reprodução, busca, trim, salvamento e reabertura passaram. A exportação de 90 quadros em 720p pelo encoder de software `c2.android.avc.encoder` avançou muito lentamente: foram observadas cinco unidades H.264 completas em aproximadamente oito minutos, sem que isso certifique cinco quadros renderizados. A execução foi interrompida deliberadamente; 720p ficou incompleta e 1080p não foi iniciada. O runner escreveu “Process crashed” depois do `force-stop`, sem evidência de crash espontâneo, OOM ou ANR anterior à interrupção.

O maior PSS observado foi aproximadamente 389 MiB e RSS 518 MiB; esses números não incluem toda a memória gráfica do host. O trim relatou liberação de 189.721.680 bytes pelo motor, o que não equivale a uma queda medida de PSS. A thread de prévia ocupava cerca de uma CPU apesar de a exportação estar ativa; sua causa foi corrigida e medida em um caso menor, mas o cenário extremo completo não foi revalidado. Evidências e condições estão em `android/heavy-interruption.json`. Esse ensaio não é contado como aprovado.

## Execuções deliberadamente não realizadas e limites

- Sem Xcode, SDK Apple ou `swiftc` neste Windows: sem IPA, compilação Swift/ObjC++, VideoToolbox, simulador, teste de gestos/áudio Apple, iCloud ou iPhone. Fixtures existentes e novas não são descritas como executadas.
- Sem aparelho físico Android: emulador/GPU desktop não reproduzem memória, temperatura, drivers, interrupções e codecs de todos os celulares. Não há garantia de ausência de lag em modelos 3D arbitrariamente pesados.
- Sem deploy, migração de dados remotos, CI remoto, publicação de app ou geração/anúncio real pago. Integrações backend usam serviços locais; não comprovam disponibilidade de produção.
- A suíte completa 18 passou após corrigir as falhas das rodadas anteriores. Os logs intermediários continuam preservados: a segunda rodada misturou gerações de objetos e foi interrompida deliberadamente, sem contar como aprovação nem como novo achado do produto. Testes que passaram não certificam ausência de bugs fora dos casos executados.
- A seleção final Android 18 passou 15/15 após a repetição de uma fixture; o consolidado 95/95 cobre métodos aprovados entre gerações e não uma execução de todos os 95 no APK 18. HeavyStress continua incompleto. Release e assinatura foram verificados; identificação/hash constam acima.
- Sem teste visual exaustivo de todas as traduções, acessibilidade real em leitor de tela, cada shader, cada codec externo ou todo arquivo importável. As 352 pendências legadas iOS não são crashes comprovados nem foram ocultadas como aprovação visual.
- `AureaModel.stop()` não possui chamador no app; seu teardown síncrono e a ponte de host com ponteiro bruto dependem do ciclo de vida existente. Introduzir um novo chamador concorrente requer guarda própria e validação nativa.

Este registro fecha a implementação e validação local disponíveis. Permanecem pendentes a compilação/execução nativa iOS, testes em celulares físicos e a conclusão do ensaio extremo de desempenho. As limitações de compatibilidade de imagens e duração da análise de batidas estão explicitadas acima. O emulador exclusivo da tarefa foi encerrado após restaurar sua rede; nenhum processo Android de teste ou compilação ficou ativo (`android/freeze18-cleanup.json`).


