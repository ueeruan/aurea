# AM2 STUDIO — MOTOR DE ANIMAÇÃO DE TEXTO PROFISSIONAL
### Especificação de implementação, paridade com After Effects
**Base:** motor atual descrito em `AM2_STUDIO.md`, seção "Texto e animadores de texto".
**Objetivo:** chegar ao nível do sistema de animadores do After Effects, incluindo a parte que quase nenhum app mobile tem: **múltiplos seletores por animador, combinados por modo.**

---

## 0. O QUE VOCÊ JÁ TEM

Crédito onde é devido. O motor atual já é mais completo que o do Alight Motion:

| Já implementado | Situação |
|---|---|
| Múltiplos animadores empilhados por camada | ✅ |
| Base do seletor: caractere, palavra, linha | ✅ |
| Formas: quadrada, ramp up, ramp down, triangular, round, smooth | ✅ |
| Início, fim, offset, amount, smoothness, ease high/low | ✅ |
| Ordem aleatória determinística com semente | ✅ |
| Propriedades: posição, pivô por item, escala, rotação, inclinação, opacidade, tracking | ✅ |
| Ligar/desligar e remover animador | ✅ |
| 13 presets que já criam keyframes no seletor | ✅ |
| Rasterização por unidade só quando há animador | ✅ |

## 0.1 O QUE FALTA PARA SER O AE

| Falta | Impacto | Sev |
|---|---|---|
| **Vários seletores por animador, com modo de combinação** | É isto que faz animação complexa. Sem isso, tudo vira onda | **P0** |
| **Seletor Wiggly** | Ruído por caractere. Base de metade dos efeitos "vivos" | **P0** |
| **Cor de preenchimento e contorno por animador** | Sem isso não há reveal colorido, glitch cromático, ênfase | **P0** |
| **Unidades por índice** (além de porcentagem) | "do caractere 3 ao 7" é impossível hoje | P1 |
| **Base "caracteres exceto espaços"** | Espaço contando como unidade estraga timing | P1 |
| **Agrupamento de âncora** (caractere/palavra/linha/tudo) | Define em torno de quê cada unidade gira | P1 |
| **Texto de origem animável** | Trocar a string ao longo do tempo, contador, máquina real | P1 |
| **Texto em caminho** | Texto circular, selo, arco | P1 |
| **Espessura de contorno, blur, entrelinha por animador** | Propriedades que o AE tem e você não | P1 |
| **Character Offset e Character Value** | Scramble, decodificação, glitch de código | P2 |
| Seletor por função/expressão | Curvas arbitrárias por índice | P2 |
| Per-character 3D | Depende de rotação 3D no core, que não existe | P3 |
| Justificado, kerning, baseline shift, caps | Tipografia básica ausente | P2 |
| Biblioteca de presets + salvar preset do usuário | 13 é pouco, e não dá para salvar o seu | P1 |

---

## 1. ARQUITETURA — PIPELINE DE AVALIAÇÃO

Para cada frame, para cada camada de texto:

```
1. SHAPING       string → lista de unidades (grapheme clusters shaped)
2. LAYOUT        posição base de cada unidade na linha/parágrafo
3. SELEÇÃO       para cada animador: cada seletor produz cᵢ, combinados por modo
4. APLICAÇÃO     cada propriedade combina seu valor com cᵢ, regra própria por tipo
5. AGRUPAMENTO   define o pivô de cada unidade conforme anchorGrouping
6. TRANSFORM     matriz por unidade = layout × animadores acumulados
7. RASTER        quad por unidade, glifo vindo do atlas
```

Três invariantes que não podem ser quebradas:

- **I1 — Determinismo.** Renderizar o frame N direto tem que dar exatamente o mesmo resultado de chegar nele reproduzindo desde 0. Nada de estado acumulado entre frames. Ruído é função pura de `(seed, índice, tempo)`.
- **I2 — Neutralidade.** Um animador com todas as propriedades no valor neutro, ou com cobertura zero, produz **pixel a pixel** o mesmo resultado que não ter animador nenhum. Isto é teste automatizado obrigatório (ver §13).
- **I3 — Paridade.** Preview e export usam o mesmo caminho. Só muda a resolução.

