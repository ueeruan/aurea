// Native UI-test target source. Simulator execution is required to validate it.
// These tests operate the launched application; they never link/call its engine.
import XCTest

@MainActor final class AureaStageGestureUITests: XCTestCase {
    private var app: XCUIApplication!
    private var runID = ""
    private var stage: XCUIElement { app.otherElements["aurea.parity.stage"].firstMatch }

    override func setUpWithError() throws {
        continueAfterFailure = false
        XCUIDevice.shared.orientation = .portrait
    }

    override func tearDownWithError() throws {
        if let app, app.state == .runningForeground {
            let screenshot = XCTAttachment(screenshot: app.screenshot())
            screenshot.name = "Final application screen"
            screenshot.lifetime = .keepAlways
            add(screenshot)
            attach("Final accessibility tree", app.debugDescription)
            if let value = stage.value as? String { attach("Final core probe", value) }
            app.terminate()
        }
        app = nil
    }

    func testDockMoveVideoAtTwoSecondsKeepsAppResponsiveAndPreservesDuration() throws {
        let before = try launch("video-move")
        XCTAssertEqual(before.detail.kind, 1)
        XCTAssertEqual(before.corePlayhead, 60)
        XCTAssertEqual(before.detail.startFrame, 0)
        let duration = before.detail.endFrame - before.detail.startFrame
        XCTAssertGreaterThan(duration, 0)
        let move = app.descendants(matching: .any).matching(identifier: "Pull the layer to the playhead").firstMatch
        XCTAssertTrue(move.waitForExistence(timeout: 5))
        XCTAssertTrue(move.isHittable)
        move.tap()
        let after = try awaitSnapshot("Video moved to the two-second playhead", timeout: 10) {
            $0.detail.startFrame == 60 && $0.detail.localPlayhead == 0 && $0.corePlayhead == 60
        }
        XCTAssertEqual(after.detail.endFrame - after.detail.startFrame, duration)
        XCTAssertEqual(after.primaryID, before.primaryID)
        try undo()
        let undone = try awaitSnapshot("Undo remains responsive after moving decoded video", timeout: 10) {
            $0.detail.startFrame == before.detail.startFrame && $0.detail.endFrame == before.detail.endFrame
        }
        XCTAssertEqual(undone.corePlayhead, 60)
        XCTAssertEqual(app.state, .runningForeground)
    }

    func testAddingEffectClosesBrowserAndShowsAppliedCard() throws {
        _ = try launch("effects")
        let add = app.buttons["aurea.effects.add"].firstMatch
        XCTAssertTrue(add.waitForExistence(timeout: 5))
        XCTAssertEqual(add.label, "Add effect")
        XCTAssertTrue(add.isHittable)
        add.tap()
        let search = app.textFields.firstMatch
        XCTAssertTrue(search.waitForExistence(timeout: 5))
        search.tap(); search.typeText("Deep Glow")
        let tile = app.buttons["Ver Deep Glow"].firstMatch
        XCTAssertTrue(tile.waitForExistence(timeout: 5))
        tile.tap()
        let apply = app.buttons["Add to the selection"].firstMatch
        XCTAssertTrue(apply.waitForExistence(timeout: 5))
        if !apply.isHittable { app.swipeUp() }
        apply.tap()
        let dismissed = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"), object: search)
        XCTAssertEqual(XCTWaiter.wait(for: [dismissed], timeout: 8), .completed)
        XCTAssertTrue(app.staticTexts["Deep Glow"].firstMatch.waitForExistence(timeout: 5))
        // New effects expand their real parameter card. The Add button is its
        // scroll footer, so reveal it before checking that the modal is gone.
        let stack = app.scrollViews["aurea.effects.stack"].firstMatch
        XCTAssertTrue(stack.exists)
        for _ in 0..<8 {
            if add.isHittable { break }
            stack.swipeUp()
        }
        XCTAssertTrue(add.isHittable)
        // A second interaction proves the editor did not remain under a stale modal.
        try undo()
        let removed = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"), object: app.staticTexts["Deep Glow"].firstMatch)
        XCTAssertEqual(XCTWaiter.wait(for: [removed], timeout: 5), .completed)
    }

