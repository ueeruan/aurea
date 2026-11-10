# Plugin V3 — guia para criadores

O formato 3 deixa um plugin do AUREA ir bem além de um shader com sliders: ele
recebe curvas, degradês e outras camadas, lembra o quadro anterior, ouve o som,
muda os próprios parâmetros a cada quadro e monta uma interface com abas e
seções, que pode até substituir painéis do editor.

Tudo continua **declarado** no `plugin.json`. Quem desenha a interface são os
componentes do próprio AUREA, iguais no Android e no iOS. O pacote não traz
binário nem HTML. Plugins dos formatos 1 e 2 continuam funcionando sem mudança.

Este guia é o caminho rápido. A referência completa (cabeçalho do shader,
limites, API inteira de script) está no [README](README.md), seções 1 a 15.

---

## 1. Primeiro plugin V3 em 5 minutos

Uma pasta com três arquivos:

```
meu-plugin/
├── plugin.json
├── efeito.frag
└── frame.js
```

**plugin.json**

```json
{
  "aureaPlugin": 3,
  "id": "com.seunome.pulso",
  "version": "1.0.0",
  "author": "Seu nome",
  "name": { "pt-BR": "Pulso", "en": "Pulse" },
  "kind": "shader",
  "params": [
    { "id": "amount", "type": "float", "label": { "pt-BR": "Força", "en": "Strength" },
      "default": 0.5, "min": 0, "max": 1 },
    { "id": "paint", "type": "gradient", "label": { "pt-BR": "Cores", "en": "Colors" } }
  ],
  "uses": ["audio"],
  "frameScript": "frame.js",
  "passes": [ { "shader": "efeito.frag" } ]
}
```

**efeito.frag**

```glsl
vec4 aurea_main(vec2 uv) {
    vec4 c = aurea_unpremultiply(texture(aurea_source, uv));
    vec3 cor = paint(aurea_luma(c.rgb)).rgb;
    float batida = aurea_audio(0.1) * AUREA_AUDIO_PEAK;
    vec3 rgb = mix(c.rgb, cor, amount) * (1.0 + batida);
    return vec4(rgb * c.a, c.a);
}
```

**frame.js**

```js
function onFrame(f) {
  return { amount: 0.5 + 0.5 * Math.sin(f.time * 2) };
}
```

Empacote e instale:

```
python docs/plugins/tools/pack_plugin.py meu-plugin
```

Copie o `meu-plugin.aureaplugin` para o aparelho e abra
**Efeitos → Plugins: instalar e gerenciar → Instalar plugin**.

---

## 2. O que muda do formato 2 para o 3

| Recurso | Formato 2 | Formato 3 |
| --- | --- | --- |
| Tipos de parâmetro | float, int, bool, enum, color, point, angle, layer | + `curve`, `gradient`, `layerImage` |
| Quadro anterior | — | `uses: ["history"]` |
| Som | — | `uses: ["audio"]` |
| Parâmetros animados por código | só keyframe e expressão | `frameScript` a cada quadro |
| Interface | lista de controles | árvore: `row`, `column`, `section`, `tabs` |
| Onde aparece | painel do editor, menus | + tela cheia, item no Adicionar, substituir painéis |
| Script | painéis, ações, API | + expressões, ease, texto, máscaras |

Para migrar, troque `"aureaPlugin": 2` por `3`. O resto do manifesto continua
válido.

---

## 3. Parâmetros novos

Valem em plugins `"kind": "shader"` e `"shader3d"`.

| `type` | No painel | No shader | Custo |
| --- | --- | --- | --- |
| `curve` | editor de curva (mestra, R, G, B) | `float id(float t)` | 16 posições |
| `gradient` | editor de degradê, até 16 cores | `vec4 id(float t)`, RGBA linear | 32 posições |
| `layerImage` | escolher uma camada | `sampler2D id` e `bool id_connected` | 1 sampler |

- **`curve`:** chame como função. `tone(0.5)` devolve a curva no meio.
- **`gradient`:** `paint(t)` devolve a cor em `t` de 0 a 1, já em luz linear.
- **`layerImage`:** a camada escolhida chega no tamanho da composição, com os
  efeitos e a transformação dela. Sem camada, `id_connected` é `false` e o
  sampler devolve a própria entrada; sempre teste antes de usar.

As posições contam no limite de parâmetros (252 em `shader`, 189 em
`shader3d`), e os samplers contam no limite de entradas por passe.

---

## 4. Quadro anterior e som (`uses`)

```json
"uses": ["history", "audio"]
```

**`history`** dá ao shader `sampler2D aurea_history`, o resultado do quadro
anterior, e o macro `AUREA_HISTORY`. Serve para rastro, eco e feedback:

```glsl
vec4 antes = aurea_unpremultiply(texture(aurea_history, uv));
rgb = mix(rgb, antes.rgb, rastro * antes.a);
```

**`audio`** dá:

| Símbolo | O que é |
| --- | --- |
| `float aurea_audio(float banda)` | energia da banda, de 0 (graves) a 1 (agudos) |
| `float AUREA_AUDIO_PEAK` | pico do quadro |
| `sampler2D aurea_audio_spectrum` | o espectro inteiro |
| `AUREA_AUDIO` | macro, para `#ifdef` |

---

## 5. Script por quadro (`frameScript`)

```json
"frameScript": "frame.js"
```

```js
function onFrame(f) {
  // f.time (segundos), f.frame, f.fps, f.progress (0 a 1 no clipe), f.params
  return { amount: 0.5 + 0.5 * Math.sin(f.time * 3) };
}
```

- Devolva só os ids que quer mudar: número, ou lista para cor e ponto. O que
  ficar de fora mantém o valor do painel.
