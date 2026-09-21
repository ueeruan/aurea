# 02 — Casca do editor, barra do topo, prévia, transporte, barras/menus e palco

Inventário para portar a UI do editor do Aurea (Flutter) para Jetpack Compose.
Fonte: `C:\Users\SnyX\Documents\Projetos - Claude\Aurea` (somente leitura). Caminhos
relativos abaixo partem de `Aurea/lib/src/features/editor/presentation/` salvo quando
indicado. Números em dp (texto em sp). Aparelho de referência dos prints:
1080×2400 px, densidade 2,625 → **411,43 × 914,29 dp**; barra de status 128 px =
**48,76 dp**; barra de navegação (gestos) 63 px = **24,0 dp**; corpo útil (SafeArea)
= 2209 px = **841,5 dp**.

---

## 0. LEIA PRIMEIRO — existem DUAS UIs, e os prints mostram a mais antiga

| | **UI "A.01" (a dos prints)** | **UI "nova" (HEAD, os arquivos pedidos)** |
|---|---|---|
| Onde está | Só no git: release `aba36bb` ("release: Beta A.01", 19/09). Arquivos `shell/cromo_editor.dart`, `shell/transport_bar.dart`, `context/context_sheet.dart`, `context/layer_header.dart`, `am/layer_menu.dart`, `widgets/add_layer_sheet.dart`, `editor_screen.dart` (1594 linhas). Apagados em `75b0426` (21/09). | `ui/shell/*`, `ui/toolbar/*`, `ui/palco/*` + `core/ds/*` (commits `58121bf`…`959d725`, 21/09). |
| Barra do topo | **Muda com a seleção**: projeto (sair · título · tempo · ⋮ · ⚙ · exportar) / **camada (‹ · ícone do tipo · nome · duplicar · lixeira · ⋮)** / lote (roxo). 44 dp. | **Fixa**: ‹ · nome do projeto · ⋯ · ⚙ · exportar. 42 dp. Não mostra a camada. |
| Prévia | Chip "Full" (resolução), faixa de 8 dp com botão de tela cheia, coluna de visualização (olho), indicador de zoom. | Chip "100%" (mesmo chip, rótulo numérico). Sem faixa de 8 dp, sem coluna, sem indicador de zoom. |
| Transporte | ↶ ↷ · \|◀ ▶ ▶\| · **copiar/colar · olho · tela cheia** | ↶ ↷ (✂ com camada) · \|◀ ▶ ▶\| · **tempo "0:00.00 / 0:05.00" · olho (menu)** |
| Base | Painel contextual (ContextSheet) com cabeçalho "‹ Título" 44 dp; doca de ferramentas em grade 3 colunas; "+" 52 dp com anel. | Barra contextual de 57 dp (ícone 24 + rótulo 10) + "+" 73 dp preenchido; painel de 200 dp substitui a barra. |

`t2.png` = A.01 com camada de imagem "home6" selecionada e o painel **Efeitos** aberto.
`tela96b.png` = A.01 com a galeria de efeitos (folha modal) aberta. `t1/t3/tela96` são
Home/splash (fora desta área).

**Evidência** (medida nos pixels e conferida com o código de `aba36bb`): topo 44 dp
(128–243 px), prévia 341,2 dp, faixa 8 dp, transporte 46 dp, timeline 90 dp, painel
312,3 dp — exatamente o que `EditorLayoutMetrics.solve` do A.01 calcula para este
aparelho (ver §1.3). Os botões do topo e do transporte caem nos centros previstos pelo
código A.01 com erro < 1 dp.

**Decisão pendente do dono (não técnica):** qual casca portar. Os commits de 21/09 falam
em "UI aprovada" referindo-se à nova; os prints (anteriores, arquivos de 20/09 04:12)
mostram a A.01. Este documento especifica **as duas**: a HEAD em profundidade (§1–§10) e,
em cada região visível nos prints, um bloco **"Variante A.01 (print)"** com os números
exatos. Paleta, tipografia, painéis de propriedade e gestos do palco são praticamente
iguais nas duas.

---

## 1. Layout geral

### 1.1 Tokens (fonte: `core/ds/tokens.dart`, `core/ui/am_colors.dart`, `core/theme/aurea_paleta.dart`, `aurea_colors.dart`)

**Cores — tema padrão "Aurea"** (o editor sempre usa paleta escura; no tema Light o
editor usa a paleta Aurea — `AureaPaleta.editor`). Seis temas trocam estes valores
(`aurea, aureaDark, midnight, oled, light, graphite`); tabela completa em
`core/theme/aurea_paleta.dart:216-415`.

| Papel DS (`AureaCores.*`) | AmColors | Hex (Aurea) | Uso |
|---|---|---|---|
| `palco` | `bg` = stage | `#0A0E13` | véu de folhas/menus (com alfa), fundo da timeline no A.01 |
| `cromo` | `topBar` = background | `#0F141A` | fundo do Scaffold, barra do topo, transporte, fundo em volta da composição |
| `painel` | `panelHigh` = surface | `#151C24` | barra contextual, painéis, folhas, faixa de 8 dp (A.01) |
| `elevado` | `pilula` = panel | `#1B2530` | menu flutuante (AureaMenu) |
| `campo` | `chip` | `#212D3A` | chip, bloco da folha de adicionar, botão secundário |
| `campoAlto` | lerp(chip, texto, .08) | ≈`#2F3A46` | item de menu apertado, trilho do switch desligado |
| `texto` | textoPrincipal | `#F7F9FB` | ícones/texto |
| `textoSecundario` | textSecondary | `#AAB6C3` | rótulos, ícone desabilitado (×0,4–0,45 alfa) |
| `destaque` | accent = brandLight | `#6FAED9` | estado ligado, aba ativa, exportar (HEAD), contorno de seleção |
| `destaqueApagado` | accentDim | `#1D3A55` | fundo de chip/bloco ativo |
| `acao` | action = brand | `#245D8C` | preenchimento do "+" e botões principais |
| `sobreAcao` | onAction | `#F7F9FB` | texto/ícone sobre `acao` |
| `keyframe` | keyframe = brandSoft | `#A9D3EC` | valor numérico, losango |
| `perigo` | pink = danger | `#FF6B6B` | destrutivo, linha de encaixe (com alfa) |
| `selecao` | selected = brandDeep | `#123A63` | barra do lote A.01 (roxo/azul-fundo) |
| divisor | hairline = border | `#273442` | borda do painel A.01 |
| cabeçote | playhead | `#FFFFFF` | |

Cores por tipo de camada (`core/ds/aurea_tipo_da_camada.dart:193-223`), usadas no selo
da barra do topo A.01 e em listas: vídeo `#6A52E0` videocam_fill · imagem `#3D6FD9`
photo_fill · áudio `#1F8C93` music_note · texto `#B07A16` textformat · legenda `#8A6A1E`
captions_bubble_fill · forma `#2E9459` circle_fill · partículas `#B0417A` sparkles ·
elemento 3D `#C06A24` cube_fill · cena 3D `#A85520` cube_box_fill · câmera `#2A7B9B`
camera_fill · grupo `#4C5566` folder_fill · ajuste `#5A4A7A` slider_horizontal_3 · nulo
`#444C5C` smallcircle_circle.

**Dimensões (`AureaDims`)** — as que esta área usa:

| Token | Valor | Token | Valor |
|---|---|---|---|
| barraDoTopo | 42 | transporte | 46 |
| botaoDeBarra (largura) | 40 | barraDeFerramentas (=blocoDePainel) | 57 |
| botaoDeFerramenta | 64 | botaoAdicionar | 73 |
| margemDoAdicionar | 6 | folhaDeAdicionar | 251 |
| abasDaFolhaDeAdicionar | 58 | painel | 200 |
| fracaoDaPrevia | 0,40 | previaMinima | 180 |
| regua | 42 | linhaDeCamada | 34 |
| timelineMinimaComPainel | 42+2×34 = 110 | cabecalhoDoPainel / abas | 38 / 38 |
| linhaDePropriedade | 44 | rotuloDaPropriedade | 76 |
| margemDoPainel | 22 | vaoDoPainel | 6 |
| topoDoPainel | 15 | itemDeLista | 37 |
| itemDeMenu | 40 | larguraDoMenu | 250 |
| alturaMaximaDoMenu | 450 | barraDeMarcaDoMenu | 4 |
| tracoDeSelecao / Multisselecao | 2 / 1,5 | raioDaAlcaDoPalco / Escolhida | 5 / 6 |
| alcaDeGiro | 35 | raioDaFolha | 13,5 |
| raios Xs/Sm/Md/Lg/Xl | 1,5/3/4/5/8 | raioPilula | 100 |
| ícones Sm/Md/Lg/Xl | 16/20/24/32 | espaços e2…e20 | 2,4,6,8,10,15,20 |
| toqueMinimo / Confortavel | 40 / 44 | textoDeRotulo | 10 |

**Estilos (`AureaEstilos`)**: `titulo` 14 sp w600 texto · `propriedade` 12,5 sp
textoSecundario · `valor` 14 sp w700 keyframe, algarismos tabulares · `rotulo` 10 sp
textoSecundario · `secao` 11 sp w600 letterSpacing .3 textoSecundario (MAIÚSCULAS) ·
`corpo` 13 sp texto.

**Movimento (`AureaMotion`)**: `rapido` 100 ms · `normal` 200 ms · `lento` 300 ms ·
`entrada` = `Curves.decelerate` (1−(1−t)²) · `saida` = `Cubic(1/3, 0, 2/3, 1/3)` (= t²).
Nada de mola.

**Fonte**: nenhuma `fontFamily` no tema → fonte do sistema (Roboto no Android). A única
fonte empacotada (`Aurea Motion Sans`, `assets/templates/dnyx/AureaMotionSans.ttf`) é de
template, não de UI.

### 1.2 Layout HEAD (retrato) — `ui/shell/editor_shell.dart`

Hierarquia: `Scaffold(bg = cromo)` → `SafeArea` → `LayoutBuilder` → `Column`:

```
┌ BarraDoTopo ............................ 42   (some em tela cheia)
├ Prévia (Stack com PreviewStage) ........ alturaDaPrevia (FIXA; Expanded em tela cheia)
├ BarraDeTransporte ...................... 46
└ Área da timeline (Expanded) ............ resto
     Stack:
       Column: [ TimelineDoEditor (Expanded) ,
                 AnimatedSize(100 ms, decelerate, topCenter) { base } ]
       "+" flutuante (Positioned right 6, bottom 6 ou 63) — oculto com painel aberto
```

`base` (o que fica embaixo da timeline), por prioridade:
1. painel aberto **e** camada escolhida → `SizedBox(height: alturaDoPainel)` com o painel registrado (`registroDePaineis(painel)`);
2. lote (multiseleção não vazia **ou** modo Selecionar) → `BarraDoLote`;
3. camada escolhida → `BarraContextual` da camada (chave `barra-da-camada-<id>`, uma por camada p/ não herdar rolagem);
4. nada → `BarraDoProjeto`.

**Altura da prévia** (`alturaDaPreviaPara`, editor_shell.dart:654-672), com `h` = altura
útil, `W` = largura, `a` = proporção do projeto (largura/altura):

```
resto = h − 42 − 46
teto  = resto − 110 − 57
alvo  = 0,40·h
doQuadro = (W − 2·8)/a + 24          // quadro + 12 em cima e embaixo
se doQuadro < alvo: alvo = doQuadro
se teto ≤ 180: prévia = max(0, teto)
senão:         prévia = clamp(alvo, 180, teto)
```

**Altura do painel** (editor_shell.dart:504-514), com `A` = altura da área da timeline
(`h − 42 − 46 − prévia`):

```
piso = 38 + 38 + 44 = 120
alturaDoPainel = min( max(0, A − 42),  max(120, min(200, A − 110)) )
```
(o painel cede antes da timeline; abaixo do piso de 120 quem cede é a timeline, mas a
régua de 42 fica sempre).

**Tabela no aparelho de referência** (W = 411,4; h = 841,5):

| Projeto | Prévia | Área da timeline | Timeline sem painel (área − 57) | Painel | Timeline com painel |
|---|---|---|---|---|---|
| 9:16, 4:5, 1:1 | 336,6 (0,40·h) | 416,9 | 359,9 | 200 | 216,9 |
| 4:3 | 320,6 (quadro) | 432,9 | 375,9 | 200 | 232,9 |
| 16:9 | 246,4 (quadro) | 507,1 | 450,1 | 200 | 307,1 |

Quadro da composição dentro da prévia (`compositionRect`, `widgets/composition_frame.dart:7`):
`inset = min(8, min(w,h)/4)`, `escala = min((w−2·inset)/cw, (h−2·inset)/ch)`, centrado.
Ex.: 9:16 → 180,3 × 320,6; 1:1 → 320,6 × 320,6; 16:9 → 395,4 × 222,4.

**Paisagem/tablet (HEAD)**: não há layout largo — a mesma coluna. Em 667×375 a prévia
cai para `teto` (120 dp), o painel para o piso de 120 e a timeline fica com ~47 dp (só a
régua). `resizeToAvoidBottomInset` = `ModalRoute.isCurrent` (não encolhe quando há
folha por cima).

### 1.3 Variante A.01 (print) — `editor_screen.dart@aba36bb:838-1101` + `application/ui/editor_layout.dart`

Coluna: **topo 44 · prévia · faixa 8 · transporte 46 · timeline · painel contextual**.
Sem arrasto entre regiões (as alças foram removidas; comentário em
`editor_session.dart:143-156`).

