# Timeline expansion, 2026-09-25

Android and iOS now expand a layer by tapping its header chevron, independently of selection.
The existing clip row remains, followed by a Transform entry, real effect-stack
entries, and one lane per actual animated engine track (including text animation,
particles, effect parameters, time remap, and 3D properties). Fold state is UI-only;
no synthetic keyframes or project data are created. Opening a section uses the
existing property panel; tapping a diamond seeks to its real time and uses the
existing keyframe editor. Each child diamond/drag addresses only that track,
while the parent row preserves aggregate instant editing.

Child rows cannot trim, move, reorder, lock, or toggle visibility of their parent.
Reorder targets map expanded display indices back to layer indices. Media tiles
and waveforms are not requested for property children. Android derived state and
iOS revision caches avoid regrouping animation data on every playback frame.

Validation: coordinated Android Kotlin build and existing unit suite passed.
TimelineExpansionTest adds identity/time-offset/isolation/folding coverage and is
pending the next unit run. iOS scope, symbols, pbxproj, and API/catalog checks pass;
Windows has no native Swift/iOS compiler, so device/UI execution is still required.

Emulator validation caught a real integration problem: selecting on the chevron
opened the layer dock, shrank the timeline, and clearing selection hid children
while leaving the down-arrow. Chevron now only expands/folds on both platforms;
expansion survives selection changes. Effect snapshots target the expanded layer
and are cached per model revision, rather than using the primary selection stack.
Android compilation and emulator retest passed. Final screenshot:
engine/build/android-p0/timeline-expanded-final.png shows Transform and the real
RGB effect beneath the video, without opening the dock. Tapping the effect row
opens the existing Effects panel. Final JVM suite: 90 tests, zero failures,
including TimelineExpansionTest. Native iOS execution remains pending.
