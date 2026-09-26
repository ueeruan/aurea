# Editor e compositor profissional — auditoria e execução

Pedido de 26/09/2026. Documento de trabalho; não é uma declaração de que os 60
itens do pedido já foram concluídos. Referência inicial: commit `42a3ff29`.

## Base que deve permanecer

O usuário relatou que o aplicativo parou de fechar no seu iPhone. A versão
instalada não foi confirmada. O build 2120 passou 14 testes nativos no simulador
iOS (399,565 s, zero falhas), incluindo reprodução, seek e gestos. As cinco
medições RAW não registraram underrun de áudio. FPS variou de 12,2 a 33,75 nesta
execução; o teste não exige uma taxa mínima. Não chamar esse resultado de
aprovação de fluidez em aparelhos reais. Relatórios em `build/releases/2120/`.

Manter workers de decodificação, áudio como relógio, limites de buffers em voo,
cache limitado por bytes e frames e cancelamento por geração. Nenhuma busca de
comandos deve decodificar mídia ou renderizar miniaturas de todos os resultados.

## Classificação inicial do repositório

| Área | Estado observado | Evidência / consequência |
| --- | --- | --- |
| Playback | Funcional, aceitação de desempenho incompleta | Testes nativos acima; variação entre codecs e execuções |
| Trim, split, marcadores, undo | Existem no core compartilhado | `Engine.cpp`, `test_edit.cpp`, `test_timeline.cpp` |
| Edição magnética iOS | Quebrada na integração | UI mantinha flag local após undo/load; exclusão ignorava o modo |
| Bloqueio de camadas | Quebrado em operações indiretas | Ripple podia mover/excluir camadas bloqueadas |
| Roll / slip / slide | Lacuna | Não há operações transacionais dedicadas na API auditada |
| Descoberta de ferramentas | Precisa melhorar | Busca de camadas/efeitos/presets isoladas; nenhuma busca universal |
| Favoritos | Funciona parcialmente | Efeitos e presets já persistem; faltam ações/ferramentas |
| Pré-composição / parenting | Implementado e editável | Testes de cópia, vínculo, nesting e save/reopen; preservar |
| Curvas | Precisa melhorar | Easing touch, presets, copy/paste existem; não equivalem a value/speed graph completo |
| Máscaras, mattes, blend | Implementado, aceitação por workflow pendente | Core de composição e painéis existentes; não criar outro compositor |
| Texto, vetor, animação por caractere | Implementado parcialmente | `LayerPanels.swift`, `AnimationTools.swift`, testes text/vector; auditar modificadores |
| Tracking / 3D / partículas | Implementação existente, aceitação incompleta | Não basta a presença de GLB, câmera e parâmetros para aprovar integração real |
| Legendas | Infraestrutura existente, fluxo remoto pendente de verificação | Alterações do Worker já estavam na árvore; não sobrescrever |
| Placeholders | Sem classificação geral comprovada | Auditar por execução antes de remover funcionalidades |

## Pesquisa de workflows e decisão de interação

Fontes primárias consultadas em 26/09/2026:

