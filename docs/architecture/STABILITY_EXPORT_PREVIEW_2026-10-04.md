# Estabilidade de exportação e preview — 04/10/2026

## Escopo e evidência de entrada

Solicitação: corrigir falhas de exportação, melhorar a estabilidade, preparar quadros de preview antes de reproduzir e avisar sobre modelos 3D extremamente pesados nas interfaces Android e iOS.

A gravação `Blurrr_1791071171512.mp4` tem 10,75 segundos e mostra o painel Transform: arrastar o controle de posição não altera o texto nem X 540 / Y 515. Ela não contém uma mensagem de erro de exportação. O indicador de expressão aparece destacado; isso sugere uma propriedade controlada por expressão, mas a gravação não revela a fórmula ou o projeto. A correção preserva expressões e informa quando elas impedem uma alteração manual. Testes separados verificam que texto animado sem expressão continua sendo movido corretamente.

As mudanças já existentes no checkout foram preservadas. Esta entrega não é uma certificação de ausência de todos os crashes ou problemas de desempenho.

## Implementação compartilhada

- Corrigida a vida útil das mensagens de erro do codificador, incluindo falha na abertura e término da exportação.
- Falha ao mapear os planos de leitura da GPU interrompe a exportação com erro, em vez de entregar memória inválida ao codificador.
- Parâmetros de exportação inválidos e estouros de dimensão/duração são recusados.
- Durante a troca de clipes, decodificadores antigos são liberados também enquanto se aguarda a próxima mídia. Isso permite progredir em dispositivos com apenas um decodificador disponível.
- Uma camada de vídeo completamente indisponível deixa de produzir um MP4 incompleto com indicação de sucesso.
- Falhas temporárias de recursos gráficos refazem o mesmo quadro antes de entregá-lo ao codificador. A tentativa tem prazo e cancelamento; falha persistente termina com erro, sem declarar um vídeo com camadas omitidas como concluído.
- Suspensão cancela e aguarda o trabalho nativo antes de fechar decodificadores. A criação e troca de sessões de exportação são serializadas; cancelamento e consulta de progresso têm proteção curta separada.
- Edições manuais com Auto-Key recusam propriedades controladas por expressão antes de alterar histórico ou keyframes. Editar explicitamente as chaves subjacentes continua permitido.

## Buffer de preview

O motor guarda a composição já renderizada em texturas RGBA16F, incluindo texto, efeitos e cena 3D. Um acerto no cache reutiliza esses pixels e executa apenas a saída para a tela; exportação final não utiliza o cache de preview.

Ao iniciar a reprodução, prepara até meio segundo, limitado a 30 quadros e à memória disponível para o cache. A reserva usa um quarto do orçamento de `RenderedFrames`, com teto absoluto de 48 MiB. Se um quadro não couber, o app continua sem essa preparação. A espera de preparação é limitada a três segundos; pause, scrub, edição, troca de projeto e exportação cancelam a preparação. O playhead e o áudio ficam parados durante essa etapa, e a imagem anterior permanece na tela.

A chave considera composição, revisão, resolução, qualidade e observador 3D. Alterações invalidam o cache. Pressão de memória e calor forte desativam a preparação. O sistema libera o cache antes de alocar a exportação e sob solicitação de memória do sistema. Slots ocupados pela GPU são consultados sem espera bloqueante e não causam crescimento acima da capacidade lógica.

Quadros com mídia aproximada/ausente, upload pendente, efeitos ignorados por falta de recursos ou cena 3D incompleta não entram como quadros prontos. Uploads que falham podem ser refeitos; resultados intermediários derivados desses uploads são invalidados. A thread de render espera quando não há superfície, inclusive se havia uma reprodução pendente.

O campo reservado de quatro bytes no status da ponte foi utilizado para progresso e estado do buffer; o contrato de 256 bytes foi preservado. Android e iOS exibem preparação, quadros guardados e limitação, com textos PT/EN e semântica de acessibilidade. Trata-se de uma janela limitada para dispositivos móveis, não de cache em disco da timeline inteira.

