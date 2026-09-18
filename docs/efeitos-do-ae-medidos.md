# Efeitos do After Effects: o que já foi medido

Constantes tiradas de render do AE (via painel MotionRapido), prontas para
virar efeito. **Nada aqui foi implementado ainda** — este arquivo existe
para a medição não se perder, porque medir de novo custa uma sessão.

As referências estão em `build/qa/ae-novos/` (PNG, não versionado).

---

## O que o AE do dono tem

Sondei 46 nomes. Dos oito pedidos, **seis existem**:

| pedido | nome real no AE |
|---|---|
| TimeSlice | `S_TimeSlice` (Sapphire) |
| timewarp rgb | `S_TimeWarpRGB` (Sapphire) |
| DropShadow | `S_DropShadow` (Sapphire) e `ADBE Drop Shadow` (nativo) |
| preenchimento | `ADBE Fill` (nativo) |
| page curl | `CC Page Turn` (nativo) |
| light sweep | `CC Light Sweep` (nativo) |

**RSMB não é Sapphire** — é RE:Vision, e não está instalado. **LongShadow
não responde** por `S_LongShadow`, `S_Shadow` nem `S_EdgeShadow`.

Sapphire também tem, se servirem: `S_BlurMotion`, `S_BlurMoCurves`,
`S_Streaks`, `S_LightLeak`, `S_LensFlare`, `S_SpotLight`, `S_WarpBubble`,
`S_WarpCornerPin`, `S_WarpPolar`, `S_TileScramble`, `S_MatteOps`,
`S_FilmEffect`, `S_TimeDisplace`, `S_Kaleido`, `S_BlurChannels`.

---

## Parâmetros de cada um (lidos do AE, índices 1-based)

```
ADBE Drop Shadow   1:Cor 2:Opacidade 3:Direção 4:Distância 5:Suavidade
                   6:Somente sombra
ADBE Fill          1:Máscara 2:Todas as máscaras 3:Cor 4:Inverter
                   5:Difusão horizontal 6:Difusão vertical 7:Opacidade(0..1)
CC Page Turn       1:Controls 2:Fold Position 3:Fold Direction 4:Fold Radius
                   5:Light Direction 6:Render 7:Back Page 8:Back Opacity
                   9:Paper Color
CC Light Sweep     1:Center 2:Direction 3:Shape 4:Width 5:Sweep Intensity
                   6:Edge Intensity 7:Edge Thickness 8:Light Color
                   9:Light Reception
```

---

## Preenchimento — FEITO

Implementado em `lib/src/features/editor/domain/preenchimento.dart` +
shader `effects_v2.frag` modo 63. Medido e testado.

---

## Drop Shadow — FEITO (18/09)

Referência: comp 200×200, sólido branco 80×80 centrado em (100,100),
direção 135, distância 30, cor vermelha, opacidade 255.

**Deslocamento.** Na linha y=100 o quadrado ocupa 60..139 e há vermelho de
141 a 159. O deslocamento medido é `(+21, +21)`:

```
dx = -distancia * cos(direcao)
dy = +distancia * sin(direcao)     // y para BAIXO, como no motor
```

O sinal NEGATIVO no cosseno não é escolha: a direção é a da **luz** (135 põe
a luz em cima à esquerda), e a sombra cai do lado oposto.

**Desfoque.** Com `Suavidade` 20 a queda vai de 255 em 147 até 0 em 174 —
10% a 90% em ~12 px, o que numa gaussiana é `sigma ≈ 4,5`:

```
sigma = suavidade * 0.225
```

Com suavidade 0 não há desfoque e a sombra termina na borda exata do
quadrado deslocado (159, não 160,3).

**A sombra usa o ALFA da camada**, não a caixa dela. A cor vem do
parâmetro 1 e a opacidade do 2 (0..255 no AE; aqui em %).

### Os tres mecanismos descartados — e por que o descarte era injusto

A conta sempre esteve certa. **O desenho nao funcionava** por causa da
bancada, e nao dos mecanismos: os tres dependiam de desenhar a imagem
deslocada, e a imagem de teste nao tinha alfa nenhum. O que cada um fez,
para o registro:

1. **ARVORE DE WIDGETS** — `Stack` + `Positioned.fill` + `ColorFiltered` +
   `ImageFiltered` + `Transform.translate`. Nao pintou NADA: o diagnostico
   mostrou so a camada, sem um pixel de sombra. Tres arranjos de layout
   depois, desisti.

