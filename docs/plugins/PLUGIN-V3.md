# Plugin V3 (formato 3) do AUREA

> Extraído da seção 15 de [README.md](README.md), que tem a referência completa dos formatos 1 e 2.

Com `"aureaPlugin": 3` um plugin ganha parâmetros de curva, degradê e camada,
histórico de quadros, som, script por quadro, interface em árvore, tela cheia e a
possibilidade de substituir painéis do editor. Tudo continua **declarado**: quem
desenha são os componentes do AUREA, nas duas plataformas. Não há binário
baixado nem HTML. Os formatos 1 e 2 seguem valendo sem mudança.

O exemplo [`examples/v3-showcase/`](examples/v3-showcase/) usa quase tudo isto.

## 1 Parâmetros novos

Só em plugins `"kind": "shader"` ou `"shader3d"` com formato 3.

| `type` | No painel | No shader |
| --- | --- | --- |
| `curve` | editor de curva (mestra, R, G, B) | `float id(float t)`; ocupa 16 posições |
| `gradient` | editor de degradê (até 16 cores) | `vec4 id(float t)`, RGBA linear; ocupa 32 posições |
| `layerImage` | escolher camada | `sampler2D id` e `bool id_connected`; ocupa 1 sampler |

`layerImage` chega no tamanho da composição, já com efeitos e transformações da
camada escolhida. Sem camada, `id_connected` é falso e o sampler devolve a
própria entrada. As posições somam no limite de parâmetros (252 em `shader`, 189
em `shader3d`), e os samplers somam no limite de entradas.

## 2 `uses`: histórico e som

```json
"uses": ["history", "audio"]
```

| Pedido | O que o shader ganha |
| --- | --- |
| `history` | `sampler2D aurea_history` (o resultado do quadro anterior) e o macro `AUREA_HISTORY` |
| `audio` | `sampler2D aurea_audio_spectrum`, `float aurea_audio(float banda)` (banda de 0 a 1), `float AUREA_AUDIO_PEAK` e o macro `AUREA_AUDIO` |

## 3 Script por quadro (`frameScript`)

```json
"frameScript": "frame.js"
```

```js
function onFrame(f) {
  // f.time (s), f.frame, f.fps, f.progress (0..1 no clipe), f.params
  return { amount: 0.5 + 0.5 * Math.sin(f.time * 3) };
}
```

Devolva um objeto com os ids que quer mudar: número, ou lista para cor e ponto.
O que ficar de fora mantém o valor do painel. O script não tem acesso ao projeto
e `Math.random` é repetível por quadro, então prévia e exportação batem.

Limites por quadro: 500.000 passos, 8 ms e 8 MB. Três falhas seguidas desligam o
script até reabrir o app; o efeito continua com os valores do painel.

## 4 Interface em árvore

Quatro controles guardam outros em `children`, até 6 níveis:

| `type` | O que faz |
| --- | --- |
| `row` | lado a lado, dividindo a largura |
| `column` | um embaixo do outro |
| `section` | título que abre e fecha; `"collapsed": true` começa fechada |
| `tabs` | abas; os filhos precisam ser `section` |

Os ids valem na árvore toda, e um id repetido recusa o plugin. `ui.setVisible` e
`ui.setEnabled` no container valem para tudo que estiver dentro.

## 5 Lugares novos e substituir painéis

| Onde | Campo |
| --- | --- |
| Tela cheia no editor | painel com `"placement": "editor.fullscreen"` (o botão de fechar é do app) |
| Grade Elemento do Adicionar | ação com `"placement": "add.menu"` |
| Substituir um painel | painel com `"replaces"` e `"placement": "editor.panel"` |

`replaces` aceita `transform`, `effects`, `blend`, `presets` e `material`. O app
**sempre** mostra "Voltar ao original" acima do painel do plugin, fora do controle
dele. A escolha fica guardada por painel, e o gerenciador de plugins tem a chave
para usar o painel do plugin de novo.

## 6 Script: expressões, ease, texto e máscaras

Tudo entra no desfazer da chamada.

```js
var o = layer.opacity;
o.expression = "wiggle(2, 10)";        // "" remove
o.expressionEnabled = false;
o.expressionError;                     // texto do erro, "" se está certo

o.setTemporalEaseAtKey(2, [new KeyframeEase(0, 33)], [new KeyframeEase(0, 66)]);
o.keyInTemporalEase(2);                // [{speed, influence}]
o.keyOutTemporalEase(2);

var doc = texto.sourceText.value;      // {text, font, fontSize, fillColor}
doc.text = "Novo";
doc.fontSize = 80;
texto.property("Source Text").setValue(doc);

var m = layer.addMask({vertices: [[0, 0], [100, 0], [100, 100]], closed: true});
m.feather = 8;
m.expansion = 2;
m.opacity = 50;                        // 0 a 100
m.inverted = true;
m.mode = "subtract";                   // add, subtract, intersect, difference, none
m.path = {vertices: [...], inTangents: [...], outTangents: [...], closed: true};
layer.masks;                           // lista
m.remove();
```

`mask.path` lê a forma guardada; em máscara com caminho animado, escrever grava
uma chave no cabeçote. `font` é só leitura.

## 7 Modo seguro

Se o app abrir e cair duas vezes seguidas, a abertura seguinte desliga os plugins
e o gerenciador explica o motivo. Dá para ligar e desligar pela chave **Modo
seguro** no gerenciador. Os plugins continuam listados, e os projetos que usam
efeitos deles guardam os efeitos para quando o modo seguro sair.

Motor: `plugins_safe_mode()`, `plugins_safe_mode_automatic()`,
`set_plugins_safe_mode(on)` e `mark_plugins_healthy()`. O app marca a abertura
como boa ao ir para o fundo ou depois de 15 s no editor. O degradê do painel
usa `query_effect_gradient` e `set_effect_gradient`.

## 8 O que o formato 3 não faz

- **Assinatura de pacote:** não existe; a instalação é por conta e risco de quem
  instala. Exige chave e serviço da loja.
- **Tempo de GPU por quadro:** não é medido em celular. Há o teste de renderização
  na instalação e o orçamento do `frameScript`.

## 9 Tipos de luz em `layers.addLight(tipo)`

O número passado a `addLight` (scripts de plugin e o Assistente) escolhe o tipo
da camada de luz. É o mesmo contrato de `Engine::add_light` (`LightKind` em
`engine/include/aurea/timeline/Layer.hpp`):

| Número | Luz criada |
|---:|---|
| `0` (padrão) | direcional (paralela, como a "Parallel" do AE) |
| `1` | pontual |
| `2` | spot |
| `3` | ambiente (ilumina tudo por igual, sem direção) |
| `4` | Flare 3D: uma luz pontual com o efeito Flare de cena |

Fora de 0..4 o número é preso na faixa. Até a 0.5.1 o `3` criava o Flare 3D e
não havia como criar a luz ambiente; projetos e plugins antigos que pediam `3`
para o flare agora recebem a luz ambiente (use `4`).

Nos scripts `.jsx` no estilo do AE (importação), `comp.layers.addLight(nome)`
cria a luz padrão; as constantes `LightType.PARALLEL`, `SPOT`, `POINT` e
`AMBIENT` existem para o script rodar, mas `lightType` ainda não é lido.
