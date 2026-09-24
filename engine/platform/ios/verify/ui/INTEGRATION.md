# Native gesture UI tests

Status: integrated into the Xcode project and shared `Aurea-Gestures` scheme.
Run `35914411601` (commit `ae4ce16`) compiled and executed all four native
gesture tests: zero failures in 43.4 seconds. Results are retained in
`build/ios-gestures/35914411601`. This result applies to that revision;
later code changes require their own execution. Static project/API checks
alone do not establish that the gesture tests pass.

`AureaStageGestureUITests.swift` launches the real `com.aurea.aurea` application
through XCTest and drives UIKit touches. The existing DEBUG
`AUREA_PARITY_SCENE=transform` / `shape-edit` paths create the shape with the C++
engine. Tests do not import the app module, call engine commands, replace a
renderer, or call the gesture coordinator directly.

The ID `aurea.parity.stage` is a DEBUG diagnostic contract implemented by
`app/ParityGestureProbe.swift`. It is enabled only by UI-test launch environment,
not a product UI label. The test
uses the actual existing `Undo` label: `ToolbarView.swift` passes
`editor_desfazer` to `ShellBarButton`, and the parity fixture sets `language = .en`.

## App integration

1. `app/ParityGestureProbe.swift` belongs to **Aurea Sources only**.
   It compiles to nothing in Release. It is inactive in Debug unless both
   `AUREA_UI_TEST_PROBE=1` and a nonempty `AUREA_UI_TEST_RUN_ID` are provided.
2. `PreviewMetalView.makeUIView` retains the probe through its coordinator after
   installing the existing gesture recognizers:

   ```swift
   #if DEBUG
   context.coordinator.parityProbe = ParityGestureProbe(preview: view, model: model)
   #endif
   ```

   The probe sets accessibility metadata on the actual preview view. Its frame
   remains UIKit's live view frame. No added view intercepts touches, changes
   layout, or alters gesture handling. A 5 Hz DEBUG timer reads the selected
   layer through `engine.layerDetail` and its dimensions through
   `engine.shapeParams`. It exposes the JSON in `accessibilityValue`.
3. The final `report` dictionary written by `AureaModel.prepareParityCapture`
   to `Documents/parity-ready.json` includes:

   ```swift
   "uiTestRunID": ProcessInfo.processInfo.environment["AUREA_UI_TEST_RUN_ID"] ?? "",
   ```

   Each test launch passes a unique UUID. Readiness requires both that UUID and
   the requested scene in the file. A stale file from an earlier launch cannot
   make a test start before the current core fixture has been created. Normal
   screenshot capture continues to work with an empty token.

The probe is read-only. It must not gain buttons or commands that set position,
resize shapes, perform undo, or bypass the real UI gesture path. Do not substitute
expected fixture coordinates for values returned by the engine.

## Xcode project integration

`Aurea.xcodeproj` contains the application target and a standard iOS UI Testing
Bundle target named `AureaUITests`:

| Setting or project object | Value |
| --- | --- |
| Product type | `com.apple.product-type.bundle.ui-testing` |
| Product | `AureaUITests.xctest` |
| Source membership | `verify/ui/AureaStageGestureUITests.swift` only |
| Target Application / `TEST_TARGET_NAME` | `Aurea` |
| `PRODUCT_BUNDLE_IDENTIFIER` | `com.aurea.aurea.uitests` |
| `GENERATE_INFOPLIST_FILE` | `YES` |
| `IPHONEOS_DEPLOYMENT_TARGET` | `16.3`, matching the app |
| `SWIFT_VERSION` | `5.0`, matching the app |
| Simulator signing | `CODE_SIGNING_ALLOWED=NO` at invocation |
| Dependency | Aurea application target |
| Framework | XCTest via the UI-test bundle SDK |

The UI-test target uses a normal target dependency/proxy and product reference.
It is not a unit-test bundle and does not set a custom `TEST_HOST`.
The UI-test runner must not inherit Aurea's CMake core build phase or link its
static engine libraries. Only the app builds/links those.

The shared `Aurea-Gestures` scheme builds Aurea and AureaUITests and
contains AureaUITests as its Debug TestableReference. Tests run serially
(`parallelizable="NO"` and `-parallel-testing-enabled NO`) to avoid sharing one
simulator's project/preferences across runners. The separate shared `Aurea`
scheme builds only the application, preserving the existing IPA invocation.

After the real core/simulator startup is working, invoke from the repository root:

```sh
xcodebuild test \
  -project engine/platform/ios/Aurea.xcodeproj \
  -scheme Aurea-Gestures \
  -configuration Debug \
  -destination "platform=iOS Simulator,id=$SIMULATOR_UDID" \
  -parallel-testing-enabled NO \
  -only-testing:AureaUITests/AureaStageGestureUITests \
  -resultBundlePath build/ios-gesture-tests.xcresult \
  CODE_SIGNING_ALLOWED=NO
```

`SIMULATOR_UDID` must identify the same dedicated, booted iPhone used for native
parity capture. Run the test command sequentially with the capture script, not
against an app process the script is concurrently resetting. Upload `.xcresult`
on failure as well as success. It includes the final screenshot, accessibility
tree and core probe snapshots from every case.

## Assertions and remaining coverage

- A tap on the actual layer center leaves position, scale and rotation unchanged
  through at least three probe updates.
- A body drag exceeding Android's 18 point slop changes core position, leaves
  scale/rotation alone, and one tap on the existing Undo control restores it.
- A genuine two-touch XCTest pinch changes both scale axes proportionally,
  leaves position unchanged, and one Undo restores the original transform.
- Dragging shape handle 5 (right midpoint, taken from core corners) changes
  `shapeParams[5]` and `sourceSize[0]`, leaves layer scale/position alone, and one
  Undo restores geometry. This distinguishes silhouette resizing from scaling.

All endpoints are computed from the real stage frame, composition dimensions
and core corners; the tests fail with diagnostics if the fixture no longer fits.
They use the default unchanged linked-size state. They do not mutate fixture
dimensions or disable snapping through a test-only shortcut.

XCTest documents pinch scale as best effort, so the assertion checks a meaningful
increase and preserved proportions, not an exact factor of 1.35. The tests do
not prove Android pixel parity, GPU image correctness, snapping hysteresis,
rotation dead-zone thresholds, corner-radius handles, or the behavior of one
finger remaining after a pinch. Those need separate bounded tests or a real
device gesture bench; do not report them covered by these four tests.

Apple API references: [XCUIElement gestures](https://developer.apple.com/documentation/xcuiautomation/xcuielement),
[pinch(withScale:velocity:)](https://developer.apple.com/documentation/xcuiautomation/xcuielement/pinch%28withscale%3Avelocity%3A%29),
[XCUICoordinate press/drag](https://developer.apple.com/documentation/xctest/xcuicoordinate/1615002-press).