- O script não acessa o projeto. `Math.random` repete o mesmo valor no mesmo
  quadro, então a prévia e a exportação saem iguais.
- **Limites por quadro:** 500.000 passos, 8 ms e 8 MB. Três falhas seguidas
  desligam o script até o app reabrir, e o efeito segue com os valores do
  painel.

---

## 6. Interface em árvore

Quatro controles guardam outros em `children`, até 6 níveis:

| `type` | O que faz |
| --- | --- |
| `row` | lado a lado, dividindo a largura |
| `column` | um embaixo do outro |
| `section` | título que abre e fecha; `"collapsed": true` começa fechada |
| `tabs` | abas; cada filho precisa ser uma `section` |

```json
"controls": [
  { "id": "abas", "type": "tabs", "children": [
    { "id": "basico", "type": "section", "label": { "pt-BR": "Básico", "en": "Basic" }, "children": [
      { "id": "linha", "type": "row", "children": [
        { "id": "ola", "type": "button", "label": { "pt-BR": "Olá", "en": "Hello" }, "onClick": "dizerOla" },
        { "id": "zerar", "type": "button", "label": { "pt-BR": "Zerar", "en": "Reset" }, "style": "secondary", "onClick": "zerar" }
      ]}
    ]},
    { "id": "ajuste", "type": "section", "label": { "pt-BR": "Ajuste", "en": "Adjust" }, "children": [
      { "id": "forca", "type": "slider", "label": { "pt-BR": "Força", "en": "Strength" },
        "min": 0, "max": 100, "default": 50, "unit": "%" }
    ]}
  ]}
]
```

- Os ids valem na árvore toda. Um id repetido faz o app recusar o plugin.
- `ui.setVisible(id, …)` e `ui.setEnabled(id, …)` num container valem para tudo
  que está dentro dele.

---

## 7. Onde o plugin aparece

| Onde | Como declarar |
| --- | --- |
| Painel em tela cheia | painel com `"placement": "editor.fullscreen"` |
| Grade Elemento do Adicionar | ação com `"placement": "add.menu"` e `"panel"` |
| No lugar de um painel do editor | painel com `"placement": "editor.panel"` e `"replaces"` |

`replaces` aceita `transform`, `effects`, `blend`, `presets` e `material`.

Regras que o app garante, independentemente do plugin:

- O botão de fechar da tela cheia é sempre do app.
- Acima de um painel substituído aparece sempre **Voltar ao original**. A
  escolha fica guardada por painel, e o gerenciador de plugins tem a chave para
  usar o painel do plugin de novo.

---

## 8. Script: expressões, ease, texto e máscaras

Cada chamada inteira vira um único passo de desfazer.

```js
// Expressões
var o = layer.opacity;
o.expression = "wiggle(2, 10)";     // "" remove
o.expressionEnabled = false;
o.expressionError;                  // texto do erro, "" se está certo

// Ease nas chaves
o.setTemporalEaseAtKey(2, [new KeyframeEase(0, 33)], [new KeyframeEase(0, 66)]);
o.keyInTemporalEase(2);             // [{speed, influence}]

// Texto
var doc = texto.sourceText.value;   // {text, font, fontSize, fillColor}
doc.text = "Novo";
doc.fontSize = 80;
texto.property("Source Text").setValue(doc);

// Máscaras
var m = layer.addMask({ vertices: [[0, 0], [100, 0], [100, 100]], closed: true });
m.feather = 8;
m.opacity = 50;                     // 0 a 100
m.mode = "subtract";                // add, subtract, intersect, difference, none
m.remove();
```

`font` é só leitura. Escrever `mask.path` numa máscara com caminho animado grava
uma chave no cabeçote.

---

## 9. Modo seguro

Se o app abrir e cair duas vezes seguidas, a próxima abertura desliga todos os
plugins e o gerenciador explica o motivo. A chave **Modo seguro** no
gerenciador liga e desliga isso à mão.

Com o modo seguro ligado, os plugins continuam listados, e os projetos guardam
os efeitos deles para quando o modo seguro sair. Se alguém relatar que o seu
plugin "sumiu", peça para conferir essa chave.

---

## 10. Limites

| Limite | Valor |
| --- | --- |
| Parâmetros | 252 em `shader`, 189 em `shader3d`, 256 em `stack` e painéis |
| Passes | 32 |
| Entradas + texturas por passe | 11 + a fonte |
| Imagens do pacote | 8, até 64 MB e 8192 px cada |
| Níveis da interface | 6 |
| `frameScript` | 500.000 passos, 8 ms e 8 MB por quadro |
| Quadro de teste na instalação | 1 s em 256×256 |

O quadro de teste roda **no aparelho de quem instala**. Um plugin aceito num
celular rápido pode ser recusado num de entrada, então projete para o mais
fraco. Laço em shader precisa ter fim fixo.

---

## 11. O que o formato 3 ainda não faz

- **Tipos de camada, ferramentas e modos próprios:** ainda não existem.
- **Assinatura de pacote:** não há; instalar é por conta e risco de quem
  instala.
- **Tempo de GPU por quadro:** não é medido no celular durante o uso. Há o
  quadro de teste na instalação e o limite do `frameScript`.

---

## 12. Checklist antes de publicar

- [ ] `python docs/plugins/tools/pack_plugin.py --check minha-pasta` sem erros
- [ ] Textos em `pt-BR` e `en` no mínimo
- [ ] Testado numa camada com transparência e numa camada pequena em quadro grande
- [ ] Prévia e exportação iguais (com `frameScript`, confira um quadro dos dois)
- [ ] `layerImage` testado com e sem camada escolhida
- [ ] Rodou num celular de entrada, além do seu

O exemplo completo está em [`examples/v3-showcase/`](examples/v3-showcase/).