2. **`Paint.colorFilter` no `drawImage`** — a sombra saiu BRANCA, com a
   cor de origem. O filtro nao tingiu.

3. **`saveLayer` com o filtro na camada** — a sombra saiu branca com
   **ALFA ZERO**: o RGB sobreviveu e a transparencia sumiu. Pior que o
   anterior, porque o efeito desaparece sem erro nenhum.

4. **`drawRect` com `srcIn` por cima** — mesmo resultado do 3.

### O ACHADO QUE IMPORTA, e que veio no fim

Numa caneta LIMPA, com o canvas transladado e a MESMA imagem:

```
c.drawImage(im, Offset(21,21), Paint());   //  -> RGB branco, ALFA 0
c.drawImage(im, Offset.zero,  Paint());    //  -> correto, alfa 255
```

E desenhando **so** a do offset, ela tambem sai com alfa 0. Ou seja: nesta
bancada, `drawImage` com deslocamento NAO COMPOE — so os canais de cor
chegam, e o alfa sai zerado.

Isso explica os tres fracassos de uma vez: TODOS eles dependiam de desenhar
a imagem deslocada. E e o proximo lugar a olhar — antes de tentar um quarto
mecanismo, vale conferir se o problema e a imagem de teste (criada por
`ImageDescriptor.raw` + codec descartado) ou o `drawImage` deslocado.

**RESOLVIDO EM 18/09 — E A BANCADA ESTAVA ERRADA.**

O diagnostico foi refeito (`test/drawimage_deslocado_test.dart`) com a
imagem vinda de BYTES PNG por `ui.instantiateImageCodec`, em tres
superficies: o canvas do gravador, um segundo gravador por `drawPicture`,
e um `RepaintBoundary` de verdade. **Seis testes, alfa 255 nos seis.**

A imagem da bancada antiga vinha de `ImageDescriptor.raw` com o codec
descartado logo depois — os pixels iam embora com ele, e o desenho sem
deslocamento ainda acertava porque chegava a copiar antes. "`drawImage`
deslocado nao compoe" era falso; o motor nunca teve esse defeito.

**A sombra projetada esta IMPLEMENTADA e medida** — ver
`lib/src/features/editor/domain/sombra_projetada.dart`,
`sombra_projetada_pass.dart` e `test/sombra_projetada_test.dart` (23
testes, com leitura de pixel do quadro montado). O mecanismo e a arvore
de widgets, que era o primeiro que eu tinha descartado.

## TimeSlice e TimeWarpRGB — o bloqueio era falso

Os dois são **temporais**: precisam de quadros vizinhos. O `S_TimeSlice`
mostra um instante por faixa horizontal; o `S_TimeWarpRGB` desloca R, G e
B no tempo.

O bloqueio era falso. O que faltava não é o pipeline: é que o compositor
**já sabe montar a camada noutro instante** — `_emOutroTempo<T>()` em
`preview_stage.dart` monta a mesma camada em qualquer tempo do projeto, e é
por isso que o TimeSlice já está implementado (`time_slice.dart` + a
chamada em `preview_stage.dart`) e funciona. O TimeWarpRGB é o mesmo
caminho, três vezes, uma por canal: `ColorFilter.matrix` com uma matriz que
zera dois canais, um por instante, os três somados.

O custo é que cada instante é um render a mais da camada. Fica no mesmo
teto que o TimeSlice já respeita.

## Light Sweep — FEITO (18/09)

Refeito com uma fonte que **preenche o quadro e tem conteúdo** (gradiente
de canto a canto com dois quadrados), quatro variantes em
`build/qa/ae-novos/R3_ls_*.png`. O render anterior tinha saído chapado
porque a fonte era um sólido liso de 48×48.

O que a medição fixou (`tool/medir_luz_e_dobra.py`):

* **a luz soma um valor absoluto.** O mesmo `+63` aparece sobre um cinza
  102 e sobre um cinza 146 — não é fator da cor de origem;
* **o valor somado é `255 × Intensidade/100`** (25% → 63; 60% → 153, que o
  corte em 255 comeu até 155 no pixel mais claro);
* **a cor entra por inteiro**: o delta medido é R+63 G+61 B+59, que é o
  branco quente padrão do AE (1 / 0,9804 / 0,9412) vezes 63;
* **a faixa é uma reta**, não uma mancha: com a direção em 0 a luz é a
  mesma em toda a altura do quadro;