---

## 2. MODELO DE DADOS

```text
TextLayer
  sourceText: AnimProp<String>        // NOVO — keyframes hold
  typography: Typography
  pathOptions: PathOptions?           // NOVO
  moreOptions: MoreOptions            // NOVO
  animators: TextAnimator[]

MoreOptions
  anchorGrouping: CHARACTER | WORD | LINE | ALL
  groupingAlignmentX: Float   // -100..100 %
  groupingAlignmentY: Float
  interCharBlending: BlendMode

TextAnimator
  name: String
  enabled: Boolean
  selectors: Selector[]               // NOVO: era 1, agora N
  properties: AnimatorProperty[]
  allowOvershoot: Boolean = false

Selector = RangeSelector | WigglySelector | FunctionSelector

RangeSelector
  mode: ADD | SUBTRACT | INTERSECT | MIN | MAX | DIFFERENCE
  units: PERCENT | INDEX               // NOVO
  basedOn: CHARACTERS | CHARACTERS_NO_SPACES | WORDS | LINES   // NOVO valor
  start: AnimProp<Float>
  end: AnimProp<Float>
  offset: AnimProp<Float>
  shape: SQUARE | RAMP_UP | RAMP_DOWN | TRIANGLE | ROUND | SMOOTH
  amount: AnimProp<Float>              // -100..100
  smoothness: AnimProp<Float>          // 0..100, só SQUARE
  easeHigh: AnimProp<Float>            // -100..100
  easeLow: AnimProp<Float>
  randomizeOrder: Boolean
  randomSeed: Int

WigglySelector                          // NOVO
  mode, basedOn
  maxAmount: AnimProp<Float>           // default 100
  minAmount: AnimProp<Float>           // default -100
  wigglesPerSecond: AnimProp<Float>    // default 2
  correlation: AnimProp<Float>         // 0..100, default 50
  temporalPhase: AnimProp<Float>       // graus
  spatialPhase: AnimProp<Float>
  lockDimensions: Boolean
  randomSeed: Int

FunctionSelector                        // NOVO, substitui a expressão do AE
  mode, basedOn
  curve: CurveSpec                      // reusa o editor de curvas já existente
  // eixo X = índice normalizado, eixo Y = cobertura

AnimatorProperty
  type: PropertyType                    // ver tabela §6
  value: AnimProp<FloatArray | Color>
```

**Compatibilidade:** ao carregar projeto antigo com um seletor único, envolva em `selectors: [aquele]` com `mode = ADD`. Serialização por nome de enum, como você já faz.

---

## 3. UNIDADES DE TEXTO — A PARTE QUE QUEBRA APPS

Este é o ponto onde a maioria dos editores mobile erra, e o erro só aparece com usuário real.

### 3.1 Nunca dividir por caractere Java
`String.charAt` percorre code units UTF-16. Isso quebra:

| Entrada | Divisão ingênua | Correto |
|---|---|---|
| `👨‍👩‍👧` | 8 unidades de lixo | 1 unidade |
| `café` com acento combinante | `e` + `´` separados | 1 unidade |
| `ﬁm` com ligadura | glifo cortado ao meio | 1 unidade, ou ligadura desativada |
| `مرحبا` | letras na forma isolada errada | shaping contextual da linha |

**Regra:** segmentar com `BreakIterator.getCharacterInstance()` para grapheme clusters. Em API 31+, usar `android.graphics.text.TextShaper` para obter clusters já moldados. Abaixo disso, medir com `Paint.getTextRunAdvances` e agrupar por cluster.

### 3.2 Ligaduras
Se houver qualquer animador com base `CHARACTERS`, desative ligaduras discricionárias:
```kotlin
paint.fontFeatureSettings = "'liga' 0, 'clig' 0, 'dlig' 0"
```
Alternativa: tratar a ligadura como uma unidade só. Escolha uma e documente — o que não pode é o glifo aparecer partido.

