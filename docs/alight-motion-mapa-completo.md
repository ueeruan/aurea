# ALIGHT MOTION — MAPA COMPLETO DO EDITOR
### Camadas, keyframes, curvas, efeitos, painéis e cada parâmetro
*Compilado a partir da documentação oficial (support.alightmotion.com + guide.alightmotion.com) e do comportamento do app. Referência para AM 5.x (Android/iOS).*

---

## LEGENDA DE CONFIABILIDADE

| Marca | Significado |
|---|---|
| ✅ | Confirmado na documentação oficial da Alight Motion |
| 🔧 | Comportamento do app, estável entre versões |
| ⚠️ | O nome do botão / posição na tela **muda entre versões e entre telefone / tablet**. Confira no seu build |

O AM avisa isso na própria documentação: *"o menu no seu aparelho pode parecer diferente — varia entre versões e dispositivos"*. ✅

---

# PARTE 0 — MODELO MENTAL (leia isto primeiro)

O Alight Motion não é um editor de vídeo com trilhas. É um **compositor de camadas**, igual After Effects. A hierarquia interna é:

```
PROJETO (resolução, fps, duração, fundo)
 └── CAMADAS (empilhadas; a de cima aparece na frente)
      ├── PROPRIEDADES DE TRANSFORM (posição, escala, rotação, skew, pivô)
      ├── PROPRIEDADES DE APARÊNCIA (opacidade, blend mode, cor/fill, máscara)
      ├── PILHA DE EFEITOS (ordem importa — de cima para baixo)
      │    └── cada efeito tem seus próprios PARÂMETROS
      └── PARENTING (herda transform de outra camada)

Qualquer propriedade numérica → aceita KEYFRAMES
Entre dois keyframes → existe UMA CURVA DE EASING
```

**As 4 regras que explicam 90% das dúvidas:**

1. **Tudo que tem número pode ter keyframe.** Inclusive parâmetros de efeito, cor, opacidade, e valores dentro de efeitos. ✅ *("Keyframe animation available for all settings")*
2. **Entre cada par de keyframes existe uma curva independente.** N keyframes = N−1 curvas. ✅
3. **A ordem da pilha de efeitos altera o resultado.** Blur → Glow ≠ Glow → Blur.
4. **Grupo = precomposição.** Agrupar não é só organizar: cria um novo "container de render" com transform, opacidade, blend e efeitos próprios.

---

# PARTE 1 — TELA INICIAL (fora do editor)

## 1.1 Abas principais ⚠️

| Aba | O que é |
|---|---|
| **Projects** | Seus projetos. Long-press seleciona → Duplicar / Deletar / Exportar |
| **Elements** | Componentes reutilizáveis (ver Parte 10). Sub-aba dentro de Projects ✅ |
| **Templates / Presets** | Templates prontos e presets salvos (a partir do AM 5.0) |
| **Discover / Community** | Conteúdo baixável, pacotes de elementos |
| **Settings / Perfil** | Conta, assinatura, idioma, cache |

## 1.2 Botão (+) → criar novo

Ao criar, você define: ⚠️

| Parâmetro | Opções típicas | Observações |
|---|---|---|
| **Tipo** | Project / Element ✅ | Element = componente reutilizável |
| **Nome** | texto | Editável depois tocando no nome no topo ✅ |
| **Proporção** | 16:9, 9:16, 4:3, 1:1, 4:5, 21:9, custom | |
| **Resolução** | 480p / 720p / 1080p / 1440p / 4K | Quanto maior, mais pesado o preview |
| **Frame rate** | 24 / 25 / 30 / 60 fps | **Escolha antes de começar.** Mudar depois desloca timing |
| **Cor de fundo** | sólida / transparente | Em Element, o recomendado oficial é **Transparente** ✅ |

> 🔧 **Regra de ouro:** o fps do projeto define a grade de tempo dos keyframes. Projeto 30fps = keyframes caem de 1/30 em 1/30 de segundo.

---

# PARTE 2 — MAPA DA TELA DO EDITOR

Regiões da interface (telefone, retrato): ⚠️

```
┌─────────────────────────────────────────┐
│ [←] Nome do Projeto  [⚙] [↶][↷] [⤓ Exportar] │  ← barra superior
├─────────────────────────────────────────┤
│                                         │
│            ÁREA DE PREVIEW              │  ← canvas + handles da camada selecionada
│         (pinça = transformar)           │
│                                         │
├─────────────────────────────────────────┤
│  [👁 View Options] [🔍 Pan&Zoom]         │  ← opções de visualização
├─────────────────────────────────────────┤
│  ⏮ ⏯ ⏭    00:00.00 / 00:10.00           │  ← transporte + tempo atual
├─────────────────────────────────────────┤
│ ▏▏▏▏▏ RÉGUA DE TEMPO ▏▏▏▏▏▏▏▏▏▏         │
│ ═══════════ Camada 1 ═══════════        │
│ ═══════ Camada 2 ═══════                │  ← TIMELINE (playhead fixo ao centro
│ ═══ Camada 3 ═══                        │     na maioria dos builds)
├─────────────────────────────────────────┤
│  [+ Adicionar]  [ferramentas da camada] │  ← muda conforme a seleção
└─────────────────────────────────────────┘
```

## 2.1 Barra superior ⚠️