A régua da timeline nas duas interfaces recebe uma linha azul (`#4DA3FF`, 3 dp/pt) sobre os intervalos de quadros realmente guardados. A faixa usa a mesma projeção temporal dos clipes e acompanha zoom e deslocamento. Lacunas continuam vazias; edição, liberação de memória e substituição de quadros atualizam os trechos. O motor publica uma cópia pequena dos intervalos, e a consulta da interface não aguarda a GPU. As interfaces consultam os intervalos mesmo quando a quantidade de quadros permanece igual, para representar corretamente a substituição do cache durante a reprodução.

## Interfaces e plataformas

Android: exceções de exportação têm estado terminal recuperável; captura da capa ocorre fora da thread principal; exportação e captura mantêm o motor vivo até liberarem seu acesso nativo. Publicação na galeria é tratada como transação e arquivos vazios são recusados. Compartilhamento em Android 8–9 usa URI de conteúdo, evitando expor URI de arquivo.

O MediaCodec valida estado e buffers, limita a fila inicial e permite cancelamento durante a espera. O teste vertical em emulador reproduziu um timeout de cinco segundos; os novos prazos consideram progresso e diferenciam codificador de software e hardware.

iOS: fila de quadros e pool do codificador são limitados pela dimensão, com validação de planos, strides e travamento do buffer. Erros detalhados atravessam a ponte. Estados transitórios de inatividade não suspendem a exportação; suspensão real ocorre em fila serial fora da thread principal. Ver [evidência específica do iOS](STABILITY_IOS_2026-10-04.md).

As duas interfaces avisam que modelos 3D extremamente pesados podem causar consumo excessivo de memória, aquecimento, lentidão, travamentos ou fechamento do app, com uso por conta e risco e opção de otimização.

As duas interfaces atualizam o detalhe da camada quando o motor confirma o novo instante após um seek. Antes, a posição otimista da timeline podia mascarar essa confirmação e deixar valores antigos no painel Transform.

## Validação

Implementação, compilação, emulador e aparelho físico são evidências distintas. Os relatórios brutos desta investigação ficam em `build/reference/stability-20261004/`.

### Motor compartilhado no Windows

Compilação Release do motor e dos testes concluída. A execução usou Vulkan em uma NVIDIA GeForce RTX 3050 quando o caso exige GPU, além dos backends de teste para falhas controladas.

| Filtro | Resultado final | Evidência bruta |
| --- | --- | --- |
| `PreviewBuffer` | 9 casos, 268 verificações, zero falhas; inclui intervalos da linha azul e consulta durante render bloqueado | `host-PreviewBuffer-blue-line.log` |
| `Export` | 65 casos registrados, 17.727 verificações, zero falhas; 3 benchmarks opcionais não executados | `host-Export-final.log` |
| `Stability` | 29 casos, 4.557 verificações, zero falhas, incluindo os dois casos `TransformStability` | `host-Stability-verified.log` |
| `Renderer` | 4 casos, 68 verificações, zero falhas | `host-Renderer-final.log` |

Os filtros também selecionam nomes de casos, portanto há sobreposição; os números não devem ser somados como casos únicos. A primeira execução do novo caso de arrasto lia o detalhe antes de drenar a fila de comandos: o teste foi corrigido para processar o quadro e a repetição passou. A primeira execução de upload usava objeto anterior à edição; o binário final recompilado passou a recuperação de upload.

### Codificador Android no emulador

Probe nativo compilado e executado em Android API 35, x86_64, com codificadores de software `c2.android`: H.264 848×480 com AAC, HEVC 848×480 com AAC e H.264 1080×1920 sem áudio passaram. Verificou igualdade dos bytes e timestamps de 76, 76 e 30 pacotes no remux, além de entradas inválidas, cancelamento e reutilização. O primeiro teste vertical reproduziu timeout; `probe-emulator-h264-portrait-retry.log` registra a repetição corrigida.

