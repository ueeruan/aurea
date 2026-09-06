# AM2 STUDIO — FORMAS, EFEITOS, MOTOR 3D, NULLS E PRECOMP
### Especificação das quatro peças que faltam para o app ser um motion designer completo
**Base:** `AM2_STUDIO.md`, `AM2-auditoria-e-correcoes.md`, `AM2-motor-de-texto.md`, `AM2-modo-edicao-e-legendas.md`.

---

## 0. AS QUATRO DECISÕES ESTRUTURAIS

Antes da lista de recursos, quatro decisões que definem se isso vai ser possível ou virar remendo.

| # | Decisão | Por quê |
|---|---|---|
| **D1** | **Forma deixa de ser bitmap e vira árvore vetorial renderizada** | Enquanto forma for `Bitmap` → textura, Trim Paths, Repeater, tracejado e operadores de caminho são impossíveis, e a forma borra ao escalar |
| **D2** | **Camada ganha flag `is3D` e a comp ganha ordenação por profundidade** | Hoje "Z não muda empilhamento" é contrato. Com 3D real isso precisa mudar, mas **só entre camadas 3D** |
| **D3** | **Precomp com framebuffer próprio, tempo próprio e cache** | Sem isso não há reuso, não há organização e não há desempenho em cena complexa |
| **D4** | **Sistema de vínculo de propriedade (pickwhip) no lugar de expressões** | 80% do poder das expressões do AE, sem interpretador de JavaScript no celular |

D4 é a que ninguém pede e é a que mais muda o app. Volto nela no §4.3.

---

# 1. FORMAS VETORIAIS

## 1.1 O problema atual

> *"As formas são rasterizadas em bitmap e enviadas como textura."*

Consequências:
- Escalar 300% mostra pixels do bitmap original.
- Não existe caminho para aplicar operador; só existe a imagem final.
- Animar qualquer parâmetro força re-rasterização em CPU a cada frame.
- Traço é desenhado junto no bitmap, então não dá para tracejar, afinar ou aparar.

## 1.2 Nova arquitetura de render

Híbrida, escolhida por tipo de conteúdo:

| Conteúdo | Técnica | Motivo |
|---|---|---|
| Primitivas analíticas (retângulo, elipse, polígono, estrela, setor, cruz, coração) | **SDF no fragment shader** | Antialiasing perfeito em qualquer escala, custo quase zero, sem re-raster |
| Caminhos bezier arbitrários e resultado de operadores | **Tesselação em CPU + cache**, desenho por triângulos | Genérico; recalcula só quando o caminho muda |
| Traço | **Stroke-to-fill** (converte contorno em polígono) | Permite tracejado, pontas, junções e afinamento |

Regras de cache:
- Chave da tesselação: `hash(pathData + operadores + parâmetros)`. Escala **não** entra na chave — a tesselação é em espaço de objeto.
- Tolerância de achatamento (flattening) proporcional à escala efetiva na tela, com faixas discretas para não recalcular a cada pixel de zoom.
- SDF não tem cache: é calculado no shader todo frame, e é o caminho barato.

**Ganho colateral:** forma animada deixa de custar CPU por frame.

## 1.3 A árvore de forma

Cada camada de forma vira uma árvore, igual ao AE:

```text
ShapeLayer
└── contents: ShapeItem[]
    ShapeItem =
      | Group        { items: ShapeItem[], transform: ShapeTransform }
      | Path         { primitive | bezier, closed }
      | Fill         { color|gradient, rule: NONZERO|EVENODD, opacity }
      | Stroke       { color|gradient, width, cap, join, miterLimit, dash[], dashOffset, opacity }
      | Operator     { ver §1.5 }
```

Ordem de avaliação dentro de um grupo: **de baixo para cima**, como no AE. Um operador afeta tudo que está abaixo dele no mesmo grupo. É contraintuitivo na primeira vez e precisa estar documentado na UI, com uma dica visual.

Um grupo tem transform próprio (pivô, posição, escala, rotação, skew, opacidade), animável. É isso que permite montar personagem e ícone complexo numa camada só.

## 1.4 Catálogo de primitivas ampliado

Você tem 13. Adicionar:

| Nova primitiva | Parâmetros próprios |
|---|---|
| **Retângulo arredondado independente** | raio por canto (4 valores) |
| **Anel / donut** | raio externo, raio interno |
| **Arco** | raio, ângulo inicial, varredura, espessura, pontas |
| **Polígono estrelado com raio duplo** | pontas, raio externo, raio interno, arredondamento externo/interno |
| **Onda / senoide** | amplitude, frequência, fase, comprimento |
| **Espiral** | voltas, raio inicial/final, sentido |
| **Balão de fala** — variantes | retangular, redondo, pensamento, grito |
| **Seta** — variantes | reta, curva, dupla, cotovelo; ponta ajustável nas duas extremidades |
| **Marcador de pino** | corpo, ponta, furo |
| **Escudo, hexágono, octógono, losango, trapézio, paralelogramo** | lados, arredondamento |
| **Raio / relâmpago** | segmentos, irregularidade, semente |
| **Chevron** | espessura, ângulo |
| **Blob orgânico** | pontos, irregularidade, suavidade, semente |
| **Ticket / cupom** | recortes laterais, raio, quantidade |
| **Nuvem, engrenagem, folha, gota, check, x, mais, menos** | conforme cada uma |

Todas com: raio de canto normalizado, fill/stroke independentes, e **todos os parâmetros animáveis**.

## 1.5 Operadores de caminho — o que faz "animar shapes"

Esta é a parte que falta de verdade. Sem ela não existe motion graphics vetorial.

| Operador | Parâmetros | Para que serve |
|---|---|---|
| **Trim Paths** ⭐ | start, end, offset, modo (simultâneo/individual) | **O mais usado de todos.** Linha se desenhando, loader circular, progresso, contorno animado |
| **Repeater** ⭐ | cópias, offset, transform por cópia (posição, escala, rotação, opacidade início/fim), âncora | Grades, raios, leques, padrões |
| **Offset Paths** | quantidade, junção, miter limit | Engrossar/afinar a forma toda, contorno paralelo |
| **Round Corners** | raio | Arredondar qualquer caminho, inclusive resultado de operador |
| **Zig Zag** | tamanho, cristas por segmento, suave/pontudo | Serrilha, borda rasgada, onda |
| **Pucker & Bloat** | quantidade (-100 a 100) | Inflar e contrair organicamente |
| **Twist** | ângulo, centro | Torcer |
| **Wiggle Paths** | tamanho, detalhe, tipo, wiggles/s, correlação, semente | Contorno tremido, estilo desenho à mão |
| **Wiggle Transform** | mesmos + quais transforms afetar | Balanço orgânico |
| **Merge Paths** | união, subtração, interseção, exclusão | Booleanas. Cria formas complexas de primitivas |

**Trim Paths precisa de parametrização por comprimento de arco** — exatamente o mesmo cálculo do seu caminho espacial de keyframes e do texto em caminho. Escreva uma vez, use nos três.

**Determinismo:** Wiggle Paths e Wiggle Transform usam a mesma função de ruído puro `(seed, índice, tempo)` do seletor Wiggly do motor de texto. Nada de estado acumulado.

## 1.6 Traço completo

| Recurso | Detalhe |
|---|---|
| Largura | animável, com opção de escalar junto com a camada ou não |
| Pontas | reta, redonda, quadrada |
| Junções | miter (com limite), redonda, chanfrada |
| **Tracejado** | lista de traço/espaço + **dash offset animável** = formiguinha e progresso tracejado |
| **Gradiente no traço** | linear e radial, com paradas |
| **Afinamento (taper)** | comprimento e largura inicial/final, e onda no traço |
| Alinhamento | centro, dentro, fora |
| Múltiplos traços | mais de um `Stroke` na árvore, empilhados |

## 1.7 Editor de nós bezier

Hoje **"Editar Vetor" abre o painel de Forma e Tamanho** — uma das três mentiras da interface listadas na auditoria. Precisa existir de verdade:

- Toque num nó seleciona; arrastar move, com snap em grade, guias e outros nós.
- Alças bezier arrastáveis, com modo **espelhado / assimétrico / independente**.
- Toque no traço adiciona nó no ponto exato.
- Toque duplo num nó alterna entre canto e suave.
- Selecionar múltiplos nós por laço, mover e escalar em bloco.
- Fechar/abrir caminho.
- Converter primitiva em caminho editável (irreversível, com aviso).
- Colar caminho SVG do clipboard.
- Barra de ferramentas flutuante: adicionar, remover, tipo do nó, fechar, inverter sentido.

Sem esse editor, "vetor" no app é só decorativo.

## 1.8 Animação de caminho e morph

Você já tem morph por reamostragem de 160 pontos, que é bom para primitiva diferente. Falta:

- **Caminho como propriedade animável**: keyframe no próprio path, interpolando vértice a vértice quando a contagem bate.
- **Correspondência automática** quando não bate: inserir vértices no caminho mais simples até igualar, preservando a forma.
- **Ponto de partida ajustável** no morph, para evitar a rotação indesejada (o clássico "a estrela gira ao virar círculo"). Expor como parâmetro `matchOffset`.
- **Interpolação em espaço de arco**, não só linear por índice.

---

# 2. EFEITOS QUE FALTAM

## 2.1 A questão do catálogo importado