| Controle | Abre / faz |
|---|---|
| **←** | Volta para a lista de projetos (salva automático) |
| **Nome do projeto** | Toque para renomear ✅ |
| **⚙ Engrenagem** | **Configurações do projeto**: resolução, frame rate, fundo, duração. Em Element abre também Precompose e Re-timing ✅ |
| **↶ / ↷** | Undo / Redo (multi-nível) |
| **⋯ (três pontos)** | Menu overflow. Em camada selecionada: *Save to My Elements*, duplicar, etc. ✅ |
| **⤓ Export** | Painel de exportação (Parte 12) |

## 2.2 Painel View Options ✅

Não altera o render final, só o que você vê:

| Opção | Função |
|---|---|
| **Active Camera View** | Ver através da câmera ativa. Sem isso você só vê o wireframe da câmera ✅ |
| **Preview Zoom** | Zoom por toque (além da pinça) ✅ |
| **Pan and Zoom Mode** (🔍 lupa) | Liga gestos de 2 dedos para navegar o canvas. **Só funciona com nenhuma camada selecionada** — com camada selecionada, 2 dedos = transformar a camada ✅ |
| **Low Quality Preview** | Renderiza o preview em resolução baixa. Salva-vidas em projeto pesado ✅ |
| **Checkerboard / fundo** | Mostra transparência |
| **Grid / guias / safe area** | Auxílio de alinhamento ⚠️ |

Gestos do Pan & Zoom: ✅
- Pinçar afastando = zoom in
- Pinçar juntando = zoom out
- **Duplo toque com 2 dedos** = recentraliza e volta a 100%. Repetir = volta ao zoom anterior
- Ligar Active Camera View recentraliza; ao desligar, volta ao zoom anterior

---

# PARTE 3 — MENU (+) ADICIONAR: TODAS AS ABAS

O botão (+) no canto inferior abre o menu de adição, dividido em abas. ⚠️ (nomes variam)

| Aba | Conteúdo | Vira que tipo de camada |
|---|---|---|
| **Media / Photo & Video** | Galeria do aparelho, arquivos | Camada de vídeo ou imagem (raster) |
| **Shape** | Biblioteca de formas vetoriais (círculo, retângulo, estrela, seta, balão, floco, gota…) | Camada vetorial |
| **Text** | Editor de texto | Camada de texto (vetorial) |
| **Drawing / Freehand / Vector** | Desenho à mão livre e caneta bézier | Camada vetorial |
| **Audio** | Músicas, gravações, áudio extraído | Camada de áudio |
| **Object** | **Camera**, **Null** ✅ | Objetos invisíveis de rig |
| **Elements** | Seus elements salvos + *Download Elements* ✅ | Camada de element (linkada) |
| **Presets / Templates** | Presets salvos e importados (AM 5.0+) | Aplica pilha de efeitos/animação |

> ✅ Câmera e Null ficam na aba **Object** no iPhone/Android; no iPad, a aba *Object* é direta.

---

# PARTE 4 — TIPOS DE CAMADA E O QUE CADA UMA TEM

| Tipo | Raster/Vetor | Propriedades exclusivas |
|---|---|---|
| **Vídeo** | Raster | Trim in/out, velocidade, **Time Remap**, volume, extrair áudio ✅, frame blending |
| **Imagem** | Raster | Duração livre |
| **Forma / Vetor** | Vetor | Edição de nós (bézier), fill, stroke, espessura, cantos |
| **Texto** | Vetor | Fonte, tamanho, alinhamento, tracking, leading, cor, stroke |
| **Desenho livre** | Vetor | Traço com largura variável; combina com *Drawing Progress* |
| **Áudio** | — | Volume, fade, trim, sem transform visual |
| **Grupo** | Container | Precompõe filhos; transform/opacidade/blend/efeitos próprios |
| **Element** | Container linkado | *Element Properties*, campos de texto editáveis, re-timing ✅ |
| **Câmera** | Objeto | Z, View Angle, Zoom Distance, Focus Blur, Fog ✅ |
| **Null** | Objeto invisível | Só transform. Não renderiza na exportação ✅ |

**Sobre vetor vs raster:** efeitos marcados como *Raster* no navegador de efeitos rasterizam a camada. Efeitos vetoriais (*Stroke Color*, *Drawing Progress*, *Stroke Taper*) só funcionam em camada vetorial.

---

# PARTE 5 — PAINEL DA CAMADA: CADA BOTÃO E O QUE ELE ABRE

Selecionando uma camada, a barra inferior vira o painel de edição. Este é o coração do app.

## 5.1 MOVE & TRANSFORM ✅

O painel mais usado. Abre sub-abas, uma por propriedade:

| Sub-aba | Parâmetros | Notas |
|---|---|---|
| **Location / Position** | X, Y **e Z** ✅ | Z só tem efeito visível com câmera na cena. Pixels |
| **Scale / Size** | Largura, Altura, link de proporção | Em % ou px ⚠️ |
| **Rotation / Angle** | Graus | Aceita valores >360 para múltiplas voltas |
| **Skew** | Inclinação ✅ | Cisalhamento horizontal/vertical |
| **Pivot / Anchor Point** | X, Y ✅ | Ponto em torno do qual gira e escala. Padrão = centro |

**Cada sub-aba tem sua própria linha de keyframes.** Estar na aba errada é o erro nº1: os diamantes aparecem apagados e sem borda quando pertencem a outra propriedade. ✅

**Truque oficial:** para animar X e Y com timings diferentes (ou X-scale e Y-scale independentes), parenteie a camada a um **Null** e anime um eixo no null. ✅

## 5.2 BLENDING & OPACITY ⚠️

| Controle | Valores |
|---|---|
| **Opacity** | 0–100%, keyframeável |
| **Blend Mode** | lista abaixo |
| **Mask** | modo de máscara dentro do grupo ✅ (ver Parte 8) |

