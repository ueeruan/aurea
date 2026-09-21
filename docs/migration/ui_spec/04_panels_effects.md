# 04 — Painéis, inspector, Efeitos, keyframes, controles por tipo e Exportação

Inventário para portar a UI aprovada do Aurea (Flutter) para Jetpack Compose.
Área: painéis de baixo (inspector), navegador e editor de efeitos, keyframe nas
propriedades, controles por tipo (transformar, cor, texto, forma, áudio,
máscara, tempo...), exportação. Tudo em dp (texto em sp). Cores em hex do tema
padrão "Aurea" com o nome do token entre crases.

---

## 0. Fontes, método e as DUAS gerações visuais

### 0.1 O que foi lido

- Projeto antigo: `C:\Users\SnyX\Documents\Projetos - Claude\Aurea` (somente
  leitura). Working tree = HEAD `959d725` (21/09/2026 12:56), limpo (só os
  PNGs de referência não versionados).
- Os 40 arquivos de `lib/src/features/editor/presentation/ui/paineis/`, os 10
  de `paineis/efeitos/`, o DS inteiro de `lib/src/core/ds/` (tokens,
  property row, slider, value field, teclado, keyframe button, effect card,
  seletor de cor, conta-gotas, section, panel, tabs, toggle, dropdown, menu,
  bottom sheet, chip, toolbar button, layer row), `core/ui/am_tick_ruler.dart`,
  `core/ui/tocavel.dart`, `core/ui/snack.dart`, `core/ui/pedir_nome.dart`,
  `core/theme/aurea_colors.dart` + `aurea_paleta.dart`, a casca
  (`ui/shell/editor_shell.dart`, `ui/shell/contrato.dart`) e
  `features/export/presentation/*.dart` + `export/domain/export_settings.dart`.
- Para explicar as âncoras visuais, lidos por `git show` (sem tocar no
  working tree) os arquivos da tag `antes-da-ui-nova`:
  `am/effects_panel.dart`, `widgets/linha_de_parametro.dart`,
  `widgets/fita_de_ajuste.dart`, `widgets/campo_de_valor.dart`,
  `widgets/rails_do_painel.dart`, `context/context_sheet.dart`,
  `context/effects/effect_gallery.dart`, `am/panel_chrome.dart`.

### 0.2 Tags que importam

| Tag / commit | Data | O que é |
|---|---|---|
| `antes-da-ui-nova` (`d8fcebc`) | 21/09 00:04 | UI ANTIGA. É a que aparece em `t2.png` e `tela96b.png` (ambos de 20/09 04:12). |
| `ui-nova-aprovada` (`e099989`) | 21/09 12:15 | UI nova aprovada pelo dono. |
| HEAD `959d725` | 21/09 12:56 | Aprovada + 2 commits: "Recursos que o redesign tinha perdido voltam, sem mexer na UI aprovada" e "Celular deitado: o painel tem um piso". |

