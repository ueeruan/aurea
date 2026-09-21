# Grafo de efeitos e fusão

## O problema, em números

Uma camada com correção de cor (exposição, contraste, saturação, temperatura),
matriz de cor, opacidade e vinheta é, ingenuamente, 6 passes sobre 4K.

Cada passe sobre 4K RGBA16F lê e escreve ~66 MB. Seis passes dão ~400 MB de
tráfego de memória **por frame**. A 60 fps: 24 GB/s só de banda — mais do que a
memória de um celular entrega.

A saída não é otimizar o shader. É **não passar pela memória**.

## As três fusões

### Effect Fusion — a que mais rende

Efeitos que são função pura do pixel de entrada viram UMA expressão no mesmo
shader. Exposição, contraste, saturação, opacidade, vinheta, matriz de cor,
curva, LUT, preto-e-branco, blend de cor: seis deles viram **um passe**, com uma
leitura e uma escrita.

### Pass Fusion

Efeitos que precisam de vizinhança (blur, glow, sharpen) não fundem com cor, mas
fundem **entre si** quando compartilham kernel e raio. Um gaussiano separável é
H + V: `2N` taps em vez de `N²`.

### Shader Fusion

Blurs de raios diferentes (glow = blur grande + blur pequeno) compartilham um
passe de downsample em vez de dois passes completos.

## Resultado no exemplo

De 6 passes para **2**. Um até o blur, um depois.

## Classificação

O que decide a fusão é a CLASSE do efeito, declarada no registro:

| Classe | Exemplos | Funde? |
| --- | --- | --- |
| `PerPixel` | exposição, contraste, vinheta, LUT | **sempre** — N viram 1 |
| `Neighborhood` | blur, glow, sharpen, dilate | entre si, com mesmo raio, em 2 passes |
| `Temporal` | eco, motion blur acumulativo, ghosting | **não** — precisa de histórico |
| `Global` | auto-levels, pixel sort | **não** — precisa do frame inteiro |
| `Domain` | displacement, motion tile, warp | **não** — muda o domínio |
| `MatteGenerator` | extração de luminância, chroma key | **não** — vira entrada de outro |

Quando a fusão não é matematicamente possível, o compilador **diz que não é** e
emite os passes separados. Nunca finge que fundiu e produz imagem diferente do
export. `fusionBlockers` registra o motivo de cada quebra — o painel de debug
mostra isso em vez de deixar o usuário adivinhar.

## Efeito neutro é removido

Blur com raio 0, glow com intensidade 0, vinheta com quantidade 0: o efeito não
muda nada, e aplicar um passe que não muda nada custa 132 MB de banda por frame.

A checagem usa o valor **animado** no frame (via `TrackSet`), não o estático: um
blur cujo raio é animado de 0 a 40 existe no meio da animação e não pode ser
removido no frame 60.

Regra de segurança: só é considerado neutro quando o PRIMEIRO parâmetro da
descrição é `Float` e vale exatamente zero. Sem essa restrição, um efeito cujo
primeiro parâmetro é um ângulo ou um enum seria removido ao chegar em zero — e o
frame sairia errado sem ninguém perceber.

## Degradação só no preview

Efeitos caros têm duas contagens de amostras:

```
Motion blur  export = 32 samples   preview = 8
```

O export SEMPRE usa a configuração final. O compilador recebe um booleano
`preview` e produz o plano correspondente — é a MESMA função gerando os dois, e
é por isso que preview e export não podem divergir.

Blurs de raio grande rodam em meia resolução no preview: visualmente idêntico e
4× mais barato. É o truque que faz um blur de raio 100 caber no orçamento de um
aparelho médio.

## Métrica de custo

O custo de uma etapa é `pixels × amostras × escala²`. Amostras de textura é a
métrica que importa: uma leitura é 1, um gaussiano de 9 taps é 9, um blur de 32
samples é 32.

É essa soma que o scheduler compara com o orçamento de frame para decidir se
degrada. Um número inventado de "ms" não serviria — varia demais por aparelho.

## Estado atual

O compilador está completo e testado. Os SHADERS dos efeitos não existem e o
registro está vazio — então, na prática, nenhuma camada tem efeito hoje. O
caminho está pronto; falta o conteúdo.
