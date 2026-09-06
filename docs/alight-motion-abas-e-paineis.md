# ALIGHT MOTION — MAPA DE ABAS E PAINÉIS
### Onde cada aba fica, o que abre dentro dela, e cada parâmetro

*Complemento do documento "Mapa completo do editor". ⚠️ = rótulo/posição varia por versão e por tamanho de tela. ✅ = confirmado na documentação oficial.*

---

## A REGRA QUE EXPLICA A INTERFACE INTEIRA

O Alight Motion **não tem uma barra de abas fixa**. A barra inferior é **contextual**: ela muda conforme o que está selecionado. E a navegação é em **níveis de profundidade** — você entra e o botão de voltar (✓ ou ←) sobe um nível.

```
NÍVEL 0   Nada selecionado        → barra de projeto: (+) Adicionar, bookmarks
NÍVEL 1   Camada selecionada      → barra de painéis: Move & Transform, Effects, ...
NÍVEL 2   Painel aberto           → SUB-ABAS do painel (Location, Scale, Rotation…)
NÍVEL 3   Sub-aba ativa           → parâmetros + diamante de keyframe
NÍVEL 4   Curve editor            → curva entre 2 keyframes + menu ⋯
```

**Consequência prática nº1:** a **sub-aba ativa define quais keyframes aparecem na timeline**. ✅
- Diamante **branco sólido com borda escura** = da aba atual, editável
- Diamante **apagado, sem borda** = de outra aba. Troque de aba antes de mexer

**Consequência prática nº2:** o Curve Editor só mostra curva se o **playhead estiver entre dois keyframes** daquela aba. ✅

---

# NÍVEL 0 — NADA SELECIONADO

Toque numa área vazia da timeline ou no fundo do preview para desselecionar.

| Onde | Elemento | Abre |
|---|---|---|
| Topo esquerda | **←** | Sai do projeto |
| Topo centro | **Nome do projeto** | Renomear ✅ |
| Topo | **⚙** | **Configurações do projeto** (ver adiante) |
| Topo | **↶ ↷** | Undo / Redo |
| Topo direita | **⤓ Export** | Painel de exportação |
| Sobre a timeline | **👁 View Options** | Active Camera View, Preview Zoom, qualidade ✅ |
| Sobre a timeline | **🔍 lupa** | Pan & Zoom Mode (só funciona sem camada selecionada) ✅ |
| Inferior direita | **(+)** | Menu Adicionar |
| Barra de tempo | **long-press no tempo** | Menu de marcas / retiming ✅ |

### ⚙ Configurações do projeto ⚠️
| Campo | Opções |
|---|---|
| Resolução | 480p / 720p / 1080p / 1440p / 4K |
| Frame rate | 24 / 25 / 30 / 60 |
| Background | cor sólida / transparente |
| Duração | tempo total do projeto |

Em **Element**, a mesma engrenagem ganha dois blocos a mais: **Precompose** (Dynamic / Fixed Resolution) e **Re-timing** (Freeze / Stretch / Loop / Loop & Stretch / Blank / Off). ✅

### (+) Menu Adicionar — abas ⚠️
| Aba | Conteúdo |
|---|---|
| Media / Photo & Video | galeria e arquivos |
| Shape | biblioteca de formas vetoriais |
| Text | novo texto |
| Drawing / Freehand | caneta bézier e mão livre |
| Audio | música, gravação |
| **Object** | **Camera** e **Null** ✅ |
| Elements | seus elements + *Download Elements* ✅ |
| Presets / Templates | presets salvos, templates |

---

# NÍVEL 1 — CAMADA SELECIONADA: A BARRA DE PAINÉIS

Toque numa camada na timeline (ou no objeto no preview). A barra inferior vira esta lista. ⚠️ A ordem e os ícones mudam entre versões; role a barra para o lado, quase sempre tem mais botão do que cabe na tela.