```
ws = h − 44 − 46 − 8                                    // "workspace"
fracaoPrévia = min(0,50, max(96, ws − 90 − clamp(ws·0,42, 250, 320)) / h)
prévia = clamp(h · clamp(fracao, .14, .60), 96, max(96, ws − 110))
painel = ws · fração                                    // ver abaixo
piso   = 90 (camada ou painel) | 120 (nada) | 44 (aba de animação de texto) | 0 (adicionando)
timeline = ws − prévia − painel; se < piso: painel −= (piso − timeline)
se piso == 0 e 0 < timeline < 110: painel += timeline; timeline = 0
```
Frações do painel: categoria aberta 0,46 · adicionando 0,48 · camada sem painel (doca)
0,40 · lote `(12+124)/ws` · nada selecionado `(12 + (sem camadas ? 86 : 30))/ws` ·
dicas de 1º uso `(12+108)/ws` · animação de texto 0,60.

**Medido em t2.png** (confere com a fórmula: ws = 743,5 → prévia 341,2; painel 0,46 →
342 → corta para 312,3 porque o piso da timeline é 90):

| Região | px (y) | dp | Cor |
|---|---|---|---|
| Barra de status | 0–127 | 48,8 | `#070A0E` |
| Barra do topo | 128–243 | 44 | `#0F141A` |
| Prévia | 244–1138 | 341,2 | `#0F141A` |
| Faixa (PreviewResizeHandle) | 1139–1159 | 8 | `#151C24` |
| Transporte | 1160–1280 | 46 | `#0F141A` |
| Timeline | 1281–1516 | 90 | `#0A0E13` |
| Painel: divisor | 1517–1519 | ~1 | `#273442` |
| Painel: cabeçalho "‹ Efeitos" | 1520–1634 | 44 | `#151C24` |
| Painel: corpo | 1635–2336 | 267,4 | `#0F141A` |
| Barra de navegação | 2337–2399 | 24 | `#000000` |

Layout largo A.01 (`largo` = largura ≥ 600 e paisagem, ou largura ≥ 900): topo em cima;
à esquerda coluna [prévia (Expanded), faixa, transporte, timeline com
`clamp((h−44−46−8)·0,34, 88, 280)`]; à direita o painel com largura
`clamp(W·0,4, 280, 380)` e altura `h − 44`.

### 1.4 Tela cheia

- **HEAD**: `EditorSession.previewExpanded` (`application/ui/editor_session.dart`). A coluna vira `[Prévia (Expanded), BarraDeTransporte]` — some o topo e a área da timeline; `SystemChrome.setEnabledSystemUIMode(immersiveSticky)` ao entrar e `edgeToEdge` ao sair (`editor_screen.dart`). Entrar/sair: menu do olho (transporte) "Tela cheia / Sair da tela cheia", menu ⋯ do projeto, ou Voltar. **Não há botão de sair na tela** (ver bugs).
- **Variante A.01**: prévia com altura `m.preview − 44`, embaixo `BarraDeTempoTelaCheia` (44 dp: tempo 12 sp w600 · trilho 3 dp (5 dp arrastando) branco 22% com parte feita em `#6FAED9` · duração em muted; toque/arrasto busca, pausando durante o arrasto) e o transporte. Botão flutuante **"Voltar ao editor"**: `Positioned(right 10, top 10)`, círculo 40×40 `#CC12151A`, ícone Material `fullscreen_exit` branco (24).

### 1.5 Ordem do Voltar (sistema e botão ‹) — `editor_shell.dart:235-282`

Fecha a coisa mais interna, uma por toque:
1. teclado aberto → tira o foco;
2. desenho livre ligado → desliga;
3. folha persistente com histórico local → `pop`;
4. tela cheia → sai;
5. painel aberto → fecha;
6. lote/modo Selecionar → sai do modo (limpa marcadas);
7. camada escolhida → desseleciona;
8. dentro de grupo → sai um nível;
9. senão → captura a miniatura do projeto (se `thumbTime == null`) e sai do editor.
`PopScope.canPop = !temContexto && !dentroDeGrupo`.
(A.01: mesma ideia, com "adicionando" primeiro, depois desenho livre, folhas, tela
cheia, timeline expandida, pontos→volta, curva→volta, etc. — `editor_screen@aba36bb:180-247`.)

Tocar no **vazio da timeline** (`_soltarSelecao`): com painel aberto só fecha o painel;
senão sai do modo Selecionar e desseleciona.

### 1.6 Matriz de estados (o que aparece em cada região)

| Estado | Topo HEAD | Topo A.01 | Base HEAD | Base A.01 | "+" HEAD | "+" A.01 | Tesoura no transporte (HEAD) |
|---|---|---|---|---|---|---|---|
| Nada escolhido | fixo | BarraDoProjeto | BarraDoProjeto (57) | dica de 1 linha (painel fino 42 dp) ou dicas de 1º uso | bottom 6 | bottom 18+painel | não |
| 1 camada, sem painel | fixo | BarraDaCamada | BarraContextual da camada (57) | LayerToolsDock (0,40·ws) | bottom 63 | bottom 18+painel | sim (desabilitada fora da camada/travada) |
| 1 camada, painel aberto | fixo | BarraDaCamada | Painel (200; mín 120) | ContextSheet com "‹ Título" (0,46·ws) | oculto | oculto | sim |
| Lote / modo Selecionar | fixo | BarraDoLote roxa | BarraDoLote (57) | MultiSelectionPanel (12+124) | bottom 63 | bottom 18+painel | não |
| Adicionando | folha modal por cima | igual | igual | AddLayerPanel embutido (0,48·ws) | (folha cobre) | oculto | — |
| Desenho livre | fixo | igual | igual | igual | igual | igual | — (palco mostra BarraDoDesenho no topo da prévia; chip de resolução some) |
| Tela cheia | oculto | oculto | oculta (só prévia + transporte) | prévia + barra de tempo 44 + transporte | oculto | oculto | segue a regra |

Trocar a seleção fecha o painel aberto e o editor de pontos (`editor_screen.dart`,
listener de `selectedLayerProvider`). O provider do painel e o zoom do palco são zerados
ao abrir o editor.

---

## 2. Barra do topo

### 2.1 HEAD — `ui/shell/barra_do_topo.dart`

`Container(height 42, color cromo)` → `Row`:

| Ordem | Chave | Ícone (Cupertino) | Alvo | Ação |
|---|---|---|---|---|
| — | | `SizedBox(2)` | | |
| 1 | `topo-voltar` | `chevron_left` 22 dp, texto | 40×42 | `_voltar()` (§1.5). Semântica "Voltar" |
| 2 | `topo-nome` | nome do projeto, `titulo` 14 w600, 1 linha, reticências, alinhado à esquerda | Expanded × 42 | abre `pedirNome` ("Nome do projeto", OK/Cancelar) → `renameProject(nome.trim())` |
| 3 | `topo-menu` | `ellipsis` 22 | 40×42 | menu do projeto (§5.8). "Mais do projeto" |
| 4 | `topo-projeto` | `gear_alt` 22 | 40×42 | pausa + `showProjectSettingsSheet` (§6). "Configurações do projeto" |
| 5 | `topo-exportar` | `square_arrow_up` 22 em **destaque `#6FAED9`** | 40×42 | pausa, `exitAllGroups()`, abre tela Exportar (rota `fullscreenDialog`). "Exportar" |
| — | | `SizedBox(2)` | | |

Feedback de toque: `Tocavel` (escala 0,965 + opacidade 0,82; ver §8). Só observa o
nome do projeto. **Não muda com a seleção** (nada / camada / lote = mesma barra).

### 2.2 Variante A.01 (print) — `shell/cromo_editor.dart@aba36bb`

Altura **44**, cor `#0F141A`. Botão padrão `_BotaoDoCromo`: alvo 40×44, ícone 21 (19 nos
de ação), branco `#F7F9FB`; desabilitado = branco 25% (`0x66FFFFFF` com alfa 0,25);
háptico leve no toque, médio no toque longo; tooltip.

**a) Camada escolhida — `BarraDaCamada` (é a do t2)** — `Row`, `padding right 4`:

| Ordem | Chave | Visual | Ação |
|---|---|---|---|
| 1 | `editor-back` | `chevron_left` 21, alvo **44**×44 | Voltar (tira a seleção) |
| 2 | — | selo 24×24, raio 7, cor do tipo (imagem `#3D6FD9`), ícone do tipo 14 branco; `margin-right 8` | — |
| 3 | `camada-nome` | nome 14 sp w600 branco (vazio: "(Camada sem nome)" em branco 40%) | toque → edição **inline** (`CupertinoTextField`, fundo `#212D3A` raio 8, padding 8/6, 14 w600; confirma no Enter ou ao perder foco; vazio não vale) → `renameLayer` |
| 4 | `camada-parentesco` | `link_circle_fill` 20 em `#6FAED9` — **só aparece se a camada tem pai** | abre a folha de parentesco |
| 5 | `camada-duplicar` | `plus_square_on_square` 19 | `duplicateLayer(id)` |
| 6 | `camada-lixeira` | `trash` 19 | `excluirCamadas({id})` |
| 7 | `camada-menu` | Material `more_vert` 19 | menu da camada (folha) |

Posições medidas no t2 (centros): ‹ 22 dp · selo 44–68 dp · nome começa em 76 dp ·
duplicar 307,4 · lixeira 347,4 · ⋮ 387,4 (= 411,4 − 4 − 20). Glifo "home6": altura de
ascendente 10,7 dp (≈14–15 sp). Selo medido 23,6 dp.

**b) Nada escolhido — `BarraDoProjeto` A.01** — `padding right 6`:
sair (`Icons.logout` espelhado, 20, alvo 44×44; tooltip "Projetos" ou "Voltar" dentro de
grupo) · [migalhas de grupo: pílulas 26 dp, raio 13, `#212D3A`, ícone 12 `film`/
`rectangle_stack` em `selecao`, nome 11,5 sp, `chevron_right` 10; largura máx 30% da
tela, rolagem reversa; toque volta àquele nível] · título editável inline (14 w600;
vazio "(Sem título)"; `maxLength 320`) · chip do cronômetro (só contando: pílula
`#212D3A`, padding 7/3, `timer` 12 em `acao` + "h:mm:ss" 11 sp tabular) · tempo atual
"m:ss.cc" 12,5 sp branco 40% (toque → diálogo "Ir para o tempo") · ⋮ `more_vert` 19
("Mais da linha do tempo") · `gear_alt_fill` 19 ("Projeto") · `square_arrow_up` 21 em
**`acao #245D8C`** ("Exportar").

**c) Multiseleção — `BarraDoLote` A.01** (substitui o topo; fundo **`selecao #123A63`**),
duas páginas:
- página 1: ✕ `xmark` (36) · texto "N selecionadas" / "Selecione ao menos duas camadas" 13 sp w700 · `rectangle_stack` Agrupar (36) · `square_stack_3d_down_right_fill` Agrupar e mascarar (36) · `square_stack_3d_down_right` Agrupar e recortar (36) · `trash` (36) · `chevron_right` → página 2 (30). Ícones 18, desabilitados com < 2.
- página 2: `chevron_left` (30) · Spacer · alinhar esquerda `arrow_left_to_line` / centro H `arrow_left_right` / direita `arrow_right_to_line` / topo `arrow_up_to_line` / meio V `arrow_up_arrow_down` / base `arrow_down_to_line` (30 cada; toque alinha, toque longo abre a folha Alinhar) · distribuir V `arrow_up_down_square` · distribuir H `arrow_left_right_square` (≥ 3 camadas).

---

## 3. Prévia (chrome), sobreposições e edição no palco

Arquivos: `widgets/preview_stage.dart` (1–1350 relevantes), `ui/shell/editor_shell.dart:420-454`,
`ui/shell/sobreposicoes_da_previa.dart`, `ui/palco/*`, `widgets/composition_frame.dart`,
`widgets/faixa_de_bloqueio.dart`.

### 3.1 Estrutura da prévia (HEAD)

```
_Previa = Stack(key 'zona-previa'):
  Positioned.fill → RepaintBoundary(key previewStageKey)  ← usado p/ miniatura e conta-gotas
                     → PreviewStage
  [modo dev] Positioned(top 6, left 8) IgnorePointer → DiagnosticoDaPrevia
  Positioned(top 6, right 8) → AvisoDeRascunho
PreviewStage = RawGestureDetector(ReconhecedorDoPalco) → Stack(expand):
  ColoredBox(AmColors.panel = #0F141A) → LayoutBuilder → Stack(clip hardEdge):
     Positioned(quadro) → CompositionFrame(ClipRect) → [fundo do projeto, composição,
         casca de cebola, guias/encaixe, grade/pixels, gizmo 3D, gizmo da cena,
         editor de nós, desenho livre]
     Positioned.fill → CamadaDaSelecao (moldura + alças, em px de tela)
  Positioned(right 4, top 4)   → chip de resolução   (oculto desenhando)
  Positioned(top 8, l/r 8)     → FaixaDeBloqueio      (se a escolhida está travada)
  Positioned(top 4, l/r 8)     → BarraDoDesenho       (desenho livre)
```

