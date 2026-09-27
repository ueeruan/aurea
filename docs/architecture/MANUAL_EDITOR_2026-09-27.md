# Manual editor acceptance — 27 September 2026

Work continues in the existing Android/iOS editor and shared engine. This is an
implementation checkpoint, not a claim that the full professional-editor prompt
has passed acceptance.

## Implemented in this checkpoint

- Hold a layer body and drag vertically to reorder. Ordinary vertical swipes
  scroll; horizontal drags move time. The lifted layer and its expanded property
  rows move together, other rows make space, and release commits one undo step.
- Gizmo screen extent stays fixed while movement uses the original projection.
  World/Local XYZ choice uses evaluated engine transforms, including parent scale.
- Easing changes and curve presets affect only the chosen component's outgoing
  segment, rather than coincident X/Y/Z or effect-component keys.
- Value graphs support frame-aligned horizontal time edits and vertical value
  edits, bounded by neighbouring keys so they are not overwritten.
- Value graphs now include Select/All, group time drag, Copy/Paste, Duplicate
  and Delete. Shared engine operations preserve relative timing and reject
  collisions atomically. Rotation diamonds/focus follow the selected axis.

## Evidence so far

- Android: 132 JVM tests passed after graph-time changes.
- Emulator API 35: six actual editor tests passed (scroll, body reorder/undo,
  horizontal move versus tap, independent curves, trim, compact-panel trim).
- Local/World engine regression passed: 2 tests, 29 checks.
- Actual Android value-graph touch drag and single undo passed.
- Shifted/trimmed clipboard regression passed: 4 clipboard tests, 63 checks.
  iOS toolbar now passes composition time to the engine for keyframe copy/paste.
- Material preset access moved to the top of the text 3D panel on both platforms;
  iOS chips now have a minimum 44-point touch target. Android compilation passed.
- Rotation component editing no longer inserts keys in untouched axes; Android
  real-editor rotation and graph tests both passed. Native dial test added.
- Repeated effect occurrences retain separate pasted animation; locked targets
  are skipped. Five clipboard tests / 82 checks passed.
- Snapping is exposed in both editor menus and respected by Android clip move,
  trim and key drag. Actual Android gesture test passed with the same drag first
  ignoring and then snapping to a nearby marker.
- Existing parenting tests: 6 / 750 checks passed, including GPU world-image
  preservation. Existing ClipTime tests: 19 / 1206 checks passed, including
  remap/reverse/freeze and audio timing. These are not full mobile acceptance.
- Shared multi-key clipboard tests: 6 / 106 checks passed. Android real graph
  test passed group dragging, spacing, copying, pasting, duplication, deletion
  and single-step undo. Native iOS multi-key gesture regression added.
- GPU motion blur tests: 4 / 121 checks passed, including 3D objects, glyphs,
  particles and full-preview/export equivalence for particles.
- Native d9c74d13 compile caught an Int64/Int32 mismatch in the toolbar clipboard
  call. Explicit bounded conversion is now applied; native retest is required.
- iOS compile/package passed at 084592bd. The first simulator dispatch used
  invalid capture scene names and did not execute gestures; corrected dispatch
  is run 36319772180. This is not a native gesture pass.
- iOS native regressions added for body reorder, isolated easing and graph
  time/value dragging. Native execution is still required.
- This does not validate a physical Samsung A51 5G or sustained iPhone workload.

## Acceptance still to complete

### Continued implementation checkpoint

- Explicit transform Auto-Key toggle on both stages. Off offsets the complete
  existing transform animation without inserting keys; On retains component-only
  keying. Layout edits now also support opacity/skew. Android toggle/undo passed;
  native iOS regression added, not yet run for this checkpoint.
- Speed Graph now has interval tangent handles: horizontal dragging changes
  influence and vertical dragging changes signed endpoint velocity. Writes the
  original outgoing Bézier interval, preserves key times/values and other tracks,
  and groups one drag into one undo. Three Android graph gesture tests passed,
  including speed handles and independent rotation/Auto-Key. iOS test added.
  Flat intervals and hold/bounce/elastic/steps retain their own easing editor;
  they do not show tangent handles that their evaluator cannot represent.
- RGB Split and Chromatic Aberration are independent shared GPU effects, with
  animated channel offsets/radial amount, mix, edge handling and expanded bounds.
  Two real Vulkan pixel tests passed (6167 checks): RGB locations, signed radial
  channel reversal, and zero/mix identity. iOS Metal/native acceptance pending.