**Blend modes disponíveis** (conjunto padrão tipo Photoshop): ⚠️

| Família | Modos |
|---|---|
| Normal | Normal, Dissolve |
| Escurecer | Darken, Multiply, Color Burn, Linear Burn, Darker Color |
| Clarear | Lighten, Screen, Color Dodge, **Linear Dodge (Add)**, Lighter Color |
| Contraste | Overlay, Soft Light, Hard Light, Vivid Light, Linear Light, Pin Light, Hard Mix |
| Comparativos | Difference, Exclusion, Subtract, Divide |
| Componentes | Hue, Saturation, Color, Luminosity |

> 🔧 Para brilho/luz: **Add (Linear Dodge)** e **Screen**. Para sombra/textura: **Multiply**. Para contraste sem lavar: **Overlay** ou **Soft Light**.

## 5.3 COLOR & FILL ✅

| Controle | Função |
|---|---|
| **Paint can (balde)** | Liga/desliga o preenchimento. Elements vêm sem fill por padrão, mostrando a cor natural ✅ |
| **Color picker** | HSV / RGB / HEX, com conta-gotas |
| **Alpha** | Transparência da cor |

Alternativa oficial ao fill em elements: aplicar o efeito **Colorize**, que troca o matiz mantendo o brilho original. ✅

## 5.4 EFFECTS (navegador de efeitos) ✅

- Busca por nome
- Filtro por **tags** (as tags reais aparecem na ficha de cada efeito: *Essentials, Blur, Raster, Compositing, Matte/Mask/Key, Green Screen, Transform, Move/Transform, Shake, Auto, Property Adjust, Camera, Layer Parenting*…)
- Botão **Guide** na lateral de cada efeito → abre a documentação oficial daquele efeito ✅
- Favoritos / recentes
- Empilhamento: vários efeitos por camada, **ordem editável por arrasto**
- **Copiar e colar efeitos** entre camadas ✅
- Marca **"(Compatibility)"**: quando um efeito é atualizado, projetos antigos mantêm a versão antiga. Para usar a nova, delete e aplique de novo ✅

## 5.5 LAYER PARENT ✅
Dropdown com todas as outras camadas. "None" desparenteia. Ver Parte 9.

## 5.6 OUTROS BOTÕES ⚠️

| Botão | Função |
|---|---|
| **Trim / Split** | Corta a camada no playhead. Ao dividir um pai, **os filhos continuam com a metade esquerda** ✅ |
| **Duplicate** | Duplica com efeitos e keyframes |
| **Delete** | Remove |
| **Copy / Paste Style** | Cola só a aparência (fill, stroke, efeitos), não o conteúdo ✅ |
| **Speed / Time Remap** | Velocidade fixa ou remapeamento com keyframes (AM 5.0+) |
| **Volume / Audio** | Ganho, fade in/out, extrair áudio de vídeo ✅ |
| **Element Properties** | Só em elements: editar textos, *Edit original Element*, *Convert to Group* ✅ |
| **Vector Edit** | Só em vetor: mover nós, alças bézier, adicionar/remover pontos |
| **Text Options** | Fonte (inclusive fontes importadas ✅), tamanho, alinhamento, tracking, leading |

---

# PARTE 6 — TIMELINE EM DETALHE

| Elemento | Comportamento |
|---|---|
| **Playhead** | Linha branca vertical. Na maioria dos builds fica fixo e a timeline rola |
| **Régua de tempo** | Escala em segundos/frames; pinça horizontal = zoom temporal |
| **Barras de camada** | Arrastar = mover no tempo; pontas = trim; arrastar vertical = reordenar |
| **Tempo atual (display)** | **Long-press abre o menu de retiming/marcas** ✅ |
| **Bookmarks** | Marcadores de tempo para navegação rápida ✅ |
| **Marcas de retiming** | Indicadores **amarelos** de intro/outro em Elements ✅ |
| **Linha de keyframes** | Diamantes da propriedade atualmente selecionada |
| **Grupos** | Expandem/colapsam mostrando os filhos |

**Diamantes de keyframe — leitura visual oficial:** ✅

| Aparência | Significado |
|---|---|
| **Branco sólido com borda escura** | Keyframe da propriedade **selecionada agora** (editável) |
| **Apagado, sem borda** | Keyframe de **outra** propriedade. Troque de aba antes de editar |

---

# PARTE 7 — KEYFRAMES E CURVAS (o núcleo do AM)

## 7.1 Como criar ✅

1. Selecione a camada
2. Abra a propriedade (ex.: *Move & Transform → Location*)
3. Posicione o playhead
4. Toque no **diamante (◆)** → cria keyframe com o valor atual
5. Mova o playhead
6. Altere o valor → **o segundo keyframe é criado sozinho**

Mínimo para animar: **2 keyframes**. ✅

## 7.2 O que aceita keyframe
Praticamente tudo: posição, escala, rotação, skew, pivô, opacidade, **cor**, e **qualquer parâmetro numérico de qualquer efeito**. ✅

## 7.3 O CURVE EDITOR (editor de curvas) ✅

Passo a passo oficial:

1. Tenha 2+ keyframes na propriedade
2. Abra o **Curve Editor** (ícone de curva no painel)
3. **Role a timeline até o playhead ficar ENTRE os dois keyframes** — sem isso a curva não aparece
4. Ajuste

Duas formas de ajustar:
- **Presets** (botões laterais): ease-in, ease-out, ease-in-out, linear etc.
- **Alças brancas** (bézier cúbica de 2 alças), arrastadas manualmente

