import SwiftUI
import AVFoundation
import UniformTypeIdentifiers
import Security
import CryptoKit
import os

struct NativeCaptionBlock: Codable, Equatable, Identifiable {
    var id: Int64; var start: Int32; var end: Int32; var text: String
}
struct NativeCaptionTrack: Codable, Equatable {
    var layer: Int64; var source: Int64; var segments: [NativeCaptionBlock]
}

@MainActor
struct CaptionBlockEditor: View {
    @EnvironmentObject private var model: AureaModel
    let track: NativeCaptionTrack
    @State private var selected: Set<Int64> = []
    @State private var text = ""
    @State private var start = ""
    @State private var end = ""
    @State private var error: String?
    private func edit(_ op: String, _ fields: [String: Any] = [:]) {
        var value = fields; value["op"] = op; value["ids"] = Array(selected)
        guard let data = try? JSONSerialization.data(withJSONObject: value), let command = String(data: data, encoding: .utf8) else { return }
        if model.engine.editCaptionTrack(track.layer, command: command) { error = nil; model.refreshModel(force: true) }
        else { error = AureaText.t("ios_caption_edit_invalid") }
    }
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(AureaText.t("ios_caption_track_hint"))
            ScrollView(.horizontal) {
                HStack {
                    ForEach(track.segments) { block in
                        Button { if selected.contains(block.id) { selected.remove(block.id) } else { selected.insert(block.id) }; text = block.text; start = String(block.start); end = String(block.end) } label: {
                            Text("\(ltrIsolado("\(block.start)–\(block.end)"))\n\(block.text)").lineLimit(2).padding(8).background(selected.contains(block.id) ? AureaColors.accent.opacity(0.5) : AureaColors.muted.opacity(0.15)).clipShape(RoundedRectangle(cornerRadius: 8))
                        }.buttonStyle(.plain)
                    }
                }
            }
            if !selected.isEmpty {
                HStack {
                    Button(AureaText.t("edt_cap_back_frame")) { edit("move", ["delta": -1]) }
                    Button(AureaText.t("edt_cap_fwd_frame")) { edit("move", ["delta": 1]) }
                    Button(AureaText.t("dock_short_split")) { edit("split", ["frame": model.status.playhead]) }.disabled(selected.count != 1)
                    Button(AureaText.t("panel_unir")) { edit("merge"); selected.removeAll() }.disabled(selected.count < 2)
                    Button(AureaText.t("common_delete")) { edit("delete"); selected.removeAll() }
                }
                if selected.count == 1 {
                    TextField(AureaText.t("panel_texto"), text: $text).textFieldStyle(.roundedBorder)
                    Button(AureaText.t("ios_caption_apply_text")) { edit("text", ["text": text]) }
                    HStack { TextField(AureaText.t("ios_caption_start_frame"), text: $start); TextField(AureaText.t("ios_caption_end_frame"), text: $end) }.textFieldStyle(.roundedBorder).keyboardType(.numberPad)
                    Button(AureaText.t("ios_caption_adjust_duration")) { if let a = Int(start), let b = Int(end) { edit("trim", ["start": a, "end": b]) } }
                }
                Button(AureaText.t("editor_limpar_selecao")) { selected.removeAll() }
            }
            if let error { Text(error).foregroundStyle(.red) }
        }.padding(.vertical, 10)
    }
}