- **Fundo**: `#0F141A` (mesmo do cromo) — a composição não tem contorno; quem a delimita é o próprio recorte e a cor de fundo do projeto (`backgroundColor`, padrão preto). Sem borda, sem sombra.
- **Chip de resolução** (`preview_stage.dart:956-988`): `Material(color 0xCC171D25, raio 6)` (sobre `#0F141A` resulta `#151B23`), padding 10 h / 8 v, texto 12 sp branco. HEAD mostra **"100%"** (enum `PreviewResolution`: 100%, 75%, 50%, 33%, 25%, 12,5% — `application/ui/preview_resolution.dart`). Toque abre `PopupMenuButton` Material com os 6 valores → `previewResolutionProvider` (só sessão; nunca afeta export). "Pixels reais" força 100%. Medido no t2 (A.01, rótulo "Full"): 38,5 × 30,5 dp, `right 4`, `top ≈4,8`.
- **AvisoDeRascunho** (`sobreposicoes_da_previa.dart:212-265`): só enquanto toca **e** se há cena 3D, efeito com orçamento de amostras ou Unsharp Mask. Pílula raio 999, `#0F141A` 80%, padding 8/4, "Rascunho · pause para ver a qualidade final" 10,5 sp muted. IgnorePointer.
- **DiagnosticoDaPrevia** (modo dev, ⚙ Projeto › "Diagnóstico na tela"): caixas `#0F141A` 80% raio 8 borda hairline, texto 11 sp `#6FAED9` altura 1,4 tabular.
- **FaixaDeBloqueio compacta** (`widgets/faixa_de_bloqueio.dart`): `#151C24`, raio 10, borda `#6FAED9` 50%, padding 10/5; `lock_fill` 13 accent · "Camada bloqueada" 12 w600 · botão-pílula "Desbloquear" (accent, padding 10/4, 11,5 w700 onAction) → `toggleLocked`.
- **Composição**: `CompositionFrame` não desenha contorno (removido a pedido do dono); com `safeAreas` desenha retângulos 80% e 90% em branco 40% traço 1.

### 3.2 Sobreposições dentro da composição (coordenadas da composição)

`_GuidesPainter` (`preview_stage.dart:1258-1350`):
- **Linha de encaixe** (só durante arrasto e só no eixo que encaixou): `#CCFF6B6B`, traço `1,5/escala` (= 1,5 dp na tela), linha inteira da composição.
- **Colunas**: `guides.columns > 0` → retângulos `#6FAED9` 13% (margem/gutter do `GuidesSpec`).
- **Áreas seguras** (`showSafeAreas`): 90% e 80% do quadro, traço 2 (em px da composição!) branco 40%.
- **Guias** verticais/horizontais: `#AA35C4E7`, traço 2 (px da composição).
- **Máscara de enquadramento** (`framePreview`): escurece fora do corte com preto 60%.

`_GradeDoPalcoPainter` (`:1203-1256`): grade = 24 divisões, desenha a cada 3 (branco 15%,
traço 1/escala) e a cada 8 = terços (branco 40%, traço 1,5/escala). "Pixels reais" com
escala ≥ 6: uma linha por pixel, branco 12%.

Casca de cebola (⚙ › Prévia): k = 1..2 quadros para cada lado, opacidade `0,34/k`,
passado avermelhado, futuro esverdeado; some enquanto toca.

### 3.3 Moldura e alças da seleção — `ui/palco/alcas_do_palco.dart`