- [Premiere — slip](https://helpx.adobe.com/premiere/desktop/edit-projects/trim-clips/perform-slip-edits.html): trocar o conteúdo de um clipe mantendo posição e duração é uma operação própria. Não simular isso movendo a camada inteira.
- [Resolve — Cut](https://www.blackmagicdesign.com/products/davinciresolve/cut): ferramentas próximas da tarefa reduzem navegação entre montagem e ajuste. No Aurea, manter os painéis contextuais e criar um caminho direto entre eles.
- [Alight Motion — curvas](https://support.alightmotion.com/hc/en-us/articles/10536934703889-Animation-Easing-Curves): o ajuste entre keyframes precisa de contexto temporal; não exibir uma curva decorativa desconectada da avaliação do core.
- [Alight Motion — parenting](https://support.alightmotion.com/hc/en-us/articles/10536997444369-Layer-Parenting-and-Null-Objects): controlador compartilhado evita editar cada camada do rig separadamente.
- [CapCut — velocity](https://www.capcut.com/resource/how-to-do-velocity-on-capcut): acesso direto ao remapeamento e pontos editáveis devem coexistir com presets.
- [Node Video — guia](https://nodevideo.com/guide/): 3D e composição precisam permanecer no fluxo de edição do projeto.

Pesquisa documental, não sessões observadas com usuários nem vídeos assistidos.
Não copiar código, assets, nomes proprietários ou a disposição visual dessas ferramentas.

## Prioridade 1 em execução

1. O modo magnético iOS passa a ler a composição nativa no refresh, inclusive
   depois de undo, carregamento e navegação entre pré-composições.
2. Exclusão iOS centralizada e compatível com o modo magnético; Android filtra
   camadas bloqueadas. O core rejeita delete/split/time-range bloqueados antes
   de gravar undo. Ripple preserva camadas bloqueadas.
3. Fechar espaços mantém lacunas quando uma camada bloqueada precisaria se
   mover. Repetir a operação não deve empilhar deslocamentos indesejados.
4. Busca universal com catálogo de ações compartilhado em JSON, catálogos
   existentes de efeitos e presets, termos sem acento, contexto e favoritos.
   Efeitos usam os favoritos existentes. Presets usam a mesma biblioteca e
   abrem na aba e no resultado encontrados, preservando a decisão de aplicar.
5. A busca ocupa o lugar do contador redundante no cabeçalho do projeto; o
   timecode principal continua no transporte. Tamanhos dos painéis preservados.

### Critérios de aceitação desta etapa

- Desfazer alternância de modo atualiza indicador e comportamento nos dois OS.
- Camada bloqueada não some, não é aparada e não se desloca indiretamente.
- Busca não modifica nada até selecionar um resultado habilitado.
- Busca por `glow`, `camera`, `velocidade`, `preset` e palavras sem acento encontra
  ferramentas reais; seleção inválida explica o requisito.
- Favoritar, fechar, reabrir e executar funciona; desfazer reverte a edição.
- Projeto de teste continua abrindo e tocando depois das operações.

Medições de toques devem ser registradas após uso real, distinguindo toque,
digitação e rolagem. Não declarar números de redução baseados só no desenho.

### Verificação local do primeiro incremento (2121)

- Compilação Android x86_64 concluída.
- Emulador API 35: favorito persiste ao reabrir; criar texto pelo favorito;
  buscar `glow`; aplicar Brilho profundo; desfazer remove o efeito; buscar
  `velocity` numa camada de texto explica que é preciso selecionar vídeo/áudio.
  Evidências: `build/pro-editor-ui/`.
- Caminho observado para efeito por busca: abrir busca, tocar campo, digitar,
  tocar resultado. São três toques + digitação; o fluxo anterior de catálogo
  passa por pilha → adicionar → buscar → detalhe → aplicar (cinco toques +
  digitação, sem contar rolagem). Favorito na busca dispensa a digitação.
- Core, execuções por filtro: Edit 21 testes/437 verificações;
  Timeline 22/1.827; Clipboard 3/48; Precomp 5/75. Zero falhas.
- Auditorias de API Swift, tipos, projeto Xcode e recursos compartilhados
  passaram. Não substituem compilação Swift e testes no simulador.
- Dois novos testes nativos iOS exercitam busca sem acento, modo magnético +
  undo, aplicação de efeito + undo e favoritos; execução ainda pendente.

## Fila de aceitação do pedido completo

Seguir a ordem pedida: timeline → keyframes/graphs/motion → compositor/mattes/
precomps → texto/vetor → tracking → 3D → partículas → automação/presets.
O término desta primeira etapa não conclui essa fila.

Projetos A (Reel), B (motion), C (edição longa), D (3D) e E (anime) ainda precisam
de montagem e exportação ponta a ponta. Testes unitários e catálogos presentes
não substituem esses cinco projetos. Samsung físico, iPhone físico e a falha
Unity/WebView não podem ser declarados retestados a partir de emulador.