**Como ler o gráfico:** ✅
- Eixo **X** = tempo original do projeto
- Eixo **Y** = tempo remapeado aplicado à animação
- Esquerda = início, direita = fim
- **A inclinação é a velocidade.** Trecho íngreme = rápido; trecho plano = lento/parado

### Tipos de easing avançados ✅

Além da bézier cúbica padrão:

| Tipo | Comportamento |
|---|---|
| **Bounce** | Cai em direção ao valor final como sob gravidade e quica. A "gravidade" aponta para o valor final, mesmo que não seja direção física (funciona até em cor) |
| **Elastic** | Mola esticada: passa do valor final e assenta |
| **Cyclic** | Repete a progressão entre inicial e final; vai-e-volta ou loop de mão única |
| **Random** | Aleatoriedade controlada, tipo movimento browniano |
| **Steps** | Avança em degraus discretos (relógio, stop-motion). Mais controlável que o efeito *Time Quantization* e aplicável por propriedade |
| **Elastic Steps** | Steps, mas com quique elástico em cada degrau |

### Overshoot ✅
Por padrão a curva fica presa dentro das linhas pontilhadas (não ultrapassa os valores dos keyframes vizinhos).
**Ligar:** Curve Editor → **⋯** (canto inferior direito) → *Enable Overshoot*.
Aí as alças podem passar das pontilhadas. Serve para antecipação e recuo. Desliga no mesmo menu.

### Copiar e colar curvas ✅
Curve Editor → **⋯** → *Copy Curve* → vá para outro par de keyframes (mesma camada ou outra) → Curve Editor → **⋯** → *Paste Curve*.
Existe também **Paste Curve to All Keyframes**: cola em todos os pares daquela propriedade, sem afetar outras propriedades/camadas.

### Múltiplos keyframes ✅
Com 3 keyframes existem 2 curvas independentes. **Número de curvas = keyframes − 1.**

---

# PARTE 8 — GRUPOS, MÁSCARAS E PRECOMPOSIÇÃO

## 8.1 Grupo
Selecione 2+ camadas → botão de agrupar. O grupo passa a ter transform, opacidade, blend mode e pilha de efeitos próprios, aplicados ao conjunto já composto.

Use grupo quando quiser:
- Animar várias camadas como uma só
- Aplicar um efeito ao resultado combinado (e não a cada camada)
- Limitar máscara a um subconjunto

## 8.2 Máscara ⚠️
No AM a máscara é **relação entre camadas dentro de um grupo**: a camada de cima recorta a(s) de baixo.

Fluxo: camada máscara **acima** da camada a mascarar, ambas no mesmo grupo → selecione a camada de cima → **Blending & Opacity → Mask** → escolha o modo.

| Modo | O que usa | Resultado |
|---|---|---|
| **None** | — | Sem máscara |
| **Alpha** | Transparência | Aparece só onde a máscara é opaca |
| **Alpha Invert** | Transparência | Aparece só onde a máscara é transparente |
| **Luma** | Brilho | Branco revela, preto esconde |
| **Luma Invert** | Brilho | Preto revela, branco esconde |

Alternativas de recorte:
- **Chroma Key** — remove uma cor (green screen) ✅
- **Luma Key** — remove por brilho
- **Solid Matte / Matte Choker / Feather / Smooth Edges** — tratamento de borda do matte

## 8.3 Precomposição
No AM, grupo e element funcionam como precomp. O controle explícito de resolução de render está nas **configurações de Element** (Parte 10).

---

# PARTE 9 — PARENTING E NULL OBJECTS ✅

## 9.1 Como funciona
- Selecione a camada **filha** → botão **Layer Parent** → escolha o pai
- Uma seta indica a relação na timeline
- **"None"** desparenteia

Regras:
- Mover o pai move o filho
- Girar o pai gira o filho **em torno do pivô do pai**
- Escalar o pai escala o filho proporcionalmente
- Relação de **mão única**: mexer no filho não afeta o pai
- **Ordem na timeline não influencia** o parenting
- Cada camada tem **no máximo 1 pai**; um pai pode ter filhos ilimitados
- Cadeias são permitidas (filho vira pai de outro) → rig de personagem, braço de guindaste

⚠️ **Armadilha oficial:** a posição do playhead **na hora de parentear importa**. O AM compensa automaticamente a transform do filho para ele não pular, travando a relação espacial daquele instante. Se pai ou filho já tem animação, **role o playhead até o frame onde a relação está correta antes de parentear**. ✅

## 9.2 Efeitos e parenting ✅
Efeitos no pai que mexem em posição/rotação/escala **também afetam os filhos**. São os efeitos com a tag **Layer Parenting**: *Bend*, *Oscillate*, *Auto-Shake*, *Swing*.

**Move Along Path** aplicado ao **filho** faz ele percorrer o **contorno do pai**. Combinado com desenho vetorial, cria caminhos curvos. ✅

## 9.3 Efeito PARENTING HELPER ✅

Controla como o filho reage à rotação/escala do pai. Modos independentes para **Rotation** e **Scale**:

| Modo | Efeito |
|---|---|
| **Normal** | Comportamento padrão |
| **Locked** | O filho é levado pela rotação/escala do pai mas **não gira nem muda de tamanho** (ex.: cadeirinhas de roda-gigante) |
| **Weighted** | Grau de herança ajustável. 200% = o filho gira o dobro do pai. Use *Rotation Weight* / *Scale Weight* |

Extra: **Auto Rotate** — aplica rotação automática baseada no movimento horizontal/vertical do pai. Serve para rodas. Se a roda não preenche a camada inteira, use **Radius Adjust** para corrigir (roda ocupando 70%, 30% transparente → Radius Adjust = −30). ✅