@MainActor
struct TrackingPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var mode: UInt32 = 1
    @State private var status: [String: Any] = [:]
    @State private var sourceLayer: Int64? = nil
    @State private var advanced = false
    @State private var cameraMotion: UInt32 = 0
    @State private var knownFov: Float = 0
    @State private var distanceText = "100"
    @State private var pointDisplay = false
    @State private var calibration = false
    @State private var objectKind: UInt32 = 1
    /// 0.3.0 (TrackingPanel.kt): tipo de tomada (0 fixo, 1 zoom, 2 informado) e estimar a lente.
    @State private var shotType: UInt32 = 0
    @State private var lens = false
    private let objectTitles = ["cam_create_null_anchor", "cam_create_camera_shape", "cam_create_camera_text", "cam_create_camera_solid"]
    private let timer = Timer.publish(every: 0.4, on: .main, in: .common).autoconnect()
    private var state: Int { (status["state"] as? NSNumber)?.intValue ?? 0 }
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_rastreio"), onBack: { model.panel = .none })
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    Picker(AureaText.t("panel_rastreio"), selection: $model.cameraTrackerVisible) {
                        Text(AureaText.t("cam_mode_2d")).tag(false)
                        Text(AureaText.t("panel_camera_3d")).tag(true)
                    }.pickerStyle(.segmented).frame(minHeight: 44)
                    Spacer().frame(height: 16)
                    if model.cameraTrackerVisible { cameraSection } else { MotionTrackingSection() }
                }.padding(.init(top: 12, leading: 18, bottom: 24, trailing: 18))
            }
        }.foregroundStyle(AureaColors.text).background(AureaColors.editorPanel)
            .onAppear { restore(); reload() }.onReceive(timer) { _ in reload() }
            .onChange(of: model.primarySelection) { _ in restore(); reload() }
            .onDisappear { model.cameraFeatures = []; model.cameraSelection = nil }
    }
    @ViewBuilder private var cameraSection: some View {
        Text(AureaText.t(state == 2 ? "cam_step_place" : "cam_step_analyze")).font(.aurea(size: 17, weight: .bold))
        Spacer().frame(height: 6)
        if state != 1 && state != 2 {
            qualityPicker
            solveOptions
        }
        Spacer().frame(height: 8)
        if state == 1 {
            let progress = number("progress").clamped(to: 0...1)
            Text(AureaText.t("ios_analyzing_motion_pct", Int(progress * 100))).font(.aurea(size: 13))
            Spacer().frame(height: 6)
            GeometryReader { geometry in
                ZStack(alignment: .leading) {
                    AureaColors.chip
                    AureaColors.accent.frame(width: geometry.size.width * CGFloat(progress))
                }.clipShape(RoundedRectangle(cornerRadius: 3))
            }.frame(height: 6)
            Spacer().frame(height: 8)
            action("panel_cancelar", "panel_analise_projeto_nao_muda") { model.engine.cancelCameraTracking(); reload() }
        } else if state == 2 {
            let kind = AureaText.t(flag("rotationOnly") ? "panel_camera_so_gira_lugar_sem_profundidade" : "panel_movimento_camera_encontrado")
            Text(kind + (flag("cached") ? AureaText.t("panel_analise_guardada") : ""))
                .font(.aurea(size: 13, weight: .semibold))
            Spacer().frame(height: 4)
            let quality = AureaText.t(number("solved") != number("frames") || number("error") > 2 ? "cam_quality_poor" : number("error") > 1 ? "cam_quality_fair" : number("error") > 0.5 ? "cam_quality_good" : "cam_quality_excellent")
            note(AureaText.t("cam_solve_quality", quality, model.cameraSelectedCount))
            // 0.3.0: o erro médio em px da fonte (o "Average Error" do AE), a faixa do zoom e a lente.
            note(solveSummary).accessibilityIdentifier("cam.average_error")
            Spacer().frame(height: 8)
            action("panel_criar_camera", "cam_create_camera_desc") { model.createTrackedObject(0); reload() }
            if flag("lensSolved") {
                Spacer().frame(height: 6)
                action("cam_lens_remove", "cam_lens_remove_desc") { model.addCameraLensEffect(redistort: false, trackedLayer: sourceLayer) }
                Spacer().frame(height: 6)
                action("cam_lens_apply", "cam_lens_apply_desc") { model.addCameraLensEffect(redistort: true, trackedLayer: sourceLayer) }
            }
            if !flag("rotationOnly") {
                note(AureaText.t("cam_tap_hint")).padding(.top, 12)
            }
            Toggle(AureaText.t("cam_multi_select"), isOn: $model.cameraMultiSelect).font(.aurea(size: 13)).tint(AureaColors.accent)
            if !flag("rotationOnly") {
                Menu {
                    ForEach(Array(objectTitles.enumerated()), id: \.offset) { index, title in
                        Button(AureaText.t(title)) { objectKind = UInt32(index + 1) }
                    }
                } label: {
                    HStack { Text(AureaText.t(objectTitles[Int(objectKind) - 1])); Image(systemName: "chevron.down").fontWeight(AureaIconWeight.symbol) }
                        .font(.aurea(size: 14)).frame(minHeight: 44)
                }
                action("cam_place_selected", "cam_place_on_points", enabled: model.cameraSelectedCount > 0) { model.createTrackedObject(objectKind); reload() }
            }
            DisclosureGroup(AureaText.t("cam_point_display"), isExpanded: $pointDisplay) {
                Toggle(AureaText.t("cam_good_points"), isOn: $model.cameraGoodPointsOnly).font(.aurea(size: 13)).tint(AureaColors.accent)
                Toggle(AureaText.t("cam_drag_surface_toggle"), isOn: $model.cameraTargetMode).font(.aurea(size: 13)).tint(AureaColors.accent)
                HStack { Text(AureaText.t("fx_tamanho_ponto")).font(.aurea(size: 12)); Slider(value: $model.cameraPointSize, in: 2...8).tint(AureaColors.accent) }
            }.padding(.vertical, 12)
            if model.cameraSelectedCount > 0 && !flag("rotationOnly") {
                DisclosureGroup(AureaText.t("cam_reference_controls"), isExpanded: $calibration) {
                Button(AureaText.t("cam_set_origin")) { model.calibrateCamera(0) }.padding(.vertical, 12)
                if model.cameraSelectedCount >= 3 { Button(AureaText.t("cam_set_ground")) { model.calibrateCamera(1) }.padding(.vertical, 12) }
                if model.cameraSelectedCount == 2 {
                    TextField(AureaText.t("cam_distance"), text: $distanceText).keyboardType(.decimalPad).textFieldStyle(.roundedBorder)
                    Button(AureaText.t("cam_set_scale")) { if let value = Float(distanceText.replacingOccurrences(of: ",", with: ".")) { model.calibrateCamera(2, distance: value) } }.padding(.vertical, 12)
                }
                ForEach(model.layers.filter { $0.kind == 10 }) { layer in
                    Button(AureaText.t("cam_place_3d", layer.name)) { model.placeTrackedModel(layer.id) }.padding(.vertical, 12)
                }
                }.padding(.vertical, 12)
            }
            Button(AureaText.t(advanced ? "cam_hide_advanced" : "panel_avancado")) { advanced.toggle() }.padding(.vertical, 12)
            if advanced { note(AureaText.t("cam_stats_short", Int(number("solved")), Int(number("frames")), String(format: "%.2f", number("error")), Int(number("fovDeg").rounded()))) }
            if advanced {
                qualityPicker
                solveOptions
                action("cam_resolve", "cam_resolve_desc") {
                    if !model.engine.refineCameraTrack(false, motion: cameraMotion, fov: knownFov, shotType: shotType, lens: lens ? 1 : 0) { model.toast = AureaText.t("app_cam_refine_failed") }
                    reload()
                }
                if model.cameraSelectedCount > 0 {
                    action("cam_delete_resolve", "cam_delete_resolve_desc") {
                        // Par do Android: falhou = avisa e mantém a seleção.
                        if model.engine.refineCameraTrack(true, motion: cameraMotion, fov: knownFov, shotType: shotType, lens: lens ? 1 : 0) { model.cameraSelectedCount = 0 }
                        else { model.toast = AureaText.t("app_cam_refine_failed") }
                        reload()
                    }
                }
                Spacer().frame(height: 8)
                action("panel_analisar_novo", "panel_modo_escolhido_acima", run: analyze)
            }
        } else {
            if state == 3 || state == 4 {
                note(state == 4 ? AureaText.t("panel_analise_cancelada") : AureaText.t("ios_camera_solve_failed", AureaEngineText.reason(status["message"] as? String)))
                Spacer().frame(height: 8)
            }
            action("panel_analisar_camera", "panel_acha_movimento_camera_video_roda_segundo", run: analyze)
        }
    }
    private func number(_ key: String) -> Double { (status[key] as? NSNumber)?.doubleValue ?? 0 }
    private func flag(_ key: String) -> Bool { (status[key] as? NSNumber)?.boolValue ?? false }
    private func note(_ text: String) -> some View {
        Text(text).font(.aurea(size: 12)).lineSpacing(2).foregroundStyle(AureaColors.muted)
    }
    private func reload() {
        guard sourceLayer == model.primarySelection else { status = [:]; model.cameraFeatures = []; return }
        status = model.engine.cameraTrackingStatus()
        if state == 2 && model.cameraTrackerVisible { model.refreshCameraTrackPoints() } else { model.cameraFeatures = []; model.cameraTarget = [] }
    }
    private func restore() {
        guard let id = model.primarySelection else { sourceLayer = nil; return }
        if model.engine.restoreCameraTrack(forLayer: id) { sourceLayer = id; model.cameraSelectedCount = 0 }
    }
    private func analyze() {
        guard let id = model.primarySelection else { return }
        // Não começou (outra análise rodando, clipe curto): avisa e NÃO adota a
        // camada — antes o painel desta camada mostrava a análise de outra.
        guard model.engine.startCameraTrack(id, mode: mode, motion: cameraMotion, fov: knownFov, shotType: shotType, lens: lens ? 1 : 0) else {
            model.toast = AureaText.t("msg_nao_foi_possivel_analisar_esta_camada")
            reload()
            return
        }
        sourceLayer = id
        reload()
    }
    private var solveSummary: String {
        var text = AureaText.t("cam_average_error", String(format: "%.2f", number("averageErrorPx")))
        if Int(number("shotType")) == 1 { text += " · " + AureaText.t("cam_zoom_range", Int(number("fovStartDeg").rounded()), Int(number("fovEndDeg").rounded())) }
        if flag("lensSolved") { text += " · " + AureaText.t("cam_lens_values", String(format: "%.3f", number("k1")), flag("k2Solved") ? String(format: "%.3f", number("k2")) : "0") }
        return text
    }
    /// 0.3.0: tipo de tomada, método (códigos do motor: 0 auto, 1 típico, 2 tripé, 3 quase plana) e lente.
    @ViewBuilder private var solveOptions: some View {
        note(AureaText.t("cam_shot_type")).padding(.top, 8)
        Picker(AureaText.t("cam_shot_type"), selection: Binding(get: { shotType }, set: { v in
            shotType = v
            knownFov = v == 2 ? (knownFov > 0 ? knownFov : (number("fovDeg") > 0 ? Float(number("fovDeg")).clamped(to: 10...120) : 50)) : 0
        })) {
            Text(AureaText.t("cam_shot_fixed")).tag(UInt32(0)); Text(AureaText.t("cam_shot_zoom")).tag(UInt32(1)); Text(AureaText.t("cam_shot_known")).tag(UInt32(2))
        }.pickerStyle(.segmented).accessibilityIdentifier("cam.shot")
        if shotType == 2 {
            note(AureaText.t("cam_fov_value", Int(knownFov.rounded())))
            Slider(value: $knownFov, in: 10...120).tint(AureaColors.accent)
        }
        note(AureaText.t("cam_method")).padding(.top, 6)
        Picker(AureaText.t("cam_method"), selection: $cameraMotion) {
            Text(AureaText.t("trk_auto")).tag(UInt32(0)); Text(AureaText.t("cam_method_typical")).tag(UInt32(1))
            Text(AureaText.t("cam_method_flat")).tag(UInt32(3)); Text(AureaText.t("cam_tripod")).tag(UInt32(2))
        }.pickerStyle(.segmented).accessibilityIdentifier("cam.method")
        Toggle(isOn: $lens) {
            VStack(alignment: .leading, spacing: 2) {
                Text(AureaText.t("cam_lens_estimate")).font(.aurea(size: 13))
                Text(AureaText.t("cam_lens_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            }
        }.tint(AureaColors.accent).frame(minHeight: 48).accessibilityIdentifier("cam.lens")
    }
    private var qualityPicker: some View {
        HStack(spacing: 8) {
            ForEach(Array(["panel_rapido", "panel_equilibrado", "panel_alta_qualidade"].enumerated()), id: \.offset) { index, key in
                Button { mode = UInt32(index) } label: {
                    Text(AureaText.t(key)).font(.aurea(size: 12)).multilineTextAlignment(.center)
                        .foregroundStyle(mode == UInt32(index) ? AureaColors.accent : AureaColors.text)
                        .padding(8).frame(maxWidth: .infinity, minHeight: 44)
                        .background(mode == UInt32(index) ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                }.buttonStyle(AureaPressStyle()).accessibilityAddTraits(mode == UInt32(index) ? .isSelected : [])
            }
        }
    }
    private func action(_ title: String, _ subtitle: String, enabled: Bool = true, run: @escaping () -> Void) -> some View {
        Button(action: run) {
            VStack(alignment: .leading, spacing: 2) {
                Text(AureaText.t(title)).font(.aurea(size: 14, weight: .semibold))
                Text(AureaText.t(subtitle)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            }.frame(maxWidth: .infinity, alignment: .leading).padding(.horizontal, 14).padding(.vertical, 12)
                .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }.buttonStyle(AureaPressStyle()).disabled(!enabled)
    }
}

@MainActor
private struct MotionTrackingSection: View {
    @EnvironmentObject private var model: AureaModel
    @State private var lock = false
    @State private var smooth: Float = 0.5
    @State private var zoom: Float = 1.15
    @State private var crop: UInt32 = 1
    private let timer = Timer.publish(every: 0.4, on: .main, in: .common).autoconnect()
    private func n(_ key: String) -> Double { (model.motionStatus[key] as? NSNumber)?.doubleValue ?? 0 }
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(AureaText.t("trk_title")).font(.aurea(size: 14, weight: .bold))
            if n("state") != 1 && model.pointPick == nil {
                ForEach(Array(["trk_tool_point", "trk_tool_two_points", "trk_tool_planar", "trk_tool_corner_pin", "trk_tool_stabilizer"].map { AureaText.t($0) }.enumerated()), id: \.offset) { i, name in
                    button(name) { model.beginMotionPick(UInt32(i)) }
                }
                Toggle(AureaText.t("trk_backward"), isOn: $model.motionBackward)
                Picker(AureaText.t("trk_motion"), selection: $model.motionModel) {
                    Text(AureaText.t("trk_auto")).tag(UInt32(0)); Text(AureaText.t("trk_motion_position")).tag(UInt32(1))
                    Text(AureaText.t("trk_motion_prs")).tag(UInt32(2)); Text(AureaText.t("trk_motion_perspective")).tag(UInt32(3))
                }
                Text(AureaText.t("trk_feature_radius", Int(model.motionFeature)))
                Slider(value: $model.motionFeature, in: 6...48)
                Text(AureaText.t("trk_search_radius", Int(model.motionSearch)))
                Slider(value: $model.motionSearch, in: 16...192)
                button(AureaText.t("trk_restore")) { model.restoreMotion() }
            }
            if model.pointPick != nil {
                Text(AureaText.t("ios_motion_points_selected", model.motionPicked))
                button(AureaText.t("editor_cancelar_selecao")) { model.cancelMotionPick() }
            }
            if n("state") == 1 {
                ProgressView(value: n("progress"))
                Text(AureaText.t("ios_analyzing_pct", Int(n("progress") * 100)))
                button(AureaText.t("common_cancel")) { model.engine.cancelMotionTrack() }
            }
            if let message = model.motionStatus["message"] as? String, !message.isEmpty { Text(AureaEngineText.sentence(message)).foregroundStyle(AureaColors.muted) }
            if n("state") == 2 {
                Text(AureaText.t("trk_stats", Int(n("validFrames")), Int(n("frames")), Int(n("confidence") * 100), String(format: "%.2f", n("error"))))
                // Quadros perdidos não escondem mais as ações: ficam sem key (interpolados).
                if n("lost") > 0 { Text(AureaText.t("track_lost_frames_note", Int(n("lost")))).foregroundStyle(AureaColors.muted) }
                if n("frames") > 0 {
                    if n("tool") == 4 {
                        Toggle(AureaText.t("trk_lock_camera"), isOn: $lock)
                        Text(AureaText.t("trk_smoothness", String(format: "%.1f", smooth)))
                        Slider(value: $smooth, in: 0.1...2)
                        Text(AureaText.t("trk_max_zoom", Int((zoom - 1) * 100)))
                        Slider(value: $zoom, in: 1...1.5)
                        Picker(AureaText.t("trk_crop"), selection: $crop) { Text(AureaText.t("trk_crop_none")).tag(UInt32(0)); Text(AureaText.t("trk_crop_static")).tag(UInt32(1)); Text(AureaText.t("trk_crop_dynamic")).tag(UInt32(2)) }
                        button(AureaText.t("trk_apply_stabilization")) { model.applyMotion(3, lock: lock, smooth: smooth, maxScale: zoom, crop: crop) }
                    } else {
                        button(AureaText.t("trk_create_null")) { model.applyMotion(0) }
                        // Alvo escolhido aqui: a camada que recebe o rastreio não tem
                        // este painel (é do vídeo), então "a selecionada" era o próprio vídeo.
                        let targets = model.layers.filter { $0.id != model.primarySelection && !$0.threeD && $0.parentIndex < 0 && !$0.adjustment && [1, 2, 4, 5, 6, 11, 12].contains($0.kind) }
                        if targets.isEmpty { Text(AureaText.t("track_no_target")).foregroundStyle(AureaColors.muted) }
                        ForEach(targets) { layer in
                            Button(AureaText.t("track_apply_to", layer.name)) { model.applyMotion(1, target: layer.id) }.padding(.vertical, 6)
                        }
                        if n("tool") <= 1 { button(AureaText.t("editor_estabilizar_pelo_ponto")) { model.applyMotion(3, lock: true, smooth: 0.5, maxScale: 1, crop: 0) } }
                        if n("tool") >= 2 {
                            ForEach(targets.filter { $0.kind == 1 || $0.kind == 2 }) { layer in
                                Button(AureaText.t("track_corner_pin_to", layer.name)) { model.applyMotion(2, target: layer.id) }.padding(.vertical, 6)
                            }
                        }
                    }
                }
            }
        }.font(.aurea(size: 12)).tint(AureaColors.accent)
            .onAppear { model.refreshMotionStatus() }
            .onChange(of: model.primarySelection) { _ in model.refreshMotionStatus() }
            .onReceive(timer) { _ in model.refreshMotionStatus() }
    }
    private func button(_ title: String, run: @escaping () -> Void) -> some View {
        Button(action: run) { Text(title).font(.aurea(size: 14, weight: .semibold)).frame(maxWidth: .infinity, alignment: .leading).padding(12).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12)) }.buttonStyle(AureaPressStyle())
    }
}

