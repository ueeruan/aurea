# Referência do editor — implementação e validação iOS

Registro de 13/10/2026. A referência é o APK Alight Motion 5.0.280 fornecido
pelo usuário, inspecionado com REA/AAPT. Material extraído e resultados brutos
permanecem em `build/reference/alight-motion-5.0.280/`. Nenhum código, imagem,
ícone, shader ou outro asset desse APK foi incorporado ao produto.

## Implementação

- Timeline ativa: fileiras de 28pt, corpo de 23pt, margem superior de 2,5pt,
  cantos de 1,5pt, cabeçalho de 70pt com olho e miniatura, topo das fileiras
  em 42pt, relógio discreto e cabeçote central. Texto grande amplia as fileiras.
- Zoom inicial de 100pt/s; alças externas de 30pt; auto-rolagem com faixa de
  38pt e velocidade máxima de 120pt/s. Timeline da camada selecionada no painel
  e opção de mostrar todas/abrir trilhas de propriedade.
- O começo/fim real do clipe usa exclusivamente `x(start)`/`x(end)`. A alça
  esquerda termina em `x(start)`; a direita começa em `x(end)`. Foi removida
  a largura visual mínima que deslocava o fim de clipes curtos. Losangos
  conservam o eixo temporal, inclusive nas bordas e em aglomerações densas.
  Em aglomerações, fronteiras/cabeçote são desenhados antes da chave
  selecionada/arrastada; a deduplicação conserva a última ocorrência. Essa
  correção acompanha uma falha real detectada no emulador Android, em que a
  âncora inicial cobria a chave selecionada vizinha. Não há deslocamento visual
  dos tempos para separar artificialmente os losangos.
- No layout legado, a tampa branca também termina no início real do clipe.
  O tamanho do corpo e os tempos dos keyframes não incluem essa decoração.
- Resumo e trilhas expandidas respeitam `timelineVisible` vindo do motor;
  keyframes ocultos por trim não reaparecem antes do corpo do clipe.
- O menu de agrupamento oferece Agrupar, Agrupar e mascarar, Agrupar e excluir.
  `AureaEngine` encaminha a operação para `Engine::group_and_mask`. O modelo
  seleciona o novo grupo somente após resultado positivo; falha ou no-op
  preserva a seleção. A ajuda explica qual camada recorta/exclui as demais.
  Ao desagrupar, a nova recusa do motor para máscaras que afetariam camadas
  externas também recebe a mensagem localizada `eng_group_mask_external`,
  com catálogo nas sete línguas compartilhadas com Android.
- Anterior/próximo keyframe e curva recebem disponibilidade, tempos da
  composição e segmento local de `Engine::query_keyframe_navigation`.
  Controles sem operação possível ficam desabilitados. O toque no losango
  abre uma curva apenas quando o motor fornece um segmento editável.
  O próprio gráfico usa esse segmento e os vizinhos compartilhados, desabilita
  suas setas nas extremidades e exclui os keyframes ocultos por trim.
- Textos e acessibilidade compartilham as chaves Android; catálogo Swift
  regenerado por `tools/_ios_strings.py`, com português/inglês e fallback
  existente para os demais idiomas. A timeline expõe seleção e visibilidade
  das camadas ao VoiceOver.

## Auditorias executadas no Windows

- `check_api_swift.py`: zero problemas em modelo, ponte e catálogo.
- `check_symbols.py`: todos os 729 símbolos usados pela ponte confirmados.
- `check_pbxproj.py`: zero problemas; todos os arquivos referenciados existem.
- `_ios_check.py textos simbolos tipos`: zero problemas.
- `check_timeline_reference.py`: treze verificações do contrato no código
  passaram. Sem `swiftc`, a suíte que recorta e executa a matemática/hit-test
  Swift **não foi executada**. Em um Mac, o mesmo script testa modo ativo e
  legado, compacto/normal, âncora inicial x216, tampa antes do corpo, offsets
  de trim, keyframes ocultos nas trilhas expandidas, falta de espaço para a
  alça, ordem de pintura em aglomerações e clipes com largura temporal inferior
  a 40pt.
- `git diff --check`: sem erros nas alterações iOS.

A auditoria completa `_ios_check.py` ainda possui 363 ocorrências históricas
de medidas/cores literais e 27 nomes de arquivos antigos no mapa de telas.
Os 363 achados de tokens foram comparados aos arquivos do HEAD por
`git cat-file`: delta zero. `check_scope.py` também permanece com quatro
achados preexistentes: ARKit/CoreMotion nas telas de Real Camera e uma menção
ao caminho iOS em `test_text.cpp`. Esta entrega não altera esses assuntos.

## Compilação e aparelho

**Compilação nativa iOS: não executada.** Este host Windows não possui Xcode
nem Swift. As auditorias acima não validam tipos SwiftUI nem compilação ObjC++
com frameworks Apple.

**Validação em aparelho/simulador iOS: não executada.** Permanecem pendentes
gestos, VoiceOver, texto grande, portrait/landscape e comparação visual com
a referência em um iPhone/iPad. A compilação e validação Android são
registradas separadamente pelo responsável pela plataforma.