### 3.3 Scripts complexos
Árabe, hebraico, devanágari e tailandês não sobrevivem à divisão por caractere. Comportamento exigido: detectar script complexo e **rebaixar automaticamente a base para `WORDS`**, avisando na UI: *"animação por caractere indisponível neste idioma"*. Silenciosamente produzir lixo é pior que a limitação.

### 3.4 O bug do texto que "abre"
O erro clássico: sem animador, o texto é medido como linha inteira, com kerning. Com animador, cada unidade é medida sozinha e somada. Kerning some, o texto fica mais largo e **salta** no instante em que o animador é adicionado.

**Regra:** o avanço de cada unidade vem **sempre** da medição da linha completa, nunca da soma de larguras individuais. Vale mesmo sem animador. Isto é a invariante I2 e tem teste próprio.

---

## 4. SELETOR DE INTERVALO — MATEMÁTICA COMPLETA

Sejam `N` unidades e a unidade `i` com centro normalizado:

```
pᵢ = (i + 0.5) / N
```

Se `randomizeOrder`, substitua `i` por `π(i)`, onde `π` é uma permutação Fisher-Yates com PRNG semeado por `randomSeed`. A permutação depende só de `(seed, N)`, então é estável no seek.

### 4.1 Conversão de unidades
```
PERCENT:  lo = min(start, end) + offset        (valores em 0..1)
          hi = max(start, end) + offset
INDEX:    lo = min(start, end)/N + offset/N
          hi = max(start, end)/N + offset/N
w = hi - lo
```

### 4.2 Posição relativa e forma
```
t = (pᵢ - lo) / w        // se w <= 0 → cobertura 0
se t < 0 ou t > 1 → c = 0
```

| Shape | c(t) |
|---|---|
| SQUARE | `1` |
| RAMP_UP | `t` |
| RAMP_DOWN | `1 - t` |
| TRIANGLE | `1 - abs(2t - 1)` |
| ROUND | `sqrt(max(0, 1 - (2t-1)²))` |
| SMOOTH | `0.5 - 0.5 * cos(2π · t)` |

### 4.3 Smoothness (só SQUARE)
Em vez de degrau, rampa de largura `sm` unidades em cada borda, com `u = 1/N`:
```
sm = smoothness/100
se sm == 0 → c = 1
senão      → c = clamp((pᵢ - lo)/(sm·u), 0, 1) · clamp((hi - pᵢ)/(sm·u), 0, 1)
```

### 4.4 Ease High / Ease Low
Remapeia a cobertura por uma bezier cúbica 1D. Com `eL = easeLow/100` e `eH = easeHigh/100`, ambos em `-1..1`:
```
P1 = ( max(0, eL),  max(0, -eL) )
P2 = ( 1 - max(0, eH),  1 - max(0, -eH) )
c' = cubicBezier1D(c; P1, P2)     // mesmo solver do seu editor de curvas
```
Leitura: `easeLow = 100` achata a saída do zero; `easeLow = -100` faz saltar. Idem para o topo com `easeHigh`.

### 4.5 Amount
```
c'' = c' · (amount / 100)
```
Amount negativo inverte a contribuição. É como se faz "todo mundo menos o selecionado".

### 4.6 Combinação de vários seletores
Avaliar em ordem, acumulando:
```
acc = c₁                          // o primeiro seletor define a base, qualquer que seja o modo
para cada seletor k a partir do 2º:
  ADD         acc = acc + cₖ
  SUBTRACT    acc = acc - cₖ
  INTERSECT   acc = acc · cₖ
  MIN         acc = min(acc, cₖ)
  MAX         acc = max(acc, cₖ)
  DIFFERENCE  acc = abs(acc - cₖ)

final = allowOvershoot ? acc : clamp(acc, 0, 1)
```