struct CaptionWord: Codable, Identifiable {
    var id = UUID()
    var word: String
    var start: Double
    var end: Double
    var native: [String: Any] { ["word": word, "start": start, "end": end] }
    init(word: String, start: Double, end: Double) { self.word = word; self.start = start; self.end = end }
    init?(_ row: [String: Any]) {
        guard let word = row["word"] as? String,
              let start = (row["start"] as? NSNumber)?.doubleValue,
              let end = (row["end"] as? NSNumber)?.doubleValue,
              start.isFinite, end.isFinite, end > start, !word.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
        self.init(word: word, start: start, end: end)
    }
}

enum CaptionKeychain {
    private static let query: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
        kSecAttrService as String: "com.aurea.captions", kSecAttrAccount as String: "groq_api_key"]
    static func read() -> String {
        var q = query; q[kSecReturnData as String] = true; q[kSecMatchLimit as String] = kSecMatchLimitOne
        var result: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &result) == errSecSuccess, let data = result as? Data else { return "" }
        return String(data: data, encoding: .utf8) ?? ""
    }
    static func save(_ key: String) -> Bool {
        let value = key.trimmingCharacters(in: .whitespacesAndNewlines)
        if value.isEmpty { let status = SecItemDelete(query as CFDictionary); return status == errSecSuccess || status == errSecItemNotFound }
        let attrs: [String: Any] = [kSecValueData as String: Data(value.utf8), kSecAttrAccessible as String: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly]
        let status = SecItemUpdate(query as CFDictionary, attrs as CFDictionary)
        if status == errSecItemNotFound { return SecItemAdd(query.merging(attrs) { _, new in new } as CFDictionary, nil) == errSecSuccess }
        return status == errSecSuccess
    }
}