**Consequência:** as âncoras visuais (t2 / tela96b) mostram o código ANTIGO.
O HEAD reimplementou o controle de efeitos "no desenho do app antigo"
(commit `741e766`, pedido do dono: *"deixe o controle de efeitos igual a do
app antigo"*), mas com os números do DS novo. Por isso este documento dá as
duas medidas onde elas divergem:

- **[A] = âncora** — o que está no screenshot, medido em pixel e confirmado
  no código da tag `antes-da-ui-nova`.
- **[H] = HEAD** — o código atual aprovado.

A seção 11 lista as decisões que o port precisa tomar entre [A] e [H].

Nuance: na tag antiga havia DUAS linhas de parâmetro. A do painel de
efeitos (`widgets/linha_de_parametro.dart`: chip 94 + fita + caixa 68 × 24)
é a do t2. A das folhas de contexto (`context/parameter_row.dart`,
`ParameterFrame`: losango 30 + rótulo 76 de uma linha 12,5 sp + caixa
74 × 34, 14 sp) é a que o HEAD copiou para o DS como `AureaPropertyRow` —
é a que os comentários do HEAD chamam de "o app antigo que os testadores
conheciam". As duas são "antigas"; só a primeira aparece nas âncoras.

### 0.3 Conversão e verificação das medidas

- Screenshots: 1080 × 2400 px. **dp = px / 2,625.** (As imagens foram exibidas
  a 900 × 2000; coordenada exibida × 1,2 = px original; × 0,4571 = dp.)
- Escala de fonte do aparelho = **1,0** (verificado): altura de versal de
  "Centro X" = 22 px = 8,4 dp → 12 sp Roboto; "Motion Tile" (ascendente)
  33 px → 17 sp; "Efeitos" 28 px → 14 sp. Todos batem com o código antigo.
- Toda cor medida no screenshot bateu EXATAMENTE com um token:
  `#151C24` (surface), `#0F141A` (bg), `#212D3A` (chip), `#273442` (border),
  `#6FAED9` (accent), `#AAB6C3` (muted), `#F7F9FB` (text), `#0A0E13`
  (stage). Riscos da régua = `#3A424C` = muted `#AAB6C3` a 25 % sobre
  `#151C24` (a conta confere).

---

## 1. Tokens (DS)

### 1.1 Cores — tema padrão "Aurea"

Fonte: `core/ds/tokens.dart:383-417` (`AureaCores`, papéis) →
`core/ui/am_colors.dart` → `core/theme/aurea_paleta.dart:216-244` (valores).
Sob o tema "Light" o EDITOR continua escuro (usa a paleta Aurea —
`AureaPaleta.editor`, `aurea_paleta.dart:147`).

| Papel (`AureaCores.*`) | Uso | Hex |
|---|---|---|
| `palco` | atrás da composição; veu de folha/menu | `#0A0E13` |
| `cromo` | barra do topo, transporte, timeline | `#0F141A` |
| `painel` | painel que sobe, folhas, teclado, seletor de cor | `#151C24` |
| `elevado` | cartão de efeito, menu, pronto na folha de detalhe | `#1B2530` |
| `campo` | caixa de valor, chip, dropdown, campo de texto | `#212D3A` |
| `campoAlto` | item de menu apertado, trilho do slider, toggle desligado | `#323D49` (= lerp(`#212D3A`,`#F7F9FB`,0,08)) |
| `texto` | texto principal | `#F7F9FB` |
| `textoSecundario` | rótulo de propriedade, dicas, ícones secundários | `#AAB6C3` |
| `destaque` | O ÚNICO destaque de estado ligado (aba, chip aceso, ✓, estrela) | `#6FAED9` |
| `destaqueApagado` | fundo de chip aceso, teclas de operador, botão principal de folha | `#1D3A55` |
| `acao` | preenchimento de ação (Exportar, "+", botão Aplicar, toggle ligado) | `#245D8C` |
| `sobreAcao` | texto sobre `acao` | `#F7F9FB` |
| `keyframe` | losango, setas, NÚMERO da caixa de valor [H] | `#A9D3EC` |
| `perigo` | apagar, erro | `#FF6B6B` |
| `selecao` | fundo de camada selecionada (AureaLayerRow) | `#123A63` |
| `cabecote` | cabeçote; centro da régua inativa [A] | `#FFFFFF` |
| hairline (`AmColors.hairline`) | divisor, borda de bolinha de cor | `#273442` |
| warning | aviso | `#FFC978` |
| success | — | `#4CD08A` |

Derivadas úteis (alfa sobre fundo): textoSecundario 60 % sobre `painel` =
`#6E7883` (losango apagado); 30 % = `#424A54` (seta desabilitada);
25 % sobre `#151C24` = `#3A424C` (risco fraco [A]); 18 % = `#303841`
(trilho da leitura de posição).

### 1.2 Os outros temas (mesmos papéis; o DS do Compose deve ler papéis, nunca hex)

| Papel | Aurea Dark | Midnight | OLED | Graphite |
|---|---|---|---|---|
| cromo (background) | `#0A0E13` | `#0B1020` | `#000000` | `#131416` |
| painel (surface) | `#0F141A` | `#111831` | `#0A0B0D` | `#1A1B1E` |
| elevado (panel) | `#151C24` | `#18213F` | `#121316` | `#222327` |
| campo (chip) | `#1B2530` | `#202B4D` | `#1A1C20` | `#2A2C31` |
| campoAlto | `#2D3640` | `#313B5B` | `#2C2E31` | `#3A3C41` |
| textoSecundario | `#A3AFBC` | `#A5AECF` | `#9AA3AE` | `#A9ACB3` |
| destaque (accent) | `#6FAED9` | `#8EA2FF` | `#6FAED9` | `#8DB9DA` |
| destaqueApagado | `#19344D` | `#252F66` | `#172F46` | `#263A4A` |
| acao (primary) | `#245D8C` | `#3D4FB8` | `#245D8C` | `#3A6A94` |
| keyframe | `#A9D3EC` | `#C3CCFF` | `#A9D3EC` | `#C5DCEC` |
| hairline (divider) | `#212C38` | `#2A3660` | `#30343A` | `#33363C` |
| palco (stage) | `#06090C` | `#070A16` | `#000000` | `#0C0D0E` |

`texto` (`#F7F9FB`) e `AmColors.muted` (`#AAB6C3`) ainda são `const` no
código antigo (não mudam com o tema) — ver bug B-40.

### 1.3 Cores escritas à mão (fora de token) nesta área

| Onde | Hex | Uso |
|---|---|---|
| `core/ui/am_tick_ruler.dart:396,399` | `#43516A` / `#7485A3` | risco fraco / forte da régua [H] |
| `am_tick_ruler.dart:422` | `Colors.white` | indicador central da régua [H] (`accentCenter:false`) |
| rail antigo `rails_do_painel.dart:386` | `#434956` | losango/curva desabilitados [A] |
| seletor de cor `_XadrezPainter` | `#3A4150` / `#2A303B` | xadrez de transparência |
| `aurea_teclado_numerico.dart:420` | `#FF6B6B` | erro do editor de expressão |
| `miniatura_do_efeito.dart:110,125,142` | `#FFFFFF`, `#FF4D2D`, `#0F141A`→`#6A7BA8` | cartela das miniaturas de efeito |
| `borda_sombra.dart:745,750` | `#FFFFFF` / `AureaColors.bg` | cor das bordas novas (alternadas) |
| `estilo.dart:324,570,667` | `#000000`, `#000000`, `#8FD3FF` | contorno novo, caixa atrás, 2ª cor do degradê |
| `cor.dart:202` | `AureaCores.destaque` | cor inicial de "Cor por cima" — COR DE UI VIRANDO CONTEÚDO (bug B-21) |
| `snack.dart` | Material `SnackBar` | aviso (cores do tema Material) |

### 1.4 Dimensões (`AureaDims`, `core/ds/tokens.dart:16-349`)

| Token | Valor | Uso nesta área |
|---|---|---|
| `painel` | 200 | altura MÁXIMA do painel de camada |
| `painelGrande` | 326 | folha do editor de curva (316 + 10) |
| `cabecalhoDoPainel` | 38 | cabeçalho do AureaPanel |
| `abas` | 38 | barra de sub-abas |
| `margemDoPainel` | 22 | margem lateral de todo corpo de painel/folha |
| `topoDoPainel` | 15 | respiro no FIM da lista do painel (bottom padding) |
| `vaoDoPainel` | 6 | vão entre peças; margem inferior do cartão |
| `linhaDePropriedade` | 44 | altura da linha de propriedade [H] |
| `rotuloDaPropriedade` | 76 | coluna do nome [H] |
| `caixaDeValor` / `alturaDaCaixaDeValor` | 74 / 34 | caixa de valor [H]; altura de dropdown e botões de alinhamento |
| `losangoDaLinha` | 30 | coluna do losango à esquerda [H] |
| `setasDoKeyframe` | 36 | ‹ › à direita (18 + 18) [H] |
| `botaoDeKeyframe` | 64 | ‹◆› com largura fixa (painel Pontos) |
| `alcaDoDeslizante` / `trilhoDoDeslizante` | 25 / 2,5 | slider com faixa (AureaSlider) |
| `toqueDoDeslizante` | 44 | altura de toque do AureaSlider |
| `cabecalhoDoCartao` = `itemDeLista` | 37 | cabeçalho do cartão de efeito; item de lista |
| `blocoDePainel` | 57 | item de lista do catálogo/presets; bloco de ação |
| `barraDeFerramentas` | 57 | barra contextual que o painel substitui |
| `itemDeMenu` / `larguraDoMenu` / `alturaMaximaDoMenu` | 40 / 250 / 450 | AureaMenu |
| `barraDeMarcaDoMenu` | 4 | barra do item marcado |
| `raioXs/Sm/Md/Lg/Xl` | 1,5 / 3 / 4 / 5 / 8 | raios |
| `raioDaFolha` | 13,5 | topo das folhas do DS |
| `raioPilula` | 100 | chip |
| `iconeSm/Md/Lg/Xl` | 16 / 20 / 24 / 32 | ícones |
| `toqueMinimo` / `toqueConfortavel` | 40 / 44 | alvos |
| `e2..e20` | 2 4 6 8 10 15 20 | espaços |
| `regua` | 42 | régua da timeline (entra na conta da altura do painel) |
| `linhaDeCamada` | 34 | linha de camada (idem) |
| `timelineMinimaComPainel` | 110 = 42 + 2 × 34 | o que a timeline sempre mantém |

### 1.5 Tipografia (`AureaEstilos`, `tokens.dart:421-460`)

| Estilo | Tamanho / peso / cor | Uso |
|---|---|---|
| `titulo` | 14 sp w600 `texto` | título de painel e de folha |
| `propriedade` | 12,5 sp w400 `textoSecundario` | rótulo da linha [H]; avisos do painel |
| `valor` | 14 sp w700 `keyframe` `#A9D3EC`, algarismos tabulares | número da caixa de valor [H]; código hex da linha de cor |
| `rotulo` | 10 sp `textoSecundario` | rótulo de bloco/botão; linha 2 de listas; prefixo X/Y |
| `secao` | 11 sp w600 letterSpacing 0,3 `textoSecundario`, CAIXA ALTA | título de seção |
| `corpo` | 13 sp `texto` | item de menu, dropdown, nomes |

Âncora [A] (código antigo): chip do rótulo 12 sp w600 altura de linha 1,05;
número 13 sp w600 `accent #6FAED9` SUBLINHADO; título do cartão 17 sp w600;
cabeçalho "Efeitos" 14 sp w600; cabeça de grupo 12 sp w700 letterSpacing 0,6.
Fonte: Roboto (sistema); nenhuma família custom no cromo.

### 1.6 Movimento (`AureaMotion`, `tokens.dart:356-373`)

- `rapido` 100 ms (painel ↔ barra, menu, folha pequena, seta do cartão),
  `normal` 200 ms (folha grande, seletor de cor), `lento` 300 ms.
- Entrada `Curves.decelerate` (= 1−(1−t)²); saída `Cubic(1/3, 0, 2/3, 1/3)`
  (= t²). Nada de mola.
- Compose: `tween(100, easing = { 1-(1-it)*(1-it) })` na entrada,
  `tween(100, easing = { it*it })` na saída.

### 1.7 Feedback de toque e háptica

`Tocavel` (`core/ui/tocavel.dart:17-91`) é o átomo de todo toque:

- Apertado: escala 0,965 (descida 90 ms, subida 220 ms, `easeOutCubic`) +
  opacidade 0,82 (60 ms / 180 ms, `easeOut`). Sem ripple.
- `encolhe: 1` = só escurece (usado em linhas, cabeçalhos, itens de lista).
- `haptico: true` = `HapticFeedback.lightImpact()` na DESCIDA do dedo.
- Um `Tocavel` sem ação não entra na arena de gestos (não rouba o toque do pai).

Hápticas existentes nesta área (e só estas):

| Onde | Háptica |
|---|---|
| Losango: tocar para CRIAR marca (não ao remover) | `lightImpact` |
| Teclas do teclado numérico | `lightImpact` na descida |
| Botão principal de folha (Detectar/Aplicar), botão Exportar | `lightImpact` |
| Abrir painel pela barra contextual | `selectionClick` |
| "+" da casca, Entrar/Desagrupar grupo | `lightImpact` |
| Arrastar régua/valor | NENHUMA (sem tique por risco, sem háptica em limite) |

---

## 2. O container do painel de baixo

### 2.1 [H] Onde o painel mora na casca

`ui/shell/editor_shell.dart:457-567` (`_AreaDaTimeline`). Zonas de cima
para baixo: barra do topo 42 · prévia (altura fixa, 40 % da tela útil) ·
transporte 46 · zona da timeline (resto). Dentro da zona da timeline:

```
Column(
  Expanded( TimelineDoEditor ),
  AnimatedSize(100 ms, decelerate, topCenter)(
     painel aberto  -> SizedBox(height: alturaDoPainel, painel)
     lote           -> BarraDoLote
     camada escolhida -> BarraContextual (57)
     nada           -> BarraDoProjeto
  )
)
+ botão "+" (73, a 6 da borda) — ESCONDIDO enquanto há painel aberto
```

O painel NÃO sobe por cima da timeline: ele ocupa o lugar da barra
contextual, embaixo, e a timeline encolhe (animada pelo `AnimatedSize`).

**Altura do painel** (`editor_shell.dart:504-514`), com `altura` = altura
da zona da timeline:

```
piso        = 38 (cabeçalho) + 38 (abas) + 44 (uma linha) = 120
alturaPainel = min( max(0, altura − 42),
                    max(piso, min(200, altura − 110)) )
```

Num 1080 × 2400 (914 dp de altura, ~870 úteis), o painel fica em 200 dp.

**Regras de abertura** (`editor_shell.dart:69-92`, `contrato.dart:103`):

- Um painel por vez (`painelAbertoProvider: PainelId?`), sempre da camada
  selecionada; trocar a seleção fecha o painel.
- Abrir pausa a reprodução. Tocar a MESMA ferramenta da barra de novo fecha.
- Voltar do sistema (PopScope, `editor_shell.dart:240-282`), nesta ordem:
  consome folha persistente → sai da tela cheia → **fecha o painel** →
  sai da multisseleção → solta a seleção → sai do grupo → sai do editor.
- Tocar no vazio da timeline também fecha o painel (`_soltarSelecao`).

### 2.2 [H] Anatomia do `AureaPanel` (`core/ds/aurea_panel.dart:22-124`)

```
┌──────────────────────────────────────────────────────────┐ fundo `painel` #151C24, sem borda
│22│ Título (14 sp w600, 1 linha, …)   [ação 44×38]…  [✓ 44×38] │6│  altura 38
├──────────────────────────────────────────────────────────┤
│  aba · aba · aba  (opcional, AureaTabs, 38)               │
├──────────────────────────────────────────────────────────┤
│ corpo: ListView padding L22 T4 R22 B15  (ou `corpo` próprio)│  Expanded
└──────────────────────────────────────────────────────────┘
```

- ✓ = `CupertinoIcons.checkmark_alt` 20, cor `destaque`, alvo 44 × 38,
  chama `fecharPainel`. Não há seta "voltar" no cabeçalho [H].
- Ações do cabeçalho (`AcaoDoCabecalho`, `pecas_centrais.dart:111-138`):
  ícone 20 num alvo 44 × 38; cor `destaque` se `ativo`, senão
  `textoSecundario`.
- Painéis com rolagem própria usam `respiroDoPainel` =
  `EdgeInsets.fromLTRB(22, 4, 22, 15)` (`pecas_centrais.dart:161`).
- Aviso de painel sem controle (`AureaAvisoDoPainel`): padding vertical 10,
  texto `propriedade` (12,5 sp muted), várias linhas.
- Camada apagada com o painel aberto → `PainelSemCamada` (título + aviso
  "Esta camada não existe mais."). Tipo errado → `PainelDeTipoErrado`.

**Sub-abas `AureaTabs`** (`aurea_tabs.dart`): altura 38; ListView
horizontal com padding lateral 12 (= 22 − 10); cada aba com padding
horizontal 10: `[4] rótulo 12 sp (ativa w600 destaque / inativa w500
textoSecundario) [6] traço 16 × 2 raio 1 (destaque, alfa 0 se inativa)`.
Aba ativa não é tocável. Sem animação do traço. Sem scroll-to-active.

### 2.3 [A] O painel de `t2.png` (UI antiga) — medido

Medidas em px do screenshot → dp. Tela 411,4 dp de largura.

| Elemento | px (y ou x) | dp | Código antigo |
|---|---|---|---|
| Linha divisória topo do painel | y 1517–1519 (3 px) | 1 dp | `Border(top: hairline #273442)` (`context_sheet.dart:45`) |
| Cabeçalho "‹ Efeitos" | y 1520–1634 (115 px) | **44** | `titleHeight = 44`, fundo `surface #151C24` |
| Seta ‹ do cabeçalho | centro x 62 | 23,6 | `IconButton` 48 × 48, `Icons.chevron_left` 26, `text` |
| Título "Efeitos" | começa x 127 | 48 | 14 sp w600 `text`; `SizedBox(12)` no fim |
| Corpo do painel | y 1635–2336 | 267 dp visíveis | fundo `bg #0F141A` (`AmColors.panel`) |
| Trilho esquerdo (rail) | x 0–120 | **46** | `RailEsquerdo.largura = 46` |
| Lista de cartões | padding L2 T8 R12 B16 | — | `ReorderableListView(padding: 2,8,12,16)` |
| Cartão | x 126–1047, topo y 1656 | x 48–399 | fundo `#151C24`, **raio 14**, margem inferior 10, padding L10 T2 R6 B8 |
| Cabeçalho do cartão | 48 de altura | 48 | ▼ 13 `text` · 12 · nome 17 sp w600 · ⋯ 44 × 44 (ícone 22) · 🗑 44 × 44 (ícone 22) |
| ▼ | centro x 168 | 64 | `arrowtriangle_down_fill` / `_right_fill` 13 |
| "Motion Tile" | começa x 220 | 83,7 | = 58 + 13 + 12 |
| ⋯ / 🗑 | centros x 859 / 974 | 327 / 371 | alvos 44 × 44 colados à direita (padding R 6) |
| Linha de parâmetro | 126 px | **48** | `LinhaDeParametro.altura = 48` |
| Chip do rótulo | x 152–398, y 1934–2018 | **94 × 32** | raio 8; selecionado: fundo `#212D3A` |
| Régua (fita) | x ~420–853 | ~160 | `FitaDeAjuste`, altura 40 (48 − 8) |
| Riscos | passo 23,6 px, 3 px de largura, altura 63 px | **9 dp**, 1 dp, **24 dp** | `#3A424C`; bordas somem em 24 dp |
| Indicador central | 5 px, 63 px de altura | 2 × 24 | branco `#FFFFFF`; aceso `#6FAED9` na linha escolhida |
| Caixa de valor | x 854–1032, y 1819–1881 | **68 × 24** | raio 8, fundo `#212D3A`, 13 sp w600 `#6FAED9` sublinhado |
| Folga caixa → borda do cartão | 15 px | 6 | padding direito do cartão |

Vão chip → régua 8 dp; régua → caixa 8 dp. A régua e a caixa ficam
centradas verticalmente na linha de 48 (mesmo y 1819–1881).

### 2.4 [A] O trilho esquerdo (`widgets/rails_do_painel.dart`, tag antiga)

Coluna de 46 dp, fundo do corpo (`#0F141A`), três células `Expanded` de
alturas iguais (no t2: 267 / 3 = 89 dp cada; centros em y 667, 756, 845 dp):

1. **Voltar** `‹` (`Icons.chevron_left_rounded` 24, `text`; desabilitado
   = muted 28 %). Volta UM nível (para a grade de ferramentas da camada).
2. **Keyframe** — `_DiamondKeyframePainter` 22 × 22: losango em traço 1,6
   inset 2; dentro, "+" (sem marca aqui) ou "−" (há marca aqui), traços de
   7 × 1,5. Cor: desabilitado `#434956`; marca aqui `accent`; senão
   `#FFFFFF`. Mira o efeito ABERTO (ou o do parâmetro escolhido): o keyframe
   do efeito é UNIVERSAL — toca = `toggleEffectKeyframe(layer, effect, t)`
   (crava/remove TODOS os parâmetros no instante). Selo "AUTO" por baixo
   quando o keyframe automático está ligado: pílula `accent` raio 6, texto
   8,5 sp w800 `bg`, altura de toque 18, tocar DESLIGA.
3. **Curva** — `_CurveIconPainter` 20 × 20: caixa raio 4 traço 1,3 (cor a
   50 %) + curva S traço 1,5 ponta redonda. Cor: inativo `#434956`;
   animado `accent`; senão muted. Ativo só com o efeito animado; abre o
   editor de curva da primeira trilha animada do efeito.

No t2: ‹ branco, losango branco com "+" (efeito sem marca no cabeçote), curva
cinza (efeito não animado).

### 2.5 Navegação entre painéis ("pilha")

**Não existe pilha de painéis [H].** `painelAbertoProvider` guarda UM id.
Saltos entre painéis substituem o atual, e o voltar do sistema FECHA o painel
(não volta ao anterior):

| De | Para | Gatilho |
|---|---|---|
| Texto | Fonte | linha "Fonte" (`texto.dart:232-273`) |
| Texto (em camada Texto 3D) | Texto 3D | porta "Editar a palavra do Texto 3D" |
| Tempo | Efeitos | chip de efeito de tempo (`tempo.dart:104-111`) |
| Animar | Efeitos | porta "Animador de Texto (efeito)" |
| Forma (aba Cor) | Cor | porta "Mais opções de preenchimento" |
| Efeitos (vídeo) | Áudio | chip "Efeitos de áudio" |
| Catálogo (entrada Camera Tracker) | Rastrear | toque na entrada |
| Forma / Máscara | Pontos | lápis do cabeçalho / "Editar pontos" (`pontos.dart:36-93`) |
| Estilo › Fundo | (fecha e reabre Estilo) | ligar "Caixa atrás" cria camada e reseleciona o texto |

[A] tinha dois níveis: a grade de ferramentas da camada e a ferramenta
(‹ do cabeçalho e ‹ do rail voltavam para a grade). O HEAD trocou a grade
pela barra contextual (57) e o ‹ pelo ✓.

Por cima do painel abrem folhas (seção 8): catálogo de efeitos, detalhe do
efeito, seletor de cor, teclado numérico, editor de curva (não modal, 316),
"Animar sozinho" (não modal), gradiente vetorial (não modal), batidas,
pulsar, câmeras, elemento 3D, extrude, menus flutuantes. Presets e
Exportação são ROTAS de tela cheia.

### 2.6 Rolagem

- Corpo = uma `ListView` vertical; rola só o corpo (cabeçalho e abas fixos).
- Linhas numéricas capturam arrasto HORIZONTAL; a lista rola no VERTICAL —
  a arena de gestos decide pela direção.
- Efeitos: `ReorderableListView` (cabeçalho = peças acima; rodapé = chips).
- Texto: o campo de texto fica FIXO acima da lista (não rola).
- Não há "scroll to selected", nem snap, nem sombra de rolagem.

---

## 3. A linha de propriedade (o controle central do app)

### 3.1 [H] `AureaPropertyRow` numérica (`core/ds/aurea_property_row.dart:265-337`)

```
altura 44
[◆ 30 | ou respiro 6] [nome 76] [6] [ régua Expanded, altura 36 ] [6] [valor 74×34] [‹ › 36 só se animada]
```

- **Coluna do losango**: 30 dp (`AureaLosango`) quando a propriedade anima;
  6 dp de respiro quando não anima (`keyframe == null`).
- **Nome**: caixa 76 × 44, alinhado à esquerda e ao centro vertical, UMA
  linha, `FittedBox(scaleDown)` (encolhe o texto para caber — ver bug B-01),
  estilo `propriedade` (12,5 sp `#AAB6C3`). Toque LONGO no nome = resetar
  (`aoResetar`), sem confirmação, sem háptica. Sem estado "selecionado".
- **Régua**: `AmTickRuler(arrastavel:false, accentCenter:false, height:36)`
  com opacidade 0,4 se desabilitada.
- **Valor**: `AureaValueField` 74 × 34 (seção 3.5).
- **Setas ‹ ›**: só quando `keyframe.animated` (18 dp cada).
- **A LINHA INTEIRA arrasta** (`AmArrastoDeValor` envolvendo a Row). Losango,
  caixa e nome continuam recebendo TOQUE (toque ≠ arrasto na arena).
- Largura da régua (usada na conta de sensibilidade):
  `larguraRegua = larguraDaLinha − (keyframe==null ? 6 : 30) − 76 − 2×6 − 74 − (animada ? 36 : 0)`.
  Ex.: painel de 411 dp → corpo 367 → régua 161 (125 com setas); dentro de
  cartão de efeito (padding 10/4) → 353 → régua 147 / 111.
- Chaves de teste: `prop-<chave>`, `kf-<chave>`, `valor-<chave>`,
  `deslizante-<chave>`; chave padrão = rótulo sem acento em kebab-case.

### 3.2 [A] `LinhaDeParametro` (t2; `widgets/linha_de_parametro.dart`, tag antiga)

```
altura 48
[chip 94×32] [8] [ fita Expanded, altura 40 ] [8] [caixa 68, altura 24]
```

- **Chip do rótulo** 94 × 32, raio 8, padding horizontal 6, texto centrado
  em ATÉ DUAS LINHAS com reticências (`"Largura do / mosaico"` no t2),
  12 sp w600 altura 1,05, cor muted `#AAB6C3`.
  **Escolhido**: fundo `#212D3A`, texto `accent #6FAED9` SUBLINHADO na
  mesma cor ("Centro Y" no t2). Tocar no chip escolhe; começar a arrastar a
  fita também escolhe.
- **Fita** (seção 3.3 [A]): centro aceso (`accent`) na linha escolhida,
  BRANCO nas outras. Arrasto só na fita (não na linha inteira).
- **Caixa** 68 × 24 (seção 3.5 [A]).
- No [A] o losango NÃO fica na linha: fica no trilho esquerdo e vale para o
  efeito inteiro (2.4).

### 3.3 A régua de riscos

Matemática comum (`core/ui/am_tick_ruler.dart:25-183`), idêntica em [A] e [H]:

- **Passo entre riscos: 9 dp** (`passoDosRiscos`).
- **Um risco forte a cada 5** (`riscosPorForte`) — índice ABSOLUTO no
  "papel", para o forte andar junto com os fracos (evita o efeito
  roda-de-carroça até 1350 px/s).
- **Os riscos seguem o dedo**: posição = `origem(centro) + valor / porPixel`.
  Direita aumenta o valor e os riscos andam para a direita.
  Algoritmo: `base = valor/porPixel + centro; inteiro = floor(base/9);
  fase = base − inteiro×9; para x = fase−9 até largura passo 9 (k=0,1,…):
  índice = k − 1 − inteiro; forte = índice % 5 == 0`.
- **Leitura de posição** (só com `min` e `max` finitos): trilho de 3 dp na
  BASE da régua (muted 18 %) e preenchimento que cresce PARA A DIREITA; se a
  faixa cruza o zero, cresce a partir do zero (−100..100 em 0 = vazio; 50 =
  do meio para a direita). Os riscos param 2 dp acima do trilho.
  Preenchimento: `accent` (régua ativa) ou `cabecote` branco (inativa).
- Sem faixa = régua relativa: só riscos, sem trilho.

**[H] Pintor `_TickRulerPainter`** (`am_tick_ruler.dart:362-443`), altura h
(36 na linha de 44):

| Traço | Topo → base | Cor / espessura |
|---|---|---|
| Risco fraco | `pad` → `h − pad`, `pad = 0,18h` (6,5 → 29,5) | `#43516A`, 1,6 |
| Risco forte | `0,55·pad` → `h − 0,55·pad` (3,6 → 32,4) | `#7485A3`, 2 |
| Indicador central (último traço) | `0,4·pad` → `h − 0,4·pad` (2,6 → 33,4) | branco `#FFFFFF` (`accentCenter:false`) ou `accent`; 3 |

Com leitura de posição, todos os traços param em `h − 3 − 2`.

**[A] Pintor `_PintorDaFita`** (`widgets/fita_de_ajuste.dart`, tag antiga),
altura 40:

| Traço | Geometria | Cor |
|---|---|---|
| Folga topo/base | 8 dp → área útil 24 dp | — |
| Risco fraco | encurtado 18 % da área em cada ponta (código); **no t2 o risco tem os mesmos 24 dp do centro** (build anterior ao encurtamento) | muted a 25 % × fade = `#3A424C`, 1 dp |
| Risco forte (cada 5º) | 24 dp cheios | muted a 60 % = `#6E7883`, 1,5 dp (não aparece no t2: build anterior) |
| Fade nas bordas | opacidade = distância da borda / 24 dp | — |
| Indicador central | 24 dp, 2 dp | `accent` se ativa, senão `#FFFFFF` |

`AureaSlider` (`aurea_slider.dart`) existe no DS mas **nenhum painel desta
área o usa** (a linha usa a régua). Com faixa ele desenha trilho 2,5 dp
`campoAlto`, cheio `destaque`, alça círculo r = 7,5 `texto`; sem faixa,
riscos ±4/±7 e indicador ±10 dp de 2 dp.

### 3.4 O gesto de arrasto (`AmArrastoDeValor`, `am_tick_ruler.dart:266-360`)

- `onHorizontalDragStart`: guarda `inicio = valor atual`, zera `acumulado`,
  chama `aoComecarGesto` (= `controller.beginGesture()` — abre UM passo de
  desfazer para o arrasto inteiro).
- `onHorizontalDragUpdate`: `acumulado += delta.dx`;
  **`novo = clamp(inicio + acumulado × porPixel, min, max)`** (acumula desde o
  início; não soma delta a delta sobre o valor vindo do estado).
- **Uma entrega por quadro**: o 1º evento do quadro é entregue na hora; os
  seguintes ficam pendentes e o último sai no `postFrameCallback`. Fim e
  cancelamento descarregam o pendente e chamam `aoTerminarGesto`
  (`endGesture`).
- Cada passo passa por `aCadaPasso` → `Interacao.marcar()` (o palco troca
  qualidade por resposta enquanto o dedo mexe). `linhaNumerica`
  (`comum_de_objetos.dart:73-76`) também chama `Interacao.soltar()` no fim.

**Sensibilidade** (`AureaSlider.sensibilidadePara`, `aurea_slider.dart:63-75`):

```
se sensibilidade pedida > 0          → pedida (unidades por dp)
se min e max finitos e max > min     → (max − min) / max(larguraRegua − 25, 1)
senão                                → 0,5 unidade por dp
```

Ou seja, com faixa, arrastar a largura da régua percorre a faixa inteira.
Parâmetros de efeito passam `dragStep` como sensibilidade (ex.: Time Remap
1/30 s por dp). [A]: `porPixel = dragStep ?? (max − min) / 500`.

| Existe? | [H] | [A] |
|---|---|---|
| Aceleração / curva de velocidade | NÃO (linear) | NÃO |
| Modo ajuste fino (2 dedos, segurar, arrasto vertical) | NÃO | NÃO |
| Encaixe (snapping) de valor | NÃO. (`velocidadeDaRegua`, com ímãs 0,25/0,5/0,75/1/2/3 e passo 0,05, existe em `domain/velocidade.dart:34` mas NÃO é usado — bug B-12) | a régua de velocidade antiga usava |
| Arredondamento | só na exibição (casas); o valor gravado é o bruto | idem |
| Háptica no arrasto / no limite | NÃO | NÃO |
| Inércia (fling) | NÃO | NÃO |

### 3.5 A caixa de valor

**[H] `AureaValueField`** (`aurea_value_field.dart:16-131`):

- Caixa 74 × 34 (ou `largura` dada; `double.infinity` na linha de ponto),
  padding horizontal 4, fundo `campo #212D3A`, raio 8, conteúdo centrado em
  `FittedBox(scaleDown)`.
- Texto: `[prefixo 10 sp muted + 4]` + número `valor` (14 sp w700 `#A9D3EC`
  tabular; desabilitado = `textoSecundario`). **Sem sublinhado.**
- Formato: `formatarValorDigitado(v, casas)` + unidade — `toStringAsFixed`
  e APARA zeros à direita, **ponto decimal** ("0.82", "100%"); não finito =
  "—". (Diverge da âncora — bug B-03.)
- Toque → teclado numérico (3.6). Toque longo → `aoSegurar` (menu do campo,
  seção 5.20). Com `arrastavel` (linha de ponto X/Y), arrastar a caixa muda
  o valor (sensibilidade própria, padrão 0,5).

**[A] `CampoDeValor`** (`widgets/campo_de_valor.dart`, tag antiga — cópia
viva em `features/export/presentation/campo_de_valor.dart`):

- Largura 68 (na linha; padrão 61), caixa altura **24**, raio **8**, padding
  horizontal 4, fundo `campo #212D3A`.
- Número 13 sp w600 `accent #6FAED9` tabular, **SUBLINHADO** na mesma cor
  quando digitável (sem callback = sem sublinhado).
- **pt-BR, casas FIXAS**: `numeroPtBr` → `toStringAsFixed(casas)`, troca
  `.` por `,`, "-0,0" vira "0,0" ("0,82", "100,0%"). Encolhe antes de vazar.
- Toque → `CupertinoAlertDialog` com campo (o HEAD trocou pelo teclado).

**Casas automáticas** (efeitos, `cartao_do_efeito.dart:664-669`): faixa ≤ 2
→ 3 casas; ≤ 20 → 2; senão 1 (a menos que o parâmetro defina `decimals`).

### 3.6 Teclado numérico (`core/ds/aurea_teclado_numerico.dart`)

Abre com `showModalBottomSheet` (NÃO `mostrarAureaFolha`): fundo `painel`,
topo raio **20**, véu `Colors.black38`, rolagem controlada, arrastar para
fechar LIGADO (padrão do Flutter — bug B-14).

```
SafeArea, padding L16 T14 R16 B(12 + teclado do sistema)
Título ("<rótulo>" ou "Valor exato (<unidade>)")   15 sp w700 texto
[10]
Visor: CupertinoTextField alinhado à direita, padding 14×12,
       26 sp w700 tabular texto, sufixo unidade 16 sp muted (padding R14),
       fundo `palco` raio 12, autofocus, keyboardType NONE,
       texto inicial TODO selecionado
Linha de dica (altura 22, à direita, 12 sp muted):
       "Conta incompleta" | "Fica em <valor preso>" | "= <resultado>" | nada
5 fileiras × 4 teclas (Expanded, padding h3, espaço inferior 6):
   7  8  9  ⌫
   4  5  6  ÷
   1  2  3  ×
   ,  0  ±  −
   :  %  =  +
tecla: altura 48, raio 10, fundo `campo`; operadores (÷ × − + =) fundo
`destaqueApagado` e texto `destaque`; texto 21 sp w600; ⌫ = ícone
delete_left 22; ⌫ longo = limpa tudo; háptica leve na descida
[4]
[ Cancelar (CupertinoButton campo, 15 sp) ] [10] [ OK (destaque, 15 sp w700 sobreAcao) ]
   padding vertical 12; OK desabilitado se a conta não fecha
```

Semântica (`domain/expr.dart:13-57`): × ÷ − viram operadores; "1:30" = 90
(tempo com dois-pontos, "1:02:03.5" = 3723,5); "50%" = 50 % de
`percentOf` (100 se a unidade for "%", senão o `max` finito, senão 100);
aceita "1.234,56" e "1234.56"; "=" mostra a conta resolvida; resultado
fora da faixa é PRESO em `[min,max]` (a dica avisa antes). OK devolve o
texto; quem chamou converte e prende.

Editor de expressão (Pro) `showExpressionEditor`
(`aurea_teclado_numerico.dart:388-445`): `CupertinoAlertDialog` "Expressão ·
<nome>", campo 1–3 linhas (dica "ex.: wiggle(2, 30) ou time * 90"), erro do
motor em 12 sp `#FF6B6B`, ações Limpar / Cancelar / OK.

### 3.7 Keyframe na linha

**Estado** (`KeyframeState`, `aurea_keyframe_button.dart:89-106`):
`animated` (a trilha tem marcas), `here` (há marca no instante do cabeçote,
tolerância **8 ms** = `toleranciaDaMarcaUs`), `onToggle`, `onCurve`.

| Estado | [H] losango (`AureaLosango`, 30 × 44, ícone 15) | [H] setas | [A] rail |
|---|---|---|---|
| Não animada | `rhombus` (vazado), `textoSecundario` 60 % (`#6E7883`) | ocultas (a régua fica 36 dp mais larga) | "+" branco |
| Animada, sem marca aqui | `rhombus` vazado, `keyframe #A9D3EC` | ‹ › 18 × 44, ícone 13 `keyframe`; sem vizinha = muted 30 % e inerte | "+" branco |
| Marca aqui | `rhombus_fill` (cheio), `keyframe` | idem | "−" `accent` |

- **Tocar no losango**: `onToggle` = põe ou tira a marca NO INSTANTE LIDO NO
  TOQUE (`playback.time.value`, nunca o do build). Háptica leve só ao
  criar. A marca nova grava o valor atual (inclusive o pendente, ver abaixo).
- **Tocar longo no losango**: abre o EDITOR DE CURVA do trecho
  (`abrirEditorDeCurva`, folha não modal de 316 dp) — só com 2+ marcas.
- **Setas**: vão à marca anterior/próxima (pausa + `seek`), a conta é
  `marcasVizinhas` (ignora a marca dentro da tolerância).
- **Uma trilha, um losango**: na aba Posição de Transformar, X, Y e Z mostram
  o mesmo losango (`toggleKeyframe` crava os três).
- **Regra do keyframe explícito** ("edição pendente", `docs/keyframe-explicito.md`):
  trilha parada → a edição muda a base; animada e SOBRE a marca → muda a
  marca; animada e FORA da marca → a edição fica PENDENTE (a prévia mostra,
  o projeto não muda, nenhum keyframe nasce) até tocar no losango, que crava.
  Os números da linha leem o projeto VISÍVEL (com a pendência); o losango lê o
  projeto GRAVADO. Exceções que gravam marca sozinhas: ver bug B-05 (Borda e
  sombra) e o modo **Auto keyframe** (ação ✨ no cabeçalho de Transformar,
  nasce desligado a cada sessão).
- `AureaKeyframeButton` (‹◆› com largura FIXA 64 = 18 + 28 + 18, altura 44)
  só é usado no cabeçalho do painel Pontos.

### 3.8 Editores por tipo

| Tipo | Componente [H] | Anatomia |
|---|---|---|
| float | `AureaPropertyRow` | 3.1; casas conforme parâmetro |
| int | `AureaPropertyRow` com `casas: 0` | o escritor arredonda (`v.round()`), ex. Colunas, Cópias, Semente |
| bool | `AureaPropertyRow.personalizada` + `AureaToggle` | `CupertinoSwitch` (51 × 31) ligado `acao #245D8C`, desligado `campoAlto`; alinhado à esquerda da área |
| enum curto (tudo à vista) | `LinhaDeEscolha` (efeitos) / `FileiraDePilulas` / `LinhaDeFichas` (3D) | `LinhaDeEscolha`: `Wrap` de `AureaChip` (espaço 6, padding vertical 8), a linha CRESCE (minHeight 44). `FileiraDePilulas`: ListView horizontal de altura 44, chips centrados, vão 6 |
| enum longo | `AureaDropdown` | caixa altura 34, padding h10, fundo `campo`, raio 4, texto 13 sp, `chevron_down` 12 muted; abre `AureaMenu` com a atual marcada |
| semente | `LinhaDeSemente` | `[◆][nome 76][Spacer][valor 74×34][dado 38×44 ícone shuffle 17 destaque][setas]`; dado sorteia inteiro ≠ atual em [ceil(min), floor(max)] (sem max: +9999) |
| cor | `AureaPropertyRow.cor` | `[◆][nome 76][amostra 34×22 raio 4][8][#RRGGBB estilo valor][chevron_right 13 muted]`; linha inteira tocável → seletor (seção 3.8.1) |
| ponto 2D | `AureaPropertyRow.ponto` | `[◆][nome 76][caixa X Expanded][6][caixa Y Expanded][setas]`; caixas arrastáveis (sensibilidade 1 em Posição/Pivô; padrão 0,5); prefixo "X"/"Y" 10 sp muted; sem régua. O eixo que não mexe é RELIDO do projeto a cada passo |
| ângulo | `AureaPropertyRow` com unidade "°" | sem dial no [H] (a UI antiga tinha `widgets/dial_de_angulo.dart`) |
| curva | não é linha | toque longo no losango / item "Editar curva" do ⋯ abre `EditorDeCurva` |
| degradê | linhas de cor + posição por parada, ângulo, centro, alcance | seção 5.15 |
| texto | `CupertinoTextField` | fundo `campo`, raio 4 (nome) ou 8 (texto principal), padding 10 × 6/10, estilo corpo |
| ação | `LinhaDeAcao` / `LinhaDePorta` / `FileiraDeAcoes` | item de 43 (37 + 6): ícone 18 `destaque` (ou `perigo`), 10, rótulo 13 sp; `LinhaDePorta` acrescenta `chevron_right` 13 muted. `FileiraDeAcoes` = `Wrap` de chips, espaço 6, padding vertical 8 |

`AureaChip` (`aurea_chip.dart`): altura **28**, padding horizontal 12, raio
pílula, fundo `campo` (aceso `destaqueApagado`), texto 12 sp `texto` (aceso
`destaque`), ícone opcional 16 + 4.

#### 3.8.1 Seletor de cor (`core/ds/aurea_seletor_de_cor.dart`)

`showColorPicker(initial, onChanged, withAlpha=true, recent)`: folha modal
fundo `painel`, topo 13,5, véu `palco` 35 %, sem arrastar para fechar,
entra 200 ms / sai 100 ms. Altura máx 80 % da tela, conteúdo rola.
Padding L18 T12 R18 B16.

```
[ "Cor" 17 sp w700 ] 12 [Original|Nova 76×28 raio 8, xadrez] ... [conta-gotas 20] [Pronto 15 sp w600 accent]
[10]
Segmentado Quadro | Roda | RGB  (thumb `accentDim`, fundo `chip`, 13 sp, padding v7) — lembra a aba em prefs
[12]
Quadro: SV 170 de altura, largura toda, raio 10; [14]; faixa de matiz 26 de altura
Roda:   lado min(largura,240); anel de matiz 26; quadro SV inscrito (raio 8)
RGB:    3 faixas 26 (R G B) com rótulo 16 e valor tocável 52×30 → teclado 0..255
[10] Alfa: faixa 26 (xadrez) + valor tocável "NN%" → teclado 0..100
[12] "#" [campo hex 96, 14 sp, fundo chip raio 8] 8 "H 210°  S 49%  V 85%" 12 sp muted  [copiar ▾ hex/rgba] [colar]
[12] "Minhas cores" 12 muted ...... "segure e arraste para a lixeira" 10,5 muted
[8]  [+ 40 círculo chip]  8  amostras 40 (até 24, arrastar até a lixeira apaga)
[12] "Rápidas" + Wrap de bolinhas 30 (espaço 10): branco, preto, #6FAED9, #A9D3EC,
     #35C4E7, #2BE3A0, #FFB020, #FF6B6B, #FF4FA3, #AAB6C3, #1B2530, #F7F9FB
```

- Alça das faixas: 14 × (altura+4), raio 7, borda branca 2,5.
- A cor sai VIVA (`onChanged`) enquanto arrasta; `escolherCor`
  (`pecas_centrais.dart:42-63`) abre `beginGesture` antes e fecha no fim
  (tudo = UM desfazer). Vários painéis chamam `showColorPicker` direto e
  pulam essa regra (bug B-07).
- Tocar a cor Original volta a ela.
- **Conta-gotas** (`core/ds/conta_gotas.dart`): só aparece se há palco.
  Fotografa o `RepaintBoundary` do palco (`toImage(pixelRatio:1)`), abre uma
  rota transparente (véu preto 54 %) com a FOTO no mesmo lugar do palco; o
  dedo passeia: anel branco 16 (borda 2) sob o dedo e LUPA de 50 dp (borda
  branca 3, sombra preta 45 % blur 8) 86 dp acima; soltar escolhe; texto
  "Arraste sobre o palco e solte na cor que quer." 14 sp + ✕
  (`xmark_circle_fill` 30) 16 dp abaixo do palco. Alfa descartado. Para o
  motor Vulkan novo isto precisa ler o quadro do MOTOR (readback), não
  fotografar a árvore de UI (bug B-30).

### 3.9 Linha selecionada ("Centro Y" do t2)

Só existe no [A]: o parâmetro escolhido acende o CHIP (fundo `#212D3A`,
texto `accent` sublinhado) e o CENTRO DA FITA (`accent`); as outras fitas têm
centro branco. A escolha define o alvo do rail (keyframe/curva) e qual trilha
a timeline realça. No [H] não há linha selecionada: o cartão ABERTO é a
propriedade ativa (`PropriedadeAtiva.efeito(id)`) e cada linha tem o próprio
losango. Ver decisão D-2.

### 3.10 Seção recolhível

**[H] `AureaSection`** (`aurea_section.dart`): cabeçalho de 30 dp, título em
CAIXA ALTA estilo `secao` (11 sp w600 +0,3), `chevron_up`/`chevron_down` 12
muted à direita (só se recolhível); o cabeçalho inteiro é o alvo; abre/fecha
sem animação; aberta por padrão (`inicialmenteAberta`).

**[A] `_CabecaDeGrupo`** (`am/effects_panel.dart`, também copiada em
`export_video_screen.dart:1764-1814`): altura 42, `chevron_down/right` 13
muted, 8, rótulo 12 sp w700 letterSpacing 0,6 muted, traço 40 × 1
`hairline` à direita.

---

## 4. Efeitos

### 4.1 [H] Painel Efeitos (`paineis/efeitos/painel_de_efeitos.dart`)

```
Efeitos ......................................... [+] [✓]      38
(camada de ajuste) Intensidade  ◆ ┃┃┃│┃┃┃ [100%]               44
▼ Deep Glow ............................ 👁  ≡  ⋯              37  (cartão aberto)
   ◇ Raio      ┃┃┃┃┃┃│┃┃┃┃┃┃   [ 200px ]                      44
▶ Motion Tile .......................... 👁  ≡  ⋯              37
▶ Animador de Texto .................... 👁     ⋯              37  (camada de texto)
[+ Adicionar efeito] [Colar efeitos] [Meus presets] [Efeitos de áudio]   chips
```

- Título "Efeitos"; ação `+` (`plus`, ATIVA = cor `destaque`) abre o catálogo.
- Corpo = `PilhaDeEfeitos` (4.2) com:
  - cabeçalho: linha **Intensidade** só em camada de ajuste — é a OPACIDADE da
    camada: 0–100 %, casas 0, losango da opacidade, toque longo no nome
    reseta para 100 %; `editOpacity(layer, t, v/100)`.
  - rodapé: `FileiraDeAcoes` com `+ Adicionar efeito` (catálogo),
    `Colar efeitos` (só se há efeitos copiados; `pasteEffects`),
    `Meus presets` (tela de presets), `Efeitos de áudio` (só vídeo; abre o
    painel Áudio).
- Camada de ÁUDIO: o painel mostra a seção de efeitos de áudio (5.11) no
  lugar da pilha de pixel.
- Vazio: aviso "Nenhum efeito nesta camada. Toque em + para adicionar."

**[A]** (t2): cabeçalho "‹ Efeitos" 44 + rail 46 + lista padding 2/8/12/16;
rodapé = seção de animadores de texto, bloco "Intensidade da camada de
ajuste" (fundo chip 55 %, raio 12, padding 8), botão texto "Efeitos de
audio" e botão largo `+ Adicionar efeito` (padding vertical 13, fundo chip,
raio 12, ícone 17 + 8 + 14 sp w600, cor `action`, háptico).