| Botão | Aparece em | Abre (Nível 2) |
|---|---|---|
| **Move & Transform** | todas as camadas visuais | Location · Scale · Rotation · Skew · Pivot |
| **Effects** ⚠️ (às vezes "Effects & Fill") | todas | Pilha de efeitos + botão de adicionar |
| **Blending & Opacity** | todas as visuais | Opacity · Blend Mode · Mask |
| **Color & Fill** | vetor, texto, forma, element | Fill on/off · seletor de cor · alpha ✅ |
| **Layer Parent** | todas | Dropdown com as outras camadas ✅ |
| **Text** ⚠️ | camada de texto | Conteúdo, fonte, tamanho, alinhamento, espaçamento |
| **Edit path / nós** ⚠️ | vetor e desenho | Nós, alças bézier, fechar caminho |
| **Trim / Split** | vídeo, áudio, todas | Corta no playhead |
| **Speed / Time Remap** ⚠️ | vídeo, áudio | Velocidade fixa ou remapeamento por keyframes |
| **Volume / Audio** | vídeo com áudio, áudio | Ganho, fade, extrair áudio ✅ |
| **Element Properties** | element | Campos de texto, Edit original, Convert to Group ✅ |
| **Camera** ⚠️ | câmera | View Angle / Zoom Distance, Focus Blur, Fog |
| **⋯ overflow** | todas | Duplicar, deletar, copiar, colar estilo, agrupar, *Save to My Elements* ✅ |

---

# NÍVEL 2 — DENTRO DE CADA PAINEL

## 2.1 MOVE & TRANSFORM ✅
**Caminho:** camada → *Move & Transform*

Abre uma fileira de **sub-abas**. Cada uma tem sua **própria linha de keyframes**.

| Sub-aba | Parâmetros | Notas |
|---|---|---|
| **Location / Position** | X · Y · **Z** ✅ | Z só faz diferença visível com câmera. Em pixels |
| **Scale / Size** | Largura · Altura · cadeado de proporção | ⚠️ % ou px conforme a versão |
| **Rotation / Angle** | Graus | Aceita >360° para várias voltas |
| **Skew** | Inclinação ✅ | Cisalhamento |
| **Pivot / Anchor** | X · Y ✅ | Centro de giro e escala. Padrão = centro do objeto |

**Anatomia de cada sub-aba (Nível 3):**
```
[nome do parâmetro]  [− valor +]  ◆  ‹◆›  ∿  ↺
                       spinner    kf  nav  curva  reset
```
- **spinner**: arrasta para variar; **toque no número abre teclado numérico** para valor exato (AM 5.0+)
- **◆**: cria/remove keyframe no tempo atual
- **‹◆›**: pula para o keyframe anterior/próximo
- **∿**: abre o **Curve Editor**
- Handles no preview também alteram esses valores (pinça = escala+rotação, arraste = posição)

## 2.2 EFFECTS — a parte com mais níveis
**Caminho:** camada → *Effects*

### Nível 2 — a pilha
Lista dos efeitos já aplicados **nesta** camada, em ordem de aplicação (de cima para baixo).

| Elemento da linha | Função |
|---|---|
| Nome do efeito | Toque → abre os parâmetros (Nível 3) |
| Alça de arrasto | **Reordena.** A ordem muda o resultado |
| Olho / toggle ⚠️ | Liga e desliga o efeito sem apagar |
| ⋯ da linha | Deletar, duplicar, copiar ✅ |
| **(+) Add Effect** | Abre o **Effect Browser** |
| Marca **"(Compatibility)"** | Efeito de versão antiga. Delete e reaplique para usar a versão nova ✅ |