Suas 315 definições têm IDs `com.alightcreative.*`. Como já apontei na auditoria, se vieram do APK do Alight Motion, publicar com elas é problema jurídico real. Independente disso, há um motivo técnico para ter um kit nativo: efeitos nativos podem receber controles próprios, integrar com 3D e com a árvore de forma; efeitos importados são caixa-preta.

**Recomendação:** construir um **kit nativo essencial** de ~45 efeitos, próprios, e tratar o catálogo importado como camada de compatibilidade opcional para importar projetos do AM.

## 2.2 Kit nativo essencial

| Família | Efeitos |
|---|---|
| **Desfoque** | Gaussian, Box, Directional, Radial (zoom e giro), Lens, Bilateral (preserva borda), Sharpen |
| **Cor** | Curves (RGB + por canal), Levels, HSL, Vibrance, Color Balance (sombras/médios/altas), LUT 3D (.cube), Gradient Map, Selective Color, Auto contraste |
| **Key e matte** | Chroma Key com spill suppression, Luma Key, Difference Key, Refine Matte, Choker, Feather |
| **Distorção** | Displacement Map, Turbulent Displace, Wave Warp, Bulge, Twirl, Corner Pin, Mesh Warp, Optics Compensation, Polar Coordinates |
| **Gerar** | Fractal Noise ⭐, Cell Pattern, Gradient (4 cores), Lens Flare, Beam, Radio Waves, Grid, Checkerboard, Vegas (contorno animado) |
| **Estilizar** | Glow (com limiar e cor), Long Shadow, Halftone, Mosaic, Find Edges, Posterize, Roughen Edges, CC-style Light Sweep |
| **Tempo** | Echo, Time Displacement, Posterize Time, Frame Blend, **Motion Blur com amostragem real** |
| **Transição** | Linear/Radial Wipe, Card Wipe, Block Dissolve, Gradient Wipe, Luma Fade |
| **Utilidade** | Motion Tile, Transform (transform extra dentro da pilha), Set Matte, Levels por canal, Fill, Stroke, Drop Shadow |
| **3D** | Camera Lens Blur com profundidade real, Depth Matte, Fog 3D |

⭐ **Fractal Noise** merece destaque: é a base de fumaça, fogo, textura, transição orgânica, distorção e displacement. É o efeito com melhor relação custo/benefício de todo o AE.

## 2.3 Sistema de partículas

Falta grande, e é o que separa "editor" de "ferramenta de motion".

| Grupo | Parâmetros |
|---|---|
| Emissor | tipo (ponto, linha, círculo, retângulo, **caminho de uma camada de forma**, superfície de camada), posição 3D, tamanho, direção, dispersão |
| Emissão | taxa/s, rajada, quantidade máxima, semente |
| Vida | duração, variação |
| Física | velocidade inicial + variação, gravidade, vento, arrasto, **turbulência por campo de ruído** |
| Aparência | fonte (forma, camada, textura, sprite sheet), tamanho ao longo da vida, cor ao longo da vida, opacidade ao longo da vida, rotação e giro |
| Render | blend, ordenação, motion blur |

**Restrição de projeto crítica — determinismo.** Simulação iterativa quebra a invariante I1 (frame N direto ≠ frame N após reproduzir). Solução: **trajetória analítica por partícula.** Cada partícula tem tempo de nascimento e estado inicial derivados da semente; a posição no tempo `t` é função fechada de `(t - nascimento)`, incluindo gravidade e arrasto, com turbulência amostrada de um campo de ruído em `(posição, t)`.

Consequência honesta: **colisão entre partículas não é suportada**, porque exige iteração. Documente isso em vez de fingir. Vale a troca — seek instantâneo importa mais que colisão num app de celular.

## 2.4 Áudio reativo

Ligar amplitude de áudio a qualquer propriedade. Você já extrai waveform e guarda em `wave-<uid>.bin`, então o custo é baixo e o resultado é **pré-calculado**, portanto determinístico.

- Separar em bandas: grave, médio, agudo, ou faixa de Hz definida.
- Suavização (attack/release) e ganho.
- Saída vira uma fonte de vínculo de propriedade (§4.3).

Com isso: texto que pulsa no grave, forma que reage ao vocal, corte no beat. É o recurso mais pedido por quem edita clipe musical.

---

# 3. MOTOR 3D

## 3.1 Estratégia em três níveis

Não tente construir uma engine 3D completa. Construa em degraus, cada um entregando valor sozinho.

| Nível | Entrega | Custo |
|---|---|---|
| **N1 — Camada 3D real** | Rotação X/Y/Z, orientação, câmera de perspectiva verdadeira, ordenação por profundidade, DOF real | Médio. É onde está 80% do valor |
| **N2 — Malhas procedurais** | Cubo, cilindro, esfera, cone, toro, prisma, plano segmentado — texturizados com o conteúdo da camada | Médio |
| **N3 — Extrusão e luzes** | Texto e forma extrudados com chanfro, luzes com sombra | Alto |