    func testFloatingAddKeepsPreviewStableAndClosesAfterSelection() throws {
        let before = try launch("layer-dock")
        let frame = stage.frame
        let add = app.buttons["Add layer"].firstMatch
        XCTAssertTrue(add.waitForExistence(timeout: 5)); add.tap()
        let category = app.buttons["aurea.add.category.0"].firstMatch
        XCTAssertTrue(category.waitForExistence(timeout: 5))
        XCTAssertEqual(stage.frame.height, frame.height, accuracy: 1)
        let bubbles = XCTAttachment(screenshot: app.screenshot())
        bubbles.name = "Floating add categories"; bubbles.lifetime = .keepAlways; self.add(bubbles)
        category.tap()
        let circle = app.buttons["Circle"].firstMatch
        XCTAssertTrue(circle.waitForExistence(timeout: 5))
        let dialog = XCTAttachment(screenshot: app.screenshot())
        dialog.name = "Centered shape dialog"; dialog.lifetime = .keepAlways; self.add(dialog)
        circle.tap()
        _ = try awaitSnapshot("Shape added and editor responds") { $0.layerCount == before.layerCount + 1 }
        XCTAssertFalse(category.exists)
        XCTAssertFalse(circle.exists)
        XCTAssertEqual(stage.frame.height, frame.height, accuracy: 1)
        try undo()
        _ = try awaitSnapshot("Undo added shape") { $0.layerCount == before.layerCount }
    }

    func testShortTapDoesNotMoveScaleOrRotateLayer() throws {
        let before = try launch("transform")
        coordinate(bodyCenter(before)).tap()
        // Observe a full probe refresh window, not an immediate stale value.
        let deadline = Date().addingTimeInterval(0.6)
        repeat {
            let after = try snapshot()
            assertTransform(after, equals: before)
            XCTAssertEqual(after.primaryID, before.primaryID)
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
    }

    func testBodyDragChangesPositionAndOneUndoRestoresIt() throws {
        let before = try launch("transform")
        let start = bodyCenter(before)
        let end = CGPoint(x: start.x + 54, y: start.y + 28)
        try requireInsideStage(start, end)
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(end),
                                withVelocity: .slow, thenHoldForDuration: 0.1)
        let moved = try awaitSnapshot("Drag changes core position") {
            !$0.isManipulating && self.distance($0.detail.position, before.detail.position) > 1
        }
        XCTAssertEqual(moved.primaryID, before.primaryID)
        assertVector(moved.detail.scale, equals: before.detail.scale)
        assertVector(moved.detail.rotation, equals: before.detail.rotation)
        try undo()
        let restored = try awaitSnapshot("One undo restores the drag") { self.sameTransform($0, before) && $0.canRedo }
        assertTransform(restored, equals: before)
    }

    func testPinchChangesScaleWithoutTranslationAndOneUndoRestoresIt() throws {
        let before = try launch("transform")
        // XCUIElement.pinch centers its two touches on the actual preview.
        // The core-created transform fixture must cover that midpoint.
        let center = bodyCenter(before)
        XCTAssertLessThan(hypot(center.x - stage.frame.midX, center.y - stage.frame.midY), 12)
        stage.pinch(withScale: 1.35, velocity: 0.6)
        let scaled = try awaitSnapshot("Pinch changes core scale") {
            !$0.isManipulating && abs($0.detail.scale[0] - before.detail.scale[0]) > 0.02
        }
        assertVector(scaled.detail.position, equals: before.detail.position)
        XCTAssertGreaterThan(scaled.detail.scale[0], before.detail.scale[0])
        XCTAssertEqual(scaled.detail.scale[0] / before.detail.scale[0],
                       scaled.detail.scale[1] / before.detail.scale[1], accuracy: 0.005)
        // XCTest synthesizes scale on a best-effort basis; do not require 1.35 exactly.
        try undo()
        _ = try awaitSnapshot("One undo restores scale and rotation") { self.sameTransform($0, before) && $0.canRedo }
    }