### Nível 2b — Effect Browser
| Elemento | Função |
|---|---|
| Campo de busca | Por nome |
| **Tags / categorias** | Filtro oficial. Tags reais: *Essentials, Blur, Raster, Vector, Compositing, Matte/Mask/Key, Green Screen, Key, Transform, Move/Transform, Property Adjust, Shake, Auto, Camera, Layer Parenting, Color, Distort, Stylize, Text, Time* ⚠️ |
| Thumbnail animado | Prévia do efeito |
| Botão **Guide** | Abre a ficha oficial daquele efeito, com todos os parâmetros ✅ |
| Favoritos / recentes ⚠️ | Acesso rápido |

**Filtros úteis:** tag **Camera** = o que funciona em câmera ✅. Tag **Layer Parenting** = efeitos que também afetam camadas filhas ✅.

### Nível 3 — parâmetros do efeito
Tocar num efeito aplicado abre **uma fileira de sub-abas, uma por parâmetro**, exatamente como Move & Transform. Cada parâmetro tem seu próprio diamante e sua própria curva.

**Tipos de controle** (os ícones que o guia oficial usa): ✅

| Tipo | Aparência | Exemplo |
|---|---|---|
| `spinner` | número inteiro | Frequency do Oscillate |
| `spinner-float` | número decimal | Threshold do Chroma Key |
| `spinner-angle` | graus | Angle do Oscillate |
| `switch` | liga/desliga | Defringe, Invert |
| `radio` | escolha entre opções | Wave: Sine / Triangle |
| `color` | seletor + **conta-gotas** | Key Color |
| point | posição no canvas | centro de um Glow |
| path | caminho vetorial | Move Along Path |
| gradient | rampa de cores | Gradient Overlay |

**Toda linha de parâmetro tem o diamante ◆.** É isso que faz "animar qualquer coisa" no AM. ✅

### Exemplo real de Nível 3 — Chroma Key ✅
| Sub-aba | Tipo | Range · Default |
|---|---|---|
| Key Color | color + conta-gotas | — |
| Threshold | float | 0 a 1 · **0.1** |
| Feather | float | 0.01 a 0.75 · **0.05** |
| Defringe | switch | **off** |
| Invert | switch | **off** |

### Exemplo real de Nível 3 — Oscillate ✅
| Sub-aba | Tipo | Range · Default |
|---|---|---|
| Angle | ângulo | −3600° a 3600° · **45°** |
| Frequency | spinner | 0 a 16 Hz · **2** |
| Magnitude | float | 0 a 4000 px · **25** |
| Wave | radio | Sine / Triangle · **Sine** |
| Phase | float | 0 a 1000 · **0** |

### Exemplo real de Nível 3 — Motion Blur ✅
| Sub-aba | Tipo | Default |
|---|---|---|
| Tune | float | **1.00** |
| Position | switch | **On** |
| Scale | switch | **On** |
| Angle | switch | **On** |

> **Consulta rápida de qualquer efeito:** `guide.alightmotion.com/effects/nome-em-kebab-case`
> Ex.: `/effects/turbulent-displace`, `/effects/radial-repeat`, `/effects/gradient-map`

## 2.3 BLENDING & OPACITY ⚠️
**Caminho:** camada → *Blending & Opacity*

| Sub-aba / controle | Conteúdo |
|---|---|
| **Opacity** | 0–100%, com diamante de keyframe |
| **Blend Mode** | lista rolável: Normal, Dissolve · Darken, Multiply, Color Burn, Linear Burn, Darker Color · Lighten, Screen, Color Dodge, **Linear Dodge (Add)**, Lighter Color · Overlay, Soft Light, Hard Light, Vivid Light, Linear Light, Pin Light, Hard Mix · Difference, Exclusion, Subtract, Divide · Hue, Saturation, Color, Luminosity |
| **Mask** (no topo do painel) | None · Alpha · Alpha Invert · Luma · Luma Invert |

**Como a máscara funciona:** a camada **de cima**, dentro do **mesmo grupo**, recorta a(s) de baixo. Se as duas não estiverem agrupadas, não acontece nada. Fluxo: agrupar → selecionar a de cima → *Blending & Opacity* → *Mask* → escolher o modo.

