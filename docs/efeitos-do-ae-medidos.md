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

## TimeSlice e TimeWarpRGB — bloqueados por arquitetura

Os dois são **temporais**: precisam de quadros vizinhos. O `S_TimeSlice`
mostra um instante por faixa horizontal; o `S_TimeWarpRGB` desloca R, G e
B no tempo. O pipeline entrega **um quadro** ao efeito
(`_applyEffects(effects, child, local)`).

Não é shader novo em cima do que existe: é uma segunda fonte de imagem, com
cache de quadros, e um teto de custo por quadro — a mesma conversa do
"puxar quadro em tela cheia por quadro" que já congelou a camada neste
projeto.

---

## Light Sweep e Page Curl — a bancada estava errada

Os dois renders saíram **chapados** porque usei um sólido liso de 48×48
como fonte. Nenhum dos dois tem o que fazer com superfície uniforme: o
Light Sweep precisa de uma faixa atravessando, e o Page Turn precisa da
dobra cruzando a camada. Refazer com uma fonte que **preenche o quadro e
tem conteúdo** (um gradiente com formas, por exemplo).
