# AUREA effect plugins

> Versão em português (a principal): [README.md](README.md)

> **0.3.4:** besides effects, a plugin can bring **panels and tabs** to the
> interface, **scripts** with an After Effects style API and **access to internet
> APIs** on the domains it declares — see [section 14](#14-panels-scripts-and-apis-format-2-034).

A **plugin** is a package that adds **one effect** to the AUREA effect
browser, in the **Plugins** category. Once installed it behaves like any
built-in effect: it goes into the layer panel, its parameters take keyframes
and expressions, it renders the same in preview and export, and it is saved in
the project.

This is the complete reference for plugin authors: the package format, every
`plugin.json` field, every symbol the shader receives, the limits, how to test
and package, and what to do when installation refuses a plugin.

- [1. The two plugin kinds](#1-the-two-plugin-kinds)
- [2. The `.aureaplugin` package](#2-the-aureaplugin-package)
- [3. `plugin.json` reference](#3-pluginjson-reference)
- [4. The shader interface](#4-the-shader-interface)
- [5. Stack plugins and expressions](#5-stack-plugins-and-expressions)
- [6. Importing ISF shaders (`.fs`)](#6-importing-isf-shaders-fs)
- [7. Limits and why](#7-limits-and-why)
- [8. Performance tips](#8-performance-tips)
- [9. Testing and packaging](#9-testing-and-packaging)
- [10. Troubleshooting](#10-troubleshooting)
- [11. Projects, versions and removal](#11-projects-versions-and-removal)
- [12. Engine API (for UI integrators)](#12-engine-api-for-ui-integrators)
- [13. Licences](#13-licences)
- [14. Panels, scripts and APIs (format 2, 0.3.4)](#14-panels-scripts-and-apis-format-2-034)

Ready-made examples in [`examples/`](examples/):

| Folder / file | Kind | Shows |
| --- | --- | --- |
| `rgb-wave/` → `rgb-wave.aureaplugin` | shader, 1 pass | wavy RGB split; float, angle and color; `margin`; time |
| `soft-glow/` → `soft-glow.aureaplugin` | shader, 2 passes | a pass at 1/4 resolution (`scale`), `target` and `inputs` |
| `dream-look/` → `dream-look.aureaplugin` | stack | built-in blur + glow + saturation driven by expressions |
| `pulse-vignette.fs` | ISF 2.0 | importing a `.fs` (point2D, color, bool, TIME) |
| `target-3d/` → `target-3d.aureaplugin` | shader3d, 1 pass | a ring around a 3D null seen through the scene camera; `layer` parameter with `accept`; `aurea_ray` |
| `animated-text/` → `animated-text.aureaplugin` | panel + script (0.3.4) | **Animated text generator**: panel, Home tab, layer-bar button; creates layers and keyframes |
| `weather-text/` → `weather-text.aureaplugin` | panel + script (0.3.4) | **Weather on screen**: `fetch` from a declared public API (Open-Meteo) and creates a text |

---

## 1. The two plugin kinds

**`"kind": "shader"`** — you write the effect in **GLSL 4.50**: a function
`vec4 aurea_main(vec2 uv)` returning each pixel's colour. A plugin has 1 to 8
passes (each with its own shader), intermediate textures at reduced resolution
and up to 16 parameters. AUREA compiles the shader **on the device** at install
time: GLSL → SPIR-V (glslang) → the device's GPU API (Vulkan consumes SPIR-V;
OpenGL ES gets ESSL 3.10 and Metal gets MSL, translated by SPIRV-Cross with the
same options as the app's own shaders).

**`"kind": "stack"`** — a **stack of the app's own effects** (blur, glow,
colour...) whose values are computed from the plugin's parameters by simple
expressions (`amount * 40`). No shader: the safest kind, works on every device.
It is how you ship a "look" with its own controls.

**`"kind": "shader3d"`** (0.3.1) — a `shader` that also sees the **3D scene**:
the composition camera and up to 4 linked layers (3D null, camera, light). Use it
for effects that live in 3D space and follow the camera. See
[3D plugins](#3d-plugins-kind-shader3d-031) in section 4.

## 2. The `.aureaplugin` package

A `.aureaplugin` is a **ZIP** (stored or deflate) with `plugin.json` at the
root plus the files it references:

```
my-effect.aureaplugin
├── plugin.json
├── pass1.frag
└── pass2.frag
```

A ZIP of the folder (`plugin.json` inside a single subfolder) is accepted too.
Extra files (licence, documentation images) may be included and are ignored.
AUREA installs into `<plugin folder>/<id>/`:

- Android: `files/plugins/` (app-internal storage)
- iOS: `Documents/Plugins/`

Plugins have **no file system access**: the shader only reads the textures the
engine binds (the layer and the plugin's own intermediate textures) and the
parameter block. No network, no disk, no wall clock.

## 3. `plugin.json` reference

UTF-8 text. Full example (from `rgb-wave`):

```json
{
  "aureaPlugin": 1,
  "id": "com.aurea.examples.rgbwave",
  "version": "1.0.0",
  "author": "AUREA (example)",
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

### Top-level fields

| Field | Type | Required | Description |
| --- | --- | --- | --- |
| `aureaPlugin` | number | yes | Format version. This app reads **`1`**. |
| `id` | text | yes | Unique reverse-domain identity: 3–64 chars `a-z 0-9 . _ -`, not starting/ending with `.`, no `..`. E.g. `com.studio.glow`. The project stores the effect under `plugin.<id>` — **never change the id** of a published plugin. |
| `version` | text | yes | `x.y.z` (each part 0..99999). Re-installing the same `id` with an **equal or newer** version replaces it; older is refused. |
| `author` | text | no | Author (up to 128). |
| `name` | text or locales | yes | Catalogue name (up to 64). |
| `description` | text or locales | no | Description (up to 512). |
| `category` | text | no | Sub-category shown inside **Plugins** (up to 32). |
| `kind` | `"shader"`, `"shader3d"` or `"stack"` | yes | Section 1. `shader3d` (0.3.1) is a `shader` that also receives the scene camera and the linked layers (section 4, "3D plugins"). |
| `params` | list | no | Up to **16** parameters (below). |
| `passes` | list | shader | 1 to **8** passes (below). |
| `margin` | number | no | How many **layer px** the effect reads/draws beyond the layer box (0..1024). A glow of radius 50 declares `50`. Without a margin the result is clipped to the box. |
| `colorSpace` | `"linear"` or `"srgb"` | no | `linear` (default): the shader sees the engine working space — **linear** RGB, **premultiplied** alpha. `srgb`: the engine hands the shader **sRGB-encoded, straight-alpha** colours and converts the result back (what ISF import uses). |
| `stack` | list | stack | 1 to 16 built-in effects (section 5). |

### Localised text

`name`, `description`, `label` and enum options accept plain text (all
languages) or a locale → text object:

```json
"name": { "pt-BR": "Brilho suave", "en": "Soft Glow", "es": "Brillo suave",
          "ru": "Мягкое свечение", "hi": "कोमल चमक", "id": "Cahaya Lembut", "ar": "توهج ناعم" }
```

App locales: `pt-BR`, `en`, `es`, `ru`, `hi`, `id`, `ar`. A missing locale
falls back to `en`, then `pt-BR`, then the first declared. The `pt-BR` name is
the engine name of the effect; the UI shows the device language.

### Parameters (`params`)

| Field | Description |
| --- | --- |
| `id` | Parameter name: letters, digits and `_`, up to 32, starting with a letter or `_`. It is also the **shader name** (a macro) and the expression name. Must not be a GLSL keyword or built-in function (`mix`, `texture`, `float`...), must not start with `gl_`, `aurea`, `isf_`, `IMG_`, `vv_`, and must not contain `__`. |
| `type` | `float`, `int`, `bool`, `enum`, `color`, `point`, `angle`, `layer` (`shader3d` only). |
| `label` | Text or locales (up to 48). Default: the `id`. |
| `default` | Initial value (per type, below). |
| `min`, `max` | Control range (numbers). |
| `options` | `enum` only: 1 to 32 texts (or locale objects). |
| `animatable` | `true` (default) or `false`: accepts keyframes/expressions. |
| `accept` | `layer` only: a list of `"camera"`, `"null"`, `"light"` or `"any"` (default: any layer). The panel only offers those layers. |

| Type | `default` | default `min`/`max` | In the shader |
| --- | --- | --- | --- |
| `float` | number | 0..1 | `float` |
| `int` | integer | 0..100 | `float` (integral) |
| `bool` | `true`/`false` | — | `float` 0 or 1 |
| `enum` | option index | — | `float` index |
| `color` | `"#RRGGBB"`, `"#RRGGBBAA"` or `[r, g, b(, a)]` 0..1 **in sRGB** (like a colour picker) | — | `vec4` **linear** RGBA, straight alpha (with `colorSpace: srgb`, sRGB RGBA) |
| `point` | `[x, y]` relative to the layer (0..1 = width/height; `[0.5, 0.5]` = centre) | -1..2 | `vec2` **in the pass `uv` space** (converted by the engine) |
| `angle` | degrees | -360..360 | `float` in **radians** |
| `layer` | — (no layer) | — | an **integer** (the layer's position among the `layer` parameters) for the `aurea_layer_*` functions (up to **4** per plugin; never animatable) |

### Passes (`passes`)

| Field | Description |
| --- | --- |
| `shader` | GLSL file in the package (relative path, no `..`). Up to 64 KB. |
| `target` | Name of the intermediate texture this pass writes. **Required** on every pass but the last; the last pass has **no** `target`: it is the effect output. |
| `scale` | Pass resolution relative to the layer image: 0.125..1 (default 1). A blur at `0.25` costs 1/16. |
| `inputs` | `"name": "source"` object, up to 3: each name becomes a `uniform sampler2D`; the source is `"source"` (the layer) or the `target` of an **earlier** pass. The layer is always available as `aurea_source` too. |

Two-pass example (`soft-glow`):

```json
"passes": [
  { "shader": "bright_blur.frag", "target": "bloom", "scale": 0.25 },
  { "shader": "combine.frag", "inputs": { "bloom": "bloom" } }
]
```

## 4. The shader interface

You write **only**:

```glsl
vec4 aurea_main(vec2 uv) {
    return texture(aurea_source, uv);
}
```

The engine concatenates three parts and compiles them as GLSL 4.50 with Vulkan
semantics:

1. **the engine header** (below) — named `aurea_header` in error messages;
2. **your file** — lines number from 1, with your file name (`rgb_wave.frag:12: ...`);
3. **the engine `main`**: `void main() { o_color = aurea_main(v_uv); }` (plus the
   sRGB → linear conversion on the last pass of an `srgb` plugin; NaN/infinity
   become 0 so they never poison the composite).

### Header symbols

| Symbol | Type | Meaning |
| --- | --- | --- |
| `#version 450` | — | Already declared: **don't** write `#version`. |
| `AUREA_PLUGIN` | macro | `1` (for `#ifdef`). |
| `AUREA_TEX0`..`AUREA_TEX11`, `AUREA_PARAMS`, `AUREA_IMG0`, `AUREA_IMG1`, `AUREA_DATA`, `AUREA_DATA1` | macros | The engine resource slots (0..11, 12, 13, 14, 15, 16). Informational: the plugin samplers are already declared. |
| `AUREA_PASS_INDEX` | macro | This pass index (0 = first). |
| `AUREA_PASS_COUNT` | macro | Number of passes. |
| `AUREA_COLOR_SRGB` | macro | `1` when `colorSpace` is `srgb`, else `0`. |
| `v_uv` | `in vec2` | The pixel uv (the same value `aurea_main` receives). |
| `o_color` | `out vec4` | The output (written by the engine `main`). |
| `aurea_source` | `sampler2D` (binding 0) | The layer image (with the effects before this one). |
| `<name>` from `inputs` | `sampler2D` (bindings 1..3) | The pass's named inputs, in declaration order. |
| `aurea.resolution` | `vec4` | `xy` = texels of this pass, `zw` = `1/xy`. |
| `aurea.time` | `vec4` | `x` = seconds, `y` = frame (fractional under motion blur), `z` = composition fps. Layer time (0 at the layer start). |
| `aurea.layer` | `vec4` | `xy` = layer size in px; `zw` = **texels per layer px in this pass** (1 for a natural-size export, 0.25 in a 1/4 preview or a `scale: 0.25` pass). |
| `aurea.region` | `vec4` | The area `uv` 0..1 covers, in layer px: `xy` = origin, `zw` = size. Without `margin` it is the layer box (`0, 0, width, height`). |
| `aurea.params[16]` | `vec4[16]` | Parameter values, one `vec4` each, in `plugin.json` order. Use the macros below. |
| `<parameter id>` | macro | `#define amount (aurea.params[0].x)` — the value already converted (table in section 3). `color` = `aurea.params[i]` (vec4), `point` = `aurea.params[i].xy`. |
| `AUREA_PI` | `const float` | π. |
| `aurea_srgb_to_linear(x)`, `aurea_linear_to_srgb(x)` | `float`/`vec3` | The sRGB curve (IEC 61966-2-1), identical to the engine's. |
| `aurea_luma(rgb)` | `float` | BT.709 luminance (`0.2126, 0.7152, 0.0722`). |
| `aurea_unpremultiply(c)`, `aurea_premultiply(c)` | `vec4` | Premultiplied ↔ straight alpha. |
| `aurea_hash(p)` | `float` | Deterministic pseudo-random 0..1 from a `vec2`. |
| `aurea_hash2(p)` | `vec2` | Same, two values. |
| `aurea_noise(p)` | `float` | Smooth value noise 0..1. |
| `aurea_fbm(p, octaves)` | `float` | Up to 8 octaves of `aurea_noise`, 0..1. |
| `aurea_px_to_uv(px)` | `vec2` | Converts an offset in **layer px** into this pass's uv. Use it for every length (radius, offset): the effect then matches between a reduced preview and the export. |
| `aurea_uv_to_px(uv)` | `vec2` | The inverse. |
| `aurea_layer_uv(uv)` | `vec2` | Pass uv → coordinates relative to the **layer box** (0..1), handy with `margin`. |
| `aurea_uv_from_layer(p)` | `vec2` | The inverse. |

### Coordinates

- `uv` (0,0) is the **top-left** corner; (1,1) bottom-right.
- **Every** texture the shader sees — `aurea_source`, `inputs` and `target`s —
  covers the **same rectangle** (the effect working region). The same `uv`
  samples the same point in all of them, whatever the pass `scale`. Only the
  resolution changes.
- Outside 0..1 textures return **transparent** (transparent border):
  `texture(aurea_source, uv + offset)` near the edge reads `vec4(0)`.
- With `margin`, the region is the layer box grown by the margin (and clipped to
  what the frame shows). The engine copies the layer into that region before the
  first pass, so source and output uv stay identical.

### Colour

- `colorSpace: linear` (default): **linear** RGB (BT.709 primaries),
  **premultiplied** alpha, floating point (can exceed 1 in HDR). Return the same
  space: `rgb` already multiplied by alpha. For straight-alpha maths use
  `aurea_unpremultiply` / `aurea_premultiply`.
- `colorSpace: srgb`: the shader receives sRGB straight alpha (0..1) and returns
  sRGB straight alpha; the engine converts. Slower (one preparation pass) and
  less precise in HDR; prefer `linear` for new plugins.

### Time and animation

`aurea.time.x` follows playback and is identical in preview, scrubbing and
export (no wall clock). Animation must depend only on time and parameters — no
state between frames. Parameters take keyframes and expressions in the panel
like any effect's.

### Multiple passes

Each pass is a shader with its own `aurea_main`. Pass N reads `aurea_source`
and the named `inputs` that point at earlier `target`s. The last pass is the
output. `aurea.resolution` and `aurea.layer.zw` reflect each pass's `scale`.


### 3D plugins (`"kind": "shader3d"`, 0.3.1)

A 3D plugin is a `shader` that can also see the **3D scene**:

- the **scene camera**: the composition's active camera, or the default camera
  when there is none;
- the **world** of up to **4 linked layers**, chosen through `layer`
  parameters: a **3D null** as a target or pivot, a **camera**, a **light**.

With that the shader can work in 3D: cast the camera ray through each pixel,
project world points to the screen, measure distances to a null. Moving the
null, moving the camera or changing the lens updates the effect on the same
frame, in the preview and in the export. Everything else is the same as a
`shader`: passes, `margin`, `colorSpace`, parameters with keyframes and
expressions, and the test frame at install time.

#### The manifest

```json
{
  "aureaPlugin": 1,
  "id": "com.studio.target",
  "version": "1.0.0",
  "name": { "pt-BR": "Mira 3D", "en": "3D Target" },
  "category": "3D",
  "kind": "shader3d",
  "params": [
    { "id": "target", "type": "layer", "accept": ["null"],
      "label": { "pt-BR": "Alvo (nulo 3D)", "en": "Target (3D null)" } },
    { "id": "radius", "type": "float", "default": 120, "min": 4, "max": 1200 }
  ],
  "passes": [ { "shader": "target.frag" } ]
}
```

- `"kind": "shader3d"` turns on the 3D mode. A `layer` parameter in a plain
  `"shader"` plugin is refused at install time.
- **`layer` parameter**: in the panel it becomes a list of the composition's
  layers.
  - `accept` filters the list: `"camera"`, `"null"`, `"light"`, or `"any"`
    (any layer; the default without `accept`). They combine:
    `["null", "camera"]`.
  - It starts with no layer (`aurea_layer_valid` is false).
  - It is never animatable: the link is fixed, the linked layer is what moves.
  - Up to **4** per plugin, counting every `layer` parameter.
  - Linking the layer that carries the effect has no effect: it reads as no layer.

#### What the shader receives

The `aurea` block gains these fields **after** `params[16]`. Nothing before
them changes, so a 2D shader pasted into a `shader3d` plugin keeps working:

| Symbol | Type | Contents |
| --- | --- | --- |
| `aurea.view` | `mat4` | view ← world (x right, y down, z forward) |
| `aurea.compFromWorld` | `mat4` | composition px (homogeneous) ← world |
| `aurea.compFromLayer` | `mat4` | composition px (homogeneous) ← px of the layer that carries the effect |
| `aurea.camera` | `vec4` | `xyz` camera position in the world; `w` = 1 camera layer, 0 default camera |
| `aurea.lens` | `vec4` | `x` vertical field of view (rad), `y` near plane (px), `z` aspect (width/height), `w` focal length in px |
| `aurea.comp` | `vec4` | `xy` composition size (px), `zw` = 1/xy |
| `aurea.layerWorld[4]` | `mat4` | world ← px of each linked layer (parents included) |
| `aurea.layerPos[4]` | `vec4` | `xyz` the linked layer's anchor point in the world; `w` = 1 linked, 0 no layer |
| `aurea.layerInfo[4]` | `vec4` | `x` kind (6 null, 8 camera, 9 light; anything else = another layer), `y` field of view (rad) for a camera, `z` = 1 if the layer is 3D |

Helpers (only in `shader3d`; the header defines `AUREA_SCENE3D 1`):

| Function | What it does |
| --- | --- |
| `bool aurea_layer_valid(int i)` | a layer is linked, visible and inside its time span on this frame |
| `vec3 aurea_layer_position(int i)` | the linked layer's anchor point in the world |
| `mat4 aurea_layer_matrix(int i)` | world ← linked layer px (position, X/Y/Z rotation, scale and parents) |
| `vec3 aurea_layer_axis(int i, int a)` | the layer's axis `a` in the world, normalised (0 = x, 1 = y, 2 = z) |
| `int aurea_layer_kind(int i)` | the kind: 6 null, 8 camera, 9 light |
| `float aurea_layer_fov(int i)` | the linked camera's vertical field of view (rad) |
| `vec3 aurea_camera_position()` | the scene camera position |
| `vec3 aurea_camera_right()`, `aurea_camera_down()`, `aurea_camera_forward()` | the scene camera axes in the world |
| `vec2 aurea_comp_px(vec2 uv)` | the pass `uv` in composition px |
| `void aurea_ray(vec2 uv, out vec3 ro, out vec3 rd)` | the camera ray through this pixel: origin `ro` and direction `rd` (normalised) in the world |
| `vec3 aurea_world_to_comp(vec3 p)` | world point → composition px; `z` = homogeneous depth (≤ 0 = behind the camera) |
| `vec3 aurea_world_to_view(vec3 p)` | world point → camera space (`z` = distance in front of it) |

`i` is the **name of the `layer` parameter**: the header turns it into its
position among the `layer` parameters (first `layer` = 0, second = 1...). Write
`aurea_layer_position(target)`, not `aurea.params[...]`.

#### The AUREA world

- The unit is the **composition pixel**: x to the right, y **down**, z **away**
  from the camera, as in After Effects.
- With the default camera, the z = 0 plane shows 1:1 on the composition: the
  point `(x, y, 0)` lands on pixel `(x, y)`.
- A layer's position (including z) and anchor point work in the world as in the
  transform panel. `aurea_layer_position` returns the anchor point with
  position, rotation, scale and parents applied.
- `aurea_layer_axis(i, 2)` is the layer's "front" (z): use it as the normal of
  a plane that turns with the null.

#### Example: a ring around a null

This is `examples/target-3d`, a ring drawn on the null's plane and seen in
perspective (its parameters: `target`, `radius`, `width`, `tint`; the halo is left out here):

```glsl
vec4 aurea_main(vec2 uv) {
    vec4 base = texture(aurea_source, uv);
    // No null linked: the ring sits at the composition centre, on the z = 0 plane.
    vec3 center = aurea_layer_valid(target) ? aurea_layer_position(target) : vec3(aurea.comp.xy * 0.5, 0.0);
    vec3 normal = aurea_layer_valid(target) ? aurea_layer_axis(target, 2) : vec3(0.0, 0.0, 1.0);
    vec3 ro, rd;
    aurea_ray(uv, ro, rd);
    float facing = dot(rd, normal);
    if (abs(facing) < 1e-5) return base;            // ray parallel to the plane
    float t = dot(center - ro, normal) / facing;
    if (t <= 0.0) return base;                       // the plane is behind the camera
    vec3 hit = ro + rd * t;                          // where the ray crosses the null's plane
    float d = abs(length(hit - center) - radius);
    float ring = smoothstep(width, 0.0, d);
    vec3 light = tint.rgb * ring;
    return vec4(base.rgb + light, max(base.a, ring));
}
```

Other common recipes:

- **A point on screen**: `vec3 s = aurea_world_to_comp(p);` and if `s.z > 0.0`
  the point shows at `s.xy` (composition px). Compare it with
  `aurea_comp_px(uv)` to draw a glow, a crosshair or a label.
- **Size in perspective**: `aurea_world_to_view(p).z` is the distance to the
  camera; `aurea.lens.w / z` turns world px into screen px at that distance.
- **Sky or infinite background**: use only the direction `rd` from `aurea_ray`
  (position does not matter at infinity), like Aurora does.
- **Linked light**: `aurea_layer_position(light)` gives the light position for a
  glow or light ray that follows the scene light.

#### Where to apply the plugin

- Apply it to a **2D layer the size of the composition**, such as a transparent
  solid: the result covers the whole frame and the camera does not move the
  layer.
- On a 3D layer the camera also moves the layer itself (the effect lands on its
  plane). The maths stays right for the pixels the layer covers, but nothing
  shows outside it.
- The effect is drawn in the layer's region (plus `margin`). To draw outside it,
  make the layer or the `margin` bigger.

#### Limits and caveats

- The test frame at install time runs with the default camera and no linked
  layer. Handle the `aurea_layer_valid(...) == false` case.
- The 3D block takes 944 of the 1024 parameter bytes per pass; all 16
  parameters are still available.
- There is no access to 3D models, meshes or the scene depth: the plugin sees
  the camera and the linked layers, not the geometry.

## 5. Stack plugins and expressions

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

- `effect`: the key of a **built-in** effect (`aurea.blur.gaussian`,
  `aurea.light.glow`, `aurea.color.saturation`, `aurea.color.exposure`...). Keys
  and parameter ids are the engine's (stable; see
  `engine/src/effects/builtin/*.cpp`).
- `set`: built-in parameter → value. Unlisted parameters keep their default.
  - number or `true`/`false`: a fixed value;
  - text: an **expression** over the plugin parameters;
  - list: one value/expression per component (`[x, y]` for points, `[r, g, b]`
    or `[r, g, b, a]` for linear colours);
  - the `id` of a plugin `color`/`point` parameter copies the whole value into a
    parameter of the same type (`"color": "warmth"`).
- In a project the plugin **becomes the built-ins** during each frame's
  planning: colour-op fusion, neutral effects leaving the chain, margins and
  resources behave exactly as if the user had applied the built-ins by hand —
  the result is the same frame.
- Not stackable (they run outside the pixel chain or need per-instance data):
  `aurea.time.*`, `aurea.audio.*`, `aurea.control.*`, `aurea.3d.*`,
  `aurea.layout.*`, `aurea.text*`, `aurea.shape3d.*`, mesh warp, puppet,
  Particular, Rotobrush and the parenting helper.

### Expressions

A small, closed arithmetic language compiled at install time:

- numbers (`12`, `0.5`, `1e3`), `+ - * / %`, `^` (power), parentheses;
- plugin parameters by `id`; components: `point.x`, `point.y`, `color.r`,
  `color.g`, `color.b`, `color.a` (or `.x .y .z .w`);
- `pi`;
- functions: `min`, `max` (2 to 8 args), `clamp(x, a, b)`, `mix(a, b, t)`,
  `step(edge, x)`, `smoothstep(a, b, x)`, `sin`, `cos`, `tan`, `atan(x)`,
  `atan(y, x)`, `abs`, `sign`, `pow`, `sqrt`, `exp`, `log`, `floor`, `ceil`,
  `round`, `fract`, `radians`, `degrees`.

Units are the parameter's (angles in degrees, points relative to the layer,
bool 0/1, enum = index). Division by zero gives 0; a non-finite result becomes
0. The value is clamped to the built-in parameter's typed range.

## 6. Importing ISF shaders (`.fs`)

AUREA installs a single-pass **ISF 2.0** file directly: pick the `.fs` instead
of a `.aureaplugin`. It becomes a `shader` plugin with `colorSpace: srgb` and id
`isf.<file-name>` (e.g. `Bad TV.fs` → `isf.bad-tv`).

| ISF | In AUREA |
| --- | --- |
| `INPUTS` `float` | `float` parameter (DEFAULT/MIN/MAX) |
| `bool`, `event` | `bool` parameter (`event` starts off) |
| `long` with `VALUES`/`LABELS` | `enum`; in the shader the name yields the **value** from `VALUES` |
| `long` without `VALUES` | `int` |
| `color` | `color` (DEFAULT in sRGB) |
| `point2D` | `point`. Normalised (0..1) when `MAX` goes up to 1 (or, without `MAX`, the `DEFAULT` lies in 0..1); otherwise pixels of a 1920×1080 reference frame. The shader gets it in ISF space (y up; in pixels when the ISF uses pixels). |
| `image` (the input) | `aurea_source` |
| `IMG_NORM_PIXEL`, `IMG_PIXEL`, `IMG_THIS_PIXEL`, `IMG_THIS_NORM_PIXEL`, `IMG_SIZE` | equivalent functions |
| `isf_FragNormCoord`, `vv_FragNormCoord`, `gl_FragCoord`, `RENDERSIZE` | coordinates with the origin at the **bottom-left**, as in ISF |
| `TIME`, `TIMEDELTA`, `FRAMEINDEX`, `PASSINDEX` | layer time, `1/fps`, frame, `0` |
| `DATE` | `vec4(0)` (no wall clock: exports are deterministic) |
| `gl_FragColor` | the output |
| `texture2D` | `texture` |

Refused with a message: `PASSES` with more than one pass, `TARGET`/`PERSISTENT`,
`PERSISTENT_BUFFERS`, `IMPORTED`, `audio`/`audioFFT` inputs, a second input
image. Compile errors point at the **original `.fs` line**. For a multi-pass
effect, convert it into a `.aureaplugin` with `passes`.

## 7. Limits and why

| Limit | Value | Why |
| --- | --- | --- |
| Passes | 8 | Every pass is a texture and a memory round trip; memory bandwidth is the bottleneck on mobile GPUs. |
| Parameters | 16 | The parameter block fits in 320 bytes (the engine allows at most 1 KB per pass). |
| Inputs per pass | 3 + the source | The engine binds 4 textures per effect pass. |
| Linked layers (`layer`) | 4, `shader3d` only | Each layer's world travels in the parameter block; the 3D block takes 944 of the 1024 bytes. |
| Shader | 64 KB per file | On-device compilation costs CPU and memory; large shaders also compile slowly in the driver. |
| `plugin.json` | 256 KB | — |
| Package | 16 MB | — |
| `margin` | 1024 px | Every margin pixel grows the working texture. |
| Stack | 16 effects | — |
| **Test frame** | **250 ms at 256×256** | A heavy shader or a long loop **hangs a phone GPU** (the OS may reset the driver and kill the app). At install AUREA compiles every pass on the device GPU and renders one 256×256 frame with the default values; if it takes longer than 250 ms (measured after pipeline creation), installation is refused. At 1080p the same shader would cost ~30×. |

The measurement happens **on this device**: a plugin accepted on a fast phone
may be refused on an entry-level one. Design for the weakest. An infinite loop
can still hang the GPU during the test frame — the engine detects device loss
and refuses, but some drivers kill the app first; **never** write a loop whose
end depends on a computed value.

## 8. Performance tips

- **Loops with fixed bounds** (`for (int i = 0; i < 16; ++i)`); when the sample
  count depends on a parameter, keep the loop fixed and `break` on `i >= n`.
- **Count texture reads**: each `texture()` per pixel costs; a 7×7 blur is 49
  reads. Do the heavy part in a `scale: 0.25` or `0.5` pass (as `soft-glow`).
- Use `aurea_px_to_uv` for every length: a reduced preview runs fewer pixels
  without changing the look.
- `float` is enough; avoid `double`. On OpenGL ES everything is `highp`.
- Avoid `pow`/`exp`/`sin` inside large loops; precompute outside.
- Declare a `margin` only as large as the effect really reaches: it grows the
  working texture of every pass.
- Prefer `colorSpace: linear`: `srgb` adds a preparation pass.
- A `stack` plugin has no shader cost of its own: it inherits the built-ins'
  optimisations (colour ops fused into one pass).

## 9. Testing and packaging

1. Create a folder with `plugin.json` and the `.frag` files (start from an example).
2. Check and package with the script (Python 3.8+, no dependencies):

   ```
   python docs/plugins/tools/pack_plugin.py my-folder          # → my-folder.aureaplugin
   python docs/plugins/tools/pack_plugin.py my-folder -o x.aureaplugin
   python docs/plugins/tools/pack_plugin.py --check my-folder  # check only
   ```

   The script applies the app's rules (format, id, version, parameters, passes,
   inputs, stack, sizes, `shader3d`, `layer` and `accept`). GLSL compilation and the test frame happen only in the
   app.
3. To compile on a computer first, prepend the section 4 header to your file and
   run the Vulkan SDK `glslangValidator -V` (the same compiler, glslang).
4. Copy the `.aureaplugin` (or the `.fs`) to the device and install it from the
   app: **Effects → "Plugins: install and manage" → Install plugin** (Android
   and iOS). The same screen shows the author, version, whether the plugin
   works on this device and the test-frame time, and removes it. The effect
   appears in the **Plugins** group of the browser. A refusal names the file
   and line.
5. Test the effect on a layer with transparency, on a small layer in a big frame
   and in an export: the result must match the preview.

While developing, re-install with the **same version** as often as you like
(equal replaces).

## 10. Troubleshooting

Installation messages are in Portuguese (the engine language); `<...>` is a
value from your package. The meaning of each:

| Message | Cause and fix |
| --- | --- |
| `o arquivo nao e um pacote .aureaplugin (um ZIP com plugin.json)` | Not a ZIP (or a ZIP method other than stored/deflate). Use `pack_plugin.py`. |
| `pacote corrompido (ZIP invalido, nome de arquivo inseguro ou CRC errado)` | Damaged ZIP or a path like `../x` / absolute. |
| `pacote maior que 16 MB` | Remove files the plugin does not use. |
| `plugin.json ausente no pacote` / `plugin.json ausente ou ilegivel no pacote` | `plugin.json` must be at the ZIP root (or in a single subfolder). |
| `plugin.json invalido: <detail>` | Malformed JSON (trailing comma, quotes). |
| `plugin.json: falta "aureaPlugin": 1 (versao do formato)` / `... nao suportado (este app le a versao 1 do formato)` | Declare `"aureaPlugin": 1`. |
| `plugin.json: "id" invalido "<id>": use 3 a 64 caracteres a-z, 0-9, '.', '_' ou '-' (ex.: com.estudio.brilho)` | Lower case only; no spaces. |
| `plugin.json: "version" deve ser "x.y.z" (ex.: "1.0.0")` | — |
| `plugin.json: "kind" deve ser "shader", "shader3d" ou "stack"` | — |
| `plugin.json: <n> parametros (limite 16)` | Merge parameters or split the plugin. |
| `plugin.json: parametro "<id>": id invalido (letras, digitos e _, ate 32, sem palavra reservada do GLSL nem prefixo aurea/gl_)` | E.g. `mix`, `texture`, `float`, `uv`, `main`, `time`, `source` are reserved. |
| `plugin.json: parametro "<id>": tipo desconhecido "<t>" (use float, int, bool, enum, color, point, angle ou layer)` | — |
| `plugin.json: parametro "layer" so em plugin "kind": "shader3d"` | Change `"kind": "shader"` to `"shader3d"`. |
| `plugin.json: <n> parametros "layer" (limite 4)` | Use at most 4 linked layers. |
| `plugin.json: parametro "<id>": "accept" deve ser uma lista (camera, null, light, any)` / `... "accept" desconhecido "<x>" (camera, null, light ou any)` | E.g. `"accept": ["null", "camera"]`. |
| `plugin.json: parametro "<id>": "default" fora de [min, max]` | Default outside the range. |
| `plugin.json: parametro "<id>": enum precisa de "options" (lista de textos)` | — |
| `plugin.json: <n> passes (limite 8)` | Merge passes. |
| `plugin.json: passe <n>: falta "target" (so o ultimo passe escreve a saida)` | Every pass but the last needs a `target`. |
| `plugin.json: o ultimo passe nao tem "target": ele e a saida do efeito` | Remove the last pass's `target`. |
| `plugin.json: passe <n>: "scale" deve estar entre 0.125 e 1` | — |
| `plugin.json: passe <n>: entrada "<name>" le "<x>", que nao e "source" nem o target de um passe anterior` | A pass only reads targets of **earlier** passes. |
| `plugin.json: "margin" deve estar entre 0 e 1024 (px da camada)` | — |
| `passe <n>: caminho de shader invalido "<file>"` | Relative path, no `..`, `:` or leading `/`. |
| `passe <n>: arquivo de shader ausente: <file>` | File missing from the package (case matters). |
| `passe <n>: <file> maior que 64 KB` | — |
| `<file>:<line>: <glslang message>` | GLSL compile error on a line of **your** file, e.g. `rgb_wave.frag:12: 'offst' : undeclared identifier`. An `aurea_header:...` line means a clash with the header (e.g. redeclaring `aurea_source` or writing `#version`). |
| `<file>: a GPU recusou o shader: <message>` | This device's driver rejected the translated shader (ESSL/MSL). Simplify (big arrays, rare features) and report the device. |
| `o quadro de teste falhou: <detail>` | The effect did not render (pipeline refused, texture too large). |
| `o quadro de teste levou <t> ms (limite 250 ms em 256x256): simplifique o shader (lacos, amostras por pixel, passes)` | Sections 7 and 8. |
| `a GPU reiniciou durante o quadro de teste (laco infinito ou shader pesado demais)` | Endless loop or extreme cost. |
| `a GPU esta ocupada (preview ou export); tente instalar de novo` | An export or capture held the GPU for more than ~4 s. |
| `ja esta instalada a versao <v> de "<id>", mais nova que <w>` | Bump `version` (or remove the installed one). |
| `plugin.json: stack[<i>] (<effect>): efeito embutido desconhecido` | Wrong key (section 5). |
| `plugin.json: stack[<i>] (<effect>): este efeito nao pode entrar numa pilha (depende da camada, do tempo ou de dado proprio)` | Section 5. |
| `plugin.json: stack[<i>] (<effect>): parametro desconhecido "<p>"` | Use the built-in parameter id. |
| `plugin.json: stack[<i>] (<effect>): "<p>" tem <n> componentes: use uma lista ou o id de um parametro do mesmo tipo` | Colours/points need a list or a copy. |
| `plugin.json: stack[<i>] (<effect>): "<p>": nome desconhecido '<x>' (use o id de um parametro do plugin) (coluna <c>)` | Expression error (also `funcao desconhecida`, `falta ')'`, `texto sobrando`). |
| `ISF: falta o cabecalho JSON /*{ ... }*/ no inicio do arquivo` | The `.fs` must start with the JSON comment. |
| `ISF: ISF com varios passes (PASSES) nao e suportado; use um plugin .aureaplugin com "passes"` | — |
| `ISF: entrada de audio "<name>" nao e suportada` / `ISF: mais de uma imagem de entrada (...) nao e suportado` | — |
| `este build do app nao compila shaders de plugin` | A build without the compiler (never the published apps). |

Plugins that **fail to load** when the app starts (e.g. after an update that
changed the header) stay in the list with `ok: false` and the message in
`error`; the effect stays out of the catalogue until re-installed.

## 11. Projects, versions and removal

- The project stores the effect under `plugin.<id>` and the values by parameter
  index, like any effect.
- Opening a project **without** the plugin: the instance and all values (and
  keyframes) stay in the project; the panel shows the effect as unavailable and
  the layer renders without it. Saving loses nothing. **Installing the plugin
  later** brings the effect back with its values.
- **New version**: keep the `id` and the **parameter order** (projects store by
  index). Append new parameters **at the end**; old projects get their `default`.
- Removing a plugin takes the effect out of the catalogue and deletes its
  folder; projects behave as in the "without the plugin" case.

## 12. Engine API (for UI integrators)

C++ (`engine/include/aurea/Engine.hpp`):

```cpp
Result<std::string> install_plugin(const char* path) noexcept;   // id, or a Status whose detail() is readable
std::vector<plugins::PluginInfo> list_plugins() const;
std::string plugins_json() const;                                  // the list below
bool remove_plugin(const char* id) noexcept;
void set_plugin_directory(const char* path) noexcept;              // switch folder and load
std::string plugin_directory() const;
// EngineConfig::pluginDirectory — the folder loaded at initialize
```

Android (`AureaEngine.kt`, JNI in `aurea_jni.cpp`): `installPlugin(path)` →
`PluginInstallResult(ok, id, error)`; `installPluginRaw(path)` → `"ok:<id>"` or
`"error:<message>"`; `pluginsJson()`; `removePlugin(id)`; `pluginDirectory()`;
`initialize(..., pluginDir = defaultPluginDir(documentsDir))` (default
`files/plugins`). Natives: `nativeInstallPlugin(handle, path): String`,
`nativeListPlugins(handle): String`, `nativeRemovePlugin(handle, id): Boolean`,
`nativePluginDirectory(handle): String`.

iOS (`AureaEngine.h`): `installPlugin:` → `"ok:<id>"`/`"error:<message>"`,
`pluginsJSON`, `removePlugin:`, `pluginDirectory` (Documents/Plugins, wired in
`startWithDevice:`).

List JSON (`plugins_json` / `nativeListPlugins` / `pluginsJSON`):

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

- `name`, `description`, `label` and each option are always locale → text
  objects (`"*"` = the text applies to every locale). Fallback: the locale,
  `en`, `pt-BR`, `*`, the first.
- `effectTypeId` is the effect id used by the catalogue and layer-effect queries
  (`query_effect_catalog`, `query_layer_effects`): the UI joins a catalogue row
  (category `Plugins`, pt-BR name) to its plugin by it and shows names/labels in
  the device language.
- `errorCode` (stable): `manifest`, `shader_source`, `shader_compile`, `stack`,
  `unsupported`, `duplicate`, `registry`. `testRenderMs` is -1 when the plugin
  was loaded from the folder (not measured this session).
- `scene3d: true` appears on `"kind": "shader3d"` plugins (the list's `kind`
  stays `shader`). The UI shows "3D shader (camera and nulls)". `layer`
  parameters come with `"type": "layer"`.

## 13. Licences

The on-device compiler uses two open-source libraries, fetched at build time at
fixed revisions (URL + SHA-256 in the CMake files):

| Library | Pinned revision | Licence | Use |
| --- | --- | --- | --- |
| glslang (Khronos), the revision ncnn pins (`nihui/glslang` `a9ac7d5f307e5db5b8c4fbf904bdba8fca6283bc`, 15.x base) — `engine/cmake/AureaNcnn.cmake`, `engine/cmake/AureaRuntimeShaders.cmake` | see left | BSD-3-Clause, BSD-2-Clause, MIT and Apache-2.0 (its `LICENSE.txt` also carries the GPL-3.0 notice with the Bison exception, which covers the generated parser and permits use under any licence) | GLSL → SPIR-V |
| SPIRV-Cross (Khronos) `d8e3e2b141b8c8a167b2e3984736a6baacff316c` — `engine/cmake/AureaShaderXlate.cmake` | see left | Apache-2.0 | SPIR-V → ESSL (Android GLES) and MSL (iOS) |

Their licence notices must ship with the app's licences screen. Plugins belong
to their authors: AUREA does not redistribute third-party plugins. The examples
in this folder were written for AUREA and may be freely used as a starting
point.

Plugin scripts (0.3.4) run on **AureaScript**, an interpreter written for AUREA
(`engine/src/plugins/script/`): no new third-party library.

## 14. Panels, scripts and APIs (format 2, 0.3.4)

Starting with 0.3.4 a plugin can do much more than add an effect: **change the
interface** (Home tabs, editor panels, menu items, buttons on the layer bar),
**run scripts** with an object API in the style of After Effects scripts and
**fetch data from internet APIs** on the domains it declares. It all ships in the
same `.aureaplugin`, with `"aureaPlugin": 2`.

- [14.1 The format 2 manifest](#141-the-format-2-manifest)
- [14.2 Panels and controls](#142-panels-and-controls)
- [14.3 Actions (menus and the layer bar)](#143-actions-menus-and-the-layer-bar)
- [14.4 Scripts: the AureaScript language](#144-scripts-the-aureascript-language)
- [14.5 API reference](#145-api-reference)
- [14.6 Permissions and network](#146-permissions-and-network)
- [14.7 Limits, errors and the console](#147-limits-errors-and-the-console)
- [14.8 Examples](#148-examples)
- [14.9 Engine API (for the interface)](#149-engine-api-for-the-interface)

### 14.1 The format 2 manifest

```json
{
  "aureaPlugin": 2,
  "id": "com.studio.text",
  "version": "1.0.0",
  "name": {"pt-BR": "Texto animado", "en": "Animated text"},
  "kind": "panel",
  "script": "main.js",
  "permissions": {"network": ["api.example.com"], "clipboard": false, "files": false},
  "strings": {"pt-BR": {"done": "{0} camadas criadas"}, "en": {"done": "{0} layers created"}},
  "ui": {"panels": [ ... ], "actions": [ ... ]}
}
```

| Field | What it is |
| --- | --- |
| `aureaPlugin` | `2`. An older app (format 1 only) refuses it with "format version not supported". `ui`, `script`, `permissions` and `strings` with `1` are refused. |
| `kind` | `"panel"` (new: no effect, only interface and script) or the effect kinds (`shader`, `shader3d`, `stack`), which may now also carry an interface and a script. |
| `script` | A `.js` file in the package (up to 512 KB). Its syntax is checked on install: an error refuses the package with `file:line:column`. |
| `permissions` | What the plugin asks for (section 14.6). Unknown fields are refused. |
| `strings` | Script texts per language (`app.t("key", ...)`, with `{0}`, `{1}`...). Up to 512 keys per language. |
| `ui` | `panels` (up to 8) and `actions` (up to 16). |

Every text (`title`, `label`, `options`, `placeholder`, `text`) takes plain text or
a language → text object, like `name`, with the fallback order of section 3. Size
limits count **characters** (Hindi, Russian and Arabic count the same as English).

### 14.2 Panels and controls

```json
{
  "id": "main",
  "title": {"pt-BR": "Texto animado", "en": "Animated text"},
  "icon": "sparkles",
  "placement": ["editor.panel", "home.tab"],
  "onOpen": "opened",
  "controls": [
    {"id": "text", "type": "text", "label": "Text", "default": "AUREA"},
    {"id": "make", "type": "button", "label": "Generate", "onClick": "generate"}
  ]
}
```

- **`placement`**: `editor.panel` — the panel opens in the editor, inside the common
  panel frame (‹ closes it); it is listed in the **plugin menu** (wrench button at
  the top of the editor) and under Plugins > "Open". `home.tab` — becomes a **Home
  tab**: with a single plugin tab the dock shows its name and icon; with several, a
  "Plugins" tab with a picker at the top.
- **`icon`**: one of these names (both platforms use AUREA's own icons):
  `wand sparkles text star bolt cloud music photo film layers gear brush clock list
  cube lightbulb wrench book flag grid camera chart sliders eye folder link speed
  waveform`. Any other name is refused.
- **`onOpen`**: function called when the panel opens (optional).
- **`controls`**: 1 to 32. Control ids are unique across the whole plugin (the
  script reads them with `ui.get("id")`).

| `type` | Fields | Value (`ui.get`) | Event |
| --- | --- | --- | --- |
| `button` | `label` (required), `style`: `primary` · `secondary` · `danger` | — | `onClick` (required) |
| `slider` | `min`, `max`, `step` (0 = continuous), `default`, `unit` | number | `onChange` (on release) |
| `ruler` | drag ruler: optional `min`/`max` (±1e6), `step`, `default`, `unit` | number | `onChange` (on release) |
| `color` | `default` `"#RRGGBB"` or `"#RRGGBBAA"` | `"#RRGGBB"` | `onChange` |
| `choice` | `options` (1 to 32 texts), `default` (index) | index | `onChange` |
| `text` | `default`, `placeholder`, `multiline`, `maxLength` (≤ 10000) | text | `onChange` (on confirm) |
| `list` | initial `items` `[{id, label, detail}]` (≤ 200) | chosen id or `null` | `onSelect` |
| `image` | `src` (`.png`/`.jpg` in the package, ≤ 2 MB), `aspect` (width/height) | — | — |
| `label` | `text` | the text | — |
| `toggle` | `default` (`true`/`false`) | boolean | `onChange` |

Handlers receive `(value, controlId)`. Every call coming from the interface is
**one undo step** (see `app.beginUndoGroup`).

### 14.3 Actions (menus and the layer bar)

```json
{"id": "quick", "title": "Animate layer", "icon": "bolt", "placement": "layer.toolbar", "call": "animate"}
```

| `placement` | Where it shows |
| --- | --- |
| `editor.menu` | in the plugin menu at the top of the editor |
| `layer.menu` | in the layer ⋮ menu (plugin section at the end) |
| `layer.toolbar` | button on the selected layer's bar (`icon` required) |

Each action has **`call`** (a script function) **or** **`panel`** (opens that
panel, which needs `editor.panel`). `needsLayer` (default `true` for layer actions).

### 14.4 Scripts: the AureaScript language

The script is **JavaScript** (an ES2020 subset) run by **AureaScript**, AUREA's own
interpreter, identical on Android and iOS. The file runs once when the plugin is
first used (defining functions and global variables, which **persist between
calls**); after that the interface calls the functions.

Supported: `let`/`const`/`var` (all block scoped), functions, arrow functions,
closures, `this` in methods, `new` with constructor functions, objects, arrays,
template strings, destructuring (`const {a, b} = obj`, `[x, ...rest]`, also in
parameters), spread (`...`), default parameters, `if`/`switch`/`for`/`for...of`/
`for...in`/`while`/`do`, `try`/`catch`/`finally`/`throw`, `?.`, `??`, `??=`, `**`,
`typeof`, `instanceof`, `in`, `delete`.

Library: `Math`, `JSON` (`parse`, `stringify` with indentation), `Object` (`keys`,
`values`, `entries`, `assign`, `fromEntries`, `freeze`, `hasOwn`), `Array`
(`isArray`, `from`, `of` and the methods `push pop shift unshift slice splice
concat join reverse indexOf lastIndexOf includes at forEach map filter find
findIndex findLast findLastIndex some every reduce reduceRight sort flat flatMap
fill`), string methods (`toUpperCase toLowerCase trim trimStart trimEnd includes
startsWith endsWith indexOf lastIndexOf slice substring substr charAt charCodeAt
codePointAt at split replace replaceAll repeat padStart padEnd concat
localeCompare`), `Number` (`toFixed`, `toPrecision`, `toString(radix)`,
`isInteger`...), `String`, `Boolean`, `parseInt`, `parseFloat`, `isNaN`,
`isFinite`, `encodeURIComponent`, `decodeURIComponent`, `Date` (UTC: `Date.now`,
`new Date()`, `toISOString`, `getFullYear`...), `Error`, `TypeError`,
`RangeError`, `ReferenceError`, `SyntaxError`, `console.log/info/warn/error`.

Not supported (refused with a specific message): `class`, `async`/`await`/Promise,
generators, regular expressions (use `split`, `includes`, `replaceAll`),
`import`/`export`/`require`, `eval`, `Function`, labels, getters/setters in
literals. There is **no** `window`, `document`, `XMLHttpRequest`, `process`,
`globalThis` or anything that touches files or the system: the script only reaches
what the reference below lists.

Small differences from JavaScript: strings count **characters** (`"ação".length`
is 4); `var` is block scoped; `Math.random` is reproducible per plugin.

### 14.5 API reference

Times in **composition seconds**; layer indices **from 1, top to bottom** (as in
After Effects); position and anchor in pixels, scale and opacity in %, rotation
in degrees; colors `[r, g, b]` (or `[r, g, b, a]`) from 0 to 1, or `"#RRGGBB"`.

**`app`**

| Member | |
| --- | --- |
| `app.version` | `"0.3.4"` |
| `app.language` | the app language for the call (`"pt-BR"`, `"en"`...) |
| `app.plugin` | `{id, version, name}` |
| `app.project` | the project (below); `app.project.activeItem` is `null` with no project open |
| `app.beginUndoGroup(name)` / `app.endUndoGroup()` | group changes into one undo step (nestable). Every call from the interface is already a group; groups left open are closed at the end. |
| `app.alert(text)` | alert with OK |
| `app.t(key, ...args)` | text from `strings` in the app language (`{0}`, `{1}` replaced by the arguments) |

**`app.project`**: `name`, `numItems`, `item(1)`, `activeItem` (the open composition).

**Composition** (`app.project.activeItem`)

| Member | |
| --- | --- |
| `name`, `width`, `height`, `frameRate`, `frameDuration`, `numLayers` | read |
| `duration` | read and write (seconds) |
| `time` | read and write: the playhead |
| `layer(index or name)` | a layer (`null` if none) |
| `layers` | AE-like collection: `layers.length`, `layers[1]`... plus `addText(text)`, `addShape(shape)`, `addSolid(color, name, width, height)`, `addNull(threeD)`, `addCamera()`, `addLight(type)`, `byName(name)`, `toArray()` |
| `allLayers` | a JavaScript array of the layers (top to bottom) |
| `selectedLayers` | the selected layers |
| `markers` | `[{time, name, color, beat}]` |
| `addMarker(time, name, color)` / `removeMarker(time)` | composition markers |
| `renderFrame({maxSize})` | renders the playhead frame (up to 1024 px): `{width, height, pixel(x, y), average()}`; can be shown in an `image` control with `ui.setImage` |

`addShape` takes a shape index or `circle`, `roundedSquare`, `cross`, `ring`,
`triangle`, `hexagon`, `arrow`, `square`, `star`, `capsule`, `heart`, `line`,
`diamond`.

**Layer**

| Member | |
| --- | --- |
| `id`, `index`, `type` (`text shape video image audio null camera light model3d particles group adjustment`) | read |
| `name`, `inPoint`, `outPoint`, `enabled`, `locked`, `threeD`, `selected`, `parent` | read and write |
| `startTime` | read |
| `text`, `fontSize` | text layers |
| `fillColor` | text and shape |
| `position`, `scale`, `rotation`, `rotationX`, `rotationY`, `rotationZ`, `opacity`, `anchorPoint` | properties (below); also as `transform.*` and `property("Position")` |
| `effects`, `effect(name, key or index)`, `addEffect(key)` | effects (`"aurea.blur.gaussian"`...) |
| `remove()`, `duplicate()`, `moveTo(index)`, `moveToBeginning()`, `moveToEnd()` | |

**Property**

| Member | |
| --- | --- |
| `name`, `value`, `dimensions`, `numKeys`, `isTimeVarying` | read |
| `valueAtTime(t)` | value at a time |
| `setValue(v)` | static value. With keyframes it refuses (use `setValueAtTime`), as in AE |
| `setValueAtTime(t, v)`, `setValuesAtTimes(ts, vs)`, `addKey(t)` | keyframes |
| `keyTime(k)`, `keyValue(k)`, `nearestKeyIndex(t)`, `removeKey(k)` | `k` starts at 1 |
| `setInterpolationTypeAtKey(k, type)` | `linear`, `hold`, `bezier`, `ease`, `easeIn`, `easeOut` |

**Effect**: `name`, `matchName` (the key), `enabled` (read and write),
`numProperties`, `property(id, label or index)` (a Property), `remove()`.

**`ui`** (the plugin's controls)

| Function | |
| --- | --- |
| `ui.get(id)` / `ui.set(id, value)` | control value (types are checked: an invalid color = `TypeError`) |
| `ui.setItems(id, items)` | items of a `list`: texts or `{id, label, detail}` |
| `ui.setText(id, text)` | text of a `label` |
| `ui.setLabel(id, text)` | replaces any control's label |
| `ui.setEnabled(id, yes)` / `ui.setVisible(id, yes)` | |
| `ui.setImage(id, file or frame)` | a package `"cover.png"`, a `renderFrame()` result or `null` |
| `ui.progress(fraction, text)` | progress bar while the function runs (`null` clears it) |
| `ui.toast(text)` / `ui.alert(text)` | messages |
| `ui.openPanel(id)` | opens another panel of the plugin in the editor |

**`storage`** (per plugin, persistent, 256 KB): `get(key, fallback)`, `set(key,
value)` (any JSON value), `remove(key)`, `keys()`.

**`fetch(url, options)`** (network permission): synchronous, returns
`{ok, status, url, headers, text(), json()}`. Options: `method` (`GET POST PUT
PATCH DELETE HEAD`), `headers` (object), `body` (text, or an object sent as JSON),
`timeout` (ms, 1000 to 30000). Errors: `PermissionError` (no permission / domain
not listed) and `NetworkError` (offline, timeout, response too large).

**`clipboard.readText()` / `clipboard.writeText(text)`** (clipboard permission).

**`files.pick({types: ["json", "txt", "csv"]})`** (files permission): opens the
system picker and returns `{name, size, text(), json()}` (up to 1 MB), or `null`
if the person cancels. Only the chosen file; nothing else on disk.

**`console.log/info/warn/error`**: go to the plugin console (Plugins > Console).

### 14.6 Permissions and network

```json
"permissions": {"network": ["open-meteo.com"], "clipboard": true, "files": true}
```

- **`network`**: up to 8 domains, name only (`api.example.com`; no `https://`,
  port or path; `*` is refused). A domain also covers its **subdomains**
  (`open-meteo.com` allows `api.open-meteo.com`). `fetch` only accepts `https://`,
  no explicit port or `user@`, and **does not follow redirects** (a 3xx comes back
  to the script). Responses up to 2 MB, request bodies up to 256 KB, 15 s by default.
- **`clipboard`** and **`files`**: as above.
- **Consent**: on install the app shows a sheet with what the plugin asks for; each
  permission has a switch the person can turn off before installing. Later, under
  Plugins, each permission can be turned on and off.
- **Denied permission** (not declared in the manifest, or turned off by the
  person): the function throws `PermissionError` and no byte leaves the device.
  Catch it with `try/catch` if you want your own message.

### 14.7 Limits, errors and the console

| Limit | Value | When exceeded |
| --- | --- | --- |
| CPU time per call | 5 s (waiting for the network or the picker does not count) | `LimitError`, the call stops |
| Steps per call | 20 million | `LimitError` |
| Script memory | 32 MB live | `LimitError` |
| Call depth | 64 | `RangeError` |
| Script size | 512 KB | refused on install |

Limits and cancellation (the panel's **Stop** button) **go through the script's
`try/catch`**: an infinite loop cannot hang the app. The context stays usable.
Memory cycles (closures pointing at each other) are collected automatically.

Errors show **in the panel** (with `file:line:column`) and in the plugin
**console** (last 200 lines, including the script's `console.log`). A failing
script does not crash the app or the project; whatever it already changed undoes
in one tap. For an error while loading the file: fix it and reinstall, or use
"Reload script" in the console.

**On/off**: under Plugins, the "On" switch hides the plugin's panels and actions and
stops its script. The plugin's effect (if any) stays in the catalog, so projects
that use it don't change.

### 14.8 Examples

| Folder / file | What it shows |
| --- | --- |
| `animated-text/` → `animated-text.aureaplugin` | **Animated text generator**: editor panel and Home tab; text, choice, slider, ruler, toggle, color; creates text layers with keyframes (opacity, position, scale, rotation, `ease`) in one undo step; a layer-bar button that animates the selected layers; `storage`; texts in all 7 languages |
| `weather-text/` → `weather-text.aureaplugin` | **Weather on screen**: `fetch` from the public Open-Meteo API (`permissions.network: ["open-meteo.com"]`, using the `geocoding-api` and `api` subdomains), a result list filled by the script, `NetworkError` handled, creates a text layer with the temperature |

Pack with `python docs/plugins/tools/pack_plugin.py <folder>` (it checks format 2:
ids, icons, placements, permissions and that every function the interface calls
exists in the script).

### 14.9 Engine API (for the interface)

C++ (`Engine.hpp`, `0.3.4 plugins` block):

```cpp
std::string inspect_plugin(const char* path) noexcept;          // reads without installing: what it asks for
bool set_plugin_enabled(const char* id, bool on) noexcept;
bool set_plugin_permissions(const char* id, u32 granted) noexcept;   // 1 network, 2 clipboard, 4 files
std::string plugin_call(const char* id, const char* fn, const char* argsJson, const char* locale) noexcept;
std::string plugin_event(const char* id, const char* control, const char* kind, const char* valueJson, const char* locale) noexcept;
std::string plugin_open_panel(const char* id, const char* panel, const char* locale) noexcept;
std::string plugin_state(const char* id) noexcept;
std::string plugin_console(const char* id) noexcept;
void plugin_clear_console(const char* id) noexcept;
void plugin_cancel(const char* id) noexcept;       // any thread
void plugin_reload(const char* id) noexcept;
void set_plugin_host_io(plugins::HostIO io) noexcept;   // the platform's fetch, clipboard and picker
```

`plugin_call`/`plugin_event`/`plugin_open_panel` **block** (the script runs on the
caller's thread): the interface calls them from a large-stack (8 MB) worker thread.
One script call at a time in the engine; a busy plugin answers `BusyError`. The
reply:

```json
{"ok": true, "value": <the function's return value as JSON>,
 "error": {"name": "TypeError", "message": "...", "text": "main.js:12:5: TypeError: ...",
           "file": "main.js", "line": 12, "column": 5, "aborted": false},
 "messages": [{"kind": "toast" | "alert" | "select", "text": "..."}],
 "openPanel": "",
 "state": {"values": {...}, "items": {...}, "texts": {...}, "labels": {...},
           "disabled": [...], "hidden": [...], "images": {...},
           "progress": 0.5, "progressText": "", "busy": false}}
```

(`"error"` only when `ok` is false; `progress` and a `list` with no choice come as `[]`.)
`plugins_json()` gained `format`, `hasScript`, `enabled`, `generation`,
`permissions` `{network[], clipboard, files}`, `granted` `{network, clipboard,
files}` and `ui` (`panels` with `controls`, and `actions`). On/off and permissions
live in `<plugin folder>/.estado.json`; `storage` and frames in `<folder>/.dados/`.

Android: `AureaEngine.kt` (`inspectPlugin`, `setPluginEnabled`,
`setPluginPermissions`, `pluginCall`, `pluginEvent`, `pluginOpenPanel`,
`pluginState`, `pluginConsole`, `pluginClearConsole`, `pluginCancel`,
`pluginReload`, `installPluginHost`); I/O in `effects/PluginHost.kt`; the interface
in `effects/PluginRuntime.kt` and `effects/PluginUi.kt`. iOS: `AureaEngine.h` (same
names, `installPluginHost` and the `pluginPickFile` block) and `PluginsV2.swift`.