## 3.2 Transform 3D

Hoje: `posição X/Y/Z, escala X/Y, skew, rotação (2D), pivô X/Y`.

Passa a ser:

```text
Transform3D
  position:    Vec3
  anchor:      Vec3
  scale:       Vec3
  orientation: Vec3      // orientação, interpolada como no AE (caminho curto)
  rotationX/Y/Z: Float   // rotações separadas, animáveis, acumulam sobre a orientação
  opacity:     Float
```

Matriz por camada:
```
M = T(position) · R(orientation) · Rz · Ry · Rx · Skew · S(scale) · T(-anchor)
```
Acumulada pela cadeia de pais, como você já faz em 2D.

**Orientação vs rotação separada:** orientação interpola pelo caminho mais curto (bom para apontar); rotações separadas somam e permitem mais de uma volta (bom para animar giro). Ter as duas é o que o AE faz e resolve casos opostos.

## 3.3 Ordenação por profundidade — a mudança de contrato

Seu documento hoje diz, como decisão deliberada:

> *"**Z não muda empilhamento**; Z serve apenas para perspectiva, profundidade de campo e neblina. Essa separação impede a pilha de se reordenar sozinha durante uma animação de profundidade."*

A decisão foi correta para 2.5D. Com 3D real ela precisa evoluir, sem perder o que protegia:

**Regra nova:**
- Camada **2D** mantém a ordem da pilha. Sempre.
- Camadas **3D contíguas** na pilha formam um **grupo 3D** e são ordenadas por profundidade **dentro do grupo**.
- Uma camada 2D **quebra** o grupo 3D, exatamente como no AE. Isso dá controle previsível: quem quiser travar a ordem, insere uma 2D no meio.

**Método de ordenação:** algoritmo do pintor por profundidade do centroide, não z-buffer. Motivo: z-buffer quebra alpha blending, e composição com transparência é o núcleo do app.

**Limitação a assumir e documentar:** camadas 3D **não se interpenetram**. O AE clássico também não. Fingir que interpenetram exigiria z-buffer + order-independent transparency, o que não cabe no orçamento de um celular.

## 3.4 Câmera de verdade

Você já tem câmera 2.5D com zoom/ângulo, foco e neblina. Evoluir para:

| Recurso | Detalhe |
|---|---|
| **Câmera de 2 nós** | Posição + **ponto de interesse**, com o alvo animável. É o que permite orbitar um objeto |
| Câmera de 1 nó | Livre, dirigida por rotação. Manter as duas |
| Projeção | Perspectiva verdadeira; **ortográfica** como opção |
| Distância focal, abertura, zoom | Ligados fisicamente, como já são |
| **DOF por profundidade real** | Círculo de confusão calculado do buffer de profundidade, não aproximação por camada |
| Predefinições de lente | 15, 24, 35, 50, 80, 135 mm |
| Auto-orient "encarar a câmera" | Você já tem, mantenha |

## 3.5 Luzes e sombras (N3)

| Tipo | Parâmetros |
|---|---|
| Pontual | cor, intensidade, raio de queda, decaimento |
| Spot | + ângulo do cone, penumbra |
| Paralela | + direção |
| Ambiente | cor, intensidade |

Sombras por **shadow map** de 1024 px, uma luz projetora por vez no nível 1, com opção de escurecimento e difusão. Por camada: `castsShadows`, `acceptsShadows`, `acceptsLights`, `ambient`, `diffuse`, `specular`, `shininess`, `metal`.

Isto é caro. Só entra depois de N1 e N2 estarem sólidos, e com desligar global.

## 3.6 Malhas procedurais — os "cubos"

Efeito/objeto que gera malha e usa o conteúdo da camada como textura:

| Malha | Parâmetros |
|---|---|
| **Cubo / caixa** | largura, altura, profundidade, arredondamento, faces abertas, **textura por face** (mesma em todas, desdobrada, ou uma camada por face) |
| **Cilindro** | raio, altura, segmentos, tampas |
| **Esfera** | raio, segmentos, mapeamento equiretangular |
| **Cone / pirâmide** | raio base, altura, lados |
| **Toro** | raio maior, raio menor, segmentos |
| **Prisma** | lados, altura |
| **Plano segmentado** | subdivisões — base para dobra, onda e ripple 3D |
| **Tubo / túnel** | raio, comprimento, textura interna |

Parâmetros comuns: subdivisões, normais suaves/duras, dupla face, deslocamento por mapa de altura.

**Textura por face é o detalhe que importa.** Um cubo com uma camada diferente por face resolve caixa de produto, dado, cubo de logos — os pedidos reais.

