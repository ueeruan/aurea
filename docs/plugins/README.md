# Plugins de efeito do AUREA

> English version: [README.en.md](README.en.md)

> **0.3.4:** além de efeitos, um plugin pode trazer **painéis e abas** na
> interface, **scripts** com uma API no estilo do After Effects e **acesso a APIs
> da internet** nos domínios que declarar — veja a [seção 14](#14-painéis-scripts-e-apis-formato-2-034).

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
- [14. Painéis, scripts e APIs (formato 2, 0.3.4)](#14-painéis-scripts-e-apis-formato-2-034)

Exemplos prontos em [`examples/`](examples/):

| Pasta / arquivo | Tipo | O que mostra |
| --- | --- | --- |
| `rgb-wave/` → `rgb-wave.aureaplugin` | shader, 1 passe | separação RGB em onda; float, angle e color; `margin`; tempo |
| `soft-glow/` → `soft-glow.aureaplugin` | shader, 2 passes | passe em 1/4 da resolução (`scale`), `target` e `inputs` |
| `dream-look/` → `dream-look.aureaplugin` | stack | desfoque + brilho + saturação embutidos, com expressões |
| `pulse-vignette.fs` | ISF 2.0 | importação de um `.fs` (point2D, color, bool, TIME) |
| `target-3d/` → `target-3d.aureaplugin` | shader3d, 1 passe | anel em volta de um nulo 3D visto pela câmera da cena; parâmetro `layer` com `accept`; `aurea_ray` |
| `animated-text/` → `animated-text.aureaplugin` | panel + script (0.3.4) | **Gerador de texto animado**: painel, aba na Home, botão na barra da camada; cria camadas e keyframes |
| `weather-text/` → `weather-text.aureaplugin` | panel + script (0.3.4) | **Clima na tela**: `fetch` numa API pública declarada (Open-Meteo) e cria um texto |

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

**`"kind": "shader3d"`** (0.3.1) — um `shader` que também enxerga a **cena 3D**:
a câmera da composição e até 4 camadas ligadas (nulo 3D, câmera, luz). Serve
para efeitos que vivem no espaço 3D e acompanham a câmera. Veja
[Plugins 3D](#plugins-3d-kind-shader3d-031) na seção 4.

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

Um plugin 3D é um `shader` que também enxerga a **cena 3D**:

- a **câmera da cena**: a câmera ativa da composição, ou a câmera padrão quando
  não há nenhuma;
- o **mundo** de até **4 camadas ligadas** por parâmetros `layer`: um **nulo
  3D** como alvo ou pivô, uma **câmera**, uma **luz**.

Com isso o shader calcula em 3D: lança o raio da câmera por cada pixel,
projeta pontos do mundo na tela, mede distâncias até um nulo. Mover o nulo, a
câmera ou mudar a lente atualiza o efeito no mesmo quadro, no preview e no
export. Tudo o mais é igual a um `shader`: passes, `margin`, `colorSpace`,
parâmetros com keyframes e expressões, quadro de teste na instalação.

#### O manifesto

```json
{
  "aureaPlugin": 1,
  "id": "com.estudio.mira",
  "version": "1.0.0",
  "name": { "pt-BR": "Mira 3D", "en": "3D Target" },
  "category": "3D",
  "kind": "shader3d",
  "params": [
    { "id": "alvo", "type": "layer", "accept": ["null"],
      "label": { "pt-BR": "Alvo (nulo 3D)", "en": "Target (3D null)" } },
    { "id": "raio", "type": "float", "default": 120, "min": 4, "max": 1200 }
  ],
  "passes": [ { "shader": "mira.frag" } ]
}
```

- `"kind": "shader3d"` liga o modo 3D. Um parâmetro `layer` num plugin
  `"shader"` comum é recusado na instalação.
- **Parâmetro `layer`**: no painel vira uma lista das camadas da composição.
  - `accept` filtra a lista: `"camera"`, `"null"`, `"light"`, ou `"any"`
    (qualquer camada; é o padrão sem `accept`). Pode combinar:
    `["null", "camera"]`.
  - Começa sem camada (`aurea_layer_valid` = falso).
  - Nunca é animável: a camada ligada é fixa, o que anima é a camada.
  - Até **4** por plugin, contando todos os `layer`.
  - Ligar a própria camada que tem o efeito não vale: fica como sem camada.

#### O que o shader recebe

O bloco `aurea` ganha estes campos **depois** de `params[16]`; nada antes
muda, então um shader 2D colado num `shader3d` continua funcionando:

| Símbolo | Tipo | Conteúdo |
| --- | --- | --- |
| `aurea.view` | `mat4` | vista ← mundo (x para a direita, y para baixo, z para a frente) |
| `aurea.compFromWorld` | `mat4` | px da composição (homogêneo) ← mundo |
| `aurea.compFromLayer` | `mat4` | px da composição (homogêneo) ← px da camada que tem o efeito |
| `aurea.camera` | `vec4` | `xyz` posição da câmera no mundo; `w` = 1 câmera de camada, 0 câmera padrão |
| `aurea.lens` | `vec4` | `x` campo de visão vertical (rad), `y` plano próximo (px), `z` aspecto (largura/altura), `w` distância focal em px |
| `aurea.comp` | `vec4` | `xy` tamanho da composição (px), `zw` = 1/xy |
| `aurea.layerWorld[4]` | `mat4` | mundo ← px de cada camada ligada (com os pais) |
| `aurea.layerPos[4]` | `vec4` | `xyz` ponto de âncora da camada ligada no mundo; `w` = 1 ligada, 0 sem camada |
| `aurea.layerInfo[4]` | `vec4` | `x` tipo (6 nulo, 8 câmera, 9 luz; outros = outra camada), `y` campo de visão (rad) se for câmera, `z` = 1 se a camada é 3D |

Funções prontas (só em `shader3d`; o cabeçalho define `AUREA_SCENE3D 1`):

| Função | O que faz |
| --- | --- |
| `bool aurea_layer_valid(int i)` | há camada ligada, visível e no tempo dela neste quadro |
| `vec3 aurea_layer_position(int i)` | o ponto de âncora da camada ligada no mundo |
| `mat4 aurea_layer_matrix(int i)` | mundo ← px da camada ligada (posição, rotação X/Y/Z, escala e pais) |
| `vec3 aurea_layer_axis(int i, int a)` | o eixo `a` da camada no mundo, normalizado (0 = x, 1 = y, 2 = z) |
| `int aurea_layer_kind(int i)` | o tipo: 6 nulo, 8 câmera, 9 luz |
| `float aurea_layer_fov(int i)` | o campo de visão vertical (rad) da câmera ligada |
| `vec3 aurea_camera_position()` | a posição da câmera da cena |
| `vec3 aurea_camera_right()`, `aurea_camera_down()`, `aurea_camera_forward()` | os eixos da câmera da cena no mundo |
| `vec2 aurea_comp_px(vec2 uv)` | o `uv` do passe em px da composição |
| `void aurea_ray(vec2 uv, out vec3 ro, out vec3 rd)` | o raio da câmera que passa por este pixel: origem `ro` e direção `rd` (normalizada) no mundo |
| `vec3 aurea_world_to_comp(vec3 p)` | ponto do mundo → px da composição; `z` = profundidade homogênea (≤ 0 = atrás da câmera) |
| `vec3 aurea_world_to_view(vec3 p)` | ponto do mundo → espaço da câmera (`z` = distância à frente dela) |

`i` é o **nome do parâmetro `layer`**: no cabeçalho ele vira a posição dele
entre os parâmetros `layer` (o primeiro `layer` = 0, o segundo = 1...). Escreva
`aurea_layer_position(alvo)`, não `aurea.params[...]`.

#### O mundo do AUREA

- A unidade é o **px da composição**: x para a direita, y para **baixo**, z
  para **longe** da câmera, como no After Effects.
- Com a câmera padrão, o plano z = 0 aparece 1:1 na composição: o ponto
  `(x, y, 0)` cai no pixel `(x, y)`.
- A posição de uma camada (incluindo z) e o ponto de âncora valem no mundo como
  no painel de transformação. `aurea_layer_position` devolve o ponto de
  âncora já com a posição, a rotação, a escala e os pais aplicados.
- `aurea_layer_axis(i, 2)` é a "frente" (z) da camada: use como normal de um
  plano que gira com o nulo.

#### Exemplo: anel em volta de um nulo

Este é o `examples/target-3d`, um anel desenhado no plano do nulo e visto em
perspectiva (parâmetros dele: `target`, `radius`, `width`, `tint`; aqui sem o halo):

```glsl
vec4 aurea_main(vec2 uv) {
    vec4 base = texture(aurea_source, uv);
    // Sem nulo ligado: o anel fica no centro da composição, no plano z = 0.
    vec3 center = aurea_layer_valid(target) ? aurea_layer_position(target) : vec3(aurea.comp.xy * 0.5, 0.0);
    vec3 normal = aurea_layer_valid(target) ? aurea_layer_axis(target, 2) : vec3(0.0, 0.0, 1.0);
    vec3 ro, rd;
    aurea_ray(uv, ro, rd);
    float facing = dot(rd, normal);
    if (abs(facing) < 1e-5) return base;            // raio paralelo ao plano
    float t = dot(center - ro, normal) / facing;
    if (t <= 0.0) return base;                       // o plano está atrás da câmera
    vec3 hit = ro + rd * t;                          // onde o raio corta o plano do nulo
    float d = abs(length(hit - center) - radius);
    float ring = smoothstep(width, 0.0, d);
    vec3 light = tint.rgb * ring;
    return vec4(base.rgb + light, max(base.a, ring));
}
```

Outras receitas comuns:

- **Ponto na tela**: `vec3 s = aurea_world_to_comp(p);` e se `s.z > 0.0`, o
  ponto aparece em `s.xy` (px da composição). Compare com
  `aurea_comp_px(uv)` para desenhar um brilho, uma mira ou um rótulo.
- **Tamanho em perspectiva**: `aurea_world_to_view(p).z` é a distância à
  câmera; `aurea.lens.w / z` converte px do mundo em px na tela naquela
  distância.
- **Céu ou fundo infinito**: use só a direção `rd` de `aurea_ray` (a posição
  não importa no infinito), como a Aurora.
- **Luz ligada**: `aurea_layer_position(luz)` dá a posição da luz para um
  brilho ou um raio de luz que segue a luz da cena.

#### Onde aplicar o plugin

- Aplique numa camada **2D do tamanho da composição**, como um sólido
  transparente: o resultado cobre o quadro inteiro e a câmera não move a camada.
- Numa camada 3D, a câmera também move a própria camada (o efeito sai no plano
  dela). As contas continuam certas para os pixels que a camada cobre, mas fora
  dela não aparece nada.
- O efeito é desenhado na região da camada (mais a `margin`). Para desenhar fora
  dela, aumente a camada ou a `margin`.

#### Limites e cuidados

- A projeção é a mesma em todas as GPUs (Vulkan, OpenGL ES e Metal), e o
  quadro de teste da instalação roda com a câmera padrão e sem camada ligada.
  Teste o caso `aurea_layer_valid(...) == false`.
- O bloco 3D ocupa 944 dos 1024 bytes de parâmetros por passe; os 16
  parâmetros continuam valendo.
- Não existe acesso a modelos 3D, malhas ou profundidade da cena: o plugin
  enxerga a câmera e as camadas ligadas, não a geometria.

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
| Camadas ligadas (`layer`) | 4, só em `shader3d` | O mundo de cada camada vai no bloco de parâmetros; o bloco 3D ocupa 944 dos 1024 bytes. |
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
   passes, entradas, pilha, tamanhos, `shader3d`, `layer` e `accept`). A compilação do GLSL e o quadro de teste
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
| `plugin.json: "kind" deve ser "shader", "shader3d" ou "stack"` | — |
| `plugin.json: <n> parametros (limite 16)` | Junte parâmetros ou faça dois plugins. |
| `plugin.json: parametro "<id>": id invalido (letras, digitos e _, ate 32, sem palavra reservada do GLSL nem prefixo aurea/gl_)` | Ex.: `mix`, `texture`, `float`, `uv`, `main`, `time`, `source` são reservados. |
| `plugin.json: parametro "<id>": tipo desconhecido "<t>" (use float, int, bool, enum, color, point, angle ou layer)` | — |
| `plugin.json: parametro "layer" so em plugin "kind": "shader3d"` | Troque `"kind": "shader"` por `"shader3d"`. |
| `plugin.json: <n> parametros "layer" (limite 4)` | Use até 4 camadas ligadas. |
| `plugin.json: parametro "<id>": "accept" deve ser uma lista (camera, null, light, any)` / `... "accept" desconhecido "<x>" (camera, null, light ou any)` | Ex.: `"accept": ["null", "camera"]`. |
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
- `scene3d: true` aparece nos plugins `"kind": "shader3d"` (o `kind` da lista
  continua `shader`). A interface mostra "Shader 3D (câmera e nulos)".
  Parâmetros `layer` vêm com `"type": "layer"`.

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

Os scripts dos plugins (0.3.4) rodam no **AureaScript**, interpretador escrito para o
AUREA (`engine/src/plugins/script/`): não há biblioteca de terceiros nova.

## 14. Painéis, scripts e APIs (formato 2, 0.3.4)

A partir da 0.3.4 um plugin pode fazer muito mais do que um efeito: **mudar a
interface** (abas na Home, painéis no editor, itens de menu, botões na barra da
camada), **rodar scripts** com uma API de objetos no estilo dos scripts do After
Effects e **buscar dados de APIs da internet** nos domínios que declarar. Tudo
isso vem no mesmo `.aureaplugin`, com `"aureaPlugin": 2`.

- [14.1 O manifesto do formato 2](#141-o-manifesto-do-formato-2)
- [14.2 Painéis e controles](#142-painéis-e-controles)
- [14.3 Ações (menus e barra da camada)](#143-ações-menus-e-barra-da-camada)
- [14.4 Scripts: a linguagem AureaScript](#144-scripts-a-linguagem-aureascript)
- [14.5 Referência da API](#145-referência-da-api)
- [14.6 Permissões e rede](#146-permissões-e-rede)
- [14.7 Limites, erros e console](#147-limites-erros-e-console)
- [14.8 Exemplos](#148-exemplos)
- [14.9 API do motor (interface)](#149-api-do-motor-interface)

### 14.1 O manifesto do formato 2

```json
{
  "aureaPlugin": 2,
  "id": "com.estudio.texto",
  "version": "1.0.0",
  "name": {"pt-BR": "Texto animado", "en": "Animated text"},
  "kind": "panel",
  "script": "main.js",
  "permissions": {"network": ["api.exemplo.com"], "clipboard": false, "files": false},
  "strings": {"pt-BR": {"pronto": "{0} camadas criadas"}, "en": {"pronto": "{0} layers created"}},
  "ui": {"panels": [ ... ], "actions": [ ... ]}
}
```

| Campo | O que é |
| --- | --- |
| `aureaPlugin` | `2`. Um app antigo (que só lê o 1) recusa com "versão do formato não suportada". `ui`, `script`, `permissions` e `strings` com `1` são recusados. |
| `kind` | `"panel"` (novo: plugin sem efeito, só interface e script) ou os tipos de efeito (`shader`, `shader3d`, `stack`), que agora também podem ter interface e script. |
| `script` | Arquivo `.js` do pacote (até 512 KB). A sintaxe é conferida na instalação: erro = recusa com `arquivo:linha:coluna`. |
| `permissions` | O que o plugin pede (seção 14.6). Campos desconhecidos são recusados. |
| `strings` | Textos do script por idioma (`app.t("chave", ...)`, com `{0}`, `{1}`...). Até 512 chaves por idioma. |
| `ui` | `panels` (até 8) e `actions` (até 16). |

Todo texto (`title`, `label`, `options`, `placeholder`, `text`) aceita texto
simples ou objeto idioma → texto, como `name`. O app mostra o idioma dele, com a
mesma ordem de fallback da seção 3. Os limites de tamanho contam **caracteres**
(hindi, russo e árabe valem igual ao português).

### 14.2 Painéis e controles

```json
{
  "id": "main",
  "title": {"pt-BR": "Texto animado", "en": "Animated text"},
  "icon": "sparkles",
  "placement": ["editor.panel", "home.tab"],
  "onOpen": "abriu",
  "controls": [
    {"id": "texto", "type": "text", "label": "Texto", "default": "AUREA"},
    {"id": "gerar", "type": "button", "label": "Gerar", "onClick": "gerar"}
  ]
}
```

- **`placement`**: `editor.panel` — o painel abre no editor, na moldura comum dos
  painéis (o ‹ fecha); aparece no **menu de plugins** (botão de chave inglesa no
  topo do editor) e em Plugins > "Abrir". `home.tab` — vira uma **aba da Home**: com
  uma aba de plugin, o dock mostra o nome e o ícone dela; com várias, uma aba
  "Plugins" com um seletor no topo.
- **`icon`**: um destes nomes (as duas plataformas usam os ícones do AUREA):
  `wand sparkles text star bolt cloud music photo film layers gear brush clock list
  cube lightbulb wrench book flag grid camera chart sliders eye folder link speed
  waveform`. Outro nome = recusa.
- **`onOpen`**: função chamada quando o painel abre (opcional).
- **`controls`**: 1 a 32. O `id` de cada controle vale para o plugin inteiro
  (o script usa `ui.get("id")`).

| `type` | Campos | Valor (`ui.get`) | Evento |
| --- | --- | --- | --- |
| `button` | `label` (obrigatório), `style`: `primary` · `secondary` · `danger` | — | `onClick` (obrigatório) |
| `slider` | `min`, `max`, `step` (0 = contínuo), `default`, `unit` | número | `onChange` (ao soltar) |
| `ruler` | régua de arrastar: `min`/`max` opcionais (±1e6), `step`, `default`, `unit` | número | `onChange` (ao soltar) |
| `color` | `default` `"#RRGGBB"` ou `"#RRGGBBAA"` | `"#RRGGBB"` | `onChange` |
| `choice` | `options` (1 a 32 textos), `default` (índice) | índice | `onChange` |
| `text` | `default`, `placeholder`, `multiline`, `maxLength` (≤ 10000) | texto | `onChange` (ao confirmar) |
| `list` | `items` iniciais `[{id, label, detail}]` (≤ 200) | id escolhido ou `null` | `onSelect` |
| `image` | `src` (`.png`/`.jpg` do pacote, ≤ 2 MB), `aspect` (largura/altura) | — | — |
| `label` | `text` | o texto | — |
| `toggle` | `default` (`true`/`false`) | booleano | `onChange` |

As funções recebem `(valor, idDoControle)`. Toda chamada vinda da interface é **um
passo de desfazer** (veja `app.beginUndoGroup`).

### 14.3 Ações (menus e barra da camada)

```json
{"id": "rapido", "title": "Animar camada", "icon": "bolt", "placement": "layer.toolbar", "call": "animar"}
```

| `placement` | Onde aparece |
| --- | --- |
| `editor.menu` | no menu de plugins do topo do editor |
| `layer.menu` | no ⋮ da camada (seção do plugin no fim) |
| `layer.toolbar` | botão na barra da camada escolhida (`icon` obrigatório) |

Cada ação tem **`call`** (função do script) **ou** **`panel`** (abre esse painel,
que precisa ter `editor.panel`). `needsLayer` (padrão `true` nas ações da camada).

### 14.4 Scripts: a linguagem AureaScript

O script é **JavaScript** (um subconjunto do ES2020) rodado pelo **AureaScript**,
o interpretador do próprio AUREA, igual no Android e no iOS. O arquivo roda uma
vez quando o plugin é usado pela primeira vez (definindo funções e variáveis
globais, que **persistem entre chamadas**); depois a interface chama as funções.

Tem: `let`/`const`/`var` (todos com escopo de bloco), funções, arrow functions,
closures, `this` em métodos, `new` com funções construtoras, objetos, listas,
template strings, desestruturação (`const {a, b} = obj`, `[x, ...resto]`, também
em parâmetros), spread (`...`), parâmetros padrão, `if`/`switch`/`for`/`for...of`/
`for...in`/`while`/`do`, `try`/`catch`/`finally`/`throw`, `?.`, `??`, `??=`, `**`,
`typeof`, `instanceof`, `in`, `delete`.

Biblioteca: `Math`, `JSON` (`parse`, `stringify` com indentação), `Object`
(`keys`, `values`, `entries`, `assign`, `fromEntries`, `freeze`, `hasOwn`),
`Array` (`isArray`, `from`, `of` e os métodos `push pop shift unshift slice
splice concat join reverse indexOf lastIndexOf includes at forEach map filter
find findIndex findLast findLastIndex some every reduce reduceRight sort flat
flatMap fill`), métodos de texto (`toUpperCase toLowerCase trim trimStart
trimEnd includes startsWith endsWith indexOf lastIndexOf slice substring substr
charAt charCodeAt codePointAt at split replace replaceAll repeat padStart padEnd
concat localeCompare`), `Number` (`toFixed`, `toPrecision`, `toString(base)`,
`isInteger`...), `String`, `Boolean`, `parseInt`, `parseFloat`, `isNaN`,
`isFinite`, `encodeURIComponent`, `decodeURIComponent`, `Date` (UTC: `Date.now`,
`new Date()`, `toISOString`, `getFullYear`...), `Error`, `TypeError`,
`RangeError`, `ReferenceError`, `SyntaxError`, `console.log/info/warn/error`.

Não tem (recusa com mensagem própria): `class`, `async`/`await`/Promise,
geradores, expressões regulares (use `split`, `includes`, `replaceAll`),
`import`/`export`/`require`, `eval`, `Function`, rótulos, getters/setters em
literais. Também **não existem** `window`, `document`, `XMLHttpRequest`,
`process`, `globalThis` nem nada que toque arquivos ou o sistema: o script só
alcança o que está na referência abaixo.

Diferenças pequenas do JavaScript: textos contam **caracteres** (`"ação".length`
é 4); `var` tem escopo de bloco; `Math.random` é reproduzível por plugin.

### 14.5 Referência da API

Tempos em **segundos da composição**; índices de camada **a partir de 1, de cima
para baixo** (como no After Effects); posição e âncora em pixels, escala e
opacidade em %, rotação em graus; cores `[r, g, b]` (ou `[r, g, b, a]`) de 0 a 1
ou `"#RRGGBB"`.

**`app`**

| Membro | |
| --- | --- |
| `app.version` | `"0.3.4"` |
| `app.language` | idioma do app na chamada (`"pt-BR"`, `"en"`...) |
| `app.plugin` | `{id, version, name}` |
| `app.project` | o projeto (abaixo); `app.project.activeItem` é `null` sem projeto aberto |
| `app.beginUndoGroup(nome)` / `app.endUndoGroup()` | agrupa alterações num passo de desfazer (aninhável). Cada chamada da interface já é um grupo; grupos esquecidos abertos fecham no fim. |
| `app.alert(texto)` | alerta com OK |
| `app.t(chave, ...args)` | texto de `strings` no idioma do app (`{0}`, `{1}` trocados pelos argumentos) |

**`app.project`**: `name`, `numItems`, `item(1)`, `activeItem` (a composição aberta).

**Composição** (`app.project.activeItem`)

| Membro | |
| --- | --- |
| `name`, `width`, `height`, `frameRate`, `frameDuration`, `numLayers` | leitura |
| `duration` | leitura e escrita (segundos) |
| `time` | leitura e escrita: o cabeçote |
| `layer(índice ou nome)` | uma camada (`null` se não houver) |
| `layers` | coleção como a do AE: `layers.length`, `layers[1]`... e `addText(texto)`, `addShape(forma)`, `addSolid(cor, nome, largura, altura)`, `addNull(3d)`, `addCamera()`, `addLight(tipo)`, `byName(nome)`, `toArray()` |
| `allLayers` | lista JavaScript das camadas (de cima para baixo) |
| `selectedLayers` | camadas escolhidas |
| `markers` | `[{time, name, color, beat}]` |
| `addMarker(tempo, nome, cor)` / `removeMarker(tempo)` | marcadores da composição |
| `renderFrame({maxSize})` | desenha o quadro do cabeçote (até 1024 px): `{width, height, pixel(x, y), average()}`; pode ir para um controle `image` com `ui.setImage` |

`addShape` aceita um índice de forma ou `circle`, `roundedSquare`, `cross`,
`ring`, `triangle`, `hexagon`, `arrow`, `square`, `star`, `capsule`, `heart`,
`line`, `diamond`.

**Camada**

| Membro | |
| --- | --- |
| `id`, `index`, `type` (`text shape video image audio null camera light model3d particles group adjustment`) | leitura |
| `name`, `inPoint`, `outPoint`, `enabled`, `locked`, `threeD`, `selected`, `parent` | leitura e escrita |
| `startTime` | leitura |
| `text`, `fontSize` | camadas de texto |
| `fillColor` | texto e forma |
| `position`, `scale`, `rotation`, `rotationX`, `rotationY`, `rotationZ`, `opacity`, `anchorPoint` | propriedades (abaixo); também em `transform.*` e `property("Position")` |
| `effects`, `effect(nome, chave ou índice)`, `addEffect(chave)` | efeitos (`"aurea.blur.gaussian"`...) |
| `remove()`, `duplicate()`, `moveTo(índice)`, `moveToBeginning()`, `moveToEnd()` | |

**Propriedade**

| Membro | |
| --- | --- |
| `name`, `value`, `dimensions`, `numKeys`, `isTimeVarying` | leitura |
| `valueAtTime(t)` | valor no tempo |
| `setValue(v)` | valor parado. Com keyframes, recusa (use `setValueAtTime`), como no AE |
| `setValueAtTime(t, v)`, `setValuesAtTimes(ts, vs)`, `addKey(t)` | keyframes |
| `keyTime(k)`, `keyValue(k)`, `nearestKeyIndex(t)`, `removeKey(k)` | `k` a partir de 1 |
| `setInterpolationTypeAtKey(k, tipo)` | `linear`, `hold`, `bezier`, `ease`, `easeIn`, `easeOut` |

**Efeito**: `name`, `matchName` (a chave), `enabled` (leitura e escrita),
`numProperties`, `property(id, rótulo ou índice)` (uma Propriedade), `remove()`.

**`ui`** (os controles do plugin)

| Função | |
| --- | --- |
| `ui.get(id)` / `ui.set(id, valor)` | valor do controle (o tipo vale: cor inválida = `TypeError`) |
| `ui.setItems(id, itens)` | itens de uma `list`: textos ou `{id, label, detail}` |
| `ui.setText(id, texto)` | texto de um `label` |
| `ui.setLabel(id, texto)` | troca o rótulo de qualquer controle |
| `ui.setEnabled(id, sim)` / `ui.setVisible(id, sim)` | |
| `ui.setImage(id, arquivo ou quadro)` | `"capa.png"` do pacote, um `renderFrame()` ou `null` |
| `ui.progress(fração, texto)` | barra de progresso enquanto a função roda (`null` tira) |
| `ui.toast(texto)` / `ui.alert(texto)` | mensagens |
| `ui.openPanel(id)` | abre outro painel do plugin no editor |

**`storage`** (do plugin, persistente, 256 KB): `get(chave, padrão)`, `set(chave,
valor)` (valor que vira JSON), `remove(chave)`, `keys()`.

**`fetch(url, opções)`** (permissão de rede): síncrono, devolve
`{ok, status, url, headers, text(), json()}`. Opções: `method` (`GET POST PUT
PATCH DELETE HEAD`), `headers` (objeto), `body` (texto ou objeto → JSON),
`timeout` (ms, 1000 a 30000). Erros: `PermissionError` (sem permissão / domínio
fora da lista) e `NetworkError` (sem conexão, tempo, resposta grande demais).

**`clipboard.readText()` / `clipboard.writeText(texto)`** (permissão de área de transferência).

**`files.pick({types: ["json", "txt", "csv"]})`** (permissão de arquivos): abre o
seletor do sistema e devolve `{name, size, text(), json()}` (até 1 MB) ou `null`
se a pessoa cancelar. Só o arquivo escolhido; nada mais do disco.

**`console.log/info/warn/error`**: vão para o console do plugin (Plugins > Console).

### 14.6 Permissões e rede

```json
"permissions": {"network": ["open-meteo.com"], "clipboard": true, "files": true}
```

- **`network`**: até 8 domínios, só o nome (`api.exemplo.com`, sem `https://`,
  porta ou caminho; `*` é recusado). Vale o domínio e os **subdomínios** dele
  (`open-meteo.com` libera `api.open-meteo.com`). O `fetch` só aceita `https://`,
  sem porta explícita nem `usuario@`, e **não segue redirecionamentos** (um 3xx
  volta ao script). Resposta até 2 MB, corpo de envio até 256 KB, 15 s por padrão.
- **`clipboard`** e **`files`**: como acima.
- **Consentimento**: ao instalar, o app mostra a folha com o que o plugin pede;
  cada permissão vem com uma chave que a pessoa pode desligar antes de instalar.
  Depois, em Plugins, cada permissão pode ser ligada e desligada.
- **Permissão negada** (não pedida no manifesto ou desligada pela pessoa): a
  função lança `PermissionError` e nenhum byte sai do aparelho. Trate com
  `try/catch` se quiser uma mensagem própria.

### 14.7 Limites, erros e console

| Limite | Valor | Ao estourar |
| --- | --- | --- |
| Tempo de CPU por chamada | 5 s (esperas de rede e do seletor não contam) | `LimitError`, a chamada para |
| Passos por chamada | 20 milhões | `LimitError` |
| Memória do script | 32 MB vivos | `LimitError` |
| Profundidade de chamadas | 64 | `RangeError` |
| Script | 512 KB | recusa na instalação |

Limites e cancelamento (botão **Parar** do painel) **atravessam o `try/catch`** do
script: um laço infinito não prende o app. O contexto continua válido depois.
Ciclos de memória (closures que se apontam) são coletados sozinhos.

Erros aparecem **no painel** (com `arquivo:linha:coluna`) e no **console** do
plugin (últimas 200 linhas, com o `console.log` do script). Um script que falha
não derruba o app nem o projeto; o que ele já tinha mudado desfaz num toque.
Erro na carga do arquivo: corrija, reinstale ou use "Recarregar script" no console.

**Ligar/desligar**: em Plugins, a chave "Ligado" esconde painéis e ações e impede
o script de rodar. O efeito do plugin (se houver) continua no catálogo — os
projetos que o usam não mudam.

### 14.8 Exemplos

| Pasta / arquivo | O que mostra |
| --- | --- |
| `animated-text/` → `animated-text.aureaplugin` | **Gerador de texto animado**: painel no editor e aba na Home; texto, escolha, slider, régua, chave, cor; cria camadas de texto com keyframes (opacidade, posição, escala, rotação, `ease`) num passo de desfazer; botão na barra da camada que anima as camadas escolhidas; `storage`; textos nos 7 idiomas |
| `weather-text/` → `weather-text.aureaplugin` | **Clima na tela**: `fetch` na API pública Open-Meteo (`permissions.network: ["open-meteo.com"]`, com os subdomínios `geocoding-api` e `api`), lista de resultados preenchida pelo script, `NetworkError` tratado, cria uma camada de texto com a temperatura |

Empacote com `python docs/plugins/tools/pack_plugin.py <pasta>` (confere o
formato 2: ids, ícones, lugares, permissões e se cada função chamada pela
interface existe no script).

### 14.9 API do motor (interface)

C++ (`Engine.hpp`, bloco `0.3.4 plugins`):

```cpp
std::string inspect_plugin(const char* path) noexcept;          // lê sem instalar: o que pede
bool set_plugin_enabled(const char* id, bool on) noexcept;
bool set_plugin_permissions(const char* id, u32 granted) noexcept;   // 1 rede, 2 clipboard, 4 arquivos
std::string plugin_call(const char* id, const char* fn, const char* argsJson, const char* locale) noexcept;
std::string plugin_event(const char* id, const char* control, const char* kind, const char* valueJson, const char* locale) noexcept;
std::string plugin_open_panel(const char* id, const char* panel, const char* locale) noexcept;
std::string plugin_state(const char* id) noexcept;
std::string plugin_console(const char* id) noexcept;
void plugin_clear_console(const char* id) noexcept;
void plugin_cancel(const char* id) noexcept;       // qualquer thread
void plugin_reload(const char* id) noexcept;
void set_plugin_host_io(plugins::HostIO io) noexcept;   // fetch, clipboard, seletor da plataforma
```

`plugin_call`/`plugin_event`/`plugin_open_panel` **bloqueiam** (o script roda na
thread de quem chama): a interface chama de uma thread de pilha grande (8 MB).
Uma chamada de script por vez no motor; um plugin ocupado responde `BusyError`.
A resposta:

```json
{"ok": true, "value": <retorno da função em JSON>,
 "error": {"name": "TypeError", "message": "...", "text": "main.js:12:5: TypeError: ...",
           "file": "main.js", "line": 12, "column": 5, "aborted": false},
 "messages": [{"kind": "toast" | "alert" | "select", "text": "..."}],
 "openPanel": "",
 "state": {"values": {...}, "items": {...}, "texts": {...}, "labels": {...},
           "disabled": [...], "hidden": [...], "images": {...},
           "progress": 0.5, "progressText": "", "busy": false}}
```

(`"error"` só quando `ok` é falso; `progress` e uma `list` sem escolha vêm como `[]`.)
`plugins_json()` ganhou: `format`, `hasScript`, `enabled`, `generation`,
`permissions` `{network[], clipboard, files}`, `granted` `{network, clipboard,
files}` e `ui` (`panels` com `controls`, e `actions`). O estado ligado/permissões
fica em `<pasta de plugins>/.estado.json`; `storage` e quadros em `<pasta>/.dados/`.

Android: `AureaEngine.kt` (`inspectPlugin`, `setPluginEnabled`,
`setPluginPermissions`, `pluginCall`, `pluginEvent`, `pluginOpenPanel`,
`pluginState`, `pluginConsole`, `pluginClearConsole`, `pluginCancel`,
`pluginReload`, `installPluginHost`); a E/S em `effects/PluginHost.kt`; a interface
em `effects/PluginRuntime.kt` e `effects/PluginUi.kt`. iOS: `AureaEngine.h`
(mesmos nomes, `installPluginHost` e o bloco `pluginPickFile`) e `PluginsV2.swift`.