/// Qualidade da transcrição = tamanho do modelo Whisper (pesos oficiais q5_1
/// do ggerganov/whisper.cpp; tamanho e SHA-256 do ponteiro LFS publicado).
/// O mesmo trio existe no Android (`WhisperQuality` em WhisperModels.kt).
enum WhisperQuality: Int, CaseIterable, Identifiable {
    case fast = 0, balanced = 1, accurate = 2
    var id: Int { rawValue }
    var model: String { ["tiny", "base", "small"][rawValue] }
    var bytes: Int { [32152673, 59707625, 190085487][rawValue] }
    var megabytes: Int { [32, 60, 190][rawValue] }
    var sha256: String {
        ["818710568da3ca15689e31a743197b520007872ff9576237bda97bd1b469c3d7",
         "422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898",
         "ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb"][rawValue]
    }
    var fileName: String { "ggml-\(model)-q5_1.bin" }
    var label: String { AureaText.t(["cap025_quality_fast", "cap025_quality_balanced", "cap025_quality_accurate"][rawValue]) }
    var hint: String { AureaText.t(["cap025_quality_fast_hint", "cap025_quality_balanced_hint", "cap025_quality_accurate_hint"][rawValue]) }
    /// Cabe agora? O jetsam corta o app pelo limite do processo, não pela RAM
    /// total: `os_proc_available_memory` é o que ainda dá para usar (base usa
    /// ~350 MB e small ~650 MB durante a transcrição, com o editor aberto).
    var fits: Bool {
        if self == .fast { return true }
        let physical = ProcessInfo.processInfo.physicalMemory
        let reported = UInt64(os_proc_available_memory())
        let available = reported > 0 ? reported : physical / 2   // simulador: sem limite informado
        let gib: UInt64 = 1 << 30
        switch self {
        case .balanced: return physical >= 3 * gib && available >= 600 << 20
        case .accurate: return physical >= gib * 11 / 2 && available >= 1200 << 20
        case .fast: return true
        }
    }
    var downloaded: Bool {
        let url = AureaPaths.documents.appendingPathComponent("Whisper", isDirectory: true).appendingPathComponent(fileName)
        return ((try? FileManager.default.attributesOfItem(atPath: url.path)[.size]) as? NSNumber)?.intValue == bytes
    }
    static var best: WhisperQuality { allCases.last { $0.fits } ?? .fast }
    /// A pedida, ou a melhor abaixo dela que ainda cabe (a memória livre muda).
    func usable() -> WhisperQuality { WhisperQuality.allCases.filter { $0.rawValue <= rawValue }.last { $0.fits } ?? .fast }
    private static let key = "aurea.captions.whisperQuality"
    /// Guardada ou a melhor que cabe (em Modo de Pouca Energia, no máximo a equilibrada).
    static func initial() -> WhisperQuality {
        if let saved = UserDefaults.standard.object(forKey: key) as? Int, let quality = WhisperQuality(rawValue: saved) { return quality }
        let best = WhisperQuality.best
        return ProcessInfo.processInfo.isLowPowerModeEnabled && best == .accurate ? .balanced : best
    }
    static func save(_ quality: WhisperQuality) { UserDefaults.standard.set(quality.rawValue, forKey: key) }
}

/// Idiomas da fala (0.2.5): os sete do app mais os comuns que o Whisper
/// reconhece bem. Código do Whisper; nome no próprio idioma. Mesma lista no Android.
enum CaptionLanguages {
    static let codes = ["pt", "en", "es", "ru", "hi", "ar", "id", "fr", "de", "it", "ja", "ko", "zh", "tr"]
    private static let names = ["pt": "Português", "en": "English", "es": "Español", "ru": "Русский", "hi": "हिन्दी",
                                "ar": "العربية", "id": "Bahasa Indonesia", "fr": "Français", "de": "Deutsch", "it": "Italiano",
                                "ja": "日本語", "ko": "한국어", "zh": "中文", "tr": "Türkçe"]
    static func endonym(_ code: String) -> String { names[code] ?? code }
    /// Para o VoiceOver: o nome no idioma do app junto do nativo ("Japonês (日本語)").
    static func spoken(_ code: String) -> String {
        let app = AureaText.language.resolved.rawValue
        guard let local = Locale(identifier: app).localizedString(forLanguageCode: code), local != endonym(code) else { return endonym(code) }
        return "\(local.prefix(1).uppercased())\(local.dropFirst()) (\(endonym(code)))"
    }
    private static let key = "aurea.captions.speechLanguage"
    /// Idioma escolhido no app ou, seguindo o sistema, o do aparelho — quando o Whisper o tem; "" = automático.
    static func preferred() -> String {
        let chosen = AureaText.language == .system ? nil : String(AureaText.language.rawValue.prefix(2))
        let device = Locale.preferredLanguages.first.map { tag in String(tag.prefix { $0 != "-" && $0 != "_" }) }
        for raw in [chosen, device].compactMap({ $0 }) {
            let code = raw == "in" ? "id" : raw
            if codes.contains(code) { return code }
        }
        return ""
    }
    static func initial() -> String {
        if let saved = UserDefaults.standard.string(forKey: key), saved.isEmpty || codes.contains(saved) { return saved }
        return preferred()
    }
    static func save(_ code: String) { UserDefaults.standard.set(code, forKey: key) }
}

