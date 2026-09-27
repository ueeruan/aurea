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
- iOS compile/package passed at 084592bd. The first simulator dispatch used
  invalid capture scene names and did not execute gestures; corrected dispatch
  is run 36319772180. This is not a native gesture pass.
- iOS native regressions added for body reorder, isolated easing and graph
  time/value dragging. Native execution is still required.
- This does not validate a physical Samsung A51 5G or sustained iPhone workload.

## Acceptance still to complete

Native iOS execution and the two previous UI failures (curve-panel bounds and
material preset reachability); axis manipulation and cancellation at different
zoom levels; multiple-key selection/copy/move; explicit auto-key behaviour;
effect-stack operations and numeric ranges; parenting/camera/remap and real motion
blur checks; a complete 8–12-clip manual editing workflow using the same project
on both platforms. Existing tools must be exercised before adding replacements.

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