## 9.4 Null Objects ✅
Camada invisível (só wireframe no editor, **não sai na exportação**). Adicionar: **(+) → Object → Null**.

Usos oficiais:
- Separar eixos: animar X e Y da câmera na própria câmera e **Z no null pai**
- Escala independente em X e Y
- Aplicar um efeito de movimento uniformemente a várias camadas (aplica no null, todas seguem)

**Dica oficial:** keyframes de posição simultâneos no pai e no filho, ambos com easing, produzem **arco de movimento curvo** no filho. ✅

---

# PARTE 10 — ELEMENTS (componentes reutilizáveis) ✅

## 10.1 O que são
Componentes reutilizáveis parecidos com grupos, guardados fora dos projetos, na sub-aba **Elements** dentro de **Projects**. Ao usar num projeto, o projeto **mantém o link** com o original: editar o original altera todos os projetos que o usam.

## 10.2 Element Properties
Selecione o element → botão **Element Properties**:
- Campos de texto customizáveis (se o element contiver texto) — permite reaproveitar títulos sem alterar o original
- **Edit original Element** — edita o original (reflete em todos os projetos)
- **Convert to Group** — quebra o link e vira grupo editável livremente

## 10.3 Configurações de Element (⚙ dentro do element)

| Parâmetro | Função |
|---|---|
| **Resolution** | Vale quando *Precompose = Fixed Resolution* ou ao exportar o element direto. Em *Dynamic*, serve só para posicionamento |
| **Frame Rate** | Só na exportação direta e no preview. **Ignorado quando embarcado num projeto** |
| **Background** | Cor de fundo. Recomendação oficial: **Transparente** |

### Precompose
| Opção | Comportamento |
|---|---|
| **Dynamic Resolution** | Resolução ajustada automaticamente para qualidade/performance; **partes fora da tela não são renderizadas** |
| **Fixed Resolution** | Renderiza exatamente na resolução definida, **incluindo o que está fora da tela**. Necessário para *Polar Coordinates*, *Pinch/Bulge*, *Magnify Background* e afins |

### Re-timing (como o element reage à mudança de duração)
Aplica-se **ao miolo**, entre as marcas de intro e outro:

| Opção | Comportamento |
|---|---|
| **Freeze** | Congela o último frame do miolo (ou corta se for mais curto) |
| **Stretch** | Estica (mais lento) ou comprime (mais rápido) o miolo |
| **Loop** | Repete o miolo; o último loop pode ser cortado no meio |
| **Loop & Stretch** | Repete e depois estica os loops inteiros para fechar exato, sem loop pela metade |
| **Blank** | Ignora intro/outro, duração fixa; sobra vira transparente |
| **Off** | Só aparece ao converter element em grupo; volta ao comportamento normal de grupo |

### Marcas de intro/outro (retiming marks) ✅
Role o playhead → **long-press no tempo atual** → escolha a ação.
As marcas aparecem como **indicadores amarelos**. A da esquerda encerra o **intro**, a da direita inicia o **outro**. O trecho entre elas é o que loopa/estica/congela. Intro e outro rodam sempre na velocidade original.

## 10.4 Criar, gerenciar, compartilhar ✅
- **Criar do zero:** (+) na tela inicial → *Element*
- **De um grupo existente:** selecione o grupo → **⋯** → *Save to My Elements*
- **Renomear:** abrir e tocar no nome
- **Deletar/duplicar:** long-press na aba Elements
- **Compartilhar:** abrir → **Export** → *Project Package* → gera **Alight link + QR code**
- **Baixar:** (+) → *Elements* → *Download Elements*

## 10.5 Element faltando ✅
Se o original não existe no aparelho, o projeto abre com aviso e usa a **cópia embutida**. Em Element Properties aparecem duas saídas: **Convert to Group** ou **Recreate Linked Project** (recria e salva na sua biblioteca).

---

# PARTE 11 — CÂMERA (guia oficial completo) ✅

## 11.1 O que dá para fazer
Simular profundidade, focus blur e fog; animar posição/rotação/tamanho por keyframes; ter **várias câmeras e cortar entre elas**; parentear camadas à câmera (overlays/HUD) e parentear a câmera a camadas (tracking).

## 11.2 Adicionar
**(+) → Object → Camera.** Dica oficial: tenha algumas camadas na cena antes, senão não dá para ver o efeito.

## 11.3 Active Camera View
Ao adicionar, você vê só o **wireframe**. Ligue **Active Camera View** no painel View Options para olhar através dela. É só visualização: **não afeta a exportação**.

## 11.4 Eixo Z
Além de X e Y, a câmera anda para frente/trás no **Z**, animável. Colocando camadas em Zs diferentes você cria travelling 3D e **parallax**.

## 11.5 Zoom Distance × View Angle
Propriedades ligadas: mexer numa muda a outra.

| Parâmetro | Definição |
|---|---|
| **Zoom Distance** | Distância na qual uma camada do tamanho do projeto preenche a tela. Maior = mais zoom. Ex.: projeto 1080p, zoom 1000 → camada de 1080px de altura a 1000px de distância preenche a tela na vertical |
| **View Angle** | Quanto do projeto a câmera enxerga, em graus, medido na dimensão longa. Junto com o aspect ratio define o **campo de visão** |

