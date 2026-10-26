import SwiftUI
import UIKit

// =============================================================================
//  Omega aparência (área 05, P1-6): OPÇÕES DA CÂMERA
//  (`fragment_camera_options` da referência; par do `CameraOptionsPanel.kt`).
//
//  Moldura comum (`PanelFrame`, 200 pt) com três abas: **Visão** (perspectiva/
//  paralela, régua vertical curva do ângulo de visão, régua de zoom, "Afeta as
//  de baixo" e a dica), **Desfoque de foco** (o que era a face Lente do
//  Transformar: profundidade de campo, foco, abertura, intensidade, escolher o
//  foco no palco) e **Névoa** (ainda não existe por câmera no motor: aviso).
//
//  A câmera do AUREA guarda a DISTÂNCIA FOCAL (mm); ângulo e zoom saem dela
//  pelas contas de `CameraLensMath` (as mesmas do `CameraLensMath.kt`).
// =============================================================================

/// Contas puras da lente (porte do `editor/panels/CameraLensMath.kt`, objeto
/// `CameraLens`). A projeção segue a distância focal como o motor
/// (timeline/Layer.hpp): sensor full frame, FOV VERTICAL = 2·atan(24 / 2f),
/// focal presa em 4…1200 mm. O ângulo de visão e o zoom são LEITURAS da focal:
/// zoom 1× = a lente do FOV padrão do motor (50°, `CameraData::fov`).
enum CameraLensMath {
    static let sensorHeightMm: Float = 24
    static let focalMinMm: Float = 4
    static let focalMaxMm: Float = 1200
    /// Faixa do arrasto da focal (o teclado aceita `focalMinMm`…`focalMaxMm`).
    static let dragMinMm: Float = 8
    static let dragMaxMm: Float = 400
    static let apertureMin: Float = 1.2
    static let apertureMax: Float = 22
    static let blurMaxPercent: Float = 300
    /// FOV padrão da câmera nova do motor; zoom 1× = esta lente.
    static let defaultFov: Float = 50
    /// Lentes prontas (mm), das grandes-angulares às teleobjetivas.
    static let presetsMm: [Int] = [14, 18, 24, 35, 50, 85, 135, 200]

    /// FOV vertical (°) da distância focal; 0 se a focal não presta.
    static func fovFromFocal(_ mm: Float) -> Float {
        guard mm > 0, mm.isFinite else { return 0 }
        return Float(2 * atan(Double(sensorHeightMm) / 2 / Double(mm)) * 180 / .pi)
    }
    /// Faixa do ângulo que a focal do motor alcança (a referência vai de 0,1° a 179,9°): ~1,15°…143,1°.
    static var angleMin: Float { fovFromFocal(focalMaxMm) }
    static var angleMax: Float { fovFromFocal(focalMinMm) }
    /// Focal (mm) do FOV vertical (°): `12 / tan(fov/2)`; 0 se o ângulo não presta.
    static func focalFromFov(_ degrees: Float) -> Float {
        guard degrees.isFinite, degrees > 0, degrees < 180 else { return 0 }
        return Float(Double(sensorHeightMm) / 2 / tan(Double(degrees) / 2 * .pi / 180))
    }
    /// Focal para gravar: o ângulo preso na faixa do motor (nunca 0).
    static func focalForAngle(_ degrees: Float) -> Float {
        let clamped = min(angleMax, max(angleMin, degrees.isFinite ? degrees : defaultFov))
        return min(focalMaxMm, max(focalMinMm, focalFromFov(clamped)))
    }
    /// Focal padrão (a do FOV de 50°).
    static var defaultFocalMm: Float { focalFromFov(defaultFov) }
    /// Zoom (×) da focal: 1× é a lente do FOV padrão.
    static func zoomFromFocal(_ mm: Float) -> Float {
        let base = focalFromFov(defaultFov)
        return base > 0 && mm.isFinite ? mm / base : 1
    }
    /// Focal (mm) do zoom, presa na faixa do motor.
    static func focalFromZoom(_ zoom: Float) -> Float {
        guard zoom.isFinite else { return defaultFocalMm }
        return min(focalMaxMm, max(focalMinMm, zoom * focalFromFov(defaultFov)))
    }
    static var zoomMin: Float { zoomFromFocal(focalMinMm) }
    static var zoomMax: Float { zoomFromFocal(focalMaxMm) }
    /// "1,25×" (duas casas).
    static func formatZoom(_ zoom: Float) -> String { numeroPtBr(zoom, casas: 2) + "×" }
    /// "46,8" (o "°" vai no texto de quem chama).
    static func formatAngle(_ degrees: Float) -> String { numeroPtBr(degrees, casas: 1) }
    /// "50 mm".
    static func formatFocal(_ mm: Float) -> String { "\(Int(mm.rounded())) mm" }
    /// "f/2.8" (ponto, como nas lentes).
    static func formatAperture(_ f: Float) -> String { "f/" + numeroPtBr(f, casas: 1).replacingOccurrences(of: ",", with: ".") }
    /// O preset marcado é o que bate com a focal atual (meio mm de folga).
    static func presetIndex(_ mm: Float) -> Int { presetsMm.firstIndex { abs(Float($0) - mm) < 0.5 } ?? -1 }
}