- Explicit manual time-remap curve entry added on both platforms.
- Android eight-clip integration test passed: actual video/audio import, 16
  markers from UI, UI split, null parenting, effect search/stack, motion blur,
  remap, playback beyond six seconds across cuts, save/reopen and content checks.
  Fixture import/numeric setup uses the normal store API. Synthetic media and
  mixed API/UI interaction do not constitute a finished real AMV acceptance.
- Expanded Android integration also passed with six stacked effects (blur, glow,
  RGB Split, Shake, Turbulent Displace, Motion Tile), 2D/3D text changed through
  the actual text dialog, and an animated camera retained after reopen.
- Gizmo position writes now touch only changed components, keying only already
  animated axes. An X drag no longer manufactures Y/Z keys. Android regression
  passed; native iOS touch regression added.
- Native f15ef224 compiled and packaged successfully. Simulator transform
  capture timed out at first launch; text-3d and layer-dock captures succeeded.
  Its gesture suite completed: 26/27 passed, including layer reorder, independent
  easing, group-key moves, value graph, video/text/captions and decoder matrix.
  Compact curve accessibility bounds still included 34 pt below its container;
  explicit content/scroll clipping added and awaits native retest. New features
  above are not included in that older native build.
- Animated anchor is now sampled when projecting the gizmo, matching the
  evaluated renderer transform. Three core gizmo tests / 44 checks passed.
- The expanded Android project and its two synthetic media files are preserved
  in engine/tests/data/manual-editing with hashes. A native iOS test loads that
  exact project, verifies resolved media/layer/effect/marker counts, edits and
  undoes its stack, then plays across cuts. Native result pending.

Native iOS execution and the two previous UI failures (curve-panel bounds and
material preset reachability); axis manipulation and cancellation at different
zoom levels; native multiple-key selection/copy/move acceptance; explicit auto-key behaviour;
effect-stack operations and numeric ranges; parenting/camera/remap and real motion
blur checks; a complete 8–12-clip manual editing workflow using the same project
on both platforms. Existing tools must be exercised before adding replacements.

## Follow-up validation

- Single-effect copying now preserves only that effect and its animation on both
  platform bridges. Locked paste targets are skipped without creating an undo
  entry. Core clipboard regressions passed: 7 tests / 122 checks.
- Android's integrated workflow passed again with single-effect copy/paste/undo.
  Adding an effect now scrolls its card into view, including long stacks.
- The portable Android project passed headless asset/layer/effect checks (1 test,
  8 checks). Native playback of this same project is still awaiting execution.
- Native run 36322789798 failed to compile a complex Swift Speed Graph expression.
  Replaced it with an explicitly typed loop; a new native build is required.
- Follow-up run 36323431150 compiled and packaged successfully (04f26a85).
  All three requested UI scenes captured, including transform. Native gesture
  results are still pending; this IPA is an intermediate build, not completion.
- Interrupted Android gizmo drags now close their undo group in a finally block.
  A real touch test leaves/re-enters the preview during an X drag, confirms Y/Z
  stay unchanged, and undoes a subsequent Y edit separately. Passed on emulator.
- The expanded Android workflow exported a real H.264 MP4: 16 seconds, 480x320,
  audio present, all planned frames encoded; decoded frames at 1/3/7/13 seconds
  were preserved and viewed. Output is synthetic noisy test media with overlapping
  test titles, not a finished AMV or evidence of artistic quality.

## Behaviour references consulted

- [Alight Motion easing](https://support.alightmotion.com/hc/en-us/articles/10536934703889-Animation-Easing-Curves): curves are edited between neighbouring keys and can be copied to other segments.
- [Alight Motion parenting](https://support.alightmotion.com/hc/en-us/articles/10536997444369-Layer-Parenting-and-Null-Objects): assigning a parent compensates the current placement.
- [Alight Motion effect copying](https://support.alightmotion.com/hc/en-us/articles/13725250940689-How-do-I-copy-and-paste-effects): effect copying/pasting belongs to the layer's effects workflow.
- [Node Video quick start](https://nodevideo.com/guide/quick-start): parameter-level keyframe access, numeric entry, long-press reordering, and preview quality independent of export.
- [Node Video time tools](https://www.nodevideo.com/guide/video-speed): stretch, time remapping and speed ramps are different operations.
- [CapCut velocity workflow](https://www.capcut.com/resource/how-to-do-velocity-on-capcut): custom speed points vary timing and playback rate.
- [Adobe 3D gizmo](https://helpx.adobe.com/after-effects/desktop/work-with-3d-composition/work-in-3d-design-space/3d-transform-gizmo.html): axis-constrained manipulation and orientation modes.

These are documented workflow references, not evidence of having interactively
tested the other applications or copied their implementation.