> **Desvio consciente do AE:** no AE, se o primeiro seletor for INTERSECT o resultado é sempre zero, porque o acumulador começa em 0. É uma armadilha conhecida. Aqui o primeiro seletor define a base. Documente isso.

**Por que isto é P0:** "letras entram uma a uma **e** tremem **e** só as do meio" é impossível com um seletor. Com três seletores combinados, é trivial. É a diferença entre animação de app e animação de motion designer.

---

## 5. SELETOR WIGGLY

Ruído por unidade, função pura do tempo. Nada de acumular estado.

```
u   = i · (1 - correlation/100) + spatialPhase/360
τ   = t · wigglesPerSecond + temporalPhase/360

shared = valueNoise01(seed, 0,     τ)
own    = valueNoise01(seed, u + 1, τ)
n      = lerp(own, shared, correlation/100)

c = (minAmount + n · (maxAmount - minAmount)) / 100
```

- `correlation = 0` → cada unidade treme sozinha.
- `correlation = 100` → todas tremem juntas.
- `lockDimensions` → o mesmo `n` alimenta X e Y da propriedade, em vez de duas amostras.

`valueNoise01` deve ser hash puro `(seed, x, y) → [0,1]` com interpolação suave, sem tabela mutável e sem depender de chamada anterior. Isto satisfaz I1.

---

## 6. PROPRIEDADES DO ANIMADOR

Cada propriedade tem **regra de combinação própria**. Somar tudo é o erro clássico: escala e opacidade não são aditivas.

| Propriedade | Faixa | Neutro | Regra com cobertura `c` |
|---|---|---|---|
| Position X/Y/Z | px | 0 | `base + v · c` |
| Anchor Point | px | 0 | `base + v · c` |
| **Scale X/Y** | % | 100 | `base · lerp(1, v/100, c)` |
| Skew | graus | 0 | `base + v · c` |
| Skew Axis | graus | 0 | `base + v · c` |
| Rotation | graus | 0 | `base + v · c` |
| **Opacity** | % | 100 | `base · lerp(1, v/100, c)` |
| **Fill Color** | RGBA | — | `lerp(base, v, c)` |
| Fill Hue | graus | 0 | aditivo em HSV, com wrap |
| Fill Saturation / Brightness | % | 0 | aditivo em HSV, clamp |
| Fill Opacity | % | 100 | multiplicativo |
| **Stroke Color** | RGBA | — | `lerp(base, v, c)` |
| **Stroke Width** | px | 0 | `base + v · c` |
| Tracking | px ou em | 0 | `base + v · c`, aplicado ao avanço |
| Line Spacing | px | 0 | `base + v · c` |
| **Blur X/Y** | px | 0 | `base + v · c`, gaussiano por unidade |
| **Character Offset** | int | 0 | `round(base + v · c)` sobre o code point |
| **Character Value** | int | — | `round(lerp(baseCode, v, c))` |
| Rotation X/Y (3D) | graus | 0 | aditivo, requer core 3D |

**Acúmulo entre animadores:** os animadores aplicam em ordem de pilha, cada um sobre o resultado do anterior. Aditivos somam; multiplicativos multiplicam; cores interpolam em cadeia.

**Character Offset e Character Value** obrigam a buscar outro glifo por frame. Mantenha o atlas com LRU e um teto de glifos vivos.

---

## 7. AGRUPAMENTO DE ÂNCORA

Define em torno de que ponto cada unidade escala e gira.

| anchorGrouping | Pivô de cada unidade |
|---|---|
| CHARACTER | centro da própria unidade |
| WORD | centro da palavra a que pertence |
| LINE | centro da linha |
| ALL | centro do bloco de texto inteiro |

`groupingAlignmentX/Y` em `-100..100 %` desloca esse pivô dentro da caixa do grupo. É o que permite "as letras giram a partir da base da linha" em vez do centro.

Sem isso, rotação por caractere sempre parece errada. É P1 e é barato de implementar.

---

## 8. TEXTO DE ORIGEM ANIMÁVEL