    func testShapeRightHandleChangesWidthAndOneUndoRestoresGeometry() throws {
        let before = try launch("shape-edit")
        XCTAssertGreaterThanOrEqual(before.shapeParams.count, 7)
        let c = before.detail.corners
        // ShapeEditStage.kt handle 5: midpoint of TR/BR, in composition pixels.
        let start = screenPoint(x: (c[2] + c[4]) / 2, y: (c[3] + c[5]) / 2, snapshot: before)
        let end = CGPoint(x: start.x + 36, y: start.y)
        try requireInsideStage(start, end)
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(end),
                                withVelocity: .slow, thenHoldForDuration: 0.1)
        let changed = try awaitSnapshot("Shape handle changes source width") {
            $0.shapeParams.count >= 7 && $0.shapeParams[5] > before.shapeParams[5] + 1
        }
        // This must resize the core silhouette, not merely scale its transform.
        assertVector(changed.detail.scale, equals: before.detail.scale)
        assertVector(changed.detail.position, equals: before.detail.position)
        XCTAssertGreaterThan(changed.detail.sourceSize[0], before.detail.sourceSize[0])
        try undo()
        let restored = try awaitSnapshot("One undo restores shape parameters") {
            $0.canRedo && self.sameVector($0.shapeParams, before.shapeParams)
        }
        assertVector(restored.detail.sourceSize, equals: before.detail.sourceSize)
        assertTransform(restored, equals: before)
    }

    func testTimelineDragChangesRealCorePlayheadAndKeepsLayerTransform() throws {
        let before = try launch("transform")
        let timeline = app.otherElements["aurea.parity.timeline"].firstMatch
        XCTAssertTrue(timeline.waitForExistence(timeout: 5))
        XCTAssertTrue(timeline.isHittable)
        // Drag the ruler left. No direct seek/scrub engine calls in this test.
        let start = CGPoint(x: timeline.frame.midX + 70, y: timeline.frame.minY + 14)
        let end = CGPoint(x: start.x - 90, y: start.y)
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(end),
                                withVelocity: .slow, thenHoldForDuration: 0.1)
        let moved = try awaitSnapshot("Timeline drag reaches a nonzero frame in the real C++ clock") {
            $0.corePlayhead > before.corePlayhead + 5 && $0.playhead == $0.corePlayhead
        }
        assertTransform(moved, equals: before)
        XCTAssertEqual(moved.primaryID, before.primaryID)
        // An optimistic UI value must not snap back to zero on the next core poll.
        let deadline = Date().addingTimeInterval(0.8)
        repeat {
            let current = try snapshot()
            XCTAssertEqual(current.corePlayhead, moved.corePlayhead)
            XCTAssertEqual(current.playhead, current.corePlayhead)
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
    }

    func testText3DSelectionFollowsCoreBoundsAndGizmoDrag() throws {
        let before = try launch("text-3d")
        // Model3D may have no projected core corners. Its displayed selection
        // must still enclose the real silhouette around the model pivot.
        // Read dimensions from the core; font/platform metrics are not fixed.
        try assertText3DSelectionBounds(before)
        XCTAssertEqual(before.stageGizmo.count, 8)
        let start = screenPoint(x: before.stageGizmo[2], y: before.stageGizmo[3], snapshot: before)
        let end = CGPoint(x: start.x + 40, y: start.y)
        try requireInsideStage(start, end)
        // Touch the actual X-axis tip, avoiding text holes and overlapping
        // rotation/scale handles. The application performs the engine command.
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(end),
                                withVelocity: .slow, thenHoldForDuration: 0.1)
        let moved = try awaitSnapshot("3D gizmo changes the real layer position") {
            !$0.isManipulating && $0.detail.position[0] > before.detail.position[0] + 1
        }
        XCTAssertEqual(moved.primaryID, before.primaryID)
        XCTAssertEqual(moved.detail.position[1], before.detail.position[1], accuracy: 0.01)
        XCTAssertEqual(moved.detail.position[2], before.detail.position[2], accuracy: 0.01)
        assertVector(moved.detail.scale, equals: before.detail.scale)
        assertVector(moved.detail.rotation, equals: before.detail.rotation)
        try assertText3DSelectionBounds(moved)
        try undo()
        let restored = try awaitSnapshot("One undo restores the 3D gizmo move") { self.sameTransform($0, before) && $0.canRedo }
        try assertText3DSelectionBounds(restored)
    }

    private func assertText3DSelectionBounds(_ state: Snapshot) throws {
        let c = state.stageCorners
        guard c.count == 8, state.detail.anchor.count == 3 else {
            XCTFail("The actual stage geometry was not exposed by the Debug probe")
            throw ProbeError.geometry
        }
        assertVector(state.detail.rotation, equals: [0, 0, 0])
        let xs = stride(from: 0, to: 8, by: 2).map { c[$0] }
        let ys = stride(from: 1, to: 8, by: 2).map { c[$0] }
        let width = (xs.max() ?? 0) - (xs.min() ?? 0)
        let height = (ys.max() ?? 0) - (ys.min() ?? 0)
        XCTAssertGreaterThan(width, 0)
        XCTAssertGreaterThan(height, 0)
        XCTAssertEqual(width, state.detail.sourceSize[0] * abs(state.detail.scale[0]), accuracy: 0.01)
        XCTAssertEqual(height, state.detail.sourceSize[1] * abs(state.detail.scale[1]), accuracy: 0.01)
        XCTAssertEqual(xs.reduce(0, +) / 4,
                       state.detail.position[0] - state.detail.anchor[0] * state.detail.scale[0], accuracy: 0.01)
        XCTAssertEqual(ys.reduce(0, +) / 4,
                       state.detail.position[1] - state.detail.anchor[1] * state.detail.scale[1], accuracy: 0.01)
        for i in 0..<4 {
            let point = screenPoint(x: c[i * 2], y: c[i * 2 + 1], snapshot: state)
            try requireInsideStage(point)
        }
    }

    private func launch(_ scene: String) throws -> Snapshot {
        runID = UUID().uuidString
        app = XCUIApplication(bundleIdentifier: "com.aurea.aurea")
        app.launchEnvironment["AUREA_PARITY_SCENE"] = scene
        app.launchEnvironment["AUREA_UI_TEST_PROBE"] = "1"
        app.launchEnvironment["AUREA_UI_TEST_RUN_ID"] = runID
        if scene == "video-move" { app.launchEnvironment["AUREA_PARITY_EXPORT"] = "1" }
        let preparationTimeout: TimeInterval = scene == "video-move" ? 120 : 30
        app.launch()
        guard stage.waitForExistence(timeout: preparationTimeout) else {
            XCTFail("Read-only DEBUG preview probe is missing; inspect the app configuration and INTEGRATION.md")
            throw ProbeError.missing
        }
        let state = try awaitSnapshot("Core fixture ready for \(scene)", timeout: preparationTimeout) {
            $0.ready && $0.scene == scene && $0.runID == self.runID
        }
        XCTAssertTrue(state.coreStarted, state.coreError)
        XCTAssertEqual(state.layerCount, 1)
        XCTAssertEqual(state.selectionCount, 1)
        XCTAssertGreaterThan(state.primaryID, 0)
        XCTAssertEqual(state.detail.position.count, 3)
        XCTAssertEqual(state.detail.scale.count, 3)
        XCTAssertEqual(state.detail.rotation.count, 3)
        XCTAssertEqual(state.detail.corners.count, 8)
        XCTAssertEqual(state.detail.sourceSize.count, 2)
        XCTAssertTrue(stage.isHittable)
        attach("Initial core probe", stage.value as? String ?? "missing")
        return state
    }

    private func snapshot() throws -> Snapshot {
        guard let json = stage.value as? String, let data = json.data(using: .utf8) else { throw ProbeError.missing }
        return try JSONDecoder().decode(Snapshot.self, from: data)
    }

    private func awaitSnapshot(_ description: String, timeout: TimeInterval = 6,
                               matching predicate: @MainActor (Snapshot) -> Bool) throws -> Snapshot {
        let deadline = Date().addingTimeInterval(timeout)
        repeat {
            if stage.exists, let value = try? snapshot(), predicate(value) { return value }
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
        attach("Timed-out probe: \(description)", stage.value as? String ?? "missing")
        XCTFail(description)
        throw ProbeError.timedOut
    }

    private func undo() throws {
        // Existing source: ToolbarView -> editor_desfazer; parity setup forces .en.
        let undo = app.buttons["Undo"].firstMatch
        guard undo.waitForExistence(timeout: 5), undo.isEnabled, undo.isHittable else {
            XCTFail("The existing Undo transport control is not available")
            throw ProbeError.missing
        }
        undo.tap()
    }

    private func coordinate(_ point: CGPoint) -> XCUICoordinate {
        app.coordinate(withNormalizedOffset: .zero).withOffset(CGVector(dx: point.x - app.frame.minX, dy: point.y - app.frame.minY))
    }

    private func bodyCenter(_ state: Snapshot) -> CGPoint {
        let c = state.detail.corners
        return screenPoint(x: (c[0] + c[2] + c[4] + c[6]) / 4,
                           y: (c[1] + c[3] + c[5] + c[7]) / 4, snapshot: state)
    }

    private func screenPoint(x: Double, y: Double, snapshot s: Snapshot) -> CGPoint {
        let frame = stage.frame
        let fit = max(frame.width / CGFloat(s.compositionWidth), frame.height / CGFloat(s.compositionHeight))
        return CGPoint(x: frame.midX + CGFloat(x - s.compositionWidth / 2) * fit,
                       y: frame.midY + CGFloat(y - s.compositionHeight / 2) * fit)
    }

    private func requireInsideStage(_ points: CGPoint...) throws {
        guard points.allSatisfy({ stage.frame.insetBy(dx: 8, dy: 8).contains($0) }) else {
            XCTFail("Fixture gesture endpoints are outside the real preview; inspect its frame")
            throw ProbeError.geometry
        }
    }

    private func distance(_ a: [Double], _ b: [Double]) -> Double {
        guard a.count == b.count else { return .infinity }
        return sqrt(zip(a, b).reduce(0) { $0 + pow($1.0 - $1.1, 2) })
    }

    private func sameVector(_ a: [Double], _ b: [Double]) -> Bool {
        a.count == b.count && zip(a, b).allSatisfy { abs($0.0 - $0.1) <= 0.01 }
    }

    private func sameTransform(_ a: Snapshot, _ b: Snapshot) -> Bool {
        sameVector(a.detail.position, b.detail.position) && sameVector(a.detail.scale, b.detail.scale) && sameVector(a.detail.rotation, b.detail.rotation)
    }

    private func assertVector(_ a: [Double], equals b: [Double], file: StaticString = #filePath, line: UInt = #line) {
        XCTAssertEqual(a.count, b.count, file: file, line: line)
        for (first, second) in zip(a, b) { XCTAssertEqual(first, second, accuracy: 0.01, file: file, line: line) }
    }

    private func assertTransform(_ a: Snapshot, equals b: Snapshot, file: StaticString = #filePath, line: UInt = #line) {
        assertVector(a.detail.position, equals: b.detail.position, file: file, line: line)
        assertVector(a.detail.scale, equals: b.detail.scale, file: file, line: line)
        assertVector(a.detail.rotation, equals: b.detail.rotation, file: file, line: line)
    }

    private func attach(_ name: String, _ value: String) {
        let attachment = XCTAttachment(string: value)
        attachment.name = name
        attachment.lifetime = .keepAlways
        add(attachment)
    }

    private enum ProbeError: Error { case missing, timedOut, geometry }
    private struct Snapshot: Decodable {
        let runID: String
        let scene: String
        let ready: Bool
        let coreStarted: Bool
        let coreError: String
        let layerCount: Int
        let primaryID: Int64
        let selectionCount: Int
        let isManipulating: Bool
        let canRedo: Bool
        let playhead: Int64
        let corePlayhead: Int64
        let compositionWidth: Double
        let compositionHeight: Double
        let detail: Detail
        let shapeParams: [Double]
        let stageCorners: [Double]
        let stageGizmo: [Double]
    }
    private struct Detail: Decodable {
        let kind: UInt32
        let startFrame: Int64
        let endFrame: Int64
        let localPlayhead: Int64
        let position: [Double]
        let scale: [Double]
        let rotation: [Double]
        let corners: [Double]
        let sourceSize: [Double]
        let anchor: [Double]
    }
}
