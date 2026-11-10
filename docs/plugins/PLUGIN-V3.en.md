# Plugin V3 — creator guide

Format 3 takes an AUREA plugin well beyond a shader with sliders: it receives
curves, gradients and other layers, remembers the previous frame, listens to
the sound, changes its own parameters every frame and builds a UI with tabs and
sections that can even replace editor panels.

Everything is still **declared** in `plugin.json`. The UI is drawn by AUREA's
own components, the same on Android and iOS. The package carries no binary and
no HTML. Format 1 and 2 plugins keep working unchanged.

This guide is the quick path. The full reference (shader header, limits, the
whole script API) is in the [README](README.en.md), sections 1 to 15.

---

## 1. First V3 plugin in 5 minutes

A folder with three files:

```
my-plugin/
├── plugin.json
├── effect.frag
└── frame.js
```

**plugin.json**

```json
{
  "aureaPlugin": 3,
  "id": "com.yourname.pulse",
  "version": "1.0.0",
  "author": "Your name",
  "name": { "pt-BR": "Pulso", "en": "Pulse" },
  "kind": "shader",
  "params": [
    { "id": "amount", "type": "float", "label": { "pt-BR": "Força", "en": "Strength" },
      "default": 0.5, "min": 0, "max": 1 },
    { "id": "paint", "type": "gradient", "label": { "pt-BR": "Cores", "en": "Colors" } }
  ],
  "uses": ["audio"],
  "frameScript": "frame.js",
  "passes": [ { "shader": "effect.frag" } ]
}
```

**effect.frag**

```glsl
vec4 aurea_main(vec2 uv) {
    vec4 c = aurea_unpremultiply(texture(aurea_source, uv));
    vec3 color = paint(aurea_luma(c.rgb)).rgb;
    float beat = aurea_audio(0.1) * AUREA_AUDIO_PEAK;
    vec3 rgb = mix(c.rgb, color, amount) * (1.0 + beat);
    return vec4(rgb * c.a, c.a);
}
```

**frame.js**

```js
function onFrame(f) {
  return { amount: 0.5 + 0.5 * Math.sin(f.time * 2) };
}
```

Pack and install:

```
python docs/plugins/tools/pack_plugin.py my-plugin
```

Copy `my-plugin.aureaplugin` to the device and open
**Effects → Plugins: install and manage → Install plugin**.

---

## 2. What changes from format 2 to 3

| Feature | Format 2 | Format 3 |
| --- | --- | --- |
| Parameter types | float, int, bool, enum, color, point, angle, layer | + `curve`, `gradient`, `layerImage` |
| Previous frame | — | `uses: ["history"]` |
| Sound | — | `uses: ["audio"]` |
| Parameters driven by code | keyframes and expressions only | `frameScript` every frame |
| UI | list of controls | tree: `row`, `column`, `section`, `tabs` |
| Where it shows up | editor panel, menus | + full screen, Add menu item, replace panels |
| Script | panels, actions, API | + expressions, ease, text, masks |

To migrate, change `"aureaPlugin": 2` to `3`. The rest of the manifest stays
valid.

---

## 3. New parameters

Available in `"kind": "shader"` and `"shader3d"` plugins.

| `type` | In the panel | In the shader | Cost |
| --- | --- | --- | --- |
| `curve` | curve editor (master, R, G, B) | `float id(float t)` | 16 slots |
| `gradient` | gradient editor, up to 16 colors | `vec4 id(float t)`, linear RGBA | 32 slots |
| `layerImage` | pick a layer | `sampler2D id` and `bool id_connected` | 1 sampler |

- **`curve`:** call it as a function. `tone(0.5)` returns the curve at the middle.
- **`gradient`:** `paint(t)` returns the color at `t` from 0 to 1, already in
  linear light.
- **`layerImage`:** the chosen layer arrives at composition size, with its
  effects and transform. With no layer, `id_connected` is `false` and the
  sampler returns the input itself; always check before using it.

Slots count toward the parameter limit (252 in `shader`, 189 in `shader3d`),
and samplers count toward the per-pass input limit.

---

## 4. Previous frame and sound (`uses`)

```json
"uses": ["history", "audio"]
```

**`history`** gives the shader `sampler2D aurea_history`, the result of the
previous frame, and the `AUREA_HISTORY` macro. Use it for trails, echo and
feedback:

```glsl
vec4 before = aurea_unpremultiply(texture(aurea_history, uv));
rgb = mix(rgb, before.rgb, trail * before.a);
```

**`audio`** gives:

| Symbol | What it is |
| --- | --- |
| `float aurea_audio(float band)` | energy of the band, from 0 (lows) to 1 (highs) |
| `float AUREA_AUDIO_PEAK` | peak of the frame |
| `sampler2D aurea_audio_spectrum` | the whole spectrum |
| `AUREA_AUDIO` | macro, for `#ifdef` |

---

## 5. Per-frame script (`frameScript`)

```json
"frameScript": "frame.js"
```

```js
function onFrame(f) {
  // f.time (seconds), f.frame, f.fps, f.progress (0 to 1 over the clip), f.params
  return { amount: 0.5 + 0.5 * Math.sin(f.time * 3) };
}
```

