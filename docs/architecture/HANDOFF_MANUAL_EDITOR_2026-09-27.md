# Handoff — Aurea manual editor, 27 September 2026

User explicitly requested this handoff because the session is ending. Do not
mark the complete professional-editor prompt finished. No goal/automation was
created. No agents are running. Broad authorization to implement, publish this
branch and run CI was already given; do not ask for the same permission again.

## Resume here

- Workspace: `C:/Users/Ruan/Documents/Aureabeta` (PowerShell).
- Branch: `codex/build-2112-timeline`.
- HEAD and origin: `ce957a2f`. All implementation changes from this continuation
  are committed and pushed. This handoff file itself is newly added locally.
- Full current user prompt:
  `C:/Users/Ruan/.codex/attachments/c3fe4352-bc0d-4353-b281-398a96c70e8e/Texto colado.txt`.
- Main implementation/evidence checkpoint: `docs/architecture/MANUAL_EDITOR_2026-09-27.md`.
- No AGENTS.md was found earlier. ui-ux-pro-max skill was read/applied earlier.
- There are unrelated dirty files, pycache deletions, discovery worker changes,
  old notes and user sample assets. Do NOT reset, clean, broadly stage or overwrite.
- User allows emulator use. Keep one emulator, `emulator-5554` / `Aurea_API35`.
  NEVER uninstall/clear `com.aurea.aurea.debug` (user projects). Disposable test
  package is `com.aurea.aurea.uitest` and its instrumentation `.uitest.test`.

## Immediate next action: finish the native result

Run https://github.com/ueeruan/aurea/actions/runs/36323431150 is still running.
It tests commit `04f26a85`; later commits only change Android/tests/docs.

- `build-ipa`: SUCCESS. Native app, Metal validation, packaging passed.
- `simulator-parity`: three actual captures succeeded; currently running
  **Run native preview gestures and undo tests** (31 tests in source).
- Do NOT dispatch another workflow before this finishes: concurrency cancels
  the previous run. Pushing to this branch alone does not dispatch it.
- A local `gh run watch` process session 84386 may still be active, writing
  `build/ios-04f26a85-watch.log`. Query GitHub directly if session is unavailable.
- Follow results, download `ios-gesture-screenshots` first (compact) and inspect
  `build-gesture-tests.log`. The large artifact is `ios-gesture-tests`.
- Previously f15ef224 passed 26/27 native tests. Only compact curve accessibility
  bounds failed: 313pt vs maximum281pt. Explicit clipping of curve root and
  scroll content was added in b0f6200d and is awaiting this run's result.
- Previous b0f6200d run 36322789798 failed Swift compilation of the long
  speedHandles flatMap. Replaced with explicitly typed loop in 04f26a85. This
  run successfully compiled, so that compile failure is resolved.
- Earlier transform capture timed out; this run captured transform, text-3d
  and layer-dock successfully. Do not describe the old timeout as an app crash.

## Recent commits

- `5d10cebf`: Auto-Key toggle both OS; editable interval Speed Graph tangents;
  independent spatial RGB Split + Chromatic Aberration GPU effects; manual remap entry.
- `aaca602c`: gizmo only writes changed XYZ components, preserving untouched keys.
- `5a0dedeb`, `8078f5fc`: portable Android project/media fixture and native test
  loading the exact same binary project, with asset/count checks and playback.
- `b0f6200d`: animated anchor sampled by projected gizmo, compact curve clipping.
- `04f26a85`: single-effect copy with only its animation, locked paste targets
  skipped, native Speed Graph compiler fix, portable fixture core test.
- `ee08caa8`: Android gizmo drag finally closes undo group on cancellation;
  actual touch/lifecycle/undo regression.
- `ce957a2f`: Android real export acceptance and captured decoded frames;
  effect card scroll state recorded before suspending animation.

Earlier current-branch work includes body hold/reorder both OS, scroll versus
drag arbitration, constant-screen-size World/Local gizmo, isolated easing,
editable value graphs, multi-key copy/move/delete/duplicate and snap toggle.

## Proof from this continuation

- Clipboard core: 7 tests / 122 checks passed (single effect animation,
  invalid copy preserves clipboard, locked paste skipped, undo).
- Portable Android fixture core: 1 test / 8 checks passed; no missing assets,
  14 layers, 9 video layers after split, 6 effects, 2 parent links.
- Android `ManualEditingWorkflowTest`: passed with 8 imported clips + music,
  16 toolbar markers, split, null parenting, animated null/camera, 6 effects,
  individual copy/paste/undo, 2D/3D text editor, remap, playback past 6s,
  save/reopen, then real H.264 export with audio. Final run: 47.05 seconds.
  MP4 is 16s, 480x320, all expected frames encoded; frames at1/3/7/13s decode.
- `GizmoCancellationTest`: actual diagonal X-handle drag changes only X;
  remove/recreate stage mid-touch, edit Y, undo Y then undo X separately. Passed.
  Recreate preview before undo: engine commands need the active render surface;
  an earlier test variant without it timed out rather than proving undo failure.