`sourceText` vira `AnimProp<String>` com **interpolação hold obrigatória** — string não interpola.

Habilita: contador, legenda sincronizada, decodificação, troca de palavra no ritmo da música.

Cuidados:
- Trocar a string **remede** a linha. Se a contagem de unidades muda, os seletores por índice mudam de alvo. Comportamento definido: seletores em PERCENT continuam proporcionais; em INDEX ficam presos ao índice, podendo ficar fora do intervalo, e nesse caso `c = 0`.
- Invalidação de cache por frame quando a string muda. Não recalcule layout todo frame se a string é a mesma.

---

## 9. TEXTO EM CAMINHO

`PathOptions`, equivalente ao AE e ao "Text on Path" do Alight Motion:

| Parâmetro | Função |
|---|---|
| path | referência a uma camada de forma/vetor da cena |
| reversePath | inverte o sentido |
| perpendicularToPath | glifo gira acompanhando a tangente |
| forceAlignment | distribui do primeiro ao último ponto |
| firstMargin / lastMargin | recuo animável nas pontas |

Implementação: reamostrar o path por comprimento de arco, mapear o avanço acumulado de cada unidade para uma distância no path, obter ponto e tangente. É o mesmo cálculo do seu caminho espacial de keyframes, então boa parte já existe.

Isto entrega selo circular, arco e texto em curva, que é um dos pedidos mais comuns de quem edita no celular.

---

## 10. TIPOGRAFIA QUE FALTA

Independente de animadores, o painel de texto precisa de:

| Item | Estado |
|---|---|
| Alinhamento justificado | ❌ (só esquerda/centro/direita) |
| Kerning: métrico e óptico | ❌ |
| Baseline shift | ❌ |
| Caixa alta / versalete | ❌ |
| Superscript / subscript | ❌ |
| Faux bold / faux italic | ❌ |
| Contorno no texto, com ordem contorno-sobre-preenchimento | ❌ |
| Entrelinha absoluta em px, além do multiplicador 0,65–1,8 | ❌ |
| Recuo e espaço antes/depois de parágrafo | ❌ |
| Importar fonte do usuário (.ttf/.otf) | ❌ — hoje só Aeonik e sistema |

Importação de fonte é P1: quem faz motion tem fonte de marca, e um editor que não aceita fonte própria não é usado profissionalmente.

---

## 11. PRESETS

### Formato
```json
{
  "format": "am2-text-preset",
  "version": 1,
  "name": "Cascata elástica",
  "tags": ["entrada", "elástico"],
  "suggestedDuration": 1.4,
  "animators": [ ... ],
  "sourceTextIndependent": true
}
```
Keyframes gravados **relativos ao início do preset**, não ao tempo absoluto. Ao aplicar: ancorar no cabeçote e permitir esticar para a duração desejada, reescalando os tempos.

### Biblioteca mínima — 40 presets, agrupados
- **Entrada:** fade por letra, subir por letra, cascata, máquina de escrever, escala pop, blur in, rotação 3D falsa, deslizar por palavra, revelar por linha, dominó.
- **Saída:** espelhos de todos os de entrada, com seletor invertido.
- **Ênfase:** wiggle sutil, pulso, tremor nervoso, respiração, onda contínua, jitter de tracking.
- **Cinético:** impacto com overshoot, mola por palavra, elástico por linha, chicote, kick no beat.
- **Glitch:** deslocamento RGB por caractere, scramble com Character Value, corte horizontal, ruído de opacidade.
- **Especiais:** contador numérico, decodificação, texto em caminho circular, karaokê por palavra.

### Preset do usuário
Salvar a pilha de animadores atual como preset, com nome e tag. Guardar em `filesDir/textpresets/`. Mesmo tratamento de biblioteca que os Elementos — e, diferente da aba Elementos de hoje, **listar de verdade**.

---

## 12. UI NO INSPETOR

Seguindo a pilha de navegação que você já tem (`Seções → Efeitos → Parâmetros → Curva`):