Um único `CustomPainter` (sem saveLayer/sombra), em **px de tela**, por cima do quadro:
- **Contorno da principal**: polígono dos 4 cantos transformados da caixa da camada; primeiro traço escuro `#0A0E13` 55% com largura `2 + 1,5 = 3,5`, depois `2` em **destaque `#6FAED9`** (travada: `textoSecundario`). Junções arredondadas.
- **Outras do lote**: traço de fundo 2,5 + traço 1,5 destaque.
- **Alças de escala** (3 cantos: inf-dir, sup-esq, inf-esq): círculo de fundo raio `r+1,5` (#0A0E13 55%), miolo branco raio `r`, anel 1,5 destaque; `r = 5` (6 quando pega). **Alvo de toque: raio 26** (52 dp).
- **Pegador de giro** (canto sup-dir): disco raio `35/2·0,55 = 9,6` (0,62 → 10,85 quando pego) em `painel #151C24` (pego: destaque); arco de 270° (início −162°) raio `0,52r`, traço 1,6 branco com ponta de seta triangular; anel 1 destaque quando solto. **Alvo: raio 22** (44 dp).
- Regras de posição: cada alça fica no mínimo **30 dp** do centro; se sup-dir e inf-dir ficarem a < 60 dp, são separadas ±30 dp em Y a partir do meio; todas presas **22 dp** para dentro da área do palco.
- Sem alças (só moldura) quando travada ou editando nós (máscara/caminho). Com "Editar forma" aberto numa forma paramétrica: alças extras da forma (mesmo desenho, alvo 24).
- Marcadores 0×0 com chave (`alca-escala`, `alca-giro`, `alca-forma-*`) existem só para teste.

Variante A.01: alças da seleção eram marcadores invisíveis 0×0; o contorno branco de 1 px
visto no t2 é a borda da camada escolhida desenhada dentro da composição (comentário em
`editor_screen@aba36bb`: "a borda branca da camada escolhida"). Alças da forma: círculo
18 dp branco, borda 3 accent, sombra preta 54% blur 4.

### 3.4 Gestos do palco — `ui/palco/gestos_do_palco.dart` + `edicao_no_palco.dart`

**Um único reconhecedor** (`ReconhecedorDoPalco`) entra na arena; ganha no dedo que desce
se estiver sozinho, ou quando o centroide anda a *pan slop* (36 px lógicos no Flutter) /
a distância entre 2 dedos muda a *scale slop*. Ao ganhar, repassa **todo o histórico**
(onde cada dedo desceu) ao árbitro, para não perder os primeiros px.

**Árbitro (`ArbitroDoPalco`)** — um dono por gesto, nunca troca no meio:

| Situação | Resultado |
|---|---|
| Dedo desce | anota o alvo sob o dedo, na prioridade: **alça da forma (r 24) > gizmo da camada 3D > pegador de giro (r 22) / alça de escala (r 26), a mais próxima > dentro da camada JÁ escolhida > camada de cima visível > vazio**. Nada muda ainda. |
| Solta sem andar (toque) | alça: nada. Camada: escolhe a camada de cima **visível** no ponto (fora: áudio, ajuste, câmera, fora do tempo, oculta, fora do solo, fonte de matte, travada, opacidade ≤ 1%); primeiro quem contém o ponto, depois folga de 12 dp. Vazio: desseleciona. **Dois toques no vazio** (≤ `kDoubleTapTimeout` 300 ms, ≤ `kDoubleTapSlop` 100 px) → vista volta a zoom 1/pan 0 + háptico. Toque nunca mexe no relógio. Modo Selecionar: toque marca/desmarca. |
| Anda > **18 dp** (`kTouchSlop`) com 1 dedo (> **4 dp** numa alça) | arrasto do alvo anotado: camada → posição (escolhe a camada no COMEÇO); canto → escala; giro → rotação; vazio → passeia a vista (só se zoom ≠ 1). O 1º passo aplica a folga inteira (objeto fica sob o dedo). Travada: recusa + aviso "Camada bloqueada: desbloqueie para mover" com ação "Desbloquear" (1× por gesto). Arrasto pausa a reprodução. |
| 2º dedo | vira **pinça** (fecha o arrasto antes): se um dos dedos ou o ponto médio está sobre a escolhida (folga 12) e ela não está travada nem em modo Selecionar → **pinça da camada = escala + giro (nunca posição)**, com zona morta de giro de 4°; senão → **pinça da vista** (zoom 0,25–4, "gruda" em 1 dentro de ±0,04, ancorado no ponto entre os dedos, passeio). |
| Sai 1 dedo da pinça | da camada: o que sobra não faz nada até todos subirem; da vista: o que sobra continua passeando. Terceiro dedo em diante é ignorado (troca de par sem salto). |
| Fim | fecha 1 passo de desfazer (`beginGesture` só no 1º passo que muda o projeto; `endGesture` ao soltar), apaga a linha de encaixe e a infobar. |

**Mover a camada** (`edicao_no_palco.dart:616-709`):
- trava de eixo: deslocamento acumulado > 24 num eixo com < 12 no outro → o outro fica fixo;
- **encaixe** 10 dp de tela (`10/escala` na composição) contra: centro/bordas da composição, guias (± meia caixa), centro e bordas de todas as outras camadas ativas; passo maior que a tolerância não encaixa ("quem passa correndo não está mirando"); háptico leve a cada novo encaixe; linha vermelha no eixo encaixado;
- infobar mostra X, Y, Escala %, Rotação °.
- **Escala por alça**: fator = distância(dedo, pivô)/distância inicial (mín 8); preserva proporção X/Y e espelhamento; limite |s| ∈ [0,05; 8].
- **Giro por alça**: relativo (ângulo varrido em volta do pivô), sem salto.
- Pan da vista preso a `max(0,(comp·s − área)/2) + 48` por eixo.

**Zoom do palco**: `zoomDoPalcoProvider` (1 = ajustado); zera ao abrir o editor; "Ajustar
à tela" no menu do olho volta a 1; pan zera quando zoom = 1.

Variante A.01: tinha também **Coluna de visualização** (aberta pelo olho do transporte):
40 dp de largura, `Positioned(right 0, top 40, bottom 6)`, fundo `#212D3A`, cantos
esquerdos raio 12, padding 4 v; botões com altura `clamp(floor((H−18)/6,6), 18, 44)`:
pixels `square_grid_3x2` · grade `grid` · solo `Icons.center_focus_strong` · câmera
`videocam(_fill)` (toque longo → câmeras) · divisor 22×1 · zoom + `Icons.zoom_in` (×1,25,
máx 4) · "100%" 9,5 sp (toque volta a 1) · zoom − `Icons.zoom_out` (×0,8, mín 0,25).
Ativos em `acao`. E o **indicador de zoom** (canto sup-esq, left 8 top 8): pílula preta
55%, padding 8/5, `zoom_in` 14 + "150%" 11,5 w700; toque volta a 1.

### 3.5 Outros donos de toque sobre a prévia

Mais fundo na árvore e com prioridade quando ativos: `FreehandOverlay` (desenho livre —
o palco desliga o reconhecedor), `MaskNodeEditor` (editando máscara/caminho),
`GizmoDaCenaOverlay` (objeto da cena 3D; só onde há alça), fichas Mover|Girar|Escalar do
gizmo, chip de resolução e faixa de bloqueio.

---

## 4. Barra de transporte

### 4.1 HEAD — `ui/shell/barra_de_transporte.dart`

`Container(height 46, color cromo)` → `Row` de **três colunas** (os lados `Expanded`
têm a mesma largura → play no centro exato da tela):

**Esquerda** (`FittedBox(scaleDown, centerLeft)`): [2 dp se não houver tesoura] ·
**Desfazer** `arrow_uturn_left` · **Refazer** `arrow_uturn_right` · **Dividir**
`scissors` (só com UMA camada escolhida e sem lote).
**Centro**: **Quadro anterior** `backward_end_alt` · **Play/Pausa** `play_fill`/
`pause_fill` (ícone 24) · **Próximo quadro** `forward_end_alt`.
**Direita** (alinhada ao fim): tempo `"m:ss.cc / m:ss.cc"` (`valor` com 11 sp, w500,
`textoSecundario`, tabular, `FittedBox`) · **Olho** `eye` (menu de prévia) · 2 dp.

Botão: alvo 40×46, ícone 20 (play 24), `texto` quando ativo, `textoSecundario` 40%
quando desabilitado; toque longo com háptico médio.

| Chave | Toque | Toque longo | Desabilitado quando |
|---|---|---|---|
| `transporte-desfazer` | `undo()` | — | `!canUndo` |
| `transporte-refazer` | `redo()` | — | `!canRedo` |
| `transporte-dividir` | pausa + háptico + `splitLayer(id, t)` | — | cabeçote fora de `[início+100 ms, fim−100 ms]` ou camada travada |
| `transporte-anterior` | pausa + `stepFrame(−1)` | pausa + vai ao keyframe anterior da escolhida (tolerância 1 ms) ou 0 | — |
| `transporte-play` | `toggle()` | liga/desliga repetição (`loop`) | — |
| `transporte-proximo` | pausa + `stepFrame(+1)` | pausa + próximo keyframe ou fim | — |
| `transporte-tempo` | diálogo "Ir para o tempo" (aceita `12.5`, `1:02.5`, `h:mm:ss`, `hh:mm:ss:ff` em quadros; vírgula vira ponto; preso a [0, duração]) | — | — |
| `transporte-modo` | menu "Prévia" (§5.9) | — | — |

Semântica: "Desfazer", "Refazer", "Dividir", "Quadro anterior · segure: keyframe anterior
ou início", "Tocar"/"Pausar", "Próximo quadro · segure: próximo keyframe ou fim", "Modo de
prévia". Só o texto do tempo e o ícone do play reconstroem com o relógio.

**Infobar** (substitui a barra inteira enquanto o dedo manipula algo; `infobarProvider`):
- tempo: `rhombus` 14 keyframe · tempo (`valor`) · 20 dp · deslocamento com sinal "+";
- pares: até 6 colunas iguais, rótulo (`rotulo` 10 sp) sobre valor (`valor` 14 w700).
Padding horizontal 10.

### 4.2 Variante A.01 (print) — `cromo_editor.dart@aba36bb:1042-1257`

Altura 46, `#0F141A`. Largura dos botões laterais `lado = clamp((W − 132)/6, 30, 40)` (=40
no aparelho de referência). Ordem: **Desfazer** (lado) · **Refazer** (lado) · Expanded
com o trio centrado [**|◀** `backward_end` 40 · **▶** `play_fill`/`pause_fill` ícone 26
alvo 52 · **▶|** `forward_end` 40] · **Copiar e colar** `doc_on_clipboard` (lado) ·
**Olho** (lado) · **Tela cheia** `fullscreen`/`fullscreen_exit` (lado). Ícones 21.

Centros medidos no t2: ↶ 20 · ↷ 60 · |◀ 139,7 · ▶ 185,7 · ▶| 231,7 · copiar 311,4 ·
olho 351,4 · tela cheia 391,4 dp — batem com o cálculo.

Comportamentos A.01 que diferem da HEAD:
- |◀ / ▶| andam **por keyframe** se a camada escolhida tiver marcas, senão 1 quadro; toque longo vai ao início/fim.
- Play com repetição ligada fica em `acao` e ganha selinho `repeat` 11 dp em `acao` (right 8, bottom 8).
- Olho: ícone `videocam` se "Visão da câmera" desligada; `eye_fill` em `acao` com a coluna aberta; `eye` normal. Alterna a coluna de visualização (§3.4).
- Copiar e colar abre folha "Copiar e colar": Copiar camada · Colar camada no cabeçote · Duplicar camada · Selecionar todas as camadas · Limpar seleção · [seção "Estilo e efeitos"] Copiar estilo · Colar estilo… · Copiar efeitos · Colar efeitos.
- Medidor de nível atrás dos botões enquanto toca: faixa do centro para as bordas com largura `W·nível`, gradiente `acao` 0 → 16% → 0.
- Desabilitado = branco 25%.

---

## 5. Barras e menus

### 5.1 `BarraContextual` — `ui/toolbar/barra_contextual.dart`

`Container(height 57, color painel #151C24)` → `Row[inicio?, Expanded(ListView h),
presas…, fim?]`. Botões = `AureaToolbarButton` (`core/ds/aurea_toolbar_button.dart`):
coluna centrada `ícone 24` + 4 + rótulo 10 sp (máx 2 linhas, centro, reticências);
ativo = ícone e rótulo em `destaque` (único sinal de estado, sem caixa); desabilitado =
`textoSecundario` 45%.

Algoritmo de largura:
```
util = larguraDisponível − 6 − (presas vazias ? recuoFinal : 0)
cabe = nBotões × 64 ≤ util
largura = cabe ? util / nBotões : 64          // nunca < 64
física = cabe ? sem rolagem : BouncingScroll
padding: left 6, right = presas vazias ? recuoFinal : 0   (recuoFinal padrão 6)
```
"Soltar" do lote nunca rola (fixo no fim). Ferramenta apagada com `motivo` fala: toque →
háptico + toast de 1,5 s com o motivo. Ferramenta que abre painel alterna: tocar de novo
no mesmo painel o fecha (`_tocarFerramenta`). Abrir painel pausa a reprodução.

### 5.2 Ferramentas por tipo de camada — `ui/shell/contrato.dart:157-350`

Rótulos verbatim; ícones Cupertino. ▣ = abre painel (PainelId), ⚡ = ação direta.

| Tipo | Ferramentas (na ordem) |
|---|---|
| **Vídeo** | Transformar ▣`move` · Efeitos ▣`sparkles` · Cor ▣`color_filter` · Tempo ▣`timer` · Áudio ▣`speaker_2` · Máscara ▣`circle_lefthalf_fill` · Borda ▣`square_on_square` · Rastrear ▣`viewfinder` · Dividir ⚡`scissors` · Camada ▣`info_circle` · Mais ⚡`ellipsis` |
| **Imagem** | Transformar · Efeitos · Cor (`color_filter`) · Máscara · Borda · Dividir · Camada · Mais |
| **Texto** | Texto ▣`textformat` · Fonte ▣`textformat_alt` · Estilo ▣`bold_italic_underline` · Animar ▣`wand_stars` · Efeitos · Transformar · Cor ▣`paintbrush` · Máscara · Borda · Texto 3D ⚡`cube` · Dividir · Camada · Mais |
| **Cena 3D c/ texto 3D** | Texto · Texto 3D ▣`cube` · Transformar · Efeitos · Material ▣`circle_grid_hex` · Luz ▣`lightbulb` · Ambiente ▣`cloud_sun` · Animação ▣`play_circle` · Cena ▣`cube_box` · Dividir · Camada · Mais |
| **Cena 3D** | Transformar · Material · Luz · Ambiente · Animação · Cena · Efeitos · Dividir · Camada · Mais |
| **Elemento 3D** | Transformar · Material · Efeitos · Máscara · Borda · Dividir · Camada · Mais |
| **Forma** | Transformar · Forma ▣`slider_horizontal_below_rectangle` · Cor ▣`paintbrush` · Borda · Máscara · Efeitos · Dividir · Camada · Mais |
| **Áudio** | Áudio ▣`speaker_2` · Velocidade ▣`speedometer` · Efeitos · Dividir · Camada · Mais |
| **Câmera** | Transformar · Câmera ▣`videocam` · Dividir · Camada · Mais |
| **Nulo** | Transformar · Clonar ▣`circle_grid_3x3` · Dividir · Camada · Mais |
| **Grupo** | Grupo ▣`folder` · Transformar · Efeitos · Máscara · Borda · Dividir · Camada · Mais |
| **Ajuste** | Efeitos · Transformar · Máscara · Borda · Dividir · Camada · Mais |
| **Legenda** | Legendas ▣`captions_bubble` · Transformar · Efeitos · Máscara · Borda · Dividir · Camada · Mais |
| **Partículas** | Partículas ▣`sparkles` · Transformar · Efeitos · Máscara · Borda · Dividir · Camada · Mais |

Ações diretas: **Dividir** → `splitLayer(id, t)` (pausa, háptico); **Mais** → menu da
camada (§5.7); **Texto 3D** → `ativarTexto3D(id)` (vira cena com texto 3D, seleciona a
cena e abre o painel Texto 3D; falha → toast com `ultimoMotivoDoTexto3D` ou "Nao foi
possivel transformar este texto em 3D.").

Painéis (PainelId) e o que cada um cobre: `transformar` (posição, escala, rotação,
opacidade, inclinação, pivô), `efeitos`, `cor`, `tempo` (velocidade, rampas, reverso,
congelar), `audio`, `mascara` (mistura, opacidade, máscaras), `texto`, `fonte`, `estilo`,
`animar`, `texto3d`, `material`, `luz`, `ambiente`, `animacao3d`, `propriedades` (ficha:
nome, visível, cadeado, solo, tímida, vínculo), `camera`, `velocidade`, `bordaSombra`,
`forma`, `pontos`, `clonar`, `legendas`, `particulas`, `rastrear`, `cena3d`, `grupo`.
Conteúdo dos painéis: fora desta área.

Casca do painel (`core/ds/aurea_panel.dart`): fundo `painel #151C24`, sem borda;
cabeçalho 38 = [22 · título `titulo` · ações · ✓ `checkmark_alt` 20 em destaque, alvo
44×38 · 6]; abas opcionais 38 (`AureaTabs`: texto 12 sp, ativa w600 destaque + traço
16×2 raio 1, inativas w500 secundário, padding h 10, lista rola); corpo com padding
22/4/22/15.

**Variante A.01**: cabeçalho do painel (ContextSheet) de **44**: `IconButton`
Material 48×48 com `chevron_left` 26 ("Voltar às ferramentas da camada") + título 14 sp
w600 + 12 dp; fundo `#151C24`, borda superior 1 dp `#273442`. Sem título → faixa de 12
dp vazia. Títulos: "Transformar · <prop>", "Mesclagem e opacidade", "Cor e
preenchimento", "Efeitos", "Curva de gradação", "Editar texto"/"Animação de texto"/
"Presets de texto", "Editar forma", "Editar pontos". Com Efeitos/Curva/Animação de texto,
o cabeçote da timeline fica rosa `#FF6B6B` (visível no t2).
Doca A.01 com camada e sem painel (`LayerToolsDock`, `am/layer_menu.dart@aba36bb:65`):
fundo `#0F141A`; fileira de ações rápidas 44 dp (margem 10/8, `#1E222D`, raio 10,
`IconButton`s em `spaceEvenly`: [grupo: entrar/desagrupar/tempo] [áudio: velocidade]
mover início ao cabeçote `arrow_right_to_line` · dividir `scissors` · mover fim
`arrow_left_to_line` · volume `speaker_2` · vincular `link`/`link_circle_fill`); abaixo
grade **3 colunas** de fichas (`_MenuTile`: `#222634`, raio 10, altura
`clamp((H−70)/2, 58, 82)`, gap 8, ícone 21 (18 se < 65) `#D4D8E2`, rótulo 10,5 sp w500
até 3 linhas (9,5 se < 65), selo amarelo `#FFD600` "NEW" 8,5 w900) com as seções
"Movimentação e transformação", "Cor e preenchimento", "Borda e sombra", "Mistura e
opacidade", "Volume", "Fade", "Editar forma", "Clonar", "Editar texto", "Editar
legendas", "Partículas", "Cena 3D", "Câmera", "Efeitos"…

### 5.3 Barra do projeto (nada escolhido) — `ui/toolbar/barra_do_projeto.dart`

`BarraContextual(chave 'barra-do-projeto', recuoFinal = 73 + 6 + 6 = 85)` (guarda o
lugar do "+"). Itens (os condicionais só aparecem quando valem):

| Id (`ferramenta-…`) | Ícone | Rótulo | Condição | Ação |
|---|---|---|---|---|
| projeto-adicionar | `plus_square` | Adicionar | sempre | folha de adicionar (§5.5) |
| projeto-configuracoes | `gear` | Projeto | sempre | ⚙ Projeto (§6) |
| projeto-legendas | `captions_bubble` | Legendas | há vídeo/áudio | folha Legendas (§5.11) |
| projeto-selecionar | `checkmark_square` | Selecionar | ≥ 2 camadas | liga modo Selecionar |
| projeto-colar | `doc_on_clipboard` | Colar | há camada copiada | `colarCamada(t)` (1 desfazer) |
| projeto-marcas | `bookmark` | Marcas | sempre | menu das marcas (§5.8) |
| projeto-batidas | `metronome` | Batidas | há vídeo/áudio | folha Batidas da 1ª mídia com som |
| projeto-guia | `question_circle` | Guia | sempre | tela Guia rápido |

Toda ação pausa antes. Variante A.01: sem barra — linha de dica "toque num objeto"
(`DicaDoPalco`) ou as dicas de 1º uso (`OnboardingCoach`) no painel fino.

### 5.4 Barra do lote (multiseleção / modo Selecionar) — `ui/toolbar/barra_do_lote.dart`

Entrar: toque longo parado num clipe da timeline; "Selecionar várias" no menu da camada;
"Selecionar" na barra do projeto. Com o modo ligado, tocar numa camada (timeline ou
palco) marca/desmarca (`alternarNaSelecao`: a principal também sai; com 1 só ela vira
seleção simples; multi só guarda ≥ 2). Ligar o modo fecha o painel aberto.

`BarraContextual(chave 'barra-do-lote')` com `inicio` = contagem (largura 44: número em
`titulo` destaque + 2 + "camada"/"camadas" em `rotulo`).

| Id | Ícone | Rótulo | Precisa | Ação |
|---|---|---|---|---|
| lote-agrupar | `folder_badge_plus` | Agrupar | ≥ 2 | `groupLayers(ids)` (1 desfazer), sai do modo |
| lote-duplicar | `plus_square_on_square` | Duplicar | ≥ 1 | duplica cada uma acima da original (ordem da pilha), clona rastreio de câmera de vídeos; cópias viram o lote |
| lote-alinhar | `rectangle_grid_1x2` | Alinhar | ≥ 1 | folha Alinhar (§5.10) |
| lote-cascata | `chart_bar_alt_fill` | Cascata | ≥ 2 | folha Cascata |
| lote-vincular | `link` | Vincular | ≥ 2 | escolher pai (§5.11) → vincula todas; toast "Camadas vinculadas" + Desfazer |
| lote-apagar | `trash` | Apagar | ≥ 1 | exclui (respeita ímã e cadeado; 1 desfazer), sai do modo |
| lote-mais | `ellipsis` | Mais | ≥ 1 | menu "Seleção" (abaixo) |
| lote-soltar | `xmark_circle` | Soltar | — | sai do modo (fixo no fim, não rola) |

Motivos quando apagado: n < 1 → "Toque nas camadas da timeline para marcar"; n < 2 nas
que precisam de duas → "Escolha duas ou mais camadas".

**"Mais" do lote** (`mostrarAureaMenu`, título "Seleção", âncora padrão = canto inf-dir
acima da barra): itens com motivo ficam apagados e mostram "(motivo)" no rótulo:
Dividir no cabeçote `scissors` (motivo "cabeçote fora das camadas") · Aparar o início no
cabeçote `arrow_right_to_line` · Aparar o fim no cabeçote `arrow_left_to_line` · Estender
até o cabeçote `arrow_left_right` · Encostar no cabeçote `arrow_right_to_line_alt` ·
Começar juntas `increase_indent` (≥2) · Terminar juntas `decrease_indent` (≥2) · Uma
depois da outra `chart_bar_alt_fill` (≥2) · Copiar camadas `doc_on_doc` · Colar camadas
`doc_on_clipboard` ("nada copiado") · Colar efeitos em todas `wand_stars` ("copie os
efeitos de uma camada antes") · Ocultar/Mostrar `eye_slash`/`eye` · Travar/Destravar
`lock`/`lock_open` · Mover para cima `arrow_up_to_line` · Mover para baixo
`arrow_down_to_line` · Agrupar e mascarar `square_stack_3d_down_right_fill` (≥2) ·
Agrupar e recortar `square_stack_3d_down_right` (≥2) · Selecionar todas
`checkmark_square_fill` ("já estão todas").
Travadas: operações de tempo/pilha avisam "As camadas bloqueadas ficaram de fora" (toast
1,5 s) ou recusam com "Camadas bloqueadas: desbloqueie para editar".

Variante A.01: a multiseleção trocava a **barra do topo** (roxa, §2.2c) e o painel virava
`MultiSelectionPanel` (cabeçalho + uma fileira de botões, altura 12+124).

### 5.5 Botão "+" e folha de adicionar — `ui/shell/editor_shell.dart:614-645`, `ui/toolbar/adicionar.dart`, `adicionar_acoes.dart`

**"+" HEAD**: círculo **73×73** `acao #245D8C`, sem anel nem sombra, `Icons.add` (Material)
32 em `sobreAcao`. `Positioned(right 6, bottom 6 + (camada ou lote ? 57 : 0))`; oculto
com painel aberto. Toque: háptico leve → `abrirAdicionar`. Semântica "Adicionar".
**"+" A.01**: círculo **52×52** `#1E2130` com anel 2,2 `acao`, sombra preta 45% blur 8
offset (0,3), `Icons.add` 32 em `acao`; `Positioned(right 18, bottom 18 + alturaDoPainel)`;
oculto em tela cheia, adicionando ou com categoria aberta.

**Folha de adicionar HEAD** (`mostrarAureaFolha`, `grande: true` → entra em 200 ms,
altura do conteúdo `251 − 10` → total 251 com o respiro de 10 do topo; sem título,
sem arraste para fechar, véu `#0A0E13` 35%, raio 13,5, fundo `#151C24`). Pausa o relógio.
Tudo entra **no cabeçote** e a camada nova vira a seleção.

Estrutura: linha de abas 58 = 5 × `AureaToolbarButton` (Expanded, ativo em destaque) +
"Fechar" (`xmark`, 44 de largura); conteúdo (Expanded) com `AnimatedSwitcher` 100 ms
(entrada decelerate / saída t²), sempre alinhado ao topo.

| Aba (`adicionar-aba-<id>`) | Ícone | Conteúdo |
|---|---|---|
| Mídia (`midia`) | `photo_on_rectangle` | trilho vertical de 58: **Recentes** `clock` · **Galeria** `photo_on_rectangle` · **Áudio** `music_note_2` (abre em Recentes se houver, senão Galeria) |
| Texto (`texto`) | `textformat` | blocos: Texto `textformat` · Legenda `captions_bubble` · Texto 3D `textformat_alt` |
| Formas (`formas`) | `square_on_circle` | 3 ferramentas (grade 3 col, altura 57): Desenho livre `scribble` · Vetorial `pencil_outline` · SVG `doc_text`; depois a biblioteca de formas (grade `maxCrossAxisExtent 57`, gap 6, ladrilho `#212D3A` raio 5 padding 10 com a forma desenhada em branco; traço 2,5) |
| 3D (`3d`) | `cube` | Do aparelho `device_phone_portrait` · Sketchfab `cloud_download` · Texto 3D `textformat_alt` · Sólido 3D `cube_fill` |
| Objetos (`objetos`) | `circle_grid_hex` | Nulo `smallcircle_circle` · Câmera `videocam` · Ajuste `slider_horizontal_3` · Partículas `sparkles` · Grupo vazio `folder` · Agrupar camadas `folder_badge_plus` |

Blocos (`_blocos`): 4 por fileira, largura `(W − 2·22 − 6·3)/4`, `Wrap` gap 6, padding
22/10/22/10; bloco = `AureaToolbarButton(bloco)`: altura 57, `#212D3A` (ativo
`#1D3A55`), raio 5, ícone 32, rótulo 10 sp 1 linha. Ocupado (importando) → blocos
desabilitados.

Ações (ids `adicionar-<id>`):
- Texto → `addTextLayer(t)` e fecha. Legenda → fecha e abre folha Legendas. Texto 3D → fecha; `pedirNome("Texto 3D", "TEXTO 3D")` → menu "Material" (estilos de texto 3D) → [se > 1 família] menu "Fonte" → `addTexto3D(t, texto, estilo, familia)`; seleciona a cena e abre painel Texto 3D; erro "Não consegui criar o texto 3D com essa fonte.".
- Desenho livre → pausa e liga `freehandRequestProvider` (o palco passa a desenhar; Voltar desliga). Vetorial → fecha; cria forma só-traço branca (10) "Desenho" e abre Editar pontos. SVG → seletor `.svg` → `addSvgLayers`; parcial → toast "{n} desenho(s); ficou de fora: …".
- Forma da biblioteca → `addShapeLayer(t, contents, name)`.
- 3D Do aparelho → seletor (múltiplo, qualquer tipo; GLB/glTF/OBJ/FBX/.zip) → `concluirImportacao3D` (§5.11). Sketchfab → tela do Sketchfab. Sólido 3D → `addElement3DLayer(cube)` + rotação X −20°, Y −30° (1 desfazer).
- Nulo/Câmera/Partículas/Grupo vazio → `add*Layer(t)`. Ajuste → cria e **abre o painel Efeitos**. Agrupar camadas → folha "Agrupar quais camadas?" (lista `AureaLayerRow` com `circle`/`checkmark_circle_fill`; botão `CupertinoButton.filled` "Agrupar" ativo com ≥ 2) → `groupLayers` (1 desfazer); < 2 camadas → toast "Um grupo precisa de duas ou mais camadas".
- Mídia › Recentes: grade `maxCrossAxisExtent 96`, gap 2, padding 0/6/6/0; miniatura raio 3 fundo `#212D3A`, selo `videocam_fill` 16 no canto inf-esq (4,4). Vazio: "As fotos e os vídeos que você usar ficam aqui.". Recente sumido do disco → sai da lista + "Essa mídia não está mais no aparelho.".
- Mídia › Galeria → `GalleryPanel` (§5.6) com padding right 6.
- Mídia › Áudio: fileira (altura 57+12, padding 6/6/22/6) com dois blocos "Arquivo de áudio" `music_note` ("Preparando…" ocupado) e "Som de um vídeo" `film`; abaixo a lista de sons recentes (`LinhaDeSomRecente`: anel de prévia 38 com `CircularProgressIndicator` 34 traço 2,4 `#6FAED9` sobre `#212D3A` e `play_fill`/`pause_fill` 14; nome 13 sp; botão "+" 34×34 raio 10 `#212D3A` com `plus` 16 accent; padding vertical 3). Erros: "Não consegui importar esse áudio. Tente outro arquivo." / "Não consegui usar esse som. Importe o arquivo de novo.".

**Variante A.01**: "Adicionar" era um painel embutido na zona do painel contextual
(`AddLayerPanel`, fração 0,48, podia cobrir a timeline inteira), não uma folha modal.

### 5.6 Galeria — `ui/toolbar/galeria.dart`

Painel embutido (não cobre a prévia). Cabeçalho 32: seletor de álbum (`PopupMenuButton`
Material: `Icons.grid_view` 16 · nome do álbum 12 sp · `arrow_drop_down` 18; itens:
"Recentes" (se houver) + álbuns) · botão Recentes `clock` 18 (accent quando ativo; só
com recentes) · "Fotos do sistema" `photo` 18 · "Vídeos do sistema" `videocam` 18
(IconButtons de minWidth 36). Linhas opcionais: "Acesso limitado · Selecionar mais fotos"
(11 sp accent), erro "{erro} Tentar novamente" (11 sp pink), aviso (11 sp muted).
Grade 3 colunas, gap 1, paginação de 60 (carrega quando faltam < 240 px), miniatura com
fundo `#212D3A`; vídeo: `Icons.play_arrow` 26 branco no centro + duração "m:ss" 10 sp
branco com sombra (right 3, bottom 2).
**Lote**: toque longo marca; com marcadas, toque marca/desmarca; círculo 20 accent com o
número da ordem (11 sp w700 `#0B1117`) no canto sup-esq (3,3). Cabeçalho do lote: "{n}
marcadas" 12 w700 · "juntas no cabeçote" `square_stack_3d_down_right` · "em sequência"
`arrow_right_to_line` · sair `xmark` (minWidth 34, ícone 18); com imagens: linha 26 "Cada
imagem fica" · − · "3.0 s" (12 w700 accent) · + (passo 0,5; 0,5–30 s; persistido).
Sem acesso: texto centrado + blocos "Permitir acesso"/"Liberar fotos" `lock_open` e
"Escolher arquivos" `folder`. Importando: véu `#DD17191D` com spinner e "Carregando
mídia…". Álbum lembrado entre aberturas (prefs `galeria.ultimo_album`); reimportar a
mesma origem reaproveita a cópia.

### 5.7 Menu da camada ("Mais") — `ui/toolbar/menu_da_camada.dart`

Menu flutuante `AureaMenu` (§5.12), aberto na posição do dedo (toque longo) ou na âncora
padrão. Pausa; se a camada não era a escolhida, passa a ser. Cada ação síncrona = 1
passo de desfazer. Chaves `menu-camada-<id>`.

**Principal**: Duplicar `plus_square_on_square` (`duplicarCamada` + clona rastreio) ·
Dividir no cabeçote `scissors` (hab. se ativa em t; senão toast "Leve o cabeçote para
dentro da camada") · [Juntar ao pedaço vizinho `link`, só se `hasJoinableNeighbour`] ·
**Apagar** `trash` (destrutivo, vermelho) · Renomear `pencil` (hab. se destravada;
diálogo "Nome da camada") · Ocultar/Mostrar `eye_slash`/`eye` · Travar/Destravar
`lock`/`lock_open` · Solo `scope` (marcado se solo) · Selecionar várias
`checkmark_square` · Mover para cima `arrow_up_to_line` (hab. idx > 0) · Mover para baixo
`arrow_down_to_line` · Copiar camada `doc_on_doc` (toast "Camada copiada") · Colar
camada `doc_on_clipboard` (acima desta) · Copiar keyframes `suit_diamond` (os escolhidos
desta camada, ou todos) · Colar keyframes `suit_diamond_fill` · Copiar estilo
`paintbrush` · Colar estilo… `paintbrush_fill` (folha) · [não grupo] Agrupar
(pré-compor) `rectangle_stack` | [grupo] Editar o grupo `arrow_down_right_square` ·
Desagrupar `square_split_2x2` · Tempo do grupo `timer` (abre painel Grupo) · Vincular a…
`link` / Vínculo (pai)… `link_circle_fill` (marcado) · Alinhar… `square_grid_3x2` ·
Etiqueta… `tag` (menu "Etiqueta": "Sem etiqueta" `nosign` + cores `circle_fill` da paleta
de rótulos) · **Mais ações…** `ellipsis`.

**"Mais ações"** (2º menu, título "Mais ações", mesma âncora): [vídeo] Decupar
`rectangle_split_3x1` (desab. com reverso/time remap) · Aparar o início no cabeçote
`arrow_right_to_line` · Aparar o fim no cabeçote `arrow_left_to_line` (dentro) · Mover o
início para o cabeçote `arrow_right_to_line_alt` · Mover o fim para o cabeçote
`arrow_left_to_line_alt` · [vídeo] Congelar quadro `snow` (dentro) · Copiar efeitos
`sparkles` · Colar efeitos `wand_stars` · [áudio/vídeo] Mudo `speaker_slash` (marcado) ·
Detectar batidas `metronome` · Inserir cópia no cabeçote `arrow_right_to_line` ·
Sobrescrever no cabeçote `rectangle_on_rectangle` · Levantar trecho (Entrada–Saída)
`arrow_up_to_line` · Extrair trecho (Entrada–Saída) `scissors_alt` (sem I/O → "Marque
Entrada (I) e Saída (O) na régua") · [vídeo] Legendar `captions_bubble` · Reenquadrar
`crop` ("Achando o assunto..." → "Reenquadrado"/"Não achei um assunto claro") ·
Estabilizar `camera_viewfinder` · [cena 3D] Câmeras `videocam` · [texto] Texto no
caminho `arrow_turn_up_right` · Camada 3D `cube` (marcado) · Motion blur `wind`
(marcado) · [extrudável] Extrude 3D `cube_box` · [visual] Pulsar na batida `waveform` ·
Loop de keyframes `repeat` · Organizar `folder` · [mídia] Informações da mídia
`info_circle` · [vídeo] Extrair o áudio `music_note_2` · [visual] Recortar pela camada de
baixo `arrow_turn_left_down` (hab. se há base) / Soltar o recorte `arrow_turn_up_right` ·
Caber na composição `rectangle_arrow_up_right_arrow_down_left` · Preencher a composição
`fullscreen` · Esticar até as bordas `arrow_up_left_arrow_down_right` · Espelhar na
horizontal `arrow_left_right_square` · Espelhar na vertical `arrow_up_down_square` ·
[grupo] Grupo de máscara `square_stack_3d_down_right_fill` · Grupo de recorte
`square_stack_3d_down_right` (≥ 2 filhos; marcados conforme o modo do 1º filho) ·
**Apagar e fechar o buraco** `delete_left` (destrutivo) · Fechar buracos da timeline
`arrow_left_right` ("Não há buraco para fechar") · Selecionar a camada de cima
`chevron_up` · Selecionar a camada de baixo `chevron_down`.

Variante A.01: menu da camada era folha (`mostrarFolhaDeMenu`) aberta pelo ⋮ da barra
do topo.

### 5.8 Menu do projeto (⋯) e menu das marcas — `ui/toolbar/menu_do_projeto.dart`

Ambos são **folhas** (`mostrarAureaFolha`, pequena: 100 ms), porque as linhas têm
detalhe. Linha `ItemDoMenuEmFolha`: altura mín 40; barra de marca 4×24 em destaque se
marcado; 10; ícone 20; 10; rótulo `corpo` 13 sp + detalhe 11 sp secundário (máx 2
linhas), padding vertical 6; visto `checkmark_alt` 16 destaque à direita se marcado; 15.
Desabilitada = secundário 50%; perigo = `#FF6B6B`. Toque: háptico `selectionClick`.
Título de seção (`SecaoDoMenuEmFolha`): MAIÚSCULAS `secao`, padding 14/15/15/4.
Altura máx 78% da tela.

**Menu do projeto** (chave `timeline-menu`; pausa ao abrir):
- SELEÇÃO: Selecionar todas as camadas `checkmark_square` (≥ 2) · Limpar seleção `square`.
- REPRODUÇÃO E PRÉVIA: Reprodução em loop `repeat` (marcado) · Tela cheia/Sair da tela cheia `fullscreen` · "Prévia: Resultado final" `sparkles` / "Prévia: Sem efeitos" `wand_rays_inverse` / "Prévia: Selecionada a 50%" `circle_lefthalf_fill` (rádio).
- PROJETO: Aparar o projeto no cabeçote `scissors_alt` (detalhe "Corta tudo o que passa de {t}"; desab. em t = 0; toast "Projeto aparado no cabeçote" + Desfazer) · Usar este quadro como miniatura `photo` (detalhe "Hoje: {t}") → desseleciona, espera 1 quadro, fotografa `previewStageKey`, toast "Este quadro virou a miniatura do projeto" · [se definida] Voltar à miniatura automática `photo_on_rectangle` · Marcar aqui o fim da introdução `arrow_right_to_line` (detalhe "Esticado noutro projeto, a introdução toca intacta" ou "Hoje: …") · [se marcada] Tirar a marca da introdução `xmark` · Marcar aqui o começo do final `arrow_left_to_line` ("Esticado noutro projeto, o final toca intacto") · [se marcada] Tirar a marca do final `xmark`.
- MARCAS E RITMO: Marcar este instante `bookmark` (`toggleMarker(timeForInput)`) · Marcas na timeline `bookmark_solid` (detalhe = contagem) · Batidas da música `music_note_2` ("Adicione um áudio ou um vídeo primeiro").
- CRONÔMETRO DE EDIÇÃO: Iniciar o cronômetro `timer` ("Conta o tempo que você passa editando este projeto") | Pausar `pause_circle` / Retomar `play_circle` (detalhe = total) · Apagar o cronômetro `trash`.
- MAIS: Ímã da timeline `arrow_right_arrow_left_square` (marcado; "Ligado: apagar fecha o buraco" / "Desligado: apagar deixa o buraco") · Agrupar camadas… `rectangle_stack` · Guia rápido `book`.

**Menu das marcas** (título "{n} marca(s)", ação à direita "{bpm} bpm" se houver):
Marcar aqui `bookmark` · [marca sob o cabeçote ±120 ms] Renomear, pintar ou apagar a
marca daqui `pencil` · Ir para a próxima marca `chevron_right_2` · Cortar em todas as
marcas `scissors` (toast "{n} corte(s)") · Distribuir as camadas nas marcas
`square_grid_2x2` ("Uma camada por marca, na ordem em que estão"; ≥ 2) · Marcar Entrada
(I) / Marcar Saída (O) (`arrow_right_to_line` ou `checkmark_circle_fill` marcado; detalhe
"O começo/fim do trecho que Levantar e Extrair usam" ou "já marcado em {timecode}") ·
Batidas da música… `music_note_2` ("Detecta o ritmo e risca a régua") · Batidas viram
marcas `flag` · Limpar as marcas `delete` (perigo).

Variante A.01: mesmo conteúdo em `menuDaTimeline` (folha Material, raio 18, fundo
`#0F141A`, puxador 36×4 muted 40%, seções 12 sp w600 muted, padding 20/14/20/4), aberto
pelo ⋮ do topo.

### 5.9 Menu "Prévia" (olho do transporte, HEAD)

`AureaMenu` com título "Prévia" ancorado no botão: Resultado final · Sem efeitos ·
Selecionada a 50% (marcado = atual) · Grade `grid` (marcado) · Pixels reais
`square_grid_4x3_fill` (marcado) · Visão da câmera `videocam` (marcado) · Ajustar à tela
`viewfinder` (zoom 1) · Tela cheia/Sair da tela cheia `fullscreen`/`fullscreen_exit`.
"Sem efeitos" e "Visão da câmera" desligada alteram só o projeto que o palco desenha
(`projetoParaOPalco`), nunca o export.

### 5.10 Alinhar e Cascata — `ui/toolbar/alinhar.dart`

**Alinhar** (`showAlignSheet`): folha **não modal** (véu 0%, o palco continua tocável),
título "Alinhar · {n} camada(s)". Corpo (padding 22/4/22/15):
1. Linha "Em relação a": pílulas Composição | Seleção (`FileiraDePilulas`, altura 44, gap 6).
2. Fileira de 6 blocos (Expanded, altura 57; ícone 22 destaque + 4 + rótulo 10 sp): Esquerda `rectangle_grid_1x2` · Centro H `arrow_left_right` · Direita `rectangle_grid_1x2_fill` · [8 dp] · Topo `arrow_up_to_line` · Centro V `arrow_up_arrow_down` · Base `arrow_down_to_line` → `alignSelection(ids, borda, t, to: referência)`.
3. Seção DISTRIBUIR: aviso "Por centro iguala os centros; por vão iguala os espaços. Com tamanhos diferentes, dão resultados distintos." + chips "↔ centro", "↔ vão igual", "↕ centro", "↕ vão igual" (opacidade 35% com < 3; toque avisa "Distribuir precisa de 3 ou mais camadas") → `distributeSelection`.
4. Linha "Espaço exato": pílulas 0px · 16px · 24px · 48px → `spaceSelection(ids, horizontal, g, t)`.

**Cascata** (`abrirCascata`): folha modal "Cascata · {n} camadas"; abas Pronto | Montar |
Avançado. Pronto: aviso "40 ms entre cada camada, com Mola de interface.". Montar:
Intervalo (0–200 ms, 0 casas) + Ordem (Início/Centro/Fim/Aleatória). Avançado: + Vínculo
(Posição/Escala/Rotação/Opacidade) + Curva (Apple padrão/entrada/saída, Mola interface,
Mola suave) + aviso. Botão cheio (`acao`, padding v 12, 14 w700): "Escalonar seleção" /
"Aplicar cascata" / "Vincular com atraso incremental" → `cascadeSelection` ou
`linkCascadeSelection`; toast "Cascata aplicada"/"Vinculo em cascata aplicado" +
Desfazer.

### 5.11 Demais folhas desta área

- **Congelar quadro** (`congelar.dart`): folha "Congelar quadro"; Duração (0,1–10 s, 1 casa, padrão 1) · Onde: Clipe separado | Dentro do clipe · botão "Congelar aqui" → `freezeFrame(id, t, duration, placement)`; falha mantém a folha e avisa.
- **Decupar** (`decupar.dart`): aviso "Acha onde a cena muda e corta o clipe em todos os pontos — o mesmo detector do Scene Edit Detection." · Sensibilidade: Sensível (0,22) | Normal (0,35) | Só cortes secos (0,55) + explicação · "Decupar agora" (acao) → `decuparCamada` · "Só marcar na régua (revisar antes)" (campo) → `cortesDeCenaViramMarcas`. Rodando: caixa 44 `#212D3A` raio 8 com spinner e "Lendo o vídeo...". Resultado: "{n} corte(s) de cena feitos" (+Desfazer) / "{n} marca(s) de cena na régua" / "Nenhuma mudança de cena nesse trecho. Tente Sensível." (5 s).
- **Escolher pai** (`escolher_pai.dart`): menu "Vincular a": "Nenhum" `clear_circled` + nulos primeiro, depois as demais (ícone do tipo; a própria e descendentes apagadas). → `linkProperty(id, parent, pai, t)` / `unlinkProperty`; cena 3D também `setSceneCameraCompParent` (1 desfazer).
- **Legendas** (`legendar.dart`, folha grande "Legendas"): pílulas de divisão (`CaptionMode`) e de onde transcrever (`ModoDeTranscricao`: nuvem/aparelho; grava nos Ajustes) + explicação · botão principal "Transcrever" (`waveform`; "Transcrevendo..." com spinner) · status + "Continuar em segundo plano" · erro em perigo + chips "Tentar de novo" / "Usar o Whisper do aparelho" · "Legendas prontas: {n} falas." · "ou cole um SRT:" + campo (3–6 linhas, `#212D3A` raio 8, padding 12) · "Criar do SRT colado". A transcrição continua com a folha fechada.
- **Texto em caminho** (`texto_no_caminho.dart`): folha não modal, máx 50% da altura: Caminho (tipos) · [camada: lista de formas] · Raio 20–800 · Começo −180–180° · Abertura 10–360° (arco) · Deslizar −1000–1000 · Espaço −20–60 · Alinhar Acima/Sobre/Abaixo · Girar com a curva · Inverter o sentido.
- **Organizar** (`oficio.dart`): cabeçalho com ícone/nome da camada; RÓTULO (12 cores, círculos 30, visto contrastado; "nenhum" `clear_circled` 22) · Solo · Tímida · Bloquear · Motion blur (switch + dica 10 sp).
- **Loop de keyframes** (não modal): Propriedade (Posição/Escala/Rotação/Opacidade) · (< 2 marcas: aviso) · Loop: Sem loop/Ciclo/Vai-e-volta/Deslocado/Continuar + explicação · "Inverter no tempo".
- **Colar estilo** (`acoes_de_midia.dart`): lista de categorias possíveis (linha 43, ícone 18, visto à direita) + [Cancelar | Colar] (CupertinoButtons raio 8, 15 sp) → `colarEstilo` → toast "Estilo colado".
- **Informações da mídia**: linhas rótulo (flex 2, `propriedade`) × valor (flex 3, `valor`, à direita).
- **Extrair o áudio**: toast "Extraindo o áudio…" → "Áudio extraído para uma camada própria" + Desfazer / "Este vídeo não tem áudio para extrair".
- **Importação 3D** (`importacao_3d.dart`): etapas "Baixando… / Preparando o modelo… / Preparando as texturas… / Importando… / Finalizando…"; diálogo Cupertino "Modelo pesado" (triângulos/texturas/memória antes → depois, materiais/animações/ossos, aviso de licença ND) com "Otimizar automaticamente" (padrão) / "Importar original" / "Cancelar".

### 5.12 Primitivas de menu e folha (DS)

- **`mostrarAureaMenu`** (`core/ds/aurea_menu.dart`): `showGeneralDialog` com véu `#0A0E13` 25%, 100 ms; menu de **250** de largura, fundo `elevado #1B2530`, raio 8, padding vertical 4, sem sombra/borda; título opcional `secao` (padding 15/6/15/4); item 40: barra de marca 4×24 destaque (marcado) · 10 · ícone 20 · 10 · rótulo 13 sp (destaque se marcado, perigo se destrutivo, secundário 50% se desabilitado) · 10; apertado = `campoAlto`. Posição: abaixo da âncora se couber (`top = âncora.bottom + 4`), senão acima; alinhado pela direita da âncora, preso a 8 dp das bordas; altura máx `min(450, tela − 16)`. Transição: fade + escala 0,96→1 ancorada em topRight.
- **`mostrarAureaFolha`** (`core/ds/aurea_bottom_sheet.dart`): `showModalBottomSheet(isScrollControlled, enableDrag: false, useSafeArea)`, fundo `painel #151C24`, raio superior 13,5, véu `#0A0E13` 35% (modal) ou 0% (não modal); entrada 100 ms (200 ms `grande`) decelerate, saída 100 ms t². Cabeçalho com título: altura 44 = [22 · título `titulo` · ações · ✕ `xmark` 20 secundário (alvo 44×38) · 6]; sem título: 10 dp de respiro.
- **`AureaChip`**: altura 28, padding h 12, raio 100, fundo campo (ativo `#1D3A55`), texto 12 sp (ativo destaque).
- **`AureaToggle`**: `CupertinoSwitch` trilho ativo `acao`, inativo `campoAlto`.
- **Toast** (`core/ui/snack.dart`): `SnackBar` flutuante Material, um por vez, fecha por timer próprio (duração + 250 ms); padrão 4 s; "motivo" 1,5 s; com ação ("Desfazer", "Desbloquear") ou ícone de fechar.
- **`pedirNome`**: `CupertinoAlertDialog` com campo autofocus; Cancelar / OK; vazio = cancelado.

---

## 6. ⚙ Projeto (ajustes do projeto) — `ui/shell/ajustes_do_projeto.dart`

Folha grande (200 ms) com título **"Projeto"**, altura máx 82% da tela, `ListView`
padding 22/4/22/15. Lê o projeto por conta própria. Cada escolha = 1 passo de desfazer.
Modo Pro está sempre ligado (`application/ui/pro_mode.dart`) → todas as seções aparecem.

1. **Linha do nome**: `pencil` · nome (conteúdo) · "Toque para renomear" · seta → diálogo "Nome do projeto" → `renameProject`.
2. **COMPOSIÇÃO**: Proporção `16:9 | 9:16 | 1:1 | 4:5 | 4:3` (`setComposition(aspectRatio)`) · Resolução `HD 720p | Full HD 1080p | QHD 1440p | 4K 2160p` (`resolutionHeight`) · Quadros `24 | 30 | 60` (`fps`) · Fundo (linha de cor → seletor de cor **sem alfa**, ao vivo, 1 desfazer; `setBackgroundColor`) · texto "{largura} × {altura} · {s} s".
3. **PRÉVIA**: Casca de cebola `Desligada | 1 quadro | 2 quadros` (`onionSkinProvider`, sessão).
4. **GUIAS**: Áreas seguras (switch; "Margens de título e ação na prévia") · Colunas `Sem | 2 | 3 | 4 | 6 | 12` · Adicionar guia vertical (x = largura/2; subtítulo "{v} vertical, {h} horizontal") · Adicionar guia horizontal (y = altura/2) · [se houver] Limpar guias (perigo).
5. **MOTION BLUR DA COMPOSIÇÃO**: switch Motion blur (subtítulo "Obturador {a}° · {n} amostras" ou "Desligado (as camadas com motion blur só borram com isto ligado)"); ligado: Obturador `90° | 180° | 270° | 360°` · Amostras `8 | 16 | 32`.
6. **PALETA DO PROJETO**: uma linha por cor (nome, "Toque para trocar a cor · segure para tirar", amostra 34×22 raio 4) · Adicionar cor à paleta `add_circled` (nome "Cor {n}" → cor).
7. **PROPRIEDADES EXPOSTAS (TEMPLATE)**: vazio "Nenhuma propriedade exposta" / "Exponha um parâmetro para quem usar este projeto como template."; itens "{grupo} · {propriedade}" com `minus_circle` perigo → `unexposeProperty`.
8. **DADOS (CSV)**: "Carregar CSV" / nome do arquivo ("Colunas viram fontes para textos (vincular no painel do texto)" / "{c} colunas · {l} linhas · toque para trocar") → seletor `.csv/.txt` → `setDataSource` + `applyDataBindings` (1 desfazer); erro dentro da folha "Não foi possível ler este arquivo."; [se há dados] Remover dados (perigo).
9. **AJUDA**: Como usar o editor `question_circle` ("Guia rápido, com busca") · Diagnóstico na tela `waveform_path_ecg` (switch; "Marcha, composições por segundo, memória e o motor 3D.").

Componentes: `LinhaDeAjuste` (linha inteira tocável; min 43; padding v 6; ícone 18 em
destaque (perigo/inerte conforme) + 10; título 13 sp; subtítulo 11 sp altura 1,3 até 2
linhas; fim = switch / seta `chevron_right` 13 / enfeite); `_LinhaDeEscolha` (rótulo na
coluna de 76, 2 linhas, alinhado à 1ª fileira de pílulas; pílulas em `Wrap` gap 6 que
**quebra linha**; padding vertical (44−28)/2 = 8; tocar na atual não faz nada);
`AureaSection` não recolhível (título 30 dp MAIÚSCULAS).

Variante A.01: `shell/project_settings_sheet.dart@aba36bb` (475 linhas), mesma
finalidade, visual antigo.

---

## 7. Ícones e fontes

- **Todos os ícones da área são `CupertinoIcons`** (pacote `cupertino_icons` 1.0.9; fonte `CupertinoIcons.ttf` em `%LOCALAPPDATA%\Pub\Cache\hosted\pub.dev\cupertino_icons-1.0.9\assets\CupertinoIcons.ttf`, licença MIT). Exceções Material: `Icons.add` (o "+"), `Icons.logout`/`more_vert`/`zoom_in`/`zoom_out`/`center_focus_strong`/`fullscreen_exit`/`chevron_left` (A.01), e na galeria `grid_view`, `arrow_drop_down`, `play_arrow`, `photo_outlined`, `cloud_download_outlined`, `broken_image_outlined`, `more_horiz`.
- Nenhum asset de ícone próprio nesta área (sem PNG/SVG).
- **Recomendação Compose**: empacotar `CupertinoIcons.ttf` em `res/font` e desenhar por codepoint (`Text(String(Character.toChars(cp)), fontFamily = cupertino)`), ou gerar `ImageVector`s. Codepoints dos ícones usados nesta área:

```
add_circled f48a  arrow_2_squarepath f4e6  arrow_down_right_square f4f7  arrow_down_to_line f4fb
arrow_left_right f500  arrow_left_right_square f503  arrow_left_to_line f507  arrow_left_to_line_alt f508
arrow_right_arrow_left f50b  arrow_right_arrow_left_square f50e  arrow_right_to_line f514
arrow_right_to_line_alt f515  arrow_turn_left_down f519  arrow_turn_up_right f51e  arrow_up_arrow_down f51f
arrow_up_down_square f52d  arrow_up_left_arrow_down_right f386  arrow_up_to_line f53d
arrow_uturn_left f544  arrow_uturn_right f549  backward_end f578  backward_end_alt f579
bold_italic_underline f591  book f3e7  bookmark f3e9  bookmark_solid f3ea  camera_fill f3f6
camera_viewfinder f5b9  captions_bubble f5be  captions_bubble_fill f5bf  chart_bar_alt_fill f8b7
checkmark_alt f8c1  checkmark_circle_fill f3ff  checkmark_square f5cf  checkmark_square_fill f5d0
chevron_down f5d5  chevron_left f3d2  chevron_right f3d3  chevron_right_2 f5e0  chevron_up f5e5
circle f401  circle_fill f400  circle_grid_3x3 f5ec  circle_grid_hex f5ee  circle_lefthalf_fill f5f0
clear/xmark f404  clear_circled/xmark_circle f405  clock f4be  cloud_download f8c4  cloud_sun f60e
color_filter f8c8  crop f618  cube f61a  cube_box f61b  cube_box_fill f61c  cube_fill f61d
decrease_indent f61f  delete/trash f4c4  delete_left f621  device_phone_portrait f8cf
doc_on_clipboard f632  doc_on_doc f634  doc_text f638  drop_fill f8d9  ellipsis f46a  eye f424
eye_fill f425  eye_slash f662  film f66b  flag f42c  folder f434  folder_badge_plus f678
folder_fill f435  forward_end f67f  forward_end_alt f680  fullscreen f386  fullscreen_exit f37d
gear/gear_alt f43c  gear_alt_fill f43d  grid f6a5  increase_indent f6cc  info_circle f44c
lightbulb f6dd  line_horizontal_3 f6e1  link f6e5  link_circle_fill f6e7  lock f4c8  lock_open f6fa
metronome f70b  minus f70f  minus_circle f463  move f8f8  music_note f46b  music_note_2 f46c
nosign f727  paintbrush f72e  paintbrush_fill f72f  pause_circle f736  pause_fill f478  pencil f37e
pencil_outline f73d  photo f767  photo_fill f768  photo_on_rectangle f76a  play_circle f76f
play_fill f488  plus f489  plus_square f77e  plus_square_on_square f781  question_circle f78f
rectangle_arrow_up_right_arrow_down_left f79e  rectangle_dock f7a3  rectangle_grid_1x2 f7aa
rectangle_grid_1x2_fill f7ab  rectangle_on_rectangle f7b0  rectangle_split_3x1 f7b3
rectangle_stack f3c9  repeat f7bf  rhombus f7c2  scissors f7c9  scissors_alt f905  scope f7ca
scribble f7cb  slider_horizontal_3 f7dc  slider_horizontal_below_rectangle f7dd
smallcircle_circle f7df  snow f7e7  sparkles f7e8  speaker_2 f7eb  speaker_slash f7ee
speedometer f7f5  square f7f8  square_arrow_up f4ca  square_grid_2x2 f804  square_grid_3x2 f806
square_grid_4x3_fill f808  square_on_circle f80c  square_on_square f80d  square_split_2x2 f813
square_stack_3d_down_right f817  square_stack_3d_down_right_fill f818  suit_diamond f831
suit_diamond_fill f832  table f844  tag f48c  text_justify f857  textformat f85c
textformat_alt f860  timer f868  videocam f4cc  videocam_fill f4cd  viewfinder f88d
wand_rays_inverse f891  wand_stars f892  waveform f894  waveform_path_ecg f89a  wind f89e
```

- Texto: fonte do sistema. Números de tempo/valor com `FontFeature.tabularFigures()` → em Compose `fontFeatureSettings = "tnum"`.
- Todo texto de UI passa por `AppText` (tradução por chave pt-BR em `core/l10n/translations.dart`, 10 idiomas); conteúdo do usuário (nomes) usa `Text` direto. Em Compose: pt-BR como fonte das strings.

---

## 8. Animações e transições

| O quê | Duração / curva | Onde |
|---|---|---|
| Barra ↔ painel na base (a timeline encolhe/cresce junto) | `AnimatedSize` 100 ms `decelerate`, alinhado ao topo | editor_shell.dart:551 |
| Folha pequena (menus em folha, alinhar, congelar…) | entra 100 ms decelerate; sai 100 ms t² | aurea_bottom_sheet.dart:115 |
| Folha grande (adicionar, ⚙ Projeto, legendas) | entra 200 ms decelerate; sai 100 ms t² | idem |
| Menu flutuante (`AureaMenu`) | 100 ms: fade + escala 0,96→1 (origem topRight); véu 25% | aurea_menu.dart:200 |
| Troca de aba na folha de adicionar | `AnimatedSwitcher` 100 ms (entrada decelerate, saída t²), conteúdo empilhado no topo | adicionar.dart:167 |
| Toque (`Tocavel`, todos os botões) | escala 0,965 (desce 90 ms / volta 220 ms `easeOutCubic`) + opacidade 0,82 (60 / 180 ms `easeOut`); sem ripple; interrompível | core/ui/tocavel.dart |
| Alça ativa no palco | raio 5→6 (escala) / 0,55→0,62 do pegador de giro, sem animação (troca de estado) | alcas_do_palco.dart |
| Encaixe | aparece/some junto com o encaixe + háptico leve | edicao_no_palco.dart:713 |
| Destaque de ferramenta/aba ativa | troca de cor sem animação | aurea_toolbar_button.dart |
| Háptico | `selectionClick` ao abrir/fechar painel por ferramenta e em itens de menu em folha; `lightImpact` no "+", dividir, duplicar; `mediumImpact` em toque longo do transporte | vários |
| Tela cheia | sem animação; troca de modo do sistema (immersiveSticky) | editor_screen.dart |

Variante A.01: faixa de nível no transporte (animada pelo relógio), galeria de efeitos com
folha Material padrão (véu preto 54%).

---

## 9. Bugs, hacks e riscos (arquivo:linha)

**Casca / layout**
1. `ui/shell/editor_shell.dart:366-379` — em tela cheia não há botão de sair na tela; só o menu do olho ou o Voltar (A.01 tinha círculo de 40 dp no canto). Descoberta ruim.
2. `ui/shell/editor_shell.dart:560-567` — o "+" (73 dp) com camada/lote escolhido fica em `bottom 63`, por cima do canto inf-dir da timeline (~79×79 dp), cobrindo clipes e losangos.
3. `ui/shell/editor_shell.dart:450` + `widgets/preview_stage.dart:957` — `AvisoDeRascunho` (top 6, right 8) e o chip de resolução (top 4, right 4) ocupam o mesmo canto: sobrepõem-se durante a reprodução.
4. `ui/shell/barra_do_topo.dart` — a barra não mostra a camada escolhida nem migalhas de grupo (regressão vs. A.01 `_TrilhaDeGrupos`); dentro de grupo só o Voltar sai.
5. `ui/shell/editor_shell.dart:339` — hack `resizeToAvoidBottomInset: ModalRoute.isCurrent` para não reencolher sob folhas.
6. `ui/shell/editor_shell.dart:654-672` — a prévia reserva `57` da barra mesmo com painel de 200 aberto; em telas baixas (paisagem) prévia cai a 120 dp e a timeline a ~47 dp (só régua). Não há layout largo na HEAD (A.01 tinha).
7. Três portas para ⚙ Projeto (topo ⚙, barra do projeto "Projeto") e duas para "+" (barra do projeto "Adicionar" + botão flutuante) — redundância proposital, mas ocupa espaço.

**Transporte**
8. `ui/shell/barra_de_transporte.dart:197-231` — `FittedBox(scaleDown)` encolhe os botões (e o alvo de toque) abaixo de 40 dp em larguras < 360.
9. `application/ui/opcoes_de_visualizacao.dart:33,70` — `aberta`/`alternarColuna` são código morto na HEAD (a coluna de visualização não existe mais).

**Palco / prévia**
10. `widgets/preview_stage.dart:1310,1327` — áreas seguras e guias usam `strokeWidth 2` em px da composição (não divididos pela escala, ao contrário da linha de encaixe): num celular (1080→~180–320 dp) viram 0,3–0,6 dp, quase invisíveis.
11. `widgets/faixa_de_bloqueio.dart:66-84` — botão "Desbloquear": texto `#F7F9FB` sobre `#6FAED9` ≈ 2,2:1 (reprova contraste) e alvo de ~22 dp de altura.
12. `ui/palco/gestos_do_palco.dart:187` — folga de arrasto = `kTouchSlop` (18 dp): em Compose o `touchSlop` padrão é ~8 dp; replicar 18 para manter "tocar para escolher nunca move".
13. Sem indicador de zoom na HEAD (A.01 tinha pílula "150%"): com a vista aproximada não há pista; volta só com dois toques no vazio ou "Ajustar à tela".
14. `widgets/preview_stage.dart:963` — seletor de resolução usa `PopupMenuButton` Material (fora do padrão Cupertino/DS do resto).

**Barras / menus / folhas**
15. `ui/toolbar/escolher_pai.dart:56` — sentinela `'\0nenhum'` com **byte NUL literal** no fonte: o git trata o arquivo como binário (`Bin 3652 -> 3598 bytes`) e o grep também. Em Kotlin usar `sealed class`/`null`.
16. `ui/toolbar/alinhar.dart:109,119` — "Esquerda"/"Direita" usam `rectangle_grid_1x2(_fill)`, ícones que não representam alinhamento (A.01 usava `arrow_left_to_line`/`arrow_right_to_line`).
17. `ui/toolbar/congelar.dart:61` — texto sem acentos "Leve o cabecote para dentro de um clipe de video" (e fora do padrão das outras frases).
18. `ui/toolbar/menu_do_projeto.dart:318` — `formatTimecode(tempo, 30)` fixa 30 fps em vez do fps do projeto.
19. `ui/toolbar/galeria.dart:470-505,627-673` — `IconButton`s com `minWidth 34/36` numa linha de 32 dp: alvos < 44 dp; mistura Material (`InkWell`, `PopupMenuButton`, `Icons.*`) com o DS Cupertino.
20. `ui/toolbar/galeria.dart:626` + `adicionar.dart:241-272` — "Recentes" duplicado: aba do trilho da folha **e** botão relógio/álbum virtual dentro da galeria.
21. `ui/toolbar/adicionar.dart:76` — folha de 251 dp: sobram ~183 dp para o conteúdo e ~151 dp para a grade da galeria (≈1,3 fileira de miniaturas de ~115 dp).
22. `ui/toolbar/linha_de_som_recente.dart:119` — nome de arquivo do usuário passa por `AppText` (lookup de tradução em conteúdo); `CircularProgressIndicator` Material.
23. `ui/toolbar/oficio.dart:448,468-495` — toques em Rótulo/Solo/Tímida/Bloquear/Motion blur não usam `runAsOneUndo`; toques rápidos (< 450 ms) coalescem com a edição anterior num só desfazer.
24. `application/ui/pro_mode.dart:11-13` — `set(bool)` ignora o argumento (sempre `true`): toggle morto.
25. `core/ui/pedir_nome.dart:121` — `TextEditingController` descartado logo após o `await`, enquanto o diálogo ainda anima a saída (risco clássico de "used after dispose").

**Variante A.01 (se for a escolhida)**
26. `context/context_sheet.dart@aba36bb:120-145` — botão de tela cheia na faixa de 8 dp: alvo **40×8 dp**, ícone 10 dp.
27. `cromo_editor.dart@aba36bb:236-241,1168-1169` — Exportar e o play em repetição pintados em `acao #245D8C` sobre `#0F141A` (contraste ≈ 2,6:1).
28. `cromo_editor.dart@aba36bb:685` — botões da barra do lote com 30 dp de largura.
29. `cromo_editor.dart@aba36bb:1099` — `CromoEditor.apagado.withValues(alpha:.25)` *substitui* o alfa (branco 25%), não multiplica — ok visualmente, mas o nome engana.

---

## 10. Mapeamento Flutter → Compose e comandos do motor

Nomes de comando = métodos de `EditorController` (`application/editor_controller.dart`),
usados só como referência semântica para o motor novo. `t` = tempo do cabeçote.

| Flutter (arquivo) | Composable proposto | Estado lido | Ações → comando |
|---|---|---|---|
| `EditorShell` (ui/shell/editor_shell.dart) | `EditorScreen` / `EditorScaffold` (Column + `BoxWithConstraints`; `BackHandler` com a ordem do §1.5) | seleção, lote, modoSelecionar, painelAberto, telaCheia, aspecto | abrir/fechar painel; desselecionar; sair (captura miniatura) |
| `alturaDaPreviaPara` | função pura `previewHeight(h, w, aspect)` | — | — |
| `_AreaDaTimeline` | `TimelineArea` (Box: Column[Timeline, AnimatedContent/animateContentSize(100ms)] + FAB) | idem | — |
| `BarraDoTopo` | `EditorTopBar` (HEAD) ou `ProjectTopBar`/`LayerTopBar`/`BatchTopBar` (A.01) | nome do projeto / camada | renomear projeto `renameProject`; exportar (`exitAllGroups` + navegar); A.01: `duplicateLayer`, `removeLayers`/`rippleDeleteLayer`, `renameLayer` |
| `_Previa` + `PreviewStage` (chrome) | `PreviewPane` (Box; `AndroidView`/`SurfaceView` do motor + `Canvas` overlay) | zoom, pan, resolução, opções de visualização | resolução da prévia (sessão) |
| `CompositionFrame` / `compositionRect` | `compositionRect()` + `Modifier.clipToBounds()` | tamanho do projeto | — |
| `_GuidesPainter`, `_GradeDoPalcoPainter` | `GuidesOverlay` (`Canvas`) | guias, colunas, áreas seguras, grade, encaixe | — |
| `CamadaDaSelecao` / `PintorDaSelecao` | `SelectionOverlay` (`Canvas`, `drawPath`/`drawCircle`) | seleção, lote, travada, alça ativa | — |
| `ReconhecedorDoPalco` + `ArbitroDoPalco` | `Modifier.pointerInput { awaitEachGesture { … } }` com uma máquina de estados idêntica (touchSlop 18 dp, alça 4 dp) | — | — |
| `EdicaoNoPalco` | `StageEditController` (classe Kotlin) | projeto, tempo | mover `editPosition(id,t,pos)`; escala `editScaleUniform`/`editScaleX/Y`; giro `editRotation`; forma `editShapeParam`; 1 gesto = `beginGesture`/`endGesture`; selecionar camada (estado UI) |
| `AvisoDeRascunho`, `DiagnosticoDaPrevia` | `DraftBadge`, `DevHud` | tocando, métricas | — |
| `FaixaDeBloqueio` | `LockedBanner` | travada | `toggleLocked(id)` |
| `BarraDeTransporte` | `TransportBar` (Row com 3 colunas de pesos iguais) | canUndo/Redo, tocando, tempo, duração, seleção | `undo`, `redo`, `splitLayer(id,t)`, `stepFrame(±1)`, `seek(t)`, `toggle`, `loop` |
| `_Infobar` | `InfoBar` | `infobarProvider` | — |
| `irParaOTempo` / `parseTimecodeInput` | `GoToTimeDialog` + `parseTimecode()` | fps, duração | `seek(t)` |
| `BarraContextual` + `AureaToolbarButton` | `ContextToolbar` (`LazyRow` ou `Row` com pesos; regra dos 64 dp) + `ToolbarButton` | ferramentas, ativo | — |
| `ferramentasDa` (contrato.dart) | `toolsFor(layer)` (sealed `Tool.OpenPanel`/`Tool.Action`) | tipo da camada | `splitLayer`, `ativarTexto3D`, abrir menu |
| `BarraDoProjeto` / `acionarProjeto` | `ProjectToolbar` | nº camadas, tem som, tem cópia | `colarCamada(t)`, modo Selecionar, abrir folhas |
| `BarraDoLote` / `acionarLote` / `executarMaisDoLote` | `BatchToolbar` + `BatchMoreMenu` | multiseleção | `groupLayers`, `duplicarCamada`×n, `alignSelection`, `cascadeSelection`/`linkCascadeSelection`, `linkProperty(parent)`, `removeLayers`, `dividirLote`, `aparaInicio/FimDoLote`, `estenderLoteAteOCabecote`, `moverLoteAteOCabecote`, `alinharInicios/FinsNoTempo`, `distribuirNoTempo`, `copiarCamadas`, `colarCamadas`, `pasteEffects`, `toggleHidden`, `toggleLocked`, `reorderLayers`, `agruparComForma` |
| `_BotaoAdicionar` | `AddFab` | painel aberto, seleção | abrir folha |
| `FolhaDeAdicionar` + `adicionar_acoes.dart` | `AddSheet` (`ModalBottomSheet` sem drag, 251 dp) | categoria, recentes | `addTextLayer`, `addTexto3D`, `addShapeLayer`, `addSvgLayers`, `addElement3DLayer`+rotação, `addImportedModel3D`, `addNullLayer`, `addCameraLayer`, `addAdjustmentLayer`, `addParticulasLayer`, `addEmptyGroup`, `groupLayers`, `addImageLayer`, `addVideoLayer`/`importVideoAwaitingDuration`, `importAudioFile`, `addAudioRecente`, desenho livre (estado UI) |
| `GalleryPanel` | `GalleryPane` (MediaStore) | álbum, marcadas | importar mídia (lote/sequência) |
| `LinhaDeSomRecente` | `RecentSoundRow` (ExoPlayer só no 1º play) | — | `addAudioRecente` |
| `mostrarMenuDaCamada` / `executarAcaoDaCamada` | `LayerMenu` (Popup próprio 250 dp) + `LayerMoreMenu` | camada, clipboard | todos os itens do §5.7 (`duplicarCamada`, `joinWithNeighbour`, `splitLayer`, `removeLayers`/`rippleDeleteLayer`, `renameLayer`, `toggleHidden/Locked/Solo`, `reorderLayer`, `copiarCamada`, `colarCamada`, `copiarKeyframes`, `colarKeyframes`, `copiarEstilo`, `colarEstilo`, `groupLayer`, `enterGroup`, `ungroupLayer`, `setLayerLabel`, `trimLayerStart/End`, `moveLayer`, `freezeFrame`, `copyEffects`, `pasteEffects`, `updateAudioSpec(muted)`, `insertLayerAt`, `overwriteLayerAt`, `liftTimeRange`, `extractTimeRange`, `autoReframeLayer`, `stabilizeLayer`, `toggle3D`, `toggleLayerMotionBlurReal`, `recortarPelaDeBaixo`, `setMatte`, `encaixarNaComposicao`, `espelharCamada`, `definirFormaDoGrupo`, `closeTimelineGaps`, `selectNeighbor`) |
| `mostrarMenuDoProjeto` | `ProjectMenuSheet` | loop, modo de prévia, thumb, intro/final, marcas, cronômetro, ímã | `aparaProjetoNoCabecote(t)`, `definirQuadroDaMiniatura`, `marcarFimDaIntroducao`, `marcarInicioDoFinal`, `toggleMarker`, cronômetro, ímã |
| `menuDasMarcas` | `MarkersSheet` | marcas, bpm, I/O | `toggleMarker`, `markerAfter`→`seek`, `cutAtMarkers`, `distributeAtMarkers`, `batidasViramMarcadores`, `clearMarkers`, I/O (sessão) |
| `_menuDoModo` (transporte) | `PreviewModeMenu` | opções de visualização | estado UI; zoom 1; tela cheia |
| `showAlignSheet` / `abrirCascata` | `AlignSheet` (não modal) / `CascadeSheet` | seleção | `alignSelection`, `distributeSelection`, `spaceSelection`, `cascadeSelection`, `linkCascadeSelection` |
| `showFreezeSheet` | `FreezeSheet` | — | `freezeFrame(id,t,dur,placement)` |
| `showDecuparSheet` | `SceneDetectSheet` | — | `decuparCamada(id, limiar)` / `cortesDeCenaViramMarcas` |
| `escolherPai` / `vincularAoPai` | `ParentPickerMenu` | pais possíveis | `linkProperty/unlinkProperty(parent)`, `setSceneCameraCompParent` |
| `showCaptionCreationSheet` | `CaptionsSheet` | transcrição em andamento | `addCaptionLayer(falas)`, `addCaptionLayerFromSrt` |
| `showTextPathSheet` | `TextPathSheet` (não modal, 50%) | textPath | `updateTextPath` |
| `showOrganizeSheet` / `showLoopSheet` | `OrganizeSheet` / `KeyframeLoopSheet` | meta, loops | `setLayerLabel`, `toggleSolo/Shy/Locked`, `toggleLayerMotionBlur`, `setPropertyLoop`, `reversePropertyInTime` |
| `mostrarColarEstilo` / `mostrarInfoDaMidia` / `extrairAudioComAviso` | `PasteStyleSheet` / `MediaInfoSheet` / função | — | `colarEstilo`, `extrairAudioDaCamada` |
| `concluirImportacao3D` / `AvisoDeModeloPesado` | `Import3DFlow` + `HeavyModelDialog` | — | `addImportedModel3D`, `addModel3D` |
| `showProjectSettingsSheet` | `ProjectSettingsSheet` | projeto | `renameProject`, `setComposition(aspect/resolução/fps)`, `setBackgroundColor`, `setGuides`, `addGuide`, `setMotionBlur`, `setPaletteColor`, `removePaletteColor`, `unexposeProperty`, `setDataSource`+`applyDataBindings` |
| `AureaMenu`/`mostrarAureaMenu` | `AureaPopupMenu` (Popup com `PopupPositionProvider` próprio) | — | — |
| `mostrarAureaFolha` | `AureaSheet` (`ModalBottomSheet(sheetGesturesEnabled=false)` + tween 100/200 ms) | — | — |
| `Tocavel` | `Modifier.tocavel()` (`indication = null`, `graphicsLayer` scale/alpha por `interactionSource`) | — | — |
| `AureaSnack`/`showReasonToast` | `SnackbarHost` flutuante, 1 por vez, timer próprio | — | ação "Desfazer" → `undo` |

---

## Apêndice A — Galeria de efeitos sobre o editor (tela96b.png, A.01)

Só o "chrome" (o conteúdo da galeria é de outra área): folha modal com véu preto ≈54%
(`#0F141A`→`#07090C`); topo da folha em 888 px = **338,3 dp** da borda da tela (289,5 dp
abaixo do topo útil), altura 552 dp até a barra de navegação; raio superior ≈18 dp; fundo
`#0F141A`. Título "Efeitos" (altura de maiúscula 14,1 dp ≈ 20 sp, bold) a 15 dp da
esquerda; campo de busca Cupertino (`#272B33`, ~36 dp de altura, margens 14 dp,
placeholder "glow, rgb split, pixelate..." `#9D9EA7`, ícone lupa); fileira de chips de
categoria "Cor 6", "Estilizar 13", "Distorcer 8", "Diversos 2", "Glow e …" (fundo
`#212D3A`, ~31 dp de altura, gap 8 dp, rola); grade de 3 colunas (cartão ≈120 dp de
largura, gap 10 dp, margens 14 dp; miniatura ≈111 dp com gradiente `#151B23`→`#6676A1`,
estrela no canto sup-dir, selo "custo 2"; nome 16 sp bold + categoria 14 sp muted;
passo vertical ≈183 dp).
