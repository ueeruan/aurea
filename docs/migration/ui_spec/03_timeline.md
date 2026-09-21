# 03 — TIMELINE (inventário para o port Flutter → Jetpack Compose)

Fonte: `C:\Users\SnyX\Documents\Projetos - Claude\Aurea` (somente leitura). Caminhos abaixo são
relativos a `Aurea/lib/src/` salvo indicação. `file:linha` = linha real no arquivo do HEAD atual
(`959d725`, 21/09/2026). Unidades: dp (Flutter "logical px") e sp; tempo em µs como no código.
Conversão do print: **dp = px / 2,625** (t2.png = 1080×2400 px = 411,4×914,3 dp).

---

## 0. LEIA PRIMEIRO — a âncora visual NÃO é o código atual

### 0.1 O que o t2.png realmente mostra

`t2.png` (não versionado, mtime 20/09/2026 04:12) foi capturado **antes** da timeline nova existir:

| fato | evidência |
|---|---|
| A pasta `features/editor/presentation/ui/timeline/` nasceu em `ff6da19` (21/09 03:51) | `git log` |
| A timeline antiga `features/editor/presentation/am/am_timeline.dart` foi apagada em `75b0426` (21/09 04:57) | `git show --stat 75b0426` |
| No momento do print a timeline em uso era `am_timeline.dart@aba36bb` (release Beta A.01, 19/09 22:47) | nenhum commit tocou `am_timeline.dart` entre `aba36bb` e o print |
| Todas as medidas do print batem com as constantes de `am_timeline.dart@aba36bb` | seção 1.B (régua 20+18, linha 46, barra 36, pílula 58×28 `#1E222D`, quadradinho `#FFE899`, raio 8, cabeçote rosa 1,6 com botão 8×8, relógio 13sp w700 + sublinhado 58×1,5) |

Consequências importantes:

1. **O print está em "modo compacto"** (painel *Efeitos* aberto): a timeline antiga mostrava **só a camada
   selecionada** (`singleLayerId`), e os **`<` `>` dentro do clipe NÃO são alças de trim** — são as setas
   `_SetaDeVizinho` que trocam a camada selecionada (`selectNeighbor(+1)` no `<`, `selectNeighbor(-1)` no `>`).
   As alças de trim da antiga eram retângulos brancos 16 dp *dentro* das pontas do clipe e **não aparecem em modo
   compacto**.