- Return only the ids you want to change: a number, or a list for colors and
  points. Anything left out keeps the panel value.
- The script has no access to the project. `Math.random` repeats the same value
  on the same frame, so preview and export match.
- **Per-frame limits:** 500,000 steps, 8 ms and 8 MB. Three failures in a row
  turn the script off until the app reopens, and the effect keeps the panel
  values.

---

## 6. Tree UI

Four controls hold others in `children`, up to 6 levels deep:

| `type` | What it does |
| --- | --- |
| `row` | side by side, splitting the width |
| `column` | one below the other |
| `section` | title that opens and closes; `"collapsed": true` starts closed |
| `tabs` | tabs; every child must be a `section` |

```json
"controls": [
  { "id": "tabs", "type": "tabs", "children": [
    { "id": "basic", "type": "section", "label": { "pt-BR": "Básico", "en": "Basic" }, "children": [
      { "id": "line", "type": "row", "children": [
        { "id": "hello", "type": "button", "label": { "pt-BR": "Olá", "en": "Hello" }, "onClick": "sayHello" },
        { "id": "reset", "type": "button", "label": { "pt-BR": "Zerar", "en": "Reset" }, "style": "secondary", "onClick": "reset" }
      ]}
    ]},
    { "id": "adjust", "type": "section", "label": { "pt-BR": "Ajuste", "en": "Adjust" }, "children": [
      { "id": "strength", "type": "slider", "label": { "pt-BR": "Força", "en": "Strength" },
        "min": 0, "max": 100, "default": 50, "unit": "%" }
    ]}
  ]}
]
```

- Ids are unique across the whole tree. A repeated id makes the app reject the
  plugin.
- `ui.setVisible(id, …)` and `ui.setEnabled(id, …)` on a container apply to
  everything inside it.

---

## 7. Where the plugin shows up

| Where | How to declare it |
| --- | --- |
| Full-screen panel | panel with `"placement": "editor.fullscreen"` |
| Element grid in the Add menu | action with `"placement": "add.menu"` and `"panel"` |
| In place of an editor panel | panel with `"placement": "editor.panel"` and `"replaces"` |

`replaces` accepts `transform`, `effects`, `blend`, `presets` and `material`.

Rules the app enforces, whatever the plugin does:

- The close button of the full screen always belongs to the app.
- **Back to original** always appears above a replaced panel. The choice is
  stored per panel, and the plugin manager has the switch to use the plugin's
  panel again.

---

## 8. Script: expressions, ease, text and masks

Each whole call becomes a single undo step.

```js
// Expressions
var o = layer.opacity;
o.expression = "wiggle(2, 10)";     // "" removes it
o.expressionEnabled = false;
o.expressionError;                  // error text, "" when it's fine

// Keyframe ease
o.setTemporalEaseAtKey(2, [new KeyframeEase(0, 33)], [new KeyframeEase(0, 66)]);
o.keyInTemporalEase(2);             // [{speed, influence}]

// Text
var doc = text.sourceText.value;    // {text, font, fontSize, fillColor}
doc.text = "New";
doc.fontSize = 80;
text.property("Source Text").setValue(doc);

// Masks
var m = layer.addMask({ vertices: [[0, 0], [100, 0], [100, 100]], closed: true });
m.feather = 8;
m.opacity = 50;                     // 0 to 100
m.mode = "subtract";                // add, subtract, intersect, difference, none
m.remove();
```

`font` is read-only. Writing `mask.path` on a mask with an animated path sets a
key at the playhead.

---

## 9. Safe mode

If the app opens and crashes twice in a row, the next launch turns all plugins
off and the manager explains why. The **Safe mode** switch in the manager turns
it on and off by hand.

While safe mode is on, plugins stay listed and projects keep their effects for
when safe mode is off. If someone reports that your plugin "disappeared", ask
them to check that switch.

---

## 10. Limits

| Limit | Value |
| --- | --- |
| Parameters | 252 in `shader`, 189 in `shader3d`, 256 in `stack` and panels |
| Passes | 32 |
| Inputs + textures per pass | 11 + the source |
| Package images | 8, up to 64 MB and 8192 px each |
| UI levels | 6 |
| `frameScript` | 500,000 steps, 8 ms and 8 MB per frame |
| Test frame at install | 1 s at 256×256 |

The test frame runs **on the installer's device**. A plugin accepted on a fast
phone can be rejected on an entry-level one, so design for the weakest. Shader
loops need a fixed end.

---

## 11. What format 3 doesn't do yet

- **Custom layer types, tools and modes:** not available yet.
- **Package signing:** none; installing is at the installer's own risk.
- **GPU time per frame:** not measured on the phone during use. There is the
  test frame at install and the `frameScript` limit.

---

## 12. Checklist before publishing

- [ ] `python docs/plugins/tools/pack_plugin.py --check my-folder` with no errors
- [ ] Texts in at least `pt-BR` and `en`
- [ ] Tested on a layer with transparency and on a small layer in a large frame
- [ ] Preview and export match (with `frameScript`, compare one frame of each)
- [ ] `layerImage` tested with and without a chosen layer
- [ ] Ran on an entry-level phone, not just yours

The full example is in [`examples/v3-showcase/`](examples/v3-showcase/).