## 11.6 Escala
**A câmera não tem escala.** Pinçar no preview ou mexer em Scale altera **Angle** e **Zoom Distance**. Os números na aba Scale representam a largura/altura do wireframe (a área do plano Z=0 visível).
Para sensação real de profundidade, mexa no **Z em Location**, não na escala.
Diferenças importantes: **efeitos de camada não afetam a câmera** e **câmeras com pai não são afetadas pelo pai** em escala.

## 11.7 Focus Blur
| Parâmetro | Função |
|---|---|
| **Focus distance** | Distância (no Z) onde a câmera está focada |
| **Depth of field** | Faixa em torno do foco que fica nítida |
| **Blur strength** | Intensidade do desfoque fora de foco |

Tudo relativo à posição da câmera: mover a câmera faz camadas entrarem e saírem de foco.

## 11.8 Fog
| Parâmetro | Função |
|---|---|
| **Color** | Cor da névoa |
| **Near distance** | Limite próximo; camadas mais próximas que isso ficam totalmente visíveis |
| **Far distance** | Limite distante; camadas além disso ficam totalmente encobertas |

Entre near e far a névoa encobre parcialmente. Também é relativo à câmera.

## 11.9 Múltiplas câmeras
- A câmera **mais alta** na timeline é a **Active Camera**
- A **mais baixa** é a **Default Camera**
- Desligar a visibilidade da ativa passa o controle para a próxima
- Sem nenhuma câmera na timeline, a **Default Camera** vale e se estende infinitamente

## 11.10 Efeitos em câmera
- **Motion Blur** numa câmera equivale a aplicar em todas as camadas visíveis por ela — jeito prático de ligar/desligar o blur da cena inteira (desligue para editar, ligue para exportar)
- **Auto-Shake** e **Oscillate** simulam movimento realista de câmera
- Efeitos que mexem em escala **não** afetam câmeras
- Filtre pela tag **Camera** no navegador de efeitos para ver o que é compatível

## 11.11 Parenting com câmera
- Camada parenteada à câmera **segue a câmera** → HUD, viewfinder, legendas fixas
- Câmera parenteada a uma camada **segue aquela camada**

---

# PARTE 12 — EXPORTAÇÃO ✅

| Formato | Uso |
|---|---|
| **MP4 (vídeo)** | Saída padrão |
| **GIF** | Animação curta em loop |
| **PNG sequence** | Sequência de frames (mantém alfa) |
| **Still image** | Frame único |
| **Project Package** | Compartilhamento: gera **Alight link + QR code** |

Notas:
- Free exporta **com marca d'água**; a assinatura remove ✅
- Resolução e fps da exportação seguem o projeto
- Para transparência: PNG sequence
- Exportar **project package** é a forma correta de fazer backup/migrar de versão

---

# PARTE 13 — SISTEMA DE EFEITOS

## 13.1 Como funciona
- Mais de **160 blocos de efeito** combináveis ✅
- Aplicados **por camada** (ou por grupo/câmera), em **pilha ordenada**
- **Todos os parâmetros aceitam keyframe**
- Entrada numérica precisa por teclado (AM 5.0+)
- Cada efeito tem botão **Guide** no app, que abre a ficha oficial ✅

## 13.2 Tipos de controle de parâmetro ✅
(vistos nas fichas oficiais)

| Ícone/tipo | Controle |
|---|---|
| `spinner` | Número inteiro |
| `spinner-float` | Número decimal |
| `spinner-angle` | Ângulo em graus |
| `switch` | Liga/desliga |
| `radio` | Escolha entre opções |
| `color` | Seletor de cor + conta-gotas |
| point / path / gradient | Posição no canvas, caminho vetorial, gradiente |

## 13.3 CONSULTA DIRETA DE QUALQUER EFEITO
A ficha oficial de cada efeito, **com tabela de parâmetros, range e valor default**, fica em:

```
https://guide.alightmotion.com/effects/<nome-do-efeito-em-kebab-case>
```

Exemplos: `/effects/chroma-key`, `/effects/gaussian-blur`, `/effects/oscillate`, `/effects/turbulent-displace`

## 13.4 LISTA COMPLETA DOS EFEITOS (163)

### 3D e formas geométricas
360º Reorient Sphere · 360º Viewer · Box · Cube · Cylinder · Ellipsoid · Hexagonal Prism · Hollow Box · Octahedron · Pyramid · Star Polyhedron · Star Prism · Three-axis Cross · Torus · Tunnel · Ribbon · Raster Extrude · Curl

### Desfoque e nitidez
Box Blur · Precise Box Blur · Gaussian Blur · Directional Blur · Lens Blur · Inner Blur · Mask Blur · Motion Blur · Spin Blur · Zoom Blur · Sharpen · Unsharp Mask

### Rastros / streaks
Linear Streaks · Spin Streaks · Zoom Streaks

### Cor e correção
Brightness/Contrast · Exposure/Gamma · Highlights and Shadows · Color Temperature · Color Tune · Colorize · Hue Shift · Saturation/Vibrance · Invert · Posterize · Threshold · Gradient Map · Palette Map · Spectral Map · Replace Color · Spot Color · Hot Color · Channel Remap (HSV) · Channel Remap (RGB) · Iridescence

### Key, matte e máscara
Chroma Key · Luma Key · Solid Matte · Matte Choker · Feather · Smooth Edges · Roughen Edges · Fill Behind · Copy Background · Magnify Background

### Distorção e warp
Bend · Pinch/Bulge · Inner Pinch/Bulge · Spherize · Squeeze · Stretch Axis · Stretch Segment · Swirl · Wave Warp · Turbulence · Turbulent Displace · Fractal Warp · Displacement Map · Polar Displacement Map · Polar Coordinates · Bump Map · Glass · Omino Glass · Omino Diffusion+ · Mirror · Kaleidoscope · Offset · Flip Layer · Random Displacement