```
Editar Texto
├── Conteúdo, fonte, tamanho, alinhamento, métricas
├── Mais opções     → agrupamento de âncora, alinhamento, blending
├── Caminho         → path, reverso, perpendicular, margens
└── Animadores
    ├── [+ Animador]
    └── Animador 1  [on/off] [⋯]
        ├── Propriedades  [+ Propriedade]
        │   └── cada uma: valor + ◆ + curva
        └── Seletores     [+ Seletor]
            └── Seletor 1 · Intervalo · modo Add
                └── start, end, offset, forma, amount, ease…
```

**Duas coisas que o AE não tem e você deveria ter:**

1. **Barra de cobertura visual.** Enquanto o seletor está aberto, desenhe no preview (ou numa faixa acima do texto) uma barrinha por unidade com a altura igual à cobertura `c`. A pessoa **vê** o seletor. Isto sozinho resolve a maior dificuldade de aprendizado do sistema de animadores.
2. **Chips de propriedade.** Adicionar propriedade por chips com miniatura do efeito, em vez de menu de lista.

Na timeline, cada propriedade do animador e cada parâmetro do seletor vira faixa de keyframe, igual às outras propriedades.

---

## 13. PERFORMANCE

| Regra | Motivo |
|---|---|
| Atlas de glifos, chave `(fontId, size, glyphId, strokeWidth, fauxStyle)` | Rasterizar uma vez, não por frame |
| Um quad por unidade, transform na GPU | 500 unidades a 60 fps precisa disso |
| Layout só recalcula quando muda string, fonte, tamanho, tracking, largura | Layout por frame é o gargalo típico |
| Blur por unidade só quando `blur > 0` em alguma unidade | Passe caro, evite quando desligado |
| LRU no atlas, teto de glifos vivos | Character Value animado gera glifos infinitos |
| Sem animador, caminho rápido: rasteriza a linha inteira | Já é o que você faz, mantenha |

Orçamento alvo: **500 unidades animadas a 60 fps** no proxy de preview, num aparelho médio de 2023.

---

## 14. TESTES OBRIGATÓRIOS

Sem estes, a Fase 0 da auditoria não está cumprida para texto.

**Unitários da matemática do seletor**
- `N=10`, `start=0`, `end=0.5`, `shape=RAMP_UP` → tabela de 10 coberturas esperadas, valores fixos.
- Cada uma das 6 formas com a mesma entrada, valores esperados tabelados.
- Combinação: dois seletores em cada um dos 6 modos, resultado esperado.
- `easeHigh/easeLow` nos extremos `-100`, `0`, `100`.
- `randomizeOrder` com semente fixa → permutação idêntica em 100 execuções.

**Invariantes**
- **I2:** camada com animador vazio renderiza **pixel a pixel** igual à camada sem animador. Este é o teste do "texto que abre".
- **I1:** wiggly no frame 137 renderizado direto == renderizado após reproduzir do 0.
- Cobertura zero em todas as unidades → idêntico ao base.

**Segmentação**
- `"👨‍👩‍👧 café ﬁm שלום"` → contagem de unidades esperada por base (`CHARACTERS`, `CHARACTERS_NO_SPACES`, `WORDS`, `LINES`).

**Golden frames**
- Um por preset da biblioteca, em 3 instantes: 25%, 50%, 90% da duração.

**Serialização**
- Round-trip de um animador com 3 seletores de tipos diferentes → idêntico.
- Projeto salvo na versão antiga (1 seletor) carrega migrado para `selectors: [x]`.

---

## 15. PLANO DE PRs

**PR-T1 — Multi-seletor.** Refatorar `TextAnimator` para lista de seletores com modo. Migração de projetos antigos. Testes unitários da matemática de §4 completos. **Sem UI nova ainda**, só o motor e os testes.

**PR-T2 — Segmentação correta.** Grapheme clusters, ligaduras, script complexo com rebaixamento automático, avanço vindo da linha inteira. Testes de §14 "Segmentação" e a invariante I2.