## 3.7 Extrusão de texto e forma (N3)

Pegar um caminho vetorial (texto ou forma) e gerar sólido:

| Parâmetro | Função |
|---|---|
| Profundidade | espessura da extrusão |
| Chanfro | estilo (ângulo, côncavo, convexo), largura, profundidade |
| Material da frente / lado / chanfro | cor ou camada por região |
| Suavidade | segmentos do chanfro |

Implementação: triangular a tampa (mesmo tesselador do §1.2), gerar paredes laterais a partir do caminho achatado, chanfro por offset do caminho. **Reuso direto do renderizador de formas.**

Isso entrega texto 3D com profundidade, que é um dos usos mais desejados e hoje impossível no app.

## 3.8 Gizmo e vistas no celular

Trabalhar em 3D numa tela de 6 polegadas é o desafio real de UX.

- **Gizmo de eixos** com cores fixas: X vermelho, Y verde, Z azul. Setas para posição, anéis para rotação, cubos para escala.
- Tocar num eixo **trava o arrasto naquele eixo**. Sem isso é impossível ser preciso com o dedo.
- Botão que alterna posição / rotação / escala.
- **Seletor de vista rápida:** Câmera ativa, Frente, Topo, Direita, Livre. Trocar de vista é o que torna posicionamento em Z compreensível.
- Opcional: vista dividida em duas (câmera + topo) em tablet e paisagem.
- Grade de chão no espaço 3D, com fade pela distância.
- Indicador numérico flutuante durante o arrasto.

## 3.9 Orçamento 3D

| Item | Teto |
|---|---|
| Camadas 3D por comp | 100 |
| Triângulos por frame | 200 mil no preview, sem teto no export |
| Luzes com sombra | 1 no N3.1, 3 depois |
| Shadow map | 1024 px |
| Malhas procedurais simultâneas | 20 |

Com o orçamento estourado, degradar: desligar sombra, reduzir subdivisão, cair para o proxy de preview. Nunca engasgar.

---

# 4. NULLS E RIGGING

## 4.1 Null 2D e Null 3D

Você já tem Null com parenting e compensação ao trocar de pai — a base está certa. Falta:

| Item | Detalhe |
|---|---|
| **Null 3D** | Transform3D completo, com gizmo de 3 eixos |
| Tamanho do gizmo | ajustável, para não sumir nem poluir |
| Cor do null | para distinguir vários rigs |
| Nunca renderiza | você já garante isso — mantenha e teste |
| Null com forma de referência | opção de mostrar cruz, caixa, eixos ou câmera-alvo |

## 4.2 Ferramentas de rigging em um toque

- **Criar null do centro da seleção** — cria um null no centro dos objetos escolhidos e parenteia todos a ele.
- **Parentear seleção a novo null**.
- **Null no ponto de interesse** da câmera, para orbitar.
- **Transferir animação para o null**: move os keyframes do filho para o pai, mantendo o resultado visual.
- **Soltar mantendo posição** (unparent sem pulo) — o inverso da compensação que você já faz.
- **Parenting Helper** conforme a auditoria: rotação e escala em modo Normal, Locked ou Weighted, mais auto-rotate para rodas.

## 4.3 Vínculos de propriedade — o substituto das expressões

**Esta é a peça de maior alavancagem do documento inteiro.**

O poder do AE não vem dos efeitos, vem das expressões. Interpretador JavaScript num celular é caro, arriscado e ruim de digitar com o dedo. A alternativa que cobre a maior parte dos casos:

```text
PropertyLink
  source:     { layerId, propertyPath, componente? }   // ex: Null_1 → rotation
  target:     propriedade desta camada
  multiplier: Float     // ganho
  offset:     Float     // deslocamento
  delay:      Float     // atraso em segundos ← faz seguidor/eco
  curve:      CurveSpec?    // remapeia usando o editor de curvas que já existe
  clamp:      { min, max }?
  enabled:    Boolean
```

Fórmula: `alvo = curva(origem(t - delay)) · multiplicador + offset`, com clamp.

Fontes possíveis: qualquer propriedade de qualquer camada, **amplitude de áudio por banda** (§2.4), tempo da cena, e índice da camada (para offsets em série).

O que isso destrava, sem uma linha de código do usuário:
- Roda que gira conforme o carro anda (posição X → rotação, com multiplicador).
- Ponteiro de relógio, medidor, barra de progresso.
- Uma camada seguindo outra com atraso — animação em cascata com um vínculo e um `delay` incremental.
- Texto que pulsa no grave da música.
- Controle mestre: um null com valores que dirigem dez camadas.
- Parenting Helper em modo Weighted vira caso particular disso.