### Geradores (desenham conteúdo novo)
Clouds · Noise · Block Noise · Checker · Grid · Stripes · Dots · Contour Lines · Contour Strips · Contour Gradient · Four-color Gradient · Gradient Overlay · Solid Color · Lightning · Electric Edges · Lens Flare · Rays · Radial Rays · Starfield · Simple Starfield · Voronoi Cells · Fractal Ridges · Heart · Star · Hexagon Array · Hexagon Tiling

### Estilização
Glow · Soft Glow · Light Glow · Dark Glow · Edge Glow · Inner Glow · Long Shadow · Radial Shadow · Smooth Bevel · Stroke Color · Stroke Taper · Find Edges · Mosaic · Pixelate · Halftone Dots · Halftone Lines · CMYK Halftone Dots · RGB Split · Tiles · Tile Rotate · Tile Shift · Hexagon Tile Rotate · Hexagon Tile Shift · Vignette

### Repetição / arrays
Repeat · Linear Repeat · Radial Repeat · Grid Repeat · Scatter Repeat · Repeat Along Path · Echo Keyframes

### Movimento automático (afetam transform)
Auto-Shake · Oscillate · Swing · Spin · Random Jitter · Pulse Size · Pulse Opacity · Move Along Path · Text Transform · **Parenting Helper**

### Tempo, transição e reveal
Time Quantization · Blink · Flicker · Dissolve · Block Dissolve · Fade In/Out · Wipe · Radial Wipe · Drawing Progress · Glow Scan · Circular Ripple

### Texto e dados
Text Progress · Text Randomizer · Text Spacing · Text Transform · Count Up/Down · Timecode

---

# PARTE 14 — FICHAS COMPLETAS (exemplos oficiais de como ler um efeito)

## 14.1 CHROMA KEY ✅
*Tags: Compositing, Essentials, Green Screen, Key, Mask, Matte, Matte/Mask/Key, Raster*
Deixa transparentes todos os pixels parecidos com uma cor.

| Parâmetro | Tipo | Descrição | Range / Default |
|---|---|---|---|
| **Key Color** | color | Cor a remover. Cores similares somem conforme o Threshold | — |
| **Threshold** | float | Quanto um pixel pode diferir da Key Color e ainda sumir. **É ~4× mais sensível a matiz/saturação do que a brilho** | 0 a 1; **default 0.1** |
| **Feather** | float | Suavidade da borda. Perto de zero a borda é dura; alto pode deixar franja colorida. Use com Defringe | 0.01 a 0.75; **default 0.05** |
| **Defringe** | switch | Remove resíduo da cor-chave nos pixels semitransparentes da borda | **default off** |
| **Invert** | switch | Inverte: mantém só a cor-chave | **default off** |

**Uso oficial:** aplique em vídeo/imagem, escolha a Key Color **com o conta-gotas**, ajuste **Threshold primeiro**, depois refine o **Feather**. Cabelo/borda macia → ligue **Defringe**.
**Casos de uso:** trocar fundo de green screen; isolar objeto de cor sólida para virar máscara (com Invert); isolar uma faixa de cor para correção localizada (Chroma Key + Saturation/Vibrance).
**Captação:** fundo verde/azul/laranja vibrante e uniforme, sem rugas; sujeito longe do fundo; nenhuma cor do fundo presente no sujeito (por isso fundo branco é ruim — some o branco dos olhos).

## 14.2 OSCILLATE ✅
*Tags: Auto, Essentials, Move/Transform, Property Adjust, Shake, Transform*
Move a camada para lá e para cá repetidamente.

| Parâmetro | Tipo | Descrição | Range / Default |
|---|---|---|---|
| **Angle** | ângulo | Direção do vai-e-vem, horário. 0º = horizontal (direita primeiro), 90º = vertical (baixo primeiro). +180º inverte a ordem | −3600º a 3600º; **default 45º** |
| **Frequency** | spinner | Velocidade em Hz. 1.0 = ciclo completo por segundo. **Cálculo cumulativo**, então dá para acelerar/desacelerar com keyframes de forma natural | 0 a 16; **default 2** |
| **Magnitude** | float | Distância máxima em pixels a partir da posição original | 0 a 4000; **default 25** |
| **Wave** | radio | **Sine** = movimento suave que desacelera ao inverter. **Triangle** = velocidade constante, inversão abrupta | **default Sine** |
| **Phase** | float | Deslocamento no ciclo. Com Frequency = 0, anime Phase para controlar manualmente a progressão | 0 a 1000; **default 0** |

**Fluxo recomendado oficial:** ajuste Frequency → role a timeline até um **extremo** da oscilação → ajuste Magnitude e Angle vendo o resultado.
**Compatível com Motion Blur** (o movimento gerado é reconhecido pelo Motion Blur).
**Casos de uso:** shake de transição (keyframes no Magnitude subindo/descendo de zero, junto com rotação); objetos balançando; zig-zag (anime a posição e some Oscillate em Triangle perpendicular).
⚠️ Efeito reescrito no AM 3.7 (ângulo virou horário, ranges maiores, frequência corrigida). Projetos antigos mostram **(Compatibility)** — delete e reaplique para usar a versão nova.

## 14.3 MOTION BLUR ✅
Aplica desfoque proporcional à variação de posição, escala ou rotação ao longo do tempo. Só aparece em camada animada por **Move & Transform**.

