# Plugins de efeito do AUREA

> English version: [README.en.md](README.en.md)

Um **plugin** é um pacote que acrescenta **um efeito** ao navegador de efeitos do
AUREA, na categoria **Plugins**. Depois de instalado, o efeito funciona como
qualquer efeito do app: entra no painel da camada, tem parâmetros com keyframes e
expressões, sai igual no preview e no export, e é salvo no projeto.

Este documento é a referência completa para quem cria plugins: o formato do
pacote, cada campo do `plugin.json`, cada símbolo que o shader recebe, os limites,
como testar e empacotar, e o que fazer quando a instalação recusa.

- [1. Os dois tipos de plugin](#1-os-dois-tipos-de-plugin)
- [2. O pacote `.aureaplugin`](#2-o-pacote-aureaplugin)
- [3. Referência do `plugin.json`](#3-referência-do-pluginjson)
- [4. A interface do shader](#4-a-interface-do-shader)
- [5. Plugins de pilha e expressões](#5-plugins-de-pilha-e-expressões)
- [6. Importar shaders ISF (`.fs`)](#6-importar-shaders-isf-fs)
- [7. Limites e por quê](#7-limites-e-por-quê)
- [8. Dicas de desempenho](#8-dicas-de-desempenho)
- [9. Testar e empacotar](#9-testar-e-empacotar)
- [10. Solução de problemas](#10-solução-de-problemas)
- [11. Projetos, versões e remoção](#11-projetos-versões-e-remoção)
- [12. API do motor (para quem integra a interface)](#12-api-do-motor-para-quem-integra-a-interface)
- [13. Licenças](#13-licenças)

Exemplos prontos em [`examples/`](examples/):

| Pasta / arquivo | Tipo | O que mostra |
| --- | --- | --- |
| `rgb-wave/` → `rgb-wave.aureaplugin` | shader, 1 passe | separação RGB em onda; float, angle e color; `margin`; tempo |
| `soft-glow/` → `soft-glow.aureaplugin` | shader, 2 passes | passe em 1/4 da resolução (`scale`), `target` e `inputs` |
| `dream-look/` → `dream-look.aureaplugin` | stack | desfoque + brilho + saturação embutidos, com expressões |
| `pulse-vignette.fs` | ISF 2.0 | importação de um `.fs` (point2D, color, bool, TIME) |

---

## 1. Os dois tipos de plugin

**`"kind": "shader"`** — você escreve o efeito em **GLSL 4.50**: uma função
`vec4 aurea_main(vec2 uv)` que devolve a cor de cada pixel. O plugin pode ter de
1 a 8 passes (cada um com o próprio shader), texturas intermediárias em
resolução reduzida e até 16 parâmetros. O AUREA compila o shader **no aparelho**
na instalação: GLSL → SPIR-V (glslang) → a API de GPU do aparelho (Vulkan usa o
SPIR-V; OpenGL ES recebe ESSL 3.10 e o Metal recebe MSL, traduzidos pelo
SPIRV-Cross com as mesmas opções dos shaders do próprio app).

**`"kind": "stack"`** — uma **pilha de efeitos do próprio app** (desfoque,
brilho, cor...) com valores calculados a partir dos parâmetros do plugin por
expressões simples (`amount * 40`). Não tem shader: é o tipo mais seguro e
funciona em qualquer aparelho. É o jeito de distribuir um "look" com controles
próprios.

## 2. O pacote `.aureaplugin`

Um `.aureaplugin` é um **ZIP** (sem compressão ou deflate) com o `plugin.json`
na raiz e os arquivos que ele cita:

```
meu-efeito.aureaplugin
├── plugin.json
├── passe1.frag
└── passe2.frag
```

Também é aceito um ZIP feito da pasta (o `plugin.json` dentro de uma única
subpasta). Arquivos extras (licença, imagens de documentação) podem ir junto e
são ignorados. O AUREA instala em `<pasta de plugins>/<id>/`:

- Android: `files/plugins/` (armazenamento interno do app)
- iOS: `Documents/Plugins/`

O plugin **não tem acesso a arquivos**: o shader só lê as texturas que o motor
amarra (a camada e as texturas intermediárias do próprio plugin) e o bloco de
parâmetros. Não há rede, disco nem relógio de parede.

## 3. Referência do `plugin.json`

Texto em UTF-8. Exemplo completo (o do `rgb-wave`):

```json
{
  "aureaPlugin": 1,
  "id": "com.aurea.examples.rgbwave",
  "version": "1.0.0",
  "author": "AUREA (exemplo)",
  "name": { "pt-BR": "Onda RGB", "en": "RGB Wave" },
  "description": { "pt-BR": "Separa os canais ...", "en": "Splits the red ..." },
  "category": "Glitch",
  "kind": "shader",
  "params": [
    { "id": "amount", "type": "float", "label": { "pt-BR": "Deslocamento (px)", "en": "Offset (px)" },
      "default": 12, "min": 0, "max": 80 }
  ],
  "passes": [ { "shader": "rgb_wave.frag" } ],
  "margin": 80
}
```

### Campos do topo

| Campo | Tipo | Obrigatório | Descrição |
| --- | --- | --- | --- |
| `aureaPlugin` | número | sim | Versão do formato. Este app lê **`1`**. |
| `id` | texto | sim | Identidade única, em domínio reverso: 3 a 64 caracteres `a-z 0-9 . _ -`, sem começar/terminar em `.` e sem `..`. Ex.: `com.estudio.brilho`. A chave do efeito no projeto é `plugin.<id>` — **nunca mude o id** de um plugin publicado. |
| `version` | texto | sim | `x.y.z` (cada parte 0..99999). Reinstalar o mesmo `id` com versão **igual ou mais nova** substitui; mais velha é recusada. |
| `author` | texto | não | Autor (até 128). |
| `name` | texto ou idiomas | sim | Nome no catálogo (até 64). |
| `description` | texto ou idiomas | não | Descrição (até 512). |
| `category` | texto | não | Subcategoria mostrada dentro de **Plugins** (até 32). |
| `kind` | `"shader"`, `"shader3d"` ou `"stack"` | sim | O tipo (seção 1). `shader3d` (0.3.1) é um `shader` que também recebe a câmera da cena e as camadas ligadas (seção 4, "Plugins 3D"). |
| `params` | lista | não | Até **16** parâmetros (abaixo). |
| `passes` | lista | shader | 1 a **8** passes (abaixo). |
| `margin` | número | não | Quantos **px da camada** o efeito lê/desenha além da caixa da camada (0..1024). Ex.: um brilho de raio 50 declara `50`. Sem margem, o resultado é recortado à caixa. |
| `colorSpace` | `"linear"` ou `"srgb"` | não | `linear` (padrão): o shader vê o espaço de trabalho do motor — RGB **linear**, alfa **pré-multiplicado**. `srgb`: o motor entrega cores **sRGB de alfa reto** e converte o resultado de volta (é o que a importação ISF usa). |
| `stack` | lista | stack | 1 a 16 efeitos embutidos (seção 5). |

### Texto por idioma

`name`, `description`, `label` e as opções de enum aceitam um texto simples
(vale para todos os idiomas) ou um objeto idioma → texto:

```json
"name": { "pt-BR": "Brilho suave", "en": "Soft Glow", "es": "Brillo suave",
          "ru": "Мягкое свечение", "hi": "कोमल चमक", "id": "Cahaya Lembut", "ar": "توهج ناعم" }
```

Idiomas do app: `pt-BR`, `en`, `es`, `ru`, `hi`, `id`, `ar`. Se faltar o idioma
da pessoa, vale `en`, depois `pt-BR`, depois o primeiro declarado. O nome em
`pt-BR` é o nome do efeito no motor; a interface mostra o do idioma do aparelho.

### Parâmetros (`params`)

| Campo | Descrição |
| --- | --- |
| `id` | Nome do parâmetro: letras, dígitos e `_`, até 32, começando por letra ou `_`. É também o **nome no shader** (macro) e nas expressões. Não pode ser palavra reservada do GLSL nem função nativa (`mix`, `texture`, `float`...), nem começar com `gl_`, `aurea`, `isf_`, `IMG_`, `vv_`, nem conter `__`. |
| `type` | `float`, `int`, `bool`, `enum`, `color`, `point`, `angle`, `layer` (só `shader3d`). |
| `label` | Texto ou idiomas (até 48). Padrão: o `id`. |
| `default` | Valor inicial (por tipo, abaixo). |
| `min`, `max` | Faixa do controle (números). |
| `options` | Só `enum`: lista de 1 a 32 textos (ou objetos de idioma). |
| `animatable` | `true` (padrão) ou `false`: aceita keyframes/expressões. |
| `accept` | Só `layer`: lista com `"camera"`, `"null"`, `"light"` ou `"any"` (padrão: qualquer camada). A tela só oferece essas camadas. |

| Tipo | `default` | `min`/`max` padrão | No shader |
| --- | --- | --- | --- |
| `float` | número | 0..1 | `float` |
| `int` | número inteiro | 0..100 | `float` (valor inteiro) |
| `bool` | `true`/`false` | — | `float` 0 ou 1 |
| `enum` | índice da opção | — | `float` com o índice |
| `color` | `"#RRGGBB"`, `"#RRGGBBAA"` ou `[r, g, b(, a)]` 0..1 **em sRGB** (como um seletor de cor) | — | `vec4` RGBA **linear**, alfa reto (no `colorSpace: srgb`, RGBA sRGB) |
| `point` | `[x, y]` relativo à camada (0..1 = largura/altura; `[0.5, 0.5]` = centro) | -1..2 | `vec2` **no espaço `uv`** do passe (já convertido pelo motor) |
| `angle` | graus | -360..360 | `float` em **radianos** |
| `layer` | — (nenhuma camada) | — | um **inteiro** (a posição da camada entre os parâmetros `layer`), para usar nas funções `aurea_layer_*` (até **4** por plugin; nunca animável) |

### Passes (`passes`)

| Campo | Descrição |
| --- | --- |
| `shader` | Arquivo GLSL no pacote (caminho relativo, sem `..`). Até 64 KB. |
| `target` | Nome da textura intermediária que este passe escreve. **Obrigatório** em todo passe menos o último; o último **não** tem `target`: ele é a saída do efeito. |
| `scale` | Resolução do passe relativa à imagem da camada: 0.125..1 (padrão 1). Um desfoque em `0.25` custa 1/16. |
| `inputs` | Objeto `"nome": "origem"`, até 3: cada nome vira um `uniform sampler2D` no shader; a origem é `"source"` (a camada) ou o `target` de um passe **anterior**. A camada também está sempre em `aurea_source`. |

Exemplo de dois passes (o `soft-glow`):

```json
"passes": [
  { "shader": "bright_blur.frag", "target": "bloom", "scale": 0.25 },
  { "shader": "combine.frag", "inputs": { "bloom": "bloom" } }
]
```

## 4. A interface do shader

Você escreve **apenas**:

```glsl
vec4 aurea_main(vec2 uv) {
    return texture(aurea_source, uv);
}
```

O motor junta três trechos e compila como GLSL 4.50 com a semântica do Vulkan:

1. **o cabeçalho do motor** (abaixo) — nas mensagens de erro ele aparece como `aurea_header`;
2. **o seu arquivo** — as linhas numeram de 1, com o nome do arquivo (`rgb_wave.frag:12: ...`);
3. **o `main`** do motor: `void main() { o_color = aurea_main(v_uv); }` (com a
   conversão sRGB → linear no último passe de um plugin `srgb`, e NaN/infinito
   viram 0 para não contaminar a composição).

### Símbolos do cabeçalho

| Símbolo | Tipo | Significado |
| --- | --- | --- |
| `#version 450` | — | Já declarado: **não** escreva `#version`. |
| `AUREA_PLUGIN` | macro | `1` (para `#ifdef`). |
| `AUREA_TEX0`..`AUREA_TEX11`, `AUREA_PARAMS`, `AUREA_IMG0`, `AUREA_IMG1`, `AUREA_DATA`, `AUREA_DATA1` | macros | Os slots de recurso do motor (0..11, 12, 13, 14, 15, 16). Informativos: os samplers do plugin já estão declarados. |
| `AUREA_PASS_INDEX` | macro | Índice deste passe (0 = primeiro). |
| `AUREA_PASS_COUNT` | macro | Número de passes do plugin. |
| `AUREA_COLOR_SRGB` | macro | `1` se `colorSpace` é `srgb`, senão `0`. |
| `v_uv` | `in vec2` | O uv do pixel (o mesmo que chega em `aurea_main`). |
| `o_color` | `out vec4` | A saída (o `main` do motor escreve). |
| `aurea_source` | `sampler2D` (binding 0) | A imagem da camada (com os efeitos anteriores a este). |
| `<nome>` de `inputs` | `sampler2D` (bindings 1..3) | As entradas nomeadas do passe, na ordem declarada. |
| `aurea.resolution` | `vec4` | `xy` = texels deste passe, `zw` = `1/xy`. |
| `aurea.time` | `vec4` | `x` = segundos, `y` = quadro (com fração no desfoque de movimento), `z` = fps da composição. É o tempo **da camada** (0 no início dela). |
| `aurea.layer` | `vec4` | `xy` = tamanho da camada em px; `zw` = **texels por px da camada neste passe** (1 no export em tamanho natural, 0,25 num preview em 1/4 ou num passe `scale: 0.25`). |
| `aurea.region` | `vec4` | A região que o `uv` 0..1 cobre, em px da camada: `xy` = origem, `zw` = tamanho. Sem `margin` é a caixa da camada (`0, 0, largura, altura`). |
| `aurea.params[16]` | `vec4[16]` | Os valores dos parâmetros, um `vec4` por parâmetro, na ordem do `plugin.json`. Use as macros abaixo. |
| `<id do parâmetro>` | macro | `#define amount (aurea.params[0].x)` — o valor já convertido (tabela da seção 3). `color` = `aurea.params[i]` (vec4), `point` = `aurea.params[i].xy`. |
| `AUREA_PI` | `const float` | π. |
| `aurea_srgb_to_linear(x)`, `aurea_linear_to_srgb(x)` | `float`/`vec3` | Curva sRGB (IEC 61966-2-1), a mesma do motor. |
| `aurea_luma(rgb)` | `float` | Luminância BT.709 (`0.2126, 0.7152, 0.0722`). |
| `aurea_unpremultiply(c)`, `aurea_premultiply(c)` | `vec4` | Alfa pré-multiplicado ↔ reto. |
| `aurea_hash(p)` | `float` | Número pseudoaleatório 0..1 determinístico a partir de `vec2`. |
| `aurea_hash2(p)` | `vec2` | Idem, dois valores. |
| `aurea_noise(p)` | `float` | Ruído de valor suave 0..1. |
| `aurea_fbm(p, oitavas)` | `float` | Soma de até 8 oitavas de `aurea_noise`, 0..1. |
| `aurea_px_to_uv(px)` | `vec2` | Converte um deslocamento em **px da camada** para uv deste passe. Use para todo comprimento (raio, deslocamento): o efeito fica igual no preview reduzido e no export. |
| `aurea_uv_to_px(uv)` | `vec2` | O inverso. |
| `aurea_layer_uv(uv)` | `vec2` | Converte o uv do passe para coordenadas relativas à **caixa da camada** (0..1), útil com `margin`. |
| `aurea_uv_from_layer(p)` | `vec2` | O inverso. |

### Coordenadas

- `uv` (0,0) é o canto **superior esquerdo**; (1,1) o inferior direito.
- **Toda** textura que o shader vê — `aurea_source`, as `inputs` e os
  `target`s — cobre o **mesmo retângulo** (a região de trabalho do efeito).
  O mesmo `uv` amostra o mesmo ponto em todas, qualquer que seja o `scale` do
  passe. Só a resolução muda.
- Fora de 0..1 as texturas devolvem **transparente** (borda transparente):
  `texture(aurea_source, uv + offset)` perto da borda lê `vec4(0)`.
- Com `margin`, a região é a caixa da camada aumentada pela margem (e recortada
  ao que o quadro mostra). O motor copia a camada para essa região antes do
  primeiro passe, então o `uv` da fonte e o da saída continuam iguais.

### Cor

- `colorSpace: linear` (padrão): RGB **linear** (primárias BT.709), alfa
  **pré-multiplicado**, em ponto flutuante (pode passar de 1 em HDR). Devolva no
  mesmo espaço: `rgb` já multiplicado pelo alfa. Para trabalhar em alfa reto, use
  `aurea_unpremultiply` e `aurea_premultiply`.
- `colorSpace: srgb`: o shader recebe sRGB de alfa reto (0..1) e devolve sRGB
  de alfa reto; o motor converte. Mais lento (um passe de preparo) e menos
  preciso em HDR; prefira `linear` em plugins novos.

### Tempo e animação

`aurea.time.x` anda com o playback e é o mesmo no preview, no scrubbing e no
export (sem relógio de parede). Animações devem depender só do tempo e dos
parâmetros — nada de estado entre quadros. Os parâmetros recebem keyframes e
expressões no painel como os de qualquer efeito.

### Vários passes

Cada passe é um shader com o próprio `aurea_main`. O passe N lê
`aurea_source` e as entradas nomeadas (`inputs`) que apontam para `target`s de
passes anteriores. O último passe é a saída. `aurea.resolution` e
`aurea.layer.zw` refletem o `scale` de cada passe.

### Plugins 3D (`"kind": "shader3d"`, 0.3.1)

Um plugin 3D é um `shader` que também enxerga a **câmera da cena** (a câmera
ativa da composição, ou a padrão quando não há câmera) e o **mundo** de até 4
camadas ligadas por parâmetros `layer` — um **nulo 3D** como alvo ou pivô, uma
**câmera**, uma **luz**. O efeito continua sendo desenhado na camada que o
recebe; o 3D é o que o shader calcula (raios, projeções, distâncias).

```json
{
  "aureaPlugin": 1, "id": "com.estudio.mira", "version": "1.0.0",
  "name": { "pt-BR": "Mira 3D", "en": "3D Target" }, "kind": "shader3d",
  "params": [
    { "id": "alvo", "type": "layer", "accept": ["null"], "label": { "pt-BR": "Alvo", "en": "Target" } },
    { "id": "raio", "type": "float", "default": 40, "min": 1, "max": 400 }
  ],
  "passes": [ { "shader": "mira.frag" } ]
}
```

O bloco `aurea` ganha, **depois** de `params[16]` (nada antes muda):

| Símbolo | Tipo | Conteúdo |
| --- | --- | --- |
| `aurea.view` | `mat4` | vista ← mundo (x direita, y para baixo, z para a frente) |
| `aurea.compFromWorld` | `mat4` | px da composição (homogêneo) ← mundo |
| `aurea.compFromLayer` | `mat4` | px da composição (homogêneo) ← px da camada |
| `aurea.camera` | `vec4` | `xyz` posição da câmera no mundo; `w` = 1 câmera de camada, 0 padrão |
| `aurea.lens` | `vec4` | `x` campo de visão vertical (rad), `y` plano próximo, `z` aspecto, `w` focal em px |
| `aurea.comp` | `vec4` | `xy` tamanho da composição (px), `zw` = 1/xy |
| `aurea.layerWorld[4]` | `mat4` | mundo ← px de cada camada ligada |
| `aurea.layerPos[4]` | `vec4` | `xyz` ponto de âncora no mundo; `w` = 1 ligada e visível |
| `aurea.layerInfo[4]` | `vec4` | `x` tipo (6 nulo, 8 câmera, 9 luz), `y` campo de visão (câmera), `z` 1 = camada 3D |

Funções (só em `shader3d`; `#define AUREA_SCENE3D 1`):

| Função | O que faz |
| --- | --- |
| `bool aurea_layer_valid(int i)` | a camada ligada existe, está visível e no tempo |
| `vec3 aurea_layer_position(int i)` | posição (âncora) no mundo |
| `mat4 aurea_layer_matrix(int i)` | mundo ← camada (com pais) |
| `vec3 aurea_layer_axis(int i, int a)` | eixo `a` (0 x, 1 y, 2 z) da camada no mundo |
| `int aurea_layer_kind(int i)` / `float aurea_layer_fov(int i)` | tipo / campo de visão da câmera ligada |
| `vec3 aurea_camera_position()`, `aurea_camera_right()`, `aurea_camera_down()`, `aurea_camera_forward()` | a câmera da cena |
| `vec2 aurea_comp_px(vec2 uv)` | o `uv` do passe em px da composição |
| `void aurea_ray(vec2 uv, out vec3 ro, out vec3 rd)` | o raio da câmera que passa por este pixel (origem e direção no mundo) |
| `vec3 aurea_world_to_comp(vec3 p)` | ponto do mundo → px da composição (`z` = profundidade homogênea; ≤ 0 = atrás da câmera) |
| `vec3 aurea_world_to_view(vec3 p)` | ponto do mundo → espaço da câmera |

Unidades: o mundo do AUREA é em **px da composição** (o plano z = 0 com a
câmera padrão é a composição 1:1). O nome de um parâmetro `layer` vira a
posição dele entre os `layer` (`alvo` = 0 acima): `aurea_layer_position(alvo)`.
Exemplo completo: `examples/target-3d`.

## 5. Plugins de pilha e expressões

```json
"kind": "stack",
"params": [
  { "id": "softness", "type": "float", "default": 0.4, "min": 0, "max": 1 },
  { "id": "warmth", "type": "color", "default": "#FFE0C0" }
],
"stack": [
  { "effect": "aurea.blur.gaussian", "set": { "blurriness": "softness * 12" } },
  { "effect": "aurea.light.glow", "set": { "radius": "20 + softness * 60", "color": "warmth" } },
  { "effect": "aurea.color.saturation", "set": { "saturation": "-25 * softness" } }
]
```

- `effect`: a chave de um efeito **embutido** (`aurea.blur.gaussian`,
  `aurea.light.glow`, `aurea.color.saturation`, `aurea.color.exposure`...).
  A chave e os ids dos parâmetros são os do motor (estáveis; veja
  `engine/src/effects/builtin/*.cpp`).
- `set`: parâmetro do embutido → valor. Os que não aparecem ficam no padrão.
  - número ou `true`/`false`: valor fixo;
  - texto: **expressão** sobre os parâmetros do plugin;
  - lista: um valor/expressão por componente (`[x, y]` para ponto, `[r, g, b]`
    ou `[r, g, b, a]` para cor, linear);
  - o `id` de um parâmetro `color`/`point` do plugin copia o valor inteiro para
    um parâmetro do mesmo tipo (`"color": "warmth"`).
- No projeto, o plugin **vira os embutidos** no planejamento de cada quadro:
  fusão das operações de cor, efeitos neutros que saem da cadeia, margens e
  recursos funcionam exatamente como se a pessoa tivesse aplicado os embutidos
  à mão — o resultado é o mesmo quadro.
- Não entram numa pilha os efeitos que o app executa fora da cadeia de pixels
  ou que dependem de dados próprios da instância: `aurea.time.*`,
  `aurea.audio.*`, `aurea.control.*`, `aurea.3d.*`, `aurea.layout.*`,
  `aurea.text*`, `aurea.shape3d.*`, malha de deformação, marionete, Particular,
  Rotobrush e o ajudante de parentesco.

### Expressões

Aritmética pequena e fechada, compilada na instalação:

- números (`12`, `0.5`, `1e3`), `+ - * / %`, `^` (potência), parênteses;
- os parâmetros pelo `id`; componentes: `ponto.x`, `ponto.y`, `cor.r`, `cor.g`,
  `cor.b`, `cor.a` (ou `.x .y .z .w`);
- `pi`;
- funções: `min`, `max` (2 a 8 argumentos), `clamp(x, a, b)`, `mix(a, b, t)`,
  `step(borda, x)`, `smoothstep(a, b, x)`, `sin`, `cos`, `tan`, `atan(x)`,
  `atan(y, x)`, `abs`, `sign`, `pow`, `sqrt`, `exp`, `log`, `floor`, `ceil`,
  `round`, `fract`, `radians`, `degrees`.

Unidades: as do parâmetro (ângulo em graus, ponto relativo à camada, bool 0/1,
enum = índice). Divisão por zero dá 0; resultado não finito vira 0. O valor
calculado é preso à faixa digitada do parâmetro do embutido.

## 6. Importar shaders ISF (`.fs`)

O AUREA instala diretamente um arquivo **ISF 2.0** de passe único: escolha o
`.fs` em vez de um `.aureaplugin`. Ele vira um plugin `shader` com
`colorSpace: srgb` e id `isf.<nome-do-arquivo>` (ex.: `Bad TV.fs` →
`isf.bad-tv`).

| ISF | No AUREA |
| --- | --- |
| `INPUTS` `float` | parâmetro `float` (DEFAULT/MIN/MAX) |
| `bool`, `event` | parâmetro `bool` (`event` nasce desligado) |
| `long` com `VALUES`/`LABELS` | `enum`; no shader o nome devolve o **valor** de `VALUES` |
| `long` sem `VALUES` | `int` |
| `color` | `color` (DEFAULT em sRGB) |
| `point2D` | `point`. Normalizado (0..1) quando o `MAX` vai até 1 (ou, sem `MAX`, o `DEFAULT` está em 0..1); senão em pixels de uma tela de referência 1920×1080. No shader chega no espaço do ISF (y para cima; em pixels quando o ISF usa pixels). |
| `image` (a de entrada) | `aurea_source` |
| `IMG_NORM_PIXEL`, `IMG_PIXEL`, `IMG_THIS_PIXEL`, `IMG_THIS_NORM_PIXEL`, `IMG_SIZE` | funções equivalentes |
| `isf_FragNormCoord`, `vv_FragNormCoord`, `gl_FragCoord`, `RENDERSIZE` | coordenadas com a origem **embaixo à esquerda**, como no ISF |
| `TIME`, `TIMEDELTA`, `FRAMEINDEX`, `PASSINDEX` | tempo da camada, `1/fps`, quadro, `0` |
| `DATE` | `vec4(0)` (sem relógio de parede: o export é determinístico) |
| `gl_FragColor` | a saída |
| `texture2D` | `texture` |

Recusado com mensagem: `PASSES` com mais de um passe, `TARGET`/`PERSISTENT`,
`PERSISTENT_BUFFERS`, `IMPORTED`, entradas `audio`/`audioFFT`, uma segunda
imagem de entrada. Os erros de compilação apontam a **linha do `.fs` original**.
Para um efeito de vários passes, converta para um `.aureaplugin` com `passes`.

## 7. Limites e por quê

| Limite | Valor | Por quê |
| --- | --- | --- |
| Passes | 8 | Cada passe é uma textura e uma ida à memória; em GPU de celular a banda é o gargalo. |
| Parâmetros | 16 | O bloco de parâmetros cabe em 320 bytes (o motor usa no máximo 1 KB por passe). |
| Entradas por passe | 3 + a fonte | 4 texturas por passe de efeito no motor. |
| Shader | 64 KB por arquivo | Compilar no aparelho custa CPU e memória; shaders grandes também compilam devagar no driver. |
| `plugin.json` | 256 KB | — |
| Pacote | 16 MB | — |
| `margin` | 1024 px | Cada px de margem aumenta a textura de trabalho. |
| Pilha | 16 efeitos | — |
| **Quadro de teste** | **250 ms em 256×256** | Shader pesado ou com laço longo **trava a GPU do celular** (o sistema pode reiniciar o driver e fechar o app). Na instalação o AUREA compila cada passe na GPU do aparelho e desenha um quadro de 256×256 com os valores padrão; se ele passar de 250 ms (medido depois de criados os pipelines), a instalação é recusada. Em 1080p o mesmo shader custaria ~30× mais. |

A medição é **neste aparelho**: um plugin aceito num celular rápido pode ser
recusado num de entrada. Projete para o mais fraco. Um laço infinito ainda pode
travar a GPU durante o quadro de teste — o motor detecta a perda do dispositivo
e recusa, mas o driver de alguns aparelhos fecha o app antes; **nunca** escreva
laço cujo fim depende de um valor calculado.

## 8. Dicas de desempenho

- **Laços com limites fixos** (`for (int i = 0; i < 16; ++i)`); se o número de
  amostras depende de um parâmetro, faça o laço fixo e saia com `if (i >= n) break;`.
- **Conte as leituras de textura**: cada `texture()` por pixel custa; um
  desfoque 7×7 são 49 leituras. Faça o pesado num passe com `scale: 0.25` ou
  `0.5` (como o `soft-glow`).
- Use `aurea_px_to_uv` para todo comprimento: o preview reduzido roda menos
  pixels sem mudar o resultado.
- `float` é suficiente; evite `double`. Em OpenGL ES tudo é `highp`.
- Evite `pow`/`exp`/`sin` dentro de laços grandes; pré-calcule fora.
- Declare `margin` só do tamanho que o efeito realmente alcança: ela aumenta a
  textura de trabalho em todos os passes.
- Prefira `colorSpace: linear`: `srgb` acrescenta um passe de preparo.
- Um plugin `stack` não tem custo de shader próprio: herda a otimização dos
  efeitos do app (fusão das operações de cor num passe só).

## 9. Testar e empacotar

1. Crie uma pasta com `plugin.json` e os `.frag` (use um exemplo como base).
2. Confira e empacote com o script (Python 3.8+, sem dependências):

   ```
   python docs/plugins/tools/pack_plugin.py minha-pasta          # → minha-pasta.aureaplugin
   python docs/plugins/tools/pack_plugin.py minha-pasta -o x.aureaplugin
   python docs/plugins/tools/pack_plugin.py --check minha-pasta  # só confere
   ```

   O script aplica as mesmas regras do app (formato, id, versão, parâmetros,
   passes, entradas, pilha, tamanhos). A compilação do GLSL e o quadro de teste
   só o app faz.
3. Para compilar o GLSL no computador antes, junte o cabeçalho da seção 4 com
   o seu arquivo e use o `glslangValidator -V` do Vulkan SDK (o mesmo
   compilador, glslang).
4. Copie o `.aureaplugin` (ou o `.fs`) para o aparelho e instale pelo app:
   **Efeitos → "Plugins: instalar e gerenciar" → Instalar plugin** (Android e
   iOS). A mesma tela mostra autor, versão, se o plugin funciona neste aparelho
   e o tempo do quadro de teste, e remove. O efeito aparece no grupo
   **Plugins** do navegador. A mensagem de recusa diz o arquivo e a linha.
5. Teste o efeito numa camada com transparência, numa camada pequena num
   quadro grande e no export: o resultado deve ser o mesmo do preview.

Para desenvolver, reinstale com a **mesma versão** à vontade (igual substitui).

## 10. Solução de problemas

Mensagens exatas da instalação (`<...>` = valor do seu pacote):

| Mensagem | Causa e solução |
| --- | --- |
| `o arquivo nao e um pacote .aureaplugin (um ZIP com plugin.json)` | Não é ZIP (ou ZIP com método de compressão diferente de stored/deflate). Use o `pack_plugin.py`. |
| `pacote corrompido (ZIP invalido, nome de arquivo inseguro ou CRC errado)` | ZIP danificado ou com caminho como `../x` ou absoluto. |
| `pacote maior que 16 MB` | Tire arquivos que o plugin não usa. |
| `plugin.json ausente no pacote` / `plugin.json ausente ou ilegivel no pacote` | O `plugin.json` tem de estar na raiz do ZIP (ou numa única subpasta). |
| `plugin.json invalido: <detalhe>` | JSON malformado (vírgula sobrando, aspas). |
| `plugin.json: falta "aureaPlugin": 1 (versao do formato)` / `... nao suportado (este app le a versao 1 do formato)` | Declare `"aureaPlugin": 1`. |
| `plugin.json: "id" invalido "<id>": use 3 a 64 caracteres a-z, 0-9, '.', '_' ou '-' (ex.: com.estudio.brilho)` | Só minúsculas; sem espaço. |
| `plugin.json: "version" deve ser "x.y.z" (ex.: "1.0.0")` | — |
| `plugin.json: "kind" deve ser "shader" ou "stack"` | — |
| `plugin.json: <n> parametros (limite 16)` | Junte parâmetros ou faça dois plugins. |
| `plugin.json: parametro "<id>": id invalido (letras, digitos e _, ate 32, sem palavra reservada do GLSL nem prefixo aurea/gl_)` | Ex.: `mix`, `texture`, `float`, `uv`, `main`, `time`, `source` são reservados. |
| `plugin.json: parametro "<id>": tipo desconhecido "<t>" (use float, int, bool, enum, color, point ou angle)` | — |
| `plugin.json: parametro "<id>": "default" fora de [min, max]` | — |
| `plugin.json: parametro "<id>": enum precisa de "options" (lista de textos)` | — |
| `plugin.json: <n> passes (limite 8)` | Junte passes. |
| `plugin.json: passe <n>: falta "target" (so o ultimo passe escreve a saida)` | Todo passe menos o último precisa de `target`. |
| `plugin.json: o ultimo passe nao tem "target": ele e a saida do efeito` | Tire o `target` do último. |
| `plugin.json: passe <n>: "scale" deve estar entre 0.125 e 1` | — |
| `plugin.json: passe <n>: entrada "<nome>" le "<x>", que nao e "source" nem o target de um passe anterior` | Um passe só lê targets de passes **anteriores**. |
| `plugin.json: "margin" deve estar entre 0 e 1024 (px da camada)` | — |
| `passe <n>: caminho de shader invalido "<arquivo>"` | Caminho relativo, sem `..`, `:` ou `/` inicial. |
| `passe <n>: arquivo de shader ausente: <arquivo>` | O arquivo não está no pacote (maiúsculas contam). |
| `passe <n>: <arquivo> maior que 64 KB` | — |
| `<arquivo>:<linha>: <mensagem do glslang>` | Erro de compilação GLSL, na linha do **seu** arquivo. Ex.: `rgb_wave.frag:12: 'offst' : undeclared identifier`. Uma linha `aurea_header:...` indica conflito com o cabeçalho (ex.: redeclarar `aurea_source` ou escrever `#version`). |
| `<arquivo>: a GPU recusou o shader: <mensagem>` | O driver deste aparelho não aceitou o shader traduzido (ESSL/MSL). Simplifique (arrays grandes, recursos raros) e reporte o aparelho. |
| `o quadro de teste falhou: <detalhe>` | O efeito não desenhou (pipeline recusado, textura grande demais). |
| `o quadro de teste levou <t> ms (limite 250 ms em 256x256): simplifique o shader (lacos, amostras por pixel, passes)` | Seção 7 e 8. |
| `a GPU reiniciou durante o quadro de teste (laco infinito ou shader pesado demais)` | Laço sem fim ou custo extremo. |
| `a GPU esta ocupada (preview ou export); tente instalar de novo` | Um export ou captura estava com a GPU por mais de ~4 s. |
| `ja esta instalada a versao <v> de "<id>", mais nova que <w>` | Aumente a `version` (ou remova a instalada). |
| `plugin.json: stack[<i>] (<efeito>): efeito embutido desconhecido` | Chave errada (veja seção 5). |
| `plugin.json: stack[<i>] (<efeito>): este efeito nao pode entrar numa pilha (depende da camada, do tempo ou de dado proprio)` | Seção 5. |
| `plugin.json: stack[<i>] (<efeito>): parametro desconhecido "<p>"` | Use o id do parâmetro do embutido. |
| `plugin.json: stack[<i>] (<efeito>): "<p>" tem <n> componentes: use uma lista ou o id de um parametro do mesmo tipo` | Cor/ponto precisam de lista ou de cópia. |
| `plugin.json: stack[<i>] (<efeito>): "<p>": nome desconhecido '<x>' (use o id de um parametro do plugin) (coluna <c>)` | Erro de expressão (também: `funcao desconhecida`, `falta ')'`, `texto sobrando`). |
| `ISF: falta o cabecalho JSON /*{ ... }*/ no inicio do arquivo` | O `.fs` precisa começar com o comentário JSON. |
| `ISF: ISF com varios passes (PASSES) nao e suportado; use um plugin .aureaplugin com "passes"` | — |
| `ISF: entrada de audio "<nome>" nao e suportada` / `ISF: mais de uma imagem de entrada (...) nao e suportado` | — |
| `este build do app nao compila shaders de plugin` | Build sem o compilador (não acontece nos apps publicados). |

Plugins que **não carregam** na abertura do app (por exemplo, depois de uma
atualização que mudou o cabeçalho) continuam na lista com `ok: false` e a
mensagem em `error`; o efeito fica fora do catálogo até ser reinstalado.

## 11. Projetos, versões e remoção

- O projeto guarda o efeito pela chave `plugin.<id>` e os valores por índice de
  parâmetro, como qualquer efeito.
- Abrir um projeto **sem** o plugin instalado: a instância e todos os valores
  (e keyframes) ficam no projeto; o painel mostra o efeito como indisponível e a
  camada desenha sem ele. Salvar não perde nada. **Instalar o plugin depois**
  faz o efeito voltar com os valores.
- **Nova versão**: mantenha o `id` e a **ordem dos parâmetros** (o projeto
  guarda por índice). Acrescente parâmetros novos **no fim**; os projetos antigos
  recebem o `default` deles.
- Remover o plugin tira o efeito do catálogo e apaga a pasta; projetos ficam
  como no caso "sem o plugin".

## 12. API do motor (para quem integra a interface)

C++ (`engine/include/aurea/Engine.hpp`):

```cpp
Result<std::string> install_plugin(const char* path) noexcept;   // id, ou Status com detail() legível
std::vector<plugins::PluginInfo> list_plugins() const;
std::string plugins_json() const;                                  // a lista abaixo
bool remove_plugin(const char* id) noexcept;
void set_plugin_directory(const char* path) noexcept;              // troca a pasta e carrega
std::string plugin_directory() const;
// EngineConfig::pluginDirectory — a pasta lida na inicialização
```

Android (`AureaEngine.kt`, JNI em `aurea_jni.cpp`): `installPlugin(path)` →
`PluginInstallResult(ok, id, error)`; `installPluginRaw(path)` → `"ok:<id>"` ou
`"error:<mensagem>"`; `pluginsJson()`; `removePlugin(id)`; `pluginDirectory()`;
`initialize(..., pluginDir = defaultPluginDir(documentsDir))` (padrão
`files/plugins`). Nativos: `nativeInstallPlugin(handle, path): String`,
`nativeListPlugins(handle): String`, `nativeRemovePlugin(handle, id): Boolean`,
`nativePluginDirectory(handle): String`.

iOS (`AureaEngine.h`): `installPlugin:` → `"ok:<id>"`/`"error:<mensagem>"`,
`pluginsJSON`, `removePlugin:`, `pluginDirectory` (Documents/Plugins, ligado no
`startWithDevice:`).

JSON da lista (`plugins_json` / `nativeListPlugins` / `pluginsJSON`):

```json
[{
  "id": "com.aurea.examples.rgbwave", "version": "1.0.0", "author": "AUREA (exemplo)",
  "kind": "shader", "category": "Glitch",
  "name": {"pt-BR": "Onda RGB", "en": "RGB Wave"},
  "description": {"pt-BR": "...", "en": "..."},
  "effectKey": "plugin.com.aurea.examples.rgbwave", "effectTypeId": 2876543210,
  "ok": true, "errorCode": "", "error": "",
  "directory": "/data/user/0/.../files/plugins/com.aurea.examples.rgbwave",
  "passCount": 1, "testRenderMs": 0.8,
  "params": [{"id": "amount", "type": "float", "label": {"pt-BR": "...", "en": "..."},
              "animatable": true, "options": [ {"pt-BR": "..."} ]}]
}]
```

- `name`, `description`, `label` e cada opção são sempre objetos idioma → texto
  (`"*"` = o texto vale para todos os idiomas). Fallback: o idioma, `en`,
  `pt-BR`, `*`, o primeiro.
- `effectTypeId` é o id do efeito nas consultas do catálogo e dos efeitos da
  camada (`query_effect_catalog`, `query_layer_effects`): é por ele que a
  interface liga a linha do catálogo (categoria `Plugins`, nome em pt-BR) ao
  plugin e mostra o nome/rótulos no idioma do aparelho.
- `errorCode` (estável): `manifest`, `shader_source`, `shader_compile`, `stack`,
  `unsupported`, `duplicate`, `registry`. `testRenderMs` = -1 quando o plugin foi
  carregado da pasta (não medido nesta sessão).

## 13. Licenças

O compilador no aparelho usa duas bibliotecas de código aberto, baixadas no
build em versões fixas (URL + SHA-256 nos arquivos CMake):

| Biblioteca | Versão fixada | Licença | Onde |
| --- | --- | --- | --- |
| glslang (Khronos), a revisão que o ncnn fixa (`nihui/glslang` `a9ac7d5f307e5db5b8c4fbf904bdba8fca6283bc`, base 15.x) | `engine/cmake/AureaNcnn.cmake`, `engine/cmake/AureaRuntimeShaders.cmake` | BSD-3-Clause, BSD-2-Clause, MIT e Apache-2.0 (o `LICENSE.txt` também traz o aviso GPL-3.0 com a exceção do Bison, que cobre o analisador gerado e permite o uso sob qualquer licença) | GLSL → SPIR-V |
| SPIRV-Cross (Khronos) `d8e3e2b141b8c8a167b2e3984736a6baacff316c` | `engine/cmake/AureaShaderXlate.cmake` | Apache-2.0 | SPIR-V → ESSL (Android GLES) e MSL (iOS) |

Os avisos de licença devem acompanhar a tela de licenças do app. Os plugins são
do autor de cada um: o AUREA não redistribui plugins de terceiros. Os exemplos
desta pasta foram escritos para o AUREA e podem ser usados como base livremente.