enum CaptionTranscriber {
    struct Failure: LocalizedError { var message: String; var errorDescription: String? { message } }
    static func cacheURL(_ path: String) -> URL {
        let size = ((try? FileManager.default.attributesOfItem(atPath: path)[.size]) as? NSNumber)?.int64Value ?? 0
        let hash = SHA256.hash(data: Data("\(path)|\(size)".utf8)).map { String(format: "%02x", $0) }.joined()
        return AureaPaths.documents.appendingPathComponent("Captions", isDirectory: true).appendingPathComponent(hash + ".json")
    }
    static func load(_ path: String) -> [CaptionWord] {
        guard let data = try? Data(contentsOf: cacheURL(path)) else { return [] }
        return (try? JSONDecoder().decode([CaptionWord].self, from: data)) ?? []
    }
    static func save(_ words: [CaptionWord], path: String) {
        guard !path.isEmpty else { return }
        let url = cacheURL(path)
        try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        if let data = try? JSONEncoder().encode(words) { try? data.write(to: url, options: .atomic) }
    }
    /// Baixa (uma vez) e confere o modelo da qualidade pedida. `progress` recebe 0…100.
    static func prepareModel(_ quality: WhisperQuality, progress: @escaping @Sendable (Int) -> Void) async throws -> URL {
        try Task.checkCancellation()
        let folder = AureaPaths.documents.appendingPathComponent("Whisper", isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let target = folder.appendingPathComponent(quality.fileName)
        func valid(_ url: URL) throws -> Bool {
            guard (try FileManager.default.attributesOfItem(atPath: url.path)[.size] as? NSNumber)?.intValue == quality.bytes else { return false }
            let file = try FileHandle(forReadingFrom: url); defer { try? file.close() }
            var digest = SHA256()
            while let data = try file.read(upToCount: 65536), !data.isEmpty {
                try Task.checkCancellation()
                digest.update(data: data)
            }
            return digest.finalize().map { String(format: "%02x", $0) }.joined() == quality.sha256
        }
        if (try? valid(target)) == true { return target }
        try Task.checkCancellation()
        let free = (try? folder.resourceValues(forKeys: [.volumeAvailableCapacityForImportantUsageKey]))?.volumeAvailableCapacityForImportantUsage ?? Int64.max
        if free < Int64(quality.bytes) * 2 { throw Failure(message: AureaText.t("app_whisper_no_space", quality.model)) }
        let remote = URL(string: "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/\(quality.fileName)")!
        let partial = folder.appendingPathComponent(quality.fileName + ".part")
        defer { try? FileManager.default.removeItem(at: partial) }
        do { try await download(remote, to: partial, progress: progress) }
        catch let failure as Failure { throw failure }
        catch {
            // Sem rede, DNS, tempo esgotado: "não deu para baixar", não "falha na transcrição".
            if Task.isCancelled { throw CancellationError() }
            throw Failure(message: AureaText.t("app_whisper_download_failed"))
        }
        try Task.checkCancellation()
        guard try valid(partial) else { throw Failure(message: AureaText.t("ios_whisper_verify_failed")) }
        if FileManager.default.fileExists(atPath: target.path) { try FileManager.default.removeItem(at: target) }
        try FileManager.default.moveItem(at: partial, to: target)
        var values = URLResourceValues(); values.isExcludedFromBackup = true
        var local = target; try local.setResourceValues(values)
        return target
    }
    /// O handler move o arquivo antes de o URLSession apagá-lo; o progresso
    /// vem do `Progress` da tarefa (o modelo preciso tem 190 MB).
    private final class ModelDownload: @unchecked Sendable {
        var task: URLSessionDownloadTask?
        var observation: NSKeyValueObservation?
        var shown = -1
    }
    private static func download(_ remote: URL, to destination: URL, progress: @escaping @Sendable (Int) -> Void) async throws {
        let box = ModelDownload()
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { (continuation: CheckedContinuation<Void, Error>) in
                let task = URLSession.shared.downloadTask(with: remote) { url, response, error in
                    box.observation?.invalidate()
                    if let error { continuation.resume(throwing: error); return }
                    guard let url, (response as? HTTPURLResponse)?.statusCode == 200 else {
                        continuation.resume(throwing: Failure(message: AureaText.t("app_whisper_download_failed"))); return
                    }
                    do {
                        if FileManager.default.fileExists(atPath: destination.path) { try FileManager.default.removeItem(at: destination) }
                        try FileManager.default.moveItem(at: url, to: destination)
                        continuation.resume()
                    } catch { continuation.resume(throwing: error) }
                }
                box.task = task
                box.observation = task.progress.observe(\.fractionCompleted) { value, _ in
                    let percent = Int(value.fractionCompleted * 100)
                    if percent != box.shown { box.shown = percent; progress(percent) }
                }
                task.resume()
                if Task.isCancelled { task.cancel() }
            }
        } onCancel: { box.task?.cancel() }
    }
    // Mesma combinação de palavras e pontuação de Captions.kt no Android.
    static func parse(_ object: [String: Any]) -> [CaptionWord] {
        var words = (object["words"] as? [[String: Any]] ?? []).compactMap(CaptionWord.init)
        let segments = object["segments"] as? [[String: Any]] ?? []
        if !words.isEmpty {
            func letters(_ text: String) -> String { String(text.lowercased().filter { $0.isLetter || $0.isNumber }) }
            var wi = 0
            for segment in segments {
                let end = (segment["end"] as? NSNumber)?.doubleValue ?? 0
                let tokens = (segment["text"] as? String ?? "").split(whereSeparator: \.isWhitespace).map(String.init).filter { !letters($0).isEmpty }
                var ti = 0
                while wi < words.count && words[wi].start < end + 0.05 && ti < tokens.count {
                    if letters(words[wi].word) == letters(tokens[ti]) { words[wi].word = tokens[ti]; ti += 1 }
                    wi += 1
                }
            }
            return words
        }
        for segment in segments {
            let tokens = (segment["text"] as? String ?? "").split(whereSeparator: \.isWhitespace).map(String.init)
            var time = (segment["start"] as? NSNumber)?.doubleValue ?? 0
            let end = (segment["end"] as? NSNumber)?.doubleValue ?? time
            let duration = end - time, total = Double(tokens.reduce(0) { $0 + $1.count + 1 })
            guard duration > 0, total > 0 else { continue }
            for token in tokens { let d = duration * Double(token.count + 1) / total; words.append(CaptionWord(word: token, start: time, end: time + d)); time += d }
        }
        return words
    }
}

