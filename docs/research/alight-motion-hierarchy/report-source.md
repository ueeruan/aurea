# AUREA: uma hierarquia de edição mais clara

Pesquisa sobre Alight Motion e aplicação no editor AUREA

Para o criador e os usuários do AUREA | 5 de setembro de 2026

## 1. Resposta direta

O principal aprendizado é organizar a edição em torno do objeto selecionado: primeiro escolher o que será alterado; depois revelar suas ferramentas; por fim ajustar uma propriedade no tempo. A hierarquia é conceitual, não uma obrigação de atravessar quatro telas. Prévia, linha do tempo e controles precisam continuar próximos.

O próprio Alight Motion apresenta o início como adicionar, ajustar e animar. Seu guia também avisa que a aparência dos menus varia entre versões e dispositivos. Portanto, esta pesquisa usa os fluxos documentados, não uma suposta reprodução exata da interface atual. [Alight Motion Support - Quick Start Guide, atualizado em 16/06/2023](https://support.alightmotion.com/hc/en-us/articles/10536777320337-Alight-Motion-Quick-Start-Guide).

**Conclusão com limite:** os documentos e princípios de interação sustentam uma hipótese plausível de facilidade: escopo previsível, opções contextuais, estado visível e retorno seguro. Não houve experimento comparando Alight Motion e AUREA, nem prova de que um seja mais fácil para todos.

### O mapa mental

| Nível | Pergunta do editor | Responsabilidade |
|---|---|---|
| Projeto / composição | Em qual vídeo estou? | Formato, duração, pilha e saída |
| Camada ou grupo | Qual objeto vou alterar? | Seleção, organização e conteúdo |
| Categoria / propriedade | O que vou mudar? | Transformação, aparência, efeitos |
| Keyframe / intervalo | Quando e como muda? | Valor no tempo e interpolação |

Esse mapa é uma síntese de projeto do AUREA, derivada dos fluxos abaixo. Não é uma taxonomia oficialmente publicada pelo Alight Motion.

### Aplicação entregue

Ao selecionar uma camada, o AUREA agora mostra sua grade de ferramentas diretamente. No cabeçalho de propriedade, identifica projeto, camada e propriedade. O retorno fecha o contexto mais profundo antes de sair do editor. Em Transformar, botões anterior/próximo e um estado textual tornam os keyframes mais descobríveis. A capacidade existente foi preservada; não houve troca de motor, remoção de ferramentas ou geração de IPA nesta etapa.

## 2. O que a documentação realmente demonstra

### Seleção define o alcance da ação

Copiar um efeito segue camada selecionada, Efeitos, efeito individual e seu menu. Copiar a pilha usa outro menu dentro de Efeitos. A mesma palavra pode operar sobre objetos diferentes; identificar o alvo evita ambiguidade. Esse é um fluxo documentado, enquanto sua contribuição à facilidade é uma inferência de design. [Alight Motion Support - How do I copy and paste effects?, 14/03/2023](https://support.alightmotion.com/hc/en-us/articles/13725250940689-How-do-I-copy-and-paste-effects).

### Animar é editar uma propriedade no tempo

O guia distingue os keyframes da propriedade atual dos de outras propriedades. Para editar easing, pede pelo menos dois keyframes pertinentes e o indicador de tempo entre eles. A curva pertence a um intervalo; a operação de colar em todos permanece na propriedade selecionada. Isso explica por que contexto e estado temporal precisam aparecer juntos. [Alight Motion Support - Animation Easing Curves, atualizado em 14/06/2023](https://support.alightmotion.com/hc/en-us/articles/10536934703889-Animation-Easing-Curves).

### Três estruturas que não devem ser confundidas

**Grupo:** encapsula composição e pode mudar a ordem de processamento. O exemplo oficial agrupa uma camada para aplicar efeitos ao resultado de seu contorno, não apenas para arrumar a timeline. [Alight Motion Support - Displacement Map, seção Ripple / Energy Burst, atualizado em 11/11/2022](https://support.alightmotion.com/hc/en-us/articles/10537076760593-Displacement-Map).

**Parenting:** a filha acompanha transformações de um pai; a relação independe da ordem visual das camadas. O vínculo é indicado e o transform é compensado no instante atual para evitar salto. Nulos são controles invisíveis na saída. [Alight Motion Support - Layer Parenting and Null Objects, atualizado em 16/06/2023](https://support.alightmotion.com/hc/en-us/articles/10536997444369-Layer-Parenting-and-Null-Objects).

**Element:** permite reutilização. Editar o original e converter uma instância em grupo são caminhos distintos, com consequências diferentes para outras utilizações. O AUREA deve comunicar edição vinculada versus cópia local antes de ampliar esse fluxo. [Alight Motion Support - Elements: The Complete Guide, atualizado em 16/06/2023](https://support.alightmotion.com/hc/en-us/articles/10536791122449-Elements-The-Complete-Guide).

### O estado da prévia também importa

O guia de Pan/Zoom diferencia gestos com e sem seleção e descreve restauração do zoom anterior após desativar a câmera. Preservar enquadramento evita repetir ajustes de navegação. Isso não comprova retenção de todos os estados ao entrar em grupos. [Alight Motion Support - Preview Pan and Zoom, atualizado em 10/06/2025](https://support.alightmotion.com/hc/en-us/articles/10536990235409-Preview-Pan-and-Zoom).

## 3. Por que esse desenho pode facilitar o uso

### Menos opções irrelevantes, sem esconder o trabalho frequente

Revelação progressiva separa comandos principais dos especializados, mas precisa de passagens evidentes. Nielsen alerta que profundidade excessiva desorienta e que controles usados juntos não devem exigir idas e voltas entre etapas. Por isso o AUREA mostra a grade após a seleção e conserva prévia e tempo, em vez de criar um assistente linear obrigatório. [Jakob Nielsen / NNGroup - Progressive Disclosure, 03/12/2006](https://www.nngroup.com/articles/progressive-disclosure/).

### Reconhecer o estado, não decorar o caminho

Rótulos e pistas contextuais ajudam a reconhecer opções e interpretar comandos. A aplicação concreta aqui é identificar camada/propriedade e mostrar se há keyframes ou se o tempo coincide com um deles. Não se conclui que toda ação precisa de um texto permanente. [Raluca Budiu / NNGroup - Memory Recognition and Recall in User Interfaces, 15/01/2024](https://www.nngroup.com/articles/recognition-and-recall/).

### Explorar com uma saída previsível

Voltar, fechar, cancelar e desfazer têm significados diferentes. Voltar não deve saltar contextos inesperadamente; fechar uma ferramenta não significa desfazer seus ajustes. O AUREA mantém as alterações ao retornar e conserva o desfazer existente. [Maria Rosala / NNGroup - User Control and Freedom, 29/11/2020](https://www.nngroup.com/articles/user-control-and-freedom/).

### O contexto visual é parte da ferramenta

Aplicações complexas se beneficiam de acesso a detalhes sem abandonar o ambiente principal e de fluxos não lineares. Objetos visíveis, feedback incremental e ações reversíveis também caracterizam a manipulação direta. Isso fundamenta manter a prévia durante a edição; não prova uma redução específica de tempo no AUREA. [Kate Kaplan / NNGroup - 8 Design Guidelines for Complex Applications, 08/11/2020](https://www.nngroup.com/articles/complex-application-design/); [Ben Shneiderman / IEEE Computer 16(8), pp. 57-69 - Direct Manipulation, agosto de 1983](https://www.cs.umd.edu/~ben/papers/Shneiderman1983Direct.pdf).

### Gestos como atalhos, não como segredos

Apple recomenda uma forma principal descobrível de realizar ações, com gestos como alternativas. O W3C explica a dificuldade de arrastar e admite entradas ou botões equivalentes. Os novos botões de navegação entre keyframes seguem esse princípio; não significam que todo gesto do AUREA já tenha alternativa acessível. [Apple Developer - Discoverable design, WWDC21 / 2021](https://developer.apple.com/videos/play/wwdc2021/10126/); [W3C WAI - Understanding SC 2.5.7: Dragging Movements, atualizado em 10/08/2026](https://www.w3.org/WAI/WCAG22/Understanding/dragging-movements.html).

## 4. O que mudou no AUREA

| Antes, na implementação inspecionada | Agora |
|---|---|
| Selecionar camada, abrir menu, escolher categoria | Selecionar e tocar diretamente na categoria visível |
| Cabeçalho do painel mostrava apenas a categoria | Projeto e camada permanecem identificados; Transformar e Curva nomeiam a propriedade |
| Topo já limpava seleção, mas a rota não interceptava o Voltar do sistema | Retorno compartilhado: folha, subpainel, ferramentas, seleção, projeto |
| Recentes globais guardavam callbacks sem alvo | Mudança de camada/projeto limpa atalhos e fecha a folha anterior |
| Navegação dependia dos diamantes na timeline | Transformar também oferece anterior/próximo e estado textual |
| Algumas dimensões fixas ultrapassavam a tela pequena | Chips flexíveis e área rolável nos controles de transformação |

A redução de três para dois toques é uma contagem do caminho específico camada não selecionada -> Transformar, não uma medição de produtividade. Mais ações preserva as utilidades do menu completo; Cena 3D, Clonar e demais categorias reutilizam os comandos existentes e o contrato de visibilidade por tipo.

O indicador de animação considera ambos os eixos de escala e inclinação. A navegação soma o início da camada ao tempo local do keyframe, respeita a propriedade escolhida e pausa a reprodução antes de buscar o instante. Criar/remover keyframe e abrir curva continuam nos controles existentes, agora com dicas textuais.

Os novos botões de navegação têm área de 48 por 48 unidades lógicas. Esse dimensionamento se alinha à intenção de alvos amplos do guia Android; não constitui auditoria de conformidade de acessibilidade em iOS, Android ou web. [Google / Android Developers - Make apps more accessible (Views), atualizado em 21/04/2026](https://developer.android.com/guide/topics/ui/accessibility/views/apps-views?hl=en).

### Verificação técnica

Sete testes novos exercitam o editor real e o componente temporal: seleção e retorno em 375 x 667 e 430 x 932; folhas persistentes; limpeza de recentes; troca de alvo; seleção múltipla; seis propriedades de transformação; entradas diretas de 3D/Grid; tempo local e keyframes exclusivos do eixo Y. Capturas com fontes carregadas foram inspecionadas nas duas dimensões. Os painéis antigos também foram exercitados em testes de regressão.

Resultado final: 1.121 testes passaram na suíte completa e a análise estática não encontrou problemas. A verificação ocorreu no ambiente Flutter local; não foi produzido um IPA nem realizada validação em um iPhone físico.

## 5. Limites e próximos passos

### O que esta pesquisa não afirma

Não foi feita uma inspeção de uma instalação atual do Alight Motion. Tutoriais incorporados não puderam ser reproduzidos; documentação textual foi a evidência principal. Fontes de ajuda variam de 2022 a 2025 e foram consultadas em 05/09/2026. Não se confirmou o comportamento exato de Voltar entre grupos, nem a ordem atual completa de botões. A regra de retorno implementada é uma decisão do AUREA.

Também não houve estudo com usuários, telemetria de frequência, medição comparativa de desempenho ou teste em iPhone físico. Familiaridade prévia, tutoriais, tamanho do projeto e capacidade do aparelho podem afetar a experiência; esta entrega não isolou esses fatores. Testes de widget demonstram comportamentos programados, não facilidade percebida.

### Escopo preservado

Projetos, cenas 3D e recursos anteriores não foram substituídos. Grupos/precomposições, parenting, máscaras, Elements, importação e renderização não receberam uma reformulação completa nesta etapa. O painel de curvas mantém seu modelo existente, inclusive suas particularidades de eixos; esta entrega não representa paridade integral com Alight Motion.

### Próxima validação recomendada

Convidar iniciantes e editores experientes a realizar as mesmas tarefas em projetos equivalentes: adicionar e posicionar texto; criar dois keyframes e ajustar easing; aplicar/copiar efeito; agrupar e distinguir parenting; voltar ao projeto sem perder o ponto de trabalho.

Medir conclusão sem ajuda, erros de alvo, retornos indevidos, pedidos de orientação e tempo por tarefa. Alternar a ordem das interfaces para reduzir efeito de aprendizagem. Usar resultados para revisar prioridade de categorias, não para declarar sucesso com base em aparência.

Como próxima frente de produto, vale explicitar trilhas por eixo no editor de curvas e revisar navegação de composições aninhadas. São recomendações, não funcionalidades prometidas como concluídas.

### Critério de encerramento da pesquisa

A busca cobriu fluxos oficiais, princípios de interação, contrapontos e acessibilidade. Duas trilhas independentes foram reconciliadas com a inspeção do código, seguida de checagem das fontes mais importantes. A pesquisa foi encerrada quando as decisões desta implementação tinham apoio suficiente e as lacunas restantes estavam delimitadas; repetir buscas amplas não mudaria a escolha técnica desta etapa.