- Extreme effect GPU regression passed: 75 effects, 450 frames, 532 checks,
  zero failed frames/bypasses. Worst318ms was extreme text3D layout; not a
  promise of realtime performance at extreme values. Log `build/effect-extreme-gpu.log`.
- Prior relevant proofs retained:132 JVM tests; Android timeline6; graph3;
  snapping1; core gizmo3/44; parenting6/750; ClipTime19/1206;
  motionblur GPU4/121; RGB spatial GPU2/6167.
- Physical Samsung A51 Android12 and sustained user iPhone acceptance NOT done.

## Artifacts and files

- Intermediate unsigned IPA:
  `build/ios-04f26a85-ipa/aurea-beta2-unsigned.ipa`.
  Local check_ipa.py passed; version is still2123. Do not present as a completed
  new final release or as signed/App Store ready.
- iOS captures (viewed transform and text-3d):
  `build/ios-04f26a85-captures/build/ios-parity/`.
- Actual Android export: `build/manual-editing-acceptance.mp4` (8278895 bytes).
- Decoded frame contact sheet: `build/manual-editing-frames/contact.png` (viewed).
  Synthetic noisy video and intentionally overlapping test titles; this is
  technical integration evidence, NOT a finished real AMV or artistic acceptance.
- Export test log: `build/manual-export-device.log`.
- Cancellation test log: `build/gizmo-cancel-device.log`.
- Single effect workflow log: `build/single-effect-device.log`.
- Committed portable fixture: `engine/tests/data/manual-editing/` with SHA256
  manifest and verifier `tools/verify_manual_editing_fixture.py`.
- Test implementations:
  `android/app/src/androidTest/java/com/aurea/aurea/editor/ManualEditingWorkflowTest.kt`
  `android/app/src/androidTest/java/com/aurea/aurea/editor/GizmoCancellationTest.kt`
  `engine/platform/ios/verify/ui/AureaStageGestureUITests.swift`.

## Still incomplete — be explicit

1. Finish current native results and fix actual failures. New Auto-Key, speed
   tangents, spatial effects, independent gizmo axes and shared-project workflow
   have compiled, but latest native test acceptance is not known yet.
2. Broader touch acceptance of rotation/scale/pivot, zoom and parenting; current
   gizmo improvement is translation/local-world, not a new complete 3D ring system.
3. Multi-key UI selection is within the current graph track. Shared engine accepts
   cross-track references, but an explicit cross-property selection UI remains.
4. Speed tangent handles operate on nonflat supported Bézier/linear intervals.
   Flat/hold/bounce/elastic/steps use their existing easing tools; do not claim
   arbitrary velocity handles for those intervals.
5. Numeric entry still uses the declared engine hard ranges. A separate wider
   numeric-input versus slider-range contract was examined but NOT implemented.
   Do not blindly uncap memory/iteration/decoder-related parameters.
6. Full real-media 8–12 clip editing and final visual/export acceptance on both
   OS remains. Android integration uses synthetic clips and mixed real UI plus
   normal store APIs for import/numeric setup; native shared-project test pending.
7. Audit remaining camera/remap/parenting/performance requirements against the
   full prompt, without replacing architecture or claiming generic tests prove all.
8. Final APK32/APK64 + IPA + accurate release notes after completion/validation.
   No new final Android release APKs were generated in this continuation.

## Commands / pitfalls

Python: `C:/Users/Ruan/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`.
Set `PYTHONDONTWRITEBYTECODE=1` for repository verify scripts.
Java: `C:/Program Files/Eclipse Adoptium/jdk-21.0.12.101-hotspot`.
ADB: `C:/Users/Ruan/AppData/Local/Packages/Claude_pzs8sxrjxfjjc/LocalCache/Local/Android/Sdk/platform-tools/adb.exe`.

Gradle cwd `android`, JAVA_HOME above:
`./gradlew.bat :app:connectedUiTestAndroidTest -PaureaAbi=x86_64 '-Pandroid.testInstrumentationRunnerArguments.class=com.aurea.aurea.editor.GizmoCancellationTest' --console=plain`.
Quote the complete -Pclass option in PowerShell.
Gradle connected tests uninstall UITest afterward; to retain exports, install
APKs manually and use `adb shell am instrument -w -e class ... com.aurea.aurea.uitest.test/androidx.test.runner.AndroidJUnitRunner`.
APKs live in `build/android/app/outputs/apk/` (not android/app/build).
Use Python subprocess binary capture for `adb exec-out run-as ... cat` files.

Core build: `cmake --build engine/build/host --config Release --target aurea_tests -j 2`.
Core tests cwd `engine/build/host/tests`: `./Release/aurea_tests.exe FILTER`.
Filter is substring of suite OR test name, NOT `Suite.Test`; dotted filter can
silently execute zero tests. Always inspect counts.
PowerShell rg does not expand wildcard paths; use directory + `-g '*.kt'`.
Use `rg --files --no-ignore build/...` for ignored build artifacts.
GH ANSI log downloads require `--allow-escape-sequences`.

After current CI completes, only if necessary dispatch:
`gh workflow run build-ipa.yml --repo ueeruan/aurea --ref codex/build-2112-timeline -f run_simulator=true -f capture_scenes=transform,text-3d,layer-dock -f capture_only=false`.