@MainActor
struct CaptionsPanel: View {
    @EnvironmentObject private var model: AureaModel
    @State private var words: [CaptionWord] = []
    @State private var sourcePath = ""
    @State private var transcriptSource: String?
    @State private var translateEnglish = false
    @State private var hasKey = false
    @State private var language: String = CaptionLanguages.initial()
    @State private var quality: WhisperQuality = WhisperQuality.initial()
    /// Aviso sem erro (a qualidade pedida não coube na memória livre e outra foi usada).
    @State private var notice: String?
    /// O .srt escolhido (DocumentImportPicker: o `.fileImporter` aninhado não abria).
    @State private var srtPick: URL?
    @State private var busy: String?
    @State private var error: String?
    @State private var job: Task<Void, Never>?
    @State private var jobRevision = UUID()
    @State private var loading: Task<Void, Never>?
    @State private var fillers: Set<UUID> = []
    @State private var wordRevision = UUID()
    @State private var editing: UUID?
    @State private var captionCount = 0
    @State private var mode = 0
    @State private var style = 2
    @State private var maxWords = 4
    @State private var maxChars = 18
    @State private var maxLines = 2
    @State private var highlight = true
    @State private var uppercase = false
    @State private var breakOnPause = true
    @State private var removeFillers = true
    @State private var posY: Float = 0.78
    @State private var size: Float = 0.065
    private var captionTrack: NativeCaptionTrack? { model.captionTracks.first { $0.layer == model.primarySelection || $0.source == model.primarySelection } }
    private var id: Int64 { captionTrack?.source ?? model.primarySelection ?? 0 }
    private var path: String { model.engine.layerMediaPath(id) }
    var body: some View {
        VStack(spacing: 0) {
            PanelHeader(title: AureaText.t("panel_legendas"), onBack: { model.panel = .none })
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    if model.primarySelection == nil || (![1, 3].contains(model.selectedLayer?.kind ?? 0) && captionTrack == nil) {
                        Text(AureaText.t("sh_add_captions_need_speech")).font(.aurea(size: 13))
                        ForEach(model.layers.filter { $0.kind == 1 || $0.kind == 3 }) { layer in
                            CaptionAction(label: layer.name) {
                                model.select(layerId: layer.id)
                                model.panel = .captions
                            }
                        }
                    } else { controls }
                    ForEach(0..<((words.count + 23) / 24), id: \.self) { chunk in
                        transcriptChunk(chunk)
                    }
                    Spacer().frame(height: 24)
                }.padding(.horizontal, 18).padding(.vertical, 10)
            }
        }.foregroundStyle(AureaColors.text).background(AureaColors.editorPanel)
        .onAppear { loadOptions() }
        // 0.3.5: `.task(id:)` roda ao aparecer e a cada troca de camada com a view NOVA;
        // o `onChange(of: id) { _ in open() }` lia o `id`/`path` antigos (legendas da camada anterior).
        .task(id: id) { open() }
        .onChange(of: model.status.modelRevision) { _ in captionCount = Int(model.engine.captionCount(id)) }
        .onChange(of: language) { CaptionLanguages.save($0) }
        .onChange(of: quality) { notice = nil; WhisperQuality.save($0) }
        .onDisappear { _ = model.engine.captionProgress(true); job?.cancel(); loading?.cancel(); persist(); model.captionOptions = options }
        .onChange(of: srtPick) { picked in
            guard let url = picked else { return }
            srtPick = nil
            importSRT(.success(url))
        }
    }
    private var controls: some View {
        VStack(alignment: .leading, spacing: 0) {
            if let captionTrack { CaptionBlockEditor(track: captionTrack) }
            if let busy { note(busy, color: AureaColors.accent) }
            if let error { note(error, color: AureaColors.danger) }
            if let notice { note(notice, color: AureaColors.muted) }
            note(AureaText.t("ios_whisper_note"), color: AureaColors.muted)
            if busy != nil { CaptionAction(label: AureaText.t("common_cancel")) { _ = model.engine.captionProgress(true); job?.cancel() } }
            speechControls
            Toggle(AureaText.t("beta_translate_english"), isOn: $translateEnglish)
                .frame(minHeight: 44).disabled(busy != nil)
            HStack(spacing: 8) {
                CaptionAction(label: AureaText.t(words.isEmpty ? "panel_gerar_legendas" : "panel_transcrever_novo"), primary: true, enabled: busy == nil, action: transcribe)
                CaptionAction(label: AureaText.t("panel_importar_legenda_srt"), enabled: busy == nil) {
                    DocumentImportPicker.present(types: [UTType(filenameExtension: "srt") ?? .plainText, .plainText, .data]) { urls in srtPick = urls?.first }
                }
            }.padding(.vertical, 6)
            label("panel_estilo")
            CaptionChips(options: ["pn_caption_style_classic", "panel_caixa", "pn_caption_style_highlight", "pn_caption_style_neon", "pn_karaoke", "pn_pop", "pack_text_4", "pack_text_5"].map { AureaText.t($0) }, selected: style) { style = $0 }
            label("pn_caption")
            CaptionChips(options: [AureaText.t("pn_caption_grouped"), AureaText.t("pn_caption_one_per_word")], selected: mode) { mode = $0 }
            if mode == 0 { groupingControls }
            label("pn_caption_screen_height")
            CaptionChips(options: ["pn_caption_pos_top", "pn_caption_pos_middle", "pn_caption_pos_bottom"].map { AureaText.t($0) },
                selected: [Float(0.2), 0.5, 0.78].firstIndex(where: { abs($0 - posY) < 0.01 }) ?? -1) { posY = [0.2, 0.5, 0.78][$0] }
            label("panel_tamanho")
            CaptionChips(options: ["pn_size_small_short", "pn_size_medium_short", "pn_size_large_short"].map { AureaText.t($0) },
                selected: [Float(0.045), 0.065, 0.09].firstIndex(where: { abs($0 - size) < 0.001 }) ?? -1) { size = [0.045, 0.065, 0.09][$0] }
            captionToggle("pn_caption_highlight_word", value: $highlight)
            captionToggle("pn_caption_uppercase", value: $uppercase)
            captionToggle("pn_caption_break_pauses", value: $breakOnPause)
            captionToggle("pn_caption_remove_fillers", value: $removeFillers)
            HStack(spacing: 8) {
                CaptionAction(label: AureaText.t(captionCount > 0 ? "pn_caption_redo" : "pn_caption_create"), primary: true, enabled: !words.isEmpty && busy == nil, action: apply)
                if captionCount > 0 {
                    CaptionAction(label: AureaText.t("pn_caption_remove_n", captionCount), enabled: busy == nil) {
                        model.engine.removeCaptions(id); model.refreshModel(force: true)
                        captionCount = Int(model.engine.captionCount(id))
                    }
                }
            }.padding(.vertical, 8)
            if !words.isEmpty {
                Text(transcriptSource.map { AureaText.plural("pn_caption_transcript_source", words.count, $0, words.count) }
                    ?? AureaText.plural("pn_caption_transcript", words.count, words.count))
                    .font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 8)
            }
        }
    }
    /// Idioma da fala e qualidade do Whisper. Qualidade que não cabe na
    /// memória livre aparece desligada, com o motivo para o VoiceOver.
    private var speechControls: some View {
        let codes = [""] + CaptionLanguages.codes
        let auto = AureaText.t("cap025_lang_auto")
        let qualities = WhisperQuality.allCases
        let fits = qualities.map(\.fits)
        let unavailable = AureaText.t("cap025_quality_unavailable")
        let names: [String] = codes.map { $0.isEmpty ? auto : CaptionLanguages.endonym($0) }
        let spoken: [String] = codes.map { $0.isEmpty ? auto : CaptionLanguages.spoken($0) }
        let explained: [String] = qualities.map { q in
            let reason = fits[q.rawValue] ? "" : " " + unavailable
            return q.label + ". " + q.hint + reason
        }
        let idle = busy == nil
        return VStack(alignment: .leading, spacing: 0) {
            label("panel_idioma_fala")
            CaptionChips(options: names, selected: codes.firstIndex(of: language) ?? 0, descriptions: spoken,
                         enabled: { _ in idle }, wrap: true) { language = codes[$0] }
            label("cap025_quality")
            CaptionChips(options: qualities.map(\.label), selected: quality.rawValue, descriptions: explained,
                         enabled: { idle && fits[$0] }) { quality = qualities[$0] }
            note(quality.hint, color: AureaColors.muted)
            if !fits[quality.rawValue] { note(unavailable, color: AureaColors.danger) }
            else if quality.downloaded { note(AureaText.t("cap025_quality_ready"), color: AureaColors.muted) }
            else { note(AureaText.t("cap025_quality_needs_download", quality.megabytes), color: AureaColors.muted) }
        }
    }
    private var groupingControls: some View {
        VStack(alignment: .leading, spacing: 0) {
            label("pn_caption_words_per_caption")
            CaptionChips(options: (1...6).map(String.init), selected: maxWords - 1) { maxWords = $0 + 1 }
            label("pn_caption_chars_per_line")
            CaptionChips(options: ["12", "18", "24", "32"], selected: [12, 18, 24, 32].firstIndex(of: maxChars) ?? -1) { maxChars = [12, 18, 24, 32][$0] }
            label("pn_caption_lines")
            CaptionChips(options: ["1", "2", "3"], selected: maxLines - 1) { maxLines = $0 + 1 }
        }
    }
    private func label(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 8)
    }
    private func note(_ text: String, color: Color) -> some View {
        Text(text).font(.aurea(size: 13)).foregroundStyle(color).padding(.vertical, 6)
    }
    private func captionToggle(_ key: String, value: Binding<Bool>) -> some View {
        HStack {
            Text(AureaText.t(key)).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
            AureaToggle(checked: value.wrappedValue, onCheckedChange: { value.wrappedValue = $0 })
        }.frame(height: 44)
    }
    private func transcriptChunk(_ chunk: Int) -> some View {
        let lower = chunk * 24, upper = min(words.count, lower + 24)
        let items = Array(words[lower..<upper])
        return VStack(alignment: .leading, spacing: 0) {
            AureaFlowLayout(hGap: 6, vGap: 6) {
                ForEach(items) { word in
                    let struck = removeFillers && fillers.contains(word.id)
                    Button { editing = word.id } label: {
                        Text(word.word).font(.aurea(size: 13)).strikethrough(struck)
                            .foregroundStyle(struck ? AureaColors.muted : AureaColors.text)
                            .padding(.horizontal, 8).padding(.vertical, 4)
                            .background(editing == word.id ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 6))
                    }.buttonStyle(AureaPressStyle())
                }
            }.padding(.bottom, 6)
            if let editing, let word = items.first(where: { $0.id == editing }) {
                CaptionWordEditor(word: word) { edit(word.id, text: $0) }.id(word.id).padding(.bottom, 8)
            }
        }
    }
    private func open() {
        jobRevision = UUID()
        loading?.cancel(); job?.cancel(); busy = nil; error = nil; editing = nil
        let source = path, sourceId = id, project = model.projectGeneration
        sourcePath = source; words = []; fillers = []; transcriptSource = nil; wordRevision = UUID()
        hasKey = !CaptionKeychain.read().isEmpty; captionCount = Int(model.engine.captionCount(sourceId))
        guard !source.isEmpty else { return }
        loading = Task {
            let cached = await Task.detached(priority: .userInitiated) { CaptionTranscriber.load(source) }.value
            guard !Task.isCancelled, model.projectGeneration == project, model.primarySelection == sourceId, words.isEmpty else { return }
            setWords(cached, from: cached.isEmpty ? nil : AureaText.t("i18n_caption_source_cache"))
        }
    }
    private func setWords(_ value: [CaptionWord], from source: String?) {
        words = value; transcriptSource = source; editing = nil; fillers = []
        let revision = UUID(); wordRevision = revision
        let engine = model.engine
        Task {
            let marked = await Task.detached(priority: .userInitiated) {
                Set(value.filter { engine.isFillerWord($0.word) }.map(\.id))
            }.value
            if wordRevision == revision {
                let current = Dictionary(uniqueKeysWithValues: words.map { ($0.id, $0.word) })
                let unchanged = Set(value.filter { marked.contains($0.id) && current[$0.id] == $0.word }.map(\.id))
                let edited = Set(value.filter { current[$0.id] != $0.word }.map(\.id))
                fillers = unchanged.union(fillers.intersection(edited))
            }
        }
    }
    private func persist() {
        let source = sourcePath, snapshot = words
        guard !source.isEmpty else { return }
        Task.detached(priority: .utility) { CaptionTranscriber.save(snapshot, path: source) }
    }
    private func edit(_ wordId: UUID, text: String) {
        guard let index = words.firstIndex(where: { $0.id == wordId }) else { return }
        let text = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if text.isEmpty { words.remove(at: index) }
        else { words[index].word = text }
        fillers.remove(wordId)
        if !text.isEmpty && model.engine.isFillerWord(text) { fillers.insert(wordId) }
        editing = nil; persist()
    }
    private func importSRT(_ result: Result<URL, Error>) {
        guard busy == nil else { return }
        let sourceId = id, engine = model.engine, project = model.projectGeneration
        job?.cancel()
        let revision = UUID(); jobRevision = revision
        job = Task {
            do {
                let url = try result.get()
                let parsed = try await Task.detached(priority: .userInitiated) {
                    let access = url.startAccessingSecurityScopedResource()
                    defer { if access { url.stopAccessingSecurityScopedResource() } }
                    let file = try FileHandle(forReadingFrom: url)
                    defer { try? file.close() }
                    var bytes = Data()
                    while let chunk = try file.read(upToCount: 65_536), !chunk.isEmpty {
                        guard bytes.count + chunk.count <= 4 * 1024 * 1024 else { throw CocoaError(.fileReadTooLarge) }
                        bytes.append(chunk)
                    }
                    guard let text = String(data: bytes, encoding: .utf8) else { throw CocoaError(.fileReadInapplicableStringEncoding) }
                    return engine.parseSRT(text).compactMap(CaptionWord.init)
                }.value
                guard !Task.isCancelled, jobRevision == revision, model.projectGeneration == project, model.primarySelection == sourceId else { return }
                if parsed.isEmpty { error = AureaText.t("ios_srt_unreadable"); return }
                error = nil; setWords(parsed, from: "SRT"); persist()
            } catch { if !Task.isCancelled && jobRevision == revision { self.error = error.localizedDescription } }
        }
    }
    private func transcribe() {
        guard busy == nil else { return }
        let sourceId = id, engine = model.engine, selectedLanguage = language, translate = translateEnglish, project = model.projectGeneration
        guard !path.isEmpty else { error = AureaText.t("ios_no_audio_media"); return }
        // A memória livre muda: a qualidade pedida cai para a melhor que cabe agora.
        let chosen = quality.usable()
        notice = chosen != quality ? AureaText.t("cap025_quality_fallback", chosen.label) : nil
        busy = AureaText.t("ios_whisper_preparing"); error = nil
        let revision = UUID(); jobRevision = revision
        job = Task {
            do {
                let preparation = Task.detached(priority: .utility) {
                    try await CaptionTranscriber.prepareModel(chosen) { percent in
                        Task { @MainActor in
                            if jobRevision == revision && !Task.isCancelled { busy = AureaText.t("app_whisper_downloading", chosen.model, percent) }
                        }
                    }
                }
                let modelFile = try await withTaskCancellationHandler {
                    try await preparation.value
                } onCancel: { preparation.cancel() }
                try Task.checkCancellation()
                guard jobRevision == revision, model.projectGeneration == project else { return }
                let operation = model.beginProjectOperation()
                defer { model.endProjectOperation(operation) }
                busy = AureaText.t("ios_transcribing_on_device")
                let ticker = Task { @MainActor in
                    while !Task.isCancelled && jobRevision == revision {
                        try? await Task.sleep(nanoseconds: 400_000_000)
                        if !Task.isCancelled && jobRevision == revision { busy = AureaText.t("app_caption_whisper_progress", Int(engine.captionProgress(false))) }
                    }
                }
                defer { ticker.cancel() }
                let result = try await withTaskCancellationHandler {
                    try await Task.detached(priority: .utility) {
                        try engine.transcribeLocal(sourceId, model: modelFile.path, language: selectedLanguage, translateEnglish: translate).compactMap(CaptionWord.init)
                    }.value
                } onCancel: { _ = engine.captionProgress(true) }
                try Task.checkCancellation()
                guard jobRevision == revision else { return }
                guard model.projectGeneration == project, model.primarySelection == sourceId else { busy = nil; return }
                setWords(result, from: AureaText.t("i18n_caption_source_whisper")); busy = nil; persist(); apply()
            } catch {
                if jobRevision == revision {
                    if !Task.isCancelled { self.error = error.localizedDescription }
                    busy = nil
                }
            }
        }
    }
    private func apply() {
        guard busy == nil, !words.isEmpty else { return }
        guard words.allSatisfy({ $0.start.isFinite && $0.end.isFinite && $0.start >= 0 && $0.end > $0.start && !$0.word.isEmpty }) else {
            error = AureaText.t("ios_check_word_times"); return
        }
        persist(); model.captionOptions = options; busy = AureaText.t("ios_creating_captions"); error = nil
        let sourceId = id, engine = model.engine, snapshot = words.map(\.native), settings = options, project = model.projectGeneration
        let operation = model.beginProjectOperation()
        let revision = UUID(); jobRevision = revision
        job = Task {
            defer {
                model.endProjectOperation(operation)
                if jobRevision == revision { busy = nil }
            }
            let result = await Task.detached(priority: .userInitiated) {
                engine.createCaptions(sourceId, words: snapshot, options: settings)
            }.value
            guard !Task.isCancelled, jobRevision == revision, model.projectGeneration == project else { return }
            model.refreshModel(force: true)
            guard model.primarySelection == sourceId else { busy = nil; return }
            busy = nil; error = result.isEmpty ? nil : AureaEngineText.sentence(result); captionCount = Int(engine.captionCount(sourceId))
        }
    }
    private var options: [String: NSNumber] {
        var result = model.captionOptions
        result.merge(["mode": NSNumber(value: mode), "style": NSNumber(value: style), "maxWords": NSNumber(value: maxWords),
            "maxChars": NSNumber(value: maxChars), "maxLines": NSNumber(value: maxLines), "highlight": NSNumber(value: highlight),
            "uppercase": NSNumber(value: uppercase), "breakOnPause": NSNumber(value: breakOnPause), "removeFillers": NSNumber(value: removeFillers),
            "posY": NSNumber(value: posY), "sizeFrac": NSNumber(value: size)]) { _, fresh in fresh }
        return result
    }
    private func loadOptions() {
        let v = model.captionOptions
        mode = (v["mode"]?.intValue ?? 0).clamped(to: 0...1); style = (v["style"]?.intValue ?? 2).clamped(to: 0...7)
        maxWords = (v["maxWords"]?.intValue ?? 4).clamped(to: 1...12); maxChars = (v["maxChars"]?.intValue ?? 18).clamped(to: 6...60); maxLines = (v["maxLines"]?.intValue ?? 2).clamped(to: 1...4)
        highlight = v["highlight"]?.boolValue ?? true; uppercase = v["uppercase"]?.boolValue ?? false
        breakOnPause = v["breakOnPause"]?.boolValue ?? true; removeFillers = v["removeFillers"]?.boolValue ?? true
        posY = (v["posY"]?.floatValue ?? 0.78).clamped(to: 0.1...0.9); size = (v["sizeFrac"]?.floatValue ?? 0.065).clamped(to: 0.02...0.15)
    }
}