### 4.2 A pilha (`efeitos/pilha_de_efeitos.dart`)

- Ordem = ordem de aplicação do motor (de cima para baixo).
- **Acordeão**: UM cartão aberto por vez (por id; sobrevive a reordenar).
  Ao entrar, o PRIMEIRO vem aberto. Efeito recém-adicionado abre sozinho e
  fecha os outros. Tocar no cabeçalho de outro abre ele e fecha o anterior.
  O cartão aberto vira a PROPRIEDADE ATIVA da timeline
  (`PropriedadeAtiva.efeito(id)`: a timeline acende só as marcas dele);
  fechar devolve a camada inteira.
- **Reordenar**: `ReorderableListView` sem alças padrão; a alça `≡` do
  cartão inicia o arrasto NA HORA (`ReorderableDragStartListener`). Proxy de
  arrasto = o padrão do Material (elevação/sombra). Soltar =
  `reorderEffect(layer, id, para − de)` num passo de desfazer.
  [A]: segurar o CABEÇALHO (atraso de toque longo) e arrastar.
- Animadores de Texto entram NO FIM da pilha, como cartões iguais (não
  reordenáveis).
- Com `filtro` (painel Cor): não reordena (posição filtrada ≠ posição real).
- A pilha observa só a lista de ids; um número que muda refaz só o cartão
  dele; cartão fechado nem escuta o cabeçote.

### 4.3 O cartão

**[H] `AureaEffectCard`** (`core/ds/aurea_effect_card.dart`):

```
fundo `elevado` #1B2530, raio 5, margem inferior 6, sem borda
cabeçalho 37: [seta 32 (chevron_right 13 muted, gira 90° em 100 ms)] [nome 13 sp w600, 1 linha …]
              [👁 36×37 ícone 17] [≡ 35×37 ícone 16 muted] [⋯ 36×37 ícone 17 muted] [4]
corpo (só aberto): padding L10 T0 R4 B4, coluna de linhas
```

- A área seta + nome é UM alvo (abre/recolhe, sem encolher).
- Olho: `eye` (muted) / `eye_slash` (muted 50 %) — liga/desliga SEM apagar;
  desligado, o nome fica muted 60 %. `toggleEffectEnabled`, um passo.
- Recolher é instantâneo (sem animar altura), de propósito.
- Nome do catálogo passa pelo tradutor; efeito removido do catálogo mostra
  o nome do tipo cru e o aviso "Este efeito saiu do catálogo. Ele não desenha
  mais; apague pelo ⋯."

**[A] `_CartaoDoEfeito`** (t2): fundo `#151C24`, raio **14**, margem
inferior 10, padding L10 T2 R6 B8; cabeçalho 48: `arrowtriangle_down_fill` /
`_right_fill` 13 `text`, 12, nome 17 sp w600 (desligado: opacidade 0,45 + ícone
`eye_slash` 16 muted), `⋯` 44 × 44 (ícone 22 `text`), `🗑` 44 × 44 (ícone 22
`text`, apaga DIRETO). Corpo inteiro com opacidade 0,45 se desligado.

### 4.4 O menu ⋯ do efeito (`cartao_do_efeito.dart:105-300`)

`mostrarAureaMenu`, título = nome do efeito (ou "Efeito removido"). Itens
(condição → ação no controlador):

| Item | Ícone | Quando aparece | Ação |
|---|---|---|---|
| Duplicar | `plus_square_on_square` | efeito conhecido e não Time Remap | `duplicateEffect` |
| Mover para cima | `arrow_up` | pilha não filtrada e posição > 0 | `reorderEffect(−1)` |
| Mover para baixo | `arrow_down` | não filtrada e não é o último | `reorderEffect(+1)` |
| Resetar | `arrow_counterclockwise` | conhecido | todos os parâmetros ao `initial` no cabeçote (+ cor padrão), um passo; Time Remap volta à IDENTIDADE (`resetarCurvaDeTempo`) |
| Prontos | `wand_stars` | tem presets na ficha | 2º menu com os prontos → `applyEffectPronto` |
| Copiar efeitos | `doc_on_doc` | comum | `copyEffects(layer)` (copia a pilha) |
| Colar efeitos | `doc_on_clipboard` | há efeitos copiados | `pasteEffects` |
| Keyframe em todos os parâmetros / Tirar o keyframe de todos os parâmetros | `suit_diamond_fill` / `suit_diamond` | comum | `toggleEffectKeyframe(layer, efeito, t do toque)` |
| Editar curva | `graph_square` | Time Remap sempre; outros se animados | `abrirEditorDeCurva(TrilhaDaCurva.efeito/timeRemap)` |
| Assar em keyframes | `flame` | Pro + efeito procedural (Tremor…) | `bakeEffectToKeyframes(fps)` + aviso "Movimento assado em keyframes" com "Desfazer" |
| Salvar como preset | `square_arrow_down` | comum | diálogo "Nome do preset" → `saveEffectPresetFrom` → loja global; aviso "Preset salvo para todos os projetos: <nome>" |
| Meus presets | `square_stack_3d_up` | comum | tela de Presets |
| Como usar este efeito | `question_circle` | conhecido | `QuickGuideScreen(initialQuery: nome)` |
| Apagar | `trash` (vermelho) | sempre | `removeEffect`, um passo, SEM confirmação |

### 4.5 O corpo do efeito (`cartao_do_efeito.dart:415-662`)

Ordem das linhas:

1. (Time Remap) linhas "antes" (4.6).
2. Parâmetros que NÃO estão em nenhum grupo da ficha, na ordem da ficha.
3. Cada grupo da ficha como `AureaSection` (o 1º aberto, os outros
   recolhidos).
4. Cor principal (`AureaPropertyRow.cor`, rótulo = `colorLabels[0]` ou "Cor")
   e cores extras ("Cor 2", "Cor 3"… ou os nomes da ficha) → seletor com
   `setEffectColor` / `setEffectExtraColor`. Cor NÃO anima.
5. (Time Remap) linhas "depois".
6. Sem linha nenhuma: "Este efeito não tem ajustes."

Mapeamento `ParamKind` → linha:

| `ParamKind` | Linha | Detalhe |
|---|---|---|
| `number`, `point` (cada eixo é um parâmetro), `color` (número) | `AureaPropertyRow` | min/max da ficha; `unit`; casas = `decimals` ou automáticas (3.5); sensibilidade = `dragStep` ou faixa/largura; losango da trilha do parâmetro; toque longo no nome reseta ao `initial`; escreve `editEffectParam(layer, efeito, chave, t, clamp(v))` |
| `toggle` | `personalizada` + `AureaToggle` | valor ≥ 0,5 = ligado; escreve 1/0 num passo |
| `choice` | `LinhaDeEscolha` (chips à vista) | índice arredondado e preso; um passo |
| `seed` | `LinhaDeSemente` | número + dado |