**PR-T3 — Seletor Wiggly.** Ruído puro determinístico. Testes de I1.

**PR-T4 — Propriedades que faltam.** Fill color, stroke color, stroke width, blur, line spacing, character offset, character value. Depende dos controles de cor do PR-7 da auditoria.

**PR-T5 — Agrupamento de âncora** e alinhamento de grupo.

**PR-T6 — UI.** Lista de seletores, chips de propriedade, **barra de cobertura visual**, faixas na timeline.

**PR-T7 — Source text animável** com hold.

**PR-T8 — Texto em caminho.**

**PR-T9 — Tipografia:** justificado, kerning, baseline shift, caps, contorno, importar fonte.

**PR-T10 — Presets:** formato, biblioteca de 40, salvar preset do usuário, listagem real.

---

## 16. PROMPT PARA O AGENTE DE CÓDIGO

> Implemente o motor de animação de texto profissional do AM2 Studio conforme `AM2-motor-de-texto.md`. O motor atual está descrito em `AM2_STUDIO.md`, seção "Texto e animadores de texto" — leia os dois antes de começar.
>
> **Antes de codar, responda com evidência do fonte:**
> 1. `TextAnimator` hoje tem um seletor único ou uma lista? Cole a declaração.
> 2. `TextRender.kt` divide o texto por `charAt`/code unit ou por grapheme cluster? Cole o trecho da segmentação.
> 3. O avanço de cada unidade vem da medição da linha inteira ou da soma de larguras individuais? Cole o trecho.
> 4. O ruído usado em presets tipo glitch é função pura de `(seed, índice, tempo)` ou acumula estado entre frames? Cole o trecho.
>
> **Execute em ordem, um PR por item:**
>
> **PR-T1** — Refatore `TextAnimator.selector` para `selectors: List<Selector>` com `mode: ADD|SUBTRACT|INTERSECT|MIN|MAX|DIFFERENCE`. Implemente a matemática da seção 4 exatamente como especificada, incluindo units PERCENT/INDEX e base CHARACTERS_NO_SPACES. Migre projetos antigos envolvendo o seletor existente numa lista com modo ADD. Escreva os testes unitários tabelados da seção 14 **antes** da implementação — eles devem falhar, depois passar. Sem mudança de UI neste PR.
>
> **PR-T2** — Corrija a segmentação: `BreakIterator.getCharacterInstance()` para grapheme clusters, `TextShaper` quando disponível, ligaduras desativadas quando houver animador por caractere, rebaixamento automático para WORDS em script complexo com aviso na UI. O avanço de cada unidade deve vir sempre da medição da linha completa. Adicione o teste da invariante I2: camada com animador vazio renderiza pixel a pixel igual à camada sem animador.
>
> **PR-T3** — Seletor Wiggly conforme a seção 5, com `valueNoise01` como hash puro sem estado. Teste: frame 137 direto == frame 137 após reproduzir do 0.
>
> **PR-T4** — Propriedades novas: fill color, fill hue/sat/bright, stroke color, stroke width, blur X/Y, line spacing, character offset, character value. Cada uma com a regra de combinação da tabela da seção 6. Escala e opacidade são multiplicativas, não aditivas.
>
> **PR-T5** — Agrupamento de âncora CHARACTER/WORD/LINE/ALL com grouping alignment.
>
> **PR-T6** — UI: lista de seletores por animador, chips de propriedade, faixas na timeline, e a **barra de cobertura visual** que desenha a cobertura de cada unidade enquanto o seletor está aberto.
>
> **PR-T7 a PR-T10** — source text animável com hold, texto em caminho, tipografia faltante e sistema de presets, nesta ordem.
>
> **Regras:**
> - Determinismo absoluto: nada de estado acumulado entre frames.
> - Neutralidade: animador neutro não muda um pixel.
> - Preview e export pelo mesmo caminho.
> - Golden frame test para cada preset novo.
> - Atualize `AM2_STUDIO.md` no mesmo PR que muda comportamento.