private struct CaptionChips: View {
    let options: [String]
    let selected: Int
    /// Rótulo para o VoiceOver (nome do idioma no idioma do app, motivo de estar desligada).
    var descriptions: [String]? = nil
    var enabled: (Int) -> Bool = { _ in true }
    /// Quebra em linhas (a lista de idiomas inteira à vista) em vez de rolar.
    var wrap = false
    let onPick: (Int) -> Void
    var body: some View {
        if wrap {
            AureaFlowLayout(hGap: 6, vGap: 6) { chips }.padding(.vertical, 6)
        } else {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) { chips }.frame(height: 44)
            }.frame(height: 44)
        }
    }
    private var chips: some View {
        ForEach(Array(options.enumerated()), id: \.offset) { index, title in
            let on = index == selected, active = enabled(index)
            Button { onPick(index) } label: {
                Text(title).font(.aurea(size: 12)).foregroundStyle(on ? AureaColors.accent : active ? AureaColors.text : AureaColors.muted)
                    .padding(.horizontal, 10).padding(.vertical, 6)
                    .background(on ? AureaColors.accentDim : AureaColors.chip.opacity(active ? 1 : 0.4), in: RoundedRectangle(cornerRadius: 8))
            }.buttonStyle(AureaPressStyle()).disabled(!active)
            .accessibilityLabel(descriptions.flatMap { index < $0.count ? $0[index] : nil } ?? title)
            .accessibilityAddTraits(on ? .isSelected : [])
        }
    }
}

private struct CaptionAction: View {
    let label: String
    var primary = false
    var enabled = true
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(label).font(.aurea(size: 13, weight: .semibold))
                .foregroundStyle(!enabled ? AureaColors.muted : primary ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, 14).padding(.vertical, 10)
                .background(!enabled ? AureaColors.chip.opacity(0.4) : primary ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
        }.buttonStyle(AureaPressStyle()).disabled(!enabled)
    }
}

private struct CaptionWordEditor: View {
    let word: CaptionWord
    let onDone: (String) -> Void
    @State private var text = ""
    var body: some View {
        HStack(spacing: 8) {
            Text(numeroPtBr(Float(word.start), casas: 2) + " s").font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            TextField("", text: $text).font(.aurea(size: 14)).foregroundStyle(AureaColors.text)
                .tint(AureaColors.accent).padding(10).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                .submitLabel(.done).onSubmit { onDone(text) }
            CaptionAction(label: AureaText.t("pn_ok"), primary: true) { onDone(text) }
            CaptionAction(label: AureaText.t("pn_caption_delete_word")) { onDone("") }
        }.padding(.top, 10).onAppear { text = word.word }
    }
}