**Guarda obrigatória:** detecção de ciclo no grafo de vínculos, com recusa e mensagem clara. Mesmo tratamento que você já dá a ciclos de parentesco.

**UI:** botão de "pickwhip" que entra em modo de escolha; tocar na propriedade de destino cria o vínculo. Propriedade vinculada aparece com ícone e cor distinta, e mostra o valor calculado, não editável direto.

---

# 5. PRECOMP FUNCIONAL

Estado atual, pelo seu documento:

> *"Grupo/precomp: parentesco via Null funciona; grupo aninhado e framebuffer isolado não."*
> *"O importador achata precomps em Nulls e propaga parte do visual."*

Achatar é perda de informação. Precisa virar precomp de verdade.

## 5.1 Modelo

```text
PrecompLayer : Layer
  sourceSceneId: String        // referência a uma Scene do documento
  timeMap:       AnimProp<Float>?   // time remap, mapeia tempo da comp pai → tempo interno
  timeStretch:   Float = 1.0
  resolutionMode: DYNAMIC | FIXED
  fixedResolution: Size?
  collapseTransformations: Boolean = false
  // + tudo que uma camada normal tem: transform, máscara, efeitos, blend, opacidade
```

## 5.2 Render

1. Renderiza a `Scene` interna num **FBO próprio**, no tempo interno.
2. Aplica ao resultado: máscaras da camada de precomp → pilha de efeitos → transform → opacidade → blend.

É exatamente o mesmo pipeline de qualquer camada. A única diferença é a origem do conteúdo.

## 5.3 Resolução e "colapsar transformações"

| Modo | Comportamento |
|---|---|
| **DYNAMIC** | Resolução ajustada à área efetivamente visível e à escala aplicada. Padrão |
| **FIXED** | Resolução declarada, renderiza inclusive o que sai da tela. Necessário para efeitos que precisam do conteúdo fora do quadro (Polar Coordinates, Bulge, Magnify Background) |
| **Colapsar transformações** ⭐ | O conteúdo vetorial dos filhos é rasterizado **na resolução final**, e o 3D dos filhos passa para o espaço 3D do pai |

Colapsar é a diferença entre precomp que borra ao escalar e precomp nítida. Custa: com colapso, as máscaras e efeitos da camada de precomp têm ordem alterada, e nem todo efeito é compatível. Regra: quando incompatível, avisar na UI em vez de silenciosamente ignorar.

## 5.4 Tempo aninhado

- Tempo interno padrão = tempo da comp pai menos o `inTime` da camada.
- `timeStretch` multiplica.
- `timeMap` com keyframes permite congelar, inverter e retimar a precomp inteira — é o Time Remap da auditoria, aplicado a comps.
- A precomp respeita o próprio fps, mas amostra no fps do pai. Documentar o arredondamento.

## 5.5 Cache

Precomp é a maior oportunidade de desempenho da cena.

- Cachear o frame renderizado com chave `(sceneId, tempoInterno, hashDosParâmetros)`.
- Precomp cujo conteúdo não muda no tempo (logo estático) renderiza **uma vez** e reusa sempre.
- Invalidar por edição, com granularidade de cena.
- Teto de memória do cache configurável, LRU.

## 5.6 3D atravessando a fronteira

- Sem colapso: a precomp é uma **superfície plana 2D** no espaço do pai. Camadas 3D lá dentro são achatadas. É o comportamento do AE e é previsível.
- Com colapso: as camadas 3D internas entram no grupo 3D do pai e participam da ordenação por profundidade.

Documente essa diferença de forma explícita na UI, com um selo na camada. É a dúvida número um de quem aprende AE.

## 5.7 Elements = precomp externo

Unifique. Um `Element` é uma precomp guardada fora do projeto, com link. Toda a mecânica de precomp vale, mais:
- Editar o original reflete em todos os projetos.
- Converter em grupo quebra o link.
- Campos de texto expostos para personalizar sem editar o original.
- **E, ao contrário de hoje, listado e gerenciável na home.**

## 5.8 Guardas

- Recusar recursão: precomp não pode conter a si mesma, direta ou indiretamente. Detectar no grafo e mostrar erro claro.
- Profundidade máxima de aninhamento: 10, com aviso.
- Precomp faltando: usar cópia embutida e oferecer recriar, como você já faz com Elements.

## 5.9 Navegação

- Duplo toque na camada de precomp entra nela.
- **Trilha de navegação** no topo: `Projeto › Cena principal › Precomp A › Precomp B`, cada nível tocável.
- Timeline mostra a precomp como grupo expansível na cena pai.
- "Precompor seleção": pega N camadas, cria uma precomp, substitui pela camada de precomp mantendo o resultado visual pixel a pixel. **Esse "mantendo o resultado idêntico" é teste automatizado.**

---

# 6. ORDEM DE RENDER DEFINITIVA

