# Camera creation and 3D transform keyframes

## Proven fixes

The transform rail keyed only X/Y for position, scale and anchor, even when
3D/null layers exposed depth. Both Android TransformPanel and iOS TransformView
now key the Z member as part of each 3D property group. Recomposition also tracks
3D mode so the diamond state cannot keep a stale planar group. Android's static
transform setter omitted ScaleZ/AnchorZ, and iOS omitted AnchorZ; those cases now
write actual engine transform values.

The Add > 3D tab previously had no camera entry and there was no public camera
creation bridge. Engine::add_camera now creates a real Camera layer matching
the implicit composition camera, covering the composition, makes it active and
deactivates prior cameras in one history mutation. It creates no animation keys.
Android JNI and iOS ObjC expose it; real Add3D cards select the resulting camera,
whose existing transform panel edits/animates the shared camera tracks.

## Validation queued

- TransformKeyPropertiesTest: 3D versus planar key groups.
- GPU CreatedCameraAndNullDepthTracksRenderAndReload: default camera framing,
  activation exclusivity, real null depth/camera motion pixels, seek and reload.
- GPU NewText3DIsStaticWithoutPresetAnimation: default recipe and two captures.
- iOS API and bridge symbol checks passed; native build/UI tests remain required.

Text3D default is already static at the C++ spec, Android recipe and iOS creation
boundary. No speculative default change was made. The new regression checks this
explicitly. A separately reported iOS gizmo UITest failure is under investigation;
its timeout is not evidence that this UI path is validated.

## iOS gizmo failure root cause

The real native UITest `testText3DSelectionFollowsCoreBoundsAndGizmoDrag` failed.
`stepGizmo` submitted three scalar `setTransform` calls. Each reads XYZ from the
core, but `Batch::submit` is asynchronous, so subsequent Y/Z setters could reuse
old X and undo the X-axis move. Android already submits one full position.
iOS now submits one XYZ position command (or the three keyed components in one
batch). The existing UITest is retained unchanged for rerun on macOS.


## Completed host checks and dedicated workspace

- `camera-null-depth-test.log`: CreatedCameraAndNullDepthTracksRenderAndReload,
  1 test / 33 checks, PASS (actual offscreen GPU capture and save/reload).
- `static-text3d-test.log`: NewText3DIsStaticWithoutPresetAnimation,
  1 test / 12 checks, PASS (actual offscreen captures).
- `remap-current-test.log`: 6 tests / 139 checks PASS on the same host binary;
  later remap cases are tracked separately by the remap owner.

The dedicated Scene workspace now uses the actual preview renderer with a
preview-only observer camera. Its orbit/zoom never changes the composition
camera or export settings. The selected layer gizmo uses the observer projection;
core guide queries project the composition grid, camera frustums and nulls from
actual layer transforms. The object list selects real layers. Existing model
import and material/lighting/HDRI panels remain accessible in workspace sheets.

`LayerLayoutTransform` offsets position/rotation/anchor tracks and multiplies
nonzero scale curves, including tangents, without inserting or moving keys.
A zero scale uses an additive offset so an invisible object can become visible
while preserving differences between keyed poses. Expression-driven transforms
reject layout edits instead of destroying expressions. Timeline mode retains
normal keyframing semantics. New layout and observer/export GPU regressions are
queued in the consolidated build; no native UI validation claimed yet.

Android/iOS expose the same observer, guides, layout command, workspace selection,
XYZ fields, gizmo, imports and material sheets. iOS API/symbol static checks pass;
these checks are not a native Swift build or device run.


## Workspace validation and lights/material extension

- `scene-layout-test.log`: 1 test / 32 checks PASS. Existing keys retain their
  times/count; additive layout offset and nonzero/zero scale behavior verified;
  actual camera/null guide output verified.
- `scene-observer-test.log`: 1 GPU test / 11 checks PASS. Observer changes rendered
  pixels, while final-quality export is pixel-identical with observer on/off.

Directional and point lights now have real creation APIs on both platforms and
are actual timeline layers with editable XYZ transforms and intensity/RGB tracks.
The workspace lighting sheet edits intensity, RGB, point range and directional
shadows. Range and shadow controls are shown only where the renderer supports
those behaviors. Intensity/color/cone/penumbra tracks are now sampled by PBR; they
were previously serialized but ignored by the lighting context builder.