Todo parâmetro de efeito tem losango (anima), inclusive toggle/escolha/semente
(`toggleEffectParamKeyframe(layer, efeito, chave, t)`). Toque longo no
losango animado → curva daquele parâmetro.

**Exemplo — Motion Tile** (`domain/motion_tile.dart`; é o do t2):

| Rótulo | Tipo | Padrão | Faixa | Unid. | Casas | Exibido no t2 |
|---|---|---|---|---|---|---|
| Centro X | point (relativo) | 0,5 | −1 … 2 | — | 2 (auto) | "0,82" |
| Centro Y | point (relativo) | 0,5 | −1 … 2 | — | 2 | "0,28" (linha escolhida) |
| Largura do mosaico | number | 100 | 1 … 300 | % | 1 | "100,0%" |
| Altura do mosaico | number | 100 | 1 … 300 | % | 1 | "100,0%" |
| Largura da saída | number | 100 | 1 … 600 | % | 1 | (cortado) |
| Altura da saída | number | 100 | 1 … 600 | % | 1 | — |
| Bordas espelhadas | toggle | 0 | 0 … 1 | — | — | — |
| Esticar bordas | toggle | 0 | 0 … 1 | — | — | — |
| Fase | number (dragStep 0,5) | 0 | −36000 … 36000 | ° | 1 | — |
| Deslocamento de fase horizontal | toggle | 0 | 0 … 1 | — | — | — |

Prontos do Motion Tile (menu Prontos / folha de detalhe): Grade (33,3/33,3),
Tijolos (50/25, fase 180, desloc. horizontal), Espelho (50/50, espelhar),
Parede de telas (25/25, saída 200/200). "Deslocamento de fase horizontal"
(31 caracteres) é o pior caso do rótulo de 76 dp (bug B-01).

Aplicado pelo catálogo, o Motion Tile nasce com o pronto "Ao redor"
(mosaico 100/100, saída 300/300 — `catalogo_de_efeitos.dart:543-553`), para
não encolher a camada.

### 4.6 Time Remap (`efeitos/time_remap.dart`) — só em vídeo

Linhas do mesmo cartão (rótulos em inglês, vocabulário do AE):

| # | Rótulo | Tipo | Faixa / unidade | Engine |
|---|---|---|---|---|
| antes | Speed | número SEM faixa (régua relativa) | sens 0,01/dp, 2 casas, "×" | `timeRemapSpeedAt` / `setTimeRemapSpeed(layer, t, v)` |
| ficha | Time | número (a trilha `tempo`) | 0 … ∞ na UI (ficha 0…86400), "s", 3 casas, dragStep 1/30 | `editEffectParam(..., 'tempo', ...)`; relógio CRU do clipe (`t − início`); sem reset |
| depois | Frame | número | sens 0,5, 0 casas | `timeRemapFrameAt` / `setTimeRemapFrame` |
| | Reverse | toggle | — | `setClipReverse` |
| | Freeze | toggle | — | `setTimeRemapFreeze(layer, t, v)` |
| | Interpolação | dropdown | Nenhuma · Mistura · Fluxo óptico · Fluxo óptico (IA) | `setClipInterpolacao` |
| | Manter tom | toggle | — | `setClipPreservePitch` |
| | Curva | chip "Abrir curva" (`graph_square`) | — | editor de curva, trilha `timeRemap` |
| | (chips) | Inverter a curva · Reverso a partir daqui · Velocidade constante (só com curva) | — | `assarReversoNaCurva` (2×) / `definirTrilhaDeTempo(reversoAPartirDe)` / `ligarCurvaDeTempo(false)` |

### 4.7 Cartão do Animador de Texto (`efeitos/animador_de_texto.dart`)

Mesmo `AureaEffectCard` (sem alça ≡). Menu: Resetar (aplica o preset
padrão) · Apagar. Linhas:

| Seção | Rótulo | Tipo | Faixa | Unid. | Losango |
|---|---|---|---|---|---|
| — | Preset | dropdown | presets do animador (Palavra por palavra, Letra por letra, Pop, Bounce, Fade Up, Fade Down, Slide Left, Slide Right, Scale In, Typewriter) ou "Personalizado" | | não |
| Range Selector (aberta) | Start | número | 0–100 | % | sim |
| | End | número | 0–100 | % | sim |
| | Offset | número | −100–100 | % | sim |
| | Unidade | dropdown | Caracteres · Palavras · Linhas | | não |
| | Forma | dropdown | Square · Ramp Up · Ramp Down · Triangle · Round · Smooth | | não |
| Easing (fechada) | Ease High / Ease Low | número | −100–100 | — | sim |
| Propriedades (fechada) | Posicao X, Posicao Y (−1000–1000), Escala (0–400 %), Rotacao (−720–720 °), Opacidade (0–100 %), Espacamento (−100–200), Desfoque (0–60) | número, casas 0 | | | sim; reset ao neutro (100 para escala/opacidade, 0 para o resto) |

Os rótulos das propriedades vêm SEM acento de `text_animator.dart:670-686`
(bug B-26). Faixa guardada em fração 0..1 no motor, mostrada em %.

### 4.8 Catálogo de efeitos ("Effects browser")

#### 4.8.1 [A] A folha de `tela96b.png` (`context/effects/effect_gallery.dart`, tag antiga) — medida

| Elemento | Medida (px → dp) | Código |
|---|---|---|
| Folha | topo ≈ y 888; ocupa até a barra do sistema | `showModalBottomSheet`, fundo `panel #0F141A`, raio topo **20**, altura máx **62 %** da tela |
| Padding | lateral 37 px = **14** | `EdgeInsets.fromLTRB(14, 12, 14, 8 + teclado)` |
| Título "Efeitos" | — | **18 sp w700** `text` |
| Busca | y 1004–1097 = **36 dp**, cor `#272B33` | `CupertinoSearchTextField` padrão (fundo = tertiarySystemFill sobre o painel, raio 9), placeholder **"glow, rgb split, pixelate..."**, texto 14 sp; 8 dp acima |
| Fileira de filtros | chips y 1122–1205 = **32 dp** | `SizedBox(height 34)` com scroll horizontal; chip padding h12 v8, raio **9**, fundo `chip #212D3A` (aceso: `actionDim` + borda 1 `action`), texto **12 sp w600** `text` (aceso `action`), vão **8** |
| Rótulos | "Cor 6", "Estilizar 13", "Distorcer 8", "Diversos 2", "Glow e Luz …" | `"<categoria> <quantidade>"`; antes deles (rolados para fora no print) "Todos N", "Sugeridos N", "Recentes N", "★ Favoritos N" (Pro) |
| Grade | colunas = floor(largura / 118) entre 2 e 5 → **3** | espaço 10 × 10, **aspecto 0,7** |
| Tile | prévia x 37–352 = **120 × 120 dp**; passo vertical 480 px = 183 dp | prévia quadrada = largura do tile, raio **10** |
| Estrela (Pro) | canto sup. direito | `Positioned(right 2, top 2)`, alvo 32 × 32, ícone 16 `star` branco 70 % / `star_fill` `action` |
| Selo de custo | canto inf. esquerdo | `Positioned(left 6, bottom 6)`, padding 5 × 2, raio 6, preto 55 %, "custo N" 9 sp branco; só custo > 1 |
| Nome | 5 dp abaixo da prévia | 12 sp w600 `text`, 1 linha … |
| Categoria | — | 10,5 sp muted |
| Prévia | gradiente `#232B3A`→`#606F98`, disco `#FF4D2D`, traço branco | a cartela com o efeito aplicado (4.8.4) |

#### 4.8.2 [H] Catálogo atual (`efeitos/catalogo_de_efeitos.dart`)

- Abre com `mostrarAureaFolha(titulo: 'Adicionar efeito', grande: true,
  altura: min(560, 62 % da tela))` — cabeçalho 44 com ✕; pausa a reprodução
  e já começa a ler o manifesto das prévias.
- Corpo padding L22 T0 R22 B10:

```
[ CupertinoSearchTextField "Buscar efeito" (corpo 13 sp, fundo campo #212D3A) ] [grade/lista 44×38]
[8]
[ Todos N ][ Texto 1 ][ Câmera 1 ][ Sugeridos N ][ Recentes N ][ Favoritos N ][ Cor 6 ] ...  (AureaChip 28, vão 6, rola)
[10]
[ grade ou lista ]
```

- **Filtros**: Todos (catálogo + Animador de Texto em texto + Camera Tracker
  em vídeo) · Texto (só camada de texto) · Câmera (só vídeo) · Sugeridos
  (`efeitosRecomendados(ehMidia, recentes)`) · Recentes · Favoritos · uma
  por categoria na ordem Cor, Estilizar, Distorcer, Diversos, Glow e Luz,
  Lente, Desfoque, Glitch, Tempo, Gerar, Recorte, Utilitário (só as que têm
  efeito disponível). Contagem ao lado do nome.
- **Busca**: sem acento e sem caixa (`normalizarBusca`), por nome, sinônimos
  e categoria em PT/EN; enquanto há busca nenhum chip fica aceso; tocar num
  chip limpa a busca. O Animador de Texto e o Camera Tracker também são
  achados por palavras-chave. Optical Flow e Time Remap só aparecem em vídeo.
- **Grade**: colunas = floor(largura / 104) entre 3 e 6; espaço principal
  10, cruzado 8; aspecto **0,74**. Em 367 dp: 3 colunas de 117 × 158.
  Tile: prévia quadrada (raio 8) + estrela 32 × 32 no canto superior direito
  (ícone 16; favorita `destaque`, senão `textoSecundario`; SEMPRE visível) +
  selo "custo N" (left 4, bottom 4, padding 5 × 2, `palco` 70 %, raio 4,
  9 sp `texto`) + 4 + nome 12 sp w600 + categoria 10 sp muted.
- **Lista** (alternável, lembrada em prefs `efeitos.catalogo.lista`): linha
  57 = prévia 40 (raio 4) + 10 + nome/categoria + estrela.
- **Vazio**: Favoritos "Nenhum favorito ainda. Toque na estrela de um
  efeito."; Recentes "Os efeitos que você aplicar aparecem aqui."; resto
  "Nada encontrado. Tente "glow", "rgb", "pixel" ou "shake"."
- **Toque = aplicar e fechar**: `umPasso(addEffect(layer, tipo, pronto:
  prontoAoAplicar(tipo)))`, registra em Recentes, fecha a folha. O cartão
  novo abre sozinho na pilha. Aplica na camada do painel (UMA camada; não há
  aplicar em várias camadas por aqui). Animador de Texto →
  `addTextAnimator`; Camera Tracker → fecha e abre o painel Rastrear.
- **Toque longo = detalhe** (4.8.3).

#### 4.8.3 Folha de detalhe (`efeitos/detalhe_do_efeito.dart`)

`mostrarAureaFolha(grande, altura: min(520, 66 %))`, sem título (10 de
respiro em cima), padding L22 T0 R22 B10:

```
[prévia 104×104 raio 8, animada] [12] Nome 17 sp w600
                                    [2] Categoria (12,5 sp muted)
                                    [8] [★ 20, padding 4] [10] [ Aplicar ] (padding h16 v8, fundo acao, raio 8, 13 sp w700 sobreAcao)
[15]
(rola) texto do guia (13 sp, altura 1,45)
       [15] PRONTOS (secao)  →  cartões: margem topo 6, padding h12 v10, fundo elevado, raio 5:
             nome (13 sp w600) / "Raio 90 · Exposição 180" (11 sp muted, 1 linha) / chevron 14
       [15] PARECIDOS (secao) [6] Wrap de chips (até 8 sinônimos) → procura a palavra no catálogo
```

Devolve `AplicarEfeito(pronto?)` ou `ProcurarPor(palavra)`.

#### 4.8.4 Prévias animadas (`efeitos/previa_do_efeito.dart`, `miniatura_do_efeito.dart`)

- Tira JPEG no pacote: `assets/efeitos/previas/<id>.jpg` = **8 quadros de
  200 px** lado a lado; `manifesto.json` (versão 2) diz qual pronto cada
  prévia usou — o toque aplica ESSE pronto.
- **Um relógio só** para a grade inteira (`RelogioDasPrevias`): `Timer`
  a **8 fps** (125 ms), quadro = (quadro+1) % 8, só enquanto o catálogo está
  aberto; cada tile é um `RepaintBoundary` que pinta o quadro da tira.
- Cache LRU de **24** tiras decodificadas.
- Enquanto carrega: quadrado `chip #212D3A` raio 10. Sem tira para o efeito:
  `EffectThumbnail` = renderiza UMA vez a cartela no motor real e guarda
  PNG em disco (`efeitos-miniaturas/<id>-v1.png`, pixelRatio 2).
- **Cartela** (240 × 240): fundo = retângulo com degradê vertical
  `#0F141A` → `#6A7BA8` (sem efeito); disco `#FF4D2D` Ø 42 % em (36 %, 55 %)
  e traço branco 60 % × 1,2 % em (50 %, 20 %), ambos COM o efeito.

### 4.9 Painel Cor usa a mesma pilha

Ver 5.2: pilha filtrada na categoria "Color", com chips `+ <efeito>` para
cada efeito de cor, sem reordenar.

---

## 5. Painéis por tipo — controles na ordem, rótulos literais

Convenções das tabelas: **Tipo**: N = linha numérica (`AureaPropertyRow`
ou `linhaNumerica`), P = ponto X/Y, T = toggle, D = dropdown, Pí = pílulas
(`FileiraDePilulas`/chips à vista), C = linha de cor, A = ação/porta.
"sem faixa" = min/max infinitos (régua relativa, sensibilidade 0,5/dp).
Casas padrão: `AureaPropertyRow` 1, `linhaNumerica` 0. ◆ = tem losango.
Todo painel usa `AureaPanel` (título, ✓, corpo 22/4/22/15) salvo indicação.
Onde não se diz, o `t` das escritas é o do cabeçote LIDO NO TOQUE.

### 5.1 Transformar (`paineis/transformar.dart`) — "Move & Transform"

Título **Transformar**. Abas: **Posição · Escala · Rotação · Opacidade ·
Inclinação · Pivô** (a aba ativa vira a propriedade ativa da timeline).
Ações do cabeçalho: `wand_stars` = **Auto keyframe** (aceso = ligado; nasce
desligado a cada sessão) · `cube` = **camada 3D** (aceso = 3D;
`toggle3D`) · `ellipsis_circle` = **menu do campo** da aba (5.20) · ✓.

Todas as linhas de uma aba compartilham UM losango (`toggleKeyframe(id, t,
prop)`; toque longo = curva `TrilhaDaCurva.transformacao(prop)`). Toque
longo no nome = `resetProp(id, prop)`. Toque longo no valor = menu do campo.

| Aba | Rótulo | Tipo | Faixa · unid · casas · sens | Engine |
|---|---|---|---|---|
| Posição | Posição | P | sem faixa, casas 1, sens 1 px/dp | `editPosition(id, t, Offset)` (o outro eixo relido do projeto) |
| | Profundidade (só 3D) | N | sem faixa, casas 1, sens 1 | `editPositionZ` |
| Escala | Encaixe (só vídeo/imagem) | Pí | Preencher · Ajustar | `setAjusteDaMidia(cobrir/conter)` (sem ◆) |
| | Escala | N | min 0, sem máx, "%", casas 1 | `editScaleUniform(v/100)`; mostra scaleX |
| | Largura | N | sem faixa, "%" | `editScaleX(v/100)` |
| | Altura | N | sem faixa, "%" | `editScaleY(v/100)` |
| Rotação | Rotação | N | sem faixa, "°", casas 1 | `editRotation` |
| | Camada 3D | T | — | `toggle3D` (sem ◆) |
| | Rotação X / Rotação Y (só 3D) | N | sem faixa, "°" | `editRotationX/Y` |
| Opacidade | Opacidade | N | 0–100 "%" casas 0 (sens = 100/(régua−25)) | `editOpacity(v/100)` |
| Inclinação | Inclinação X / Inclinação Y | N | sem faixa "°" | `editSkewX/Y` |
| Pivô | Pivô | P | sem faixa, sens 1 | `editPivot` |

### 5.2 Cor (`paineis/cor.dart`) — "Color & Fill" + correção de cor

Título **Cor**. Três variantes pelo tipo da camada:

**Forma**
| Rótulo | Tipo | Opções | Engine |
|---|---|---|---|
| Preenchimento | D | Nenhum · Cor · Degradê · Mídia | `definirTipoDePreenchimento` (Mídia sem foto abre a galeria antes) |
| (Nenhum) | aviso | "Sem preenchimento: só o traço (se houver) aparece." | — |
| Cor | C | — | `setShapePrimaryColor` |
| Editar degradê | A (porta, `color_filter`) | — | folha Gradiente vetorial (5.15) |
| Escolher uma foto / Trocar a foto | chip (`photo`) | — | `pickImageFromGallery` → `definirTipoDePreenchimento(midia)` |
| Encaixe | D | Preencher · Caber · Esticar | `definirEncaixeDaMidiaNaForma` |

**Elemento 3D**: uma porta "Cor e material do elemento" → folha do elemento.

**Texto, vídeo, imagem e demais** (corpo = pilha filtrada, 4.9):
| Rótulo | Tipo | Opções / faixa | Engine |
|---|---|---|---|
| Preenchimento | D | "Cor do texto" (texto) ou "Da própria camada" · "Cor por cima" · "Degradê" | `updateLayerStyles` (colorOverlay / gradientOverlay, liga um e limpa o outro) |
| Cor do texto (modo 0, texto) | C | — | `editTextLayer(color)` |
| Cor (modo 1) | C | inicial = `destaque` (!) | `colorOverlay.color` |
| Início / Fim (modo 2) | C | — | `gradientOverlay.colorA / colorB` |
| CORREÇÃO DE COR (seção fixa) | chips `+ <efeito>` | um por efeito da categoria Color | `addEffect(layer, tipo)` + Recentes |
| (cartões) | pilha filtrada | vazio: "Nenhuma correção de cor. Escolha uma acima." | 4.2–4.5 |

### 5.3 Mistura e máscara (`paineis/mascara.dart`)

Título **Mistura e máscara** (a ferramenta da barra chama "Máscara"). A
propriedade ativa é a opacidade; um cartão de máscara aberto vira a ativa.

| Rótulo | Tipo | Faixa / opções | ◆ | Engine |
|---|---|---|---|---|
| Opacidade | N | 0–100 % casas 0 | ◆ | `editOpacity` |
| Mistura | D (título "Modo de mistura") | 29 modos em lista corrida: Normal, Dissolver, Escurecer, Multiplicar, Queimar cor, Queimar linear, Cor mais escura, Clarear, Tela, Subexpor cor, Adicionar, Cor mais clara, Sobrepor, Luz suave, Luz forte, Luz viva, Luz linear, Luz pontual, Mistura dura, Diferença, Exclusão, Subtrair, Dividir, Matiz, Saturação, Cor, Luminosidade, Máscara, Recortar | | `setCustomBlend` ou `setBlendMode` |
| Recorte | D ("Recortar por outra camada") | Nenhum · Alfa da camada de cima · Alfa invertido · Luminância da de cima · Luminância invertida · Só onde a de baixo tem pixel | | `setMatte` / `setMatteFromAbove` / `recortarPelaDeBaixo` (aviso se não há camada) |
| (aviso) | — | "Fonte acima: <nome>. Ela fica oculta." / "Coloque uma camada acima desta para recortar por ela." | | — |
| MÁSCARAS | seção fixa | um cartão por máscara | | 5.3.1 |
| (chips) | `+ Retângulo` (460×460) · `+ Círculo` (480) · `+ Estrela` (5 pontas, 250/125) · `+ Coração` (440×420) | | | `addMask(LayerMask(nome, caminho, feather 0))` |
| REVELAR | seção fixa, chips `wand_stars` | um por `MaskRevealPreset` ("Íris"…) | | `applyMaskReveal(id, preset, t)` (cria máscara com 2 marcas) |