/// O painel Opções da câmera (`AureaModel.PanelKind.camera`; o título da barra
/// é `om_ap_cam_title`, feito pela ToolbarView).
struct CameraOptionsPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var tab = "view"
    @State private var gestureOpen = false
    @State private var projectionOpen = false

    private var id: Int64 { model.primarySelection ?? 0 }
    private var isCamera: Bool { model.selectedLayer?.kind == 8 }
    private var locked: Bool { model.selectedLayer?.locked ?? false }
    private var lens: [Float] { model.cameraLens }
    private var ready: Bool { lens.count >= 9 }
    private var focal: Float { lens.count >= 1 ? lens[0] : CameraLensMath.defaultFocalMm }
    private var fov: Float { lens.count >= 2 ? lens[1] : CameraLensMath.fovFromFocal(focal) }
    private var dof: Bool { ready && lens[2] >= 0.5 }

    var body: some View {
        PanelFrame(rail: rail, tabs: tabs, selectedTab: tab, onTab: { next in endGesture(); tab = next },
                   scrollContent: false) {
            if !isCamera || model.cameraLens.isEmpty {
                // A camada escolhida não é uma câmera (ou a lente ainda não chegou).
                Text(AureaText.t("lens_msg_not_camera")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .multilineTextAlignment(.center).fixedSize(horizontal: false, vertical: true)
                    .padding(16)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .accessibilityIdentifier("camera.notCamera")
            } else {
                switch tab {
                case "focus": focusTab
                case "fog": fogTab
                default: viewTab
                }
            }
        }
        // Ids próprios das abas (além de `panel.tab.<i>` da moldura).
        .overlay { PanelTabTags(tags: ["camera.tab.view", "camera.tab.focus", "camera.tab.fog"]) }
        .id("camera:\(model.projectGeneration):\(id)")
        .foregroundStyle(AureaColors.text)
        .onAppear { updateTimelineFocus() }
        .onChange(of: id) { _ in endGesture(); updateTimelineFocus() }
        .onChange(of: tab) { _ in updateTimelineFocus() }
        .onChange(of: dof) { _ in updateTimelineFocus() }
        .onDisappear {
            endGesture()
            model.timelineFocus = nil
            if model.focusPick { model.cancelFocusPick() }
        }
    }

    private var tabs: [PanelTab] {
        [
            PanelTab(id: "view", label: AureaText.t("om_ap_cam_view"), glyph: CupertinoGlyph.Eye),
            PanelTab(id: "focus", label: AureaText.t("om_ap_cam_focus"), glyph: CupertinoGlyph.CameraViewfinder),
            PanelTab(id: "fog", label: AureaText.t("om_ap_cam_fog"), glyph: CupertinoGlyph.LineHorizontal3),
        ]
    }

    /// Parâmetros da lente que o ◆ da aba grava (Visão: focal = ângulo/zoom;
    /// Desfoque de foco: foco, abertura e intensidade, com a profundidade ligada).
    private var tabParams: [UInt32] {
        switch tab {
        case "view": return [AureaModel.cameraLensFocal]
        case "focus": return dof ? [AureaModel.cameraLensFocus, AureaModel.cameraLensAperture, AureaModel.cameraLensBlur] : []
        default: return []
        }
    }
    private var tabTracks: [UInt32] { tabParams.compactMap { model.cameraLensTrack($0) } }

    private var rail: PanelRail {
        let tracks = tabTracks
        let canKey = isCamera && ready && !tracks.isEmpty
        return PanelRail(
            onBack: { model.panel = .none },
            keyframeLook: look(tracks),
            onKeyframe: canKey ? { toggleKeys() } : nil,
            onDeleteAllKeyframes: canKey ? { deleteAllKeys(tracks) } : nil,
            onCurve: canKey ? { openCurve(tracks) } : nil,
            curveReady: tracks.contains { keys($0).count >= 2 })
    }

    private func updateTimelineFocus() {
        let tracks = tabTracks
        model.timelineFocus = tracks.isEmpty ? nil : tracks.map { TimelineTrack(property: Int($0)) }
    }

    // =========================================================================
    // Visão
    // =========================================================================

    private var viewTab: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                HStack(spacing: 8) {
                    Text(AureaText.t("om_ap_cam_view_title")).font(.aurea(size: 13, weight: .semibold))
                        .foregroundStyle(AureaColors.muted).lineLimit(1)
                        .accessibilityAddTraits(.isHeader)
                    Spacer(minLength: 0)
                    projectionButton
                }
                .frame(height: 37.5)
                .padding(.horizontal, 10)
                angleRow
                zoomRow.padding(.top, 8)
                presetChips.padding(.top, 6)
                cameraToggle("camera_affects_below", hint: "camera_affects_below_hint",
                             checked: lens.count > 9 && lens[9] >= 0.5, identifier: "camera.view.affectsBelow") { enabled in
                    beginGesture(); model.setCameraLens(AureaModel.cameraAffectsBelow, value: enabled ? 1 : 0); endGesture()
                }
                .padding(.horizontal, 6)
                Text(AureaText.t("om_ap_cam_tip")).font(.aurea(size: 11)).foregroundStyle(AureaColors.subtle)
                    .fixedSize(horizontal: false, vertical: true)
                    .padding(.horizontal, 10).padding(.top, 6)
                    .accessibilityIdentifier("camera.view.tip")
            }
            .padding(.bottom, 12)
        }
    }

    /// Perspectiva ⇄ paralela: menu com a Perspectiva marcada e a Paralela
    /// desabilitada (o motor só tem perspectiva; a paralela é P2-4).
    private var projectionButton: some View {
        Button { projectionOpen = true } label: {
            CupertinoGlyph.text(CupertinoGlyph.Cube, size: 18, color: AureaColors.accent)
                .frame(width: 33, height: 30)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 5))
                .frame(width: 44, height: 44)
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle(shrink: 1))
        .accessibilityLabel(AureaText.t("om_ap_cam_perspective"))
        .accessibilityHint(AureaText.t("om_ap_cam_parallel_soon"))
        .accessibilityIdentifier("camera.view.projection")
        .aureaAnchoredMenu(isPresented: $projectionOpen, alignEnd: true, tag: "camera.view.projection.menu") {
            [
                .item(AureaText.t("om_ap_cam_perspective"), glyph: CupertinoGlyph.Cube, selected: true, checked: true) {},
                .item(AureaText.t("om_ap_cam_parallel"), glyph: CupertinoGlyph.Square,
                      sublabel: AureaText.t("om_ap_cam_parallel_soon"), enabled: false),
            ]
        }
    }

    /// Régua vertical curva do ângulo de visão (presa à focal do motor:
    /// ~1,15…143,1°) + caixa 56 × 36.
    private var angleRow: some View {
        let angle = fov
        return HStack(spacing: 8) {
            CameraAngleRuler(angle: angle, enabled: !locked && ready,
                             onBegin: { beginGesture() },
                             onValue: { setAngle($0) },
                             onEnd: { endGesture() })
                .frame(height: 112)
                .frame(maxWidth: .infinity)
                .modifier(CameraRulerAccessibility(label: AureaText.t("om_ap_cam_view_angle"),
                                                   value: CameraLensMath.formatAngle(angle) + "°",
                                                   identifier: "camera.view.angle",
                                                   onTap: { angleKeypad() },
                                                   onStep: { up in beginGesture(); setAngle(fov + (up ? 1 : -1)); endGesture() }))
            cameraValueBox(CameraLensMath.formatAngle(angle) + "°", label: AureaText.t("om_ap_cam_view_angle"),
                           identifier: "camera.view.angle.value") { angleKeypad() }
        }
        .padding(.horizontal, 10)
    }
    /// Régua de zoom (36) + caixa 56 × 36; zoom 1× = a lente do FOV padrão (50°).
    private var zoomRow: some View {
        let zoom = CameraLensMath.zoomFromFocal(focal)
        let current: () -> Float = { CameraLensMath.zoomFromFocal(model.cameraLens.first ?? CameraLensMath.defaultFocalMm) }
        return HStack(spacing: 8) {
            Text(AureaText.t("om_ap_cam_zoom")).font(.aurea(size: 11, weight: .semibold)).foregroundStyle(AureaColors.muted)
                .lineLimit(1).minimumScaleFactor(0.7).frame(width: 44, alignment: .leading)
                .accessibilityHidden(true)
            GeometryReader { bounds in
                TickRuler(value: current, unitsPerDp: 0.02, active: true, height: bounds.size.height)
                    .valueDrag(enabled: !locked && ready, start: current, unitsPerDp: { 0.02 },
                               min: CameraLensMath.zoomMin, max: CameraLensMath.zoomMax,
                               onStart: { beginGesture() },
                               onValue: { model.setCameraLens(AureaModel.cameraLensFocal, value: CameraLensMath.focalFromZoom($0)) },
                               onEnd: { endGesture() })
            }
            .frame(height: 36)
            .modifier(CameraRulerAccessibility(label: AureaText.t("om_ap_cam_zoom"),
                                               value: CameraLensMath.formatZoom(zoom) + ", " + CameraLensMath.formatFocal(focal),
                                               identifier: "camera.view.zoom",
                                               onTap: { zoomKeypad() },
                                               onStep: { up in
                                                   beginGesture()
                                                   model.setCameraLens(AureaModel.cameraLensFocal,
                                                                       value: CameraLensMath.focalFromZoom(zoom + (up ? 0.1 : -0.1)))
                                                   endGesture()
                                               }))
            cameraValueBox(CameraLensMath.formatZoom(zoom), label: AureaText.t("om_ap_cam_zoom"),
                           identifier: "camera.view.zoom.value") { zoomKeypad() }
        }
        .padding(.horizontal, 10)
    }

    /// Lentes prontas em mm (extra do AUREA, era da face Lente).
    private var presetChips: some View {
        let picked = CameraLensMath.presetIndex(focal)
        return ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                Text(CameraLensMath.formatFocal(focal)).font(.aurea(size: 11.5)).foregroundStyle(AureaColors.muted)
                    .padding(.leading, 4).padding(.trailing, 4)
                ForEach(Array(CameraLensMath.presetsMm.enumerated()), id: \.offset) { index, mm in
                    let on = index == picked
                    Button {
                        beginGesture(); model.setCameraLens(AureaModel.cameraLensFocal, value: Float(mm)); endGesture()
                    } label: {
                        Text("\(mm)").font(.aurea(size: 12.5, weight: .bold))
                            .foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                            .padding(.horizontal, 12).frame(minHeight: 36)
                            .background(on ? AureaColors.accent.opacity(0.18) : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                            .frame(minHeight: 44).contentShape(Rectangle())
                    }
                    .buttonStyle(.plain)
                    .disabled(locked || !ready)
                    .accessibilityLabel(CameraLensMath.formatFocal(Float(mm)))
                    .accessibilityAddTraits(on ? .isSelected : [])
                    .accessibilityIdentifier("camera.view.preset.\(mm)")
                }
            }
            .padding(.horizontal, 6)
        }
    }

    private func setAngle(_ degrees: Float) {
        guard degrees.isFinite else { return }
        let clamped = min(CameraLensMath.angleMax, max(CameraLensMath.angleMin, degrees))
        model.setCameraLens(AureaModel.cameraLensFocal, value: CameraLensMath.focalForAngle(clamped))
    }
    private func angleKeypad() {
        guard !locked, ready else { return }
        keypad(AureaText.t("om_ap_cam_view_angle"), fov, unit: "°", min: CameraLensMath.angleMin, max: CameraLensMath.angleMax,
               decimals: 1) { setAngle($0) }
    }
    private func zoomKeypad() {
        guard !locked, ready else { return }
        keypad(AureaText.t("om_ap_cam_zoom"), CameraLensMath.zoomFromFocal(focal), unit: "×",
               min: CameraLensMath.zoomMin, max: CameraLensMath.zoomMax, decimals: 2) {
            model.setCameraLens(AureaModel.cameraLensFocal, value: CameraLensMath.focalFromZoom($0))
        }
    }

    // =========================================================================
    // Desfoque de foco (a antiga face Lente do Transformar)
    // =========================================================================

    private var focusTab: some View {
        let ready = self.ready
        let focus: Float = ready ? lens[3] : 0
        let aperture: Float = ready ? lens[4] : 2.8
        let blur: Float = ready ? lens[5] * 100 : 100
        let perMeter: Float = ready ? lens[6] : 50
        // 1 pt de dedo ≈ 2 cm da cena (px por metro vem do motor).
        let focusSpeed: Float = perMeter > 0 && perMeter.isFinite ? perMeter / 50 : 1
        let armed: Bool = model.focusPick
        return ScrollView {
            VStack(alignment: .leading, spacing: 0) {
                HStack(spacing: 10) {
                    Text(AureaText.t("om_ap_cam_focus")).font(.aurea(size: 13, weight: .semibold))
                        .foregroundStyle(AureaColors.muted).lineLimit(1)
                        .frame(maxWidth: .infinity, alignment: .leading)
                    AureaToggle(checked: dof) { enabled in
                        guard !locked, ready else { return }
                        beginGesture(); model.setCameraLens(AureaModel.cameraLensDof, value: enabled ? 1 : 0); endGesture()
                    }
                    .accessibilityLabel(AureaText.t("om_ap_cam_focus_on"))
                    .accessibilityIdentifier("camera.focus.switch")
                }
                .frame(height: 37.5)
                .padding(.horizontal, 10)
                Text(AureaText.t("lens_dof_hint")).font(.aurea(size: 11.5)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
                    .padding(.horizontal, 10).padding(.bottom, 4)
                if dof {
                    VStack(alignment: .leading, spacing: 0) {
                        lensRow(AureaText.t("lens_focus_distance"), amount: focus, speed: focusSpeed, lower: 0, upper: .infinity,
                                text: numeroPtBr(focus, casas: 0) + " px", param: AureaModel.cameraLensFocus,
                                identifier: "camera.focus.distance",
                                onValue: { model.setCameraLens(AureaModel.cameraLensFocus, value: max(0, $0)) }, onTap: {
                            keypad(AureaText.t("lens_focus_distance"), focus, unit: "px", min: 0, decimals: 0) {
                                model.setCameraLens(AureaModel.cameraLensFocus, value: max(0, $0))
                            }
                        })
                        lensRow(AureaText.t("lens_aperture"), amount: aperture, speed: 0.05,
                                lower: CameraLensMath.apertureMin, upper: CameraLensMath.apertureMax,
                                text: CameraLensMath.formatAperture(aperture), param: AureaModel.cameraLensAperture,
                                identifier: "camera.focus.aperture",
                                onValue: { model.setCameraLens(AureaModel.cameraLensAperture,
                                                               value: min(CameraLensMath.apertureMax, max(CameraLensMath.apertureMin, $0))) }, onTap: {
                            keypad(AureaText.t("lens_aperture"), aperture, unit: "f/", min: CameraLensMath.apertureMin,
                                   max: CameraLensMath.apertureMax, decimals: 1) {
                                model.setCameraLens(AureaModel.cameraLensAperture, value: $0)
                            }
                        })
                        lensRow(AureaText.t("lens_blur"), amount: blur, speed: 1, lower: 0, upper: CameraLensMath.blurMaxPercent,
                                text: numeroPtBr(blur, casas: 0) + "%", param: AureaModel.cameraLensBlur,
                                identifier: "camera.focus.blur",
                                onValue: { model.setCameraLens(AureaModel.cameraLensBlur, value: min(CameraLensMath.blurMaxPercent, max(0, $0)) / 100) }, onTap: {
                            keypad(AureaText.t("lens_blur"), blur, unit: "%", min: 0, max: CameraLensMath.blurMaxPercent, decimals: 0) {
                                model.setCameraLens(AureaModel.cameraLensBlur, value: $0 / 100)
                            }
                        })
                        // "Tocar para focar": ≥ 44 pt; arma o próximo toque no palco.
                        Button {
                            if armed { model.cancelFocusPick() } else { model.armPickFocus() }
                        } label: {
                            HStack(spacing: 10) {
                                CupertinoGlyph.text(CupertinoGlyph.Viewfinder, size: 18, color: armed ? AureaColors.accent : AureaColors.text)
                                VStack(alignment: .leading, spacing: 0) {
                                    Text(AureaText.t("lens_pick_focus")).font(.aurea(size: 14, weight: .semibold))
                                        .foregroundStyle(armed ? AureaColors.accent : AureaColors.text)
                                    Text(AureaText.t("lens_pick_focus_hint")).font(.aurea(size: 11.5)).foregroundStyle(AureaColors.muted)
                                }
                                Spacer()
                            }
                            .padding(.horizontal, 14).padding(.vertical, 8).frame(maxWidth: .infinity, minHeight: 48)
                            .background(armed ? AureaColors.accent.opacity(0.18) : AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
                        }
                        .buttonStyle(.plain)
                        .disabled(locked)
                        .padding(.top, 8)
                        .accessibilityIdentifier("camera.focus.pick")
                    }
                    .padding(.leading, 8).padding(.trailing, 12)
                }
            }
            .padding(.bottom, 12)
        }
    }

    /// Linha de parâmetro da lente: rótulo (toque = ◆ do parâmetro), régua e caixa.
    private func lensRow(_ label: String, amount: Float, speed: Float, lower: Float, upper: Float, text: String,
                         param: UInt32, identifier: String,
                         onValue: @escaping (Float) -> Void, onTap: @escaping () -> Void) -> some View {
        let keyframe = lensLook(param)
        return HStack(spacing: 8) {
            ZStack(alignment: .topLeading) {
                Text(label).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.accent).underline()
                    .multilineTextAlignment(.center).lineLimit(2).frame(width: 94, height: 32)
                    .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                if keyframe != .none {
                    CameraMiniDiamond(filled: keyframe == .keyHere).frame(width: 7, height: 7).offset(x: 3, y: 3)
                }
            }
            .frame(minHeight: 44)
            .contentShape(Rectangle())
            .onTapGesture { if !locked { model.toggleCameraLensKey(param) } }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(label)
            .accessibilityValue(AureaText.t(keyframe == .keyHere ? "panel_tirar_keyframe_daqui" : "panel_marcar_keyframe_aqui"))
            .accessibilityAddTraits(.isButton)
            .accessibilityAction { if !locked { model.toggleCameraLensKey(param) } }
            TickRuler(value: { amount }, unitsPerDp: speed, active: true, height: 40)
                .frame(maxWidth: .infinity)
                .valueDrag(enabled: !locked, start: { amount }, unitsPerDp: { speed }, min: lower, max: upper,
                           onStart: { beginGesture() }, onValue: onValue, onEnd: { endGesture() })
                .accessibilityElement(children: .ignore)
                .accessibilityLabel(label)
                .accessibilityValue(text)
                .accessibilityIdentifier(identifier)
            ValueBox(text, width: 68, onTap: locked ? nil : onTap)
        }
        .frame(height: 48)
    }

    // =========================================================================
    // Névoa
    // =========================================================================

    private var fogTab: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(AureaText.t("om_ap_cam_fog")).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.muted)
                .frame(height: 37.5)
                .accessibilityAddTraits(.isHeader)
            Text(AureaText.t("om_ap_cam_fog_soon")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
                .accessibilityIdentifier("camera.fog.notice")
            Spacer(minLength: 0)
        }
        .padding(.horizontal, 10)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }

    // =========================================================================
    // Keyframes, gestos, teclado
    // =========================================================================

    private func keys(_ property: UInt32) -> [KeyframeItem] {
        (model.keyframes[id] ?? []).filter { $0.property == property && $0.effectIndex == UInt32.max }.sorted { $0.time < $1.time }
    }
    private func look(_ tracks: [UInt32]) -> KeyframeLook {
        let local = model.localPlayhead
        if tracks.contains(where: { keys($0).contains { $0.time == local } }) { return .keyHere }
        return tracks.contains { !keys($0).isEmpty } ? .animated : .none
    }
    private func lensLook(_ param: UInt32) -> KeyframeLook {
        guard let property = model.cameraLensTrack(param) else { return .none }
        return look([property])
    }
    private func lensValue(_ param: UInt32) -> Float {
        guard ready else { return 0 }
        switch param {
        case AureaModel.cameraLensFocal: return lens[0]
        case AureaModel.cameraLensFocus: return lens[3]
        case AureaModel.cameraLensAperture: return lens[4]
        default: return lens[5]
        }
    }
    /// ◆ da aba: marca (ou tira, se já há marca no cabeçote) todos os parâmetros dela num passo.
    private func toggleKeys() {
        guard !locked, ready else { return }
        let local = model.localPlayhead, layer = id
        let entries: [(property: UInt32, value: Float)] = tabParams.compactMap { param in
            model.cameraLensTrack(param).map { (property: $0, value: lensValue(param)) }
        }
        guard !entries.isEmpty else { return }
        let here = entries.filter { entry in keys(entry.property).contains { $0.time == local } }.map { $0.property }
        model.mutate { core in
            core.beginUndoGroup()
            for entry in entries {
                if !here.isEmpty {
                    if here.contains(entry.property) { core.deleteKeyframe(forLayer: layer, property: entry.property, time: local) }
                } else {
                    core.insertKeyframe(forLayer: layer, property: entry.property, time: local, value: entry.value)
                }
            }
            core.endUndoGroup()
        }
        model.refreshModel(force: true)
    }
    /// Segurar ◆: apaga os keyframes da aba num passo de desfazer.
    private func deleteAllKeys(_ tracks: [UInt32]) {
        guard !locked else { return }
        let all = tracks.flatMap { keys($0) }
        guard !all.isEmpty else { return }
        let layer = id
        model.beginGesture("apagar keyframes")
        model.mutate { core in for key in all { core.deleteKeyframe(forLayer: layer, property: key.property, time: key.time) } }
        model.endGesture()
    }
    private func openCurve(_ tracks: [UInt32]) {
        guard let property = tracks.first(where: { keys($0).count >= 2 }) ?? tracks.first else { return }
        let curve = keys(property)
        guard let first = curve.first else { return }
        let local = model.localPlayhead
        let segment = curve.dropLast().last { $0.time <= local } ?? first
        model.openCurve(property: property, time: segment.time)
    }
    private func beginGesture() {
        guard !gestureOpen else { return }
        // A edição fica presa num instante da timeline durante o arrasto.
        model.engine.run { $0.pause() }
        model.beginGesture("lente da câmera")
        gestureOpen = true
    }
    private func endGesture() {
        guard gestureOpen else { return }
        gestureOpen = false
        model.endGesture()
    }
    private func keypad(_ title: String, _ value: Float, unit: String, min: Float = -.infinity, max: Float = .infinity,
                        decimals: Int, onValue: @escaping (Float) -> Void) {
        let project = model.projectGeneration, layer = id
        model.numericKeypad = KeypadRequest(title: title, value: value, unit: unit, min: min, max: max, decimals: decimals) { amount in
            guard model.projectGeneration == project, model.primarySelection == layer else { return }
            onValue(amount)
        }
    }

    /// Caixa de valor 56 × 36 (a da referência), número em destaque sublinhado; toque digita.
    private func cameraValueBox(_ text: String, label: String, identifier: String, onTap: @escaping () -> Void) -> some View {
        Button(action: onTap) {
            Text(text).font(.aurea(size: 13, weight: .semibold).monospacedDigit())
                .foregroundStyle(locked ? AureaColors.muted : AureaColors.accent).underline(!locked)
                .lineLimit(1).minimumScaleFactor(0.6).padding(.horizontal, 3)
                .frame(width: 56, height: 36)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 5))
                .frame(minHeight: 44).contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .disabled(locked)
        .keepLtr()
        .accessibilityLabel(label)
        .accessibilityValue(text)
        .accessibilityIdentifier(identifier)
    }

    private func cameraToggle(_ title: String, hint: String, checked: Bool, identifier: String,
                              onChange: @escaping (Bool) -> Void) -> some View {
        HStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t(title)).font(.aurea(size: 14, weight: .semibold))
                Text(AureaText.t(hint)).font(.aurea(size: 11.5)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
            }.frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: checked) { if !locked { onChange($0) } }
                .accessibilityLabel(AureaText.t(title))
                .accessibilityIdentifier(identifier)
        }
        .padding(.vertical, 6).padding(.leading, 4)
    }
}