* **a normal é a própria direção** — o ajuste do ângulo devolveu −29,5°
  para uma Direção de −30°, ou seja, a faixa corre perpendicular à direção;
* **o perfil é quadrático**, `(1 − d/semi)²`, com a semi-extensão em
  **2 vezes a Largura** e o pico em `255 × Intensidade`. Ajuste sobre 22
  mil pixels de dois renders: pico 63,4 e 62,6 (teórico 63,75), semi 98,5
  e 101,5 px com Largura 50, erro médio 0,6 e 2,1 níveis. Reta, cosseno,
  gaussiana e smoothstep erram de 2 a 7 vezes mais — a reta que estava no
  shader caía rápido demais no meio e devagar demais na ponta.

Ficha: `cc_light_sweep`, "Varredura de luz", Light, com Centro, Direção,
Largura, Intensidade e Recepção. 10 testes em `test/luz_na_faixa_test.dart`.

O realce de **borda** do CC não foi reproduzido: nos quatro renders não
sobrou degrau nenhum que separe a borda da faixa.

**Armadilha da bancada:** os quatro renders trazem um borrão claro nas ~20
primeiras linhas (pico +224, sempre em x≈50, em todos os quatro) que não
acompanha parâmetro nenhum do efeito. Não é da faixa — é da bancada. Quem
for medir de novo comece em `y ≥ 22`.

## Page Turn — FEITO (18/09)

Mesma bancada nova (`build/qa/ae-novos/R3_pt_*.png`), quatro variantes de
Fold Point, Direction e Fold Radius.

O que a medição fixou:

* o vinco é uma **reta** e o lado plano fica **intacto** — a mudança começa
  exatamente na reta, sem um pixel alterado do outro lado;
* o rolo **comprime** o desenho e o projeta de volta por cima do plano: no
  render de `Fold(110,100)`, o canto do quadrado sai de (167,167) para
  (163,140) — mais perto do vinco do que estava. É a assinatura de um
  cilindro, e não de um deslocamento;
* há **reflexo correndo pela dobra**, e o ponto mais claro do quadro não
  está no meio dela.

O modelo implementado é o do cilindro: um ponto a `s` da dobra aparece na
tela a `raio × sin(s/raio)`, e a folha vira para o **verso** passando de
90°. Ficha `cc_page_turn`, "Dobra de página", Distort, com Posição, Ângulo,
Raio, Luz, Verso e Brilho. 16 testes em `test/dobra_de_pagina_test.dart`.

**O que NÃO foi fixado:** a convenção dos parâmetros do AE. O `Fold Point`
medido não cai no centro do vinco que o render mostra, e o `Fold Radius` de
50 rende um raio de ~0,8 vez o parâmetro. Preferi entregar o modelo limpo,
com a geometria certa e o raio em pixels, a enterrar um deslocamento mágico
que só valeria para esta bancada.

## Long Shadow — FEITO (18/09), E ESTE É NOSSO

**O AE do dono não tem Long Shadow.** O `S_LongShadow` do Sapphire não está
instalado, e o `S_Shadow` e o `S_EdgeShadow` também não existem na máquina
dele. Então este não é um efeito portado: é implementação nossa, e a ficha
(`sombra_longa`) não finge um id do After Effects.

O que ele faz é o long shadow clássico do motion design — a silhueta
esticada numa direção até um comprimento, chapada numa cor só. A conta é a
união de todas as cópias da silhueta deslocadas de 0 até o comprimento, e é
por isso que ela não vira centenas de `saveLayer`: o shader marcha de trás
para diante por pixel, uma leitura de textura por passo, com teto de 96
passos.

Duas heranças da Sombra projetada, de propósito: a **direção** (135° é a
luz em cima à esquerda) e a **margem da caixa**, que é literalmente a mesma
função.

19 testes em `test/sombra_longa_test.dart`. A leitura de pixel é feita no
shader cru, e não pela árvore de widgets: `isShaderFilterSupported` é false
no ambiente de teste, o passe cai na reserva do `SnapshotWidget`, e essa
foto é assíncrona de verdade — o teste passava sozinho e falhava em fila.

**A armadilha que o teste pegou:** o sampler **não** devolve zero fora da
textura. Sem uma guarda explícita em uv, o outro lado da textura volta pelo
modo de repetição e a sombra sai um quadrado cheio em vez do losango da
união de cópias.