## 2.4 COLOR & FILL ✅
**Caminho:** camada → *Color & Fill*

| Controle | Função |
|---|---|
| **Balde de tinta** | Liga/desliga o preenchimento. Elements vêm **sem fill**, mostrando a cor natural ✅ |
| **Seletor de cor** | HSV / RGB / HEX + **conta-gotas** |
| **Alpha** | Transparência da cor (keyframeável) |

Para recolorir mantendo o brilho original de um element, a recomendação oficial é o efeito **Colorize** em vez do fill. ✅

## 2.5 LAYER PARENT ✅
**Caminho:** camada filha → *Layer Parent* → escolher o pai na lista

- A própria camada não aparece na lista (não dá para parentear em si mesma) ✅
- **"None"** desparenteia
- Uma seta na timeline indica a relação
- ⚠️ **Posicione o playhead no frame certo ANTES de parentear** — o AM trava a relação espacial daquele instante ✅

## 2.6 TEXT ⚠️
**Caminho:** camada de texto → *Text* (ou duplo toque no texto)

| Sub-aba | Parâmetros |
|---|---|
| Conteúdo | campo de digitação |
| Fonte | lista + **fontes importadas** ✅ |
| Tamanho | pt/px |
| Alinhamento | esquerda / centro / direita / justificado |
| Espaçamento | tracking (entre letras) e leading (entre linhas) |
| Cor | via Color & Fill |

Animação por caractere é feita com **efeitos de texto**: *Text Progress*, *Text Transform*, *Text Randomizer*, *Text Spacing*.

## 2.7 VETOR / DESENHO ⚠️
**Caminho:** camada vetorial → *Edit* / ícone de nós

| Ação | Como |
|---|---|
| Mover nó | Arrastar o ponto |
| Curvar | Arrastar as alças bézier |
| Adicionar nó | Tocar sobre o traço |
| Remover nó | Selecionar → deletar |
| Fechar caminho | Ligar o último nó ao primeiro |
| Espessura / cor do traço | Efeito **Stroke Color** e **Stroke Taper** |
| Revelar desenhando | Efeito **Drawing Progress** |

## 2.8 ÁUDIO ✅
**Caminho:** camada de áudio (ou vídeo) → *Volume*

| Parâmetro | Função |
|---|---|
| Volume | Ganho, keyframeável (fade manual) |
| Fade in / out ⚠️ | Atalho de entrada e saída |
| Trim | Pontas da barra na timeline |
| **Extrair áudio** | Separa o áudio de um vídeo numa camada própria ✅ |

## 2.9 CÂMERA ✅
**Caminho:** camada de câmera selecionada

| Sub-aba | Parâmetros |
|---|---|
| Location | X · Y · **Z** |
| Rotation | ângulo |
| **View Angle** | graus na dimensão longa do projeto |
| **Zoom Distance** | distância em que uma camada do tamanho do projeto preenche a tela |
| **Focus Blur** | Focus distance · Depth of field · Blur strength |
| **Fog** | Color · Near distance · Far distance |

⚠️ Câmera **não tem escala**. Pinçar altera View Angle e Zoom Distance. Para profundidade real, mexa no **Z**. ✅

## 2.10 ELEMENT PROPERTIES ✅
**Caminho:** element selecionado → *Element Properties*

| Controle | Função |
|---|---|
| Campos de texto | Personaliza sem alterar o original |
| **Edit original Element** | Abre o original (muda em todos os projetos) |
| **Convert to Group** | Quebra o link, vira grupo editável |
| **Recreate Linked Project** | Só quando o original sumiu do aparelho |

---

# NÍVEL 4 — CURVE EDITOR ✅

**Caminho:** sub-aba com 2+ keyframes → ícone de curva → **role até o playhead ficar ENTRE os dois keyframes**