Per-layer material overrides preserve imported SceneAssets and GPU textures.
Overrides target an actual material index and RGBA/metallic/roughness fields;
MaterialParam tracks retain index/parameter identity. Timeline v24 persists the
new records (serialization validation is tracked by the keyframe agent). GPU
factors are copied per instance and retained by the deferred frame-graph pass,
so deleting/replacing an object cannot invalidate queued override pointers.
Explicit opacity below 1 promotes an originally opaque instance to blending;
explicit metallic/roughness edits enable PBR shading for an unlit source instance.
The source material remains unchanged for other objects sharing it.

The imported-material panels and matching indexed keyframe routes exist on both
platforms; Text3D retains its geometry/material controls. Undo/redo and model
import are available within the workspace. Pending consolidated regressions:
`EditableLightsAndMaterialOverridesRenderAndReload` (GPU object isolation, alpha,
keyframes and reload) and `LightIntensityKeyframesChangeActualPbrPixels` (actual
light/PBR pixels). Native Android workspace checks and Swift CI remain required.


## Consolidated extension checks

- `material-gpu-test.log`: EditableLightsAndMaterialOverridesRenderAndReload,
  PASS 1 test / 38 checks (per-instance color isolation sharing the same asset,
  alpha transparency/restoration, animated overrides and save/reload).
- `light-isolation-test.log`: LightIntensityKeyframesChangeActualPbrPixels,
  PASS 1 test / 21 checks. Direct-light pixel delta 248; dark coverage 0 versus
  bright coverage 0.084219. The test disables studio IBL to avoid saturating the
  white test surface before the direct light is enabled. Metal/roughness edits
  also change actual PBR pixels.
- `serialization-current-test.log`: PASS 26 tests / 65,912 checks, including all
  five invalid material cases. Validation now propagates ByteReader failure out
  of the timeline section; merely moving the reader to its end previously
  returned success for malformed data in the final layer.
- `upscaler-post-heartbeat.log`: PASS 2 tests / 35 checks after a heartbeat
  interrupted the first consolidated run. Full run restarted separately.

The emulator project `engine/build/android-p0/scene-ui.aurea` was inspected without
changing it. Its invisible Text3D layer is at X=11000, Y=540, Z=0 in a 1920x1080
composition (camera 960,540,-1296); scale/opacity/visibility are valid and there
are no keys. Its embedded recipe has `anim=0`. This evidence explains that saved
state being offscreen; it is not a claim that native UI rendering has passed.


## Final host run and native numeric-field defect

`full-post-heartbeat-test.log`: **721 tests / 4,554,235 checks / 0 failures**, exit
0. Includes AI, light/material GPU pixels, serialization, observer/layout,
remap, export and existing rendering regressions. No AI crash was reproduced
after the heartbeat interruption.

The emulator X=11000 finding exposed a UI bug, not merely operator input:
Android's numeric draft was remembered using the evaluated Float as a key. Each
live command reformatted/recreated the field (adding `.0` and moving its caret)
while later characters were still arriving. Swift's formatted numeric binding
had the same risk. Both workspace XYZ and light fields now preserve a String
(and Android TextFieldValue selection) while focused and only resynchronize
external numeric values after focus is released. Android selects the entire
value on first focus. Native retyping verification is pending with the parent;
Swift compile/UI verification remains CI-dependent.


## Native scene follow-up: exact draft and remaining transition defects

With explicit select-all followed by `1100`, Android preserves the exact draft
without inserting decimal zeros. `engine/build/android-p0/scene-coordinate-1100.png`
shows the Text3D object and camera frustum; the text is also visible in the main
preview. The earlier invisible saved object at X=11000 was offscreen after the
numeric input defect; it is not evidence that default Text3D rendering failed.

First-focus selection was still broken in that APK: tapping X and typing `960`
into `11000.0` produced `11000960.0`. Foundation's first-tap caret placement can
overwrite selection applied directly in `onFocusChanged`. The subsequent patch
observes pointer events without consuming them and applies initial select-all
after the triggering release, preserving later taps/caret edits. It also cancels
pending selection when actual text changes. The shared helper covers light and
XYZ fields. This patch still requires the parent's fresh-APK validation.

Scene controls also persisted after returning to Timeline: both
`scene-exit-final.png` and `scene-exit-stable.png` (about 15 seconds later) show
clipped orbit controls; the latter XML retains live numeric fields at
Y=2230..2340 alongside the main editor. This is a persistent composition defect,
not a transient screenshot or only an unclipped stage overlay. The pending fix
scopes movable preview identity to scene-editor mode, disposing the old scene
subtree at that transition instead of extracting its AndroidView while removing
the surrounding Column. Normal narrow/wide/fullscreen surface reuse remains.
Existing surface lifecycle callbacks handle detach/rebind. Fresh-APK verification
must establish that no orphan controls remain and the new preview still renders.