Escreva isto no código como comentário e como teste. Quase todo bug estranho de composição vem de ordem errada.

**Por camada, de dentro para fora:**

```
1.  Conteúdo
    - texto: layout → animadores → atlas de glifos
    - forma: árvore vetorial → operadores → fill/stroke → tesselação/SDF
    - mídia: frame decodificado
    - precomp: render da cena interna no FBO próprio
2.  Máscaras da própria camada
3.  Pilha de efeitos, de cima para baixo
4.  Transform: T(-pivô) → escala → skew → rotação → T(posição)
5.  Acúmulo da cadeia de pais (com Parenting Helper e vínculos aplicados)
6.  Projeção 3D, se is3D
7.  Track matte da camada acima, se houver
8.  Opacidade
9.  Blend com o acumulado abaixo
```

**Por composição:**

```
A. Resolve vínculos de propriedade (com detecção de ciclo)
B. Avalia todas as propriedades no tempo t
C. Particiona a pilha em blocos 2D e grupos 3D contíguos
D. Ordena cada grupo 3D por profundidade do centroide
E. Compõe de baixo para cima
F. Camada de ajuste reprocessa o acumulado naquele ponto
G. Resultado vai para o pai, ou para a tela
```

---

# 7. TESTES

**Formas**
- Escalar uma forma a 800%: nenhuma borda serrilhada (falha hoje, com o bitmap).
- Trim Paths de 0 a 100% num círculo: comprimento desenhado bate com o comprimento de arco, com tolerância de 0,5%.
- Repeater com 20 cópias: posição da cópia N confere com a fórmula.
- Merge Paths nas 4 operações contra golden frames.
- Wiggle Paths: frame 137 direto == após reproduzir do 0.
- Tracejado com `dashOffset` animado: continuidade sem salto ao passar de um ciclo.

**3D**
- Camada 3D com rotação zero renderiza **pixel a pixel** igual à mesma camada em 2D. Esta é a invariante de neutralidade do 3D.
- Grupo 3D com 3 camadas em Z diferentes: ordem correta em todas as posições de câmera.
- Camada 2D inserida no meio quebra o grupo 3D, e a ordem volta a ser a da pilha.
- Cubo com 6 camadas nas faces: cada face mostra a camada certa em todas as rotações.
- Orçamento: 100 camadas 3D mantêm 30 fps no preview.

**Nulls e vínculos**
- Null nunca aparece no export: renderizar cena só com nulls dá quadro totalmente transparente.
- Vínculo com ciclo é recusado com mensagem, sem travar.
- Vínculo com `delay`: valor no tempo t é igual ao valor da origem em t−delay, exato.
- Transferir animação para o null não muda um pixel.

**Precomp**
- "Precompor seleção" mantém o resultado **pixel a pixel**.
- Precomp estática renderiza uma vez e usa cache nos frames seguintes (verificar por contador).
- Recursão é detectada e recusada.
- Time remap de precomp: congelar em t=2s mostra o frame 2s em todo o intervalo.
- Colapsar transformações: texto vetorial escalado 400% fica nítido.

---

# 8. PLANO DE PRs

**PR-S1 — Renderizador vetorial.** SDF para primitivas, tesselação com cache para caminhos, stroke-to-fill. Substituir a rasterização em bitmap. Teste da escala a 800%.

**PR-S2 — Árvore de forma.** Grupos, múltiplos paths, fill e stroke como itens, transform de grupo. Migração das formas existentes.

**PR-S3 — Operadores.** Trim Paths e Repeater primeiro, que são 80% do uso. Depois Offset, Round Corners, Zig Zag, Merge Paths, Pucker & Bloat, Twist, Wiggle.

**PR-S4 — Traço completo e primitivas novas.**

**PR-S5 — Editor de nós bezier.** Fecha a mentira nº1 da interface.

**PR-3D1 — Transform 3D e ordenação.** `is3D`, rotação X/Y/Z, orientação, grupos 3D, algoritmo do pintor. Teste de neutralidade obrigatório.

**PR-3D2 — Câmera de verdade.** 2 nós, ortográfica, DOF por profundidade, predefinições de lente.

**PR-3D3 — Gizmo e vistas.** Eixos coloridos com trava, seletor de vista, grade de chão.

**PR-3D4 — Malhas procedurais.** Cubo primeiro, com textura por face. Depois cilindro, esfera, cone, toro, prisma, plano segmentado.

**PR-3D5 — Extrusão** de texto e forma com chanfro.

**PR-3D6 — Luzes e sombras.** Só depois que os anteriores estiverem no orçamento.

**PR-N1 — Null 3D e ferramentas de rigging.**

**PR-N2 — Vínculos de propriedade.** Pickwhip, multiplicador, offset, delay, curva, clamp, detecção de ciclo.