/// A régua VERTICAL CURVA do ângulo de visão: um arco em volta da câmera, com
/// os riscos andando junto do dedo (arrastar para cima abre o ângulo) e as
/// duas linhas do cone de visão. 1 pt de dedo = 0,25°.
private struct CameraAngleRuler: View {
    let angle: Float
    let enabled: Bool
    let onBegin: () -> Void
    let onValue: (Float) -> Void
    let onEnd: () -> Void
    @State private var start: Float?

    static let degreesPerPoint: Float = 0.25

    init(angle: Float, enabled: Bool, onBegin: @escaping () -> Void, onValue: @escaping (Float) -> Void,
         onEnd: @escaping () -> Void) {
        self.angle = angle; self.enabled = enabled
        self.onBegin = onBegin; self.onValue = onValue; self.onEnd = onEnd
    }

    var body: some View {
        Canvas { context, size in
            let center = CGPoint(x: 18, y: size.height / 2)
            let radius = max(20, size.width - 26)
            let v = angle.isFinite ? angle : 0
            // Cone de visão (metade do ângulo para cada lado, até 75° no desenho).
            let half = Double(min(75, max(0.5, v / 2))) * .pi / 180
            var cone = Path()
            cone.move(to: center)
            cone.addLine(to: CGPoint(x: center.x + CGFloat(cos(half)) * radius * 0.92, y: center.y - CGFloat(sin(half)) * radius * 0.92))
            cone.move(to: center)
            cone.addLine(to: CGPoint(x: center.x + CGFloat(cos(half)) * radius * 0.92, y: center.y + CGFloat(sin(half)) * radius * 0.92))
            context.stroke(cone, with: .color(AureaColors.muted.opacity(0.45)), style: StrokeStyle(lineWidth: 1, dash: [3, 3]))
            // O arco e os riscos: o valor `t` cai no ângulo (t − v) / (°/pt) / raio,
            // os maiores EMBAIXO — arrastar para cima traz os maiores à agulha e os
            // riscos andam junto do dedo (como a régua horizontal).
            let span = min(Double(size.height / 2 - 6) / Double(radius), 1.2)
            var arc = Path()
            arc.addArc(center: center, radius: radius, startAngle: .radians(-span), endAngle: .radians(span), clockwise: false)
            context.stroke(arc, with: .color(AureaColors.dialTrack), lineWidth: 2)
            let perRadian = Double(CameraAngleRuler.degreesPerPoint) * Double(radius)   // graus por radiano de arco
            let low = max(0, Int((Double(v) - span * perRadian).rounded(.down)))
            let high = min(180, Int((Double(v) + span * perRadian).rounded(.up)))
            if high > low && high - low < 2000 {
                for t in low...high {
                    let phi = (Double(t) - Double(v)) / perRadian
                    guard abs(phi) <= span else { continue }
                    let major = t % 10 == 0
                    let length: CGFloat = major ? 12 : (t % 5 == 0 ? 8 : 5)
                    let fade = 1 - abs(phi) / span
                    let c = CGFloat(cos(phi)), s = CGFloat(sin(phi))
                    var tick = Path()
                    tick.move(to: CGPoint(x: center.x + c * (radius - length), y: center.y + s * (radius - length)))
                    tick.addLine(to: CGPoint(x: center.x + c * radius, y: center.y + s * radius))
                    context.stroke(tick, with: .color(AureaColors.text.opacity(0.25 + 0.6 * fade)), lineWidth: major ? 1.5 : 1)
                }
            }
            // Agulha no centro (o valor atual) e a câmera.
            var needle = Path()
            needle.move(to: CGPoint(x: center.x + radius - 18, y: center.y))
            needle.addLine(to: CGPoint(x: center.x + radius + 3, y: center.y))
            context.stroke(needle, with: .color(enabled ? AureaColors.accent : AureaColors.muted), style: StrokeStyle(lineWidth: 2, lineCap: .round))
            let box = CGRect(x: center.x - 14, y: center.y - 7, width: 14, height: 14)
            context.fill(Path(roundedRect: box, cornerRadius: 3), with: .color(AureaColors.text.opacity(0.85)))
            var lensShape = Path()
            lensShape.move(to: CGPoint(x: center.x, y: center.y - 4))
            lensShape.addLine(to: CGPoint(x: center.x + 6, y: center.y - 7))
            lensShape.addLine(to: CGPoint(x: center.x + 6, y: center.y + 7))
            lensShape.addLine(to: CGPoint(x: center.x, y: center.y + 4))
            lensShape.closeSubpath()
            context.fill(lensShape, with: .color(AureaColors.text.opacity(0.85)))
        }
        .contentShape(Rectangle())
        .gesture(DragGesture(minimumDistance: 4)
            .onChanged { event in
                guard enabled else { return }
                if start == nil { start = angle.isFinite ? angle : 0; onBegin() }
                let next = (start ?? angle) - Float(event.translation.height) * CameraAngleRuler.degreesPerPoint
                onValue(min(CameraLensMath.angleMax, max(CameraLensMath.angleMin, next)))
            }
            .onEnded { _ in
                if start != nil { start = nil; onEnd() }
            })
        .onDisappear { if start != nil { start = nil; onEnd() } }
    }
}