#### 5.3.1 Cartão da máscara

`AureaEffectCard`: nome = nome da máscara (cru), olho = modo `none` ↔
`add`, máscaras NOVAS nascem abertas. ⋯: Subir · Descer · Editar pontos ·
Trocar por retângulo · Trocar por elipse · Usar a forma da camada (só forma)
· Desenhar à mão · Apagar.

| Rótulo | Tipo | Faixa · unid | ◆ | Engine |
|---|---|---|---|---|
| Modo | D | Nenhum · Somar · Subtrair · Interseção · Clarear · Escurecer · Diferença | | `updateMask(mode)` |
| Inverter | T | | | `toggleMaskInverted` |
| Forma | chip "Editar pontos" (`pencil_outline`) | | ◆ do caminho | `toggleMaskPathKeyframe`; abre Pontos |
| (aviso, caminho aberto) | "Caminho aberto não corta; pode servir de entrada de efeito." | | | |
| Suavizar (ou Suavizar X) | N | 0–200 px casas 0 | ◆ | `editMaskParam('feather')` |
| Eixos juntos | T | | | `toggleMaskFeatherAxes` |
| Suavizar Y (eixos separados) | N | 0–200 px | ◆ | `'featherY'` |
| Expansão | N | −200–200 px | ◆ | `'expansion'` |
| Opacidade | N | 0–100 % (guardado 0..1) | ◆ | `'opacity'` |

### 5.4 Camada (`paineis/propriedades.dart`)

Título **Camada** (ferramenta "Camada", ícone `info_circle`).

| Rótulo | Tipo | Detalhe | Engine |
|---|---|---|---|
| Nome | campo de texto (placeholder "Nome da camada", fundo campo raio 4, padding 10×6) | grava ao sair do campo ou Enter | `renameLayer` |
| Etiqueta | C | menu "Cor da etiqueta": Sem etiqueta + paleta (nomes) | `setLayerLabel` |
| Mistura | D | mesmos 29 modos | `gravarModoDeMistura` |
| Opacidade | N ◆ | 0–100 % casas 0 | `editOpacity` |
| Visível · Bloqueada · Solo · Tímida | T | um passo cada | `toggleHidden/Locked/Solo/Shy` |
| Vincular a outra camada / Vínculo (tem pai) | A (`link` / `link_circle_fill`) | menu escolher-pai | `vincularAoPai` |
| Início no cabeçote | A (`arrow_right_to_line`) | | `moveLayer(id, t)` |
| Fim no cabeçote | A (`arrow_left_to_line`) | | `moveLayer(id, t − duração)` |
| Pasta | A (`folder`) | folha Organizar | — |
| Repetir movimento (loop) | A (`repeat`) | folha Loop | — |

### 5.5 Texto (`paineis/texto.dart`)

Título **Texto**. O CAMPO DE TEXTO fica fixo no topo (fora da rolagem):
`CupertinoTextField` 1–3 linhas, maiúscula de frase, placeholder "Digite o
texto", 16 sp, fundo campo raio 8, padding 10; sufixo durante a edição =
botão 44 × 44 `keyboard_chevron_compact_down` 20 muted (fecha o teclado);
padding do campo L22 T2 R22 B4. Cada tecla grava (`editTextLayer(text)`).

| Rótulo | Tipo | Faixa | Engine |
|---|---|---|---|
| Tamanho | N | 4–400, casas 0, reset 120 | `editTextLayer(fontSize)` (sem ◆) |
| Cor | C | | `editTextLayer(color)` |
| Alinhamento | 3 botões de ícone 40 × 34, raio 4, vão 6 (aceso: `destaqueApagado` + ícone `destaque`) | Esquerda · Centro · Direita | `editTextLayer(alinhamento)` |
| Fonte | linha tocável: nome da fonte NA PRÓPRIA fonte (valor 14 sp) ou "Padrão" + chevron | | abre o painel Fonte |
| Dados (Pro + CSV carregado) | D | "Sem vínculo" + colunas | `removeDataBinding/addDataBinding/applyDataBindings` |

Camada Texto 3D: porta "Editar a palavra do Texto 3D" → painel Texto 3D.

### 5.6 Fonte (`paineis/fonte.dart`)

Título **Fonte**, ação `+` (destaque) importa .ttf/.otf (vários). Busca fixa
"Procurar fonte" (ícone `search` 16, padding 8 × 6, campo raio 8). Lista
preguiçosa (padding 22/0/6/15):

- Seções FAVORITAS · RECENTES (até 8) · TODAS (a 1ª de Todas é "Padrão").
- Linha 44: amostra = 1ª linha do texto da camada (ou "Aa Bb Cc 123") 18 sp
  altura 1,15 NA FONTE (destaque se escolhida) / nome 10 sp muted; ✓ 16
  destaque se escolhida; estrela 44 × 44 ícone 16 (favorita = cor `acao`
  `#245D8C`, bug B-24).
- Toque = aplica (`editTextLayer(fontFamily)` ou `clearFont`) + recentes.
  Toque longo numa importada = menu "Apagar fonte do aparelho".
- Busca = lista plana; vazio "Nenhuma fonte com esse nome."

### 5.7 Estilo (texto) (`paineis/estilo.dart`)

Título **Estilo**. Abas **Texto · Contorno · Sombra · Fundo · Cor**. Trilhas
do acabamento usam a regra do keyframe explícito
(`editarTrilhaDoEstilo` / `alternarMarcaDoEstilo`).

| Aba | Rótulo | Tipo | Faixa · unid · casas | ◆ | Engine |
|---|---|---|---|---|---|
| Texto | Negrito | T | | | `editTextLayer(bold)` |
| | Espaçamento | N | −100–200, casas 1 | ◆ | animador "Espaçamento" (criado sob demanda) `editAnimadorProp(tracking)`; reset remove o animador |
| Contorno | Contorno | T | liga preto 6 px; desliga remove as bordas | | `comBordas` |
| | Cor | C | | | `stroke.color` |
| | Largura | N | 0–60, casas 1 | ◆ | `stroke.width` |
| | Opacidade | N | 0–100 % | ◆ | `stroke.opacity` |
| | Posição | Pí | Fora · Dentro · Centro | | `stroke.posicao` |
| Sombra | Sombra | T | | | `dropShadow` |
| | Cor · Opacidade (0–100 %) · Ângulo (−360–360 °) · Distância (0–300) · Desfoque (0–120) | C / N | casas 0 | ◆ (números) | `dropShadow.*` |
| | Brilho | T | | | `outerGlow` |
| | Cor · Tamanho (0–120) · Opacidade (0–100 %) | C / N | | ◆ | `outerGlow.*` |
| Fundo | Caixa atrás | T | liga = cria camada forma "Caixa" (retângulo cantos 16 px, preta, até o fim do texto, logo abaixo, `ContainerSpec`) e reabre o painel; desliga = apaga a camada | | `addShapeLayer`, `setContainer`, `reorderLayer` |
| | Cor da caixa | C | | | `setShapePrimaryColor(caixa)` |
| | Opacidade | N | 0–100 % | ◆ (da caixa) | `editOpacity(caixa)` |
| | Cantos | N | 0–200 | ◆ | `editShapeParam(caixa,'roundness')` |
| | Margem X / Margem Y | N | 0–400 | | `setContainer(padLeft=padRight / padTop=padBottom)` |
| Cor | Cor do texto | C | | | `editTextLayer(color)` |
| | Degradê | T | nasce cor do texto → `#8FD3FF` | | `gradientOverlay` |
| | Cor 1 · Cor 2 | C | | | `colorA/colorB` |
| | Ângulo (−360–360 °) · Opacidade (0–100 %) | N | | ◆ | `gradientOverlay.*` |

### 5.8 Animar (texto) (`paineis/animar.dart`)

Título **Animar**. Abas **Entrada · Ênfase · Saída · Animador**.

- Entrada/Ênfase/Saída: pílulas "Nenhuma" + animações da posição (ex.:
  Aparecer, Subir, Descer, Vir da direita, Vir da esquerda, Palavra
  deslizando, Crescer, Encolher, Estourar, Quicar por letra, Quicar por
  palavra, Cair, Aparecer em desfoque, Desfoque por palavra, Desfoque
  subindo/descendo, Maquina de escrever, Girar, Tombar, Virar, Inclinar,
  Abrir espacamento, Aparecer devagar, Glitch, Entrar em cor) →
  `setTextAnim(slot, id)` (troca, não empilha). Com uma aplicada:
  **Duração** 0,1–10 s casas 2 · **Atraso por letra** 0–500 ms.
- Animador: porta "Animador de Texto (efeito)" (`sparkles` → Efeitos);
  pílulas "Nenhuma" + receitas (Palavra por palavra … Typewriter) →
  `aplicarPresetNoAnimador` / `addTextAnimator` / remove; lista dos
  animadores (exceto "Espaçamento"): rótulo = nome, toggle ligado +
  lixeira 44 × 44 (`removeTextAnimator`, sem passo único).

### 5.9 Forma (`paineis/forma.dart`)

Título **Forma**. Abas **Forma · Cor · Traço · Desenhar · Operadores**.
Ação `scribble` (destaque) = editor de pontos (5.10). Com o painel aberto o
palco mostra as alças da forma.

**Aba Forma** (paramétrica): **Tipo** D (Retângulo · Elipse · Polígono ·
Estrela · Setor · Seta · Linha · Lua · Flor · Mais · Selo · Gota · Balão) e,
em seguida, SÓ os parâmetros daquele tipo (`parametrosDaForma`,
`domain/shape.dart:1142-1210`), todos N com ◆ + curva
(`editShapeParam(id, chave, t, v)` / `toggleShapeParamKeyframe`):

| Chave | Rótulo (por tipo) | Faixa | Unid / casas |
|---|---|---|---|
| sizeX | Largura (Comprimento em Seta/Linha; Tamanho em Mais) | 0–2000 | casas 0 |
| sizeY | Altura | 0–2000 | |
| roundness | Cantos + linha **Canto em** (Pí "% do lado" · "px") | 0–400 px ou 0–100 % | "%" no modo % |
| points | Pontas | 2–20 | casas 1 |
| outerRadius | Raio externo (Raio em Lua/Gota) | 0–1000 | |
| innerRadius | Raio interno | 0–1000 | |
| outerRoundness / innerRoundness | Suavidade externa / interna | 0–100 | |
| shapeRotation | Giro do desenho | −360–360 | ° |
| startAngle | Angulo inicial (sic) | −360–360 | ° |
| sweep | Abertura | 0–360 | ° |
| sectorInner | Furo | 0–1000 | |
| larguraDaCauda / larguraDaCabeca / comprimentoDaCabeca | Largura da haste / Largura da ponta / Comprimento da ponta | 0–1000 | |
| largura | Largura (Largura da cauda no Balão; Espessura no Mais) | 0–1000 | |
| deslocamento | Recorte | 0–1000 | |
| picote / espacamento | Picote / Espaçamento | 0–300 | |
| cauda | Cauda | 0–1000 | |
| aperto | Aperto | 0,5–10 | casas 1 |
| larguraDaPonta | Bico | 0–300 | |
| caudaX / caudaY | Cauda X / Cauda Y | −1000–1000 / 0–1000 | |

Caminho desenhado (não paramétrico): aviso + "Converter para paramétrica"
(`arrow_2_squarepath`) + porta "Editar pontos".

**Aba Cor**: **Preencher** Pí (Nenhum · Cor · Degradê · [Mídia]) ·
**Cor** C (só se o preenchimento vem antes do traço) · degradê: **Cor 1**,
**Cor 2**, **Ângulo** (−360–360 °), **Radial** T · porta "Mais opções de
preenchimento" → painel Cor.

**Aba Traço**: **Traço** T · **Cor** C · **Espessura** 0–200 (casas 1) ·
**Opacidade** 0–100 % · **Tracejado** 0–300 · **Vão** 0–300 · **Deslocar
tracejado** −2000–2000 — números com ◆ + curva
(`editShapeItemTrack(id, traço, chave, t, v)`).

**Aba Desenhar** (Trim): **Desenhar** T · aviso "O traço se desenha de um
ponto a outro. Anime Início e Fim." · **Início** 0–100 % · **Fim** 0–100 %
· **Deslocamento** −100–100 % (◆ + curva).

**Aba Operadores** (`operadores_da_forma.dart`): aviso "Operadores mudam o
caminho da forma: repetir, desenhar, combinar, torcer. Adicione um
abaixo." quando vazio; cada operador numa `AureaSection` com o nome:

| Operador | Linhas |
|---|---|
| Trim Paths | Modo Pí (Contínuo · Individual); Início / Fim 0–100 % ◆; Deslocamento −100–100 % ◆ |
| Repeater | Cópias 1–50; Deslocar X / Deslocar Y −400–400; Rotação −180–180 ° |
| Morph (título "Morph · A → B") | Progresso 0–100 % ◆ |
| Deslocar caminho | Distância −300–300 |
| Arredondar cantos | Raio 0–300 |
| Zig zag | Altura 0–300 |
| Inchar e encolher | Força −100–100 % |
| Torcer | Ângulo −720–720 |
| Bagunçar caminho | Quanto 0–300 |
| Combinar caminhos | Modo D (Unir · Subtrair · Interseção · Excluir) |
| (todos) | "Remover" / "Desfazer o morph" (vermelho) |

Seção ADICIONAR: chips Trim Paths · Repeater · Deslocar · Arredondar · Zig
zag · Inchar · Torcer · Bagunçar · Combinar · "Morfar para…" (menu Círculo,
Retângulo, Estrela, Polígono, Coração, Arco). Seção GEOMETRIA COMPOSTA:
`+ Retângulo`, `+ Círculo`.

### 5.10 Pontos (`paineis/pontos.dart`) — editor de nós (trackpad)

Título **Pontos**. Cabeçalho: `AureaKeyframeButton` ‹◆› 64 × 44 (marca a
forma do caminho no quadro = morph) + ✓ (fecha e solta o alvo do palco).
Corpo padding L22 T0 R22 B8:

```
[modos 36: chips Mover (hand_draw) · Alça (arrow_up_right_diamond) · Novo (plus_circle)]
[4]
[TRACKPAD Expanded — fundo campo raio 8; cantos em L 14 dp traço 1,5 muted 70 %;
 cruz central 24 dp + círculo r4 (destaque no modo Novo); dica 10 sp no canto:
 "Deslize até um ponto e toque" | "Deslize para mover o ponto" |
 "Selecione um ponto antes" | "Deslize para puxar a alça" | "Deslize e toque para cravar"]
[6]
[ações 32, rolam: ‹ 32×32 · "2/5" (valor) · › · Canto/Suavizar · Apagar · Fechar/Abrir ·
 (Alça) Saída/Entrada · Alças iguais · (Mover) Mover tudo · (forma) ⧉ "1/2" ⧉ · Contorno]
```

- O dedo nunca cobre o desenho: arrastar no trackpad move um CURSOR em cruz
  no palco (delta convertido pela escala e rotação da camada); tocar
  seleciona o nó mais perto (raio 26) ou crava um novo; toque duplo alterna
  canto/suave. Um arrasto = um passo de desfazer.
- Com o painel de 200 o trackpad fica com ~76 dp de altura (bug B-19).

### 5.11 Áudio (`paineis/audio.dart`)

Título **Áudio**. Sem som: aviso "Esta camada não tem som." / "Esta camada
não tem áudio."

| Rótulo | Tipo | Faixa · unid · casas | ◆ | Engine |
|---|---|---|---|---|
| Volume | N | 0–400 %, casas 0 | ◆ (envelope, relógio cru do clipe) | `updateAudioSpec(volumeEditado)`; reset apaga o envelope |
| (aviso) | "O volume tem keyframes: toque no losango para marcar este instante." (animado fora da marca) | | | |
| Mudo | T | | | `muted` |
| Ganho | N | 0–4 ×, casas 2, reset 1 | | `gain` |
| Fade de entrada / Fade de saída | N | 0–teto s (teto = clamp(duração, 0,5, 10)), casas 2, reset 0 | | `fadeIn/fadeOut` |
| ABAIXAR PELA VOZ → Voz | D | "Nenhuma" + camadas com som (nomes crus) | | `duckAgainstId` |
| → Quanto desce (com voz) | N | 0–100 %, reset 70 | | `duckAmount` |
| EFEITOS DE ÁUDIO | cartões | 5.11.1 | | |
| VOZ E EQ (fechada) → Limpar ruído · Voz · De-esser | N | 0–100 %, reset 0 | | `processing.*` |
| → Graves · Médios · Agudos | N | −12–12 dB, casas 1, reset 0 | | `lowDb/midDb/highDb` |
| chips | Normalizar (`speedometer`) · Remover silêncio (`scissors`) · Copiar som · Colar som (se copiado) · Batidas e BPM (`metronome`) | | | `normalizeAudio` (aviso "Ganho ajustado para X dB"), `removeSilence` (aviso + Desfazer), `somCopiado`, folha Batidas |

#### 5.11.1 Efeitos de áudio (também é o painel Efeitos de uma camada de áudio)

Um `AureaEffectCard` por efeito (olho liga/desliga; ⋯ Subir · Descer ·
Resetar · Apagar); corpo: parâmetro com opções = D; senão N (casas 2 se a
faixa ≤ 20, senão 0; reset ao inicial; SEM keyframe). Linha de estado
"Preparando som..." / erro do render. Chip `+ Efeito de áudio` → menu com:
Backwards · Reverso, Compressor, Delay · Eco, Distortion · Distorcao,
Flange & Chorus, Gate, High-Low Pass · Filtro, Modulator · Modulador,
Parametric EQ · Equalizador, Reverb · Reverberacao, Stereo Mixer · Mixer
estereo, Tone · Gerador de tons.

### 5.12 Folhas Batidas e Pulsar (`paineis/batidas.dart`)

**Batidas** (`mostrarAureaFolha`, título "Batidas", padding 22/4/22/15):
FAIXA DE FREQUÊNCIA Pí (Grave · Médio · Agudo · Tudo) + aviso ·
**Sensibilidade** N 0–100 (estado da folha, não anima) · SUBDIVISÃO Pí
(1/1 · 1/2 · 1/4 · 1/8) + aviso · **Andamento** "128.0 bpm · 64 marcas"
(estilo valor) ou "ainda não analisado", com `minus_circle` / `plus_circle`
(44 × 38) = `setBpm(±1)` · linha inteira "Marcar as batidas na timeline" +
toggle · botão principal (44, `destaqueApagado`, raio 8, texto destaque
w600, háptico): "Detectar batidas" / "Detectar de novo" / "Ouvindo a
faixa..." · chips (com batidas): Marcar na timeline (`flag`) · Cortar nas
batidas (`scissors`) · Limpar (`delete`).