2. **O cabeçote vermelho (#FF6B6B = `AureaColors.danger`) com o "botão" 8×8 no topo** só existia quando o painel
   aberto era Efeitos, Curva ou Animação de texto (`pinkPlayhead`, `editor_screen.dart@aba36bb:727-730`). Fora disso
   o cabeçote era branco e sem botão. Na timeline nova ele é sempre branco (`AureaColors.playhead = #FFFFFF`) e
   sem botão.
3. O código listado na tarefa (pasta `ui/timeline/`) é a **timeline nova** (redesign de 21/09), com números
   diferentes (régua 42, linha 34, clipe 29, cabeçalho 70, raio 1,5, contorno de seleção azul-claro, alças de trim
   azuis FORA do clipe, cabeçote branco com selo 70×18).
4. O histórico de commits é ambíguo sobre qual UI é "a aprovada": `5200483` diz *"Recursos que o redesign tinha
   perdido voltam, sem mexer na UI aprovada"* (sugere que o redesign foi aprovado), e `741e766` diz *"Controle de
   efeitos no desenho do app antigo"* (o dono pediu partes da UI antiga de volta).

**Decisão que precisa ser tomada pelo dono (bloqueante para pixel-perfect):** qual conjunto visual portar.
Este documento entrega os DOIS, completos:

- **Conjunto A — código atual (`ui/timeline/`)**: geometria, cores, comportamento. É o comportamento mais maduro
  (corrige dezenas de bugs da antiga) e é o que os testes atuais (`test/ui/timeline/*`) cobram.
- **Conjunto B — t2.png (`am_timeline.dart@aba36bb`)**: aparência medida no print + constantes recuperadas do git.

**Recomendação técnica:** comportamento = Conjunto A (seções 2–9). Aparência = escolher A ou B token a token pela
tabela da seção 1.C. Se a ordem for "igual ao print", usar B para régua/cabeçote/relógio/pílula/barra e manter os
gestos de A.

### 0.2 Paleta resolvida (tema padrão `AureaPaleta.aurea`, `core/theme/aurea_paleta.dart:220-242`)

Todas as cores do Conjunto A são PAPÉIS (getters que leem a paleta ativa; há 6 temas). Valores no tema padrão:

| papel (`AureaCores.*`, `core/ds/tokens.dart:383-417`) | origem | hex |
|---|---|---|
| `palco` | `stage` | `#0A0E13` |
| `cromo` (fundo da timeline, régua, cabeçalhos, selo) | `background` = `AureaColors.bg` | `#0F141A` |
| `painel` | `surface` | `#151C24` |
| `elevado` (pílula do cabeçalho, migalha de grupo) | `panel` = `surfaceHigh` | `#1B2530` |
| `campo` (fundo da linha escolhida, α .55) | `chip` | `#212D3A` |
| `campoAlto` (pílula do cabeçalho escolhido) | `lerp(chip, textPrimary, .08)` | ≈ `#323D49` |
| `texto` | `AureaColors.text` | `#F7F9FB` |
| `textoSecundario` (riscos e rótulos da régua) | `AureaColors.muted` | `#AAB6C3` |
| `destaque` (contorno de seleção, alças, guia do ímã, curva) | `accent` = `brandLight` | `#6FAED9` |
| `acao` (pontas I/O na régua) | `primary` = `brand` | `#245D8C` |
| `keyframe` (losango aceso, seta de expandir) | `brandSoft` | `#A9D3EC` |
| `selecao` (não usada na timeline nova; ver 4.A) | `brandDeep` | `#123A63` |
| `cabecote` | `playhead` | `#FFFFFF` |
| `perigo` | `danger` | `#FF6B6B` |
| divisor | `border` | `#273442` |

Tabela de cor/ícone por tipo de camada (`core/ds/aurea_tipo_da_camada.dart:54-84`, Conjunto A e B usam a mesma):

| LayerKind | cor | ícone (CupertinoIcons) |
|---|---|---|
| video | `#6A52E0` | `videocam_fill` |
| image | `#3D6FD9` | `photo_fill` |
| audio | `#1F8C93` | `music_note` |
| text | `#B07A16` | `textformat` |
| caption | `#8A6A1E` | `captions_bubble_fill` |
| shape | `#2E9459` | `circle_fill` |
| particles | `#B0417A` | `sparkles` |
| element3d | `#C06A24` | `cube_fill` |
| scene3d | `#A85520` | `cube_box_fill` |
| camera | `#2A7B9B` | `camera_fill` |
| group | `#4C5566` | `folder_fill` |
| adjustment | `#5A4A7A` | `slider_horizontal_3` |
| nullLayer | `#444C5C` | `smallcircle_circle` |

`layerTypeStripe(l) = lerp(cor, branco, .28)` existe (`aurea_tipo_da_camada.dart:87`) mas não é usado pela timeline nova.

---

## 1. GEOMETRIA

### 1.A Conjunto A — timeline nova (tokens em `core/ds/tokens.dart:80-174`)

| token | valor | uso |
|---|---|---|
| `regua` | **42** | altura da régua (`tokens.dart:83`) |
| `linhaDeCamada` | **34** | altura de TODA linha: camada E propriedade (ListView `itemExtent`, `timeline.dart:538`) |
| `recuoDoClipe` | 2,5 | recuo vertical do clipe na linha |
| `clipe` | 29 | `34 − 2×2,5` |
| `raioDoClipe` | **1,5** | raio do retângulo do clipe |
| `recuoDoRotuloDoClipe` | 14 | nome a 14 da ponta esquerda do clipe |
| `rotuloDoClipe` | 11 sp | fonte do nome (w600) |
| `cabecalhoDaCamada` | **70** | largura do cabeçalho (sobreposto à ponta esquerda da linha) |
| `faixaDeCor` | 10 | faixa de cor no cabeçalho |
| `miniaturaDoCabecalho` | 32 | slot da miniatura |
| `olhoDoCabecalho` | 44 | alvo de toque do olho |
| `alcaDeTrim` | **30** | faixa de TOQUE de cada alça, FORA do clipe |
| `desenhoDaAlcaDeTrim` | **17 × 15** | desenho da alça |
| `cabecote` | **1,5** | largura da linha do cabeçote |
| `toqueDoCabecote` | 100 × 38 | zona de toque no alto (marcar/menu de marcas) |
| `dpPorSegundo` | **100** | zoom inicial (dp/s) |
| `bordaDeAutoRolagem` | 38 | distância da borda que dispara auto-rolagem |
| `velocidadeDeAutoRolagem` | 120 dp/s | velocidade constante da auto-rolagem |
| `riscoDeSegundo / MeioSegundo / Quadro` | 18 / 12 / 5 | altura dos riscos da régua (a partir da base) |
| `larguraDoRisco` | 1 | espessura dos riscos |
| `vaoMinimoDoRisco` | 4 | vão mínimo para um nível de risco aparecer |
| `rotuloDaRegua` | 14 (**não usado**; a régua usa `textoDeRotulo` = 10 sp) | |
| `marcadorDeTempo` | 42 (não usado; a marca usa 22×20) | |
| `raioDoKeyframe` | **6** (losango de 12) | |
| `tracoDoKeyframe` | 1 | contorno do losango |
| `folgaDoKeyframe` | **20** (±10 dp) | folga de toque horizontal do losango |
| `botaoMaisDaTimeline` | 40 (não usado na timeline) | |
| `tracoDeSelecao` / `tracoDeMultisselecao` | 2 / 1,5 | contorno do clipe escolhido / do lote |
| `botaoAdicionar` | 73 | padding inferior da lista (o "+" cobre a ponta de baixo) |
| `timelineMinimaComPainel` | `42 + 2×34 = 110` | a timeline nunca fica abaixo disso com painel aberto |

Outros números soltos do Conjunto A:

| número | onde |
|---|---|
| selo do tempo 70 × 18, top 2, raio 3 (`raioSm`) | `cabecote.dart:31-32,53-57,118-124` |
| linha do cabeçote começa em y = 2 + 18 = **20** e vai até a base | `cabecote.dart:41-45` |
| zona de toque do cabeçote: left = centro − 50, top **5**, 100 × 38 | `cabecote.dart:78-82` |
| vão mínimo entre rótulos da régua **56 dp** | `regua.dart:269` |
| meia largura do selo para esconder rótulo **35 + 3 = 38** | `regua.dart:266` |
| passos de rótulo (s): 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600 | `regua.dart:261-263` |
| batidas: risco de 8 dp na base | `regua.dart:409-411` |
| marca: triângulo 8 × 7 no topo + fio 1 dp até a base | `regua.dart:428-442` |
| toque da marca: ±11 dp, só nos 20 dp de cima | `marcas_da_regua.dart:30,34` |
| migalha de grupo: left 4, top 4, **94 × 20** (`70+24`), raio 5 | `regua.dart:96-146` |
| ímã: **12 dp** (clipe/alça), **8 dp** (losango → cabeçote), **10 dp** (scrub → marca) | `ima.dart:20,23`; `timeline.dart:321` |
| folga do toque longo depois de aceito **8 dp** | `linha_da_camada.dart:44` |
| intenção da auto-rolagem **4 dp** | `estado_da_timeline.dart:201` |
| cache extent da lista: 2 linhas (68 dp) | `timeline.dart:540-543` |
| zoom: **2 … 800 dp/s** | `estado_da_timeline.dart:44-45` |

#### Fórmulas de `geometria.dart` (a MESMA conta para pintar e para tocar)

Coordenadas da LINHA = coordenadas da timeline (x = 0 na borda esquerda da tela; o cabeçalho de 70 fica POR CIMA).

```
x0 = xDoTempo(layer.startUs)          x1 = xDoTempo(layer.endUs)                         (geometria.dart:17-21)
caixa = Rect(LTRB: x0, 2.5, x1, 34 − 2.5)  →  y 2,5 … 31,5, centro y = 17               (geometria.dart:24-29)
noCorpo(p)  = x0 ≤ p.x ≤ x1   (altura inteira da linha)                                 (geometria.dart:33-36)

zonasDasAlcas (t = 30, esq = 70, dir = largura da timeline, meio = (x0+x1)/2):          (geometria.dart:59-81)
  início (só se x0 ≥ 70):
     se x0 − 30 ≥ 70:  [x0 − 30, x0)                 ← fora do clipe
     senão:            [70, max(x0, min(70 + 30, meio)))   ← entra no clipe até completar 30, sem passar do meio
  fim (só se x1 ≤ dir):
     se x1 + 30 ≤ dir: (x1, x1 + 30]
     senão:            (min(x1, max(dir − 30, meio)), dir]
  ponta escondida (x0 < 70 ou x1 > dir) = sem alça ("o que não se vê não se apara")

naAlca(p): início se zonaInício.de ≤ p.x < zonaInício.até; fim se zonaFim.de < p.x ≤ zonaFim.até   (geometria.dart:47-54)

desenhoDaAlca(x, cy = 17, lado): 17 × 15, top = cy − 7,5  (y 9,5 … 24,5)                (geometria.dart:86-100)
  início: se x − 17 < 70 → desenha DENTRO (x … x+17), senão FORA (x−17 … x)
  fim:    se x + 17 > largura → DENTRO (x−17 … x), senão FORA (x … x+17)

yDoLosangoNoClipe = 34 − 2,5 − 6 − 2 = 23,5  (faixa de baixo do clipe; losango ocupa y 17,5…29,5)   (geometria.dart:104-108)
yDoLosangoNaTrilha = 34 / 2 = 17                                                          (geometria.dart:111)

losangoEm(p): alvo = tempoDoX(p.x) − layer.startUs; busca binária em temposLocais (ordenados);
  testa os índices lo−2 … lo+1; vence o de menor |xDoTempo(start + t) − p.x| ≤ 10 (folga 20/2).  (geometria.dart:116-148)
```

Na linha da camada o losango só aceita toque na METADE DE BAIXO (`p.y ≥ 17`, `linha_da_camada.dart:983-985`);
na linha de propriedade, na altura inteira (`linha_da_propriedade.dart:269-271`).

#### Recuo à esquerda (tempo 0 alcançando o cabeçote)

Não há padding: o mapeamento é centrado. `vistaUs` (o instante sob o cabeçote) é limitado a `[0, duração]`
(`estado_da_timeline.dart:128-137`). Com `vistaUs = 0`, o instante 0 fica em `x = largura/2`; a metade esquerda
mostra "tempo negativo" (vazio, sem riscos). No fim do projeto idem à direita.

#### Layout vertical da timeline (Conjunto A)

```
y = 0 ........ Régua (42)            ← PintorDaRegua + pontas + toque longo + marcas + migalha
y = 42 ....... ListView de linhas (34 cada; padding bottom 73)
overlay ...... CamadaDeGuias (top 42 → base): fio do ímã + traço de destino do reordenar
overlay ...... CabecoteDaTimeline (tela inteira): linha 1,5 (y 20 → base), selo 70×18 (y 2), zona 100×38 (y 5)
```
Fundo de tudo: `AureaCores.cromo` (`timeline.dart:451-453`). Altura total: o que sobra na casca
(prévia = 40% da altura útil, `tokens.dart:50`; com painel aberto o painel entra ABAIXO da timeline e ela nunca
fica < 110, `editor_shell.dart:494-513`).

### 1.B Conjunto B — t2.png / `am_timeline.dart@aba36bb` (medido + constantes do git)

Medidas do print convertidas a dp; posição y relativa ao topo da timeline (px 1281 do print).

| elemento | medido no t2 | constante no git | observação |
|---|---|---|---|
| topo da timeline | px 1281 (= 488,0 dp da tela) | — | fundo `#0A0E13` (medido) |
| altura da timeline no print | 236 px = **89,9 dp** | `height` vindo da casca | modo painel aberto |
| faixa da régua (riscos) | riscos até y 17,9 | `SizedBox(height: 20)` + `SizedBox(height: 18)` = **38** (`@aba36bb:521-523,673`) | linhas começam em y **38** (medido 38,1) |
| risco de SEGUNDO | 4 px = **1,5 dp** de largura, y 1,9 → 17,9, cor `#8A97AD` | stroke **1,4**, de y=2 a h−2 (`@aba36bb:1297-1325`) | |
| risco de DÉCIMO | 2 px ≈ 1 dp, y 9,1 → 17,9, cor `#5A6880` | stroke **1**, de y=9 a h−2 | 10 por segundo |
| espaçamento dos décimos | 21 px = **8 dp** | `step = pps/10`; se `< 2` só segundos | ⇒ pps = **80 dp/s** |
| cabeçote | 4 px = **1,5 dp**, `#FF6B6B`, altura inteira (y 0 → base) | `width: 1.6`, `Center` (`@aba36bb:766-773`) | rosa só com painel Efeitos/Curva/Anim. texto |
| botão do cabeçote | 21×21 px = **8×8 dp**, raio 2, y 0 → 8 | `8×8`, `BorderRadius 2`, `topCenter` (`@aba36bb:774-788`) | só quando cor ≠ branco |
| relógio | glifos x 178,7 → 232,4 dp (centrado no cabeçote), topo dos glifos y 11,8, altura 8,8 | `Positioned(top: 8)`; texto **13 sp w700 branco, letterSpacing .5, tabular** (`@aba36bb:718-764`) | formato `MM:SS:FF` (2 dígitos no minuto, `core/utils/time_format.dart@aba36bb`) |
| sublinhado do relógio | x 176,8 → 234,3 dp (**57,5 ≈ 58**), y 28,2 → 29,3 (**1,5**) | `Container(width: 58, height: 1.5, white)` 2 dp abaixo do texto | |
| linha | — | `kAmRowHeight = 46` (`@aba36bb:34`) | |
| barra (clipe) | y 38,1 → 73,9 = **35,8 ≈ 36** | `kAmBarHeight = 36`, no TOPO da linha (`top: 0`) | sobra 10 dp abaixo |
| raio da barra | ≈ 8 | `Radius.circular(8)` (`@aba36bb:2505,2562`) | |
| contorno da barra escolhida | 4 px = **1,5 dp** branco | stroke 1,5 branco, `rrect.deflate(0.75)` (`@aba36bb:2548-2583`) | em compacto a barra é sempre "escolhida" |
| faixa esquerda da cor do tipo | `#3D6FD9` visível x 544–550 px | `Rect(0,0,4,h)` = **4 dp** (α .5 se oculta) | por cima de miniaturas |
| corpo da barra (imagem, escolhida) | `#2F539B` | `lerp(panelHigh #151C24, corDoTipo, 0.66)` (0,46 normal; 0,22 oculta) | confere: lerp(#151C24,#3D6FD9,.66)=#2F539B |
| trilho dos losangos (faixa de baixo) | `#254179`, 15 dp de altura (y 58,9 → 73,9) | preto α .22 sobre o corpo, `kAmFaixaKeyframes = 15` (`@aba36bb:66`; virou 19 em `7d7eaf1`) | |
| fio de luz no alto | — | linha 1 dp branco α .10 (α .24 escolhida), de x=4 | |
| seta `<` | glifo centrado a 24,8 dp da ponta do clipe | `_SetaDeVizinho` 22 dp de largura, ícone `chevron_left` 14, branco 70% (`@aba36bb:2586-2608`) | padding esquerdo do conteúdo 14 |
| ícone do tipo | x = ponta + 36,2 dp | `Icon(layerTypeIcon, 11, branco α .85)` + 6 de respiro | α .45 se oculta |
| nome | x = ponta + 53,7 dp, centro vertical 10,5 dp abaixo do topo da barra | **12 sp w600 branco, letterSpacing −0.1**, 1 linha, reticências | área de conteúdo = 36 − 15 = 21 dp |
| seta `>` | centro a 383,8 dp da borda esquerda da tela | `Spacer` + `_SetaDeVizinho(chevron_right)`; padding direito 10 | a barra passava da borda; a posição exata não fecha com `R − 21` — possivelmente build com mudança local |
| pílula do cabeçalho | x 4,2 → 62,1 (**58**), y 46,9 → 75,0 (**28**) | `Container(h 28, w 58, margin left 4, #1E222D, raio 14)`, centralizada na linha (`@aba36bb:887-894`) | |
| olho | glifo x 13,7 → 29,3 dp | 26 dp de largura × altura da linha, ícone 16, `Colors.white70`, `eye`/`eye_slash` | `spaceEvenly` |
| quadradinho de cor | x 39,2 → 57,1, y 52,8 → 70,7 (**18×18**) `#FFE899` | 18×18, raio 4, cor da etiqueta da camada ou `#FFE899`; borda 1 `accent` se for fonte de matte | ícones dentro (cor `#0F141A`): cadeado 11 (travada) / visto 13 (no lote) / `arrow_turn_left_down` 11 (recortada) |
| coluna dos cabeçalhos | — | `Positioned(left 0, top 38, width 66)`, gradiente bg→bg α .95 (78%)→transparente | lista própria sincronizada com a das barras |
| canto direito da régua | caixa `#090E13` x 382,1 → 411 dp, y 0 → 22; ícone ↖↘ (`arrow_up_left_arrow_down_right`, glifo 10,7 dp) branco | **não encontrado** em `am_timeline.dart@aba36bb` nem em `editor_screen.dart@aba36bb` | provável sobreposição da casca (expandir timeline); riscos sob ela aparecem apagados |

Outros valores do Conjunto B (git, não visíveis no print):

| item | valor (`am_timeline.dart@aba36bb`) |
|---|---|
| zoom | pps inicial **80**; pinça 4…400; auto-enquadrar ao abrir projeto ≥ 20 s: `pps = clamp((larguraTela − 32)/segundos, 4, 80)` (`:384-390`) |
| rolagem horizontal | `SingleChildScrollView` com padding horizontal = largura/2 dos dois lados, `BouncingScrollPhysics`; largura do conteúdo = `maxEnd·pps + 200` (`:391,508-517`) |
| alça de trim (modo normal) | 16 × (36 − 4) dp, `top 2`, left = `x0 − 3` e `x1 − 13` (DENTRO das pontas), `#F2F5F9` raio 4, risco central 2×14 preto α .38 (`:2610-2667`) |
| ≡ na barra | `line_horizontal_3` 14 branco 70% se largura > 150 (modo normal) |
| ◇ "tem animação" | `rhombus` 12 branco se largura > 120 |
| contagem do grupo | pílula preto α .35 raio 6, padding 5×1, 9,5 sp w800 |
| losango | quadrado 11×11 girado 45°, raio 2, borda preta α .85 1,2, brilho; aceso `#FFC107` (âmbar) com sombra âmbar α .5 blur 3; apagado branco α .9; alvo de toque 28 × 16/17; losangos a < 4 dp viram pílula de 10 dp de altura raio 5 (`:2229-2335, 2391-2475`) |
| posição do losango | top = `36 − 15 − 1 = 20` (normal) ou `36 − 13 = 23` (compacto) |
| losango na mão | escala 1,4 + balão de tempo `MM:SS:FF` 10 sp w700 em fundo preto α .82 raio 4 |
| batidas na régua | risco `teal α .55` 1 dp de 55% da altura até a base; 1 em cada 4 `teal α .9` 1,6 dp de 30% até a base |
| marca na régua | triângulo 10 × 9 + rótulo 9 sp na cor da marca; widget 22 × 20 arrastável |
| juntar | botão "Juntar" 48 × (36−10) em `action`, raio 6, 9,5 sp w800 |

### 1.C Tabela de decisão A × B (visual)

| token | A (código) | B (t2) |
|---|---|---|
| altura da régua / início das linhas | 42 / 42 | 20 de riscos + 18 = 38 |
| linha | 34 (todas) | 46 |
| clipe | 29, centrado (recuo 2,5) | 36, colado no topo da linha |
| raio do clipe | 1,5 | 8 |
| corpo do clipe | cor do tipo cheia (escolhida: `lerp(cor, texto, .16)`) | `lerp(#151C24, cor, .46/.66/.22)` + faixa esquerda 4 dp na cor do tipo + trilho de losangos escuro |
| contorno de seleção | 2 dp `#6FAED9` (lote 1,5) | 1,5 dp branco |
| fundo da linha escolhida | `campo #212D3A` α .55 na linha inteira | nenhum |
| nome no clipe | 11 sp w600 `#F7F9FB`; cadeado como glifo prefixo | 12 sp w600 branco ls −0,1; ícone do tipo 11 antes |
| alças de trim | 17×15 `#6FAED9` FORA do clipe, toque 30 fora | 16×32 `#F2F5F9` DENTRO das pontas |
| cabeçalho | 70 × 34, pílula reta à esquerda, faixa de cor 10, miniatura, olho 44 | pílula 58 × 28 `#1E222D` flutuando a 4 da borda, olho + quadradinho 18 |
| cabeçote | 1,5 branco, começa em y 20 | 1,6 branco (rosa com painel), topo a topo; botão 8×8 quando rosa |
| relógio | selo 70×18 fundo `cromo`, 12 sp w600, `m:ss:ff` | sem fundo, 13 sp w700 ls .5, `MM:SS:FF`, sublinhado 58×1,5 |
| riscos | segundo 18 / meio 12 / quadro 5, `#AAB6C3`, na BASE da régua, com rótulos `m:ss` 10 sp | segundo 16 / décimo 9, `#8A97AD`/`#5A6880`, sem rótulos |
| losango | 12 dp `#A9D3EC` contorno 1 `#0F141A`; escolhido branco + contorno `#6FAED9` ×1,25 | 11 dp girado âmbar `#FFC107` / branco .9 |
| zoom base | 100 dp/s | 80 dp/s |

---

## 2. TEMPO ↔ PIXEL, ZOOM, RÉGUA, TIMECODE

### 2.1 Mapeamento (A) — `estado_da_timeline.dart:83-99`

```
centro = largura / 2                          // da timeline INTEIRA, não da área à direita do cabeçalho
xDoTempo(us) = centro + (us − vistaUs) / 1e6 · pps
tempoDoX(x)  = vistaUs + (x − centro) / pps · 1e6
usPorPx(px)  = px / pps · 1e6
janela (px de CONTEÚDO, 0 = instante 0):  JanelaDaTimeline.de(offset = vistaUs/1e6·pps, viewport = largura, recuo = centro)
   ini = floor((offset − recuo − 96)/256)·256 ;  fim = ceil((offset − recuo + largura + 96)/256)·256   (janela_da_timeline.dart:48-59)
```
- `vistaUs` é **double** (fração de quadro) — o relógio (`playback.time`) anda na grade de quadros; se a vista fosse o
  relógio, a pinça/arrasto pulariam um quadro inteiro por passo (`estado_da_timeline.dart:47-53`).
- `pps` = dp por segundo, inicial 100, limitado a [2, 800] (`estado_da_timeline.dart:44-45,140-143`).
- Sem animação/suavização de zoom: cada evento aplica o valor direto.

(B): `scrollOffset = t/1e6 · pps` com o conteúdo recuado largura/2; cabeçote no centro da tela.

### 2.2 Pinça (A) — `timeline.dart:224-260`

- Começa quando o `ScaleGestureRecognizer` da raiz inicia com ≥ 2 ponteiros: pausa o playback, `segurarVista()`,
  guarda `pps0`, zera `escala0`, calcula `focoUs = tempoDoX(foco.x)` (o instante sob os dedos) e dá `selectionClick`.
- A cada evento (≥ 2 dedos): `base = escala0 ??= d.scale` (evita salto: o primeiro `scale` reportado vira a base);
  `zoom(pps0 · d.scale / base)`; depois `irPara(focoUs − (foco.x − centro)/pps·1e6)` ⇒ **âncora = instante sob o
  ponto focal** e pan de dois dedos junto (o foco pode andar).
- Fim: `soltarVista()`; se ainda há dedo na tela, `_ignorarResto = true` (o dedo que sobrou não vira scrub até todos
  levantarem).
- Efeito colateral: `irPara` faz `seek` a cada passo → o cabeçote (tempo do projeto) muda durante a pinça quando o foco
  não está no centro.
- Limite: como `irPara` limita `vistaUs` a [0, duração], perto do início/fim a âncora não se mantém (o conteúdo
  escorrega sob os dedos).
- (B) pinça: `newPps = clamp(pps0·scale, 4, 400)`, âncora no centróide, `jumpTo` pós-quadro (`@aba36bb:464-502`).

### 2.3 Régua (A) — `regua.dart:271-454`

Pintor com `repaint: estado.vista` (não reconstrói com o relógio). Algoritmo:

```
t0 = tempoDoX(0)/1e6 ; t1 = tempoDoX(largura)/1e6           (segundos)
passoDoRotulo = primeiro p em [1,2,5,10,15,30,60,120,300,600,1800,3600] com p·pps ≥ 56 (senão 3600)
passoDoSegundo = 1 se pps ≥ 4, senão primeiro p da mesma lista com p·pps ≥ 4
comMeio    = pps/2 ≥ 4           (pps ≥ 8)
passoQuadro = pps / fps ; comQuadros = passoQuadro ≥ 4   (30 fps → pps ≥ 120; 24 fps → pps ≥ 96; 60 fps → pps ≥ 240)
for s = max(0, floor(t0/passo)·passo) … ceil(t1) step passoDoSegundo:
   x = xDoTempo(s·1e6)
   risco de segundo: (x, h−18) → (x, h)
   se s % passoDoRotulo == 0: rótulo em (x+3, h − 18 − alturaDoTexto), exceto se colidir com o selo
        (x+3+larguraDoTexto > centro−38 && x+3 < centro+38)
   se passoDoSegundo == 1:
       meio: (x + pps/2, h−12) → (x+pps/2, h)            se comMeio
       quadros q = 1…fps−1: (x + q·passoQuadro, h−5)→(…, h), pulando q·2 == fps quando comMeio
```
- Tudo em 3 `Path` (segundos, meios, quadros); cor `textoSecundario`, 1 dp; quadros com α .6.
- Rótulo: 10 sp `textoSecundario`, texto `m:ss` (< 1 h) ou `h:mm:ss`; cache de `TextPainter` por segundo, limpo acima de 160.
- Nada é desenhado para tempo negativo.
- **Batidas** (`project.beats`): busca binária do primeiro visível; riscos de `h−8` a `h`, 1 dp, `destaque` α .55.
- **Marcas** (`project.markers`): triângulo `(x−4,0)(x+4,0)(x,7)` na cor da marca + fio 1 dp da cor α .6 de y 7 à base.
- **Pontas** (`_PontasDaRegua`, `regua.dart:157-227`): traço 2 dp de altura inteira em x−1 + rótulo 9 sp w800 em (x+2, 1):
  `I`/`O` (in/out da sessão) em `acao`; `intro`, `final`, `▣` (miniatura) em `destaque`. Só se marcadas.
- **Migalha de grupo** (dentro de grupo): caixa `elevado` raio 5 em (4,4) 94×20; `chevron_left` 11 + nome do grupo
  10 sp `texto`. Toque = `exitGroup`; toque longo = menu "Voltar a…" (`sairAteONivel`).
- Na régua a pinça e o scrub são da raiz (a régua não tem gestos próprios além do toque longo e das marcas).

Exemplos a 30 fps: pps 100 → riscos de segundo e meio, rótulo a cada 1 s ("0:01"), sem quadros. pps 200 → + quadros
(6,7 dp). pps 20 → segundos (20 dp) + meios (10 dp), rótulo a cada 5 s. pps 5 → só segundos (meio = 2,5 < 4),
rótulo a cada 15 s. pps 2 → riscos a cada 2 s, rótulo a cada 30 s.

(B) régua: 10 riscos por segundo (`step = pps/10`, 1 forte a cada 10), sem números; relógio central é a única leitura.

### 2.4 Timecode

| | A (selo, `cabecote.dart:103-142`; `regua.dart:457-463`) | B (`formatTimecode`) |
|---|---|---|
| formato | `m:ss:ff` — minutos SEM zero à esquerda, sem horas | `MM:SS:FF` |
| quadros | `ff = (us % 1e6) · fps ~/ 1e6` (trunca) | idem |
| fonte | 12 sp (`textoDeInfo`) w600 `texto`, `tabularFigures` | 13 sp w700 branco, ls .5, tabular |
| fundo | RRect 70×18 raio 3 `cromo` | nenhum; sublinhado 58×1,5 branco 2 dp abaixo |
| repinta | ao `playback.time` (grade de quadros), `RepaintBoundary` próprio | `ValueListenableBuilder` |
| fps inválido | ≤ 0 → 30 | |

---

## 3. MODELO DE ROLAGEM

### 3.1 Horizontal = o TEMPO anda sob um cabeçote FIXO (A e B)

- **Cabeçote fixo no centro da timeline inteira** (`estado_da_timeline.dart:81-83`). Quem anda é o conteúdo.
  Arrastar o conteúdo para a DIREITA traz o passado para baixo do cabeçote (o tempo VOLTA):
  `irPara(vista0 − usPorPx(dedo.x − x0))` (`timeline.dart:237-240`).
- Scrub = seek: cada passo chama `aoScrub` (o gerente de vídeo entra em "busca rápida" ANTES do seek, ordem
  obrigatória, `estado_da_timeline.dart:131-136`) e `playback.seek`. O playback é pausado no início do scrub.
- **Inércia do scrub** (`timeline.dart:291-309`): se ao soltar `|vx| > kMinFlingVelocity` (50 dp/s) e não há mais dedos:
  `FrictionSimulation(drag 0.135, x0 0, v0 vx)` → `x(t) = vx · (0.135^t − 1) / ln 0.135`, distância total
  ≈ `vx / 2,0025` (≈ 0,5 s de velocidade inicial). A cada tique `irPara(vistaInercia − usPorPx(x(t)))`; para quando
  `isDone` (|v| < 0,001) ou quando a vista não mudou (bateu em 0/duração). A vista fica "presa" durante a inércia.
- **Fim do scrub** (sem inércia ou quando a inércia acaba): `soltarVista()` (a vista se acomoda no quadro em que o
  relógio caiu) e `_encaixarNasMarcas()`: com o ímã ligado e parado, se há marca a ≤ 10 dp (`usPorPx(10)`) do tempo,
  `irPara(marca)` + `selectionClick` (`timeline.dart:315-333`). Não roda após pinça.
- Um dedo que pousa para a inércia (horizontal e vertical: `_rolagem.jumpTo(offset)`) (`timeline.dart:184-195`).
- **Seguir o relógio**: `playback.time` → `_seguirRelogio` → `vistaUs = time` sempre que nenhum gesto segura a vista
  (contador `_seguram`, `estado_da_timeline.dart:103-123`). Durante o play a vista acompanha o relógio (60 Hz), e só
  os pintores repintam.
- Roda do mouse (desktop/emulador): horizontal (ou Shift) anda no tempo por `scrollDelta` px (pausa o play);
  vertical rola a lista (`timeline.dart:351-372`).

### 3.2 Vertical = a lista de camadas

- `ListView.builder` com `NeverScrollableScrollPhysics(parent: ClampingScrollPhysics())`: a lista NÃO rola sozinha;
  quem rola é a raiz (o mesmo gesto que decide scrub × rolagem): `jumpTo(clamp(rolagem0 − (y − y0)))`
  (`timeline.dart:241-248`). No fim, se `|vy| > 50`: `position.goBallistic(−vy)` (física Clamping = fling do Android).
- Cabeçalho e clipe estão na MESMA linha (o cabeçalho é um `Positioned` de 70 por cima da ponta esquerda), então
  **não existe sincronização de duas listas** no Conjunto A. (No B havia duas `ListView` sincronizadas por
  listeners, `@aba36bb:199-204,228-229`.)
- `_revelar(id, noTopo)` (`timeline.dart:398-416`): ao escolher uma camada (pelo palco, desfazer…) a linha dela é
  trazida à vista após o quadro; com painel aberto (ou quando o painel abre) ela vai para o TOPO
  (`topo = índice · 34`).
- Padding inferior 73 (o "+" cobre a ponta de baixo, `timeline.dart:535-537`).

### 3.3 Auto-rolagem durante arrastos — `estado_da_timeline.dart:183-283`

- Horizontal (mover clipe, aparar, arrastar losango): zona esquerda `x < 70 + 38 = 108`, direita `x > largura − 38`.
  Só rola para o lado a que o dedo FOI desde o ponto de partida por mais de 4 dp (`x < desde − 4` / `x > desde + 4`):
  pegar um clipe já perto da borda não sai rolando.
- Vertical (reordenar): `y < 38` ou `y > alturaDaLista − 38`, mesma regra de intenção.
- Velocidade **constante 120 dp/s** (não proporcional à profundidade na borda). Ticker: `passo = 120 · dt`;
  horizontal: `irPara(vista + dir · usPorPx(passo))` e, se a vista mudou, reaplica o arrasto com o dedo parado
  (`_aoRolarX`); vertical: `jumpTo(clamp(pixels + dir·passo))` e reaplica.
- Como usa `irPara`, a auto-rolagem horizontal faz SEEK (o tempo do projeto anda) e para em 0/duração.

---

## 4. RENDERIZAÇÃO DO CLIPE

### 4.A Conjunto A — `PintorDaLinha` (`pintor_da_linha.dart:277-526`)

Ordem de pintura de uma linha de camada:

1. **Fundo da linha** se escolhida ou do lote: `Rect(0,0,largura,34)` em `campo` α .55 (`:345-352`).
2. Culling: se `x1 < −30` ou `x0 > largura + 30` → nada mais (`:355-357`).
3. **Caixa** cortada perto da tela: `esq = max(x0, −8)`, `dir = min(x1, largura + 8)`; `RRect(caixa, 1,5)`.
   Cor: tipo; escolhida/lote → `lerp(cor, texto, .16)`; oculta → α .35 (`:360-373`).
4. **Mídia** (se houver) recortada no RRect, transladada para `(x0, 2,5)`, tamanho `(x1−x0) × 29`:
   - **Vídeo → tira de miniaturas** (`FilmstripPainter`, `pintores_do_clipe.dart:200-281`): largura de cada miniatura
     = `29 · wQuadro/hQuadro` (proporção do 1º quadro); `n = ceil(w/tileW) + 1`; só os índices da janela
     (`i0..i1`, índice ABSOLUTO na barra — a tira não "anda" quando a janela troca); o quadro exibido no tile i é o do
     instante do ARQUIVO `f = a + (x/w)·span` com `a = start/duraçãoFonte`, `span = (end−start)/duraçãoFonte`;
     `drawImageRect` sem antialias, `FilterQuality.low`. Depois **véu preto α .38** por cima para o nome ser lido.
     A tira cobre o ARQUIVO inteiro (`ensureFilmstrip(path, sourceDuration ?? offset + span)`).
     Placeholder: **nenhum** — até a tira chegar, só a cor do corpo.
   - **Áudio → onda** (`ClipWaveformPainter`, `pintores_do_clipe.dart:306-565`): cor `texto` α .85; linha central
     branco α .10 1 dp; envelope espelhado (máximo pra cima, mínimo pra baixo) preenchido com cor α .55; miolo RMS
     com cor α 1; altura em dB: `h = clamp((20·log10(|a|·ganho) + 45)/45, 0, 1) · (29/2 − 1)`; mudo (volume ≤ 0 ou
     `muted`) → α .35; cada coluna segue o instante REAL do arquivo (`fonte` = n+1 instantes ao longo da barra:
     acompanha corte, velocidade, reverso, Time Remap). Vídeo NÃO desenha onda no Conjunto A (só a tira).
5. **Nome** (`:378-394`): `xNome = max(x0 + 14, 70 + 6)` — gruda logo depois do cabeçalho quando o início do clipe
   está escondido; só desenha se `x1 − xNome > 12`; y = `caixa.top + 1` (= 3,5) se a camada tem losangos, senão
   centralizado; recortado à caixa. Estilo 11 sp w600 `texto`. Travada: glifo `CupertinoIcons.lock_fill` + espaço
   antes do nome, mesmo estilo. (Obs.: `ellipsis: '…'` nunca atua porque o `layout()` é sem largura máxima; o nome é
   só cortado pelo clip.)
6. **Contorno** se escolhida/lote: `RRect(caixa.deflate(traço/2), 1,5)` stroke `traço` em `destaque`;
   `traço = 1,5` no lote, `2` na simples (`:396-410`).
7. **Alças de trim** se `comAlcas` (escolhida && !travada && !lote): início se `x0 ≥ 70`, fim se `x1 ≤ largura`.
   `RRect(desenhoDaAlca, raio 3)` em `destaque` + risco central 1,5 dp em `cromo` de `top+4` a `bottom−4`
   (`:412-421, 483-508`).
8. **Losangos** em `y = 23,5` (`:423-434`), ver 4.C.

Estados:

| estado | aparência (A) |
|---|---|
| normal | corpo na cor do tipo, sem contorno, sem fundo de linha |
| escolhida | fundo da linha `campo` α .55; corpo clareado 16% para `texto`; contorno 2 `destaque`; alças; losangos com "aceso/escolhido" |
| no lote (multi, `naMulti && lote`) | fundo da linha; corpo clareado; contorno **1,5**; SEM alças; losangos com aceso/escolhido; toque nos losangos desligado |
| oculta | corpo α .35; faixa do cabeçalho α .45; ícones do cabeçalho `textoSecundario`; miniatura α .45; olho `eye_slash` |
| travada | glifo de cadeado antes do nome; sem alças; arrastos recusados com aviso (snack 2400 ms + `lightImpact`) |
| em reordenação | véu `destaque` α .14 sobre a linha inteira (`linha_da_camada.dart:905-913`) |
| grupo | toque duplo entra no grupo (`enterGroup`) |

### 4.B Conjunto B — barra da antiga (`@aba36bb:2478-2584, 2669-2862`)

1. `_AmBarPainter`: `RRect(0,0,w,36, raio 8)` com `lerp(#151C24, cor, oculta .22 / escolhida .66 / normal .46)`;
   trilho de losangos `Rect(0, 36−15, w, 15)` preto α .22 (recortado no RRect).
2. `_ClipPreview` (dentro do RRect raio 8): vídeo com tira → tira em cima (altura inteira, ou `36 − alturaOnda` se há
   onda); gradiente preto α .62 → α .22 (0 → 45% da largura) por cima; onda (vídeo e áudio): faixa de baixo com altura
   `36` (áudio ou sem tira) ou `36·0,5` (vídeo com tira; `·0,58` se barra ≥ 56), fundo `#E60B0E12` quando há tira,
   cor `accent` com contorno `#B9FFF0` (vídeo) ou branco α .92 com contorno branco (áudio).
3. Conteúdo (Row, padding L14/R10/B15; L7/R3 se largura < 46): [compacto: `<`] · ícone do tipo 11 (se w > 28) ·
   cadeado 10 · nome 12 w600 (se w > 52) · ◇ 12 (se animada e w > 120) · contagem de grupo (w > 100) · Spacer ·
   [compacto: `>`] ou ≡ 14 (w > 150).
4. `_AmBarFrentePainter`: faixa 4 dp da cor do tipo (α .5 se oculta), fio branco α .10/.24 em y .5 de x 4 ao fim,
   contorno branco 1,5 se escolhida.
5. Largura mínima da barra 40 dp (`clamp(40, 1e6)`).
6. Legenda: cada cue vira retângulo branco α .35 raio 3, top 6, altura 36−12.

### 4.C Losangos (A) — `pintor_da_linha.dart:190-273`

- Forma: losango com vértices (x, y−r), (x+r, y), (x, y+r), (x−r, y); `r = 6 · escala`.
- Três níveis, pintados em 3 passadas (apagados → acesos → escolhidos, o importante por cima):

| nível | quando | preenchimento | contorno 1 dp | escala |
|---|---|---|---|---|
| 0 apagado | há propriedade ativa e o instante não é dela | `textoSecundario` α .45 | — | 1 |
| 1 aceso | sem propriedade ativa (`acesos == null`) ou instante da ativa | `keyframe` `#A9D3EC` | `cromo` | 1 |
| 2 escolhido | instante em `keyframesSelecionados` desta camada | `texto` `#F7F9FB` | `destaque` | **1,25** (r 7,5) |

- Em camadas NÃO escolhidas e fora do lote, todos os losangos são "acesos" e nenhum é "escolhido"
  (`linha_da_camada.dart:806-815`).
- Os losangos da linha da camada são os de **todos** os instantes com marca (`instantesDaCamada` = união de
  transformação, efeitos, máscaras, módulos; `keyframes_da_timeline.dart:99-101`), vindos do projeto GRAVADO
  (`editorControllerProvider`), não do projeto visível com edição pendente — "o losango não mente"
  (`linha_da_camada.dart:786-794`).
- Culling por busca binária no primeiro tempo ≥ `tempoDoX(−7) − início` e parada em `tempoDoX(largura+7)`.
- Não há agrupamento de losangos próximos no Conjunto A (no B, < 4 dp viravam pílula).

### 4.D Linha de propriedade (A) — `PintorDaTrilha` (`pintor_da_linha.dart:530-575`)

- Trilho horizontal em y = 17 de `max(x(início), −4)` a `min(x(fim), largura + 4)`: ativa → 1,5 dp `keyframe` α .6;
  inativa → 1 dp `textoSecundario` α .35.
- Losangos da trilha em y = 17: se OUTRA propriedade está ativa → todos apagados; senão todos acesos; escolhidos =
  seleção dessa `LayerProp` (efeitos não entram na seleção de keyframes).

---

## 5. GESTOS

### 5.1 Arquitetura da arena (A) — `timeline.dart:36-92`

Três camadas de reconhecedores; a arena do Flutter dá o ponteiro a quem ACEITA primeiro e, em empate no mesmo
evento, ao mais FUNDO (a linha recebe o movimento antes da raiz):

1. **Raiz** (`RawGestureDetector` opaco sobre a timeline inteira, `timeline.dart:462-490`):
   - `ScaleGestureRecognizer` com `touchSlop = slopDoAparelho / 2` ⇒ `panSlop = 2·touchSlop = slopDoAparelho`
     (igual à folga de um arrasto horizontal comum; no Android ≈ 8 dp, `ViewConfiguration`). Assim raiz e clipe
     aceitam no MESMO evento e o clipe (mais fundo) ganha.
   - `TapGestureRecognizer.onTapUp` = "toque no vazio" (só vence se ninguém mais pediu o toque).
   - Um `Listener` conta dedos e guarda o ponto do pouso.
   - Decisão de eixo UMA vez, pelo caminho pouso → ponto de aceite: `|dx| ≥ |dy|` → **scrub**; senão **rolagem**
     vertical; ≥ 2 ponteiros → **pinça**. O eixo não muda no meio do gesto.
2. **Zonas calculadas por linha** (`AreaDeToqueCalculada`, `area_de_toque.dart`): um `RenderProxyBox` cujo `hitTest`
   só retorna verdadeiro se `acerta(posição)` — clipe, alças e losangos "existem" para o toque exatamente onde o
   pintor os desenhou, com a vista de agora. Fora disso o toque atravessa para a raiz.
   Prioridade de hit (topo do `Stack` primeiro, `linha_da_camada.dart:885-1015`):
   **cabeçalho (x < 70) > losangos (metade de baixo) > alças de trim > corpo do clipe > raiz**.
3. **Cabeçalho** (70 px por cima da ponta esquerda): toque, toque longo, arrasto vertical.

Todas as zonas de linha usam `DragStartBehavior.down` (o arrasto parte do ponto do pouso, não do ponto de aceite) e
só têm arrasto HORIZONTAL: um arrasto vertical sobre o clipe não interessa a elas e vai para a raiz (rola a lista).

### 5.2 Constantes de tempo/distância

| constante | valor | fonte |
|---|---|---|
| slop de toque (arrasto, pan da raiz) | do aparelho (Android ≈ 8 dp; `kTouchSlop` = 18 fora do Android) | `MediaQuery.gestureSettings` |
| toque longo | **500 ms** parado; mexer além do slop antes disso = a raiz ganha | `kLongPressTimeout` |
| folga pós-aceite do toque longo (decidir eixo) | 8 dp | `linha_da_camada.dart:44` |
| toque duplo (só em clipe de GRUPO) | 300 ms (atrasa o toque simples só nesses clipes) | `linha_da_camada.dart:929-931` |
| velocidade mínima de fling | 50 dp/s (`kMinFlingVelocity`) | `timeline.dart:268,279` |
| ímã | 12 dp (clipe, alça); 8 dp (losango→cabeçote); 10 dp (scrub→marca) | `ima.dart:20,23`; `timeline.dart:321` |
| auto-rolagem | borda 38, 120 dp/s, intenção 4 | `tokens.dart:141-144`; `estado_da_timeline.dart:201` |
| 1 mutação por quadro | `scheduleFrameCallback` | `sessao_de_gesto.dart:43-54` |

### 5.3 Mapa completo de gestos (A)

| onde | gesto | condição | efeito | háptico |
|---|---|---|---|---|
| vazio (raiz) | toque | nada aceitou | limpa `keyframesSelecionados`; casca `_soltarSelecao`: se há painel aberto, fecha o painel; senão sai do modo Selecionar, limpa o lote e a seleção (`timeline.dart:342-347`; `editor_shell.dart:284-292`) | — |
| vazio / clipe não arrastável | arrasto horizontal | 1 dedo | **scrub** (pausa, segura vista, seek por passo, inércia, encaixe em marca) | — |
| qualquer lugar | arrasto vertical | 1 dedo, eixo vertical | rola a lista (fling Clamping) | — |
| qualquer lugar | 2 dedos | — | **pinça** ancorada + pan | `selectionClick` no início |
| corpo do clipe | toque | lote inativo | pausa; limpa lote; escolhe a camada; `aoTocarNaCamada` | `selectionClick` |
| corpo do clipe | toque | lote ativo (multi ≠ ∅ ou modo Selecionar) | soma/tira a camada do lote (`alternarNaSelecao`); se o lote fica ≥ 2 com painel aberto, fecha o painel | `selectionClick` |
| corpo do clipe (grupo) | toque duplo | camada é `GroupLayer` | `enterGroup(id)` | `selectionClick` |
| corpo do clipe | toque longo PARADO e soltar | — | se é a ÚNICA escolhida e lote inativo: nada (continua escolhida); senão soma/tira do lote | `mediumImpact` ao aceitar, `selectionClick` ao soltar |
| corpo do clipe | toque longo + arrastar | 1º movimento ≥ 8 dp: `|dx| > |dy|` → tempo, senão pilha | tempo: **mover** (igual ao arrasto do escolhido, inclusive lote e ímã; funciona mesmo em clipe NÃO escolhido); pilha: **reordenar** | `mediumImpact` |
| corpo do clipe | arrasto horizontal | escolhida OU do lote, e não travada (`moveClipe`, `linha_da_camada.dart:831`) | **mover no tempo** (5.4) | — |
| corpo do clipe | arrasto horizontal | NÃO escolhida e fora do lote | não tem tratador → a raiz faz **scrub** | — |
| alça de trim | arrasto horizontal | `comAlcas` (escolhida, destravada, fora do lote) | **aparar** início/fim (5.5) | `lightImpact` no início |
| alça (zona dentro do clipe) | toque | ponto dentro do corpo | trata como toque no clipe | `selectionClick` |
| losango (linha da camada, metade de baixo) | toque | lote inativo | escolhe a camada (se não era), pausa, `seek(início + t)`, **abre o editor de curva** da trilha (7.3) | `selectionClick` |
| losango (linha da camada) | toque longo | lote inativo | alterna na seleção de keyframes TODAS as marcas de propriedade daquele instante (se todas já estavam, tira todas; senão soma as que faltam) e faz `seek` | `mediumImpact` |
| losango (linha da camada) | arrasto horizontal | lote inativo, destravada, instante movível | **move o instante inteiro** (`moverKeyframe`) (5.6); se a marca é de módulo (forma/grade/lente/cena 3D) avisa "Este keyframe anima forma, grade, lente ou cena 3D e ainda não se arrasta" | `lightImpact` no início |
| losango (linha de PROPRIEDADE) | toque | — | alterna a marca na seleção (só `LayerProp`), torna a propriedade ativa, `seek` | `selectionClick` |
| losango (linha de propriedade) | toque longo | — | **abre o editor de curva** dessa propriedade/efeito | `mediumImpact` |
| losango (linha de propriedade) | arrasto horizontal | destravada | move SÓ a marca dessa propriedade (`moverKeyframeDaProp`); se há > 1 marcas escolhidas e esta é uma delas, move todas pelo mesmo delta (`moverKeyframes`); efeito → `moverKeyframeDoEfeito` | `lightImpact` |
| nome da propriedade (70 à esquerda) | toque | — | torna a propriedade ativa (`propriedadeAtivaProvider`) | `selectionClick` |
| cabeçalho | toque | camada escolhida, animada e lote inativo | abre/fecha as linhas de propriedade (`camadasExpandidasProvider`) | `selectionClick` |
| cabeçalho | toque | demais casos | mesmo que tocar no clipe (escolher / alternar no lote) | `selectionClick` |
| olho (44 à direita do cabeçalho) | toque | — | `toggleHidden(id)` | `lightImpact` |
| cabeçalho | toque longo + arrastar | — | **reordenar** (qualquer camada, inclusive não escolhida e em lote) | `mediumImpact` |
| cabeçalho | arrasto vertical direto | `reordenavel` = escolhida && destravada && !lote (miniatura vira ≡) | **reordenar** sem esperar o toque longo | `mediumImpact` |
| zona do cabeçote (100×38 no alto) | toque | — | `toggleMarker(playback.timeForInput())` (marca/desmarca com tolerância 120 ms) | `selectionClick` |
| zona do cabeçote | toque longo | — | folha "Marcas" (`menuDasMarcas`): Marcar aqui, editar a marca daqui, Ir para a próxima, Cortar em todas, … | `mediumImpact` |
| régua (fora das marcas) | toque longo | — | pausa + folha "Marcas" | `mediumImpact` |
| marca na régua (±11, y ≤ 20) | toque longo | — | menu da marca: Renomear, Cor da marca… (paleta `LayerLabel.palette`), Apagar — cada um `runAsOneUndo` | `mediumImpact` |
| marca na régua | arrasto horizontal | — | `moveMarker(de, para)` a cada passo (≥ 0), um `beginGesture/endGesture` | `lightImpact` |
| migalha de grupo | toque / toque longo | dentro de grupo | `exitGroup` / menu "Voltar a…" | — |

### 5.4 Mover clipe (A) — `linha_da_camada.dart:320-473`

1. `_comecarArrasto(mover)`: recusa se travada ("Camada bloqueada: desbloqueie para editar"). Monta o LOTE
   (`_loteDoArrasto`): se a camada está no multi → multi ∪ {principal}; senão só ela. Se QUALQUER uma do lote está
   travada → recusa inteiro com aviso "Há camada bloqueada na seleção: desbloqueie para mover". Guarda
   `inicio` de cada uma (µs) → `_grupo`.
2. `origemUs = início da arrastada`; `minimoUs = origemUs − min(inícios do lote)` (o lote para inteiro no zero:
   nenhuma distância encolhe).
3. `SessaoDeGesto` nova; `Ima.para(projeto, excluir: id, excluirTambem: lote)`; `fps` (≥ 1, senão 30);
   `deslocUs = origem − tempoDoX(xDoDedo)` (o item fica SOB o dedo no ponto em que foi pego);
   `segurarVista()`; keep-alive da linha.
4. Cada movimento (e cada tique da auto-rolagem com o dedo parado):
   ```
   desejado = naGradeDeQuadros(tempoDoX(xDedo) + deslocUs, fps)     // arredonda ao quadro mais perto; instante do quadro = ceil(q·1e6/fps)
   cabecote = round(vistaUs) ; tol = usPorPx(12)
   r = ima.encaixarIntervalo(desejado, duração, cabecote, tol)     // gruda o INÍCIO ou o FIM, o mais perto
   alvo = max(minimoUs, r.inicioUs) ; guia = r.guiaUs
   estado.guiaUs = guia ; háptico se guia nova
   sessao.pedir(() { if (início == alvo) return; abrir(); lote>1 ? moveLayers({id: ini + (alvo − origem)}) : moveLayer(id, alvo) })
   ```
5. Soltar: `sessao.encerrar()` (aplica o último pedido, fecha o passo de desfazer), limpa guia, para a auto-rolagem,
   `soltarVista()`, `setState` (tira os tratadores que só existiam pelo gesto).
6. Os tratadores de arrasto ficam vivos enquanto o gesto dura, mesmo se a seleção cair no meio
   (`movendo`/`aparando`, `linha_da_camada.dart:821-831, 950`) — ver bug 10.14.

### 5.5 Aparar (A)

- `_comecarTrim`: lado = `naAlca(p)` ou, se nulo, pelo meio do clipe. `origem` = fim (trimFim) ou início.
- Passo: `desejado` como acima; `g = ima.alvoPerto(desejado, cabecote, tol 12)`; `alvo = max(0, g ?? desejado)`;
  `trimLayerStart(id, alvo)` ou `trimLayerEnd(id, alvo)` via sessão (só se mudou).
- Regras do controlador: início ≥ 0 e ≤ fim − 100 ms, com remapeamento da animação (keyframes continuam no mesmo
  instante do projeto); fim ≥ início + 100 ms e ≤ o que sobra do arquivo (exceto Time Remap/reverso)
  (`editor_controller.dart:6070-6247`).

### 5.6 Arrastar losango (A) — `arrasto_de_losango.dart`

- Limites em quadros: `qMin = ceil(quadro(início) − 1e−3)`, `qMax = floor(quadro(início + duração) + 1e−3)`;
  com vizinha anterior `qMin ≥ ceil(quadro(início+antes) + 1 − 1e−3)`; com vizinha seguinte
  `qMax ≤ floor(quadro(início+depois) − 1 + 1e−3)` (nunca encosta na vizinha; nunca sai da camada).
- `deslocUs = início + origem − tempoDoX(xDedo)`.
- Passo: `desejado = tempoDoX(x) + deslocUs`; se `|desejado − vistaUs| ≤ usPorPx(8)` gruda no cabeçote;
  `quadro = clamp(round(alvo·fps/1e6), qMin, qMax)`; `global = ceil(quadro·1e6/fps)`; guia = global se grudou;
  `sessao.pedir(mover(atual, global − início))`; se moveu, `atualUs = para` e `aoMover(de, para)` (a seleção de
  keyframes acompanha a marca, `linha_da_camada.dart:634-650`).
- A vista fica PARADA durante o arrasto (senão a marca fugiria do dedo); o cabeçote NÃO acompanha a marca no A
  (no B, o playback fazia `seek` para a marca a cada passo com a régua congelada).
- Recusas: travada ("Camada bloqueada: desbloqueie para mover o keyframe"); módulo (aviso); outros motivos calados.

### 5.7 Ímã e guias (A) — `ima.dart`, `guias.dart`

- Alvos (reunidos UMA vez no início do gesto, ordenados para busca binária): `0`; início e fim de toda outra camada
  (exceto a arrastada e o lote); todos os instantes de keyframe das outras camadas; marcas; batidas. O **cabeçote**
  (vista atual) disputa junto em cada passo.
- `alvoPerto(us, cabecote, tol)`: considera o cabeçote e os dois vizinhos da busca binária; vence o mais perto com
  `d ≤ tol` (empate → o último considerado).
- `encaixarIntervalo`: testa início e fim; vence o de menor distância (início em empate).
- Háptico `selectionClick` UMA vez por encaixe novo (`HapticoDoIma`).
- **Guia**: fio vertical 1 dp `destaque`, da régua (y 42) até a base, em `xDoTempo(guiaUs)`, só se `70 ≤ x ≤ largura`
  (`guias.dart:49-62`).
- **Traço de destino do reordenar**: retângulo `(0, y−1, largura, 2)` `destaque` + círculo r 3 em (4, y), com
  `y = destino − offsetDaLista` (`guias.dart:63-76`).
- Preferência `magneticProvider` (`ima.dart:147-165`, chave `timeline.ima`, padrão ligado): na timeline nova só
  controla o encaixe do scrub em marcas (e, no controlador, se apagar fecha o buraco). **O ímã dos arrastos é sempre
  ativo** (não consulta a preferência).

### 5.8 Reordenar (A) — `reordenar.dart`

- A ordem das linhas é a ordem do palco: linha de cima = índice 0 = camada da FRENTE.
- `comecar(id, dedoGlobal, índice)`: recusa travada (`lightImpact`); `emReordenacao = id`; `mediumImpact`.
- `mover`: `local = globalToLocal(dedo)` na lista; `i = clamp(floor((local.y + offset)/34), 0, n−1)`;
  `destino = linhas[i].indiceDaCamada` (linha de propriedade conta como a camada dona); a cada destino novo
  `selectionClick`; traço: acima da 1ª linha do destino quando sobe, abaixo da ÚLTIMA linha dele (propriedades
  abertas inclusive) quando desce; nulo se destino = origem. Auto-rolagem vertical.
- A linha segurada NÃO muda de lugar durante o gesto (só véu + traço). Soltar: `runAsOneUndo(reorderLayer(id,
  destino − origem))` + `lightImpact`. Cancelamento do sistema → não aplica.

### 5.9 Seleção múltipla (lote)

- **Entrar**: (a) toque longo PARADO num clipe/cabeçalho de outra camada (a escolhida entra junto:
  `alternarNaSelecao`), ou (b) modo Selecionar (`modoSelecionarProvider`, ligado por botão da barra).
- **Invariantes** (`editor_controller.dart:296-313`): `multi` tem ≥ 2 ids ou é vazio; a principal
  (`selectedLayerProvider`) fica dentro do conjunto; com 1 só, volta a seleção simples.
- **Com o lote ativo**: toque no clipe/cabeçalho soma/tira; losangos só desenham (o toque é da camada); sem alças de
  trim; a barra da base vira `BarraDoLote` (dividir, aparar início/fim, estender, mover ao cabeçote, alinhar
  inícios/fins, distribuir, copiar/colar, colar efeitos, ocultar, travar, subir/descer, agrupar máscara/recorte,
  selecionar todas, duplicar — cada ação um `runAsOneUndo`, `application/operacoes_do_lote.dart`).
- **Mover o lote**: arrastar QUALQUER clipe do lote move todos pelo mesmo delta, numa mutação só (`moveLayers`),
  parando inteiro no zero; lote com travada não anda.
- **Aparar o lote pela timeline**: não existe (alças só na seleção simples); o lote apara pela barra.
- **Sair**: toque no vazio (sai do lote e do modo), ou tirar até sobrar 1.

### 5.10 Háptico → Android

| Flutter | uso | Compose / View |
|---|---|---|
| `selectionClick` | toque em clipe/cabeçalho/losango, encaixe do ímã, degrau do reordenar, início da pinça, marcar no cabeçote | `HapticFeedbackConstants.CLOCK_TICK` (API 34+: `SEGMENT_TICK`) |
| `lightImpact` | olho, início de trim/arrasto de losango/marca, soltar reordenar, avisos | `VIRTUAL_KEY` / `KEYBOARD_TAP` |
| `mediumImpact` | toque longo aceito (clipe, losango, cabeçalho, régua, cabeçote, marca), início do reordenar | `LONG_PRESS` (`HapticFeedbackType.LongPress`) |

### 5.11 Gestos do Conjunto B (para referência do comportamento antigo)

- Horizontal = rolagem nativa (`SingleChildScrollView` bouncing); cada `ScrollUpdate` com dedo → `seek`.
- Toque na barra: compacto → sai do painel; modo Selecionar → alterna; senão escolhe (tocar na já escolhida abre as
  ferramentas).
- Mover no tempo SÓ por toque longo + arrastar horizontal; toque longo + vertical reordena UM degrau por meia linha
  a cada passo (mutando ao vivo); toque longo parado alterna no lote.
- Trim pelas alças brancas internas (acumulando `delta.dx`), ímã 12 dp.
- Losango: toque = seek + easing (aceso) ou "keyframe de outra propriedade" (apagado); toque LONGO + arrastar move.
- Régua: toque duplo cria/remove marca no ponto tocado; toque longo = menu de marcas; relógio: toque marca no
  cabeçote, toque longo = menu.
- Quadradinho do cabeçalho: toque escolhe; toque longo trava/destrava.

---

## 6. CABEÇALHO DA CAMADA

### 6.A Conjunto A — `cabecalho_da_camada.dart` (70 × 34, fixo à esquerda da linha)

```
x: 0 ............. 10 ............ 42 ................. 70
   [faixa de cor 10][ miniatura/ícone 16 (slot 32) ][ olho: toque 44 (x 26..70) ]
fundo do slot inteiro: cromo; pílula: y 1..33 (padding vertical 1), cor `elevado` (#1B2530) ou `campoAlto` (#323D49)
se escolhida/no lote; cantos: esquerda RETA, direita raio 100 (meia-altura).
```

| peça | geometria | aparência | toque |
|---|---|---|---|
| fundo | 70 × 34 `cromo` | — | — |
| pílula | 70 × 32 em y 1 | `elevado` / `campoAlto` | toque = `aoTocar`; toque longo = reordenar |
| faixa de cor | x 0–10, y 1–31 dentro da pílula (2–32 na linha) | cor da etiqueta (`meta.label.color`) ou do tipo; α .45 se oculta | — |
| miniatura | slot x 10–42; padding (2,3,14,3) → conteúdo 16 × 26 em x 12–28 | ≡ `line_horizontal_3` 16 `texto` se `reordenavel`; senão 1º quadro da tira do vídeo (`BoxFit.cover`, raio 1,5, α .45 se oculta); senão ícone do tipo 16 (`texto`, ou `textoSecundario` se oculta) | — |
| seta de expandir | left 29, bottom 1 (dentro da pílula), 8 dp | `chevron_right` / `chevron_down` em `keyframe`; só se `temAnimacao` | ignora toque |
| olho | x 26–70 (44), altura inteira; padding (15,5,9,5) → ícone 16 em x ≈ 41–57 | `eye` / `eye_slash`, `texto` ou `textoSecundario` (oculta) | toque = `toggleHidden` (+ `lightImpact`); semântica "Ocultar camada"/"Mostrar camada" |

- Não há cadeado no cabeçalho do A (travada aparece só como glifo no nome do clipe). Travar/destravar é pela barra.
- `Listener.onPointerCancel` = rede de segurança que cancela o reordenar se o sistema tira o ponteiro.
- Linha de propriedade: no lugar do cabeçalho, caixa 70 `cromo` com padding L 14 / R 4 e o rótulo 10 sp (w700
  `destaque` se ativa; w500 `textoSecundario` senão), 1 linha, reticências (`linha_da_propriedade.dart:286-322`).
  Rótulos: Posição, Escala, Rotação, Opacidade, Inclinação, Pivô, nome do efeito.

### 6.B Conjunto B — pílula flutuante 58 × 28 (ver 1.B)

Olho 26 (ícone 16 `white70`) + quadradinho 18 raio 4 (`#FFE899` ou etiqueta), `spaceEvenly`, `#1E222D` raio 14,
margem esquerda 4, centralizada na linha de 46. Toque no olho = ocultar (todos os pedaços empacotados); toque no
quadradinho = escolher; toque longo no quadradinho = travar. Coluna de 66 com gradiente para o clipe passar por baixo.

### 6.C `core/ds/aurea_layer_row.dart` (lista de camadas fora da timeline)

Item de lista 37 dp (vincular/agrupar/escolher fonte de máscara): recuo `nível·12` · barra de cor 4 × 25 raio 1,5
(cor do tipo ou `campoAlto`) · 8 · slot 32 × 29 (miniatura ou ícone 20) · 8 · nome 13 sp (`AureaEstilos.corpo`,
`Text` e não `AppText` — conteúdo do usuário) · olho 44 × 37 (ícone 18, `textoSecundario`; α .5 se invisível).
Selecionada: fundo `selecao` (#123A63), sem contorno. Invisível: texto `textoSecundario` α .6. `Tocavel` com
`encolhe: 1` (sem efeito de encolher).

---

## 7. LINHAS DE PROPRIEDADE, KEYFRAMES E EDITOR DE CURVA

### 7.1 Expandir uma camada

- Estado de tela `camadasExpandidasProvider: Set<String>` (não vai para o arquivo, `linhas.dart:9-11`).
- Toque no cabeçalho da camada JÁ escolhida, animada, fora do lote → alterna o id no conjunto.
- `EstruturaDaTimeline.de(projeto, abertas)` (`linhas.dart:60-71`): para cada camada (ordem do projeto) uma linha
  `camada`; se aberta, uma linha `trilha` por `TrilhaAnimada`, na ordem: Posição, Escala, Rotação, Opacidade,
  Inclinação, Pivô (só as que têm marca) e depois um por efeito não-interno com marca
  (`keyframes_da_timeline.dart:63-70,108-126`). Máscaras e módulos NÃO ganham linha (ver bug 10.11).
- Chaves: `linha-<id>` e `trilha-<id>-<prop.name | efeito-<effectId>>`; comparação por valor (a lista só se refaz
  quando uma linha entra/sai/troca de lugar).
- Painel fechou → `propriedadeAtivaProvider = null` (tudo aceso de novo) (`timeline.dart:437-446`).

### 7.2 Navegação de keyframes (anterior / próximo / adicionar)

- A timeline NÃO tem botões ‹ ◆ ›; eles vivem nas linhas de propriedade dos painéis (`AureaKeyframeButton`, 64 dp =
  18 + 28 + 18) e no rodapé do editor de curva.
- `marcasVizinhas(marcasUs, agoraUs)` (`core/ds/aurea_keyframe_button.dart:121-136`): anterior = maior marca < agora,
  próxima = menor ≥ agora, ignorando marcas a < 8 ms (`toleranciaDaMarcaUs = 8000`) do instante atual.
- `irParaMarcaVizinha` (`navegacao_de_keyframes.dart:44-61`): pausa e `seek(início + alvoLocal)`; nada se não há.
  `setasDaTrilha` devolve callbacks nulos para a seta sem destino (o botão desenha desabilitada).
- Relógio da trilha: tempo local da camada (com Posterize) ou "cru" (`global − início`) para Time Remap.
- Adicionar keyframe: é o ◆ do painel (keyframe explícito, `docs/keyframe-explicito.md`); a timeline só mostra.

### 7.3 Editor de curva — `curva/editor_de_curva.dart`, `grafico_da_curva.dart`, `trilha_da_curva.dart`

**Abertura** (`abrirEditorDeCurva`, `editor_de_curva.dart:76-103`): folha NÃO modal (sem véu), altura
`painelGrande − 10 = 316` (+10 de respiro da folha = 326 = transporte + timeline), palco inteiro à vista. Portas:
toque no losango da linha da camada; toque longo no losango da linha de propriedade; `AureaKeyframeButton.onCurve`.
Trilha escolhida no toque do losango da camada (`linha_da_camada.dart:540-563`): a propriedade ativa se ela tem
marca ali; senão o efeito ativo; senão a 1ª trilha animada com marca naquele instante (ordem do painel).

**Layout** (`AureaPanel`):
```
[ Curva · <Propriedade> ........ (Valor) (Velocidade) [⋯ 44×38] [✓] ]   cabeçalho 38
[ Linear  Ease  Ease In  Ease Out  Ease In-Out  Bezier  Hold ]          faixa 36 (chips 28, pad h10 v4, vão 6), rola na horizontal
[ gráfico: Expanded, padding h6, ClipRRect raio 4, fundo cromo, opacidade .45 (100 ms) se o cabeçote está fora do trecho ]
[ 6 [‹ 44×44] "Ease In-Out · Trecho 1 de 3" (12 w600) / "Alça 1 (0,42; 0)   Alça 2 (0,58; 1)" (10, tabular) [› 44×44] (Selecionados (n)) 6 ]  rodapé 50
```
Chips (`AureaChip`): altura 28, padding h 12, raio pílula, 12 sp. Setas: ícone 16 `keyframe`, desabilitada
`textoSecundario` α .3. Aviso sem trecho: "Crie dois keyframes nesta propriedade para editar a curva." (< 2 marcas)
ou "Leve o cabeçote para entre dois keyframes para ver a curva.".

**Presets** (`presetsDoEditorDeCurva`): Linear, Ease (`Easing.appleStandard` = cubic 0,25 0,1 0,25 1), Ease In,
Ease Out, Ease In-Out, Bezier (personalizada), Hold — todos do `CatalogoDeCurvas`; chip aceso =
`CatalogoDeCurvas.aceso(preset, curva)`; toque = `runAsOneUndo(gravar(aoEscolher(preset, curva)))`.

**Menu ⋯**: Copiar curva · Colar curva (se há algo no `EasingClipboard` global em memória) · Aplicar em todos os
trechos · Inverter curva (se inversível) · Overshoot (toggle local) · Mais curvas… (famílias além dos básicos:
quique, elástico, degraus, mola…) · [só transformação] Loop: nenhum / repetir / vai e volta (`setPropertyLoop`).

**Trecho**: `trechoEm(marcas, agora)` (`trilha_da_curva.dart:388-404`): N marcas = N−1 trechos; em cima de uma marca
= o trecho que SAI dela; em cima da última = o que chega nela; fora = nulo; folga 8 ms. O editor SEGUE o cabeçote
para o trecho em que ele entra (exceto com alça na mão); se o cabeçote sai de todos, mantém o último, esmaecido.
Ao abrir, quem escolhe é o instante do losango tocado.

**Gráfico** (`GraficoDaCurva`): dois modos — VALOR (progresso × valor) e VELOCIDADE (derivada, teto 4×).
- Margem 15 (`margemDoGrafico`); alvo de toque da alça **raio 30**; desenho da alça raio **12**
  (`raioDoControleDaCurva`); curva traço **3** `destaque`, pontas redondas; bezier exata em modo valor, senão 120
  amostras.
- Grade: passo em [0,05; 0,1; 0,25; 0,5; 1; 2; 5; 10] (senão 20) com ≥ 28 dp entre linhas; fina `textoSecundario`
  α .14 0,5 dp; forte em 0 e 1 α .42 1 dp.
- Tangentes `texto` α .55 1 dp; âncoras (0,0) e (1,1) círculo r 4,5 `destaque` (modo valor); alças: círculo r 12
  `texto` (bezier) ou `keyframe` (famílias paramétricas); agarrada: anel `destaque` 2,5 dp.
- Cabeçote no trecho: linha vertical `cabecote` α .45 1 dp + ponto r 6 `cabecote` (escuta `percorrido`, não
  reconstrói).
- Bolinha de prévia enquanto arrasta: r 7 `keyframe` + contorno `palco` 1,5, percorre a curva em loop de 1100 ms.
- Rótulos 10 sp `textoSecundario` tabular: valor do início em (esq+4, y(0)−14), do fim em (esq+4, y(1)+2); modo
  velocidade "1×"; tempos do trecho em baixo à esquerda/direita (y = altura − 13), formato `"1,5 s"`.
- Enquadrar: 48 amostras + alças; folga 8% da faixa; overshoot abre −0,5…1,5; velocidade: topo ≥ 1,5, ±4.

**Gestos do gráfico** (tudo por `Listener` cru, sem arena):
- 1 dedo numa alca (≤ 30 dp) → arrasto SEM folga, RELATIVO (a alça anda o que o dedo andou; não pula para baixo do
  dedo); vista congelada durante o arrasto; `beginGesture` no 1º movimento, `endGesture` ao soltar.
- Ímã 8 dp: bezier/valor → x e y em 0 ou 1, e diagonal y = x (elipse com os dois alcances); velocidade → v em 0 ou
  1 e influência em 1/3 (saída) ou 2/3 (chegada). `lightImpact` só na CHEGADA ao ímã; guias tracejadas (4 on/4 off)
  `destaque` α .6 na linha em que grudou.
- Limites: valor x ∈ [0,1], y ∈ [min(0, y0), max(1, y0)] ou [−1,5; 2,5] com overshoot (uma alça que já estava fora
  não é puxada para dentro); velocidade x ∈ [0,02; 0,98], v ∈ [0, 4], `x1 ≤ x2 − 0,02`.
- Famílias paramétricas: alça A (horizontal) define `count` (1…12) — `n = round(fatorA / x)`, fator 0,75 elástico /
  0,5 quique / 1 demais; alça B (vertical) define força (`elastic: (y−1)/0,45`, `bounce: (1−y)/0,6`, clamp .05…1)
  ou suavidade (`cyclic`).
- 2 dedos → pinça (zoom igual nos dois eixos, largura da janela entre 0,02 e 20 unidades) + pan ancorado no foco;
  o 2º dedo larga a alça onde está.
- Toque duplo → reenquadra.
- Leituras: bezier `Alça 1 (x1; y1)   Alça 2 (x2; y2)`; velocidade `Saída {v}× · {x1%}%   Chegada {v}× · {(1−x2)%}%`;
  hold "Segura o valor até o próximo keyframe."; quique/elástico `Repetições n · Força f`; cíclico
  `Repetições n · Suavidade s`; degraus `Degraus n`. Números: até 2 casas, vírgula, sem zeros sobrando, "—" se não
  finito (`numeroDaCurva`).
- "Selecionados (n)" (só trilhas de transformação): também grava o trecho que sai de cada marca selecionada
  (`setSegmentEase`).

**Trilhas suportadas** (`TrilhaDaCurva`): transformação (grava `setSegmentEase(layer, prop, inícioLocal, easing)` em
todas as sub-trilhas do grupo; "todos" = `applyEaseToAllSegments`), efeito (`setEffectSegmentEase`,
`applyEaseToAllEffectSegments`), Time Remap (efeito interno `timeRemap.tempo` ou `GroupLayer.timeRemap` via
`updatePrecomp`, relógio cru), nó de cena 3D, câmera 3D, caractere de Texto 3D (curva NÃO persiste no arquivo —
`trilha_da_curva.dart:296-298`), trilha numérica genérica.

### 7.4 `core/ui/am_tick_ruler.dart` (régua de VALOR das linhas de propriedade, não a da timeline)

- Riscos a cada **9 px** (`passoDosRiscos`), 1 forte a cada **5** (padrão só se repete a cada 45 px; evita o
  efeito roda-de-carroça até 1350 px/s). Fraco `#43516A` 1,6; forte `#7485A3` 2. Indicador central fixo 3 dp
  (`accent` ou branco), de `pad·0,4` a `h − pad·0,4`, `pad = h·0,18`; fracos de `pad` a `h−pad`, fortes de `pad·0,55`.
- **Direita AUMENTA** (regra do dono, coberta por `test/sentido_dos_controles_test.dart`):
  `valor = valorNoToque + deslocamentoAcumulado · unitsPerPixel` (padrão 0,5), riscos andam junto com o dedo
  (`base = valor/porPixel + origem`, índice absoluto).
- Leitura de posição (min e max finitos): trilho 3 dp na base `muted` α .18 + preenchimento `accent` (ou cabeçote se
  inativa) crescendo da origem (0 quando a faixa cruza o zero).
- `AmArrastoDeValor`: superfície de arrasto maior que a régua (a linha inteira puxa); UMA entrega por quadro (o 1º
  evento sai na hora, os seguintes guardados e o último entregue após o quadro); `onStart/onEnd` para um passo de
  desfazer.
- No t2 a régua de valor ainda NÃO tinha o risco forte (ele entrou em `9df3beb`, 20/09 19:58): riscos iguais
  `#3A424C`-ish a 9 dp, indicador branco ~2 dp.

---

## 8. MODELO DE ESTADO

### 8.1 O que a timeline guarda localmente (A)

| estado | onde | natureza |
|---|---|---|
| `vistaUs` (double) | `EstadoDaTimeline` | instante sob o cabeçote; segue o relógio exceto com gesto segurando (`_seguram` contador) |
| `pps` | `EstadoDaTimeline` | zoom (não persiste; volta a 100 a cada montagem) |
| `guiaUs` | `EstadoDaTimeline` | fio do ímã (transitório) |
| `destinoDoReordenar`, `emReordenacao` | `EstadoDaTimeline` | prévia do reordenar |
| `largura` | `EstadoDaTimeline` | medida no build (`timeline.dart:456`) |
| `AutoRolagem` (ticker, direções) | `EstadoDaTimeline` | |
| modo do gesto da raiz, dedos, inércia | `TimelineDoEditorState` | |
| offset vertical | `ScrollController _rolagem` | |
| memo de widgets por chave, índice da chave | `TimelineDoEditorState` | cache |
| sessão de arrasto (tipo, lote snapshot, deslocUs, ímã, origem, mínimo) | `_LinhaDaCamadaState` | transitório |
| `ArrastoDeLosango` (atual, limites, desloc) | linha | transitório |
| `_ToqueDasMarcasState._arrastando` | régua | transitório |
| gráfico: vista do usuário/arrasto, dedos, alça agarrada, ímã, prévia | `GraficoDaCurvaState` | transitório |
| editor de curva: trecho mostrado, fora, modo, overshoot, "selecionados" | `EditorDeCurvaState` | transitório |

### 8.2 O que vem de fora

| fonte | conteúdo |
|---|---|
| `editorControllerProvider` (projeto real) | camadas (início, duração, tipo, nome, keyframes), meta (hidden, locked, label), marcas, batidas, fps, duração; undo/redo; operações |
| `projetoVisivelProvider` | projeto real OU o derivado de uma edição pendente (valor mexido fora de marca); a timeline desenha posição/nome dele e losangos do real |
| `PlaybackController` | `time` (grade de quadros), `playing`, `seek`, `pause`, `durationOf`, `timeForInput` |
| Riverpod de UI | `selectedLayerProvider`, `multiSelectProvider`, `modoSelecionarProvider`, `keyframesSelecionadosProvider` (`{layerId, prop, tempo}`), `camadasExpandidasProvider`, `propriedadeAtivaProvider`, `painelAbertoProvider`, `magneticProvider` (pref `timeline.ima`), `editorSessionProvider` (in/out) |
| `MediaPreviewService` (singleton) | tira (`stripOf`), pirâmide de picos (`pyramidOf`), ganho de exibição, `revision` global |

### 8.3 Duplicações de fonte de verdade (EVITAR no port)

1. **Dois "agoras"**: `vistaUs` (double, da timeline) × `playback.time` (grade de quadros). A timeline faz seek a
   cada passo e depois "se acomoda" ao quadro. No port: o tempo do motor é a verdade; a timeline mantém só um
   *deslocamento de prévia* durante o gesto (ou um `scrub` do motor: `scrubBegin/scrub/scrubEnd` já existem em
   `CommandBatch.kt:423-430`).
2. **Seleção espalhada em 3–4 providers** (principal, multi, modo Selecionar, keyframes selecionados) com invariante
   manual (principal ∈ multi; multi ≥ 2). No port: um `SelectionState` único e imutável.
3. **Keyframes selecionados identificados por TEMPO** (`{layerId, prop, tempo}`): quando a marca anda o código
   precisa reescrever a seleção à mão (`_selecaoAcompanha`, `linha_da_camada.dart:634-650`). No port: ids de
   keyframe do motor.
4. **Projeto real × visível** (edição pendente): duas árvores de projeto. No port: o motor expõe o estado pendente
   de forma explícita (um overlay), não uma segunda cópia do projeto.
5. **`camadasExpandidas` nunca é podado** (ids de camadas apagadas ficam; camada que perdeu a animação continua no
   conjunto).
6. **`largura` escrita durante o build** e lida por pintores/geometria.
7. **Auto-rolagem e pinça fazem `seek`** — mexem no tempo do projeto como efeito colateral de gestos de vista.
8. **Ímã: preferência duplicada** (StateProvider lido do SharedPreferences no nascimento e regravado à mão).
9. **Pedido de mídia dentro do build** (`_pedirMidia`, timer disparado no build).

---

## 9. TRUQUES DE DESEMPENHO (replicar)

1. **O relógio não recompõe nada**: pintores escutam `vista = merge(vistaUs, pps)` via `repaint:`; só o selo escuta
   `playback.time`. Cada pintor em `RepaintBoundary` (linha, régua, guias, selo). → Compose: ler o estado da vista
   SÓ na fase de desenho (`Modifier.drawBehind { state.viewUs }` / `drawWithCache`), nunca na composição.
2. **Lista virtual** com `itemExtent` fixo 34 e cache de 2 linhas; `findChildIndexCallback` + `ValueKey` mantém o
   `State` (e o reconhecedor do dedo) através de reordenações; `AutomaticKeepAlive` só enquanto há gesto na linha
   (a auto-rolagem pode tirar a linha da tela no meio do arrasto). → `LazyColumn(key = chave)` + manter o gesto fora
   do item (no pai) ou `rememberSaveable`/estado de gesto hoisted.
3. **Memo de linhas por chave** (`_memo`): o pai devolve o MESMO widget e a linha só reconstrói pelos próprios
   `select` (camada por id, meta como record, booleanos de seleção).
4. **Estrutura comparada por valor**: a raiz só observa a lista de linhas.
5. **`Expando` por instância imutável de `Layer`** para instantes de keyframe e trilhas animadas (evita recalcular
   `keyframeTimes`, que junta 9 conjuntos e aloca um `Duration` por marca).
6. **Buscas binárias**: primeiro losango visível, losango sob o dedo, alvos do ímã, batidas.
7. **Régua em 3 `Path`** (3 draw calls) + cache de `TextPainter` dos rótulos (≤ 160).
8. **Janela quantizada** (balde 256 px, folga 96) para tira e onda — trocas raras, rolar = transladar.
9. **Onda gravada em `Picture` com cache LRU de 32 entradas** (chave = record de tudo que muda o desenho; recorte
   local em baldes de 256 colunas: arrastar o clipe não refaz a gravação). → Compose: `android.graphics.Picture` /
   `RenderNode` por clipe, ou `ImageBitmap` em cache.
10. **Uma mutação por quadro** (`SessaoDeGesto.pedir` + `scheduleFrameCallback`), alvo ABSOLUTO a partir da origem do
    gesto (descartar eventos intermediários de 120/240 Hz não perde nada). → `withFrameNanos` / coalescer no
    `CommandBatch`.
11. **`Interacao.marcar()/soltar()`**: o palco desenha em rascunho enquanto o dedo mexe.
12. **Mídia auxiliar adiada**: tira/onda pedidas 900 ms depois, só com a UI ociosa e nunca durante o play (re-tenta a
    cada 500 ms).
13. **Sondas de teste** (`SondaDaTimeline.buildsDeLinha/buildsDaTimeline`): o teste
    `timeline_test.dart:614` prova que o tique do relógio não reconstrói linhas; `:647` que mover um clipe reconstrói
    só a linha dele; `:666` que com 100 camadas só as da tela montam. Replicar como testes de recomposição
    (`Recomposer`/contadores) no Compose.

---

## 10. BUGS, HACKS E RISCOS (file:linha)

| # | arquivo:linha | descrição |
|---|---|---|
| 10.1 | `linha_da_camada.dart:492-536` × `linha_da_propriedade.dart:92-126` | **Gestos de losango INVERTIDOS entre linhas**: na linha da camada toque = abre curva, toque longo = seleciona; na linha de propriedade toque = seleciona, toque longo = abre curva. Decidir um só contrato no port. |
| 10.2 | `geometria.dart:59-81` + `linha_da_camada.dart:978-999` | **Losango em t = 0 (ou no fim) da camada rouba a alça de trim**: a zona do losango (±10, metade de baixo) sobrepõe a zona da alça (30 fora do clipe) e tem prioridade de hit; arrastar pela metade de baixo logo à esquerda do clipe move o keyframe em vez de aparar. Keyframe no instante 0 é o caso mais comum. |
| 10.3 | `linha_da_camada.dart:978-999` | Na metade de baixo, um losango ganha do corpo: arrastar o clipe escolhido pegando num losango move o keyframe. Intencional, mas surpreende. |
| 10.4 | `linha_da_camada.dart:233-251, 342-375` | Toque longo + arrasto horizontal move um clipe **NÃO escolhido** sem escolhê-lo (sem contorno/alças durante o arrasto), contrariando "clipe não escolhido não arrasta" (`timeline.dart:65`). |
| 10.5 | `linha_da_camada.dart:879-882` | Com lote ativo, toque longo no cabeçalho reordena só UMA camada (o lote não anda junto); `reorderLayers` (bloco) existe no controlador (`editor_controller.dart:5974`) e não é usado. |
| 10.6 | `keyframes_da_timeline.dart:108-126` | `trilhasAnimadas` ignora máscaras e módulos: camada animada só por máscara mostra losangos, mas não tem seta de expandir nem linhas de propriedade; o toque no losango pode não achar trilha e não abrir a curva (`linha_da_camada.dart:505-506`). |
| 10.7 | `estado_da_timeline.dart:128-137` | `irPara` limita a vista a [0, duração]: auto-rolagem não passa do fim do projeto (arrastar um clipe para além do fim depende só do dedo) e a pinça perde a âncora perto das pontas. |
| 10.8 | `timeline.dart:249-256` → `estado_da_timeline.dart:128-137` | A pinça faz `aoScrub` + `seek` a cada evento (decodificação durante o zoom, tempo do projeto muda). |
| 10.9 | `estado_da_timeline.dart:209-216, 256-266` | Auto-rolagem com velocidade constante 120 dp/s (sem aceleração pela profundidade na borda); a 100 dp/s de zoom isso é 1,2 s de tempo por segundo — lento em projetos longos. |
| 10.10 | `timeline.dart:315-333` | Encaixe do fim do scrub só em MARCAS (não em keyframes, bordas de clipe, in/out). |
| 10.11 | `linha_da_camada.dart:405-425` | No mover do lote, só as bordas do clipe arrastado grudam; as bordas das outras do lote não. |
| 10.12 | `ima.dart:147-165` | A preferência "ímã" não desliga o ímã dos arrastos (só o encaixe do scrub). |
| 10.13 | `estado_da_timeline.dart:103-123` | Contador `_seguram` frágil: qualquer caminho que segure sem soltar para a vista de seguir o relógio ("o keyframe nasce no tempo certo e aparece longe do cabeçote" — bug real corrigido em `75b0426`). No port: dono único do gesto, com cancelamento garantido. |
| 10.14 | `linha_da_camada.dart:821-831, 950` | HACK: os tratadores de arrasto ficam registrados enquanto `_arrasto != null` mesmo que a condição (escolhida) caia, porque remover o tratador mataria o reconhecedor sem `onEnd`. |
| 10.15 | `timeline.dart:458-481` | A resolução raiz × linha depende do desempate da arena do Flutter ("o mais fundo ganha no mesmo evento") e de slop dividido por 2 — no Compose isso tem de ser explícito (5.1 / 11.3). |
| 10.16 | `cabecote.dart:78-96` | Zona de toque do cabeçote (100 × 38, `translucent`, top 5 → 43) invade 1 dp a 1ª linha e compete na arena com as marcas da régua perto do centro (toque longo numa marca a ±50 dp do cabeçote disputa com o menu de marcas). |
| 10.17 | `linha_da_camada.dart:833, 684-697` | Efeito colateral no build: `_pedirMidia` agenda `Timer` durante o build. |
| 10.18 | `linha_da_camada.dart:893-897, 1006-1010` | `MediaPreviewService.revision` é GLOBAL: qualquer mídia que fica pronta refaz o pintor/cabeçalho de TODAS as linhas de vídeo/áudio. |
| 10.19 | `pintor_da_linha.dart:293-298` | Parâmetro `revisaoDaMidia` nunca é passado (código morto). |
| 10.20 | `pintor_da_linha.dart:321-340`; `regua.dart:217-222, 290-306` | `TextPainter` criados sem `dispose` (nome do clipe, pontas por pintura, cache de rótulos limpo sem descartar). |
| 10.21 | `pintor_da_linha.dart:323-340` | `ellipsis: '…'` sem `maxWidth` no `layout()` — o nome nunca ganha reticências, só é cortado. |
| 10.22 | `regua.dart:457-463` | `textoDoTempo` sem horas: ≥ 60 min vira `60:00:00`, `75:12:03`… (a régua usa `h:mm:ss`, o selo não). |
| 10.23 | `regua.dart:366-375` | Riscos de quadro assumem fps inteiro; o pulo do quadro do meio-segundo só funciona com fps par. |
| 10.24 | `timeline.dart:456` | `estado.largura` escrito no build (layout) e lido por pintores/geometria; um quadro de atraso se a largura mudar sem rebuild. |
| 10.25 | `cabecalho_da_camada.dart:119-124, 170-174` | Olho (x 26–70) e imagem da miniatura (x 12–28) se sobrepõem 2 dp. |
| 10.26 | `timeline.dart:33`; `linha_da_camada.dart:46`; `linhas.dart:13`; `cabecalho_da_camada.dart:9,92`; `geometria.dart:23,31`; `pintor_da_linha.dart:275`; `linha_da_propriedade.dart:20` | Comentários desatualizados (linha "28", clipe "23", pílula "26"); valores reais 34 / 29 / 32. |
| 10.27 | `janela_da_timeline.dart:5-21` | Documentação descreve a arquitetura ANTIGA (`SingleChildScrollView` com o conteúdo inteiro). |
| 10.28 | `linha_da_camada.dart:929-931` | Toque duplo só em grupos atrasa ~300 ms o toque simples nesses clipes. |
| 10.29 | `tokens.dart:158, 161, 174` | Tokens declarados e não usados (`rotuloDaRegua` 14, `marcadorDeTempo` 42, `botaoMaisDaTimeline` 40). |
| 10.30 | `trilha_da_curva.dart:296-298` | Curva de ajuste por caractere do Texto 3D não é salva no arquivo (volta a linear ao reabrir). |
| 10.31 | `linha_da_propriedade.dart:98-104` | Toque no losango de linha de EFEITO não seleciona nada (seleção de keyframes só conhece `LayerProp`). |
| 10.32 | Conjunto B (`am_timeline.dart@aba36bb`) | Bugs que a nova corrigiu e o port NÃO pode reintroduzir: `jumpTo` do relógio brigando com a inércia (`:246-262`); timeline presa em "editando barra" quando o fim do arrasto não chegava (`:1516-1535`); reordenar mutando a cada meia linha (N passos de desfazer, `:1485-1500`); losango cuja chave mudava a cada passo matando o reconhecedor (`:1553-1579`); zoom que "pulava" um quadro por passo (vista presa à grade). |

---

## 11. MAPEAMENTO PARA COMPOSE E OPERAÇÕES DO MOTOR

### 11.1 Arquivo antigo → componente Compose proposto

| arquivo Flutter | responsabilidade | Compose proposto |
|---|---|---|
| `timeline/timeline.dart` | raiz: layout, gesto de scrub/rolagem/pinça, inércia, revelar | `@Composable Timeline(state: TimelineState, …)` com `Modifier.pointerInput` único na raiz (máquina de estados 11.3); `LazyColumn` com `key` e altura fixa 34 |
| `timeline/estado_da_timeline.dart` | vista (tempo sob cabeçote, zoom), guia, reordenar, auto-rolagem | `@Stable class TimelineState` (`viewUs: Double` em `mutableDoubleStateOf`, `pps: MutableFloatState`, `guideUs`, `reorderLineY`, `widthPx`) + `AutoScroller` (`LaunchedEffect` + `withFrameNanos`) + `GestureOwner` (substitui o contador `_seguram`) |
| `timeline/janela_da_timeline.dart` | janela visível quantizada | `data class VisibleWindow(startPx, endPx)` + `fun of(offset, viewport, inset)` |
| `timeline/geometria.dart` | contas de clipe, alças, losango (pintura e toque) | `object TimelineGeometry` (funções puras, com testes unitários portados de `timeline_test.dart:31`) |
| `timeline/regua.dart` | régua, rótulos, batidas, marcas, pontas, migalha | `TimelineRuler` (`Canvas` em `drawWithCache`, `TextMeasurer` com cache), `GroupBreadcrumb` |
| `timeline/marcas_da_regua.dart` | toque/arrasto/menu das marcas | `RulerMarkersGestures` (dentro do `pointerInput` da régua) + `MarkerMenu` |
| `timeline/cabecote.dart` | linha fixa, selo do tempo, zona de toque | `Playhead` (overlay `drawBehind`) + `TimecodeBadge` (só ele observa o relógio) |
| `timeline/linha_da_camada.dart` | linha de camada: estado, seleção, gestos, mídia | `LayerRow(layerId)` — pinta com `drawBehind`, hit-test explícito por `TimelineGeometry` |
| `timeline/pintor_da_linha.dart` | pintura da linha/trilha, losangos, alças | `DrawScope.drawLayerClip()`, `drawKeyframes()`, `drawTrimHandle()`, `drawPropertyTrack()` |
| `timeline/pintores_do_clipe.dart` | tira de miniaturas, onda (com cache) | `drawFilmstrip()`, `ClipWaveformRenderer` (cache LRU 32 de `Picture`/`RenderNode`) |
| `timeline/cabecalho_da_camada.dart` | cabeçalho 70 | `LayerHeader` |
| `timeline/linha_da_propriedade.dart` | linha de propriedade | `PropertyTrackRow` |
| `timeline/linhas.dart` | estrutura de linhas, expandidas | `TimelineStructure.from(project, expanded)` (lista imutável comparada por valor) |
| `timeline/keyframes_da_timeline.dart` | índices de keyframe por camada | `KeyframeIndex` memoizado por revisão da camada (motor) |
| `timeline/arrasto_de_losango.dart` | sessão de arrasto de keyframe | `KeyframeDragSession` |
| `timeline/ima.dart` | ímã, háptico do ímã, grade de quadros | `SnapEngine` + `SnapHaptics` + `fun snapToFrame(us, fps)` |
| `timeline/guias.dart` | fio do ímã, traço do reordenar | `GuidesOverlay` (`Canvas`) |
| `timeline/reordenar.dart` | reordenar com prévia | `ReorderController` |
| `timeline/sessao_de_gesto.dart` | 1 gesto = 1 desfazer, 1 mutação/quadro | `GestureTransaction` (`beginUndoGroup` na 1ª mutação real, coalescer por quadro, `endUndoGroup` ao soltar/descartar) |
| `timeline/area_de_toque.dart` | hit-test calculado | desnecessário: um `pointerInput` por linha decide com `TimelineGeometry` e só consome o que é dele |
| `curva/editor_de_curva.dart` | editor de curva (folha) | `CurveEditorSheet` |
| `curva/grafico_da_curva.dart` | gráfico, alças, pinça | `CurveGraph` (`pointerInput` cru com `awaitPointerEventScope`) |
| `curva/trilha_da_curva.dart` | adaptador de trilha | `CurveTrack` (interface: marcas, curva do trecho, gravar, gravarEmTodos, valor, relógio) |
| `curva/navegacao_de_keyframes.dart` | anterior/próximo | `KeyframeNavigation` |
| `core/ui/am_tick_ruler.dart` | régua de valor + arrasto | `ValueScrubRuler` + `Modifier.valueDrag()` (painéis) |
| `core/ds/aurea_layer_row.dart` | item de lista de camada 37 | `LayerListItem` |
| `core/ds/aurea_tipo_da_camada.dart` | cor/ícone por tipo | `LayerKind` + `layerKindColor/Icon` |

### 11.2 Gesto → operação a enviar

Motor novo (`Aureabeta/android/.../engine/CommandBatch.kt`) é baseado em QUADROS e handles (`Long`). Colunas:
operação no controlador antigo → comando do motor novo (existente ou **faltando**).

| gesto | controlador antigo | motor novo |
|---|---|---|
| scrub / inércia / auto-rolagem / pinça | `playback.seek(us)` (+ `aoScrub` antes) | `scrubBegin()` no início, `scrub(timeNs)` por quadro, `scrubEnd()` ao soltar (substitui o par aoScrub+seek) |
| pausar ao tocar | `playback.pause()` | `pause()` |
| encaixe em marca no fim do scrub | `seek(marca)` | `seek(timeNs)` — **marcas não existem no motor** |
| mover clipe (1) | `moveLayer(id, inicio)` por quadro, num gesto | `setLayerTimeRange(layer, startFrame, startFrame + duração)` dentro de `beginUndoGroup/endUndoGroup` |
| mover lote (N) | `moveLayers({id: inicio})` (1 mutação) | N × `setLayerTimeRange` no MESMO `CommandBatch` por quadro (atômico) |
| aparar início | `trimLayerStart(id, us)` (remapeia animação; ≤ fim − 100 ms) | `setLayerTimeRange(layer, novoInício, fim)` — **confirmar** que o motor remapeia keyframes e ajusta `sourceOffset` |
| aparar fim | `trimLayerEnd(id, us)` (≥ 100 ms, ≤ fonte) | `setLayerTimeRange(layer, início, novoFim)` — **confirmar** teto pelo arquivo |
| reordenar | `runAsOneUndo(reorderLayer(id, delta))` | `reorderLayer(layer, newIndex)` (índice absoluto; 0 = frente) |
| ocultar / mostrar | `toggleHidden(id)` | `setLayerVisible(layer, bool)` |
| travar (barra) | `toggleLocked(id)` | `setLayerLocked(layer, bool)` |
| dividir no cabeçote (barra/menu) | `splitLayer(id, t)` (≥ 100 ms de cada lado) | `splitLayer(layer, atFrame)` |
| mover keyframe (linha da camada: instante inteiro) | `moverKeyframe(id, deLocal, paraLocal)` (todas as trilhas do instante; recusa módulo/ocupado) | `moveKeyframe(layer, property, effectIndex, effectParam, fromFrame, toFrame)` — é POR TRILHA: a UI tem de emitir um por trilha com marca no instante (ou **faltando**: "move instante") |
| mover keyframe de uma propriedade | `moverKeyframeDaProp(id, prop, de, para)` | `moveKeyframe(...)` para cada sub-trilha da prop (escala X/Y, rotação X/Y/Z…) |
| mover keyframe de efeito | `moverKeyframeDoEfeito(id, effectId, de, para)` | `moveKeyframe(layer, prop=efeito, effectIndex, param, …)` para cada parâmetro com marca |
| mover vários keyframes escolhidos | `moverKeyframes(seleção, delta)` (tudo ou nada) | batch de `moveKeyframe`; **validar tudo-ou-nada na UI** antes de emitir |
| curva de um trecho | `setSegmentEase(layer, prop, inícioLocal, easing)` / `setEffectSegmentEase` | `setKeyframeInterpolation(layer, property, effectIndex, effectParam, timeFrame, interp, bx1, by1, bx2, by2)` por sub-trilha (só bezier; **famílias paramétricas — quique, elástico, degraus — faltam** no motor) |
| curva em todos os trechos | `applyEaseToAllSegments` / `applyEaseToAllEffectSegments` | loop de `setKeyframeInterpolation` num undo group |
| loop da propriedade | `setPropertyLoop(layer, prop, LoopSpec)` | **faltando** |
| marcar/desmarcar no cabeçote | `toggleMarker(t)` (tolerância 120 ms) | **faltando** (marcas) |
| mover / renomear / pintar / apagar marca | `moveMarker`, `renameMarker`, `setMarkerColor`, `removeMarker` | **faltando** |
| entrar / sair de grupo | `enterGroup(id)`, `exitGroup()`, `sairAteONivel(n)` | `setCurrentComposition(comp)` (grupo = composição?) — **confirmar** |
| trocar camada vizinha (Conjunto B, `<` `>`) | `selectNeighbor(±1)` | estado de UI (sem comando) |
| seleção / lote / keyframes escolhidos | providers de UI | estado de UI (sem comando) |
| 1 gesto = 1 desfazer | `beginGesture()` / `endGesture()`; `runAsOneUndo` | `beginUndoGroup(label)` / `endUndoGroup()` |

### 11.3 Receita de gestos em Compose (equivalência da arena)

No Compose os eventos vão do filho que recebeu o hit para os ancestrais (passes Initial → Main → Final); irmãos de
trás NÃO recebem. Portanto:

1. **Um `pointerInput` por `LayerRow`** decide no `PointerEventPass.Main`, com `TimelineGeometry` e a vista atual, o
   que o pouso atingiu: `HEADER` (x < 70) › `KEYFRAME` (metade de baixo, ±10) › `TRIM_START/END` › `BODY` ›
   `NOTHING`. Se `NOTHING`, não consome nada (a raiz recebe).
2. **Slop igual** para linha e raiz (`viewConfiguration.touchSlop`, ≈ 8 dp). Empate "o mais fundo ganha": a linha
   consome o evento de movimento no pass Main assim que o slop horizontal passa; a raiz, também no Main, só age se o
   evento chegar NÃO consumido.
3. **Toque longo**: `awaitLongPressOrCancellation` com 500 ms (fixar; o padrão do Android é 400). Se o dedo passar do
   slop antes, a linha desiste e NÃO consome (a raiz fica com scrub/rolagem). Após aceito, eixo pelo 1º movimento ≥ 8.
4. **Arrasto vertical sobre o clipe**: a linha só consome movimento com `|dx| > |dy|` no momento do slop; vertical
   → deixa para a raiz (rolagem da lista).
5. **Raiz**: `awaitEachGesture { … }` com contagem de ponteiros: 2+ → pinça (`calculateZoom/Centroid` com a base
   capturada no 1º evento); 1 → eixo decidido UMA vez (`|dx| ≥ |dy|` no ponto de aceite); ao terminar a pinça com
   dedo sobrando, ignorar até todos levantarem.
6. **Inércia horizontal**: decaimento próprio `x(t) = v0·(0.135^t − 1)/ln 0.135` (não usar `exponentialDecay` padrão,
   que tem outra curva). Vertical: `LazyListState.scroll { … }` + `splineBasedDecay` (equivale ao Clamping).
7. **Estado do gesto fora do item** (hoisted no `TimelineState`), para sobreviver à reciclagem do `LazyColumn` quando a
   auto-rolagem tira a linha da tela.
8. Toque no vazio: `detectTapGestures` só na raiz, disparado quando nenhum filho consumiu.

---

## 12. CRITÉRIOS DE ACEITAÇÃO (portar dos testes antigos)

`test/ui/timeline/timeline_test.dart` e `test/ui/multisselecao/multisselecao_test.dart` (nomes = contrato):

- régua 42, linha 34, clipe 29 (recuo 2,5), cabeçalho 70 (`:31`)
- tocar no clipe escolhe; tocar no vazio solta (`:69`)
- arrastar o clipe escolhido move no tempo, e é UM desfazer (`:88`)
- clipe NÃO escolhido não se arrasta: o arrasto é scrub (`:114`)
- arrastar a alça apara a ponta (fora do clipe) (`:131`)
- o ímã gruda no cabeçote (com guia) ao mover o clipe (`:162`)
- reordenar pela alça muda a ordem e o Z do palco, e é UM desfazer (`:187`)
- toque longo no cabeçalho + arrastar também reordena (`:236`)
- pinça: zoom ancorado no instante sob os dedos (`:259`)
- arrastar no vazio faz scrub (`:291`); arrastar na vertical rola as camadas (`:319`)
- toque longo PARADO no clipe não abre menu: soma ao lote (`:341`)
- TOQUE no losango abre o editor de curva (`:361`)
- arrastar o clipe até a borda rola o tempo sozinho (`:384`)
- losango: toque longo escolhe, arrastar move (UM desfazer) (`:419`)
- propriedade ativa: os losangos dela acendem, os outros apagam (`:477`)
- na linha da propriedade/efeito, arrastar move SÓ a marca dela (`:535`, `:569`)
- o tique do relógio não reconstrói linhas nem a timeline (`:614`); mover UM clipe reconstrói só a linha dele (`:647`);
  com 100 camadas só as linhas da tela são montadas (`:666`)
- dividir no cabeçote: a timeline mostra os dois pedaços (`:688`)
- multisseleção: escolhida + toque longo em B e C = 3 no lote (`:170`); com lote ativo o toque soma/tira, com uma só
  volta à simples (`:228`); tocar no vazio sai do lote (`:255`); segurar a única escolhida não a solta (`:319`);
  o lote para inteiro no zero (`:345`); lote com camada bloqueada não anda pela metade (`:364`)