| Parâmetro | Descrição | Default |
|---|---|---|
| **Tune** | Tamanho do borrão. 1.00 = diferença entre dois frames consecutivos. 2.00 = dobro | **1.00** |
| **Position** | Liga/desliga blur nos keyframes de posição | **On** |
| **Scale** | Liga/desliga blur nos keyframes de escala | **On** |
| **Angle** | Liga/desliga blur nos keyframes de rotação | **On** |

**Truque oficial de Tune:** projeto a 60fps com Tune **2.00** parece 30fps. Projeto a 30fps com Tune **0.50** parece 60fps.
**Motion blur fica muito melhor com easing** (movimento lento no começo e no fim) do que com timing linear.
**Ordem de aplicação:** efeitos que só movem (ex.: *Oscillate*) entram **antes** e borram normalmente. Efeitos que deformam (ex.: *Fractal Warp*) entram **depois**, e o blur pode não sair como esperado.

---

# PARTE 15 — TABELA DE TRADUÇÃO AM ↔ AFTER EFFECTS

| Alight Motion | After Effects |
|---|---|
| Move & Transform | Transform |
| Pivot | Anchor Point |
| Curve Editor | Graph Editor (velocity/value) |
| Overshoot | Alças fora do range |
| Element | Precomp reutilizável / Essential Graphics |
| Group | Pre-compose |
| Null Object | Null Object |
| Layer Parent | Parent & Link |
| Parenting Helper | Expressões de compensação |
| Blending & Opacity | Modes + Opacity |
| Mask (dentro do grupo) | Track Matte (Alpha/Luma, normal e invertido) |
| Effect Browser + tags | Effects & Presets |
| Project Package / Alight link | Collect Files / .aep |
| Preset / XML | Animation Preset (.ffx) |
| Time Remap | Time Remapping |
| Retiming marks (intro/miolo/outro) | Responsive Design – Time (Protected Regions) |

---

# PARTE 16 — PERFORMANCE (emulador principalmente) 

O emulador é mais lento que celular real. O que mais ajuda:

1. **Low Quality Preview** ligado enquanto edita ✅
2. Desligar **Motion Blur** durante a edição e ligar só para exportar (fazendo pela **câmera**, controla a cena toda de uma vez) ✅
3. Elements pesados em **Dynamic Resolution** ✅
4. Trabalhar em element de resolução baixa (ex.: 540p) e usar no projeto final em resolução cheia
5. Reduzir efeitos empilhados por camada; pré-renderizar partes prontas
6. No emulador: alocar mais RAM/CPU, ligar aceleração gráfica, e usar resolução de janela igual à do projeto

⚠️ Erros comuns do AM no Android/emulador documentados oficialmente: **1285 Insufficient RAM** e **1037 Codec Init Failed** ✅ — os dois costumam ser falta de memória/codec do emulador, não do projeto.

---

# PARTE 17 — CHECKLIST DE MAPEAMENTO NO SEU APARELHO

Como não consigo abrir seu emulador, aqui vai o roteiro para você fotografar/gravar e completar o mapa com os rótulos **exatos** da sua versão. Me manda as imagens e eu preencho.

- [ ] Tela inicial: quais abas aparecem embaixo
- [ ] Tela de criação de projeto: todos os campos e opções de resolução/fps
- [ ] Editor vazio: barra superior inteira
- [ ] Painel **View Options** aberto: todas as opções
- [ ] Menu **(+)**: cada aba, uma foto por aba
- [ ] Camada de vídeo selecionada: todos os botões da barra inferior
- [ ] **Move & Transform** aberto: todas as sub-abas
- [ ] **Blending & Opacity**: lista completa de blend modes (role até o fim)
- [ ] Menu **Mask**: nomes exatos dos modos
- [ ] **Curve Editor** aberto + menu **⋯**
- [ ] Lista de tipos de easing avançado
- [ ] **Effect Browser**: lista de tags/categorias
- [ ] **⚙ Configurações do projeto**
- [ ] Painel de **exportação**: todos os formatos e opções
- [ ] Long-press no tempo atual: menu que aparece
- [ ] Long-press numa camada: menu de contexto
- [ ] Camada de texto selecionada: painel de texto
- [ ] Camada vetorial selecionada: ferramentas de nó
- [ ] Camada de áudio selecionada: controles

**Atalho melhor ainda:** exporte um projeto como **Project Package** ou um **preset XML** e me mande o arquivo/texto. O XML contém os **nomes internos reais** de cada propriedade e efeito — dá para mapear o app inteiro por dentro, incluindo parâmetros que a interface esconde.

---

# FONTES

| Assunto | Link |
|---|---|
| Central de ajuda oficial | https://support.alightmotion.com/hc/en-us |
| Guia de efeitos (parâmetros, range, default) | https://guide.alightmotion.com/effects/ |
| Curvas de easing | https://support.alightmotion.com/hc/en-us/articles/10536934703889-Animation-Easing-Curves |
| Câmera | https://support.alightmotion.com/hc/en-us/articles/10536993608977-Camera-Objects-Guide |
| Parenting e Null | https://support.alightmotion.com/hc/en-us/articles/10536997444369-Layer-Parenting-and-Null-Objects |
| Elements | https://support.alightmotion.com/hc/en-us/articles/10536791122449-Elements-The-Complete-Guide |
| Motion Blur | https://support.alightmotion.com/hc/en-us/articles/10537118778129-Motion-Blur |
| Pan & Zoom do preview | https://support.alightmotion.com/hc/en-us/articles/10536990235409-Preview-Pan-and-Zoom |
| Guia rápido | https://support.alightmotion.com/hc/en-us/articles/10536777320337-Alight-Motion-Quick-Start-Guide |