**PR-N3 — Áudio reativo** como fonte de vínculo.

**PR-P1 — Precomp com framebuffer próprio.** Dynamic/Fixed, cache, guardas de recursão, "precompor seleção" com teste pixel a pixel.

**PR-P2 — Tempo aninhado e time remap** de precomp.

**PR-P3 — Colapsar transformações.**

**PR-P4 — Unificar Elements como precomp externa** e listar na home.

**PR-E1 — Kit nativo de efeitos**, começando por Fractal Noise, Curves, Glow, Displacement Map e Motion Blur com amostragem real.

**PR-E2 — Sistema de partículas** com trajetória analítica.

---

# 9. PROMPT PARA O AGENTE DE CÓDIGO

> Implemente formas vetoriais, motor 3D, nulls e precomp do AM2 Studio conforme `AM2-formas-3d-nulls-precomp.md`. Leia antes `AM2_STUDIO.md` e os outros três documentos de especificação.
>
> **Antes de codar, responda com evidência do fonte:**
> 1. `Shapes.kt` rasteriza para `Bitmap` e envia como textura? Cole o trecho. Existe algum caminho vetorial na GPU hoje?
> 2. Onde está a decisão de "Z não muda empilhamento" no `Compositor`? Cole o trecho da ordenação de camadas.
> 3. O `Compositor` percorre só camadas raiz? Cole o laço principal e diga o que acontece hoje com `children`.
> 4. Existe alguma estrutura de cache de frame por camada ou por grupo? Onde?
>
> **Execute em ordem, um PR por item, sem misturar:**
>
> **PR-S1** — Substitua a rasterização em bitmap por render vetorial: SDF no fragment shader para primitivas analíticas, tesselação com cache em espaço de objeto para caminhos bezier, stroke-to-fill para contornos. A chave do cache de tesselação **não** inclui escala. Teste: forma escalada a 800% sem serrilha.
>
> **PR-S2** — `ShapeLayer.contents` como árvore de `Group | Path | Fill | Stroke | Operator`, avaliada de baixo para cima dentro de cada grupo, com transform próprio por grupo. Migre as formas existentes para a árvore sem mudar um pixel — teste de neutralidade.
>
> **PR-S3** — **Trim Paths e Repeater primeiro.** Trim Paths precisa de parametrização por comprimento de arco; extraia essa função para uso compartilhado com o caminho espacial de keyframes e o texto em caminho. Depois os demais operadores. Wiggle usa a mesma função de ruído puro do seletor Wiggly do motor de texto.
>
> **PR-3D1** — Flag `is3D` por camada, `Transform3D` com orientação e rotações separadas, matriz `T·R(orient)·Rz·Ry·Rx·Skew·S·T(-anchor)` acumulada pela cadeia de pais. Camadas 2D mantêm a ordem da pilha; camadas 3D **contíguas** formam grupo ordenado por profundidade do centroide com algoritmo do pintor; uma camada 2D quebra o grupo. **Teste obrigatório: camada 3D com rotação zero renderiza pixel a pixel igual à mesma camada em 2D.**
>
> **PR-3D4** — Malhas procedurais começando pelo **cubo com textura por face** (mesma em todas, desdobrada, ou uma camada por face). Depois cilindro, esfera, cone, toro, prisma, plano segmentado.
>
> **PR-N2** — Sistema de vínculo de propriedade: `alvo = curva(origem(t - delay)) · multiplicador + offset`, com clamp e detecção de ciclo no grafo. Pickwhip na UI. Propriedade vinculada mostra o valor calculado e não é editável direto.
>
> **PR-P1** — Precomp com FBO próprio e cache por `(sceneId, tempoInterno, hashParâmetros)`. Modos Dynamic e Fixed. Guarda de recursão e profundidade máxima 10. Comando "precompor seleção" que **mantém o resultado pixel a pixel** — este é o teste que aprova o PR.
>
> **PR-E2** — Partículas com **trajetória analítica por partícula**, nunca simulação iterativa: posição em `t` é função fechada do tempo desde o nascimento, com turbulência amostrada de campo de ruído. Colisão não é suportada e isso deve estar documentado. Teste: frame 300 renderizado direto é idêntico ao renderizado após reproduzir do zero.
>
> **Regras:**
> - Nenhuma mudança pode quebrar a invariante de neutralidade: recurso desligado ou neutro não muda um pixel.
> - Determinismo absoluto: nada de estado acumulado entre frames, em nenhum sistema novo.
> - A ordem de render da seção 6 é normativa. Escreva-a como comentário no `Compositor` e como teste.
> - Preview e export pelo mesmo grafo.
> - Atualize `AM2_STUDIO.md` no mesmo PR que muda comportamento.
