# Estabilidade de exportação e preview — iOS — 2026-10-04

## Implementação

- `engine/platform/ios/bridge/IOSVideoDecoder.mm`: o export limita créditos de
  vídeo pelo tamanho NV12 do quadro (alvo de 32 MiB, entre 2 e 12 quadros), limita
  o pool de pixel buffers a esses créditos mais duas referências e configura o
  atraso máximo do VideoToolbox. Isso substitui os 64 créditos fixos, que poderiam
  reter aproximadamente 760 MiB somente em quadros NV12 4K.
- Falhas ao criar o pool, receber planos inválidos ou copiar buffers deixam de
  prosseguir como export válido. O erro original do gravador é preservado;
  falta de memória e armazenamento cheio recebem categorias próprias. Abertura
  malsucedida encerra a sessão e remove o arquivo parcial.
- O cancelamento compartilhado chega ao sink iOS e é consultado nas esperas de
  capacidade, drenagem e finalização. A finalização consulta a condição a cada
  100 ms em vez de aguardar 60 segundos de uma vez. O cancelamento invalida o
  encoder sem solicitar a codificação dos quadros restantes. Chamadas síncronas
  internas dos frameworks Apple ainda dependem do retorno desses frameworks.
- `AureaModel.swift` serializa suspensão e retomada fora da thread principal. O
  motor compartilhado cancela e drena a exportação antes de soltar os decoders.
  O estado transitório `inactive` apenas pausa a reprodução; a suspensão ocorre
  em `background`. A capa do projeto é adiada enquanto o export usa o renderer.
- A ponte ObjC++ e o Swift consomem o estado de buffering do motor compartilhado.
  `EditorView.swift` apresenta preparação, quadros prontos ou limite atingido,
  com texto acessível e identificador `preview.buffer.status`. O estado de
  reprodução permite cancelar a preparação pelo botão de pausa.
- A régua da timeline mostra uma linha azul `#4DA3FF` de 3 pt nos intervalos
  realmente renderizados. A ponte copia até 30 pares `[início, fim exclusivo)`
  do snapshot do motor, sem esperar a GPU. O Swift consulta os intervalos mesmo
  quando a contagem não muda, preserva lacunas e aplica o mesmo eixo/zoom dos
  clipes. A linha acompanha descarte e invalidação do cache; a acessibilidade
  informa o total com `timeline.preview.buffer`.
- O aviso sobre modelos 3D extremamente pesados aparece na entrada de modelos e
  no diálogo de otimização, em português e inglês, usando as mesmas mensagens
  do Android. O catálogo Swift foi regenerado a partir dos recursos Android.
- O painel de transformação explica quando uma expressão controla o valor. O
  gesto pausa a reprodução e mantém a expressão existente; ela pode ser editada
  ou desativada pelo controle `=`. O vídeo enviado motivou essa investigação,
  mas não contém o projeto original necessário para comprovar sua expressão.
- A atualização do detalhe da camada usa o playhead nativo antes de aplicar a
  posição otimista da timeline. Assim, a chegada de um seek enfileirado atualiza
  os valores animados do painel mesmo quando o cabeçote visual já estava no
  destino. A instrumentação Android revelou o caso; o mesmo padrão existia no
  Swift e foi corrigido por paridade.

## Motor compartilhado e revisão

`SceneRenderer` agora informa recursos 3D incompletos: uploads de geometria e
materiais, pipelines, ambiente e pós-processamento. Falhas transitórias de
memória em modelos têm nova tentativa com intervalo de 500 ms. O renderer de
profundidade também sinaliza mapa pendente ou textura indisponível. O cache de
preview pode assim recusar imagens parciais e tentar novamente.

A revisão do cache verificou chave por revisão/composição/resolução/qualidade,
separação do export final, uso de quadros compostos sem repetir a preparação,
limite de memória, consulta de fence antes de reutilizar uma posição e
invalidação por edição/trim. Os pontos encontrados foram corrigidos no motor:
admissão de uploads/efeitos incompletos, propagação da condição 3D para novas
tentativas e espera da thread quando a superfície desaparece durante o buffering.
Foi adicionado um teste com falha injetada no upload da imagem: o quadro não pode
entrar no cache; os mesmos bytes devem ser reenviados com sucesso no render
seguinte, sem repetir a preparação; só então a composição pode ser reutilizada.
A validação executada do motor consta no relatório geral da tarefa.

## Verificação estática executada neste host

| Verificação | Resultado |
| --- | --- |
| `verify/check_api_swift.py` | 0 problemas; 907 chaves usadas, 3879 no catálogo |
| `verify/check_symbols.py` | 622 símbolos confirmados, 0 não encontrados |
| `verify/check_pbxproj.py` | 0 problemas; 67 referências de arquivo válidas |
| `verify/check_scope.py` | 0 problemas; 45 arquivos Swift, imports Apple |
| `verify/check_editor_layout.py` | Regra estática passou; casos Swift exigem macOS |
| `verify/check_preview_buffer.py` | Contrato de fonte passou; geometria Swift exige macOS |
| `git diff --check` nos arquivos desta frente | Passou |

As verificações ficam em `engine/platform/ios/verify/`. Elas conferem nomes,
recursos, estrutura e regras estáticas; não compilam Swift nem executam UIKit,
Metal, VideoToolbox ou AVAssetWriter.

Última rodada repetida após a integração de cancelamento, a correção do detalhe
após seek e a linha azul: os seis scripts terminaram com código 0. Logs individuais e
resumo estruturado: `build/reference/stability-20261004/ios/`.

## Testes adicionados e limites da evidência

`AureaStageGestureUITests.swift` ganhou casos que arrastam o controle de posição
com expressão ativa, verificam o aviso e abrem o editor da expressão, além de
verificar o status acessível de preview e que a pausa não reinicia sozinha. Outro
caso arrasta a timeline de uma camada animada e compara o detalhe publicado para
o painel com o valor avaliado no motor após o seek. O teste de preview também
verifica intervalos válidos e o identificador acessível da linha azul.
Esses casos são código de teste nativo e **não foram executados neste host**.

Compilação iOS: **não realizada**. O host Windows não dispõe de Xcode, SDK iOS ou
Swift. Não foi iniciada compilação remota. Validação em simulador/iPhone:
**não realizada**. Os testes compartilhados e auditorias acima não substituem
essa execução.

A verificação nativa pendente deve cobrir export H.264/HEVC com e sem áudio em
1080p e 4K, cancelamento durante escrita/finalização, retorno do segundo plano,
pressão de memória e preview de texto/efeitos/modelos 3D. Medir pico de memória,
responsividade e reprodução do MP4 resultante em aparelho. Não há evidência
para prometer ausência absoluta de crashes ou lag em qualquer projeto.

## Referências de contrato

- Apple: [MaxFrameDelayCount](https://developer.apple.com/documentation/videotoolbox/kvtcompressionpropertykey_maxframedelaycount).
- Apple: [limite de alocação do pool de pixel buffers](https://developer.apple.com/documentation/corevideo/kcvpixelbufferpoolallocationthresholdkey).

Resultados brutos de inspeção devem permanecer em `build/reference/`.
