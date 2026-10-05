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

    func testMotionBlurCenterUsesDegreesAndOneUndoPreservesQuality() throws {
        let before = try launch("motion-blur-controls")
        let initial = try XCTUnwrap(before.motionBlurSettings)
        XCTAssertEqual(initial.count, 6)
        XCTAssertEqual(initial[1], 181, accuracy: 0.001)
        XCTAssertEqual(initial[2], 45, accuracy: 0.001)
        let shutter = app.descendants(matching: .any)["motionblur.shutter"].firstMatch
        XCTAssertTrue(shutter.waitForExistence(timeout: 5))
        XCTAssertTrue((shutter.value as? String ?? "").contains("°"))
        let advanced = app.buttons["motionblur.advanced"].firstMatch
        XCTAssertTrue(advanced.waitForExistence(timeout: 5)); advanced.tap()
        let center = app.buttons["motionblur.center"].firstMatch
        XCTAssertTrue(center.waitForExistence(timeout: 5))
        // A DisclosureGroup also exposes its label as a StaticText child.
        // Count actionable controls: expanded buttons must keep distinct IDs.
        for identifier in ["advanced", "phase", "center", "samples", "adaptive"] {
            XCTAssertEqual(app.buttons
                .matching(identifier: "motionblur.\(identifier)").count, 1, identifier)
        }
        if !center.isHittable { app.scrollViews.firstMatch.swipeUp() }
        center.tap()
        let changed = try awaitSnapshot("Center places the shutter around the frame") {
            ($0.motionBlurSettings?.count ?? 0) == 6 && abs($0.motionBlurSettings![2] + 90.5) < 0.001
        }
        XCTAssertEqual(changed.motionBlurSettings?[1], initial[1])
        XCTAssertEqual(Array(try XCTUnwrap(changed.motionBlurSettings).suffix(3)), Array(initial.suffix(3)))
        try undo()
        let restored = try awaitSnapshot("One undo restores the whole composition blur edit") {
            $0.motionBlurSettings == initial
        }
        XCTAssertEqual(restored.motionBlurSettings, initial)
    }

    func testMotionBlurTextExportCompletesAt1080p() throws {
        let snapshot = try launch("motion-blur-export")
        XCTAssertEqual(snapshot.compositionWidth, 1920)
        XCTAssertEqual(snapshot.compositionHeight, 1080)
        // Barra de cima (redesenho 2026-09-29): exportar mora na barra do PROJETO,
        // que aparece ao tirar a seleção.
        let back = app.buttons["Back (clear the selection)"].firstMatch
        XCTAssertTrue(back.waitForExistence(timeout: 5)); back.tap()
        let openExport = app.buttons["Export"].firstMatch
        XCTAssertTrue(openExport.waitForExistence(timeout: 5)); openExport.tap()
        let start = app.buttons["Export"].firstMatch
        XCTAssertTrue(start.waitForExistence(timeout: 10)); start.tap()
        // Fim do render: o app pede "Adicionar a Fotos" (PHPhotoLibrary .addOnly,
        // publishExportToPhotos) e só mostra "Vídeo pronto" depois da resposta.
        // O alerta é do SpringBoard; esperar o texto não o dispensa (CI 2136/2137:
        // render a 100% em ~10 s e o teste parado no alerta por 300 s).
        let ready = app.staticTexts["Video ready"]
        let allow = XCUIApplication(bundleIdentifier: "com.apple.springboard").buttons["Allow"].firstMatch
        let deadline = Date().addingTimeInterval(300)
        while !ready.exists && Date() < deadline {
            if allow.exists { allow.tap() }
            _ = ready.waitForExistence(timeout: 2)
        }
        XCTAssertTrue(ready.exists, "The export never reached the done screen")
        XCTAssertTrue(app.buttons["Open"].isEnabled)
    }

    func testTransformExpressionExplainsWhyPositionIsControlled() throws {
        let before = try launch("transform-expression")
        let notice = app.staticTexts["transform.expression.controlled"].firstMatch
        XCTAssertTrue(notice.waitForExistence(timeout: 5))
        XCTAssertTrue(notice.label.contains("expression"))
        XCTAssertEqual(before.detail.position[0], 540, accuracy: 0.01)
        XCTAssertEqual(before.detail.position[1], 515, accuracy: 0.01)
        let pad = app.descendants(matching: .any)["transform.move.pad"].firstMatch
        XCTAssertTrue(pad.waitForExistence(timeout: 5)); XCTAssertTrue(pad.isHittable)
        pad.coordinate(withNormalizedOffset: CGVector(dx: 0.4, dy: 0.5))
            .press(forDuration: 0.05, thenDragTo: pad.coordinate(withNormalizedOffset: CGVector(dx: 0.7, dy: 0.65)))
        let after = try snapshot()
        XCTAssertEqual(after.detail.position[0], 540, accuracy: 0.01)
        XCTAssertEqual(after.detail.position[1], 515, accuracy: 0.01)
        XCTAssertTrue(notice.exists)
        // The notice points to the existing expression editor; it must remain
        // possible to change/disable the expression instead of removing it on drag.
        let edit = app.buttons["Add expression"].firstMatch
        XCTAssertTrue(edit.waitForExistence(timeout: 5))
        edit.tap()
        XCTAssertTrue(app.staticTexts["[540, 515]"].exists || app.textViews.firstMatch.waitForExistence(timeout: 5))
    }

    func testPreviewBufferStatusAndPauseRemainAvailable() throws {
        _ = try launch("transform-expression")
        let play = app.buttons["Play · hold to repeat"].firstMatch
        XCTAssertTrue(play.waitForExistence(timeout: 5)); play.tap()
        let pause = app.buttons["Pause"].firstMatch
        XCTAssertTrue(pause.waitForExistence(timeout: 5))
        let buffer = app.descendants(matching: .any)["preview.buffer.status"].firstMatch
        XCTAssertTrue(buffer.waitForExistence(timeout: 5))
        XCTAssertFalse(buffer.label.isEmpty)
        let cached = try awaitSnapshot("The timeline receives real composed-frame ranges") {
            !($0.previewBufferRanges ?? []).isEmpty
        }
        let ranges = try XCTUnwrap(cached.previewBufferRanges)
        XCTAssertTrue(ranges.allSatisfy { $0.count == 2 && $0[0] >= 0 && $0[1] > $0[0] })
        XCTAssertLessThanOrEqual(ranges.reduce(Int64(0)) { $0 + $1[1] - $1[0] }, 30)
        XCTAssertTrue(app.staticTexts["timeline.preview.buffer"].firstMatch.waitForExistence(timeout: 5))
        pause.tap()
        let paused = try awaitSnapshot("Pause cancels playback or pending buffer startup") { $0.playing == 0 }
        // Give any queued preparation time to finish: it must not restart playback.
        RunLoop.current.run(until: Date().addingTimeInterval(1))
        let later = try snapshot()
        XCTAssertEqual(later.playing, 0)
        XCTAssertEqual(later.corePlayhead, paused.corePlayhead)
    }

    func testText3DAnimatorRailOpensTheSelectedWiggleCurve() throws {
        _ = try launch("animator-curve-rail")
        let curve = app.buttons["Edit the property curve"].firstMatch
        XCTAssertTrue(curve.waitForExistence(timeout: 5)); XCTAssertTrue(curve.isEnabled)
        curve.tap()
        _ = try awaitSnapshot("Animator rail opens Wiggle Y, not the layer transform") {
            $0.sheet == "curve" && $0.curveProperty == 40 && $0.curveParam == 13
        }
        let bounce = app.buttons["curve.preset.bounce"].firstMatch
        XCTAssertTrue(bounce.waitForExistence(timeout: 5)); bounce.tap()
        _ = try awaitSnapshot("Animator curve accepts bounce easing") {
            $0.curveKeys.contains { $0.property == 40 && $0.time == 0 && $0.interpolation == 7 }
        }
    }

    func testMaskOpacityOpensItsOwnAnimatedCurve() throws {
        _ = try launch("mask-animation")
        let border = app.buttons["3 Border"].firstMatch
        XCTAssertTrue(border.waitForExistence(timeout: 5)); border.tap()
        let row = app.descendants(matching: .any).matching(NSPredicate(format: "identifier MATCHES %@", "mask\\.[0-9]+\\.param\\.2")).firstMatch
        let scroll = app.scrollViews["mask.scroll"].firstMatch
        for _ in 0..<6 {
            if row.exists && row.isHittable { break }
            scroll.swipeUp()
        }
        XCTAssertTrue(row.isHittable)
        row.coordinate(withNormalizedOffset: CGVector(dx: 0.12, dy: 0.5)).tap()
        let curve = app.buttons.matching(NSPredicate(format: "identifier MATCHES %@", "mask\\.[0-9]+\\.curve\\.2")).firstMatch
        XCTAssertTrue(curve.waitForExistence(timeout: 5))
        if !curve.isHittable { scroll.swipeUp() }
        XCTAssertTrue(curve.isEnabled); curve.tap()
        let opened = try awaitSnapshot("Mask opacity has its own editable curve") { $0.curveProperty == 43 && $0.curveParam == 2 }
        XCTAssertEqual(opened.curveKeys.count, 2)
        XCTAssertEqual(opened.curveKeys.first?.value ?? -1, 1, accuracy: 0.001)
        XCTAssertEqual(opened.curveKeys.last?.value ?? -1, 0.25, accuracy: 0.001)
    }

    func testTextAnimatorMovesToEffectsWithoutDeletingThePreset() throws {
        let before = try launch("text-animator-editing")
        XCTAssertEqual(before.textAnimatorCount, 1)
        XCTAssertFalse(app.buttons["text.anim.0.duplicate"].exists)
        let add = app.buttons["text.animator.add"].firstMatch
        XCTAssertTrue(add.waitForExistence(timeout: 5)); add.tap()
        let added = try awaitSnapshot("Text animation opens in the real effect stack") { $0.effectCount == before.effectCount + 1 }
        XCTAssertEqual(added.textAnimatorCount, before.textAnimatorCount)
        XCTAssertTrue(app.descendants(matching: .any)["aurea.effects.stack"].firstMatch.waitForExistence(timeout: 5))
        // Only the expanded card exposes its menu; the older Text Transform stays closed.
        XCTAssertTrue(app.buttons["effects.more.\(effectCardId("aurea.text.animator"))"].waitForExistence(timeout: 5))
        XCTAssertFalse(app.buttons["effects.more.\(effectCardId("aurea.text.transform"))"].exists)
        try undo()
        let undone = try awaitSnapshot("Undo removes the effect and preserves the existing preset") { $0.effectCount == before.effectCount }
        XCTAssertEqual(undone.textAnimatorCount, before.textAnimatorCount)
    }

    /// "Text Transform não funciona" (iOS): a ficha da Animação põe o efeito, o
    /// cartão abre com os controles e arrastar Deslocamento X e depois Y muda o
    /// valor NO MOTOR — e o Y não desfaz o X (a releitura velha logo depois de
    /// escrever mandava o ponto antigo de volta).
    func testTextTransformChipAddsEffectAndOffsetDragsReachTheCore() throws {
        let before = try launch("text-transform")
        XCTAssertEqual(before.effectCount, 0)
        let add = app.buttons["text.transform.add"].firstMatch
        XCTAssertTrue(add.waitForExistence(timeout: 5)); add.tap()
        let added = try awaitSnapshot("Text Transform enters the stack") { $0.effectCount == 1 && ($0.textTransformOffset?.count ?? 0) == 2 }
        XCTAssertEqual(added.textTransformOffset ?? [], [0, 0])
        let stack = app.scrollViews["aurea.effects.stack"].firstMatch
        func ruler(_ component: Int) throws -> XCUIElement {
            let row = app.descendants(matching: .any).matching(NSPredicate(format: "identifier MATCHES %@", "effects\\.param\\.[0-9]+\\.5\\.\(component)")).firstMatch
            XCTAssertTrue(row.waitForExistence(timeout: 5), "Offset row \(component) of the open Text Transform card")
            // Full swipes can jump past the next44pt row in the compact panel.
            // Use its actual position and reverse direction after overscroll.
            for _ in 0..<10 {
                let viewport = stack.frame.insetBy(dx: 8, dy: 12)
                let center = CGPoint(x: row.frame.midX, y: row.frame.midY)
                if row.isHittable && viewport.contains(center) { break }
                let distance = row.frame.midY - viewport.midY
                let travel = min(viewport.height * 0.35, max(24, abs(distance)))
                let start = stack.coordinate(withNormalizedOffset: CGVector(dx: 0.96, dy: 0.5))
                start.press(forDuration: 0.05, thenDragTo: start.withOffset(CGVector(dx: 0, dy: distance > 0 ? -travel : travel)), withVelocity: .slow, thenHoldForDuration: 0.1)
            }
            XCTAssertTrue(row.isHittable)
            XCTAssertTrue(stack.frame.contains(CGPoint(x: row.frame.midX, y: row.frame.midY)))
            return row
        }
        let x = try ruler(0)
        x.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
            .press(forDuration: 0.05, thenDragTo: x.coordinate(withNormalizedOffset: CGVector(dx: 0.05, dy: 0.5)))
        let movedX = try awaitSnapshot("Offset X reaches the core") { abs($0.textTransformOffset?.first ?? 0) > 1 }
        let xValue = movedX.textTransformOffset?.first ?? 0
        let y = try ruler(1)
        y.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.5))
            .press(forDuration: 0.05, thenDragTo: y.coordinate(withNormalizedOffset: CGVector(dx: 0.95, dy: 0.5)))
        let movedY = try awaitSnapshot("Offset Y reaches the core") { abs(($0.textTransformOffset ?? [0, 0]).last ?? 0) > 1 }
        XCTAssertEqual(movedY.textTransformOffset?.first ?? 0, xValue, accuracy: 0.01, "Writing Y must keep the X already written")
        try undo()
        _ = try awaitSnapshot("Undo restores Offset Y") { abs(($0.textTransformOffset ?? [1, 1]).last ?? 1) < 0.01 }
    }

    /// "Só dá pra usar 3 objetos nulos": quatro nulos a mais pela barra de
    /// adicionar, todos na lista, cada um com nome próprio.
    func testManyNullsFromTheAddBarAreAllCreatedWithDistinctNames() throws {
        let before = try launch("null-add")
        var count = before.layerCount
        for _ in 0..<4 {
            let back = app.buttons["Back (clear the selection)"].firstMatch
            if back.waitForExistence(timeout: 3) && back.isHittable { back.tap() }
            let element = app.buttons["aurea.add.category.4"].firstMatch
            XCTAssertTrue(element.waitForExistence(timeout: 5)); element.tap()
            let null = app.buttons["Null"].firstMatch
            XCTAssertTrue(null.waitForExistence(timeout: 5)); null.tap()
            let expected = count + 1
            _ = try awaitSnapshot("Null \(expected) is created") { $0.layerCount == expected }
            count = expected
        }
        let after = try awaitSnapshot("Five nulls listed") { $0.layerCount == 5 && ($0.layerNames?.count ?? 0) == 5 }
        XCTAssertEqual(Set(after.layerNames ?? []).count, 5, "Each null has its own name: \(after.layerNames ?? [])")
        XCTAssertEqual(Set(after.layerOrder).count, 5)
    }

    /// "Vincula mas não mexe" / "o nulo normal faz o objeto sair da cena":
    /// o texto ligado ao 4º nulo não pula ao ganhar o pai e segue o arrasto do nulo no palco.
    func testTextLinkedToFourthNullStaysPutAndFollowsTheNullDrag() throws {
        let before = try launch("null-link")
        XCTAssertEqual(before.layerCount, 5)
        let parents = before.layerParents ?? []
        guard let child = parents.firstIndex(where: { $0 != 0 }), let centers = before.layerCenters,
              centers.indices.contains(child), centers[child].count == 2 else {
            XCTFail("Text is linked to a null: \(parents)"); throw ProbeError.missing
        }
        XCTAssertEqual(parents[child], before.primaryID, "Linked to the selected (4th) null")
        // Não pulou: o texto nasceu no centro da composição e continua lá.
        XCTAssertEqual(centers[child][0], before.compositionWidth / 2, accuracy: 2)
        XCTAssertEqual(centers[child][1], before.compositionHeight / 2, accuracy: 2)
        let start = bodyCenter(before)
        let end = CGPoint(x: start.x + 50, y: start.y + 24)
        try requireInsideStage(start, end)
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(end), withVelocity: .slow, thenHoldForDuration: 0.1)
        let moved = try awaitSnapshot("Dragging the null moves it") {
            !$0.isManipulating && self.distance($0.detail.position, before.detail.position) > 1
        }
        let dx = moved.detail.position[0] - before.detail.position[0]
        let dy = moved.detail.position[1] - before.detail.position[1]
        guard let after = moved.layerCenters, after.indices.contains(child), after[child].count == 2 else {
            XCTFail("Child center missing"); throw ProbeError.missing
        }
        XCTAssertEqual(after[child][0] - centers[child][0], dx, accuracy: 1, "Child follows the null in X")
        XCTAssertEqual(after[child][1] - centers[child][1], dy, accuracy: 1, "Child follows the null in Y")
    }

    func testAndroidManualProjectOpensEditsAndPlaysAcrossCuts() throws {
        let before = try launch("manual-android-project")
        XCTAssertEqual(before.layerCount,14)
        XCTAssertEqual(before.markerCount,16)
        XCTAssertEqual(before.missingAssets,0,"The exact Android project must resolve its portable media on iOS")
        XCTAssertEqual(before.effectCount,6)
        try openCommandSearch("Chromatic Aberration")
        let effect = app.buttons.matching(NSPredicate(format:"identifier BEGINSWITH %@","command:effect:")).firstMatch
        XCTAssertTrue(effect.waitForExistence(timeout:5)); effect.tap()
        _ = try awaitSnapshot("Existing Android stack is editable on iOS") { $0.effectCount == 7 }
        let menu = app.buttons["effects.more.\(effectCardId("aurea.color.chromatic_aberration"))"].firstMatch
        XCTAssertTrue(menu.waitForExistence(timeout:5)); menu.tap()
        let copy = app.buttons["Copy this effect"].firstMatch
        XCTAssertTrue(copy.waitForExistence(timeout: 5)); copy.tap()
        try undo()
        _ = try awaitSnapshot("One undo preserves the imported six effects") { $0.effectCount == 6 }
        // The compact phone transport retains clipboard commands in More.
        let more = app.buttons["transport.more"].firstMatch
        if more.exists {
            more.tap()
            let clipboard = app.buttons["transport.copyPaste"].firstMatch
            XCTAssertTrue(clipboard.waitForExistence(timeout: 5)); clipboard.tap()
        } else {
            app.buttons["transport.duplicate"].firstMatch.press(forDuration: 0.8)
        }
        // A folha de copiar/colar anima ao abrir: espera a linha existir.
        let paste = app.buttons["Paste effects"].firstMatch
        XCTAssertTrue(paste.waitForExistence(timeout: 5)); paste.tap()
        _ = try awaitSnapshot("Only the chosen effect is pasted, not the whole stack") { $0.effectCount == 7 }
        try undo()
        _ = try awaitSnapshot("Pasted effect is reversible") { $0.effectCount == 6 }
        let play = app.buttons["Play · hold to repeat"].firstMatch
        XCTAssertTrue(play.waitForExistence(timeout:5)); play.tap()
        _ = try awaitSnapshot("Shared project playback starts") { $0.playing != 0 }
        var passedCut = false
        for _ in 0..<15 {
            RunLoop.current.run(until:Date().addingTimeInterval(1))
            XCTAssertEqual(app.state,.runningForeground)
            let state = try snapshot()
            XCTAssertEqual(state.layerCount,14); XCTAssertEqual(state.markerCount,16)
            if state.corePlayhead > 190 { passedCut = true; break }
        }
        XCTAssertTrue(passedCut,"Composition must actually advance through multiple video cuts")
        let pause = app.buttons["Pause"].firstMatch
        XCTAssertTrue(pause.waitForExistence(timeout:5)); pause.tap()
        _ = try awaitSnapshot("Shared project pauses") { $0.playing == 0 }
    }

    func testRawPlaybackMatrixWithNativeDecodersAndAudio() throws {
        for name in ["h264-720p30", "h264-1080p30", "h264-1080p60", "hevc-1080p30", "h264-1080p-vfr"] {
            _ = try launch("raw-" + name)
            let play = app.buttons["Repeat on · hold to turn off"].firstMatch
            XCTAssertTrue(play.waitForExistence(timeout: 5)); play.tap()
            _ = try awaitSnapshot("RAW playback starts") { $0.playing != 0 }
            var records: [String] = []
            var positions: Set<Int64> = []
            var baseline: Double = 0
            for sample in 0..<12 {
                RunLoop.current.run(until: Date().addingTimeInterval(1))
                XCTAssertEqual(app.state, .runningForeground)
                let state = try snapshot()
                records.append(state.playbackReport + "\nfootprint=\(state.processFootprintBytes)")
                positions.insert(state.corePlayhead)
                XCTAssertNotEqual(state.playing, 0)
                if sample == 3 { baseline = state.processFootprintBytes }
                if sample > 3 { XCTAssertLessThan(state.processFootprintBytes, baseline + 120 * 1024 * 1024) }
            }
            attach("RAW matrix " + name, records.joined(separator: "\n---\n"))
            XCTAssertGreaterThan(positions.count, 8, "Playback must advance, not merely keep the process alive")
            XCTAssertTrue(records.last?.contains("AUREA RAW PLAYBACK TEST") == true)
            app.terminate()
        }
    }

    func testCommandSearchFindsAccentlessActionAndUndoRefreshesMagneticMode() throws {
        _ = try launch("transform")
        let before = try snapshot()
        try openCommandSearch("magnetica")
        let action = app.buttons["command:magnetic"]
        XCTAssertTrue(action.waitForExistence(timeout: 5)); XCTAssertTrue(action.isEnabled)
        action.tap()
        _ = try awaitSnapshot("Magnetic mode changes in the model and core") {
            $0.editMode != before.editMode && $0.editMode == $0.coreEditMode
        }
        try undo()
        _ = try awaitSnapshot("Undo refreshes the iOS magnetic indicator") {
            $0.editMode == before.editMode && $0.editMode == $0.coreEditMode
        }
    }

    func testSpatialChannelEffectsAreIndependentStackEntries() throws {
        _ = try launch("transform")
        for name in ["RGB Split", "Chromatic Aberration"] {
            let before = try snapshot()
            try openCommandSearch(name)
            let effect = app.buttons.matching(NSPredicate(format:"identifier BEGINSWITH %@", "command:effect:")).firstMatch
            XCTAssertTrue(effect.waitForExistence(timeout:5)); XCTAssertTrue(effect.label.contains(name))
            effect.tap()
            _ = try awaitSnapshot("Spatial effect enters native stack") { $0.effectCount == before.effectCount+1 }
            try undo()
            _ = try awaitSnapshot("Undo removes the spatial effect") { $0.effectCount == before.effectCount }
        }
    }

    func testCommandSearchAppliesEffectAndKeepsFavoriteAcrossOpenings() throws {
        _ = try launch("transform")
        let before = try snapshot()
        try openCommandSearch("glow")
        let effect = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "command:effect:")).firstMatch
        XCTAssertTrue(effect.waitForExistence(timeout: 5)); XCTAssertTrue(effect.isEnabled)
        effect.tap()
        _ = try awaitSnapshot("Effect from universal search reaches the native stack") { $0.effectCount == before.effectCount + 1 }
        try undo()
        _ = try awaitSnapshot("Search effect is a reversible edit") { $0.effectCount == before.effectCount }

        try openCommandSearch("magnetica")
        let add = app.buttons["commandSearch.favorite:magnetic"].firstMatch
        XCTAssertTrue(add.waitForExistence(timeout: 5))
        if !add.isSelected { add.tap() }
        app.buttons["commandSearchClose"].firstMatch.tap()
        try openCommandSearch("")
        app.buttons["commandSearch.category.favoritos"].firstMatch.tap()
        XCTAssertTrue(app.buttons["command:magnetic"].waitForExistence(timeout: 5))
        app.buttons["commandSearchClose"].firstMatch.tap()
        XCTAssertEqual(try snapshot().editMode, before.editMode, "Favoriting must not execute the command")
    }

    private func openCommandSearch(_ query: String) throws {
        var open = app.buttons["commandSearchOpen"].firstMatch
        // Redesenho 2026-09-29: com uma seção da camada aberta o topo é só `‹` + título
        // (sem lupa nem engrenagem); o `‹` volta às ferramentas da camada, onde a lupa mora.
        let section = app.otherElements["editor.sectionBar"].firstMatch
        if !open.waitForExistence(timeout: 2), section.exists {
            let back = section.buttons.firstMatch
            XCTAssertTrue(back.isHittable); back.tap()
            open = app.buttons["commandSearchOpen"].firstMatch
        }
        if !open.waitForExistence(timeout: 2) {
            // Redesenho 2026-09-29: sem camada escolhida, a busca mora no menu da engrenagem.
            let gear = app.buttons["editor.projectMenu"].firstMatch
            XCTAssertTrue(gear.waitForExistence(timeout: 5)); gear.tap()
            open = app.buttons["commandSearchOpen"].firstMatch
        }
        XCTAssertTrue(open.waitForExistence(timeout: 5)); XCTAssertTrue(open.isHittable); open.tap()
        let field = app.textFields["commandSearchField"].firstMatch
        XCTAssertTrue(field.waitForExistence(timeout: 5))
        if !query.isEmpty { field.tap(); field.typeText(query) }
    }

    func testSlipChangesContentKeepsBoundsAndUndoRestoresIt() throws {
        let before = try launch("clip-edit")
        try openCommandSearch("slip")
        let command = app.buttons["command:clip_edit"]
        XCTAssertTrue(command.waitForExistence(timeout: 5)); command.tap()
        let advance = app.buttons["clipEdit.forward"]
        XCTAssertTrue(advance.waitForExistence(timeout: 5)); XCTAssertTrue(advance.isEnabled)
        advance.tap()
        let changed = try awaitSnapshot("Slip creates an editable remap in the native layer") {
            ($0.clipTimeRemap.first ?? 0) > 0
        }
        XCTAssertEqual(changed.detail.startFrame, before.detail.startFrame)
        XCTAssertEqual(changed.detail.endFrame, before.detail.endFrame)
        XCTAssertEqual(changed.detail.localPlayhead, before.detail.localPlayhead)
        assertTransform(changed, equals: before)
        try undo()
        _ = try awaitSnapshot("One undo restores the original clip timing") { $0.clipTimeRemap == before.clipTimeRemap && $0.canRedo }
        let roll = app.buttons["clipEdit.mode.4"]
        XCTAssertTrue(roll.exists); roll.tap()
        XCTAssertFalse(advance.isEnabled, "Roll needs an explicit adjacent clip")
    }

    func testText3DLetterControlsPrepareGeometryAndUndoRestoresIt() throws {
        let before = try launch("text-3d")
        XCTAssertFalse(before.textGlyphLayout)
        try openCommandSearch("Text 3D Layout")
        let effect = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "command:effect:")).firstMatch
        XCTAssertTrue(effect.waitForExistence(timeout: 5)); XCTAssertTrue(effect.isEnabled)
        XCTAssertTrue(effect.label.contains("Text 3D Layout")); effect.tap()
        _ = try awaitSnapshot("Letter geometry and effect added together") {
            $0.textGlyphLayout && $0.effectCount == before.effectCount + 1
        }
        XCTAssertTrue(app.staticTexts["Text 3D Layout"].firstMatch.waitForExistence(timeout: 5))
        try undo()
        _ = try awaitSnapshot("Undo restores unsplit geometry and removes the effect") {
            !$0.textGlyphLayout && $0.effectCount == before.effectCount
        }
    }

    func testProceduralPatternsCanBeFoundAddedAndUndone() throws {
        _ = try launch("transform")
        let before = try snapshot()
        for name in ["Stripes", "Radial Rays", "Grid", "Parenting Helper"] {
            try openCommandSearch(name)
            let effect = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "command:effect:")).firstMatch
            XCTAssertTrue(effect.waitForExistence(timeout: 5)); XCTAssertTrue(effect.isEnabled)
            XCTAssertTrue(effect.label.contains(name)); effect.tap()
            _ = try awaitSnapshot("New effect reaches native stack") { $0.effectCount == before.effectCount + 1 }
            try undo()
            _ = try awaitSnapshot("New effect is reversible") { $0.effectCount == before.effectCount }
        }
    }

    func testCinematicMetalMaterialAppliesFromPanelAndCanBeUndone() throws {
        let before = try launch("text-3d")
        try openCommandSearch("material")
        let action = app.buttons["command:environment"]
        XCTAssertTrue(action.waitForExistence(timeout: 5)); action.tap()
        let preset = app.buttons["text3d.materialPreset.6"]
        let scroll = app.scrollViews["text3d.materialScroll"].firstMatch
        XCTAssertTrue(scroll.waitForExistence(timeout: 5))
        for _ in 0..<6 {
            if preset.exists && preset.isHittable { break }
            scroll.swipeUp()
        }
        XCTAssertTrue(preset.isHittable); preset.tap()
        _ = try awaitSnapshot("Cinematic Metal reaches the native material recipe") { $0.textSurfaceFinish == 4 }
        try undo()
        _ = try awaitSnapshot("Undo restores the previous material") { $0.textSurfaceFinish == before.textSurfaceFinish }
    }

    func testVideoTextAndCaptionsPlayForThirtySecondsWithBoundedMemory() throws {
        _ = try launch("playback-stress")
        let play = app.buttons["Repeat on · hold to turn off"].firstMatch
        XCTAssertTrue(play.waitForExistence(timeout: 5))
        play.tap()
        let initial = try awaitSnapshot("Playback actually starts") { $0.playing != 0 }
        var positions: Set<Int64> = []
        var baseline = initial.processFootprintBytes
        for sample in 0..<30 {
            RunLoop.current.run(until: Date().addingTimeInterval(1))
            XCTAssertEqual(app.state, .runningForeground)
            let value = try snapshot()
            XCTAssertNotEqual(value.playing, 0)
            positions.insert(value.corePlayhead)
            if sample == 5 { baseline = value.processFootprintBytes }
            if sample > 5 { XCTAssertLessThan(value.processFootprintBytes, baseline + 180 * 1024 * 1024) }
        }
        XCTAssertGreaterThan(positions.count, 2, "Playhead must advance while decoding video and drawing text/captions")
    }

    func testDockMoveVideoAtTwoSecondsKeepsAppResponsiveAndPreservesDuration() throws {
        let before = try launch("video-move")
        XCTAssertEqual(before.detail.kind, 1)
        XCTAssertEqual(before.corePlayhead, 60)
        XCTAssertEqual(before.detail.startFrame, 0)
        let duration = before.detail.endFrame - before.detail.startFrame
        XCTAssertGreaterThan(duration, 0)
        // "Puxar para o cabeçote" saiu da doca (igual à do AM) e mora no menu ⋯ da camada.
        let more = app.buttons["More layer actions"].firstMatch
        XCTAssertTrue(more.waitForExistence(timeout: 5)); more.tap()
        let move = app.buttons["Pull the layer to the playhead"].firstMatch
        XCTAssertTrue(move.waitForExistence(timeout: 5))
        let menu = app.scrollViews.containing(.button, identifier: "Pull the layer to the playhead").firstMatch
        for _ in 0..<6 where !move.isHittable && menu.exists { menu.swipeUp() }
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

    func testEffectsPanelAddsWithOneTapAndKeepsTheStackAtHand() throws {
        _ = try launch("effects")
        // Camada sem efeito: o painel abre direto na tela cheia "Adicionar efeito"
        // (✕ · título · 🔍, destaques, recentes e ladrilhos de categoria).
        let addTab = app.buttons["effects.close"].firstMatch
        XCTAssertTrue(addTab.waitForExistence(timeout: 5))
        let search = app.buttons["effects.search"].firstMatch
        XCTAssertTrue(search.waitForExistence(timeout: 5)); XCTAssertTrue(search.isHittable)
        XCTAssertGreaterThanOrEqual(search.frame.height, 44)
        // Os ladrilhos de categoria ficam embaixo dos destaques (grade preguiçosa).
        let home = app.scrollViews["effects.home"].firstMatch
        let all = app.buttons["effects.category.all"].firstMatch
        for _ in 0..<6 where !(all.exists && all.isHittable) && home.exists { home.swipeUp() }
        XCTAssertTrue(all.exists); XCTAssertGreaterThanOrEqual(all.frame.height, 44)

        // Busca: o campo abre focado acima do teclado; um toque no resultado adiciona.
        search.tap()
        let field = app.textFields["effects.search.field"].firstMatch
        XCTAssertTrue(field.waitForExistence(timeout: 5))
        field.tap(); field.typeText("deep glow")
        let deepGlow = effectCardId("aurea.light.deep_glow")
        let result = app.buttons["effects.result.\(deepGlow)"].firstMatch
        XCTAssertTrue(result.waitForExistence(timeout: 5))
        result.tap()
        let dismissed = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"), object: field)
        XCTAssertEqual(XCTWaiter.wait(for: [dismissed], timeout: 8), .completed)
        _ = try awaitSnapshot("One tap on a search result adds the effect") { $0.effectCount == 1 }

        // O efeito novo aparece aberto na pilha (redesenho 2026-10-01: a tela de
        // adicionar fecha e a pilha com o trilho fica com o efeito aberto).
        let leftCatalog = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"), object: addTab)
        XCTAssertEqual(XCTWaiter.wait(for: [leftCatalog], timeout: 5), .completed)
        XCTAssertTrue(app.staticTexts["Deep Glow"].firstMatch.waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["effects.more.\(deepGlow)"].firstMatch.waitForExistence(timeout: 5))

        // O rodapé "Add effect" da pilha leva de volta ao catálogo, com o recente à vista.
        let stack = app.scrollViews["aurea.effects.stack"].firstMatch
        XCTAssertTrue(stack.exists)
        let add = app.buttons["aurea.effects.add"].firstMatch
        for _ in 0..<8 {
            if add.isHittable { break }
            stack.swipeUp()
        }
        XCTAssertTrue(add.isHittable)
        XCTAssertEqual(add.label, "Add effect")
        add.tap()
        XCTAssertTrue(addTab.waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["effects.recent.\(deepGlow)"].firstMatch.waitForExistence(timeout: 5))

        // Ladrilho de categoria abre o grupo; cartão de um toque, sem ficha no meio.
        let glitch = app.buttons["effects.category.glitch"].firstMatch
        for _ in 0..<6 where !(glitch.exists && glitch.isHittable) && home.exists { home.swipeUp() }
        XCTAssertTrue(glitch.waitForExistence(timeout: 5)); glitch.tap()
        let vhs = app.buttons["effects.card.\(effectCardId("aurea.glitch.vhs"))"].firstMatch
        let grid = app.scrollViews["effects.grid"].firstMatch
        XCTAssertTrue(grid.waitForExistence(timeout: 5))
        for _ in 0..<8 where !(vhs.exists && vhs.isHittable) { grid.swipeUp() }
        XCTAssertTrue(vhs.waitForExistence(timeout: 5))
        vhs.tap()
        _ = try awaitSnapshot("One tap on a grid card adds the effect") { $0.effectCount == 2 }
        XCTAssertTrue(app.staticTexts["VHS"].firstMatch.waitForExistence(timeout: 5))

        try undo()
        _ = try awaitSnapshot("Grid add is one undo step") { $0.effectCount == 1 }
        try undo()
        _ = try awaitSnapshot("Search add is one undo step") { $0.effectCount == 0 }
    }

    /// O id do cartão (`effects.card.<id>`): FNV-1a 32 da chave, sem sinal — o
    /// mesmo `typeId` do motor (o teste não liga no app, então refaz a conta).
    private func effectCardId(_ key: String) -> String {
        var h: UInt32 = 0x811C_9DC5
        for byte in key.utf8 { h ^= UInt32(byte); h = h &* 16_777_619 }
        return String(h)
    }

    func testFloatingAddRestoresPreviewAndClosesAfterSelection() throws {
        let before = try launch("layer-dock")
        let frame = stage.frame
        // O "+" saiu: sem camada escolhida, a barra fixa de adicionar fica embaixo.
        app.buttons["Back (clear the selection)"].firstMatch.tap()
        let category = app.buttons["aurea.add.category.0"].firstMatch
        XCTAssertTrue(category.waitForExistence(timeout: 5))
        XCTAssertGreaterThanOrEqual(stage.frame.height, frame.height - 1)
        let bubbles = XCTAttachment(screenshot: app.screenshot())
        bubbles.name = "Add bar categories"; bubbles.lifetime = .keepAlways; self.add(bubbles)
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
        // Undo deletes the selected new layer, so the probe intentionally has
        // no geometry. Decode the layer summary instead of requiring Detail.
        struct LayerSummary: Decodable {
            let layerCount: Int
            let selectionCount: Int
            let canRedo: Bool
        }
        let restored = XCTNSPredicateExpectation(predicate: NSPredicate { element, _ in
            guard let view = element as? XCUIElement,
                  let json = view.value as? String, let data = json.data(using: .utf8),
                  let value = try? JSONDecoder().decode(LayerSummary.self, from: data) else { return false }
            return value.layerCount == before.layerCount && value.selectionCount == 0 && value.canRedo
        }, object: stage)
        XCTAssertEqual(XCTWaiter.wait(for: [restored], timeout: 6), .completed)
        attach("After undo added shape", stage.value as? String ?? "missing")
    }

    /// Presets de texto de volta (2026-10-03): a categoria ao lado do Texto
    /// abre a grade e o toque num cartão cria UM texto já animado.
    func testTextPresetFromTheAddBarAddsOneAnimatedText() throws {
        let before = try launch("layer-dock")
        app.buttons["Back (clear the selection)"].firstMatch.tap()
        let category = app.buttons["aurea.add.category.8"].firstMatch
        XCTAssertTrue(category.waitForExistence(timeout: 5)); category.tap()
        let card = app.buttons.matching(NSPredicate(format: "label BEGINSWITH %@", "Add text with")).firstMatch
        XCTAssertTrue(card.waitForExistence(timeout: 5)); card.tap()
        _ = try awaitSnapshot("Text preset adds one layer") { $0.layerCount == before.layerCount + 1 }
        XCTAssertFalse(card.exists)
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

    func testMediaLabEffectsReachNativeRendererAndUndo() throws {
        _ = try launch("transform")
        let before = try snapshot()
        for name in ["JPEG Glitch", "Signal Analog", "Tracery", "Deep Glow 2", "Shadow Studio 3"] {
            try openCommandSearch(name)
            let effect = app.buttons.matching(NSPredicate(format: "identifier BEGINSWITH %@", "command:effect:")).firstMatch
            XCTAssertTrue(effect.waitForExistence(timeout: 5)); XCTAssertTrue(effect.isEnabled)
            XCTAssertTrue(effect.label.contains(name)); effect.tap()
            _ = try awaitSnapshot("Media Lab effect reaches the native stack") { $0.effectCount == before.effectCount + 1 }
            RunLoop.current.run(until: Date().addingTimeInterval(0.5))
            XCTAssertEqual(app.state, .runningForeground)
            let screenshot = XCTAttachment(screenshot: app.screenshot())
            screenshot.name = name; screenshot.lifetime = .keepAlways; add(screenshot)
            try undo()
            _ = try awaitSnapshot("Media Lab effect can be undone") { $0.effectCount == before.effectCount }
        }
    }

    func testCurvesOpenForNullYAndShapeHeightInCompactPanel() throws {
        for scene in ["curve-null", "curve-shape"] {
            let prepared = try launch(scene)
            if scene == "curve-null" {
                XCTAssertNotEqual(prepared.detail.animatedMask & 2, 0,
                                  "The fixture must insert real Y keyframes before testing curve access")
            }
            let open = app.buttons["Edit the property curve"].firstMatch
            XCTAssertTrue(open.waitForExistence(timeout: 5)); XCTAssertTrue(open.isEnabled)
            open.tap()
            let state = try awaitSnapshot("Animated component opens its curve") { $0.sheet == "curve" }
            XCTAssertEqual(state.curveProperty, scene == "curve-null" ? 1 : 35)
            XCTAssertEqual(state.curveParam, scene == "curve-null" ? 0 : 6)
            let panel = app.otherElements["curve.panel"].firstMatch
            XCTAssertTrue(panel.waitForExistence(timeout: 5))
            XCTAssertLessThanOrEqual(panel.frame.height, 281)
            for mode in [1, 2, 0] {
                let button = app.buttons["curve.mode.\(mode)"]
                XCTAssertTrue(button.isHittable); button.tap()
                XCTAssertEqual(app.state, .runningForeground)
            }
            let screenshot = XCTAttachment(screenshot: app.screenshot())
            screenshot.name = "Compact themed graph \(scene)"; screenshot.lifetime = .keepAlways; add(screenshot)
            app.terminate()
        }
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

    func testTimelineSeekRefreshesThePublishedAnimatedLayerDetail() throws {
        let before = try launch("curve-null")
        XCTAssertEqual(before.detail.position[1], 200, accuracy: 0.01)
        let timeline = app.otherElements["aurea.parity.timeline"].firstMatch
        XCTAssertTrue(timeline.waitForExistence(timeout: 5)); XCTAssertTrue(timeline.isHittable)
        let start = CGPoint(x: timeline.frame.midX + 70, y: timeline.frame.minY + 14)
        let end = CGPoint(x: start.x - 90, y: start.y)
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(end),
                                withVelocity: .slow, thenHoldForDuration: 0.1)
        let moved = try awaitSnapshot("The inspector follows the native seek, even after an optimistic UI update") {
            guard let shown = $0.publishedDetail, shown.position.count == 3 else { return false }
            return $0.corePlayhead > 5 && $0.playhead == $0.corePlayhead
                && shown.localPlayhead == $0.detail.localPlayhead
                && abs(shown.position[1] - $0.detail.position[1]) < 0.01
                && shown.position[1] > before.detail.position[1] + 1
        }
        XCTAssertEqual(moved.publishedDetail?.localPlayhead, moved.corePlayhead)
    }

    func testHorizontalSwipeOnClipOnlyScrolls() throws {
        _ = try launch("layer-dock")
        app.buttons["Back (clear the selection)"].firstMatch.tap()
        let cleared = try awaitSnapshot("Selection cleared") { $0.selectionCount == 0 }
        let timeline = app.otherElements["aurea.parity.timeline"].firstMatch
        XCTAssertTrue(timeline.waitForExistence(timeout: 5))
        // Meio da 1ª barra: fileiras a partir de 44 (riscos 14 + relógio 30), pílula 48..76, barra 50..76.
        let start = CGPoint(x: timeline.frame.midX + 40, y: timeline.frame.minY + 62)
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(CGPoint(x: start.x - 60, y: start.y)), withVelocity: .slow, thenHoldForDuration: 0.1)
        RunLoop.current.run(until: Date().addingTimeInterval(0.5))
        let after = try snapshot()
        XCTAssertEqual(after.selectionCount, 0)
        XCTAssertEqual(after.layerStarts, cleared.layerStarts)
    }

    /// Camada escolhida com a doca aberta: a timeline é a fileira única dela,
    /// "sem ficar impossível de mexer" (par do Timeline.kt/EditorScreen.kt).
    /// Arrastar na barra compacta só rola (nunca desmarca) e o ícone do tipo na
    /// calha ABRE as trilhas da camada em vez de sair.
    func testCompactDockRowScrollsWithoutDeselectingAndGutterOpensTracks() throws {
        let before = try launch("layer-dock")
        _ = try awaitSnapshot("The layer opens with its dock") { $0.sheet == "dock" && $0.selectionCount == 1 }
        let timeline = app.otherElements["aurea.parity.timeline"].firstMatch
        XCTAssertTrue(timeline.waitForExistence(timeout: 5))
        let frame = timeline.frame
        // A barra compacta (fileiras a partir de 44; barra em 50..76), longe das alças e das setas.
        let start = CGPoint(x: frame.midX + 60, y: frame.minY + 62)
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(CGPoint(x: start.x - 60, y: start.y)),
                                withVelocity: .slow, thenHoldForDuration: 0.1)
        RunLoop.current.run(until: Date().addingTimeInterval(0.6))
        let swiped = try snapshot()
        XCTAssertEqual(swiped.selectionCount, 1, "A horizontal drag on the compact row must never deselect")
        XCTAssertEqual(swiped.sheet, "dock")
        XCTAssertEqual(swiped.primaryID, before.primaryID)
        XCTAssertEqual(swiped.layerStarts, before.layerStarts, "A plain drag only scrolls; it does not move the clip")
        // O quadradinho do tipo na pílula (x 32..54; o olho é x < 28): abre as trilhas, não sai.
        coordinate(CGPoint(x: frame.minX + 43, y: frame.minY + 62)).tap()
        let deadline = Date().addingTimeInterval(0.8)
        repeat {
            let state = try snapshot()
            XCTAssertEqual(state.selectionCount, 1, "The gutter icon opens the tracks instead of leaving the layer")
            XCTAssertEqual(state.sheet, "dock")
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        } while Date() < deadline
    }

    func testHoldingLayerToMoveDoesNotOpenOptionsButTapDoes() throws {
        _ = try launch("layer-dock")
        app.buttons["Back (clear the selection)"].firstMatch.tap()
        let timeline = app.otherElements["aurea.parity.timeline"].firstMatch
        XCTAssertTrue(timeline.waitForExistence(timeout: 5))
        let frame = timeline.frame
        // First row, inside the clip body (bar at 50..76), away from its trim handles.
        let start = CGPoint(x: frame.midX + 40, y: frame.minY + 62)
        let end = CGPoint(x: start.x + 45, y: start.y)
        coordinate(start).press(forDuration: 0.7, thenDragTo: coordinate(end),
                                withVelocity: .slow, thenHoldForDuration: 0.1)
        let moved = try awaitSnapshot("Long press moves the unselected layer without opening its options") {
            $0.detail.startFrame > 0 && $0.sheet == "addBar"
        }
        XCTAssertEqual(timeline.frame.height, frame.height, accuracy: 1)
        XCTAssertEqual(moved.selectionCount, 1)
        coordinate(end).tap()
        _ = try awaitSnapshot("A deliberate tap opens the layer options") { $0.sheet == "dock" }
    }

    func testRotationDialKeysXYZTogether() throws {
        let before = try launch("rotation-isolation")
        let mode = app.descendants(matching: .any)["aurea.panel.mode.2"].firstMatch
        XCTAssertTrue(mode.waitForExistence(timeout: 5)); mode.tap()
        let axis = app.buttons["transform.rotation.axis.0"]
        XCTAssertTrue(axis.waitForExistence(timeout: 5)); axis.tap()
        let dial = app.descendants(matching: .any)["transform.rotation.dial"].firstMatch
        XCTAssertTrue(dial.waitForExistence(timeout: 5))
        let frame = dial.frame
        let start = CGPoint(x: frame.midX + frame.width * 0.3, y: frame.midY)
        let end = CGPoint(x: frame.midX, y: frame.midY - frame.height * 0.3)
        app.buttons["stage.autokey"].tap()
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(end), withVelocity: .slow, thenHoldForDuration: 0.1)
        let layout = try awaitSnapshot("Auto-Key off moves existing animation without inserting keys") {
            $0.curveKeys.contains { $0.property == 6 && $0.time == 0 && abs($0.value) > 1 }
        }
        XCTAssertEqual(layout.curveKeys.count, before.curveKeys.count)
        try undo()
        _ = try awaitSnapshot("Layout adjustment undoes once") { $0.curveKeys == before.curveKeys }
        app.buttons["stage.autokey"].tap()
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(end), withVelocity: .slow, thenHoldForDuration: 0.1)
        let changed = try awaitSnapshot("Rotation XYZ receives one grouped key at the playhead") {
            $0.curveKeys.contains { $0.property == 6 && $0.time == 15 && abs($0.value) > 1 }
        }
        XCTAssertEqual(changed.curveKeys.count, before.curveKeys.count + 3)
        XCTAssertEqual(changed.curveKeys.filter { $0.time != 15 }, before.curveKeys)
        for property in [7, 8] {
            XCTAssertEqual(changed.curveKeys.first { $0.property == property && $0.time == 15 }?.value,
                           before.curveKeys.first { $0.property == property }?.value)
        }
        try undo()
        _ = try awaitSnapshot("Undo restores all three rotation tracks") { $0.curveKeys == before.curveKeys }
    }

    /// Os componentes que o gráfico edita juntos. Camada/nulo/câmera 3D: o grupo
    /// XYZ da propriedade (posição, escala, rotação, pivô) — decisão do build 2125
    /// ("Keyframes XYZ agrupados… o gráfico sincroniza curvas e arrastos do grupo
    /// XYZ", `graphKeyGroup` no iOS e no EditorStore.kt). Nulo 2D: no GRÁFICO, só o componente
    /// escolhido (Posição X e Y são trilhas independentes).
    /// `linked`: curva aplicada à PROPRIEDADE pelo painel/preset — o motor leva a
    /// mesma curva a todos os eixos com keyframe no instante, também no 2D (beta
    /// "o Y não acompanha o X", `linked_axis_refs` em Command.hpp); a cena 2D só
    /// tem X e Y.
    private func curveGroup(_ property: Int, threeD: Bool, linked: Bool = false) -> Set<Int> {
        guard threeD || linked, property < 12 else { return [property] }
        let base = property / 3 * 3
        return threeD ? [base, base + 1, base + 2] : [base, base + 1]
    }
    /// "curve-isolation": nulo 3D com X/Y/Z em 0/30/60; "curve-isolation-2d": nulo 2D com X/Y.
    private let curveScenes: [(scene: String, threeD: Bool)] = [("curve-isolation", true), ("curve-isolation-2d", false)]

    func testCurvePresetEasesEveryAxisOfOnlyTheSelectedSegment() throws {
        for (scene, threeD) in curveScenes {
            let before = try launch(scene)
            let open = app.buttons["Edit the property curve"].firstMatch
            XCTAssertTrue(open.waitForExistence(timeout: 5)); open.tap()
            let selected = try awaitSnapshot("Property curve opens") { $0.sheet == "curve" }
            let group: Set<Int> = curveGroup(Int(selected.curveProperty), threeD: threeD, linked: true)
            XCTAssertEqual(group.count, threeD ? 3 : 2)
            let preset = app.buttons["curve.preset.bounce"].firstMatch
            XCTAssertTrue(preset.waitForExistence(timeout: 5)); XCTAssertTrue(preset.isHittable); preset.tap()
            let count = app.buttons["curve.bounce.count"].firstMatch
            XCTAssertTrue(count.waitForExistence(timeout: 5)); count.tap()
            XCTAssertTrue(count.label.contains("4"))
            let strength = app.sliders["curve.bounce.strength"].firstMatch
            XCTAssertTrue(strength.waitForExistence(timeout: 5))
            strength.adjust(toNormalizedSliderPosition: 0.75)
            // O segmento que SAI do keyframe 0 em TODOS os eixos (X e Y no 2D, XYZ no 3D).
            let changed = try awaitSnapshot("Preset changes the selected outgoing segment (\(scene))") { state in
                group.allSatisfy { property in
                    state.curveKeys.contains { $0.property == property && $0.time == 0 && $0.interpolation == 7 }
                }
            }
            // Nenhum outro segmento, componente fora do grupo, tempo ou valor muda.
            let untouched: (CurveKey) -> Bool = { !group.contains($0.property) || $0.time != 0 }
            XCTAssertEqual(before.curveKeys.filter(untouched), changed.curveKeys.filter(untouched))
            XCTAssertEqual(before.curveKeys.map(\.time), changed.curveKeys.map(\.time))
            XCTAssertEqual(before.curveKeys.map(\.value), changed.curveKeys.map(\.value))
            // Preset, saltos e amplitude são passos próprios (igual ao Android):
            // cada desfazer volta um, nunca metade do grupo XYZ, e o último
            // devolve TODAS as curvas originais.
            for _ in 0..<5 {
                if (try? snapshot())?.curveKeys == before.curveKeys { break }
                try undo()
                RunLoop.current.run(until: Date().addingTimeInterval(0.5))
                let state = try snapshot()
                let bouncing = group.filter { property in
                    state.curveKeys.contains { $0.property == property && $0.time == 0 && $0.interpolation == 7 }
                }
                XCTAssertTrue(bouncing.isEmpty || bouncing.count == group.count, "Undo never splits the XYZ easing (\(scene))")
            }
            _ = try awaitSnapshot("Undo restores all original curves (\(scene))") { $0.curveKeys == before.curveKeys }
            app.terminate()
        }
    }

    func testValueGraphDragsTimeAndValueWithoutChangingOtherTracks() throws {
        for (scene, threeD) in curveScenes {
            let before = try launch(scene)
            app.buttons["Edit the property curve"].firstMatch.tap()
            let selected = try awaitSnapshot("Graph opens") { $0.sheet == "curve" }
            let property = Int(selected.curveProperty)
            let group: Set<Int> = curveGroup(property, threeD: threeD)
            app.buttons["curve.mode.1"].tap()
            let graph = app.otherElements["curve.trackGraph"].firstMatch
            XCTAssertTrue(graph.waitForExistence(timeout: 5))
            let frame = graph.frame
            let start = CGPoint(x: frame.midX, y: frame.midY)
            let end = CGPoint(x: start.x + 20, y: start.y - 15)
            coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(end), withVelocity: .slow, thenHoldForDuration: 0.1)
            let moved = try awaitSnapshot("Value graph changes both frame and value (\(scene))") {
                $0.curveKeys.contains { $0.property == property && $0.time > 30 && $0.time < 60 && $0.value > 230 }
            }
            let other: (CurveKey) -> Bool = { !group.contains($0.property) }
            XCTAssertEqual(before.curveKeys.filter(other), moved.curveKeys.filter(other))
            XCTAssertEqual(moved.curveKeys.count, before.curveKeys.count)
            let target: Int = moved.curveKeys.first { $0.property == property && $0.time > 30 && $0.time < 60 }?.time ?? -1
            for peer in group {
                // O grupo anda junto no tempo; só o componente arrastado muda de valor.
                if peer != property {
                    XCTAssertTrue(moved.curveKeys.contains { $0.property == peer && $0.time == target && $0.value == 230 },
                                  "XYZ peer \(peer) follows the dragged key in time and keeps its value")
                }
                // Os keyframes 0 e 60 do grupo ficam onde estavam.
                XCTAssertEqual(moved.curveKeys.filter { $0.property == peer && $0.time != target },
                               before.curveKeys.filter { $0.property == peer && $0.time != 30 })
            }
            try undo()
            _ = try awaitSnapshot("One undo restores time and value (\(scene))") { $0.curveKeys == before.curveKeys }
            app.terminate()
        }
    }

    func testSpeedGraphHandleEditsOnlyOutgoingIntervalAndUndoesOnce() throws {
        for (scene, threeD) in curveScenes {
            let before = try launch(scene)
            app.buttons["Edit the property curve"].firstMatch.tap()
            let selected = try awaitSnapshot("Graph opens") { $0.sheet == "curve" }
            let group: Set<Int> = curveGroup(Int(selected.curveProperty), threeD: threeD)
            app.buttons["curve.mode.2"].tap()
            let graph = app.otherElements["curve.trackGraph"].firstMatch
            XCTAssertTrue(graph.waitForExistence(timeout:5))
            let frame = graph.frame
            let start = CGPoint(x:frame.minX+frame.width*0.2024,y:frame.minY+frame.height*0.0968)
            let end = CGPoint(x:start.x+12,y:frame.minY+frame.height*0.4)
            coordinate(start).press(forDuration:0.05,thenDragTo:coordinate(end),withVelocity:.slow,thenHoldForDuration:0.1)
            let changed = try awaitSnapshot("Speed handle changes the selected interval (\(scene))") {
                $0.curveKeys != before.curveKeys
            }
            let untouched: (CurveKey) -> Bool = { !group.contains($0.property) || $0.time != 0 }
            XCTAssertEqual(before.curveKeys.filter(untouched),changed.curveKeys.filter(untouched))
            XCTAssertEqual(before.curveKeys.map(\.time),changed.curveKeys.map(\.time))
            XCTAssertEqual(before.curveKeys.map(\.value),changed.curveKeys.map(\.value))
            // O intervalo que sai do keyframe 0 ganha a curva de velocidade no grupo inteiro.
            let outgoing: [CurveKey] = changed.curveKeys.filter { group.contains($0.property) && $0.time == 0 }
            XCTAssertEqual(outgoing.count, group.count)
            XCTAssertTrue(outgoing.allSatisfy { $0.interpolation == 2 }, "Speed handle writes a Bezier segment on every grouped axis")
            try undo()
            _ = try awaitSnapshot("Undo restores speed curve (\(scene))") { $0.curveKeys == before.curveKeys }
            app.terminate()
        }
    }

    func testGraphMovesSelectedKeysTogetherAndUndoesOnce() throws {
        let before = try launch("curve-isolation")
        app.buttons["Edit the property curve"].firstMatch.tap()
        let selected = try awaitSnapshot("Graph opens") { $0.sheet == "curve" }
        app.buttons["curve.mode.1"].tap()
        app.buttons["curve.multi"].tap()
        app.buttons["All"].tap()
        let graph = app.otherElements["curve.trackGraph"].firstMatch
        XCTAssertTrue(graph.waitForExistence(timeout: 5))
        let frame = graph.frame
        let start = CGPoint(x: frame.midX, y: frame.midY)
        coordinate(start).press(forDuration: 0.05,
            thenDragTo: coordinate(CGPoint(x: start.x + 20, y: start.y)), withVelocity: .slow, thenHoldForDuration: 0.1)
        let moved = try awaitSnapshot("All selected keys move in the same gesture") {
            ($0.curveKeys.filter { $0.property == Int(selected.curveProperty) }.map(\.time).min() ?? 0) > 0
        }
        let times = moved.curveKeys.filter { $0.property == Int(selected.curveProperty) }.map(\.time).sorted()
        XCTAssertEqual(times.count, 3)
        if times.count == 3 { XCTAssertEqual(times[1] - times[0], 30); XCTAssertEqual(times[2] - times[1], 30) }
        XCTAssertEqual(moved.curveKeys.filter { $0.property != Int(selected.curveProperty) }, before.curveKeys.filter { $0.property != Int(selected.curveProperty) })
        try undo()
        _ = try awaitSnapshot("One undo restores the whole key selection") { $0.curveKeys == before.curveKeys }
    }

    /// Seleção de keyframes entre PROPRIEDADES na timeline (par do Android
    /// TimelineGesturesTest.multiSelectedKeysAcrossPropertiesMoveDeleteAndDuplicateTogether):
    /// Posição X (0, 30) e Escala X (15, 45), trilhas abertas, modo "Selecionar",
    /// um keyframe de cada trilha; arrastar um move os dois num passo de desfazer;
    /// Excluir e Duplicar agem na seleção inteira.
    func testDiagonalBoundaryDragMovesLinkedScaleAndPersistsAfterRelease() throws {
        let before = try launch("timeline-linked-scale")
        XCTAssertEqual(before.curveKeys.filter { [3, 4].contains($0.property) }.count, 6)
        let timeline = app.otherElements["aurea.parity.timeline"].firstMatch
        XCTAssertTrue(timeline.waitForExistence(timeout: 5))
        let f = timeline.frame
        // Start within the diamond's touch area, on the white cap. The first
        // motion is deliberately diagonal; it must not trim or scrub instead.
        let start = CGPoint(x: f.midX - 8, y: f.minY + 61)
        let end = CGPoint(x: start.x + 16 * 80 / 30, y: start.y + 30)
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(end))
        func moved(_ state: Snapshot) -> Bool {
            [3, 4].allSatisfy { property in state.curveKeys.contains { $0.property == property && $0.time == 16 } }
        }
        let after = try awaitSnapshot("Both linked axes remain at the exact release frame", matching: moved)
        XCTAssertFalse(after.curveKeys.contains { [3, 4].contains($0.property) && $0.time == 0 })
        XCTAssertEqual(after.playhead, before.playhead)
        RunLoop.current.run(until: Date().addingTimeInterval(0.6))
        XCTAssertTrue(moved(try snapshot()))
        try undo()
        _ = try awaitSnapshot("One undo restores both axes") { state in
            [3, 4].allSatisfy { property in state.curveKeys.contains { $0.property == property && $0.time == 0 } }
        }
    }

    func testTimelineMultiSelectsKeysAcrossPropertiesMovesDeletesAndDuplicates() throws {
        let before = try launch("timeline-keys")
        XCTAssertEqual(before.curveKeys.count, 4)
        let timeline = app.otherElements["aurea.parity.timeline"].firstMatch
        XCTAssertTrue(timeline.waitForExistence(timeout: 5))
        let frame = timeline.frame
        // Geometria (pt): fileiras a partir de 44, fileira 32, trilhas de 16 com o losango no meio.
        // Abertas: Transform (76), Position X (92), Scale X (108). Cabeçote no frame 30
        // (a vista É o cabeçote): 80 pt/s = 8/3 pt por frame a partir do centro.
        let positionY: CGFloat = frame.minY + 100
        let scaleY: CGFloat = frame.minY + 116
        // A vista é o cabeçote: lê o quadro REAL (a fixture pode não estar no 30).
        func keyX(_ f: Int) -> CGFloat {
            let playhead: Int64 = (try? snapshot())?.playhead ?? 30
            return frame.midX + CGFloat(Int64(f) - playhead) * 80 / 30
        }
        func has(_ state: Snapshot, _ property: Int, _ time: Int) -> Bool {
            state.curveKeys.contains { $0.property == property && $0.time == time }
        }
        // Abrir as trilhas pelo quadradinho do tipo na pílula (x 32..54; o olho é
        // x < 28; a pílula vai até 78, depois é o corpo do clipe).
        coordinate(CGPoint(x: frame.minX + 43, y: frame.minY + 62)).tap()
        func selectPositionAndScale() throws {
            // Toque simples: só a Posição X @30 (abre a curva; timeline compacta).
            coordinate(CGPoint(x: keyX(30), y: positionY)).tap()
            _ = try awaitSnapshot("Plain tap selects one timeline key") { $0.keySelectionCount == 1 && $0.sheet == "curve" }
            let select = app.buttons["timeline.keys.select"].firstMatch
            XCTAssertTrue(select.waitForExistence(timeout: 5))
            XCTAssertGreaterThanOrEqual(select.frame.height, 44)
            select.tap()
            _ = try awaitSnapshot("Select mode closes the panel") { $0.keySelectMode == true && $0.sheet == "none" }
            // A timeline voltou alta com as trilhas abertas: soma a Escala X @45.
            coordinate(CGPoint(x: keyX(45), y: scaleY)).tap()
            _ = try awaitSnapshot("Scale key joins the selection") { $0.keySelectionCount == 2 }
        }

        // --- Arrastar um move os dois -------------------------------------------------
        try selectPositionAndScale()
        coordinate(CGPoint(x: keyX(30), y: positionY)).press(forDuration: 0.05,
            thenDragTo: coordinate(CGPoint(x: keyX(40), y: positionY)), withVelocity: .slow, thenHoldForDuration: 0.1)
        let moved = try awaitSnapshot("Both selected keys move together") {
            !has($0, 0, 30) && $0.curveKeys.count == 4
        }
        let position = moved.curveKeys.filter { $0.property == 0 && $0.time != 0 }.map(\.time)
        XCTAssertEqual(position.count, 1)
        let delta = (position.first ?? 30) - 30
        XCTAssertGreaterThan(delta, 0)
        XCTAssertTrue(has(moved, 3, 45 + delta), "Scale key moves by the same delta")
        XCTAssertTrue(has(moved, 0, 0)); XCTAssertTrue(has(moved, 3, 15))
        XCTAssertEqual(moved.keySelectionCount, 2)
        try undo()
        _ = try awaitSnapshot("One undo restores both properties") { $0.curveKeys == before.curveKeys }

        // --- Excluir ---------------------------------------------------------------------
        try selectPositionAndScale()
        let delete = app.buttons["timeline.keys.delete"].firstMatch
        XCTAssertTrue(delete.waitForExistence(timeout: 5)); delete.tap()
        let deleted = try awaitSnapshot("Delete removes the whole selection") { $0.curveKeys.count == 2 }
        XCTAssertTrue(has(deleted, 0, 0)); XCTAssertTrue(has(deleted, 3, 15))
        XCTAssertEqual(deleted.keySelectionCount, -1)
        try undo()
        _ = try awaitSnapshot("Undo restores deleted keys") { $0.curveKeys == before.curveKeys }

        // --- Duplicar: cópia 1 frame depois do último (âncora 30 → 46) ----------------------
        try selectPositionAndScale()
        let duplicate = app.buttons["timeline.keys.duplicate"].firstMatch
        XCTAssertTrue(duplicate.waitForExistence(timeout: 5)); duplicate.tap()
        let duplicated = try awaitSnapshot("Duplicate pastes after the last selected key") { $0.curveKeys.count == 6 }
        XCTAssertTrue(has(duplicated, 0, 46)); XCTAssertTrue(has(duplicated, 3, 61))
        XCTAssertEqual(duplicated.keySelectionCount, 2)
        try undo()
        _ = try awaitSnapshot("Undo removes the duplicates") { $0.curveKeys == before.curveKeys }
    }

    func testHoldingLayerBodyReordersAndOneUndoRestoresOrder() throws {
        let before = try launch("timeline-reorder")
        app.buttons["Back (clear the selection)"].firstMatch.tap()
        let timeline = app.otherElements["aurea.parity.timeline"].firstMatch
        XCTAssertTrue(timeline.waitForExistence(timeout: 5))
        let frame = timeline.frame
        // Meio da 1ª barra; 96 = 3 fileiras de 32.
        let start = CGPoint(x: frame.midX + 40, y: frame.minY + 62)
        let end = CGPoint(x: start.x, y: start.y + 96)
        coordinate(start).press(forDuration: 0.7, thenDragTo: coordinate(end),
                                withVelocity: .slow, thenHoldForDuration: 0.1)
        let moved = try awaitSnapshot("Layer body hold changes stacking order") {
            $0.layerOrder != before.layerOrder && $0.sheet == "addBar"
        }
        XCTAssertEqual(Set(moved.layerOrder), Set(before.layerOrder))
        XCTAssertEqual(moved.primaryID, before.layerOrder.first)
        XCTAssertEqual(timeline.frame.height, frame.height, accuracy: 1)
        try undo()
        _ = try awaitSnapshot("One undo restores the entire original stacking order") {
            $0.layerOrder == before.layerOrder && $0.canRedo
        }
    }

    /// "Às vezes a camada é escolhida e vai junto": uma rolagem um pouco torta
    /// (dx > dy por pouco) sobre um clipe NÃO escolhido era classificada como
    /// mover, e uma diagonal depois do toque longo levantava a camada. Nenhuma
    /// das duas pode escolher, mover ou reordenar.
    func testSlightlyDiagonalSwipeOnLayerBodyNeverSelectsMovesOrReorders() throws {
        let before = try launch("timeline-reorder")
        app.buttons["Back (clear the selection)"].firstMatch.tap()
        let timeline = app.otherElements["aurea.parity.timeline"].firstMatch
        XCTAssertTrue(timeline.waitForExistence(timeout: 5))
        let cleared = try awaitSnapshot("Selection is cleared before the swipe") { $0.selectionCount == 0 }
        let frame = timeline.frame
        // First row, inside the clip body (bar at 50..76), away from its trim handles; ≈ 40° below horizontal.
        let start = CGPoint(x: frame.midX + 40, y: frame.minY + 62)
        coordinate(start).press(forDuration: 0.05, thenDragTo: coordinate(CGPoint(x: start.x + 48, y: start.y + 40)),
                                withVelocity: .slow, thenHoldForDuration: 0.1)
        RunLoop.current.run(until: Date().addingTimeInterval(0.5))
        let swiped = try snapshot()
        XCTAssertEqual(swiped.selectionCount, 0, "A slightly diagonal swipe must not select the layer under the finger")
        XCTAssertEqual(swiped.layerOrder, before.layerOrder)
        XCTAssertEqual(swiped.layerStarts, cleared.layerStarts, "A slightly diagonal swipe must not move the layer")
        // After a deliberate hold, a diagonal only scrolls: neither time nor stack is clear.
        coordinate(start).press(forDuration: 0.7, thenDragTo: coordinate(CGPoint(x: start.x + 40, y: start.y + 48)),
                                withVelocity: .slow, thenHoldForDuration: 0.1)
        RunLoop.current.run(until: Date().addingTimeInterval(0.5))
        let held = try snapshot()
        XCTAssertEqual(held.selectionCount, 0, "A diagonal after the hold must not select the layer")
        XCTAssertEqual(held.layerOrder, before.layerOrder, "A diagonal after the hold must not reorder")
        XCTAssertEqual(held.layerStarts, cleared.layerStarts, "A diagonal after the hold must not move the layer")
    }

    func testParentToNewNullLinksBothChosenLayersAndOneUndoRemovesIt() throws {
        let before = try launch("parent-new-null")
        let beforeIDs: Set<Int64> = Set(before.layerOrder)
        let open = app.buttons["link.open"].firstMatch
        XCTAssertTrue(open.waitForExistence(timeout: 5)); XCTAssertTrue(open.isHittable)
        open.tap()
        let row = app.buttons["link.newNull"].firstMatch
        XCTAssertTrue(row.waitForExistence(timeout: 5))
        XCTAssertGreaterThanOrEqual(row.frame.height, 44)
        row.tap()
        let linked = try awaitSnapshot("A new null is created and selected") {
            $0.layerCount == 3 && $0.selectionCount == 1
        }
        let nullID: Int64 = linked.primaryID
        XCTAssertFalse(beforeIDs.contains(nullID), "The selected layer must be the new null")
        let parents: [Int64] = linked.layerParents ?? []
        XCTAssertEqual(parents.count, linked.layerOrder.count)
        for index in 0..<min(parents.count, linked.layerOrder.count) {
            let id: Int64 = linked.layerOrder[index]
            let expected: Int64 = beforeIDs.contains(id) ? nullID : 0
            XCTAssertEqual(parents[index], expected, "Both chosen layers follow the new null; the null has no parent")
        }
        try undo()
        let restored = try awaitSnapshot("One undo removes the null and both links") {
            $0.layerCount == 2 && $0.canRedo
        }
        XCTAssertEqual(Set(restored.layerOrder), beforeIDs)
        let restoredParents: [Int64] = restored.layerParents ?? []
        XCTAssertEqual(restoredParents.count, 2)
        for parent in restoredParents { XCTAssertEqual(parent, 0) }
    }

    func testStaggerRowCascadesLayersInTimelineOrderAndOneUndoRestores() throws {
        let before = try launch("stagger")
        let starts: [Int64] = before.layerStarts ?? []
        XCTAssertEqual(starts.count, 3)
        XCTAssertEqual(Set(starts).count, 1, "The fixture starts all three layers together")
        let base: Int64 = starts.first ?? 0
        let step = app.staticTexts["stagger.step"].firstMatch
        XCTAssertTrue(step.waitForExistence(timeout: 5))
        let apply = app.buttons["stagger.layers"].firstMatch
        XCTAssertTrue(apply.waitForExistence(timeout: 5))
        if !apply.isHittable { app.scrollViews["timeline.batch.tools"].firstMatch.swipeUp() }
        XCTAssertTrue(apply.isHittable)
        XCTAssertGreaterThanOrEqual(apply.frame.height, 44)
        apply.tap()
        let staggered = try awaitSnapshot("Layers cascade by 3 frames in timeline order") {
            ($0.layerStarts ?? []) == [base, base + 3, base + 6]
        }
        XCTAssertEqual(staggered.layerOrder, before.layerOrder, "Staggering never reorders the stack")
        try undo()
        _ = try awaitSnapshot("One undo restores every start") {
            ($0.layerStarts ?? []) == starts && $0.canRedo
        }
        let keys = app.buttons["stagger.keys"].firstMatch
        XCTAssertTrue(keys.waitForExistence(timeout: 5))
        keys.tap()
        _ = try awaitSnapshot("Keys-only stagger keeps every bar in place") {
            ($0.layerStarts ?? []) == starts
        }
    }

    func testTimingArrangementDistributesUnequalClipsAndUndoesOnce() throws {
        let before = try launch("timeline-arrangement")
        XCTAssertEqual(before.layerStarts, [10, 12, 71])
        XCTAssertEqual(before.layerEnds, [20, 37, 78])
        let starts = app.buttons["timeline.arrange.3"].firstMatch
        XCTAssertTrue(starts.waitForExistence(timeout: 5))
        XCTAssertTrue(starts.isHittable)
        XCTAssertGreaterThanOrEqual(starts.frame.height, 44)
        starts.tap()
        let distributed = try awaitSnapshot("Starts are evenly spaced; durations are preserved") {
            $0.layerStarts == [10, 41, 71] && $0.layerEnds == [20, 66, 78]
        }
        XCTAssertEqual(distributed.layerOrder, before.layerOrder)
        try undo()
        _ = try awaitSnapshot("One undo restores all intervals") {
            $0.layerStarts == before.layerStarts && $0.layerEnds == before.layerEnds
        }
        let gaps = app.buttons["timeline.arrange.4"].firstMatch
        XCTAssertTrue(gaps.isHittable); gaps.tap()
        _ = try awaitSnapshot("Equal gaps preserve unequal clip lengths") {
            $0.layerStarts == [10, 33, 71] && $0.layerEnds == [20, 58, 78]
        }
        try undo()
        _ = try awaitSnapshot("Gap arrangement undoes once") {
            $0.layerStarts == before.layerStarts && $0.layerEnds == before.layerEnds
        }
    }

    func testTransformToolsKeepFingerSizedTargets() throws {
        _ = try launch("transform")
        let modes = app.descendants(matching: .any)
        let first = modes.matching(identifier: "aurea.panel.mode.0").firstMatch
        XCTAssertTrue(first.waitForExistence(timeout: 5))
        XCTAssertGreaterThanOrEqual(first.frame.width, 44)
        XCTAssertGreaterThanOrEqual(first.frame.height, 44)
        let tools = app.scrollViews["aurea.panel.tools"].firstMatch
        let last = modes.matching(identifier: "aurea.panel.mode.5").firstMatch
        if !last.isHittable && tools.exists { tools.swipeUp() }
        XCTAssertTrue(last.isHittable)
        XCTAssertGreaterThanOrEqual(last.frame.height, 44)
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.name = "Transform controls at usable size"; screenshot.lifetime = .keepAlways
        add(screenshot)
    }

    func testGizmoXDoesNotRewriteYOrZAnimation() throws {
        let before = try launch("curve-isolation")
        XCTAssertEqual(before.stageGizmo.count,8)
        let raw = stride(from:0,to:8,by:2).map {
            screenPoint(x:before.stageGizmo[$0],y:before.stageGizmo[$0+1],snapshot:before)
        }
        let extent = (1...3).map { hypot(raw[$0].x-raw[0].x,raw[$0].y-raw[0].y) }.max() ?? 0
        XCTAssertGreaterThan(extent,0)
        let start = CGPoint(x:raw[0].x+(raw[1].x-raw[0].x)*80/max(extent,0.001),
                            y:raw[0].y+(raw[1].y-raw[0].y)*80/max(extent,0.001))
        let end = CGPoint(x:start.x+30,y:start.y)
        try requireInsideStage(start,end)
        coordinate(start).press(forDuration:0.05,thenDragTo:coordinate(end),withVelocity:.slow,thenHoldForDuration:0.1)
        let changed = try awaitSnapshot("X gizmo changes X track") { $0.curveKeys != before.curveKeys }
        XCTAssertEqual(before.curveKeys.filter { $0.property != 0 },changed.curveKeys.filter { $0.property != 0 })
        try undo()
        _ = try awaitSnapshot("Undo restores gizmo animation") { $0.curveKeys == before.curveKeys }
    }

    /// Stage.kt gizmoGesture parity: girar/escala mexem só no eixo tocado,
    /// nunca na posição; o centro da escala é uniforme; um passo de desfazer.
    func testGizmoRotateAndScaleToolsEditOnlyTheirAxes() throws {
        let before = try launch("curve-null")
        let tool = app.buttons["stage.gizmo.tool"].firstMatch
        XCTAssertTrue(tool.waitForExistence(timeout: 5)); tool.tap()   // Mover -> Girar
        let rotating = try awaitSnapshot("Rotate tool shows the gizmo") { $0.stageGizmo.count == 8 }
        func handle(_ s: Snapshot, _ axis: Int) -> (CGPoint, CGPoint) {
            let raw = stride(from: 0, to: 8, by: 2).map { screenPoint(x: s.stageGizmo[$0], y: s.stageGizmo[$0 + 1], snapshot: s) }
            let extent = max((1...3).map { hypot(raw[$0].x - raw[0].x, raw[$0].y - raw[0].y) }.max() ?? 0, 0.001)
            let direction = CGPoint(x: (raw[axis + 1].x - raw[0].x) / extent, y: (raw[axis + 1].y - raw[0].y) / extent)
            return (CGPoint(x: raw[0].x + direction.x * 80, y: raw[0].y + direction.y * 80), raw[0])
        }
        let (xTip, _) = handle(rotating, 0)
        let xEnd = CGPoint(x: xTip.x, y: xTip.y + 60)
        try requireInsideStage(xTip, xEnd)
        coordinate(xTip).press(forDuration: 0.05, thenDragTo: coordinate(xEnd), withVelocity: .slow, thenHoldForDuration: 0.1)
        let rotated = try awaitSnapshot("Rotate X handle changes only Rotation X") {
            !$0.isManipulating && abs($0.detail.rotation[0] - before.detail.rotation[0]) > 5
        }
        XCTAssertEqual(rotated.detail.rotation[1], before.detail.rotation[1], accuracy: 0.0001)
        XCTAssertEqual(rotated.detail.rotation[2], before.detail.rotation[2], accuracy: 0.0001)
        XCTAssertEqual(rotated.detail.position, before.detail.position)
        XCTAssertEqual(rotated.detail.scale, before.detail.scale)
        try undo()
        _ = try awaitSnapshot("One undo restores rotation") { $0.detail.rotation == before.detail.rotation }

        tool.tap()   // Girar -> Escala
        let scaling = try awaitSnapshot("Scale tool shows the gizmo") { $0.stageGizmo.count == 8 }
        let (yTip, origin) = handle(scaling, 1)
        let along = CGPoint(x: (yTip.x - origin.x) / 80 * 50, y: (yTip.y - origin.y) / 80 * 50)
        let yEnd = CGPoint(x: yTip.x + along.x, y: yTip.y + along.y)
        try requireInsideStage(yTip, yEnd)
        coordinate(yTip).press(forDuration: 0.05, thenDragTo: coordinate(yEnd), withVelocity: .slow, thenHoldForDuration: 0.1)
        let scaledY = try awaitSnapshot("Scale Y handle grows only Scale Y") {
            !$0.isManipulating && $0.detail.scale[1] > before.detail.scale[1] + 0.05
        }
        XCTAssertEqual(scaledY.detail.scale[0], before.detail.scale[0], accuracy: 0.0001)
        XCTAssertEqual(scaledY.detail.scale[2], before.detail.scale[2], accuracy: 0.0001)
        XCTAssertEqual(scaledY.detail.position, before.detail.position)
        let centerEnd = CGPoint(x: origin.x + 60, y: origin.y)   // dx − dy > 0 cresce; fica dentro do preview
        try requireInsideStage(origin, centerEnd)
        coordinate(origin).press(forDuration: 0.05, thenDragTo: coordinate(centerEnd), withVelocity: .slow, thenHoldForDuration: 0.1)
        let uniform = try awaitSnapshot("Center square scales XY together (Z follows X)") {
            !$0.isManipulating && $0.detail.scale[0] > scaledY.detail.scale[0] + 0.05
        }
        let factor = uniform.detail.scale[0] / scaledY.detail.scale[0]
        XCTAssertEqual(uniform.detail.scale[1] / scaledY.detail.scale[1], factor, accuracy: 0.001)
        // Regra 3D (GestureMath.hpp): a escala Z é relativa ao X, então a escala
        // uniforme NÃO multiplica o Z guardado — a profundidade visível já cresce com o X.
        XCTAssertEqual(uniform.detail.scale[2], scaledY.detail.scale[2], accuracy: 0.0001)
        let initialDepth = scaledY.detail.scale[0] * scaledY.detail.scale[2]
        let scaledDepth = uniform.detail.scale[0] * uniform.detail.scale[2]
        XCTAssertEqual(scaledDepth / initialDepth, factor, accuracy: 0.001)
        XCTAssertEqual(uniform.detail.position, before.detail.position)
        try undo()
        _ = try awaitSnapshot("Undo uniform scale") { $0.detail.scale == scaledY.detail.scale }
        try undo()
        _ = try awaitSnapshot("Undo Y scale") { $0.detail.scale == before.detail.scale }
    }

    /// Cena 3D: arrastar um nulo com Posição ANIMADA grava keyframe no cabeçote
    /// (quadro 30) e deixa a pose do quadro 0 como estava. Antes a cena forçava
    /// "deslocar a curva inteira" e nenhum keyframe era marcado ali.
    func testDraggingAnimatedNullInSceneKeysThePlayheadAndKeepsFirstPose() throws {
        let before = try launch("scene-keyframe")
        XCTAssertEqual(before.corePlayhead, 30)
        let first = before.curveKeys.filter { $0.time == 0 }
        XCTAssertEqual(first.count, 3, "Position X/Y/Z are keyed at frame 0 by the fixture")
        XCTAssertEqual(before.stageGizmo.count, 8)
        // Cena 3D (build 2129, as duas plataformas): 1 dedo NO objeto gira; a seta
        // do gizmo continua movendo um eixo só. Arrasta a ponta da seta X, como
        // o usuário move o nulo com um dedo (projetada pela câmera da cena).
        let raw = stride(from: 0, to: 8, by: 2).map { screenPoint(x: before.stageGizmo[$0], y: before.stageGizmo[$0 + 1], snapshot: before) }
        let extent = max((1...3).map { hypot(raw[$0].x - raw[0].x, raw[$0].y - raw[0].y) }.max() ?? 0, 0.001)
        let direction = CGPoint(x: (raw[1].x - raw[0].x) / extent, y: (raw[1].y - raw[0].y) / extent)
        let tip = CGPoint(x: raw[0].x + direction.x * 80, y: raw[0].y + direction.y * 80)
        let end = CGPoint(x: tip.x + direction.x * 40, y: tip.y + direction.y * 40)
        try requireInsideStage(tip, end)
        coordinate(tip).press(forDuration: 0.05, thenDragTo: coordinate(end),
                             withVelocity: .slow, thenHoldForDuration: 0.1)
        // The projected X handle changes only world X. Auto-Key intentionally
        // skips unchanged axes rather than adding redundant Y/Z hold keys.
        let keyed = try awaitSnapshot("Dragging the animated null in the scene keys frame 30") {
            !$0.isManipulating && $0.curveKeys.contains { $0.property == 0 && $0.time == 30 }
        }
        let xKey = try XCTUnwrap(keyed.curveKeys.first { $0.property == 0 && $0.time == 30 })
        XCTAssertEqual(xKey.value, keyed.detail.position[0], accuracy: 0.01)
        XCTAssertGreaterThan(abs(keyed.detail.position[0] - before.detail.position[0]), 0.01)
        XCTAssertEqual(keyed.detail.position[1], before.detail.position[1], accuracy: 0.0001)
        XCTAssertEqual(keyed.detail.position[2], before.detail.position[2], accuracy: 0.0001)
        for property in [1, 2] {
            XCTAssertEqual(keyed.curveKeys.filter { $0.property == property },
                           before.curveKeys.filter { $0.property == property },
                           "Unchanged axis \(property) must keep its entire curve")
        }
        XCTAssertEqual(keyed.curveKeys.count, before.curveKeys.count + 1)
        XCTAssertEqual(keyed.curveKeys.filter { $0.time == 0 }, first, "The frame-0 pose must not move")
        try undo()
        _ = try awaitSnapshot("One undo removes the scene keyframe") { $0.curveKeys == before.curveKeys }
    }

    func testText3DSelectionFollowsCoreBoundsAndGizmoDrag() throws {
        let before = try launch("text-3d")
        // Model3D may have no projected core corners. Its displayed selection
        // must still enclose the real silhouette around the model pivot.
        // Read dimensions from the core; font/platform metrics are not fixed.
        try assertText3DSelectionBounds(before)
        XCTAssertEqual(before.stageGizmo.count, 8)
        let raw = stride(from: 0, to: 8, by: 2).map {
            screenPoint(x: before.stageGizmo[$0], y: before.stageGizmo[$0 + 1], snapshot: before)
        }
        let extent = (1...3).map { hypot(raw[$0].x - raw[0].x, raw[$0].y - raw[0].y) }.max() ?? 0
        XCTAssertGreaterThan(extent, 0)
        let start = CGPoint(x: raw[0].x + (raw[1].x - raw[0].x) * 80 / max(extent, 0.001),
                            y: raw[0].y + (raw[1].y - raw[0].y) * 80 / max(extent, 0.001))
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
        if ["video-move", "playback-stress", "clip-edit"].contains(scene) { app.launchEnvironment["AUREA_PARITY_EXPORT"] = "1" }
        let preparationTimeout: TimeInterval = ["video-move", "playback-stress", "clip-edit"].contains(scene) ? 120 : 30
        app.launch()
        guard stage.waitForExistence(timeout: preparationTimeout) else {
            XCTFail("Read-only DEBUG preview probe is missing; inspect the app configuration and INTEGRATION.md")
            throw ProbeError.missing
        }
        let state = try awaitSnapshot("Core fixture ready for \(scene)", timeout: preparationTimeout) {
            $0.ready && $0.scene == scene && $0.runID == self.runID
        }
        XCTAssertTrue(state.coreStarted, state.coreError)
        if scene == "playback-stress" { XCTAssertGreaterThanOrEqual(state.layerCount, 3) }
        else if scene == "timeline-reorder" { XCTAssertEqual(state.layerCount, 8) }
        else if scene == "manual-android-project" { XCTAssertEqual(state.layerCount, 14) }
        else if scene == "parent-new-null" { XCTAssertEqual(state.layerCount, 2) }
        else if scene == "null-link" { XCTAssertEqual(state.layerCount, 5) }
        else if scene == "stagger" || scene == "timeline-arrangement" { XCTAssertEqual(state.layerCount, 3) }
        else { XCTAssertEqual(state.layerCount, 1) }
        let expectedSelection: Int = scene == "parent-new-null" ? 2 : (["stagger", "timeline-arrangement"].contains(scene) ? 3 : 1)
        XCTAssertEqual(state.selectionCount, expectedSelection)
        XCTAssertGreaterThan(state.primaryID, 0)
        XCTAssertEqual(state.detail.position.count, 3)
        XCTAssertEqual(state.detail.scale.count, 3)
        XCTAssertEqual(state.detail.rotation.count, 3)
        XCTAssertEqual(state.detail.corners.count, 8)
        XCTAssertEqual(state.detail.sourceSize.count, 2)
        // "effects": camada sem efeito abre direto na tela cheia "Adicionar efeito",
        // que cobre o palco de propósito.
        if scene != "effects" { XCTAssertTrue(stage.isHittable) }
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
        let fit = min(frame.width / CGFloat(s.compositionWidth), frame.height / CGFloat(s.compositionHeight))
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
        let playbackReport: String
        let processFootprintBytes: Double
        let playing: UInt32
        let runID: String
        let scene: String
        let ready: Bool
        let coreStarted: Bool
        let coreError: String
        let layerCount: Int
        let layerOrder: [Int64]
        let layerParents: [Int64]?
        let layerStarts: [Int64]?
        let textAnimatorCount: Int?
        let textTransformOffset: [Double]?
        let layerNames: [String]?
        let layerCenters: [[Double]]?
        let layerEnds: [Int64]?
        let markerCount: Int
        let missingAssets: Int
        let curveKeys: [CurveKey]
        let editMode: Bool
        let coreEditMode: Bool
        let effectCount: Int
        let clipTimeRemap: [Double]
        let textGlyphLayout: Bool
        let textSurfaceFinish: Int
        let sheet: String
        let curveProperty: UInt32
        let curveParam: UInt32
        let primaryID: Int64
        let selectionCount: Int
        let keySelectionCount: Int?
        let keySelectMode: Bool?
        let isManipulating: Bool
        let canRedo: Bool
        let playhead: Int64
        let corePlayhead: Int64
        let previewBufferRanges: [[Int64]]?
        let motionBlurSettings: [Double]?
        let compositionWidth: Double
        let compositionHeight: Double
        let detail: Detail
        let publishedDetail: Detail?
        let shapeParams: [Double]
        let stageCorners: [Double]
        let stageGizmo: [Double]
    }
    private struct CurveKey: Decodable, Equatable {
        let property: Int
        let time: Int
        let interpolation: Int
        let value: Double
    }
    /// Sem camada escolhida o probe manda `detail` vazio ({}): os campos voltam
    /// com o padrão para o resto do snapshot (seleção, pilha, keyframes) ainda
    /// ser lido. `launch` segue exigindo a geometria real da camada escolhida.
    private struct Detail: Decodable {
        let animatedMask: UInt64
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
        private enum CodingKeys: String, CodingKey {
            case animatedMask, kind, startFrame, endFrame, localPlayhead, position, scale, rotation, corners, sourceSize, anchor
        }
        init(from decoder: Decoder) throws {
            let c = try decoder.container(keyedBy: CodingKeys.self)
            animatedMask = try c.decodeIfPresent(UInt64.self, forKey: .animatedMask) ?? 0
            kind = try c.decodeIfPresent(UInt32.self, forKey: .kind) ?? 0
            startFrame = try c.decodeIfPresent(Int64.self, forKey: .startFrame) ?? 0
            endFrame = try c.decodeIfPresent(Int64.self, forKey: .endFrame) ?? 0
            localPlayhead = try c.decodeIfPresent(Int64.self, forKey: .localPlayhead) ?? 0
            position = try c.decodeIfPresent([Double].self, forKey: .position) ?? []
            scale = try c.decodeIfPresent([Double].self, forKey: .scale) ?? []
            rotation = try c.decodeIfPresent([Double].self, forKey: .rotation) ?? []
            corners = try c.decodeIfPresent([Double].self, forKey: .corners) ?? []
            sourceSize = try c.decodeIfPresent([Double].self, forKey: .sourceSize) ?? []
            anchor = try c.decodeIfPresent([Double].self, forKey: .anchor) ?? []
        }
    }
}