| Área | Conteúdo |
|---|---|
| Gráfico | Eixo X = tempo do projeto · Eixo Y = tempo remapeado. Inclinação = velocidade |
| Alças brancas | Bézier cúbica de 2 alças, arrastáveis |
| Presets laterais | ease-in, ease-out, ease-in-out, linear |
| Tipos avançados | **Bounce · Elastic · Cyclic · Random · Steps · Elastic Steps** |
| **⋯ (canto inferior direito)** | *Enable Overshoot* · *Copy Curve* · *Paste Curve* · **Paste Curve to All Keyframes** |

Com 3 keyframes existem 2 curvas independentes. Número de curvas = keyframes − 1. ✅

---

# TABELA "QUERO FAZER X → ONDE CLICO"

| Objetivo | Caminho completo |
|---|---|
| Mover objeto na tela | Camada → Move & Transform → **Location** → ◆ |
| Aumentar/diminuir | Camada → Move & Transform → **Scale** → ◆ |
| Girar | Camada → Move & Transform → **Rotation** → ◆ |
| Mudar centro de giro | Camada → Move & Transform → **Pivot** |
| Fade in/out | Camada → Blending & Opacity → **Opacity** → ◆ (ou efeito *Fade In/Out*) |
| Suavizar a animação | Sub-aba com 2 kf → **ícone de curva** → preset ou alças |
| Bounce / mola | Curve Editor → tipo avançado **Bounce** ou **Elastic** |
| Passar do valor final | Curve Editor → **⋯ → Enable Overshoot** |
| Reaproveitar a mesma curva | Curve Editor → **⋯ → Copy Curve** → outro par → **Paste Curve** |
| Green screen | Camada → Effects → (+) → **Chroma Key** → Key Color com conta-gotas |
| Recortar por outra camada | Agrupar as duas → a de cima → Blending & Opacity → **Mask → Alpha/Luma** |
| Borrar | Effects → (+) → **Gaussian Blur** |
| Brilho / glow | Effects → (+) → **Glow** (ou blend **Add**) |
| Tremor de câmera | Effects → (+) → **Auto-Shake** ou **Oscillate** |
| Motion blur da cena toda | **Camada de câmera** → Effects → **Motion Blur** ✅ |
| Um objeto seguir outro | Camada filha → **Layer Parent** → escolher o pai |
| Rodar sem girar junto | Effects no filho → **Parenting Helper** → Rotation: **Locked** ✅ |
| Objeto seguir um traço | Filho → Effects → **Move Along Path** (pai = vetor com o caminho) ✅ |
| Animar X e Y em tempos diferentes | Criar **Null**, parentear, animar um eixo no null ✅ |
| Loop de um trecho | Element → ⚙ → **Re-timing: Loop** + marcas de intro/outro ✅ |
| Marcar intro/outro | Long-press no **tempo atual** → escolher a marca ✅ |
| Salvar para reusar | Grupo → **⋯ → Save to My Elements** ✅ |
| Compartilhar | Export → **Project Package** → Alight link + QR ✅ |
| Deixar o preview leve | View Options → **Low Quality Preview** ✅ |
| Ver pela câmera | View Options → **Active Camera View** ✅ |
| Navegar o canvas | **🔍 lupa** → 2 dedos (sem camada selecionada) ✅ |

---

# OS 5 ERROS DE NAVEGAÇÃO MAIS COMUNS

1. **Diamantes apagados** — você está na sub-aba errada. Os keyframes pertencem a outra propriedade. ✅
2. **Curva não aparece** — o playhead não está entre dois keyframes daquela aba. ✅
3. **Máscara não funciona** — as camadas não estão no mesmo grupo, ou a máscara não está por cima.
4. **Camada pula ao parentear** — o playhead estava num frame onde a relação espacial não era a desejada. ✅
5. **2 dedos transformam a camada em vez de navegar** — tem camada selecionada. Desselecione ou ligue o Pan & Zoom Mode. ✅
