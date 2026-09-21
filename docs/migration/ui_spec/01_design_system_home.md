# 01 — Design system, tema e Home/Projetos (Aurea Flutter → Jetpack Compose)

Fonte: `C:\Users\SnyX\Documents\Projetos - Claude\Aurea` (somente leitura). Todos os caminhos abaixo são relativos a `Aurea/`.
Unidades: **dp** para medidas, **sp** para texto (no Flutter é "logical px"; com fontScale 1,0 dp = sp). Cores em ARGB/RGB hex.

---

## 0. LEIA PRIMEIRO — qual versão do código é a "UI aprovada"

**Os prints aprovados (`t1.png`, `t3.png`, `tela96.png`) NÃO correspondem ao código do HEAD.** Correspondem ao commit
`762dbfe` (19/09/2026 21:26, "O motor 3D desenha no palco e na exportacao…"), o último antes de `aba36bb` ("release: Beta A.01").

Evidências (conferidas pixel a pixel, ver §5.2):

| Elemento no print | 762dbfe | aba36bb…0583d77 | HEAD (959d725) |
|---|---|---|---|
| Barra de abas | 5 abas: Inicio, Comunidade, Ajustes, Perfil, Sobre (sem "+") | Inicio, Comunidade, **"+" 46×46**, Perfil, Ajustes (Sobre some da barra) | Inicio, Comunidade, **pílula "+" 52×34**, Perfil, Ajustes; barra chapada (sem blur) |
| Cabeçalho | logo 38, "Aurea" 30 sp w800 | logo 32, "Aurea" 25 sp | topo 57 dp: logo 30 + "AUREA" 19 sp w800 tracking 3 + menu ☰ |
| Grade de projetos | 2 colunas, childAspectRatio 1,12 | 1 coluna compacta, altura 104 | 1 coluna, cartão de 80 dp |
| Hero "Continuar editando", atalhos redondos, "Modelos" | sim | sim | **não** (foi para o menu ☰) |

Conclusão aplicada neste documento: **a especificação principal (§5) é a do `762dbfe`** (lida com `git show 762dbfe:<arquivo>`, sem tocar o repositório). O HEAD está resumido em §5.20 para quem quiser comparar. Tema, cores, logo, Ajustes/Sobre/Ajuda e o design system `core/ds` são iguais ou só acrescidos no HEAD (diff conferido); `core/ds/*` **não existia** em 762dbfe (nasceu em 58121bf, 21/09, para o editor novo) — é documentado da versão HEAD.

Aparelho dos prints: 1080×2400 px, densidade **2,625** (420 dpi) → largura **411,43 dp**. Status bar = 128 px = **48,76 dp** (o sistema desenha um véu `#0B0F13` sobre o fundo). Barra de navegação gestual = 63 px = **24 dp, preta opaca `#000000`** — o app **não** é edge-to-edge embaixo (MediaQuery.padding.bottom = 0 nos prints).

---

## 1. DESIGN TOKENS

### 1.1 Cores — a tabela da marca (`lib/src/core/theme/aurea_colors.dart`, classe `AureaColors`)

Única fonte de hex; `AppColors`, `AmColors`, `AureaTokens`, `AureaCores` só leem papéis daqui/da paleta.

| Nome | Hex | Papel / onde aparece |
|---|---|---|
| `brandDeep` | `#123A63` | fundo de grupo; `selected` (fundo de camada selecionada) no tema Aurea |
| `brand` | `#245D8C` | PREENCHIMENTO de ação (Exportar, "+" do editor, botão "Vamos editar", `AmColors.action`, `AureaCores.acao`) |
| `brandLight` = `accent` | `#6FAED9` | **AÇÃO/ESTADO no app fora do editor**: botão "Novo projeto", ícones dos atalhos, aba ativa, "Continuar editando", pílula "Continuar", chips ativos, trilho cheio, `AppColors.lime` |
| `brandSoft` = `keyframe` = `selectionText` | `#A9D3EC` | keyframe, curva, seleção em texto; **fundo do avatar "?"** do cabeçalho (`AppColors.violet`) |
| `bg` | `#0F141A` | fundo de tela, cromo do editor, splash, fundo do ícone adaptativo |
| `surface` | `#151C24` | folhas (Novo projeto, Boas-vindas), grupos de Ajustes, painel do editor |
| `surfaceHigh` | `#1B2530` | círculos dos atalhos, botão redondo de template, campos de texto, placeholder de miniatura (degradê) |
| `chip` | `#212D3A` | chip, tile, caixa de valor |
| `border` | `#273442` | divisores/contornos |
| `text` | `#F7F9FB` | texto principal |
| `muted` | `#AAB6C3` | texto secundário, ícones inativos |
| `onAccent` | `#0B1117` | texto/ícone SOBRE `accent` (texto do botão "Novo projeto", "!" do aviso) |
| `accentDim` | `#1D3A55` | fundo de chip aceso, tecla de operador do teclado |
| `keyframeDim` | `#22405A` | faixa animada |
| `danger` | `#FF6B6B` | excluir, erro |
| `warning` | `#FFC978` | aviso (nível "atenção") |
| `stage` | `#0A0E13` | fundo atrás da composição (editor); fundo da barra de abas no HEAD (`AureaCores.palco`) |
| `playhead` | `#FFFFFF` | cabeçote |
| `brandGradient` | `[#123A63, #245D8C, #6FAED9]` | degradê diagonal da marca |
| `scale` | `[#123A63, #245D8C, #6FAED9, #A9D3EC]` | rampa de azuis |
| **Tema claro** | | |
| `lightBg` `#F4F6F9` · `lightSurface` `#FFFFFF` · `lightSurfaceHigh` `#E9EFF6` · `lightChip` `#DCE6F0` · `lightBorder` `#CDD9E6` · `lightText` `#0F141A` · `lightMuted` `#55697D` · `lightAccent` = `#245D8C` · `lightOnAccent` `#FFFFFF` · `lightAccentDim` `#DCEAF7` · `lightKeyframe` `#2E7FB0` · `lightKeyframeDim` `#CFE6F5` · `lightSelection` = `#123A63` · `lightDanger` `#D94B4B` | | |

Contrastes declarados no código (WCAG 2.1): accent/bg 7,8:1; soft/bg 11,9:1; text/brand 6,8:1; muted/surface 7,4:1.

#### 1.1.1 Papéis derivados usados no app (tema padrão "Aurea")

`AppColors` (`core/theme/app_theme.dart`, telas fora do editor — Home, Ajustes, Sobre):

| Getter | Valor (Aurea) | Nota |
|---|---|---|
| `background` | `#0F141A` | |
| `surface` | `#151C24` | |
| `surfaceHigh` | `#1B2530` | = `paleta.panel` |
| `lime` | `#6FAED9` | nome legado (era verde-lima até 18/09) = AÇÃO |
| `violet` | `#A9D3EC` | nome legado = seleção em texto |
| `onDark` | `#F7F9FB` | |
| `muted` | `#AAB6C3` | |
| `outline` | `#273442` | |
| `accentDim` | `#1D3A55` | |
| `hairline` | `#273442` @ α 0,72 (= `0xB8273442`) | tema claro: `#000000` @ α 0,10 |

`AmColors` (`core/ui/am_colors.dart`, cromo do editor; sob o tema Light continua a paleta Aurea escura):
`bg`=stage `#0A0E13`; `topBar`=`panel`=`#0F141A`; `panelHigh`=`#151C24`; `chip`=`campo`=`#212D3A`; `pilula`=`#1B2530`; `accent`=`teal`=`#6FAED9`; `accentDim` `#1D3A55`; `action`=`#245D8C`; `onAction`=`#F7F9FB`; `actionDim` `#16304A`; `selection`=`#123A63`; `selectionText`=`tealBright`=`keyframe`=`#A9D3EC`; `pink`=`#FF6B6B`; `cabecote` `#FFFFFF`; `text` `#F7F9FB` (const); `muted` `#AAB6C3` (const); `hairline` `#273442`; `campoAlto` = `lerp(chip, textPrimary, 0.08)` = **`#323D49`**; `textoSecundario` `#AAB6C3`; `textoPrincipal` `#F7F9FB`.

`AureaCores` (`core/ds/tokens.dart`, DS novo — hierarquia de 6 superfícies SÓ por tom, sem borda):

| Papel | Nível | Aurea |
|---|---|---|
| `palco` | 0 | `#0A0E13` |
| `cromo` | 1 | `#0F141A` |
| `painel` | 2 | `#151C24` |
| `elevado` | 3 | `#1B2530` |
| `campo` | 4 | `#212D3A` |
| `campoAlto` | 5 | `#323D49` |
| `texto` / `textoSecundario` | | `#F7F9FB` / `#AAB6C3` |
| `destaque` / `destaqueApagado` | único destaque (ligado) | `#6FAED9` / `#1D3A55` |
| `acao` / `sobreAcao` | preenchimento de ação | `#245D8C` / `#F7F9FB` |
| `keyframe` | | `#A9D3EC` |
| `perigo` | | `#FF6B6B` |
| `selecao` | | `#123A63` |
| `cabecote` | | `#FFFFFF` |

`AureaTokens` (InheritedWidget, `core/theme/tokens.dart`): `bg surface surfaceHigh chip text muted hairline accent onAccent accentDim keyframe keyframeDim selection danger` — `dark` = mesmos hex acima, `selection` = `#245D8C`.

Material `ColorScheme` montado em `AppTheme.tema` (useMaterial3 = true): `primary`=#6FAED9, `onPrimary`=#0B1117, `secondary`=#A9D3EC, `onSecondary`=#FFFFFF, `error`=#FF6B6B, `onError`=#FFFFFF, `surface`=#0F141A, `onSurface`=#F7F9FB, `surfaceContainerHighest`=`surfaceContainerHigh`=#1B2530, `surfaceContainer`=`surfaceContainerLow`=#151C24, `onSurfaceVariant`=#AAB6C3, `outline`=`outlineVariant`=#273442. `scaffoldBackgroundColor`=#0F141A.