As primeiras capturas de interface foram invalidadas por uma janela de ANR do Pixel Launcher sobre o aplicativo. Na repetição sem essa janela, a única falha visual estava no próprio teste: o seek 60 de um clipe de 60 quadros ficava após seu fim exclusivo. A inspeção `motion-fixture-inspection.log` confirma 60 quadros a 30 fps; o caso foi corrigido para verificar o último quadro válido, 59. Não se alterou o comportamento do motor fora da duração da camada para mascarar esse resultado.

Nove cenários distintos de interface passaram entre `android-instrumentation-final.log` e `android-preview-retest.log`: arrasto de texto animado, explicação de expressão, preparação/pausa do buffer, aviso 3D, URI de conteúdo, cancelar/repetir/publicar MP4, pixels exportados após reabrir projeto, contorno de texto e reprodução/seek de vídeo. Onze testes JVM focados também passaram.

Após adicionar a linha azul, a compilação Android x86_64 passou e os três cenários de preview foram repetidos sem falhas (`android-blue-line-instrumentation.log`). A instrumentação verificou os pixels azuis reais na régua, intervalos ordenados sem sobreposição, invalidação após edição e pausa durante preparação. A captura `screenshots-final/preview-buffer-blue-line.png` foi inspecionada visualmente: a linha corresponde ao trecho guardado, com a imagem do palco visível. O estado dos intervalos fica separado da geometria do palco para que a faixa não provoque recomposição desnecessária da área de preview.

### APK Android entregue

Compilação Release arm64-v8a final passou (`android-blue-line-arm64-release.log`). Arquivo: `build/android/app/outputs/apk/release/app-release.apk`, 60.238.703 bytes. Pacote `com.aurea.aurea`, versão existente preservada `2139` / `2.0.0-beta2`. Assinaturas v2/v3 verificadas; certificado corresponde à identidade preservada do projeto. SHA-256: `ba8844fdcb75329de19eea35da56b8926e1af2787c3fc8b892e7cc44148dc8a8`.

O APK inclui as correções de exportação, transformação, buffering e linha azul. O emulador usado nesta tarefa foi encerrado após a validação; o buffer de registros de crash do aplicativo estava vazio. Isso registra os cenários executados, sem certificar ausência de falhas em qualquer projeto ou aparelho. `diff --check` passou ao final.

### iOS e aparelhos físicos

Implementação iOS concluída no código, incluindo a linha azul. As seis auditorias estáticas passaram, incluindo 622 símbolos da ponte confirmados. Os casos de geometria Swift, compilação iOS e execução em simulador/iPhone não foram realizados: este host Windows não dispõe de Xcode/SDK iOS. Nenhum aparelho físico Android ou iPhone foi validado nesta entrega. Os testes de GPU no computador e do emulador Android não comprovam comportamento térmico ou memória em um celular real.

## Referências de comportamento

- [Adobe: preview e preparação antes da reprodução](https://helpx.adobe.com/after-effects/desktop/view-and-preview/preview-video-and-audio/previewing.html).
- [Android: estados e buffers do MediaCodec](https://developer.android.com/reference/android/media/MediaCodec).
- [Android: compartilhamento com FileProvider](https://developer.android.com/reference/androidx/core/content/FileProvider).
- [Apple: limite de atraso de frames no VideoToolbox](https://developer.apple.com/documentation/videotoolbox/kvtcompressionpropertykey_maxframedelaycount).
- [Apple: limite de alocação do pool de pixels](https://developer.apple.com/documentation/corevideo/kcvpixelbufferpoolallocationthresholdkey).

As referências servem apenas para especificar comportamento. Nenhum código, shader ou asset de outro aplicativo foi incorporado.