/// Acessibilidade das réguas da câmera: um elemento ajustável (±) cujo toque
/// padrão abre o teclado; o id é o da régua (`camera.view.angle` / `.zoom`).
private struct CameraRulerAccessibility: ViewModifier {
    let label: String
    let value: String
    let identifier: String
    let onTap: () -> Void
    let onStep: (Bool) -> Void
    func body(content: Content) -> some View {
        content
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(label)
            .accessibilityValue(value)
            .accessibilityAction { onTap() }
            .accessibilityAdjustableAction { direction in
                switch direction {
                case .increment: onStep(true)
                case .decrement: onStep(false)
                @unknown default: break
                }
            }
            .accessibilityIdentifier(identifier)
    }
}

private struct CameraMiniDiamond: View {
    let filled: Bool
    var body: some View {
        Canvas { context, size in
            var path = Path()
            path.move(to: CGPoint(x: size.width / 2, y: 0)); path.addLine(to: CGPoint(x: size.width, y: size.height / 2))
            path.addLine(to: CGPoint(x: size.width / 2, y: size.height)); path.addLine(to: CGPoint(x: 0, y: size.height / 2)); path.closeSubpath()
            if filled { context.fill(path, with: .color(AureaColors.keyframe)) }
            else { context.stroke(path, with: .color(AureaColors.keyframe), lineWidth: 1) }
        }
    }
}