**Pulsar na batida**: texto '"<nome>" cresce um tiquinho em cada ataque da
música.' · OUVIR DE Pí (fontes de som) · **Força** 2–60 % (padrão 12) ·
"N batidas encontradas." / "Lendo o som..." · botão "Aplicar"
(`applyBeatPulse`, fecha e avisa com Desfazer) · "Tirar os keyframes de
escala".

### 5.13 Tempo (vídeo) e Velocidade (áudio) (`paineis/tempo.dart`, `velocidade.dart`)

Seção comum `SecaoDaVelocidade`:

| Rótulo | Tipo | Faixa · detalhe | Engine |
|---|---|---|---|
| Velocidade | N | 0,1–10 ×, casas 2, sens 0,01 ×/dp, reset 1 | `setClipSpeed(id, v, modo)` |
| (aviso com Time Remap) | "Este clipe tem Time Remap: mudar a velocidade constante desfaz a curva." | | |
| chips | 0.25× · 0.5× · 1× · 2× · 3× (aceso se igual ±0,01) | | um passo |
| Ao mudar | D ("O que acontece com a barra") | Estender início · Cortar início · Cortar fim · Estender fim (lembrado em prefs; padrão Estender fim) | modo da compensação |
| Rampa (vídeo) | D ("Rampa de velocidade") | "Escolher rampa" + Impacto, Heroi, Bala, Montagem, Lento → Rápido, Rápido → Lento, Soco, Flow… | `applySpeedRamp` |
| Manter tom (com som) | T | | `setClipPreservePitch` |