Opacidades usadas na Home (valores exatos do código; em Compose use `color.copy(alpha = x)`):
- Faixa de aviso/atualização: cor do nível @ **0,14** (sobre #0F141A dá ≈ `#1C2A35`; medido no print `#1D2934`).
- Barra de abas: `#0F141A` @ **0,72** + blur σ 24.
- Barra compacta ao rolar: `#0F141A` @ **0,62** + blur σ 18.
- Barra de ações em lote: `#151C24` @ **0,88** + blur σ 20.
- Moldura placeholder da grade: `#0F141A` @ **0,55**.
- Círculo da linha "Comunidade": `#6FAED9` @ **0,14**.
- Scrim do hero: `transparent` → `#B3000000` (preto 70 %), stops [0,45 ; 1,0], vertical.
- Pega (drag handle) da folha Novo projeto: `#FFFFFF` @ **0,18**.
- Moldura da folha Novo projeto: degradê `#6FAED9`@0,22 → @0,06 (topLeft→bottomRight), borda `#6FAED9`@0,60 largura 1,2.
- Formato escolhido (mini-moldura): fundo `#6FAED9`@0,20.
- Pílula da versão (Sobre) e ícones das Boas-vindas: `#6FAED9`@0,12.
- `Colors.white70` = `#B3FFFFFF` (ficha e reticências do hero).
- Realce global de toque (`highlightColor`): branco @ 0,05 (tema claro: preto @ 0,05).

#### 1.1.2 Cores escritas à mão fora dos tokens (reproduzir, mas saber que são exceções)

| Valor | Onde | Arquivo |
|---|---|---|
| `#10130C` | ícone e texto da pílula "Continuar" do hero (legado do tema lima) | `projects_tab.dart@762dbfe:1038,1045` |
| `#FF6B6B` | "Excluir" da barra de lote | `projects_tab.dart@762dbfe:1917,1921` |
| `#FFFFFF`, `#B3FFFFFF` | nome/ficha/reticências do hero | `projects_tab.dart@762dbfe:1015,1023,1065` |
| `#FFC978`, `#FF7A7A` | faixas de aviso "atenção"/"problema" e erro da atualização | `aviso_ao_vivo.dart:119-120`, `faixa_de_atualizacao.dart:119` |
| `#FFB020` (+ `0x22FFB020` fundo, `0x55FFB020` borda) | BetaBanner (Sobre) | `about/presentation/report_sheet.dart:382-402` |
| `#43516A` / `#7485A3` | riscos fraco/forte da régua `AmTickRuler` | `core/ui/am_tick_ruler.dart:396,399` |
| `#3A4150` / `#2A303B` | xadrez de transparência (seletor de cor) | `core/ds/aurea_seletor_de_cor.dart` (`_XadrezPainter`) |
| `#1B2130`→`#16281B` | degradê do `WhatsNewCard` (verde legado; widget sem uso) | `whats_new.dart:517-521` |
| Cores de tipo de camada | ver §2.17 | `core/ds/aurea_tipo_da_camada.dart` |
| Cores da logo | ver §2.22 | `core/widgets/aurea_logo.dart` |

#### 1.1.3 Os seis temas (Ajustes > Aparência — **só existe no HEAD**, em 762dbfe era segmentado Escuro/Claro/Sistema)

`lib/src/core/theme/aurea_paleta.dart`. Id gravado em `settings.tema`: `aurea` grava `'escuro'`, `light` grava `'claro'`, `'sistema'` = Aurea/Light pelo brilho do aparelho; demais gravam o nome do enum.

| Papel | Aurea | Aurea Dark | Midnight | OLED | Light | Graphite |
|---|---|---|---|---|---|---|
| background | #0F141A | #0A0E13 | #0B1020 | #000000 | #F4F6F9 | #131416 |
| surface | #151C24 | #0F141A | #111831 | #0A0B0D | #FFFFFF | #1A1B1E |
| panel | #1B2530 | #151C24 | #18213F | #121316 | #E9EFF6 | #222327 |
| primary | #245D8C | #245D8C | #3D4FB8 | #245D8C | #245D8C | #3A6A94 |
| accent | #6FAED9 | #6FAED9 | #8EA2FF | #6FAED9 | #245D8C | #8DB9DA |
| textPrimary | #F7F9FB | #F7F9FB | #F2F4FF | #F5F7FA | #0F141A | #F5F5F6 |
| textSecondary | #AAB6C3 | #A3AFBC | #A5AECF | #9AA3AE | #55697D | #A9ACB3 |
| divider | #273442 | #212C38 | #2A3660 | #30343A | #CDD9E6 | #33363C |
| timeline | #0F141A | #0A0E13 | #0B1020 | #000000 | #F4F6F9 | #131416 |
| clip | #151C24 | #0F141A | #111831 | #0A0B0D | #151C24 | #1A1B1E |
| selected | #123A63 | #123A63 | #232F6B | #123A63 | #CFE0F2 | #2B3A4A |
| playhead | #FFFFFF | #FFFFFF | #FFFFFF | #FFFFFF | #0F141A | #FFFFFF |
| keyframe | #A9D3EC | #A9D3EC | #C3CCFF | #A9D3EC | #24709E | #C5DCEC |
| danger | #FF6B6B | #FF6B6B | #FF6B7F | #FF6B6B | #C43D3D | #FF6B6B |
| success | #4CD08A | #4CD08A | #4CD08A | #4CD08A | #1A7A42 | #4CD08A |
| stage | #0A0E13 | #06090C | #070A16 | #000000 | #0A0E13 | #0C0D0E |
| chip | #212D3A | #1B2530 | #202B4D | #1A1C20 | #DCE6F0 | #2A2C31 |
| onPrimary | #F7F9FB | #F7F9FB | #F2F4FF | #F7F9FB | #FFFFFF | #F7F9FB |
| primaryDim | #16304A | #132A41 | #1C2659 | #11273D | #DCEAF7 | #1F3345 |
| onAccent | #0B1117 | #0B1117 | #0A0E1F | #0B1117 | #FFFFFF | #0E1114 |
| accentDim | #1D3A55 | #19344D | #252F66 | #172F46 | #DCEAF7 | #263A4A |
| keyframeDim | #22405A | #1D384F | #2A3570 | #1B3449 | #CFE6F5 | #2C4253 |
| selectedText | #A9D3EC | #A9D3EC | #C3CCFF | #A9D3EC | #123A63 | #C5DCEC |
| warning | #FFC978 | #FFC978 | #FFC978 | #FFC978 | #9A5B00 | #FFC978 |

Regra: no tema Light o **editor** continua com a paleta Aurea (`AureaPaleta.editor => claro ? aurea : this`). Troca de tema remonta a árvore inteira (chave do `MaterialApp`), perdendo a pilha de navegação.

### 1.2 Tipografia

**Família:** o app **não empacota fonte de UI**. No Android o Flutter usa `Roboto` (Typography.material2021). Em Compose: `FontFamily.Default` (Roboto do sistema). A única fonte do `pubspec.yaml` é `Aurea Motion Sans` — fonte de **conteúdo** (camadas de texto/templates), não de interface (§4).

**Estilo herdado por padrão (MUITO importante para as alturas baterem):** todo `Text` dentro do `Scaffold` herda `DefaultTextStyle = textTheme.bodyMedium` = **15 sp, w400, letterSpacing −0,1, height 1,35 (linha = 1,35 × tamanho), cor #F7F9FB, leadingDistribution `even`** (vem do M3 `englishLike2021`). Quando um widget só define `fontSize`/`fontWeight`/`color`, **continua herdando `height 1,35` e `letterSpacing −0,1`**. Ex.: "Boa tarde" 13,5 sp ocupa 18,225 dp de altura de linha. Em Compose: `LocalTextStyle` base = `TextStyle(fontSize=15.sp, letterSpacing=(-0.1).sp, lineHeight=1.35.em, color=Text, lineHeightStyle=LineHeightStyle(Alignment.Center, Trim.None))` com `PlatformTextStyle(includeFontPadding=false)`, e cada estilo local faz `merge` por cima (lineHeight em `em` escala com o fontSize, igual ao `height` do Flutter). `letterSpacing` do Flutter é absoluto em px lógicos → use `.sp`.

`AppTheme.textTheme` (`core/theme/app_theme.dart:311-353`):

| Estilo | Tamanho | Peso | letterSpacing | height | Cor | Uso |
|---|---|---|---|---|---|---|
| headlineLarge | 34 | w700 | −0,8 | 1,1 | text | títulos "Ajustes"/"Sobre"; base de "Aurea" 28 (Sobre) e "Bem-vindo à Aurea" 24 |
| titleLarge | 22 | w700 | −0,5 | 1,27 (M3) | text | "Novo projeto" (folha), "Do zero ao primeiro motion" |
| titleMedium | 17 | w600 | −0,3 | 1,50 (M3) | text | passos do guia |
| bodyLarge | 17 | w400 | −0,2 | 1,50 (M3) | text | títulos das linhas de Ajustes |
| bodyMedium | 15 | w400 | −0,1 | 1,35 | text | **DefaultTextStyle** |
| bodySmall | 13 | w400 | 0 | 1,33 (M3) | muted | subtítulos de Ajustes, "Editor de video e composicao" |
| labelLarge | 17 | w600 | −0,2 | 1,43 (M3) | — | |
| AppBar title | 17 | w600 | −0,3 | — | text | centerTitle = true, elevação 0, fundo #0F141A |
| FilledButton | 17 | w600 | −0,2 | — | onAccent #0B1117 sobre #6FAED9 | raio 14, padding 20h/14v (Home sobrescreve raio 16) |

("(M3)" = o `ThemeData` funde o textTheme acima sobre o M3 `englishLike2021`; o que o app não definiu — height, `leadingDistribution: even` — vem de lá. Ao sobrescrever só `fontSize` o multiplicador de altura é mantido.)

`AureaTypography` (`core/theme/aurea_colors.dart:226-258`, disponível, pouco usado): display 34/w700/−0,8/h1,1 · title 22/w700/−0,5 · headline 17/w600/−0,3 · body 15/−0,2 · label 12/w600/+0,2 · caption 11/+0,1 · numeric 13/w600/0/tabular.

`AureaEstilos` (DS do editor, `core/ds/tokens.dart:421-460`): `titulo` 14 w600 texto · `propriedade` 12,5 textoSecundario · `valor` 14 w700 cor keyframe #A9D3EC **tabularFigures** · `rotulo` 10 textoSecundario · `secao` 11 w600 ls +0,3 textoSecundario · `corpo` 13 texto. `AureaDims` de texto: textoDeBarra 14, textoDeBarraPequeno 11, textoDeRotulo 10, textoDeInfo 12, textoDeAba 9, textoDePropriedade 12, textoDeTitulo 14.

Estilos da Home (762dbfe) — tabela completa em §5 junto de cada peça.

Números: onde o código pede `FontFeature.tabularFigures()` use `fontFeatureSettings = "tnum"`.

### 1.3 Espaçamento

- `AureaSpacing` (grade de 4): x1 4 · x2 8 · x3 12 · x4 16 · x5 24 · x6 32 · minTap 44 · topBar 44 · transport 46 · ruler 52 · tile 56. Aliases `AureaTokens.s1..s5` = 4/8/12/16/24.
- `AureaDims` espaços: e2 2 · e4 4 · e6 6 · e8 8 · e10 10 · e15 15 · e20 20.
- Home 762dbfe: margem lateral de conteúdo **20** (cabeçalho à direita 12); espaço entre blocos 18; títulos de seção 28 em cima / 12 embaixo; fim da lista 120 (para passar da barra de abas translúcida).
- DS painel: margemDoPainel 22 · vaoDoPainel 6 · topoDoPainel 15 · respiroDaLista 6,5.

### 1.4 Raios

- `AureaRadius`: chip 10 · card 12 · sheet 18 · pill 999.
- `AureaDims`: raioXs 1,5 · raioSm 3 · raioMd 4 · raioLg 5 · raioXl 8 · raioDaFolha 13,5 · raioPilula 100 · raioDoClipe 1,5.
- Home 762dbfe: botão Novo projeto **16**; hero **20**; cartão da grade (miniatura) **14**; moldura placeholder **4**; cartão de modelo **16**; faixas de aviso **12**; botões redondos/atalhos/avatar = círculo; pílula "Continuar" 999; folhas modais (Novo projeto, Boas-vindas) topo **24**; campo de nome 12; campos de medida 10; moldura grande 12; mini-moldura 4; drag handle 3; relatório 20; teclado numérico 20; grupos de Ajustes 16; logo `size × 0,22`.

### 1.5 Elevação / sombras / vidro

- `AureaShadows.sheet` = `[BoxShadow(#66000000, blur 28, offset (0,−6))]`; `popover` = `[BoxShadow(#73000000, blur 20, offset (0,8))]`; `none`. Nada na Home usa sombra.
- Separação na Home é por TOM + **blur de fundo**: barra de abas (σ 24, α 0,72), barra compacta (σ 18, α 0,62), barra de lote (σ 20, α 0,88). Em Compose: `Modifier.hazeChild`/`RenderEffect.createBlurEffect` (API 31+) com fallback para cor sólida mais opaca em API < 31.
- `elevation: 0` em AppBar. Diálogos/folhas Cupertino usam o blur do SDK (§1.10).

### 1.6 Traços

Hairline 0,5 (topo da barra de abas; `DividerThemeData` thickness 0,5 space 0,5 cor hairline); divisor de grupo de Ajustes = Divider com recuo à esquerda 16; `AureaDims.divisorFino` 0,5 · `divisorDePainel` 0,7 · `tracoDeSelecao` 2 · `tracoDeMultisselecao` 1,5; moldura da folha 1,2; mini-moldura 1,2 (normal) / 2 (escolhida); borda da bolinha de cor 2.

### 1.7 Ícones — tamanhos

`AureaDims`: iconeSm 16 · iconeMd 20 · iconeLg 24 · iconeXl 32. Home 762dbfe: abas 24; atalhos 23; botão redondo 17; "+" do botão 20; film placeholder 34 (hero) / 18 (grade) / 22 (vazio); reticências 18; barra da lista 19; marca de seleção 18; play do "Continuar" 13; `_Linha` 19 + chevron 15; `_LinhaGrande` 22 + chevron 15; xmark das faixas 15; ícone das faixas 14 dentro de círculo 22.

### 1.8 Toque mínimo

`AureaSpacing.minTap` = 44; `AureaDims.toqueMinimo` = 40 ("no app de referência"), `toqueConfortavel` = 44. Reais na Home 762dbfe (vários abaixo de 48 dp do Android — ver §8): botão redondo 44×44; avatar 44×44; reticências 40×40; botões da barra da lista **35×35**; X do aviso **40×28**; X da atualização 44×44; atalho = 56 + rótulo (largura = 1/3 da linha).

### 1.9 Movimento

- `AureaMotion` (`core/ds/tokens.dart:356-373`): `rapido` 100 ms (painel, submenu, barra, menu, folha pequena) · `normal` 200 ms (folha grande, seletor de cor) · `lento` 300 ms. Curvas: `entrada` = `Curves.decelerate` (1−(1−t)²) · `saida` = `Cubic(1/3, 0, 2/3, 1/3)` (= t²). Sem molas.
- **`Tocavel`** (feedback de toque do app inteiro, §2.21): escala 1 → **0,965** (desce em 90 ms, sobe em 220 ms, `easeOutCubic`) + opacidade 1 → **0,82** (60 ms / 180 ms, `easeOut`); sem ripple; `haptico` → `HapticFeedback.lightImpact` no toque.
- Troca de aba: `HapticFeedback.selectionClick()`; troca instantânea (IndexedStack, cada aba preserva scroll via PageStorageKey).
- Barra compacta ao rolar: `AnimatedSwitcher` 180 ms, `easeOut`/`easeIn` (fade).
- Moldura da folha Novo projeto: aspect interpolado em 240 ms `easeOutCubic`; mini-molduras `AnimatedContainer` 160 ms.
- Navegação: `CupertinoPageTransitionsBuilder` no Android e iOS — **500 ms**; a tela nova entra da direita (x 100 % → 0) com `Curves.fastEaseInToSlowEaseOut` (volta com a curva invertida); a de baixo recua para x −1/3 com `linearToEaseOut` (volta `easeInToLinear`) e ganha sombra; voltar por gesto de borda esquerda. Em Compose: `slideInHorizontally { it }` + `slideOutHorizontally { -it / 3 }`, 500 ms, easing equivalente.
- Modais: `showModalBottomSheet` M3 padrão (~250 ms entrada/200 ms saída); `showCupertinoModalPopup` (action sheet desliza de baixo, ~335 ms); `showCupertinoDialog` (fade+scale 1,3→1,0 ~250 ms).

### 1.10 Tema global / comportamento Material-Cupertino

- `splashFactory: NoSplash`, `splashColor`/`hoverColor` transparentes, `highlightColor` branco 5 % → **sem ripple** em lugar nenhum (InkWell/ListTile só "acendem" 5 %). Em Compose: `LocalIndication` = indicação própria (escala+opacidade do Tocavel) e `ripple` desligado.
- `cupertinoOverrideTheme`: brilho escuro, `primaryColor` = #6FAED9 → **texto das ações de CupertinoActionSheet/AlertDialog sai #6FAED9**, destrutivas `systemRed` escuro **#FF453A**.
- CupertinoAlertDialog (SDK): largura 270, raio 14, fundo `#CC2D2D2D` com blur, título 17 w600 ls −0,5 h1,3, conteúdo 13 h1,35 ls −0,2, ação 16,8 (ação padrão `isDefaultAction` em w600), botão ≥ 45, divisor 0,3.
- CupertinoActionSheet (SDK): margem 8 das bordas, raio 14, fundo `#BE292929` + blur, título/mensagem 13 cor `#96F1F1F1` (padding 16h/13,5v), ação 17 (altura mín. 57,17), divisores `#D57D7D7D` 0,3, botão Cancelar separado (8 acima) fundo `#FF2C2C2C`, pressionado `#C1515151`.
- CupertinoSlidingSegmentedControl (SDK): altura mín. 28, padding interno 2v/3h, raio externo 9, raio do polegar 7, separadores `#4D8E8E93` 1 px, animação mola 412 ms.
- CupertinoSwitch: trilho 51×31, polegar raio 14, área 59×39; ativo = #6FAED9, polegar ligado = onAccent (#0B1117) em Ajustes.
- `inputDecorationTheme`: preenchido #1B2530, raio 12, sem borda; focado borda #6FAED9.
- `snackBarTheme`: fundo #1B2530, texto #F7F9FB, flutuante. `AureaSnack` sempre fecha por timer próprio (4 s padrão; `showReasonToast` 1,5 s), um por vez, com X quando não há ação.
- Texto do app nunca espelha (RTL desligado: `Directionality.ltr` na raiz, mesmo em árabe).

---

## 2. COMPONENTES REUTILIZÁVEIS

> `core/ds/*` = design system do **editor novo** (HEAD). Na Home 762dbfe não aparecem; na Home HEAD aparecem `AureaMenu`, `AureaBottomSheet`, `AureaChip`, `AureaValueField`. Cores abaixo = tema Aurea.

### 2.1 `AureaBottomSheet` + `mostrarAureaFolha<T>()` — `core/ds/aurea_bottom_sheet.dart`
- Folha modal que sobe por cima do editor. Fundo `painel` #151C24, cantos superiores **13,5**, sem sombra/borda.
- Cabeçalho opcional (se `titulo != null`): altura **44** (cabecalhoDoPainel 38 + e6). Linha: 22 de margem · título (`AureaEstilos.titulo` 14 w600, 1 linha, ellipsis) · `acoes` · botão fechar 44×38 com `xmark` 20 textoSecundario · 6. Sem título: espaçador de 10.
- Corpo: `altura` fixa opcional; senão o que o conteúdo pedir (`Flexible`). SafeArea embaixo.
- `mostrarAureaFolha`: `isScrollControlled`, **`enableDrag: false`** (arrastar não fecha, porque dentro tudo se ajusta arrastando), `useSafeArea`, véu `palco`@0,35 (modal) ou @0 (`modal:false`, editor atrás continua tocável), entrada `rapido` 100 ms (ou `normal` 200 ms se `grande`), saída 100 ms, curvas entrada/saída. Chave do X: `folha-fechar`.

### 2.2 `AureaChip` — `core/ds/aurea_chip.dart`
- Pílula de escolha curta/filtro. Altura **28**, padding horizontal **12**, raio 100.
- Normal: fundo `campo` #212D3A, texto 12 (`corpo`) #F7F9FB. Ativo: fundo `destaqueApagado` #1D3A55, texto/ícone `destaque` #6FAED9.
- Ícone opcional 16 + gap 4. `traduzir=false` para conteúdo. Toque via `Tocavel`. Alvo de toque 28 de altura (abaixo do mínimo — §8).

### 2.3 `AureaDropdown<T>` — `core/ds/aurea_dropdown.dart`
- Caixa com a opção atual; toque abre `AureaMenu` ancorado com a atual marcada; só chama `aoMudar` se mudou.
- Altura **34**, padding 10h, fundo `campo`, raio **4**; texto 13 (`corpo`) texto/(desabilitado textoSecundario); `chevron_down` 12 textoSecundario à direita. `largura` nula = ocupa tudo. Para listas longas (modo de mescla, fonte).

### 2.4 `AureaEffectCard` — `core/ds/aurea_effect_card.dart`
- Um efeito na pilha: `[› ] Nome ......... [olho] [≡] [⋯]` cabeçalho **37**; corpo = `AureaPropertyRow`s.
- Container: fundo `elevado` #1B2530, raio **5**, margem inferior 6.
- Seta: coluna 32, `chevron_right` 13 textoSecundario, gira 0,25 volta (90°) quando aberto em 100 ms decelerate. Toque na seta/nome alterna (Tocavel sem encolher). Nome 13 w600: ligado #F7F9FB; desligado textoSecundario@0,6.
- Olho 36×37 (`eye`/`eye_slash` 17; desligado @0,5). Alça ≡ 35×37 `line_horizontal_3` 16 — com `indiceNaLista` vira gatilho de arrasto de ReorderableList (sem toque longo). ⋯ 36×37 `ellipsis` 17 (callback recebe o contexto do botão para ancorar o menu). 4 à direita.
- Corpo: padding (10, 0, 4, 4); abrir/fechar **instantâneo** (sem animar altura). Controlado (`aberto`+`aoMudarAberto`) ou autocontrolado (`inicialmenteAberto`).
- Chaves: `<chave>-cabecalho/-seta/-olho/-alca/-menu/-corpo`.

### 2.5 Keyframe: `AureaKeyframeButton`, `AureaLosango`, `AureaSetasDoKeyframe`, `KeyframeState` — `core/ds/aurea_keyframe_button.dart`
- `KeyframeState{animated, here, onToggle, onCurve?}`. Três estados visuais: ◇ apagado (não anima) = `rhombus` cor textoSecundario@0,6; ◇ aceso (anima, sem marca aqui) = `rhombus` #A9D3EC; ◆ cheio (marca neste quadro) = `rhombus_fill` #A9D3EC. Ícone 15.
- Toque: põe/tira a marca no cabeçote; **háptico leve só ao criar** (`!here`). Toque longo: `onCurve` (abre editor de curva).
- `AureaLosango`: o losango sozinho à esquerda do nome, caixa **30×44**.
- `AureaSetasDoKeyframe`: `‹ ›` só quando `animated`; cada seta 18×44, `chevron_left/right` 13; ação nula → textoSecundario@0,3, senão #A9D3EC.
- `AureaKeyframeButton`: largura FIXA **64** (18 + 28 + 18) × 44 — não pula quando nasce a 1ª marca; sem animação as setas viram espaçadores de 18.
- `marcasVizinhas(marcasUs, agoraUs)` → anterior/próxima ignorando marcas a menos de **8000 µs** (`toleranciaDaMarcaUs`); `temMarcaEm`.

### 2.6 `AureaLayerRow` — `core/ds/aurea_layer_row.dart`
- Camada numa lista (vincular, agrupar, fonte de máscara): `[faixa][miniatura/ícone 32] nome ....... [olho 44]`, altura **37**.
- Recuo `recuo × 12`. Faixa de cor: largura **4** (= faixaDeCor 10 / 2,5), altura 25 (37−12), raio 1,5, cor do tipo (§2.17) ou `campoAlto`. Gap 8. Miniatura/ícone 32×29 (ícone 20). Gap 8. Nome 13 (`Text`, conteúdo do usuário): visível #F7F9FB, oculto textoSecundario@0,6.
- Selecionada: fundo `selecao` #123A63 (sem contorno). Olho opcional 44×37 (`eye`/`eye_slash` 18). Tocavel sem encolher; toque longo `aoSegurar`.

### 2.7 `AureaMenu<T>` + `AureaMenuItem<T>` + `mostrarAureaMenu()` — `core/ds/aurea_menu.dart`
- Lista flutuante: largura **250** fixa, altura máx. **450** (e ≤ tela − 16), fundo `elevado` #1B2530, raio **8**, padding vertical 4, sem sombra/borda. Título opcional (padding 15/6/15/4, estilo `secao` 11 w600 +0,3).
- Item: altura **40**; barra de marca 4 × 24 à esquerda (`destaque`, visível só em `marcado`); 10; ícone opcional 20 + 10; rótulo 13 1 linha; 10. Cor: desabilitado textoSecundario@0,5; `destrutivo` #FF6B6B; `marcado` #6FAED9; normal #F7F9FB. Pressionado: fundo `campoAlto` #323D49 (enquanto o dedo está em cima).
- Posição: abaixo do botão-âncora (topo = bottom+4) se couber (altura estimada = itens×40 + 8, ou +38 com título), senão acima (topo = top − estimada − 4, ≥ 8); alinhado pela direita da âncora (`left = right − 250`, preso em [8, largura−258]).
- Véu `palco`@0,25, toque fora fecha. Transição 100 ms: fade + escala 0,96→1 com origem `topRight`, curvas entrada/saída. Chaves `menu-<chave|índice>`.

### 2.8 `AureaPanel` + `AureaAvisoDoPainel` — `core/ds/aurea_panel.dart`
- Casca única de painel: cabeçalho **38** `[22][título 14 w600][ações][✓ 44×38 checkmark_alt 20 destaque][6]`; sub-abas opcionais (§2.13); corpo rolável com padding (22, 4, 22, 15) ou `corpo` próprio. Fundo `painel` #151C24, sem borda.
- `AureaAvisoDoPainel`: texto `propriedade` (12,5 textoSecundario) com padding vertical 10 — nunca painel vazio.

### 2.9 `AureaPropertyRow` — `core/ds/aurea_property_row.dart`
Linha de propriedade no desenho do app antigo, altura **44**:
`[◆ 30 ou 6] [nome 76] 6 [ régua de riscos (Expanded) ] 6 [ valor 74×34 ] [‹ › 36 se animada]`
- Nome: caixa 76×44, alinhado à esquerda, 1 linha que **encolhe para caber** (FittedBox), `propriedade` 12,5 textoSecundario; toque longo = `aoResetar`.
- Régua: `AmTickRuler` altura 36 (44−8), `arrastavel:false`, opacidade 0,4 se desabilitada.
- **A linha inteira é a superfície de arrasto** (`AmArrastoDeValor`): arrastar para a DIREITA AUMENTA; sensibilidade = `AureaSlider.sensibilidadePara(min,max,larguraDoDeslizante)`, onde `larguraDoDeslizante = largura − (30|6) − 76 − 12 − 74 − (36 se setas)`; com faixa finita: `(max−min)/max(largura−25, 1)` unid/px; sem faixa: 0,5. `aoComecarGesto`/`aoTerminarGesto` = um passo de desfazer. Toque na caixa/losango continua sendo toque.
- Variantes: `.ponto` (X/Y: duas `AureaValueField` arrastáveis com prefixo "X"/"Y", vão 6, sensibilidade padrão 0,5); `.cor` (amostra 34×22 raio 4 + `#RRGGBB` em estilo `valor` + `chevron_right` 13; toque abre seletor); `.personalizada` (rótulo + qualquer controle alinhado à esquerda).
- Chaves: `prop-<slug>`, `kf-<slug>`, `valor-<slug>`, `deslizante-<slug>`, `cor-<slug>`; slug = rótulo minúsculo sem acento, não-alfanumérico → `-`.

### 2.10 `AureaSection` — `core/ds/aurea_section.dart`
- Título pequeno e apagado + conteúdo. Cabeçalho altura **30**: texto traduzido e depois CAIXA-ALTA, estilo `secao` 11 w600 +0,3 textoSecundario; `chevron_up/down` 12 se recolhível. Toque no título inteiro alterna (sem animação). Chave `secao-<chave|titulo>`.

### 2.11 `SeletorDeCor` / `showColorPicker()` / `ColorWell` — `core/ds/aurea_seletor_de_cor.dart`
- Folha: fundo `painel`, raio 13,5, `enableDrag:false`, véu palco@0,35, entrada 200 ms/saída 100 ms. Altura máx. 80 % da tela, rolável, padding (18,12,18,16+teclado).
- Topo: "Cor" 17 w700 · 12 · original|nova (76×28, raio 8, xadrez atrás; tocar a original volta a ela) · espaço · conta-gotas (IconButton `eyedropper` 20, só se houver palco) · "Pronto" 15 w600 #6FAED9.
- Abas segmentadas Quadro/Roda/RGB (polegar `accentDim`, fundo `chip`, texto 13); aba lembrada em prefs `cor.aba`.
- Quadro: SV 170 de altura, raio 10, mira círculo r9 (branco@0,9 traço 2,5 + preto@0,35 traço 1); abaixo matiz em faixa 26. Roda: lado min(largura, 240), anel 26, quadro SV interno `(raio−26−8)×√2`, raio 8. RGB: 3 faixas 26 com rótulo 16 de largura + valor tocável 52×30 (13 w700 #6FAED9, fundo chip, raio 8) que abre o teclado numérico.
- Faixas (`_Strip`): raio 8, alça 14 × (altura+4), raio 7, borda branca 2,5. Alfa (se `withAlpha`): faixa 26 + valor "NN%".
- Linha do código: "#" 14 muted + campo 96 de largura (fundo chip, raio 8, padding 10/9, aceita 6 ou 8 hex) + "H 000° S 00% V 00%" 12 muted tabular + copiar (menu com HEX e RGBA) + colar.
- "Minhas cores" (máx. 24, prefs `cor.amostras`): botão 40×40 (+ `plus` 18 accent para salvar; vira lixeira `trash` 18 rosa sobre rosa@0,35 enquanto arrasta) + lista horizontal de bolinhas 40 (vão 8); toque longo arrasta para a lixeira. "Rápidas": Wrap 10/10 de bolinhas 30: #FFFFFF, #000000, #6FAED9, #A9D3EC, #35C4E7, #2BE3A0, #FFB020, #FF6B6B, #FF4FA3, #AAB6C3, #1B2530, #F7F9FB (+ recentes antes). Bolinha marcada: borda #6FAED9 2, senão hairline.
- Devolve a cor viva em `onChanged` enquanto arrasta; "Pronto" fecha com a cor.

### 2.12 `AureaSlider` + `PintorDoAureaSlider` — `core/ds/aurea_slider.dart`
- Altura de toque **44**. Com faixa: trilho 2,5 (raio 1,25) `campoAlto` de 12,5 a largura−12,5; preenchimento `destaque` crescendo **a partir do zero** quando a faixa cruza 0 (senão da ponta esquerda); alça círculo raio 7,5 (25×0,3) cor `texto`. Sem faixa: riscos a cada 9 px (forte a cada 5: altura ±7, traço 1,4, textoSecundario@0,7; fraco ±4, traço 1, @0,35) que andam COM o dedo, indicador central ±10 traço 2 `destaque`. Desabilitado: opacidade 0,4. `arrastavel:false` quando a linha inteira arrasta.

### 2.13 `AureaTabs` — `core/ds/aurea_tabs.dart`
- Sub-abas de texto, altura **38**, rolagem horizontal, padding lateral 12. Item: padding 10h; 4 · rótulo 12 (ativo w600 `destaque`; inativo w500 textoSecundario) · 6 · traço 16×2 raio 1 `destaque` (só na ativa). Aba ativa não é tocável. Chaves `<chave>-<i>`.

### 2.14 Teclado numérico: `showNumberInput()`, `TecladoNumerico`, `formatarValorDigitado()`, `showExpressionEditor()` — `core/ds/aurea_teclado_numerico.dart`
- ÚNICO teclado para valor exato (caixa de valor, seletor de cor, painéis). Folha modal fundo `painel`, véu preto 38 %, raio 20, padding (16,14,16,12+teclado).
- Título 15 w700 (padrão "Valor exato" ou "Valor exato (%)").
- Visor: `CupertinoTextField` sem teclado do sistema (`keyboardType none`), alinhado à direita, 26 w700 tabular, padding 14/12, fundo `palco` #0A0E13, raio 12, sufixo da unidade 16 textoSecundario; texto inicial todo selecionado.
- Dica (altura 22, 12 textoSecundario, à direita): "Conta incompleta" | "Fica em X" (vai ser preso na faixa) | "= X" (resultado de conta).
- Grade 5×4, teclas altura **48**, raio 10, vão 6 (3+3) e 6 entre linhas: `7 8 9 ⌫ / 4 5 6 ÷ / 1 2 3 × / , 0 ± − / : % = +`. Operadores (÷×−+=) fundo `destaqueApagado` e texto `destaque`; demais fundo `campo` texto 21 w600. ⌫ = `delete_left` 22; toque longo em ⌫ limpa. Teclas com háptico.
- Rodapé: "Cancelar" (fundo campo, 15) | "OK" (fundo `destaque`, texto `sobreAcao` 15 w700; desabilitado se a conta não fecha). Vão 10.
- Semântica de `lerValorDigitado`: aceita vírgula ou ponto ("1.234,56" pt-BR), + − × ÷ ( ), "50%" = 50 % de `percentOf` (100 se unidade '%', senão `max` finito, senão 100), tempo "1:30" = 90 e "1:02:03.5" = 3723,5. Resultado preso em [min, max]. `formatarValorDigitado` apara zeros só depois da vírgula ("-0"→"0").
- `showExpressionEditor`: CupertinoAlertDialog "Expressão · nome", campo 1–3 linhas com placeholder "ex.: wiggle(2, 30) ou time * 90", erro 12 #FF6B6B, ações Limpar/Cancelar/OK.

### 2.15 `AureaToggle` — `core/ds/aurea_toggle.dart`
- `CupertinoSwitch`: trilho ativo `acao` #245D8C, inativo `campoAlto` #323D49; desabilitado = sem callback (opacidade 0,5 do SDK).

### 2.16 `AureaToolbarButton` — `core/ds/aurea_toolbar_button.dart`
- Padrão (barra contextual): caixa **64 × 57**, ícone 24, 4, rótulo 10 até 2 linhas centralizado. Bloco (`bloco:true`): altura 57, padding 4h, fundo `campo` (ativo `destaqueApagado`), raio 5, ícone 32, rótulo 1 linha.
- Cores: habilitado ícone #F7F9FB; ativo ícone e rótulo #6FAED9; desabilitado ícone textoSecundario@0,45; rótulo normal textoSecundario.

### 2.17 Tipo de camada — `core/ds/aurea_tipo_da_camada.dart`
Cor (escura, é fundo de barra) e ícone por `LayerKind`:
video `#6A52E0` `videocam_fill` · image `#3D6FD9` `photo_fill` · audio `#1F8C93` `music_note` · text `#B07A16` `textformat` · caption `#8A6A1E` `captions_bubble_fill` · shape `#2E9459` `circle_fill` · particles `#B0417A` `sparkles` · element3d `#C06A24` `cube_fill` · scene3d `#A85520` `cube_box_fill` · camera `#2A7B9B` `camera_fill` · group `#4C5566` `folder_fill` · adjustment `#5A4A7A` `slider_horizontal_3` · nullLayer `#444C5C` `smallcircle_circle`. Listras de selecionada: `lerp(cor, branco, 0,28)`.

### 2.18 `AureaValueField` — `core/ds/aurea_value_field.dart`
- Caixa **74 × 34** (padrão; `double.infinity` na linha de ponto), padding 4h, fundo `AmColors.chip` #212D3A, raio **8**; conteúdo encolhe para caber: prefixo opcional (estilo `rotulo` 10) + 4 + número `valor` (14 w700 #A9D3EC tabular; desabilitado textoSecundario). Texto = `formatarValorDigitado(v, casas) + unidade`, "—" se não finito.
- Toque → `showNumberInput` (título = rótulo da linha ou "Valor exato"); toque longo → `aoSegurar` (expressão). `arrastavel` → arrastar a caixa muda o valor (direita aumenta, sensibilidade 0,5 padrão).

### 2.19 Conta-gotas — `core/ds/conta_gotas.dart`
- `previewStageKey` (RepaintBoundary do palco). `pegarCorDoPalco()`: fotografa o palco (pixelRatio 1), abre rota transparente com véu preto 54 %, mostra a foto no MESMO retângulo; dedo arrasta: anel 16 (borda branca 2) no dedo + lupa círculo 50 (cor sob o dedo, borda branca 3, sombra preta 45 % blur 8) 86 acima; soltar escolhe. Abaixo do palco (16): texto "Arraste sobre o palco e solte na cor que quer." 14 + X `xmark_circle_fill` 30. Devolve nulo fora do editor.

### 2.20 `AmTickRuler` + `AmArrastoDeValor` + utilitários — `core/ui/am_tick_ruler.dart`
- Regra do dono: **arrastar para a DIREITA AUMENTA** (conta, riscos e leitura concordam; há teste de sentido).
- `passoDosRiscos` 9 px; forte a cada 5 (`riscosPorForte`, evita efeito roda-de-carroça); índice absoluto (o forte é sempre o mesmo risco do "papel").
- Régua: altura padrão 64 (na linha de propriedade 36); `pad = altura × 0,18`; riscos fracos `#43516A` traço 1,6 de `pad` até `altura − pad`; fortes `#7485A3` traço 2 de `pad×0,55`; indicador central traço 3 (`accentCenter` → #6FAED9, senão branco) de `pad×0,4` até `altura − pad×0,4`. Com faixa finita desenha trilho de posição na base: altura 3, raio 1,5, fundo muted@0,18, cheio #6FAED9 (cresce do zero se a faixa cruza 0); riscos param 2 acima do trilho.
- `AmArrastoDeValor`: valor = valor no início + deslocamento acumulado × unidades/px, preso em [min,max]; **no máximo uma entrega por quadro** (o resto fica pendente e sai após o frame; o valor final sempre é entregue); `onStart`/`onEnd` (também no cancelamento).

### 2.21 `Tocavel` — `core/ui/tocavel.dart` (o "átomo de fluidez")
- Envolve qualquer alvo: escala 0,965 + opacidade 0,82 enquanto pressionado (tempos em §1.9); não muda o layout (escala só a pintura, a partir do centro). Sem ação = não participa da arena (não rouba o toque do pai). `encolhe` configurável (1 = só escurece). `haptico` = lightImpact no toque. Toque longo solta o estado pressionado antes de chamar.
- Compose: `Modifier.aureaPressable(onClick, onLongClick, scaleDown = 0.965f, haptic = false)` com `graphicsLayer { scaleX/scaleY/alpha }` animados por `animateFloatAsState` com as durações assimétricas.

### 2.22 `AureaLogo` — `core/widgets/aurea_logo.dart`
- Vetor em base 108×108, `size` padrão 48; com `withBackground` (padrão true) pinta quadrado `#0F141A` e recorta com raio `size × 0,22`.
- Traço (tubo) espessura 9, cap/join round: `M26,77 C26,45 39,29 62,29 C77,29 86,37 86,49 C86,61 76,68 59,68 L44,68`.
- 4 passadas: (1) borda `#00134F` traço 9,9; (2) tubo com degradê linear topRight→bottomLeft `[#3E8BF0, #245D8C, #001A63]` stops [0; 0,45; 1] no retângulo (21,24,74,60); (3) faixa clara deslocada (−0,75,−0,75) `#8C4E9BF5` traço 4,14; (4) brilho deslocado mais (−0,85,−0,85) `#99DFF1FF` traço 1,53.
- Esfera centro (87,76) r 8: radial centro (−0,45,−0,5) raio 1,05 `[#7FC0FF, #1F63C8, #001460]` stops [0; 0,45; 1]; pingo de luz (84,6; 73,2) r 1,6 `#B3EAF6FF`.
- Usos: Home 38 (cabeçalho) e 22 (barra compacta); Sobre 96; Boas-vindas 64; HEAD topo 30. Glifo visível ≈ 74/108 × size (26 dp em size 38 — medido 25,1×21 dp).

### 2.23 Outros utilitários de UI
- `pedirNome(context, titulo, atual)` — `core/ui/pedir_nome.dart`: CupertinoAlertDialog com campo autofocus; ações "Cancelar" / "OK" (padrão); devolve nulo se vazio.
- `AureaSnack.show / hide`, `showReasonToast` — `core/ui/snack.dart` (§1.10).
- Não-DS usados na Home 762dbfe (privados em `projects_tab.dart`): `_BotaoRedondo`, `_Atalho`, `_CartaoContinuar`, `_CartaoProjeto`, `_CartaoModelo`, `_TituloSecao`, `_Linha`, `_LinhaGrande`, `_BarraDaLista`, `_BotaoDaBarra`, `_AcoesEmLote`, `_BarraAoRolar`, `_AvatarDaConta`, `_SemProjetos`, `_DialogoDeNome` — especificados em §5.

---

## 3. ÍCONES

**Fonte:** quase tudo é **`CupertinoIcons`** (fonte `CupertinoIcons.ttf` do pacote `cupertino_icons` 1.0.9, licença **MIT** © 2016 Vladimir Kharlampidi; arquivo em `%LOCALAPPDATA%\Pub\Cache\hosted\pub.dev\cupertino_icons-1.0.9\assets\CupertinoIcons.ttf`). Poucos Material Icons (`uses-material-design: true`): `Icons.language` e `Icons.chevron_right` em Ajustes, `Icons.add` (29) no botão "+" da barra das versões aba36bb..0583d77. **Nenhum ícone SVG/PNG próprio na UI** (fora logo vetorial e imagens de template).

Recomendação para Compose: empacotar `CupertinoIcons.ttf` em `res/font/` e desenhar os glifos por codepoint (idêntico ao Flutter), ou converter os glifos usados em VectorDrawables. `Icon(size = N)` do Flutter = glifo com `fontSize = N` numa caixa N×N.

| Nome (CupertinoIcons) | Codepoint | Onde |
|---|---|---|
| house / house_fill | U+F447 / U+F6CA | aba Inicio (inativa/ativa) |
| person_2 / person_2_fill | U+F740 / U+F741 | aba Comunidade; linha "Comunidade" (fill 22) |
| slider_horizontal_3 | U+F7DC | aba Ajustes (mesmo ícone ativo); tipo "ajuste" |
| person / person_fill | U+F47D / U+F47E | aba Perfil |
| info_circle / info_circle_fill | U+F44C / U+F6CF | aba Sobre; menu HEAD "Sobre" |
| plus | U+F489 | botão Novo projeto (20); "+" HEAD; salvar cor |
| doc_on_doc | U+F634 | botão redondo "Abrir template"; atalho Template; copiar código |
| person_crop_circle | U+F419 | botão Perfil da barra compacta; "Criador" (Sobre/relato) |
| photo_on_rectangle | U+F76A | atalho Mídia; menu HEAD "Importar mídia" |
| cube | U+F61A | atalho Cena 3D |
| film | U+F66B | placeholder de miniatura; estado vazio |
| play_fill | U+F488 | pílula "Continuar" |
| ellipsis | U+F46A | menu do projeto (hero/grade), ⋯ do efeito |
| search | U+F4A5 | buscar projetos |
| arrow_up_arrow_down | U+F51F | ordenar |
| checkmark_circle / checkmark_circle_fill | U+F3FE / U+F3FF | entrar na seleção, marcar todos / marca marcada |
| circle | U+F401 | marca desmarcada |
| xmark | U+F404 | fechar faixas, sair da seleção, fechar folha |
| xmark_circle_fill | U+F36E | limpar busca (16); cancelar conta-gotas |
| plus_square_on_square | U+F781 | duplicar em lote |
| delete (= trash) | U+F4C4 | excluir em lote (762dbfe usa `delete`; HEAD `trash`) |
| chevron_right / chevron_left | U+F3D3 / U+F3D2 | setas de linha, keyframe |
| chevron_up / chevron_down | U+F5E5 / U+F5D5 | "Mostrar menos"; seção/dropdown |
| square_grid_2x2 | U+F804 | "Mostrar todos os N projetos" |
| play_rectangle | U+F771 | tutorial "sua primeira cena 3D"; menu HEAD "Aprender" |
| cube_box | U+F61B | tutorial cena completa; Sketchfab (Sobre) |
| textformat | U+F85C | tutorial texto; tipo texto |
| sparkles | U+F7E8 | "O que ha de novo"; tipo partículas |
| exclamationmark_bubble | U+F656 | "Versao beta: achou um problema?"; Reportar |
| arrow_down_circle_fill | U+F4EC | faixa de atualização |
| pencil_outline / pencil | U+F73D / U+F37E | formato "Livre" / renomear (HEAD) |
| tv, device_phone_portrait, square, photo, photo_on_rectangle | U+F881, U+F8CF, U+F7F8, U+F767, U+F76A | ícones dos presets de proporção (não desenhados na folha 762dbfe) |
| line_horizontal_3 | U+F6E1 | alça de reordenar; menu ☰ HEAD |
| eye / eye_slash | U+F424 / U+F662 | visibilidade |
| rhombus / rhombus_fill | U+F7C2 / U+F7C3 | keyframe |
| checkmark_alt | U+F8C1 | ✓ do painel, amostra de tema marcada |
| delete_left | U+F621 | ⌫ do teclado |
| eyedropper, doc_on_clipboard | U+F664, U+F632 | seletor de cor |
| square_stack_3d_up, wand_stars, arrow_up_doc | U+F819, U+F892, U+F528 | Boas-vindas |
| wrench, arrow_counterclockwise, book, bolt, doc_text | U+F8A0, U+F21C, U+F3E7, U+F593, U+F638 | Sobre |
| exclamationmark_triangle | U+F660 | BetaBanner |
| camera, music_note_2 | U+F3F5, U+F46C | contatos no relato (Instagram/TikTok) |
| lock_fill, checkmark_seal | U+F4C9, U+F5CB | release notice |
| clock, play, square_arrow_up, tray_arrow_down, gear | U+F4BE, U+F487, U+F4CA, U+F874, U+F43C | Home HEAD |
| videocam_fill, photo_fill, music_note, captions_bubble_fill, circle_fill, cube_fill, cube_box_fill, camera_fill, folder_fill, smallcircle_circle | U+F4CD, U+F768, U+F46B, U+F5BF, U+F400, U+F61D, U+F61C, U+F3F6, U+F435, U+F7DF | tipos de camada |

**Assets de imagem que a Home/tema usam (o app novo precisa carregar):**
- Modelos: `assets/templates/vhf/thumbnail.jpg` (360×640), `assets/templates/dnyx/thumbnail.jpg` (384×384), `assets/templates/reference-rebuild.jpg` (360×639), `assets/templates/notes.jpg` (600×600; usada também pelo "Pindown").
- Tutoriais (tela de tutorial, fora da Home): `assets/tutoriais/{cena3d,cena-completa,texto-bounce}.{jpg,mp4,json}`.
- Ícone/splash: `assets/icon/app_icon.png` (1024), `app_icon_foreground.png` (1024 RGBA), `app_icon_monochrome.png`; Android: `ic_launcher_foreground.png`/`ic_launcher_monochrome.png` 108/162/216/324/432 px, `splash_logo.png` 160/240/320/480/640 px (mdpi…xxxhdpi), adaptive icon com inset 16 %, fundo `#0F141A`. **Já copiados no repo novo** em `Aureabeta/_identity/branding/` (android-mipmap, icon, ios-appicon, splash, colors.xml).
- Splash (print `tela96.png`): fundo `#0F141A`, logo 3D (bitmap) centralizada; glifo medido ≈ **97,5 × 84,6 dp** centrado em (50 %, 50 %) da tela. Em Compose: `core-splashscreen` com `windowSplashScreenBackground = #0F141A` e o foreground do ícone; `windowBackground` normal = `@color/aurea_background`.

---

## 4. FONTES

- UI: **nenhuma** empacotada — Roboto do sistema (Android). Manter `FontFamily.Default`.
- Conteúdo: `Aurea Motion Sans` → `assets/templates/dnyx/AureaMotionSans.ttf` (171 KB), licença **Apache 2.0** (`assets/templates/dnyx/FONT-LICENSE.txt`). Usada como família padrão de texto quando a escolhida falha (`editor_controller.dart:2877`); não pertence à UI.
- Ícones: `CupertinoIcons.ttf` (MIT, §3).

---

## 5. HOME + PROJETOS (versão dos prints = `762dbfe`)

Arquivos: `lib/src/features/projects/presentation/home_shell.dart`, `projects_tab.dart`, `new_project_sheet.dart`, `aviso_ao_vivo.dart`, `faixa_de_atualizacao.dart`, `boas_vindas.dart`, `release_notice.dart`, `whats_new.dart`; domínio `projects/domain/project_presets.dart`, `projects/application/projects_view.dart`. Raiz: `lib/src/app.dart` → `CadastroObrigatorioGate(child: HomeShell())` (gate da comunidade, fora deste escopo).

### 5.1 Árvore (HomeShell)

```
Scaffold(extendBody: true, bg #0F141A)
├─ body: Column
│   ├─ AvisoAoVivo            (0..N faixas; SafeArea(top) própria)         §5.3
│   ├─ FaixaDeAtualizacao     (0..1 faixa; SafeArea(top) própria)          §5.3
│   └─ Expanded → IndexedStack(index = homeTabProvider)
│        0 ProjectsTab  1 CommunityTab  2 SettingsTab  3 SocialProfilePage(embedded)  4 AboutTab
└─ bottomNavigationBar: barra translúcida 54 dp (+ inset inferior)           §5.13
```

ProjectsTab:
```
SafeArea(bottom:false) → Stack
├─ _BarraAoRolar (Stack: CustomScrollView + barra compacta no topo quando scroll > 64)
│   CustomScrollView slivers:
│    1 _Cabecalho                                   §5.4
│    2 Botão "Novo projeto" (54, largura total)     §5.5
│    3 Atalhos: Mídia · Template · Cena 3D          §5.6
│    4 _CartaoContinuar (hero)   [se houver herói]  §5.7
│    5 _BarraDaLista             [se total ≥ 2]     §5.8
│    6 _SemProjetos | SliverGrid de _CartaoProjeto  §5.9 / §5.15
│    7 _Linha "Mostrar todos…"   [condicional]      §5.10
│    8 _TituloSecao "Modelos" + carrossel 196       §5.11
│    9 _TituloSecao "Comunidade" + _LinhaGrande
│   10 _TituloSecao "Aprender" + 5 _Linha
│   11 SizedBox(120)
└─ _AcoesEmLote (Positioned bottom) [se selecionando] §5.14
```

### 5.2 Posições verticais conferidas no print `t3.png` (3 projetos, sem faixa)

y em dp a partir do fim da status bar (48,76 dp). "Calc." = pelo código com a herança height 1,35; "Medido" = pixels/2,625.

| Peça | Calc. topo–base | Medido |
|---|---|---|
| Cabeçalho (padding 14/12; linha = 31,5 + 2 + 18,23 = 51,73) | 0 – 77,7 | avatar 22,9–56,0 (centro 39,5 ✓) |
| Botão Novo projeto (padding top 6, altura 54) | 83,7 – 137,7 | 83,0 – 137,1 |
| Atalhos (padding top 18; círculo 56 + 7 + rótulo 16,2) | círculo 155,7 – 211,7 | 155,0 – 211,0 |
| Hero (padding top 18; 371,43 × 9/16 = 208,93) | 252,9 – 461,8 | 252,2 – 461,0 |
| Barra da lista (padding 18/6; linha 35) | 479,8 – 520,8 | texto "3 projetos" topo 490,3 |
| Grade (cartão 179,71 × 160,46) | 520,8 – 681,3 | 520,0 – (miniatura até 634) |
| "Modelos" (28 + 28,35 + 12) | 681,3 – 749,6 | carrossel começa 748,6 |
| Carrossel 196 | 749,6 – 945,6 | |
| "Comunidade" + linha 52 | 945,6 – 1066,0 | |
| "Aprender" + 5 × 45,6 | 1066,0 – 1362,4 | |
| Espaço final 120 → conteúdo total ≈ 1482 dp | | |
| Barra de abas: hairline + 54 | tela 2194–2337 px | 54,5 dp ✓ |

Larguras (411,43 dp): conteúdo = 371,43 (20 de cada lado). Cartões da grade medidos em x 19,8–200,0 e 211,4–391,6 ✓.

### 5.3 Faixas de topo (fora das abas, acima de tudo)

**AvisoAoVivo** (`aviso_ao_vivo.dart`) — recados do servidor (`AvisosService`, busca a cada 10 min enquanto a faixa existe). Print `t1.png`.
- Lista vazia → nada. Senão `SafeArea(bottom:false)` + Column de `_Faixa` (uma por aviso).
- `_Faixa`: margem (12, 8, 12, 0); padding (12, 10, 6, 10); fundo cor@0,14; raio 12. Cor por nível: `info` #6FAED9 · `atencao` #FFC978 · `problema` #FF7A7A.
- Linha (crossAxis início): círculo 22 cor cheia com "!" 14 w900 #0B1117 · 10 · texto (padding top 2) 13 w600 height 1,35 #F7F9FB — com link acrescenta "  Saiba mais ›" e o texto inteiro abre o link (navegador externo) · X: caixa **40×28** `xmark` 15 #AAB6C3 → `dispensar(id)` (só esse aviso).
- Aviso marcado como POPUP abre uma vez `CupertinoAlertDialog`: título "Aviso", conteúdo = texto (padding top 8), ações "Abrir" (padrão, só com link) e "Agora não".
- No print t1: 1 aviso info de 4 linhas (93 dp de altura).

**FaixaDeAtualizacao** (`faixa_de_atualizacao.dart`) — versão nova (Android baixa e instala). Mesmo container (margem 12/8/12/0, padding 12/10/6/10, raio 12, cor@0,14); cor = #6FAED9, ou #FF7A7A com erro. Linha centralizada: círculo 22 com `arrow_down_circle_fill` 14 #0B1117 · 10 · texto 13 w600 h1,35 · 8 · (livre) pílula padding 12h/7v raio 999 cor cheia, texto 12 w800 #0B1117 "Atualizar" | "Tentar de novo" — ou (ocupado) `CupertinoActivityIndicator` raio 8 com padding 10h · X 44×44 `xmark` 15 muted (some se obrigatória ou ocupado → "adiar").
- Textos por fase: parada: "A versao {nome} ja esta disponivel." | "A versao {nome}: {notas}"; baixando: "Baixando a versao {nome} · {p}%" | "Baixando a versao {nome}…"; conferindo: "Conferindo o arquivo…"; instalando: "Abra o instalador e toque em Instalar."; pronto: "Instalando…"; erro: a mensagem de erro.

**Defeito visível no print t1:** cada faixa e a `ProjectsTab` aplicam `SafeArea(top)` — com faixa visível o recuo da status bar entra **duas vezes** (vão de 48,76 dp entre a faixa e o cabeçalho; medido: cabeçalho desce 150,9 dp = 48,76 + 8 + 93 + ~0). Ver §8.

### 5.4 Cabeçalho (`_Cabecalho`, 762dbfe:1149-1208)

- Padding (20, 14, 12, 12). Row (centro vertical):
  - `AureaLogo(size: 38)` (fundo #0F141A invisível sobre a tela, raio 8,36).
  - 12.
  - Expanded Column (início): **"Aurea"** 30 sp **w800** letterSpacing −0,8 height 1,05 #F7F9FB (nome próprio, `Text` sem tradução) · 2 · saudação 13,5 sp #AAB6C3 (height 1,35 herdado), 1 linha ellipsis: "Bom dia" (05:00–11:59) / "Boa tarde" (12:00–17:59) / "Boa noite" (18:00–04:59), traduzida; com conta da comunidade: "{saudação}, {apelido}" (apelido não traduz).
  - `_BotaoRedondo` doc_on_doc, tooltip "Abrir template" → abre template (.aurea/.json).
  - 6.
  - `_AvatarDaConta` → vai para a aba Perfil (3).
- `_BotaoRedondo`: alvo 44×44 com círculo 36 `#1B2530` e ícone 17 #F7F9FB; `Tocavel`; Tooltip.
- `_AvatarDaConta`: alvo 44×44, `CircleAvatar` raio **17** (34 dp): com foto (arquivo local existente ou URL https… — a URL só entrou em aba36bb) = imagem; sem foto = fundo `#A9D3EC` (AppColors.violet) + inicial 14 w700 **#FFFFFF** ("?" sem conta). Chave `inicio-perfil`.

### 5.5 Botão "Novo projeto" (762dbfe:530-549)

- Padding (20, 6, 20, 0); `SizedBox(height: 54, width: ∞)`; `FilledButton.icon`: fundo #6FAED9, conteúdo #0B1117, **raio 16**, ícone `plus` 20, gap 8, rótulo "Novo projeto" 17 w600 ls −0,2. Sem ripple (só highlight 5 %). Ação: abre a folha Novo projeto com nome sugerido "Projeto {total+1}" → cria, adiciona à lista e abre o editor. Chave `novo-projeto`.
- Medido: altura 54,1 dp; "N" com altura de maiúscula 12,2 dp (= 17 sp Roboto ✓); "+" 13,3 dp de glifo.

### 5.6 Atalhos redondos (762dbfe:550-580, `_Atalho` 1290-1322)

- Padding (12, 18, 12, 0); Row com 3 `Expanded` (cada um 1/3 de 387,43 = 129,1 dp):
  1. `photo_on_rectangle` · "Mídia" → `_importarMidia` (FilePicker de foto/vídeo; cria projeto com a PROPORÇÃO da mídia, fps 30, nome = arquivo sem extensão; vídeo sem duração → 5 s; abre o editor; falha → snack "Não consegui importar essa mídia.").
  2. `doc_on_doc` · "Template" → `_openTemplate` (.json/.aurea; ".aurea" errado → "Escolha um arquivo .aurea"; ilegível → "Nao consegui ler esse template"; abre como projeto NOVO com id novo).
  3. `cube` · "Cena 3D" → `_importarCena` (XML/.zip/.amproj/.aurea — ver diálogos §5.16).
- `_Atalho`: Column mínima: círculo **56** `#1B2530` com ícone **23** `#6FAED9` · 7 · rótulo 12 **w600** #F7F9FB 1 linha ellipsis (height 1,35 → 16,2). Todo o conjunto é o `Tocavel`.

### 5.7 Hero "Continuar editando" (`_CartaoContinuar`, 762dbfe:927-1077)

- Quando aparece: há projetos **e** não está selecionando **e** sem busca **e** ordem = "Mais recentes". O herói é o 1º da lista arrumada (mais recente) e **não se repete na grade**.
- Padding (20, 18, 20, 0); `ClipRRect` raio **20**; `AspectRatio(16/9)` (371,43 × 208,93 dp). Stack:
  1. Miniatura (`BoxFit.cover`, decodificada com largura 1280 px, `gaplessPlayback`) ou placeholder: degradê topLeft #1B2530 → bottomRight #151C24 com `film` 34 #AAB6C3 no centro.
  2. Scrim vertical: transparente até 45 %, → `#B3000000` em 100 %.
  3. Positioned(left 16, right 16, bottom 14): Row (alinhado embaixo):
     - Expanded Column: "Continuar editando" 11 **w700** ls **+0,4** #6FAED9 · 3 · nome do projeto 18 w700 ls −0,2 **#FFFFFF** 1 linha ellipsis · 2 · ficha 11 `#B3FFFFFF` 1 linha ellipsis.
     - 10.
     - Pílula "Continuar": padding 14h/9v, fundo #6FAED9, raio 999; `play_fill` 13 `#10130C` · 6 · "Continuar" 12,5 w700 `#10130C`. (Medido 101 × 34,7 dp.) É só visual: o cartão inteiro abre.
  4. Positioned(top 2, right 2): reticências 40×40 `ellipsis` 18 `#B3FFFFFF` → menu do projeto (§5.16). Chave `projeto-menu-{id}`.
- Toque = abrir no editor; toque longo = menu do projeto. Chave do cartão `projeto-{id}`.
- Ficha: `"{proporção} · {resolução} · {fps} fps"`, ex. "4:5 · Full HD 1080p · 30 fps", "16:9 · 804p · 24 fps". Proporção = rótulo do preset cuja razão difere < 0,01 (senão o primeiro, "16:9"); resolução = rótulo §5.17 (fora da lista → "{altura}p").

### 5.8 Barra da lista (`_BarraDaLista`, 762dbfe:1682-1860)

- Só existe com **≥ 2 projetos no total** (`todos.length > 1`).
- Padding (20, 18, 12, 6). Modo normal: Expanded "{N} projetos" 15 w700 (N = projetos após busca, INCLUINDO o herói) · `search` · `arrow_up_arrow_down` · `checkmark_circle`.
- Modo busca (após tocar a lupa): no lugar do título e da lupa, `CupertinoTextField` altura 34, autofocus, placeholder "Procurar pelo nome", sufixo `xmark_circle_fill` 16 com padding 8h (limpa, zera a busca e fecha); filtra ao digitar (contém, sem diferenciar maiúsculas). Estilo padrão Cupertino escuro (fundo do campo do SDK).
- Modo seleção: "{n} escolhidos" 15 w700 · `checkmark_circle` (marcar todos os visíveis) · `xmark` (sair).
- `_BotaoDaBarra`: padding 8 + ícone 19 #AAB6C3 (alvo 35×35); `Tocavel`. Chaves: `projetos-buscar`, `projetos-busca-campo`, `projetos-busca-limpar`, `projetos-ordenar`, `projetos-selecionar`, `projetos-marcar-todos`, `projetos-selecao-sair`.
- Ordenar: `CupertinoActionSheet` título "Ordenar os projetos", ações "Mais recentes" / "Nome (A-Z)" / "Mais longos", Cancelar; escolha lembrada em prefs `projetos.ordem` (índice). "Nome" = alfabética sem caixa; "Mais longos" = duração decrescente; "Mais recentes" = ordem da controladora (mais novo primeiro).
- Selecionar: entra no modo com o **primeiro** projeto visível já marcado.

### 5.9 Grade de projetos (762dbfe:600-637, `_CartaoProjeto` 1371-1499)

- `SliverPadding` horizontal 20; `SliverGrid` com colunas = **4** se largura ≥ 700, **3** se ≥ 520, senão **2**; `crossAxisSpacing 12`, `mainAxisSpacing 16`, `childAspectRatio 1,12` (largura/altura).
  - Largura do cartão = (W − 40 − 12·(n−1)) / n → 179,71 dp no aparelho do print; altura = largura / 1,12 = 160,46; miniatura = altura − 6 − 40 = 114,46.
- Itens: `visiveis` = (se "abertos") todos, senão os **6** primeiros (`_recentesNaInicio`), menos o herói. "Abertos" = mostrar todos ligado OU selecionando OU busca/ordem ativa.
- `_CartaoProjeto` (Column início):
  - Expanded: `ClipRRect` raio **14**: miniatura (cover, largura 640 px) ou placeholder = degradê #1B2530→#151C24 com **moldura do formato** centralizada: se razão ≥ 1 → largura 52 % do cartão (`widthFactor 0,52`), senão altura 62 % (`heightFactor 0,62`); `AspectRatio(razão)`; fundo `#0F141A`@0,55, raio 4; `film` 18 #AAB6C3 no centro. (Razão ≤ 0 → 16:9.)
  - 6.
  - Row: [seleção: ícone 18 `checkmark_circle_fill` #6FAED9 (marcado) / `circle` #AAB6C3, padding direita 6, chave `projeto-marca-{id}`] · Expanded Column: nome 14 **w600** ls −0,1 (`Text`, 1 linha ellipsis) · 1 · ficha 11 #AAB6C3 (1 linha) · reticências 40×40 `ellipsis` 18 #AAB6C3 (chave `projeto-menu-{id}`).
- Gestos: toque → abrir (ou marcar/desmarcar se selecionando); toque longo → menu (ou marcar se selecionando); reticências → menu (ou marcar). Desmarcar o último sai do modo.
- Sem estado mantido fora da tela (`addAutomaticKeepAlives:false`), cada cartão escuta sozinho a revisão de miniaturas.

### 5.10 "Mostrar todos" (762dbfe:638-651)

- Aparece se NÃO "abertos" e há mais de 6 projetos. `_Linha` (chave `projetos-todos`): ícone `square_grid_2x2` e "Mostrar todos os {N} projetos"; quando ligado: `chevron_up` e "Mostrar menos". (Obs.: como ligado implica "abertos", o estado "Mostrar menos" nunca é exibido — §8.)

### 5.11 Seções inferiores

- `_TituloSecao(t)`: padding (20, 28, 20, 12); texto 21 **w700** ls −0,4 (#F7F9FB, height 1,35).
- **Modelos**: `SizedBox(height 196)` + ListView horizontal, padding 20h. `_CartaoModelo`: padding direita 12; largura **232**; imagem 232×**146** raio **16** (`BoxFit.cover`, decodificada a 464 px; erro → #1B2530 com `film` #AAB6C3) · 8 · título 14 w600 ls −0,1 (1 linha) · 2 · detalhe 11,5 #AAB6C3 (1 linha). Toque abre o modelo como projeto novo (id novo). 5 modelos:

| id | imagem | título | detalhe |
|---|---|---|---|
| vhf | assets/templates/vhf/thumbnail.jpg | VHF · Neon Orbit | 12 cenas · vetores e gradientes animados |
| dnyx | assets/templates/dnyx/thumbnail.jpg | Aurea App · RMK Dnyx | Texto, fotos, cursores e audio editaveis |
| reference | assets/templates/reference-rebuild.jpg | Nova recriacao · Codex | 5 cenas · 280 quadros · camadas editaveis |
| notes | assets/templates/notes.jpg | Notes | Icone, botao, listas, whip e glow |
| pindown | assets/templates/notes.jpg | Pindown | Casa, faisca medida e coroas 3D |

  Falhas: "Nao consegui preparar o motion VHF. Tente novamente." / "Nao consegui preparar o motion. Tente abrir novamente." / "Nao consegui preparar a trilha. Tente abrir o modelo novamente."
- **Comunidade**: `_LinhaGrande` (chave `inicio-comunidade`): padding (20, 2, 20, 2); círculo 48 `#6FAED9`@0,14 com `person_2_fill` 22 #6FAED9 · 14 · Column: "Veja o que a galera está criando" 15 w600 · 2 · "Poste o seu projeto, responda e reposte" 12,5 (#F7F9FB) · `chevron_right` 15 #AAB6C3. Toque → aba 1.
- **Aprender**: 5 × `_Linha` — padding (20, 13, 20, 13); ícone 19 #6FAED9 · 14 · texto 14,5 (#F7F9FB) · `chevron_right` 15 #AAB6C3 (altura 45,6):
  1. `play_rectangle` "Tutorial em vídeo: sua primeira cena 3D" → TutorialScreen('cena3d') (chave `inicio-tutorial-cena3d`)
  2. `cube_box` "Tutorial em vídeo: cena 3D com modelos e câmeras" → 'cena-completa'
  3. `textformat` "Tutorial em vídeo: texto que quica, do seu jeito" → 'texto-bounce'
  4. `sparkles` "O que ha de novo nesta versao" → diálogo de novidades (§5.18)
  5. `exclamationmark_bubble` "Versao beta: achou um problema? Conte pra gente" → folha Reportar (§6.3)
  (aba36bb acrescentou 6: `info_circle` "Sobre o Aurea" → aba 4.)
- Fim: `SizedBox(120)`.

### 5.12 Barra compacta ao rolar (`_BarraAoRolar`, 762dbfe:792-882)

- Aparece quando o scroll vertical passa de **64 dp**; some ao voltar. Presa no topo (abaixo da status bar), por cima da lista. Entrada/saída por `AnimatedSwitcher` 180 ms (fade, `easeOut`/`easeIn`); escondida sai da árvore.
- Altura **52**, padding (20, 0, 12, 0), fundo `#0F141A`@0,62 com blur σ 18. Row: `AureaLogo(22)` · 8 · "Aurea" 17 w700 ls −0,4 #F7F9FB · espaço · `_BotaoRedondo(doc_on_doc, tooltip 'Template')` · 2 · `_BotaoRedondo(person_crop_circle, tooltip 'Perfil')` → aba 3.

### 5.13 Barra de abas (HomeShell 762dbfe = `aba36bb^:home_shell.dart`)

- `bottomNavigationBar`: `RepaintBoundary` → `ClipRect` → blur σ **24** → Container fundo `#0F141A`@**0,72**, borda superior hairline (`#273442`@0,72) **0,5** → SafeArea(top:false) → altura **54** → Row de **5** `Expanded` na ordem 0..4.
- Conteúdo rola por baixo (Scaffold `extendBody: true`; no print t3 a imagem do modelo aparece borrada atrás da barra: `#181E26`).
- `_TabItem` (tudo é alvo opaco): Column centralizada: ícone **24** · 3 · rótulo 10,5 **w500** ls +0,1, 1 linha ellipsis (Flexible). Cor: ativo #6FAED9 (ícone *_fill*), inativo #AAB6C3. Mesmo peso nos dois estados.

| i | Rótulo | Ícone inativo | Ícone ativo | Corpo |
|---|---|---|---|---|
| 0 | Inicio | house | house_fill | ProjectsTab |
| 1 | Comunidade | person_2 | person_2_fill | CommunityTab |
| 2 | Ajustes | slider_horizontal_3 | slider_horizontal_3 | SettingsTab |
| 3 | Perfil | person | person_fill | SocialProfilePage(embedded) |
| 4 | Sobre | info_circle | info_circle_fill | AboutTab |

- Tocar a aba atual não faz nada; outra aba: `selectionClick` + troca. Medido: ícone glifo 22,9×21 dp começando 8 dp abaixo do topo da barra; rótulo 37–44 dp.
- Com gesto/botões do sistema: a altura total = 54 + inset inferior (0 nos prints; em Compose edge-to-edge somar `WindowInsets.navigationBars`).

### 5.14 Multi-seleção e ações em lote

- Entrar: botão `checkmark_circle` da barra da lista (marca o 1º). Com seleção ativa: não há herói; lista inteira aberta; barra da lista vira "{n} escolhidos" + marcar todos + sair; toques nos cartões marcam/desmarcam.
- `_AcoesEmLote` (Positioned no rodapé da ProjectsTab, por cima da lista; como o Scaffold tem `extendBody`, o fundo dela passa por baixo da barra de abas e o `SafeArea` interno recebe padding inferior = altura da barra de abas (54), então o conteúdo fica logo ACIMA da barra de abas): blur σ 20, fundo `#151C24`@0,88, borda superior `#273442` 0,5; SafeArea(top:false); padding 12h/8v; Row: "{n} escolhidos" 13 #AAB6C3 (Expanded) · botão "Duplicar" (padding 14h/8v; `plus_square_on_square` 18 #F7F9FB · 6 · texto 13) · botão "Excluir" (`delete` 18 #FF6B6B · 6 · texto 13 #FF6B6B). Chaves `projetos-lote-duplicar`, `projetos-lote-excluir`.
- Duplicar: cada marcado vira cópia "{nome} (cópia)" com id novo, na frente da lista; sai do modo.
- Excluir: `CupertinoActionSheet` título "Excluir {n} projetos?", mensagem "Nao da para desfazer.", ação destrutiva "Excluir" (chave `projetos-excluir-lote`), Cancelar; confirma → remove todos e sai do modo.

### 5.15 Vazio, carregamento, recuperação

- **Vazio** (`_SemProjetos`, lista arrumada vazia — sem projetos ou busca sem resultado): padding (20, 4, 20, 0); `film` 22 #AAB6C3 · 12 · "Seus projetos aparecem aqui, com a miniatura do que voce fez." 13,5 #AAB6C3. Sem herói, sem barra da lista (se < 2 no total). Busca sem resultado mostra a MESMA frase (HEAD tem "Nenhum projeto com esse nome.").
- **Carregamento**: não existe. `ProjectsController.build()` devolve `[]` e carrega do disco num microtask → o estado vazio pisca na abertura até os projetos chegarem (§8).
- **Recuperação de crash**: não há UI na Home. A proteção é interna (`main.dart`: erros interceptados; preferências indisponíveis → abre com padrões; gravação imediata ao ir para segundo plano). O que existe visível é "Travadas" em Ajustes (§6.1).
- 1 projeto só: vira herói; sem barra da lista; grade vazia.

### 5.16 Menus e diálogos da Home (762dbfe)

- **Menu do projeto** (reticências ou toque longo) — `CupertinoActionSheet`: título = nome do projeto (`Text`); mensagem = ficha; ações: "Abrir" (`projeto-abrir`), "Duplicar" (`projeto-duplicar`), "Renomear" (`projeto-renomear`), "Excluir projeto" (destrutiva, `projeto-excluir`), "Apagar todos os projetos" (destrutiva); cancelar "Cancelar".
  - Abrir → editor. Duplicar → "{nome} (cópia)". Renomear → `_DialogoDeNome`. Excluir projeto → confirmação abaixo.
- **Confirmar exclusão** — `CupertinoActionSheet` título = nome; ações destrutivas "Excluir projeto" e "Apagar todos os projetos"; "Cancelar". Excluir remove o projeto e a miniatura.
- **Renomear** (`_DialogoDeNome`) — `CupertinoAlertDialog` "Renomear"; `CupertinoTextField` (chave `renomear-campo`, autofocus, capitalização de frases, padding top 12; Enter confirma); ações "Cancelar" e "Salvar" (padrão, chave `renomear-salvar`). Vazio ou igual → nada.
- **Apagar todos** (`apagarTodosOsProjetos`) — `CupertinoAlertDialog` "Apagar todos os projetos?", conteúdo "{total} projeto(s) serao apagados. Isso nao pode ser desfeito.", ações "Cancelar" / "Apagar todos" (destrutiva). Remove todos + miniaturas. Sem projetos → não abre.
- **Cena importada** (Cena 3D) — erro: `CupertinoAlertDialog` "Nao deu para importar" + mensagem (ou "Arquivo ilegivel: {erro}") + "OK". Sucesso (XML/zip/amproj): "Cena importada" com resumo alinhado à esquerda: "{c} camadas e {k} keyframes reconhecidos." + (se houver) "\n\nFicou de fora:" + até 6 linhas "\n- {item}" + "\n- e mais {n}"; ações "Cancelar" / "Abrir" (padrão). `.aurea` abre direto (mídias extraídas para `docs/midias_importadas/{timestamp}`).
- Snacks de falha: `SnackBar(content: AppText(texto))` padrão do tema.

### 5.17 Folha "Novo projeto" (`new_project_sheet.dart@762dbfe`)

- `showModalBottomSheet`: `isScrollControlled`, fundo **#151C24**, topo raio **24**, arrastável (padrão M3), fecha ao tocar fora → devolve nulo.
- Padding (20, 12, 20, 16 + teclado); `SingleChildScrollView`; Column esticada:
  1. Pega 36×5, branco@0,18, raio 3 (centralizada).
  2. 16.
  3. Row (alinhada embaixo): "Novo projeto" (`titleLarge` 22 w700 −0,5) Expanded · ficha viva "{L} × {A} · {fps} fps" 12,5 #AAB6C3 (chave `projeto-ficha`).
  4. 16.
  5. `_Moldura` altura **150**: centralizada, `AspectRatio` animado (240 ms easeOutCubic) na razão escolhida (livre: L/A preso em [0,2; 5]); raio 12; degradê #6FAED9@0,22→@0,06; borda #6FAED9@0,6 × 1,2; dentro (encolhe para caber): rótulo 22 w700 ls −0,3 #F7F9FB (ex. "9:16" ou "1080 × 1350") (chave `moldura-formato`) + dica 12 #AAB6C3.
  6. 12.
  7. Row de 6 `Expanded` `_FormatoItem` (5 presets + "Livre"): padding vertical 6; mini-moldura numa caixa de 28 de altura com lado maior 28 (w = r≥1 ? 28 : 28r; h = r≥1 ? 28/r : 28), raio 4, borda 1,2 #AAB6C3 / escolhido borda **2** #6FAED9 + fundo #6FAED9@0,2 (`AnimatedContainer` 160 ms) · 7 · rótulo 12,5 w600 ls −0,1 (escolhido #6FAED9, senão #F7F9FB) · dica 10 #AAB6C3 1 linha ellipsis. Chaves `formato-{key}`, `formato-livre`.
  8. Se Livre: 12 + Row: `_CampoDeMedida` "Largura" · "×" 16 (padding 10h) · `_CampoDeMedida` "Altura". Campo: rótulo 11 #AAB6C3 · 4 · `CupertinoTextField` numérico 16 #F7F9FB, padding 12h/10v, fundo #1B2530, raio 10 (chaves `livre-largura`, `livre-altura`); padrão 1080 × 1350; lido com clamp [64, 7680] (inválido → padrão).
  9. 20 · rótulo de seção "NOME" · 8 · `CupertinoTextField` (chave `projeto-nome`) placeholder = nome sugerido ("Projeto N") ou "Nome do projeto"; texto 17 ls −0,2 #F7F9FB; placeholder 17 #AAB6C3; padding 14h/13v; fundo #1B2530; raio 12; capitalização de frases; Enter = criar.
  10. (se não Livre) 18 · "RESOLUÇÃO" · 8 · segmentado [HD 720p | Full HD 1080p | QHD 1440p | 4K 2160p].
  11. 18 · "QUADROS POR SEGUNDO" · 8 · segmentado [24 fps | 30 fps | 60 fps].
  12. 24 · botão altura **52** `FilledButton` (tema: #6FAED9, texto #0B1117 17 w600, raio 14) "Criar projeto" (chave `criar-projeto`).
- Rótulo de seção (`_SectionLabel`): texto em CAIXA-ALTA, 12 w500 ls +0,6 #AAB6C3.
- Segmentado (`_Segmented`): `CupertinoSlidingSegmentedControl` largura total, fundo #1B2530, polegar **#0F141A**, cada opção com padding vertical 7 e texto 13 w600 ls −0,1 (escolhida #6FAED9, demais #F7F9FB).
- Presets (`project_presets.dart`): 16:9 "YouTube / TV" 1,7778 · 9:16 "Reels / TikTok" 0,5625 · 1:1 "Feed" 1 · 4:5 "Instagram" 0,8 · 4:3 "Clássico" 1,3333 · Livre "Você escolhe" (ícone `pencil_outline`). Resoluções 720/1080/1440/2160 (rótulos HD 720p / Full HD 1080p / QHD 1440p / 4K 2160p; outro → "{h}p"). FPS 24/30/60.
- Padrões iniciais (Ajustes): proporção **16:9** em 762dbfe (HEAD mudou para 9:16), fps 30, resolução 1080.
- Regra do quadro: resolução = LADO MENOR. `quadroDoFormato(r, res)` = r ≥ 1 ? (round(res·r), res) : (res, round(res/r)) → 1080p 9:16 = 1080 × 1920. Livre: razão = L/A, resolutionHeight = min(L, A).
- Criar: nome vazio → nome sugerido ou "Projeto sem titulo".

### 5.18 Boas-vindas, novidades

**Boas-vindas** (`boas_vindas.dart`, só na 1ª abertura, antes de qualquer novidade; grava `abertura.aceite` e marca a revisão de novidades como vista):
- `showModalBottomSheet` **não dispensável e sem arrastar**, fundo #151C24, topo raio 24, SafeArea; padding (24, 28, 24, 20).
- Logo 64 centralizada · 14 · "Bem-vindo à Aurea" (headlineLarge 24) · 22 · 3 linhas (padding inferior 16): caixa 40×40 raio 12 fundo #6FAED9@0,12 com ícone 20 #6FAED9 · 14 · título 15 w700 #F7F9FB · 2 · texto 12,5 h1,4 #AAB6C3:
  - `square_stack_3d_up` "Camadas de verdade" — "Vídeo, foto, texto, forma e som na mesma timeline, com keyframes em tudo."
  - `wand_stars` "Efeitos que trabalham" — "Mais de noventa efeitos com prévia animada, presets e busca em português."
  - `arrow_up_doc` "Sai do aparelho pronto" — "Exportação em MP4, GIF, PNG e pacote .aurea para levar o projeto com as mídias."
- 18 · "Ao continuar, você combina usar a Aurea com mídias que pode usar: o que você importa e publica é responsabilidade sua." 12 h1,5 #AAB6C3 · 16 · botão 52 de altura, raio 14, fundo #6FAED9, "Começar a editar" 16 w700 (#0B1117; tema claro #FFFFFF), háptico, chave `abertura-comecar`.

**"O que mudou no Aurea"** (`release_notice.dart`; abre uma vez por revisão `releaseNoticeRevision = '2026-09-18-beta-92'`, chave de prefs `aurea.releaseNotice.seen`; também via "O que ha de novo nesta versao"):
- Material `AlertDialog` (chave `release-notice`), fundo `AmColors.panel` (#0F141A), sem tint, inset 20h/24v, rolável; título "O que mudou no Aurea" 21 w700 #F7F9FB; conteúdo largura 380: por item Row (padding inferior 14): ícone 19 #6FAED9 · 10 · texto rico 13 h1,4 #F7F9FB "**{título}:** {corpo}". Ação `FilledButton` fundo #245D8C texto #F7F9FB "Vamos editar" (chave `release-notice-dismiss`).
- Itens (verbatim em §7.6).

### 5.19 Ordem de abertura
1ª abertura → Boas-vindas (e novidades suprimidas). Depois: se revisão ≠ vista → "O que mudou no Aurea". Só abre se a Home for a rota atual. Paralelamente: faixas de aviso/atualização e janela de aviso POPUP.

### 5.20 Versão HEAD (pós-70c7606) — NÃO bate com os prints (referência)

- Fundo `cromo` #0F141A; SafeArea; Column: `_Topo` (57: 16 · logo 30 · 10 · "AUREA" 19 w800 ls 3 · espaço · ☰ 44×44 ícone 24 (chave `home-menu`) · 6) + lista.
- Botão (padding 16/4/16/0): altura 52, raio 100, fundo `acao` **#245D8C**, `plus` 20 + 8 + "Novo projeto" 16 w600 **#F7F9FB**, háptico.
- Cabeçalho da lista (padding 16/20/6/6, linha 44): "Projetos recentes" 16 w600 + busca (vira campo pílula, fundo `campo`, 14) + ordenar (AureaMenu com marcada). Seleção: "{0} escolhidos" + marcar todos + sair.
- Lista: `SliverGridDelegateWithMaxCrossAxisExtent(maxCrossAxisExtent 560, mainAxisExtent 80, spacing 8/10)` → 1 coluna no celular. Cartão: fundo `painel` #151C24 (marcado `destaqueApagado`), raio 8, padding 8; miniatura 64×64 raio 5 (fundo `campo`, vazio = moldura `campoAlto` 32 raio 3); 12; nome 15 w600; 4; ficha "1080 × 1920 · 0:05" 12 muted tabular (`{outputW} × {outputH} · {m:ss | h:mm:ss}`); 2; `clock` 12 + data/hora da última edição (hoje = hora; este ano = dia/mês curto; senão data curta); ⋯ 44×64 → AureaMenu: Abrir / Renomear / Duplicar / Compartilhar / Excluir (destrutivo). Toque longo = selecionar.
- Menu ☰: Importar mídia · Abrir template · Importar projeto · Modelos (folha com linhas 80×45) · Aprender (folha) · Ajustes · Sobre · Apagar todos os projetos (destrutivo, desabilitado sem projetos).
- Lote: barra 52 fundo `painel`, "Duplicar" / "Excluir" (#FF6B6B). Vazio: "Seus projetos aparecem aqui…" ou "Nenhum projeto com esse nome.".
- Barra de abas: fundo chapado `palco` #0A0E13, 54; ordem [Inicio, Comunidade, "+" (pílula 52×34, raio 100, fundo #245D8C, `plus` 24 #F7F9FB, háptico, `home-criar`), Perfil, Ajustes]; Sobre fora da barra.
- Folha Novo projeto HEAD: `mostrarAureaFolha` "Novo projeto" (grande 200 ms), padding 22/…/22/15; seções (`secao` 11 w600, top 15/bottom 6): Nome (campo raio 8, 15) · Proporção (AureaChip: 9:16, 16:9, 1:1, 4:5, 4:3, Livre) · Livre → AureaValueField Largura/Altura (72, 64..7680) · Resolução (720p, 1080p, 1440p, 4K) · Quadros por segundo (24, 25, 30, 60) · Fundo (6 amostras 40 com disco 26: Preto #000000, Branco #FFFFFF, Cinza #7F7F7F, Azul #0A1630, Verde #00B140, Azul chroma #0047BB) · ficha viva (moldura 28 na cor do fundo + "L × A · N fps") · "Criar projeto" (48, pílula #245D8C, 15 w600).

---

## 6. AJUSTES / SOBRE / AJUDA (estrutura)

### 6.1 Ajustes (`settings/presentation/settings_tab.dart`)
- `SafeArea(bottom:false)` → ListView padding (20, 12, 20, 120). "Ajustes" (headlineLarge 34) · 24.
- `ListTile` Idioma (Material: `Icons.language`, subtítulo = idioma atual, `Icons.chevron_right`) → `SimpleDialog` "Idioma" com: Português, English, Español, العربية, 한국어, 日本語, 简体中文, हिन्दी, Bahasa Indonesia, Русский (falha → snack "Tente novamente").
- Grupos: cabeçalho (padding esquerda 16, baixo 8; CAIXA-ALTA 12 w500 ls +0,6 #AAB6C3) + caixa `Material` #151C24 raio 16 + divisores recuados 16 (hairline 0,5); 26 entre grupos.
  1. **PADROES DE NOVOS PROJETOS**: Proporcao [16:9 9:16 1:1 4:5 4:3] · Resolucao [720p 1080p 1440p 4K] · Camada nova dura [2 s 3 s 5 s] · Quadros por segundo [24 30 60].
  2. **APARENCIA**: 762dbfe = segmentado "Tema" [Escuro | Claro | Sistema]. HEAD = "Tema" + fileira rolável de amostras (6 temas + "Sistema"; amostra 64 de largura, aro 3,5 = `chip` do tema ou #6FAED9 se marcada, disco 45 com o fundo do tema — Sistema = metade Aurea/metade Light na diagonal —, miolo 20 com a cor de ação e ✓ 14; nome 11,5, marcado w700 #6FAED9) + nota "Troca na hora, sem reabrir o app. No Light, o editor continua escuro."
  3. **LEGENDAS**: Transcrição automática [Automático | Nuvem | No aparelho] + explicação 12,5 muted ("Com internet, na nuvem; sem, no aparelho." / "Groq Whisper, pelo servidor do Aurea. Usa sua conta da comunidade." / "whisper.cpp no aparelho. O áudio não sai do celular.").
  4. **EXPORTACAO**: switch "Salvar na galeria" / "Copia o video exportado para a galeria".
  5. **DESEMPENHO** (só HEAD): "Perfil" + 5 opções tocáveis (Automático, Economia, Equilibrado, Desempenho, Máxima qualidade — rótulo 15, escolhido w700 #6FAED9 + ✓ 16; explicação 12 h1,35 muted) + nota "O perfil vale so para o que se ve enquanto se edita…".
  6. **CENA 3D**: Motor 3D [Automatico | Sempre GPU | Sempre CPU] + nota · Qualidade 3D [teto] + nota · "Travadas" / "O que demorou, medido pelo proprio aparelho" → TravadasScreen.
  7. **GRAFICOS** (só Android): switch "Desenhar com OpenGL ES" / "Para cores erradas no preview em alguns aparelhos" + nota.
  8. **GERAL**: switch "Vibracao ao interagir" · "Limpar cache" / "Remove arquivos temporarios de preview e render" → snack "Cache limpo ({mb} MB liberados)".
- Linha segmentada: padding 16/14; rótulo bodyLarge 17; 10; `CupertinoSlidingSegmentedControl` (fundo #0F141A, polegar #1B2530, opções 13 w600, escolhida #6FAED9, padding vertical 6). Switch: padding (16,10,12,10), título bodyLarge + subtítulo bodySmall, `CupertinoSwitch` (#6FAED9, polegar ligado #0B1117). Linha de toque: `InkWell`, padding 16/12, `chevron_right` 16 muted.
- **TravadasScreen** (`travadas_screen.dart`): AppBar "Travadas" com ações "Copiar tudo" (snack "Registro copiado") e "Limpar"; ListView padding (16,12,16,32): "Versao e motor 3D em uso" + texto selecionável; "Por marca, do que mais pesou"; lista de travadas (monoespaçado) ou "Nenhum quadro passou de {limite} ms…"; "Constroi x desenha".

### 6.2 Sobre (`about/presentation/about_tab.dart`)
- ListView padding (20,12,20,120): "Sobre" (headlineLarge) · 32 · logo 96 · 18 · "Aurea" (headlineLarge 28) · 4 · "Editor de video e composicao" (bodySmall) · 10 · pílula da versão (padding 12h/5v, fundo #6FAED9@0,12, raio 20, "Versao {versao}" 12 w600 #6FAED9; **7 toques** liga/desliga ferramentas de desenvolvedor → snack) · 24.
- [dev] grupo: "Ferramentas de desenvolvedor" (`wrench`, "Versao {v} · build {b}") · "Rever avisos e dicas" (`arrow_counterclockwise`, "Novidades da versao e dicas de primeiro uso voltam") → snack "Avisos e dicas voltam na próxima abertura".
- `BetaBanner`: padding 14/14, fundo `0x22FFB020`, raio 14, borda `0x55FFB020`; `exclamationmark_triangle` 18 #FFB020 · 10 · "Versao beta para testes" 13 w700 #FFB020 + "Pode ter erros, travar ou perder alteracoes nao salvas. Achou um problema ou quer sugerir algo? Toque aqui." 11 h1,35 muted · `chevron_right` 14 #FFB020 → folha Reportar.
- Caixa #151C24 raio 16 padding 16: "Aurea e um editor de video e composicao para celular: timeline multi-trilha, preview em tempo real e exportacao direto do aparelho, sem depender de nuvem." (bodyMedium muted).
- Grupo (ListTiles, ícones 21 #6FAED9): "Como usar o AUREA" / "Guia rápido e ajuda dos efeitos · offline" (`book`) → QuickGuideScreen · "Tecnologia" / "Flutter + FFmpeg" (`bolt`) · "Reportar erro ou sugerir" / "Vai direto para o criador" · "Criador" / "Ruanzitwo  ·  @ofruanzitwo  ·  TikTok @ruanzitwo" (abre Reportar) · "Licencas de codigo aberto" (`doc_text`) → `showLicensePage` · "Modelos 3D fornecidos por Sketchfab" (`cube_box`, só HEAD).
- 26 · "Feito por Ruanzitwo com Flutter" (bodySmall 11).

### 6.3 Reportar (`about/presentation/report_sheet.dart`)
- Folha fundo #0F141A (AmColors.panel), topo raio 20. Cabeçalho: `exclamationmark_bubble` + "Reportar" 18 + X. "Erro, sugestao de ferramenta ou de efeito — vai direto para o criador do app." 12 h1,35. Chips de tipo (raio 9, 12): "Erro / bug", "Nova ferramenta", "Novo efeito", "Outro". Campo (raio 12, padding 12, 14; placeholder 13 muted): "O que aconteceu?" (bug) / "O que voce gostaria que existisse?"; bug: "Como fazer acontecer de novo (opcional)". Switch "Incluir modelo e versao do sistema". Botão "Enviar" (raio 12, 15) → mailto (assunto "Aurea {versao} — {tipo}") ou copia o relato. Divisor; "Criador" + linhas (nome/e-mail, "Instagram" @…, "TikTok" @…) com chevron 14. Snacks: "Escreva o que aconteceu antes de enviar", "Abrindo seu app de e-mail", "Sem app de e-mail. Copiei o relato — cole em {email}", "Nao consegui abrir o link". (E-mail do criador em `AureaAutor.email`, `report_sheet.dart:19`.)

### 6.4 Ajuda — "Como usar o AUREA" (`help/presentation/quick_guide_screen.dart`)
- AppBar "Como usar o AUREA"; ListView padding (20,12,20,40): "Do zero ao primeiro motion" (titleLarge) · 8 · "Guia rápido • disponível sem internet" · 16 · 6 passos (título titleMedium + 4 + texto; padding inferior 16): "1 · Crie e adicione", "2 · Selecione a camada", "3 · Anime com dois keyframes", "4 · Combine efeitos", "5 · Salve e retome", "6 · Exporte" · ExpansionTiles "Receita: texto com brilho e entrada suave" e "Desempenho e limites" · "Guia dos {n} efeitos" + busca (TextField "Buscar efeito ou finalidade", limpar "Limpar busca") + ExpansionTile por efeito (nome/categoria/ajuda) ou "Nenhum efeito encontrado. Tente brilho, cor ou distorção." Textos longos: verbatim no arquivo (linhas 148-169 e o mapa `effectHelp`).

---

## 7. TEXTOS VISÍVEIS (pt-BR, reproduzir exatamente — inclusive a falta de acento onde existe)

Observação: o app traduz a partir destas frases-chave (`AppText` → `translate`); nomes de projeto/apelido/cor não traduzem.

### 7.1 Barra de abas / shell
`Inicio` · `Comunidade` · `Ajustes` · `Perfil` · `Sobre` · (aba36bb+: tooltip/semântica `Criar projeto`)

### 7.2 Home (762dbfe)
`Aurea` · `Bom dia` · `Boa tarde` · `Boa noite` · `{saudação}, {apelido}` · `Abrir template` (tooltip) · `Template` (tooltip da barra compacta) · `Perfil` (tooltip) · `Novo projeto` · `Mídia` · `Template` · `Cena 3D` · `Continuar editando` · `Continuar` · `{N} projetos` · `Procurar pelo nome` · `{0} escolhidos` · `Ordenar os projetos` · `Mais recentes` · `Nome (A-Z)` · `Mais longos` · `Cancelar` · `Mostrar todos os {N} projetos` · `Mostrar menos` · `Modelos` · `Comunidade` · `Veja o que a galera está criando` · `Poste o seu projeto, responda e reposte` · `Aprender` · `Tutorial em vídeo: sua primeira cena 3D` · `Tutorial em vídeo: cena 3D com modelos e câmeras` · `Tutorial em vídeo: texto que quica, do seu jeito` · `O que ha de novo nesta versao` · `Versao beta: achou um problema? Conte pra gente` · (aba36bb+) `Sobre o Aurea` · `Seus projetos aparecem aqui, com a miniatura do que voce fez.` · `Duplicar` · `Excluir` · títulos/detalhes dos modelos (§5.11) · ficha `{proporção} · {resolução} · {fps} fps`.

### 7.3 Menus, diálogos e snacks da Home
`Abrir` · `Duplicar` · `Renomear` · `Excluir projeto` · `Apagar todos os projetos` · `Cancelar` · `Salvar` · `Excluir {n} projetos?` · `Nao da para desfazer.` · `Excluir` · `Apagar todos os projetos?` · `{total} projeto(s) serao apagados. Isso nao pode ser desfeito.` · `Apagar todos` · `Cena importada` · `{c} camadas e {k} keyframes reconhecidos.` · `Ficou de fora:` · `- e mais {n}` · `Nao deu para importar` · `Arquivo ilegivel: {erro}` · `OK` · `Escolha um arquivo .aurea` · `Nao consegui ler esse template` · `Não consegui importar essa mídia.` · `Mídia Importada` (nome padrão) · `Nao consegui preparar o motion VHF. Tente novamente.` · `Nao consegui preparar o motion. Tente abrir novamente.` · `Nao consegui preparar a trilha. Tente abrir o modelo novamente.` · `{nome} (cópia)` · `Projeto {n}` · `Projeto sem titulo`.

### 7.4 Folha Novo projeto (762dbfe)
`Novo projeto` · `{L} × {A} · {fps} fps` · `Medida livre` · `Livre` · `Você escolhe` · `16:9` `YouTube / TV` · `9:16` `Reels / TikTok` · `1:1` `Feed` · `4:5` `Instagram` · `4:3` `Clássico` · `Largura` · `Altura` · `×` · `NOME` (de "Nome") · `Nome do projeto` · `RESOLUÇÃO` (de "Resolução") · `HD 720p` · `Full HD 1080p` · `QHD 1440p` · `4K 2160p` · `QUADROS POR SEGUNDO` · `24 fps` `30 fps` `60 fps` · `Criar projeto`.
HEAD acrescenta: `Proporção` · `Fundo` · `Preto` `Branco` `Cinza` `Azul` `Verde` `Azul chroma` · `720p` `1080p` `1440p` `4K` · `25`.

### 7.5 Faixas e aviso
`Aviso` · `Abrir` · `Agora não` · `{texto}  Saiba mais ›` · `!` · `Atualizar` · `Tentar de novo` · `A versao {nome} ja esta disponivel.` · `A versao {nome}: {notas}` · `Baixando a versao {nome} · {p}%` · `Baixando a versao {nome}…` · `Conferindo o arquivo…` · `Abra o instalador e toque em Instalar.` · `Instalando…`. Texto do aviso no print t1 (vem do servidor, não é do app): "+100 usuários no app, obrigado! A partir de agora o Aurea se prepara para a v1.0 oficial: estamos refazendo a interface, otimizando o app e trazendo mais efeitos e ferramentas. Obrigado!"

### 7.6 Boas-vindas e novidades
Boas-vindas: `Bem-vindo à Aurea` · `Camadas de verdade` · `Vídeo, foto, texto, forma e som na mesma timeline, com keyframes em tudo.` · `Efeitos que trabalham` · `Mais de noventa efeitos com prévia animada, presets e busca em português.` · `Sai do aparelho pronto` · `Exportação em MP4, GIF, PNG e pacote .aurea para levar o projeto com as mídias.` · `Ao continuar, você combina usar a Aurea com mídias que pode usar: o que você importa e publica é responsabilidade sua.` · `Começar a editar`.
Novidades (`release_notice.dart:12-48`): título `O que mudou no Aurea`; botão `Vamos editar`; itens:
- `Atualizar sem sair do app`: `Quando sair versao nova, aparece uma faixa na Inicio. Toque em Atualizar: o app baixa, confere o arquivo e abre o instalador. Nao precisa procurar APK nem entrar em grupo.`
- `O brilho parou de travar`: `Deep Glow, Brilho, S_GlowAura e os outros da aba Glow e Luz pediam centenas de leituras de textura por pixel a cada quadro. Agora a previa paga uma fracao disso e a exportacao continua no maximo — o resultado salvo nao mudou.`
- `Texto arabe ligado`: `Com animacao por letra, cada letra arabe saia solta. A forma de cada letra vem dos vizinhos, e agora o desenho mantem a ligacao.`
- `Camada bloqueada de verdade`: `O cadeado ganhou botao no menu da camada e uma faixa com Desbloquear. Bloqueada nao anda, nao apara, nao edita e nao some por engano.`
- `Editor de pontos e transicoes`: `Editar pontos voltou a desenhar e editar o mesmo caminho, e o sistema de transicoes saiu do app — projetos antigos continuam abrindo.`
(`whats_new.dart` guarda o histórico longo `aureaNews` — folha "O que ha de novo"/"Histórico de novidades", não ligada à Home 762dbfe.)

### 7.7 Ajustes / Sobre / Ajuda / Reportar
Ver §6 (todos os textos citados lá são verbatim). Outros do DS: `Valor exato` · `Valor exato ({unidade})` · `Conta incompleta` · `Fica em` · `Cancelar` · `OK` · `Expressão` · `ex.: wiggle(2, 30) ou time * 90` · `Limpar` · `Cor` · `Pronto` · `Quadro` · `Roda` · `RGB` · `Opacidade da cor` · `Copiar código` · `Colar código de cor` · `Minhas cores` · `segure e arraste para a lixeira` · `Rápidas` · `Código copiado: {codigo}` · `Não é um código de cor` · `Conta-gotas do palco` · `Arraste sobre o palco e solte na cor que quer.`

---

## 8. BUGS, GAMBIARRAS E RISCOS (arquivo:linha — versão indicada)

**Home / shell (762dbfe)**
1. `home_shell.dart` (Column com `AvisoAoVivo`/`FaixaDeAtualizacao`) + `aviso_ao_vivo.dart:96` + `faixa_de_atualizacao.dart:243` + `projects_tab.dart@762dbfe:515`: **inset da status bar aplicado duas vezes** quando há faixa — vão extra de 48,76 dp entre faixa e cabeçalho, visível no print t1. Compose: consumir `WindowInsets.statusBars` uma vez só (decidir com o dono se o vão do print é intencional; tudo indica que não).
2. `projects_tab.dart@762dbfe:462`: no menu do projeto, "Apagar todos os projetos" faz `Navigator.pop(false)` numa rota `showCupertinoModalPopup<String>` → erro de tipo (`bool` não é `String?`) em tempo de execução; o fluxo depende do que o Flutter faz com a exceção.
3. `projects_tab.dart@762dbfe:362-369`: a confirmação de excluir UM projeto oferece também "Apagar todos os projetos" (ação destrutiva em massa ao lado da unitária).
4. `projects_tab.dart@762dbfe:412-416`: excluir em lote não apaga as miniaturas (`ThumbnailService.delete` só é chamado na exclusão unitária e no "apagar todos") → arquivos órfãos.
5. `projects_controller.dart@762dbfe:85-89`: lista começa `[]` e carrega em microtask → **estado vazio pisca** na abertura; não há estado de carregamento.
6. `projects_tab.dart@762dbfe:1790`: "{N} projetos" sem plural — com busca filtrando para 1/0 mostra "1 projetos"/"0 projetos".
7. `projects_tab.dart@762dbfe:638-650`: a linha "Mostrar menos" nunca aparece (só existe quando `!abertos`, e `mostrarTodos` liga `abertos`); só a de "Mostrar todos" funciona.
8. Alvos de toque pequenos: `_BotaoDaBarra` 35×35 (`:1852-1858`); reticências 40×40 no hero (`:1056-1068`) e na grade (`:1483-1491`); X do aviso **40×28** apesar do comentário "44" (`aviso_ao_vivo.dart:173-188`). Android pede 48 dp.
9. `projects_tab.dart@762dbfe:1232-1240`: inicial branca `#FFFFFF` sobre avatar `#A9D3EC` → contraste ≈ 1,6:1 (o "?" do print quase some).
10. `projects_tab.dart@762dbfe:1038,1045`: cor `#10130C` escrita à mão (sobra do tema lima) na pílula "Continuar"; deveria ser `onAccent` #0B1117.
11. `projects_tab.dart@762dbfe:1917,1921`: `#FF6B6B` escrito à mão em vez de `danger`.
12. `projects_tab.dart@762dbfe:786`: modelo "Pindown" usa a miniatura do "Notes" (`notes.jpg`).
13. Miniatura do hero/grade é captura do palco com as faixas pretas do letterbox (visível no t3: o hero mostra barras pretas laterais); `BoxFit.cover` corta o resto.
14. `_AcoesEmLote` depende de um efeito colateral do `Scaffold(extendBody: true)` (o `MediaQuery.padding.bottom` do corpo vira a altura da barra de abas) para não ficar escondida atrás dela; a faixa de 54 dp de fundo da barra de lote fica sob a barra de abas (dois vidros sobrepostos). Em Compose: posicionar explicitamente acima da `HomeTabBar`.
15. `_AvatarDaConta._cache` (`:1247-1256`) nunca invalida: trocar a foto no mesmo caminho não atualiza a existência do arquivo.
16. `_TabItem` (`home_shell.dart`): `_selectedStyle` e `_idleStyle` idênticos (só a cor muda) — redundância.
17. Rótulo "Inicio" sem acento (e vários "Nao", "Versao", "Padroes", "Aparencia"…): são chaves de tradução — mudar o texto quebra a tradução; decidir no port se corrige em pt-BR.
18. Alturas fixas (barra de abas 54, botão 54, pílula, linhas 45,6) com fonte do sistema grande (> 1,3×) cortam texto; só os rótulos das abas têm ellipsis.
19. Barra de navegação do sistema preta opaca nos prints (não edge-to-edge); Android 15+ força edge-to-edge — o port precisa tratar o inset inferior da barra de abas.

**Faixas / diálogos**
20. `release_notice.dart:54`: fundo do diálogo = `AmColors.panel` = #0F141A, o MESMO da tela — o diálogo só se separa pelo véu.
21. `whats_new.dart:506-570`: `WhatsNewCard` sem uso e com degradê verde legado (`#16281B`).
22. `faixa_de_atualizacao.dart`/`aviso_ao_vivo.dart`: cada faixa é "dona do relógio" (inicia/para o serviço no init/dispose) — lógica de rede presa a widget.

**Design system (HEAD)**
23. `core/ds/aurea_teclado_numerico.dart:322-331`: botão OK fundo `destaque` #6FAED9 com texto `sobreAcao` #F7F9FB → contraste ≈ 2,2:1 (deveria ser `onAccent` #0B1117).
24. `core/ds/aurea_teclado_numerico.dart:28`: "50%" é calculado sobre `max`, não sobre a faixa (max − min) como diz o comentário.
25. `core/ds/aurea_chip.dart:160`: chip de 28 dp de altura = alvo abaixo do mínimo (a folha HEAD compensa com padding vertical 6 → 40).
26. `core/ds/aurea_toolbar_button.dart:170`: desabilitado apaga só o ícone; o rótulo continua `textoSecundario` cheio.
27. `core/ds/aurea_value_field.dart:9`: comentário diz "caixa de 56"; o valor real é 74 (`AureaDims.caixaDeValor`).
28. `core/ds/aurea_toggle.dart:7`: comentário cita "linha de propriedade de 51"; a linha é 44.
29. `core/ds/aurea_menu.dart:214`: altura estimada do menu com título = +38 (chute); `:248` a escala entra sempre de `topRight`, mesmo quando o menu abre ACIMA do botão.
30. `core/ds/aurea_layer_row.dart:66`: largura da faixa = `faixaDeCor / 2.5` (número mágico → 4).
31. `core/ui/am_tick_ruler.dart:396,399`: cores dos riscos fixas (#43516A/#7485A3), não seguem o tema.
32. `core/ds/aurea_seletor_de_cor.dart` (aba RGB, `_canais`): mexer num canal reconstrói via HSV → em cinzas/preto a matiz se perde e pula.
33. `core/ds/aurea_seletor_de_cor.dart` usa `IconButton`/`PopupMenuButton`/`TextField` Material no meio do visual Cupertino/DS.

**Ajustes / Sobre**
34. `settings_tab.dart:72-76,77-112,610-647`: `ListTile` + `Icons.language` + `SimpleDialog` + `InkWell` Material misturados com o visual Cupertino.
35. `about_tab.dart:268`: a linha "Criador" abre a folha de relato (não um perfil/links).
36. `about_tab.dart:219-223,313-316`: "Tecnologia: Flutter + FFmpeg" e "Feito por … com Flutter" ficarão falsos no app nativo.
37. `core/utils/versao_do_app.dart:22-25`: `versaoDoApp = '1.1.8-beta'`, `buildDoApp = 93`, enquanto o `pubspec.yaml` diz `1.2.1-beta+96` (762dbfe) / `1.2.1-beta-a01+2101` (HEAD) — a pílula do Sobre mostra versão errada.

**HEAD**
38. `new_project_sheet.dart` (`_Ficha`): `GestureDetector` externo e `AureaChip` interno com o mesmo `onTap` (dois detectores para o mesmo toque).
39. HEAD tira "Sobre" da barra de abas (só pelo menu ☰) e muda a proporção padrão para 9:16 (`settings_controller.dart:45`).

---

## 9. MAPEAMENTO Flutter → Compose (nomes propostos)

| Flutter (arquivo) | Compose |
|---|---|
| `AureaColors` (`core/theme/aurea_colors.dart`) | `object AureaBrandColors` |
| `AureaPaleta` + `AureaTemaId` (`core/theme/aurea_paleta.dart`) | `data class AureaPalette` + `enum class AureaThemeId` + `AureaPalettes` |
| `AppColors`, `AmColors`, `AureaTokens`, `AureaCores` | `AureaColorScheme` exposto por `LocalAureaColors` (papéis: background, surface, surfaceHigh/panel, chip, campoAlto, text, muted, hairline, accent, onAccent, accentDim, primary(acao), onPrimary, keyframe, danger, warning, selected, selectedText, stage, playhead) |
| `AppTheme.tema` (`core/theme/app_theme.dart`) | `@Composable fun AureaTheme(palette, content)` (MaterialTheme + CompositionLocals + `LocalTextStyle` base §1.2 + indicação sem ripple) |
| `AureaSpacing`, `AureaRadius`, `AureaShadows`, `AureaDims` | `object AureaSpacing`, `AureaRadius`, `AureaShadows`, `AureaDims` |
| `AureaTypography`, `AureaEstilos`, textTheme | `object AureaType` (TextStyles) |
| `AureaMotion` | `object AureaMotion` (tween specs; `saida = CubicBezierEasing(1/3f, 0f, 2/3f, 1/3f)`, `entrada = decelerate`) |
| `Tocavel` (`core/ui/tocavel.dart`) | `Modifier.aureaPressable(...)` |
| `AureaLogo` (`core/widgets/aurea_logo.dart`) | `@Composable AureaLogo(size, withBackground)` (Canvas) |
| `AureaSnack`, `showReasonToast` (`core/ui/snack.dart`) | `AureaSnackbarHost` + `showAureaSnack()` |
| `pedirNome` (`core/ui/pedir_nome.dart`) | `AureaNameDialog` |
| `AmTickRuler`, `AmArrastoDeValor`, `paraCadaRisco`, `leituraDePosicao` | `TickRuler`, `Modifier.dragValue(...)`, `forEachTick()`, `positionReading()` |
| `AureaBottomSheet`, `mostrarAureaFolha` | `AureaBottomSheet`, `showAureaSheet()` (ModalBottomSheet sem drag-to-dismiss) |
| `AureaChip` | `AureaChip` |
| `AureaDropdown` | `AureaDropdown` |
| `AureaEffectCard` | `AureaEffectCard` |
| `AureaKeyframeButton`, `AureaLosango`, `AureaSetasDoKeyframe`, `KeyframeState`, `marcasVizinhas` | `KeyframeButton`, `KeyframeDiamond`, `KeyframeArrows`, `KeyframeState`, `neighborKeyframes()` |
| `AureaLayerRow` | `AureaLayerRow` |
| `AureaMenu`, `AureaMenuItem`, `mostrarAureaMenu` | `AureaMenu`, `AureaMenuItem`, `AureaAnchoredMenu` (Popup posicionado) |
| `AureaPanel`, `AureaAvisoDoPainel` | `AureaPanel`, `AureaPanelNotice` |
| `AureaPropertyRow` (+ `.ponto/.cor/.personalizada`) | `PropertyRow`, `PointPropertyRow`, `ColorPropertyRow`, `CustomPropertyRow` |
| `AureaSection` | `AureaSection` |
| `SeletorDeCor`, `showColorPicker`, `ColorWell` | `AureaColorPicker`, `showColorPicker()`, `ColorWell` |
| `AureaSlider`, `PintorDoAureaSlider` | `AureaSlider` (Canvas) |
| `AureaTabs` | `AureaSubTabs` |
| `TecladoNumerico`, `showNumberInput`, `showExpressionEditor` | `NumericKeypadSheet`, `showNumberInput()`, `ExpressionEditorDialog` |
| `AureaToggle` | `AureaSwitch` (estilo Cupertino 51×31) |
| `AureaToolbarButton` | `ToolbarButton` / `ToolbarBlockButton` |
| `layerKindColor/Icon` | `LayerKind.color / .icon` |
| `AureaValueField` | `ValueField` |
| `pegarCorDoPalco` (`conta_gotas.dart`) | `EyedropperOverlay` |
| `CupertinoActionSheet` / `CupertinoAlertDialog` | `AureaActionSheet` / `AureaAlertDialog` (reproduzir métricas §1.10) |
| `CupertinoSlidingSegmentedControl` | `AureaSegmentedControl` |
| `CupertinoTextField` | `AureaTextField` |
| `CupertinoActivityIndicator` | `AureaActivityIndicator` |
| `HomeShell`, `_TabItem` (`home_shell.dart`) | `HomeScaffold`, `HomeTabBar`, `HomeTabItem` |
| `AvisoAoVivo`, `_Faixa` | `LiveNoticeBanners`, `LiveNoticeBanner`, `LiveNoticeDialog` |
| `FaixaDeAtualizacao` | `UpdateBanner` |
| `ProjectsTab` (`projects_tab.dart`) | `HomeScreen` (+ `HomeViewModel`) |
| `_BarraAoRolar` | `CollapsingHomeTopBar` |
| `_Cabecalho`, `_BotaoRedondo`, `_AvatarDaConta` | `HomeHeader`, `RoundIconButton`, `AccountAvatar` |
| botão "Novo projeto" | `NewProjectButton` |
| `_Atalho` | `QuickAction` |
| `_CartaoContinuar`, `_ThumbProjeto` | `ContinueEditingCard`, `ProjectThumbnail` |
| `_BarraDaLista`, `_BotaoDaBarra` | `ProjectListBar`, `ListBarIconButton` |
| `_CartaoProjeto` | `ProjectGridCard` |
| `_Linha`, `_LinhaGrande`, `_TituloSecao` | `HomeLinkRow`, `HomeFeatureRow`, `HomeSectionTitle` |
| `_CartaoModelo`, `_modelos` | `TemplateCard`, `builtInTemplates` |
| `_SemProjetos` | `ProjectsEmptyState` |
| `_AcoesEmLote` | `BatchActionsBar` |
| `_menuDoProjeto`, `_confirmarExclusao`, `_excluirEscolhidos`, `_DialogoDeNome`, `apagarTodosOsProjetos` | `ProjectActionsSheet`, `ConfirmDeleteSheet`, `ConfirmBatchDeleteSheet`, `RenameProjectDialog`, `DeleteAllProjectsDialog` |
| `fichaDoProjeto` | `ProjectSpec.format()` |
| `showNewProjectSheet`, `_Moldura`, `_FormatoItem`, `_Segmented`, `_SectionLabel`, `_CampoDeMedida`, `quadroDoFormato` | `NewProjectSheet`, `AspectPreviewFrame`, `AspectOptionItem`, `AureaSegmentedControl`, `SheetSectionLabel`, `DimensionField`, `frameFor()` |
| `ProjectPresets`, `AspectOption` | `ProjectPresets`, `AspectOption` |
| `OrdemDosProjetos`, `projetosArrumados` | `ProjectSort`, `arrangeProjects()` |
| `showBoasVindas` | `WelcomeSheet` |
| `showReleaseNotice`, `releaseHighlights` | `WhatsNewDialog`, `releaseHighlights` |
| `SettingsTab`, `_GroupHeader`, `_Group`, `_GroupDivider`, `_SegmentedRow`, `_TemaRow`, `_AmostraDeTema`, `_SwitchRow`, `_TapRow` | `SettingsScreen`, `SettingsGroupHeader`, `SettingsGroup`, `SettingsDivider`, `SettingsSegmentedRow`, `ThemePickerRow`, `ThemeSwatch`, `SettingsSwitchRow`, `SettingsNavRow` |
| `TravadasScreen` | `FreezeLogScreen` |
| `AboutTab`, `BetaBanner` | `AboutScreen`, `BetaBanner` |
| `showReportSheet` | `ReportSheet` |
| `QuickGuideScreen` | `QuickGuideScreen` |