Tempo acrescenta (só vídeo): **Reverso** T (+ proxy de GOP curto) ·
**Blur de velocidade** T · **Interpolação** D ("Interpolação de quadros":
Nenhuma · Mistura · Fluxo óptico · Fluxo óptico (IA)) + selo (ex. "prévia:
quadro mais próximo · exportação: fluxo óptico") · CONGELAR QUADRO
(fechada): **Duração** 0,1–10 s (estado local, padrão 1), **Onde** D (Clipe
separado · Dentro do clipe), chip "Congelar aqui" (`snow`, `freezeFrame`;
aviso "Leve o cabeçote para dentro de um clipe de vídeo") · EFEITOS DE
TEMPO: chips Time Remap · RGB Time Warp · Posterize Time (✓ aceso se já
aplicado) → aplica se falta e abre o painel Efeitos. Outras camadas: aviso
"A velocidade vale para vídeo e áudio…". Velocidade (áudio) = só a seção.

### 5.14 Borda e sombra (`paineis/borda_sombra.dart`)

Título **Borda e sombra** (ferramenta "Borda"). Áudio: "Camada de áudio não
tem borda." Números casas 0; NENHUM tem losango (bug B-05).

| Seção | Rótulo | Tipo | Faixa / opções |
|---|---|---|---|
| TRAÇO (só forma) | Traço | T | `ensureShapeStroke` / `removeShapeStroke` |
| | Cor do traço | C | |
| | Espessura | N | 0–200 px |
| | Ponta | D | Reta · Redonda · Quadrada |
| | Junção | D | Chanfro · Redonda · Mitra |
| | Início / Fim | D ("Ponta do traço") | Nenhuma · Seta · Seta cheia · Seta vazada · Círculo cheio · Círculo vazado · Losango · Losango cheio · Quadrado · Quadrado cheio · Gota cheia · Gota vazada · Linha em T |
| | Tamanho das pontas | N | 1–10 ×, casas 1 (só com ponta) |
| BORDAS | cartão "Borda N" (aberto, olho, ⋯ Subir · Descer · Apagar) | | Cor C · Posição D (Fora · Dentro · Centro) · Espessura 0–100 px · Opacidade 0–100 % |
| | `+ Adicionar borda` | chip | até 4; nasce por fora da última (+6 px), cor alternada branco / `#0F141A` |
| SOMBRA | Sombra | T | |
| | chips prontas | Suave · Dura · Longa · Contato | mantêm a cor |
| | Cor · Opacidade (0–100 %) · Ângulo (−360–360 °) · Distância (0–300 px) · Desfoque (0–120 px) · Espalhar (0–100 px) | C / N | |
| SOMBRA INTERNA (aberta se ligada) | Sombra interna + as mesmas 6 | | padrão 35 %, 270°, 4, 12 |
| BRILHO (aberta se ligado) | Brilho · Cor do brilho · Opacidade · Tamanho (0–120) | | |
| (chips) | Estilos prontos (`square_grid_2x2` → Presets › Estilos) · Salvar estilo (`bookmark` → diálogo "Nome do estilo") | | |

### 5.15 Gradiente vetorial (`paineis/degrade.dart`)

Folha NÃO modal "Gradiente vetorial", grande, altura clamp(55 % da tela,
280, 560), padding 22/0/22/15. Por degradê da forma, seção fixa "CORES E
DISTRIBUICAO" (sic):

| Rótulo | Tipo | Faixa |
|---|---|---|
| (amostra) | faixa 28 dp, raio 4, `LinearGradient` das paradas | |
| Animar cores | T + ◆ (só animado; igualdade EXATA de tempo) + setas | liga = marca as cores de agora; desliga = congela e apaga marcas |
| Radial | T | |
| Cor N / Posição N (por parada) | C / N | posição 0–100 %, presa entre as vizinhas |
| Ângulo | N | −180–180 ° |
| Centro X / Centro Y | N | −1–1, casas 2 |
| Alcance | N | 0,05–3, casas 2 |

Não há adicionar/remover parada nesta folha.

### 5.16 Presets (`paineis/presets.dart`) — TELA (rota Cupertino)

Fundo `painel`. Cabeçalho 44: ‹ 44 × 44 (`chevron_left` 20) · "Presets"
17 sp w600 · importar (`tray_arrow_down` 20, 44 × 44) · 6. Abas "Efeitos"
· "Estilos" (visual do AureaTabs, 38). Busca "Buscar" (padding 22/6/22/8).
Lista (padding 22/0/6/15), linha 57:

```
[prévia 40 raio 4] 10 [nome 13 sp w600 / detalhe 10 sp muted] [⋯ 44×44 ícone 16]
```

Prévia de efeito = `PreviaDoEfeito` do 1º efeito (animada); de estilo =
pintor do acabamento (quadrado 52 % raio 18 % sobre `campoAlto`). Detalhe:
"1 efeito"/"N efeitos" · partes "borda · sombra" · "sem acabamento".
Toque = aplica na camada (`applyPreset(at)` / `aplicarEstilo`) e volta.
Toque longo ou ⋯ = folha de ações: nome · Aplicar nesta camada · Exportar
para um arquivo (`.aurea.json`) · [meus] Renomear · Excluir (sem
confirmação). Vazio: "Nada aqui ainda. Salve um preset a partir de uma
camada e ele aparece nesta lista, em todos os projetos."

### 5.17 Grupo (`paineis/grupo.dart`)

Entrar no grupo (`arrow_down_right_square`, fecha o painel) · Desagrupar
(`square_split_2x2`) · **Duração interna** N 0,1–600 s casas 2
(`updatePrecomp(sourceDuration)`) · "Igualar à barra (x.xx s)" (só com
duração própria) · **Remapear tempo** T (só projetos antigos, só desliga) ·
**Colapsar** T · **Recortar no quadro** T.

### 5.18 Clonar (nulo) (`paineis/clonar.dart`)

Sem grade: aviso "Escolha as camadas que a grade vai repetir." + porta
"Escolher camadas". Com grade, abas **Grade · Variação · Proximidade**:

| Aba | Linhas |
|---|---|
| Grade | porta "Camadas da grade (N)" (folha com `AureaLayerRow` 37 por camada; toque alterna) · **Arranjo** Pí (Retangular · Radial · Esférico; nenhuma acesa com morph animado) · **Morph** 1–3 casas 2 ◆ · **Colunas** 1–12 · **Espaço X** / **Espaço Y** 20–800 ◆ · **Raio** 40–1200 ◆ · **Rotação** −180–180 ° ◆ · "Remover a grade" (vermelho) |
| Variação | **Torção** −180–180 ° ◆ · **Escalonar** −360–360 ° ◆ · **Profundidade** −400–400 ◆ · **Escala na frente** / **Escala atrás** 10–300 % ◆ · **Acaso** 0–300 ◆ · **Semente** 0–100 · **Embaralhar** T · **Controlador** D ("—" + nulos) |
| Proximidade | **Proximidade** T · **Centro** P · **Alcance** 20–800 · **Escala máxima** 20–400 % · **Atrair** −300–300 · aviso "O alcance é uma esfera: também vale em profundidade." |

### 5.19 Legendas (`paineis/legendas.dart`)

Abas **Falas · Estilo**.

- Falas (padding 22/4/10/15), linha 44: tempo 72 × 34 (campo raio 4,
  timecode 11 sp; `destaque` se a fala está travada) → leva o cabeçote ·
  campo de texto 1 linha (campo raio 4, padding 8 × 6) → `updateCueText`
  (trava a fala) · ✕ 40 × 44 → `removeCue`.
- Estilo: aviso se não há tempo por palavra · chips "Comum" + prontos ·
  (ativo) **Cor do destaque** C · **Cor do contexto** C · **Destaque**
  100–320 % reset 190 · **Arranjo** D (Atravessada · Empilhada · Dupla ·
  Costura · Sozinha · Viral) · **Maiúsculas** T · **Espaçamento** −6–8
  casas 1 · **Entrelinha** 70–180 % reset 105 · **Inflar** 60–600 ms ·
  **Contexto** Pí (0 … máx por lado) · **Fonte do destaque** D · **Fonte do
  contexto** D.

### 5.20 Menu do campo e folha "Animar sozinho" (`paineis/menu_do_campo.dart`)

**Menu do campo** (toque longo na caixa de valor de Transformar ou ⋯ do
cabeçalho): `AureaMenu`, título = nome da aba:

| Item | Quando | Ação |
|---|---|---|
| Expressão… (`function`, marcado se há) | Pro e prop ∈ {opacidade, rotação, escala, inclinação} | `showExpressionEditor` → `setPropExpression` |
| Animar sozinho… (`wand_stars`, marcado se há) | sempre | folha abaixo |
| Expor no projeto / Tirar de Propriedades (`slider_horizontal_3`) | opacidade (0–1), rotação (−360–360), escala (0–4) | `exposeProperty` / `unexposeProperty` + aviso "<nome> exposta em ⚙ Propriedades do projeto" |

**Animar sozinho** (`mostrarAureaFolha(modal:false)`, título "Animar <nome>
sozinho"): **Forma** Pí (tipos do animador) + aviso explicativo · **Força**
(ou Força X / Força Y em pontos): somar −2000–2000 (unid da aba) ou
multiplicar 0–400 % · **Volta** 0,05–30 s casas 2 · **Começo** 0–100 % ·
**Sorteio** 1–999 (só aleatório) · **Modo** Pí ("Somar ao valor" · "Por
cento do valor"; trocar reseta a força para 20/40 ou 20 %) · "Tirar o
animador" (vermelho, fecha).

---

## 6. 3D, partículas, câmera, luz, rastrear — só estrutura

São funções FUTURAS no motor novo; o que importa é que usem as MESMAS peças
(AureaPanel + AureaPropertyRow + chips + seções). Peças próprias do 3D
(`paineis/comum_3d.dart`):

- `LinhaDeFichas<T>`: `AureaPropertyRow.personalizada` com chips numa
  fileira que rola na horizontal (vão 6).
- `GradeDeAcoes`: blocos `AureaToolbarButton(bloco:true)` de 57 de altura,
  4 por fileira, vão 6; bloco = ícone 32 + 4 + rótulo 10 sp 1 linha, fundo
  `campo` (aceso `destaqueApagado`), raio 5, padding h4; desabilitado =
  muted 45 %.
- `EscolhaDoObjeto` (linha "Objeto"), `linhaDeCor`, `FilaDoTexto`,
  `paddingDoPainel` (= 22/4/22/15). Nenhum painel 3D tem prévia própria:
  tudo aparece no palco.

| Painel | Título / abas | Linhas principais (rótulos literais) |
|---|---|---|
| Texto 3D (`texto3d.dart`) | **Texto 3D**; abas Texto · Extrusão · Material · Luz · Caracteres | Fonte (D + "Importar fonte (.ttf / .otf)"), Espaçamento, Qualidade (Baixa · Média · Alta); Profundidade, Chanfro (Sem chanfro · Angular · Redondo), Tamanho do chanfro; material (Predefinição, Cor base, Metal, Rugosidade, Brilho próprio); Reflexo, Iluminação, Ambiente; Seleção (Todas as letras · Um caractere · Intervalo), Letras, De, Até |
| Material (`material.dart`) | **Material** | Origem (Próprio…), Predefinição, Cor, Metal, Rugosidade, Brilho próprio, Cor do brilho, Opacidade, Tipo (Realista · Sem luz · Transparente · Recorte), Corte do alfa, Face dupla; Elemento 3D: porta "Cor e material do elemento" |
| Luz (`luz.dart`) | **Luz** | Qual luz, Tipo (Direcional · Ponto · Ambiente · Foco), Cor, Intensidade, Alcance, Cone, Suavidade, Sombra, "Adicionar luz", "Remover luz" |
| Ambiente (`ambiente.dart`) | **Ambiente** | Estúdio (D), Reflexo, Luz ambiente, Cor do céu, Cor do chão |
| Animação 3D (`animacao3d.dart`) | **Animação** (posições Entrada · Ênfase · Saída) | Clipe, Velocidade, Começar em, Repetir |
| Cena (`cena3d.dart`) | **Cena**; abas Transformar · Material · Iluminação · Ambiente · Animação · Propriedades | Adicionar cubo; seções Posição · Rotação · Escala; Enquadrar, Apontar câmera, Câmeras e cortes; Visível, Bloqueado, Etiqueta, Pai na cena, Detalhe (Automático · Alto · Médio · Baixo), Triângulos, Crédito, Tamanho, Subdivisões, Duplicar, Apagar |
| Câmeras (folha, `cameras.dart`) | **Câmeras** | Transição, "Nova câmera (enquadramento atual)", seção "Seguir um nulo da composição" (Nulo: Nenhum…), seção "Tomadas" (linhas por tomada), "Limpar tomadas" |
| Câmera (camada câmera, `camera.dart`) | **Câmera**; abas Lente · Foco · Neblina | Projeção (Perspectiva…), Ângulo de visão, Lente; Desfoque de foco, Distância do foco, Intensidade, Profundidade de campo; Neblina, Cor da neblina, Começa em, Cobre tudo em |
| Elemento 3D (folha, `elemento3d.dart`) | **Elemento 3D**; abas Forma · Material | Tipo, Tamanho, Arestas, Modelo ("OBJ / FBX"), Imagem ("Escolher"), Reflexo, Ambiente, Acabamento, Brilho, Cor, Degradê |
| Extrude (folha, `extrude.dart`) | **Extrude 3D** | Espessura |
| Partículas (`particulas.dart`) | **Partículas**; abas Principal · Emissor · Movimento · Aparência · Faíscas | Desenho (Esfera · Estrela · Risco · Nuvem · Quadrado · Anel), Emissão/s, Quantidade, Vida, Velocidade, Gravidade, Turbulência, Tamanho, Opacidade; Forma (Caixa · Ponto · Esfera · Linha · Anel), Saída (Cone · Todas · Para fora), Fundo (Z), Nascimentos/s, Vida aleatória, Semente; Velocidade, Direção, Abertura, Gravidade, Freio do ar, Turbulência, Detalhe, Evolução, Centro X/Y/Z; Tamanho, Tamanho aleatório, Tamanho na vida (Fixo · Cresce · Encolhe · Sobe e desce), Opacidade, Opacidade aleatória, Opacidade na vida (Entra e sai · Some · Aparece · Fixa), Brilho, Rastro, Giro, Cor final, Cintilar; Por partícula, Vida, Herda do pai, Força, Tamanho, Começa em |
| Rastrear (`rastrear.dart`, 1129 linhas) | **Rastrear**; abas Analisar · Pontos · Criar · Seguir objeto | Analisando (progresso), Tomada, "Criar a cena", Qualidade, Nuvem, Motor; aviso "Analise o vídeo primeiro: os pontos aparecem sobre ele."; Escolher, Na mão, Chão automático, Chão escolhido, Origem aqui, Escala real, Apagar, Apagar ruins, Soltar; criar: Texto, Forma, Nulo, Placa, Imagem, Modelo 3D; seguir: Objeto, Crescer junto, "Qual camada gruda nesse objeto?". Os pontos da nuvem são pintados NO PALCO (`PontosDoRastreioNoPalco`), cor pela qualidade; tocar escolhe |
| Legendas, Clonar, Grupo | ver 5.17–5.19 | |

---

## 7. Exportação (`features/export/presentation/`)

Porta única do VÍDEO: `ExportVideoScreen` (rota de tela cheia, aberta pelo
"Exportar" da barra do topo). O que não é vídeo mora na folha "Outros
formatos". Cores lidas de `AmColors` (não de `AureaCores`).

### 7.1 Layout

```
Scaffold fundo `palco` #0A0E13, SafeArea
┌ barra 50, fundo `cromo` #0F141A, padding h6 ─────────────────────┐
│ [✕ xmark 19, padding h10]  "Exportar video" 15 sp w600 texto      │  (✕ cancela e sai)
├───────────────────────────────────────────────────────────────────┤
│ Expanded: composição em tamanho REAL dentro de FittedBox(contain), │
│ padding 16 (é a própria árvore que é fotografada quadro a quadro) │
│ durante o render: CAPA cobrindo tudo (fundo `palco`) = progresso  │
├───────────────────────────────────────────────────────────────────┤
│ rodapé (altura máx 62 % da tela), fundo `cromo` #0F141A            │
└───────────────────────────────────────────────────────────────────┘
```

### 7.2 Rodapé — fase Ajustes

Área rolável (padding L14 T10 R14 B4) + botão fixo (padding 14/8/14/16):

1. `Wrap` (vão 8 × 8) de fichas de predefinição (`_FichaDePredefinicao`):
   padding 14 × 9, raio 8, fundo `campo` (acesa `destaqueApagado #1D3A55`),
   nome 13 sp w700 (`texto` / aceso `destaque`), 2, detalhe 10,5 sp muted:
   - **Reels / TikTok** — "Tamanho do projeto · 30 fps" (fps 30, alta)
   - **YouTube 1080p** — "1080p · fps do projeto" (1080p, alta)
   - **Maxima qualidade** (sic) — "Tamanho do projeto · alta"
   - **Personalizado** — "Abrir os ajustes" (acesa quando nenhum preset bate;
     toque abre AJUSTES)
   Ao abrir, os ajustes da ÚLTIMA exportação voltam (prefs `exportar.ajustes`).
2. 12 · **Resumo** 12,5 sp altura 1,35 muted:
   `"1080 x 1920 · 30 fps · ~45 MB · 0:15"` (PNG: `"… · PNG com
   transparencia · 0:15"`). Duração = `duracaoDoConteudo` (o que sai).
3. Cabeça de grupo **AJUSTES** (42, chevron 13, 12 sp w700 +0,6 muted,
   traço 40 × 1) → abre/fecha o corpo:

| Linha | Opções (pílulas: padding 11 × 6, raio 8, fundo `campo` / aceso `destaqueApagado`, 12 sp `texto` / `destaque`) |
|---|---|
| Formato | MP4 · Sequencia PNG |
| Tamanho | Original · 4K · 1440p · 1080p · 720p · 480p ("Np" = LADO MENOR = N; lados pares) |
| Quadros | Projeto (N) · 24 · 25 · 30 · 50 · 60 |
| Codec (só MP4) | H.264 · HEVC (H.265) |
| Qualidade (só MP4) | Baixa · Media · Alta · Na mao |
| Taxa (só "Na mao") | chip "Taxa" 94 × 32 + `AmTickRuler` ARRASTÁVEL 1–120 Mb/s (119/420 por dp, altura 40, leitura de posição) + "12 Mb/s" 11 sp (62 de largura, à direita). Padrão ao escolher Na mão: 12 |

   Cada linha: chip do rótulo 94 × 32 (12 sp w600 muted, 2 linhas) + 8 +
   `Wrap` (6 × 6) de pílulas; padding vertical 6.
4. Cabeça de grupo **OUTROS FORMATOS** → folha (7.5).
5. Botão fixo **Exportar**: altura 50, raio 12, fundo `acao #245D8C`,
   15 sp w700 `sobreAcao`, háptico.

Estimativa de tamanho (`export_settings.dart:161-184`):
`bitrate = manual×10⁶ (preso 0,2–200 Mb/s)` ou
`w × h × fps × bpp × fator`, `bpp` alta 0,20 / média 0,12 / baixa 0,07,
`fator` HEVC 0,65 / H.264 1, preso a 1–120 Mb/s;
`MB = bitrate × segundos / 8 / 1024²` (≥ 1000 MB vira "~x,x GB").

### 7.3 Progresso (capa sobre a composição; o rodapé some)

```
CupertinoActivityIndicator (raio 14)
[14] "NN%"  34 sp w700 texto
[6]  etapa  14 sp w600: Preparando | Lendo os videos | Desenhando os quadros | Codificando
[10] barra: largura máx 260, altura 5, raio 6, fundo `campo`, valor `destaque`
[10] "faltam ~1 min 05 s" | "faltam ~12 s" | detalhe (11 sp muted, até 2 linhas)
[20] [ Cancelar ]  (CupertinoButton fundo `campo`, raio 12, padding 12×22, 13 sp `destaque`)
[10] "Deixe o app aberto ate terminar."  10 sp muted
```

Tempo restante só depois de 5 %: `total = gasto / progresso`. Detalhes de
etapa: "Preparando...", "Verificando se da para copiar...", "Conferindo o
espaco em disco...", 'Lendo "<camada>" (i de n)'.

Fases do motor (para o port): preparando → atalho "corte puro" (copia sem
recodificar quando MP4 + Original + H.264 + fps do projeto + sem taxa
manual) → lendo vídeos (quadros por camada de vídeo, também dentro de
grupos) → desenhando → codificando (em fluxo pelo codificador de hardware;
sem ele, PNG + FFmpeg; sequência PNG sempre em arquivos) → publica na
galeria → pronto / erro. Cancelar volta aos ajustes.

### 7.4 Fim e erro

- **Pronto** (padding 18/14/18/20): ícone 17 `checkmark_seal_fill`
  (`destaque`) ou `exclamationmark_triangle` (`perigo`) + 8 + título 14 sp
  w700: "Exportado" · "Sequencia pronta" · "Exportado, mas nao entrou na
  galeria"; se não entrou: mensagem 12 sp + caminho 10 sp muted; botões:
  [Abrir] [Compartilhar] (chip, se há URI) ou [Copiar caminho]; [Concluir]
  (`CupertinoButton` `destaque`, raio 12, padding v12, 13 sp w700 onAccent).
- **Erro**: triângulo `perigo` + "Nao deu para exportar" 14 sp w700
  `perigo`; mensagem 11 sp altura 1,4 muted; botão grande "Tentar de novo"
  (volta aos ajustes com AJUSTES aberto).

### 7.5 Folha "Outros formatos" (`outros_formatos.dart`)

`mostrarAureaFolha(titulo 'Outros formatos')`, altura máx 70 %, padding
22/4/22/15: LEGENDAS ("Legendas (.srt) · <nome>" por camada de legenda) ·
PARA PRODUTO (LOTTIE / SVG) (modo compatível com Lottie, bloqueios "N
camada(s) NÃO sobrevivem:", "Exportar Lottie (.json)", "Exportar SVG
animado") · TEMPLATE ("Exportar template") · "Exportar pacote .aurea". Linha
de status (erro em vermelho). Salva via seletor de arquivo do sistema.

---

## 8. Menus, diálogos, folhas, avisos

### 8.1 Menu flutuante `AureaMenu` / `mostrarAureaMenu` (`core/ds/aurea_menu.dart`)

- Largura FIXA 250; itens de 40; altura máx min(450, tela − 16), rola por
  dentro; fundo `elevado #1B2530`, raio 8, SEM sombra e SEM borda; padding
  vertical 4.
- Título opcional (estilo `secao`, padding 15/6/15/4).
- Item: `[barra 4 × 24 destaque se marcado][10][ícone 20 + 10 opcional][rótulo 13 sp, 1 linha …][10]`;
  cor do texto/ícone: desabilitado muted 50 %, destrutivo `perigo`,
  marcado `destaque`, senão `texto`. Apertado = fundo `campoAlto`. Sem
  divisores.
- Posição: embaixo do botão (+4) se couber, senão em cima (−4); alinhado
  pela DIREITA do botão; preso a 8 dp das bordas da tela.
- Véu `palco` 25 %, toque fora fecha; entra em 100 ms (fade + escala 0,96→1
  a partir do canto superior direito, desacelerando), sai acelerando.
- Usado por: dropdowns, ⋯ do cartão de efeito/máscara/borda/efeito de
  áudio/animador, menu do campo, etiqueta, "Morfar para…", prontos, apagar
  fonte, adicionar efeito de áudio.

### 8.2 Dropdown `AureaDropdown` (`core/ds/aurea_dropdown.dart`)

Caixa 34 de altura (largura = a disponível ou dada), padding h10, fundo
`campo`, raio 4, rótulo 13 sp (1 linha …), `chevron_down` 12 muted. Toque →
`AureaMenu` com a atual marcada; título opcional. `traduzir:false` para
conteúdo (nomes de fonte/camada/colunas).

### 8.3 Folha `AureaBottomSheet` / `mostrarAureaFolha` (`core/ds/aurea_bottom_sheet.dart`)

- `showModalBottomSheet`, `isScrollControlled`, **arrastar para fechar
  DESLIGADO** (dentro dela quase tudo se ajusta arrastando), `useSafeArea`.
- Fundo `painel #151C24`, topo raio 13,5, sem sombra, sem borda.
- Véu: modal `palco` 35 %; "não modal" = véu transparente (ver bug B-15:
  continua bloqueando o toque no editor).
- Entrada 100 ms (200 ms se `grande`), saída 100 ms; desacelera / t².
- Cabeçalho opcional 44 (38 + 6): `[22][título 14 sp w600][ações][✕ 44×38 ícone xmark 20 muted][6]`;
  sem título = respiro de 10 no topo. `altura` fixa opcional.
- Folhas desta área: catálogo (grande, min(560, 62 %)), detalhe do efeito
  (grande, min(520, 66 %)), editor de curva (não modal, 316), Animar
  sozinho (não modal), Gradiente vetorial (não modal, grande, 55 % entre
  280 e 560), Batidas, Pulsar, Camadas da grade, ações de preset, Outros
  formatos, Câmeras, Elemento 3D, Extrude.
- Teclado numérico e seletor de cor usam `showModalBottomSheet` próprio
  (3.6, 3.8.1).

### 8.4 Diálogos (Cupertino do sistema)

`CupertinoAlertDialog` com um `CupertinoTextField` (autofocus): "Nome do
preset" (`core/ui/pedir_nome.dart`, Cancelar / OK), "Nome do estilo"
(Cancelar / Salvar), renomear preset (Cancelar / Salvar), "Expressão · …"
(Limpar / Cancelar / OK). O estilo é o do Cupertino (não do DS) — ver B-16.

### 8.5 Avisos (`AureaSnack`, `core/ui/snack.dart`)

`SnackBar` Material flutuante; UM por vez (limpa o anterior); duração
padrão 4 s (+250 ms de rede de segurança); com ação ("Desfazer",
"Desbloquear") ou ícone de fechar quando não há ação. `showReasonToast` =
1,5 s. Mensagens típicas desta área: "Preset salvo para todos os projetos:
…", "Movimento assado em keyframes" + Desfazer, "Ganho ajustado para X dB",
"N pedaços, sem as pausas" + Desfazer, "Código copiado: …", "Não é um
código de cor", "Camada bloqueada: desbloqueie para editar os pontos" +
Desbloquear.

### 8.6 Menus de contexto (⋯) — resumo

| Onde | Itens |
|---|---|
| Cartão de efeito | 4.4 |
| Cartão do Animador de Texto | Resetar · Apagar |
| Cartão de máscara | Subir · Descer · Editar pontos · Trocar por retângulo · Trocar por elipse · Usar a forma da camada · Desenhar à mão · Apagar |
| Cartão de borda | Subir · Descer · Apagar |
| Cartão de efeito de áudio | Subir · Descer · Resetar · Apagar |
| Caixa de valor (Transformar) | 5.20 |
| Linha de preset | folha: Aplicar nesta camada · Exportar para um arquivo · Renomear · Excluir |
| Fonte importada (toque longo) | Apagar fonte do aparelho |

---

## 9. Bugs, hacks e riscos (arquivo:linha no HEAD)

Caminhos relativos a `Aurea/lib/src/`; `paineis/` =
`features/editor/presentation/ui/paineis/`; `ds/` = `core/ds/`.

### 9.1 Layout, texto cortado, alvos pequenos

| # | Onde | Problema |
|---|---|---|
| B-01 | `ds/aurea_property_row.dart:212-227`; `paineis/efeitos/linhas_do_efeito.dart:31-46` | Rótulo numa linha de 76 dp com `FittedBox(scaleDown)`: nomes longos ENCOLHEM a fonte até ficarem ilegíveis ("Deslocamento de fase horizontal", "Largura do mosaico", "Tamanho das pontas", "Deslocar tracejado", "Profundidade de campo" ≈ 6–8 sp). A âncora [A] usava chip de 94 com DUAS linhas. |
| B-02 | `ds/aurea_property_row.dart:237-247, 271-277` | A linha PULA quando nasce a 1ª marca: as setas (36 dp) aparecem e a régua encolhe com o dedo em cima. `AureaKeyframeButton` (largura fixa 64) existe exatamente para evitar isso, mas a linha não o usa. |
| B-03 | `ds/aurea_teclado_numerico.dart:59-66`; `ds/aurea_value_field.dart:64-67` | Número com PONTO decimal e zeros aparados ("0.82", "99.5%" → "100%"): a largura "dança" durante o arrasto e diverge da âncora pt-BR com casas fixas ("0,82", "100,0%"). O visor do teclado começa com ponto e a tecla é vírgula (B-44). |
| B-04 | `ds/aurea_keyframe_button.dart:67`; `ds/aurea_effect_card.dart:85-94, 157-191`; `ds/aurea_chip.dart:32`; `ds/aurea_section.dart:51`; `paineis/pontos.dart:674-676`; `paineis/efeitos/catalogo_de_efeitos.dart:426-427`; `paineis/texto.dart:40-41` | Alvos abaixo de 44: setas ‹ › 18 × 44; olho/⋯ 36 × 37 e alça 35 × 37 no cartão; chip 28 de altura; cabeçalho de seção 30; ‹ › do Pontos 32 × 32; estrela do catálogo 32 × 32; alinhamento 40 × 34. |
| B-19 | `paineis/pontos.dart:438-651` | No painel de 200 dp o trackpad sobra com ~76 dp (38 + 36 + 4 + 6 + 32 + 8 consumidos). |
| B-32 | `paineis/mascara.dart:17-21, 156-167`; `paineis/propriedades.dart:171-182` | 29 modos de mistura numa lista corrida (≈ 11 visíveis no menu de 450); as 7 categorias de `categoriasDeMescla` não aparecem. |
| B-42 | `ds/aurea_tabs.dart` | Abas não rolam até a ativa (Transformar tem 6, Forma e Estilo 5). |
| B-36 | `paineis/comum_3d.dart:84-86`; `ds/aurea_toggle.dart:7-8`; `paineis/texto.dart:25` | Comentários com medidas velhas ("rótulo de 75", "linha de 51"); valem 76 e 44. |

### 9.2 Comportamento / dados

| # | Onde | Problema |
|---|---|---|
| B-05 | `paineis/borda_sombra.dart:23-26, 94-113` | `_gravar` grava MARCA sempre que a trilha já anima (fora da regra do keyframe explícito) e o painel não tem losango: cria-se keyframe sem querer e não se cria de propósito. |
| B-06 | `paineis/estilo.dart:191-196, 319-331, 389-393, 438-446, 502-510, 656-672`; `paineis/fonte.dart:289-297`; `paineis/animar.dart:112, 212, 217`; `paineis/grupo.dart:91, 99, 108, 114`; `paineis/clonar.dart:156, 248-252, 281, 333, 432, 438`; `paineis/texto.dart:199, 217, 229`; `paineis/audio.dart:344, 362`; `paineis/batidas.dart:204, 209, 279`; `paineis/legendas.dart:99-100`; `paineis/forma.dart:199, 214, 325, 414, 522` | Ações deliberadas SEM `umPasso`: se vierem < 450 ms depois de outra edição, um desfazer leva as duas. |
| B-07 | `paineis/texto.dart:307-318`; `paineis/estilo.dart:291-299, 771-779`; `paineis/forma.dart:141-149`; `paineis/comum_3d.dart:208`; `paineis/particulas.dart:134`; `paineis/legendas.dart:121-127` | Seletor de cor chamado direto, sem `beginGesture/endGesture` (cada pausa vira um passo de desfazer); Legendas nem aplica ao vivo (sem `onChanged`). O certo é `escolherCor` (`pecas_centrais.dart:42-63`). |
| B-09 | `paineis/efeitos/pilha_de_efeitos.dart:142-169`; `paineis/mascara.dart:108-112`; `paineis/propriedades.dart:117`; `paineis/texto.dart:122-127` | Estado mutado DENTRO do `build` (cartão aberto, conhecidos, texto do campo). |
| B-10 | `paineis/audio.dart:592`; `paineis/borda_sombra.dart:335` | Cartões de efeito de áudio e de borda com chave por ÍNDICE: apagar/reordenar reaproveita o estado (aberto/fechado) do cartão errado. |
| B-11 | `paineis/audio.dart:690` | `somCopiado` é global mutável (vaza entre projetos). |
| B-12 | `features/editor/domain/velocidade.dart:34` | `velocidadeDaRegua` (ímãs 0,25/0,5/0,75/1/2/3 e passo 0,05) é código morto; a linha Velocidade não encaixa em 1× ao arrastar. |
| B-13 | Transformar, Mistura e máscara, Camada, Intensidade | Opacidade em 4 painéis e Mistura em 2 — a mesma propriedade em vários lugares. |
| B-14 | `ds/aurea_teclado_numerico.dart:28-36` | Teclado com arrastar-para-fechar LIGADO, raio 20 e véu `black38` (padrão das folhas: 13,5, sem arrastar, véu palco 35 %). "%" = % do MÁXIMO quando a unidade não é %: "50%" num parâmetro −1…2 vira 1,0. |
| B-15 | `ds/aurea_bottom_sheet.dart:92-128` | Folha "não modal" é modal: só o véu fica transparente; a barreira da `ModalBottomSheetRoute` continua pegando o toque (fecha a folha). Editor de curva, Animar sozinho e Gradiente vetorial prometem editor tocável atrás e não entregam. |
| B-17 | `core/ui/pedir_nome.dart:47`; `ds/aurea_teclado_numerico.dart:443`; `paineis/borda_sombra.dart:589` | `TextEditingController` descartado logo após o `await` do diálogo, com a animação de saída ainda usando o campo (o próprio `presets.dart:637-639` documenta o crash desse padrão). |
| B-18 | `paineis/efeitos/cartao_do_efeito.dart:297-298`; `paineis/presets.dart:235-237`; `paineis/clonar.dart:333`; `paineis/legendas.dart:100`; `paineis/animar.dart:217` | Apagar/excluir sem confirmação e sem aviso com "Desfazer" (efeito, preset, grade, fala, animador, máscara, borda). |
| B-20 | casca (`painelAbertoProvider`) | Sem pilha de painéis: Texto → Fonte, Tempo → Efeitos etc. não voltam ao anterior; voltar fecha tudo. |
| B-21 | `paineis/cor.dart:202, 206` | "Cor por cima" nasce com `AureaCores.destaque` — cor da UI (muda com o tema) virando conteúdo do projeto. |
| B-25 | `paineis/operadores_da_forma.dart:332-342` | HACK: modo do Combinar caminhos escolhido "girando" `cycleMergeMode` N vezes (não há setter). |
| B-27 | `paineis/legendas.dart:267-284, 297-314` | Para limpar a fonte, reconstrói o `CaptionHighlightStyle` campo a campo — campo novo some aqui. |
| B-30 | `ds/conta_gotas.dart:30`; `ds/aurea_seletor_de_cor.dart:224` | Conta-gotas fotografa a ÁRVORE de UI (`toImage(pixelRatio:1)`): resolução de tela, e com palco nativo (Vulkan) a foto pode sair vazia; alfa descartado. No port: ler o pixel do QUADRO do motor. |
| B-31 | `paineis/transformar.dart:262-284` | "Escala" mostra `scaleX` e escreve uniforme (com X ≠ Y o número mente); Largura/Altura sem mínimo (negativo espelha sem aviso). |
| B-33 | `paineis/degrade.dart:247-271` | Gradiente vetorial sem adicionar/remover parada. |
| B-34 | `paineis/estilo.dart:553-587` | Ligar "Caixa atrás" troca a seleção e reescreve o painel aberto (o painel pisca). |
| B-35 | `paineis/pecas_centrais.dart:24-27` vs `paineis/comum_de_objetos.dart:31-34, 73-76` | `aCadaPasso` duplicado; só `linhaNumerica` chama `Interacao.soltar()` — nos painéis centrais o palco pode ficar no "modo interação" depois do arrasto. |
| B-43 | `paineis/fonte.dart:33-34, 280` | Favoritas/recentes em listas estáticas quando não há prefs; estrela reflete por `setState`. |
| B-44 | `ds/aurea_teclado_numerico.dart:39, 162` | Visor inicia com "12.5" (ponto) e o teclado só tem vírgula. |

### 9.3 Desempenho (padrões que travam)

| # | Onde | Problema |
|---|---|---|
| B-08 | `paineis/comum.dart:108-121` usado em `transformar.dart:139`, `mascara.dart:125`, `audio.dart:83`, `borda_sombra.dart:70`, `propriedades.dart:132`, `estilo.dart:141`, `forma.dart:112`, `clonar.dart:66`, Intensidade e corpo do efeito | `NoCabecote` refaz o painel INTEIRO a cada quadro do relógio (inclusive o `TextField` do nome em Camada). Abrir painel pausa, mas dá para dar play com o painel aberto. No Compose: ler o tempo só onde ele entra (losango/valor) com `derivedStateOf`. |
| — | `core/ui/am_tick_ruler.dart:301-347` | (bom) uma entrega por quadro no arrasto; manter no port. |
| — | `paineis/efeitos/previa_do_efeito.dart:124-157` | (bom) um `Timer` de 8 fps para a grade inteira; manter. |
| B-41 | `paineis/efeitos/pilha_de_efeitos.dart:210-236` | Reordenar usa o proxy Material (sombra/elevação), fora do DS sem sombra. |

### 9.4 Texto sem acento, sem tradução, idiomas misturados

| # | Onde | Texto |
|---|---|---|
| B-22 | `paineis/degrade.dart:207` | "Cores e distribuicao" |
| B-22 | `features/editor/domain/shape.dart:1249` | "Angulo inicial" |
| B-22 | `features/editor/domain/audio_effect.dart` | "Distortion · Distorcao", "Reverb · Reverberacao", "Mixer estereo", opções "Nao"/"Sim" |
| B-22 | `features/export/presentation/export_video_screen.dart` | 1049 "Lendo os videos", 1111 "Deixe o app aberto ate terminar.", 1139 "Exportar video", 1175 "PNG com transparencia", 1334 "Media", "Na mao", 1415 "Nao deu para exportar", 1461 "Sequencia pronta", 1464 "nao entrou na galeria", 1695 "Maxima qualidade", 319 "O projeto esta vazio", 323 "composicao", 337 "Verificando se da para copiar...", 417 "espaco" |
| B-22 | `features/export/domain/export_settings.dart:58` | "Sequencia PNG" |
| B-22 | `paineis/grupo.dart:89`; `paineis/clonar.dart:112`; `paineis/fonte.dart:349-354`; `paineis/pontos.dart:48, 59, 83`; `paineis/forma.dart:177`; `export_video_screen.dart:248-250` | strings interpoladas fora do catálogo de tradução (e "Igualar à barra (2.50 s)" com ponto decimal) |
| B-26 | `features/editor/domain/text_animator.dart:670-686`; `domain/text_anim.dart` | "Posicao X", "Rotacao", "Espacamento", "Inclinacao", "Saturacao", "Maquina de escrever", "Distancia", "Forca", "Abrir espacamento" |
| B-23 | Time Remap, Range Selector, presets do animador | Speed/Time/Frame/Reverse/Freeze, Start/End/Offset/Ease High/Ease Low, Square/Ramp Up…, Fade Up/Slide Left/Scale In — inglês no meio do pt-BR (em parte proposital: vocabulário do AE) |

### 9.5 Fora do DS / consistência visual

| # | Onde | Problema |
|---|---|---|
| B-16 | `core/ui/pedir_nome.dart`; `paineis/presets.dart:661-682`; `paineis/borda_sombra.dart:564-588`; `ds/aurea_teclado_numerico.dart:395-442` | `CupertinoAlertDialog` do sistema (claro/escuro do SO, fora do DS). |
| B-24 | `paineis/fonte.dart:442-444` | Estrela de favorito `acao #245D8C`; no catálogo e no detalhe é `destaque #6FAED9`. |
| B-28 | `core/ui/am_tick_ruler.dart:396-423` | Cores da régua fixas (`#43516A`, `#7485A3`, branco) — não seguem o tema. |
| B-29 | `ds/aurea_seletor_de_cor.dart:283, 390, 441`; `export_video_screen.dart:1086, 1571`; `core/ui/snack.dart` | `IconButton`, `TextField`, `PopupMenuButton`, `LinearProgressIndicator`, `SnackBar` Material num DS "só Cupertino". |
| B-37 | `features/export/presentation/export_video_screen.dart:1705-1972`; `features/export/presentation/campo_de_valor.dart` | Exportação com peças próprias (`_BotaoGrande`, `_BotaoDeChip`, `_FichaDePredefinicao`, `_LinhaDeEscolha`, `_CabecaDeGrupo`, `_ChipDoRotulo`) e `AmColors`; `campo_de_valor.dart` é cópia viva do widget antigo só por `CampoDeValor.raio`. |
| B-38 | `paineis/efeitos/painel_de_efeitos.dart:60-67, 78-83` | `+` do cabeçalho e chip "+ Adicionar efeito" fazem a mesma coisa. |
| B-39 | `paineis/efeitos/catalogo_de_efeitos.dart:500-503`; `previa_do_efeito.dart:257-258` | Prévia recortada duas vezes (raio 8 do tile sobre raio 10 da prévia). |
| B-40 | `core/ui/am_colors.dart:112-113` | `AmColors.text` e `AmColors.muted` são `const`: seletor de cor, exportação e conta-gotas não seguem o tema. |

### 9.6 TODOs e dívidas declaradas no código

- `paineis/tempo.dart:405` — `kPreviaMisturaQuadros = false`: a prévia não
  mistura quadros; mistura/fluxo óptico só na exportação (o selo avisa).
- `paineis/mascara.dart:718-722` — rótulos da tabela de mesclagem não
  traduzem (sem `context` na tabela de dados).
- `paineis/grupo.dart:93-101` — "Remapear tempo" só para desligar
  (recurso removido; projeto antigo).
- `paineis/efeitos/cartao_do_efeito.dart:430-436` — efeito removido do
  catálogo continua na pilha, inerte, só apagável.
- `ds/aurea_slider.dart` — componente do DS sem uso nesta área.

---

## 10. Mapeamento → Compose e dados do motor

### 10.1 Arquivo antigo → componente Compose proposto

| Arquivo (HEAD) | Componente Compose | Observação |
|---|---|---|
| `ds/tokens.dart`, `core/theme/aurea_paleta.dart`, `core/ui/am_colors.dart` | `AureaTheme` + `LocalAureaColors` (papéis da 1.1), `AureaDims`, `AureaType`, `AureaMotion` | ler SEMPRE papéis; paleta trocável em runtime |
| `core/ui/tocavel.dart` | `Modifier.tocavel(onTap, onLongPress, encolhe = .965f, haptico)` | `graphicsLayer{scale, alpha}` animados 90/220 e 60/180 ms; sem ripple (`indication = null`) |
| `core/ui/am_tick_ruler.dart` | `TickRuler(value, min, max, unitsPerDp, active)` (Canvas) + `Modifier.dragValue(...)` | acumular desde o início; entregar 1× por quadro (`withFrameNanos`) |
| `ds/aurea_property_row.dart` | `PropertyRow.Number / .Point / .Color / .Custom` | arrasto na linha inteira; slot fixo para setas (B-02) |
| `ds/aurea_value_field.dart` | `ValueBox` | formato pt-BR (D-1) |
| `ds/aurea_teclado_numerico.dart` + `domain/expr.dart` | `NumericKeypadSheet` + `ValueExpression.parse()` + `ExpressionDialog` | portar o parser (contas, "1:30", "%") |
| `ds/aurea_keyframe_button.dart` | `KeyframeDiamond`, `KeyframeArrows`, `KeyframeButton`, `data class KeyframeUi` + `neighborMarks()` | tolerância 8000 µs |
| `ds/aurea_effect_card.dart` | `EffectCard(name, enabled, expanded, onToggleEnabled, onMenu, dragHandle)` | |
| `ds/aurea_section.dart` | `PanelSection` | |
| `ds/aurea_panel.dart` | `PanelScaffold(title, actions, tabs, onClose, body)` + `PanelNotice` | |
| `ds/aurea_tabs.dart` | `PanelTabs` (`LazyRow` + `animateScrollToItem` na ativa) | corrige B-42 |
| `ds/aurea_toggle.dart` | `AureaSwitch` (51 × 31, cores `acao`/`campoAlto`) | |
| `ds/aurea_chip.dart` | `AureaChip` (altura visual 28, alvo 44) | |
| `ds/aurea_dropdown.dart` + `ds/aurea_menu.dart` | `AureaDropdown<T>` + `AureaPopupMenu` (`Popup` + `PopupPositionProvider` alinhado à direita, 8 dp de margem) | |
| `ds/aurea_bottom_sheet.dart` | `AureaSheet` (modal) e `AureaDockedSheet` (não modal DE VERDADE, sem barreira) | corrige B-15 |
| `ds/aurea_seletor_de_cor.dart` | `ColorPickerSheet` (Quadro/Roda/RGB, alfa, hex, amostras, rápidas) | |
| `ds/conta_gotas.dart` | `EyedropperOverlay` | pixel do quadro do motor (B-30) |
| `ds/aurea_toolbar_button.dart`, `paineis/comum_3d.dart` (`GradeDeAcoes`, `LinhaDeFichas`) | `ToolbarButton(block)`, `ActionGrid`, `ChipsRow` | |
| `paineis/comum.dart`, `pecas_centrais.dart`, `comum_de_objetos.dart` | `oneUndoStep {}`, `gestureScope`, `rememberPlayheadUs()`, `ActivePropertyEffect`, `LinhaDeAcao→ActionRow`, `LinhaDePorta→DoorRow`, `FileiraDeAcoes→ChipFlow`, `FileiraDePilulas→PillRow` | |
| `ui/shell/contrato.dart`, `paineis/registro.dart`, `editor_shell.dart:457-567` | `enum PanelId`, `PanelRegistry`, `PanelHost` (substitui a barra contextual; `animateContentSize(100 ms)`) | considerar back-stack (D-4) |
| `paineis/efeitos/painel_de_efeitos.dart` | `EffectsPanel` | |
| `paineis/efeitos/pilha_de_efeitos.dart` | `EffectStack` (`LazyColumn` + reorder por alça) | acordeão por id |
| `paineis/efeitos/cartao_do_efeito.dart` | `EffectCardContent`, `EffectMenu` | |
| `paineis/efeitos/linhas_do_efeito.dart` | `ChoiceRow`, `SeedRow` | |
| `paineis/efeitos/time_remap.dart` | `TimeRemapRows` | |
| `paineis/efeitos/animador_de_texto.dart` | `TextAnimatorCard` | |
| `paineis/efeitos/catalogo_de_efeitos.dart` | `EffectBrowserSheet` (grade/lista, filtros, busca) | |
| `paineis/efeitos/detalhe_do_efeito.dart` | `EffectDetailSheet` | |
| `paineis/efeitos/previa_do_efeito.dart`, `miniatura_do_efeito.dart` | `EffectPreviewStrip` + `PreviewClock` (8 fps compartilhado) + cache LRU 24 | miniatura viva = render offscreen do motor |
| `paineis/transformar.dart` … `paineis/velocidade.dart` (um por painel) | `TransformPanel`, `ColorPanel`, `BlendMaskPanel`, `LayerPanel`, `TextPanel`, `FontPanel`, `TextStylePanel`, `AnimatePanel`, `ShapePanel` (+ `ShapeOperators`), `PointsPanel`, `AudioPanel` (+ `AudioEffectsSection`), `BeatsSheet`, `BeatPulseSheet`, `TimePanel`, `SpeedPanel`, `BorderShadowPanel`, `VectorGradientSheet`, `PresetsScreen`, `GroupPanel`, `ClonePanel`, `CaptionsPanel`, `FieldMenu`, `AutoAnimatorSheet` | 3D/partículas/rastrear: mesmas peças, fase futura |
| `features/export/presentation/export_video_screen.dart`, `outros_formatos.dart` | `ExportScreen`, `OtherFormatsSheet` | usar peças do DS (B-37) |

### 10.2 Controle → dado que o motor precisa expor

| Controle | Dado / operação do motor |
|---|---|
| Toda linha numérica | valor VISÍVEL no instante (projeto + edição pendente) · `ParamSpec{label, min, max, unit, decimals, dragStep}` · escrita `set(track, tUs, value)` que respeita a regra do keyframe explícito · `beginGesture()/endGesture()` (um desfazer por arrasto) · sinal de interação (qualidade da prévia) |
| Losango / setas | tempos das marcas da trilha GRAVADA (µs, relógio LOCAL da camada ou CRU do clipe conforme a trilha) · `toggleKeyframe(track, tUs)` · `seek(tUs)` · curva do trecho (`ease` por segmento, "aplicar em todos") |
| Parâmetro de efeito | `EffectInstance{id, type, enabled, params: Map<key, AnimatedDouble>, color, extraColors}` + `EffectSpec{id, name, category, cost, params, grupos, presets, synonyms, colorLabels, procedural}` · ops: `addEffect(layer, type, pronto)`, `removeEffect`, `duplicateEffect`, `reorderEffect(delta)`, `toggleEffectEnabled`, `editEffectParam(layer, fx, key, t, v)`, `toggleEffectParamKeyframe`, `toggleEffectKeyframe` (todos os parâmetros), `applyEffectPronto`, `copyEffects/pasteEffects`, `bakeEffectToKeyframes(fps)`, `saveEffectPresetFrom`, `setEffectColor`, `setEffectExtraColor` |
| Time Remap | trilha `tempo` (s da fonte, relógio cru) · derivadas `speedAt`, `frameAt` · `setSpeed/setFrame/setFreeze/reverse/interpolação/preservePitch`, "inverter", "reverso a partir de", "velocidade constante" |
| Transformar | por camada: `position (vec2)`, `positionZ`, `scale (vec2)`, `rotation`, `rotationX/Y`, `opacity 0..1`, `skew (vec2)`, `pivot (vec2)`, `is3D`, `ajuste da mídia (cobrir/conter)`, expressão e animador automático por propriedade, exposição no projeto |
| Mistura / máscara | `blendMode` nativo + `customBlend` · `matteMode` + fonte (camada acima/abaixo) · máscaras `{id, name, path(AnimatedPath Bezier), mode, inverted, feather(x,y, linked), expansion, opacity}` · `applyMaskReveal(preset, t)` |
| Acabamento (borda/sombra/estilo) | `LayerStyles{bordas ≤ 4 (cor, largura, opacidade, posição, enabled), dropShadow, innerShadow (cor, opacidade, ângulo, distância, desfoque, espalhar), outerGlow (cor, opacidade, tamanho), colorOverlay, gradientOverlay (A, B, ângulo, opacidade)}` com trilhas animáveis |
| Texto | `text`, `fontSize`, `color`, `alinhamento`, `fontFamily`, `bold`, animador de espaçamento (tracking), vínculo de dados; lista de famílias de fonte (importadas/embutidas) |
| Animadores de texto | `TextAnimator{name, enabled, selectors[RangeSelector{start,end,offset,easeHigh,easeLow,basedOn,shape}], props[positionX, positionY, scale, rotation, opacity, tracking, blur]}` + animações de posição (entrada/ênfase/saída: specId, duração, atraso por letra) |
| Forma | `ShapeParametric{kind, params (tracks), roundnessPercent}` · preenchimento (nenhum/cor/degradê/mídia + encaixe) · degradê vetorial `{stops, colors, colorFrames, angle, center, radiusScale, radial}` · traço `{color, width, opacity, dash, gap, dashOffset, cap, join, início, fim, tamanho}` · trim `{start, end, offset, individually}` · repeater · morph · operadores de caminho · merge · edição de nós Bezier (mover nó/alça, inserir, apagar, canto, fechar, contornos) |
| Áudio | `AudioSpec{volume envelope, muted, gain, fadeIn, fadeOut, duckAgainstId, duckAmount, processing{denoise, voice, deEsser, low/mid/high dB, effects[AudioEffect{type, enabled, params}]}, preservePitch}` · forma de onda (estado: lendo/sem áudio) · `normalize`, `removeSilence`, beats/BPM, `applyBeatPulse` |
| Tempo | `clipSpeed` + compensação · rampas prontas · `reverse`, `speedBlur`, `interpolação`, `freezeFrame(t, duração, onde)` |
| Grupo / clonar / legendas | precomp (`sourceDuration`, `collapse`, `clipToComp`) · `GridRig` (tracks + assets + controller + proximidade) · `Cue{id, start, end, text, locked}` + `CaptionHighlightStyle` |
| Catálogo | lista de specs por categoria, busca normalizada com sinônimos, recomendados, recentes e favoritos (prefs do aparelho), tiras de prévia + manifesto (pronto usado), miniatura renderizada offscreen |
| Exportação | `ExportSettings{format, size (lado menor), fps, quality, bitrateMbps, codec}` · duração do conteúdo · estimativa · callbacks de fase/progresso/detalhe · cancelar · publicação na galeria (URI, abrir, compartilhar) · outros formatos (SRT, Lottie + validação, SVG, template, pacote) |

---

## 11. Decisões pendentes e recomendações para o port

| # | Decisão | [A] âncora (t2 / tela96b) | [H] HEAD aprovado | Recomendação |
|---|---|---|---|---|
| D-1 | Formato do número | pt-BR, vírgula, casas fixas, 13 sp w600 `#6FAED9` sublinhado, caixa 68 × 24 | ponto, apara zeros, 14 sp w700 `#A9D3EC`, caixa 74 × 34 | Formato pt-BR com casas fixas (não "dança", é o do print). Tamanho da caixa: 74 × 34 (alvo melhor), cor/sublinhado a decidir com o dono |
| D-2 | Onde mora o keyframe dos efeitos | rail esquerdo 46 dp: ◆ do EFEITO inteiro + curva; chip da linha escolhida acende | losango por linha (30 dp à esquerda) + "Keyframe em todos" no ⋯ | [H] (granular, é o sistema único do app), mantendo "Keyframe em todos" |
| D-3 | Rótulo | chip 94 × 32, 2 linhas, centrado, selecionável | 76, 1 linha, encolhe | Coluna de 76–94 com ATÉ 2 linhas (sem encolher a fonte), resolve B-01 |
| D-4 | Navegação | "‹ Efeitos" (voltar à grade) + ‹ no rail | ✓ fecha; voltar do sistema fecha; sem pilha | ✓ como [H] + pilha rasa para saltos entre painéis (Texto→Fonte volta a Texto) |
| D-5 | Altura e cores do painel | 44 + 267 dp (≈ 313), corpo `#0F141A`, cartão `#151C24` raio 14 | 200 máx (fórmula 2.1), corpo `#151C24`, cartão `#1B2530` raio 5 | [H] (é a UI nova aprovada; a timeline maior foi pedido do dono) |
| D-6 | Linha | 48 de altura, arrasto só na fita | 44, arrasto na linha inteira | [H] (o beta reclamou de não conseguir pegar a fita estreita) |
| D-7 | Catálogo | título "Efeitos" 18 sp, padding 14, busca "glow, rgb split, pixelate...", chips 32 raio 9, 3 colunas aspecto 0,7, prévia raio 10, estrela só no Pro | título "Adicionar efeito" na folha do DS, padding 22, busca "Buscar efeito" + modo lista, chips 28 pílula, colunas 3–6 aspecto 0,74 raio 8, estrela sempre, selo `palco` 70 % | Estrutura [H] (filtros, lista, detalhe), geometria da grade [A] (prévias maiores, raio 10, placeholder com exemplos) |
| D-8 | Régua | fita com fade de 24 dp nas bordas, riscos muted 25 %/60 % | riscos `#43516A`/`#7485A3` sem fade | Matemática comum (9 dp, forte a cada 5), cores por papel do tema, fade nas bordas |
| D-9 | Snapping / fino | nenhum | nenhum | Opcional no port: ímãs da velocidade (`velocidadeDaRegua`) e háptica leve ao passar por 0/100 % — não existe hoje, só se o dono pedir |

Regras que NÃO podem se perder no port:

1. Direita aumenta; os riscos andam com o dedo (três lugares concordando:
   conta, riscos, leitura).
2. Um arrasto = um passo de desfazer; ação deliberada = um passo.
3. O instante é lido NO TOQUE, nunca capturado no build.
4. Números leem o projeto visível (com pendência); losangos leem o gravado.
5. Uma trilha, um losango; tolerância de 8 ms para "nesta marca".
6. Recolher cartão sem animar altura; acordeão com um aberto; recém-aplicado
   abre sozinho; o aberto é a propriedade ativa da timeline.
7. Folhas de ajuste não fecham arrastando.
8. Prévias do catálogo: um relógio só, 8 fps, só enquanto aberto.
