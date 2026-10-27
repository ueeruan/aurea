// AureaHyper — a tela da Home (substitui o "Upscale com IA"; par do Android:
// hyper/HyperScreen.kt). Quatro passos no visual do antigo Upscale; o estado é
// o do HyperFlow (HyperModel.swift). Sem o acesso (portão por conta,
// HyperEntitlements) mostra o que o recurso faz e que é pago — sem botão de
// compra (a venda ainda não existe).
//
// accessibilityIdentifier (os mesmos testTag do Android):
//   hyper.voltar · hyper.passo.indicador
//   hyper.bloqueado · hyper.bloqueado.entrar · hyper.bloqueado.verificar
//   1: hyper.escolher.galeria · hyper.escolher.gravar · hyper.retomar(.continuar/.descartar) · hyper.erro
//   2: hyper.receita.<id> · hyper.receita.<id>.<variante> · hyper.ajustar · hyper.ajuste.<campo> · hyper.continuar
//   3: hyper.medidor · hyper.medir · hyper.previa · hyper.previa.divisor · hyper.caminho.<turbo|lite|max> · hyper.comecar
//   4: hyper.etapa.<passo> · hyper.progresso · hyper.cancelar · hyper.tentar · hyper.voltar.previa
import AVFoundation
import PhotosUI
import SwiftUI
import UniformTypeIdentifiers

@MainActor struct HyperView: View {
    @EnvironmentObject private var model: AureaModel
    var body: some View { HyperFlowView(flow: model.hyper) }
}

@MainActor private struct HyperFlowView: View {
    @EnvironmentObject private var model: AureaModel
    @EnvironmentObject private var conta: ContaModel
    @ObservedObject var flow: HyperFlow
    @ObservedObject private var entitlements = HyperEntitlements.shared
    @State private var signingIn = false

    private var running: Bool { model.exporting }
    private var navigable: Bool { !running && !flow.finishing && !flow.importing }

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 4) {
                Button { flow.back() } label: {
                    Image(systemName: "chevron.left").font(.system(size: 18, weight: AureaIconWeight.symbol))
                        .foregroundStyle(navigable ? AureaColors.text : AureaColors.disabled)
                        .frame(width: 40, height: 40)
                }
                .buttonStyle(.plain).disabled(!navigable)
                .accessibilityLabel(AureaText.t("common_back")).accessibilityIdentifier("hyper.voltar")
                Text(AureaText.t("hyper_title")).font(.aurea(size: 17, weight: .semibold)).foregroundStyle(AureaColors.text)
                    .accessibilityAddTraits(.isHeader)
                Spacer()
                if entitlements.access == .unlocked {
                    Text(AureaText.t("hyper_step_of", flow.step.rawValue + 1, 4)).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                        .padding(.trailing, 12).accessibilityIdentifier("hyper.passo.indicador")
                }
            }
            .frame(height: 44).padding(.horizontal, 6)
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    if entitlements.access != .unlocked && flow.step != .working {
                        HyperLockedPanel(access: entitlements.access, onSignIn: { conta.entrando = true; signingIn = true },
                                         onRetry: { conta.atualizarAcessos() })
                    } else {
                        switch flow.step {
                        case .choose: HyperChooseStep(flow: flow)
                        case .recipe: HyperRecipeStep(flow: flow)
                        case .preview: HyperPreviewStep(flow: flow)
                        case .working: HyperWorkingStep(flow: flow)
                        }
                    }
                    Spacer().frame(height: 24)
                }.padding(.horizontal, 18)
            }
        }
        .background(AureaColors.background.ignoresSafeArea()).foregroundStyle(AureaColors.text)
        .preferredColorScheme(.dark)
        .onAppear { conta.atualizarAcessos() }
        .onChange(of: conta.logado) { if $0 { signingIn = false; conta.atualizarAcessos() } }
        .sheet(isPresented: $signingIn) {
            NavigationStack {
                ContaView().toolbar { ToolbarItem(placement: .confirmationAction) {
                    Button(AureaText.t("editor_fechar")) { signingIn = false }.accessibilityIdentifier("conta.fechar")
                } }
            }
        }
    }
}

// MARK: - Bloqueado

/// O que o AureaHyper faz e por que está bloqueado. Também abre do editor (efeitos Hyper).
struct HyperLockedPanel: View {
    let access: HyperAccess
    let onSignIn: () -> Void
    let onRetry: () -> Void

    private static let features = ["hyper_feature_upscale", "hyper_feature_clean", "hyper_feature_face",
                                   "hyper_feature_motion", "hyper_feature_stable", "hyper_feature_offline"]

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Spacer().frame(height: 20)
            HStack(spacing: 12) {
                Image(systemName: "lock").font(.system(size: 20, weight: AureaIconWeight.symbol)).foregroundStyle(AureaColors.accent)
                    .frame(width: 44, height: 44).background(RoundedRectangle(cornerRadius: 12).fill(AureaColors.accent.opacity(0.18)))
                    .accessibilityHidden(true)
                VStack(alignment: .leading, spacing: 2) {
                    Text(AureaText.t("hyper_title")).font(.aurea(size: 22, weight: .bold)).accessibilityAddTraits(.isHeader)
                    // "Pago" só quando a config remota diz que o acesso é por conta.
                    if let badge = badgeKey {
                        Text(AureaText.t(badge)).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(AureaColors.accent)
                    }
                }
            }
            Spacer().frame(height: 14)
            Text(AureaText.t("hyper_locked_intro")).font(.aurea(size: 15)).fixedSize(horizontal: false, vertical: true)
            Spacer().frame(height: 14)
            ForEach(Self.features, id: \.self) { key in
                HStack(alignment: .top, spacing: 10) {
                    Image(systemName: "checkmark").font(.system(size: 13, weight: AureaIconStyle.symbol)).foregroundStyle(AureaColors.accent)
                        .accessibilityHidden(true)
                    Text(AureaText.t(key)).font(.aurea(size: 14)).fixedSize(horizontal: false, vertical: true)
                }.padding(.vertical, 5)
            }
            if paid {
                Spacer().frame(height: 16)
                Text(AureaText.t("hyper_locked_payment_note")).font(.aurea(size: 14)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer().frame(height: 18)
            Text(statusText).font(.aurea(size: 14, weight: .semibold))
                .frame(maxWidth: .infinity, alignment: .leading).padding(14)
                .background(RoundedRectangle(cornerRadius: 10).fill(AureaColors.surface))
            if access == .checking {
                Spacer().frame(height: 12)
                ProgressView().tint(AureaColors.accent)
            }
            if access == .signedOut {
                Spacer().frame(height: 14)
                HyperButton(title: AureaText.t("conta_entrar"), id: "hyper.bloqueado.entrar", action: onSignIn)
            } else if access == .needsConfig {
                Spacer().frame(height: 14)
                HyperButton(title: AureaText.t("home_retry"), id: "hyper.bloqueado.verificar", primary: false) {
                    AppConfigStore.shared.refresh(force: true)
                }
            } else if access != .checking && access != .unlocked && access != .off {
                Spacer().frame(height: 14)
                HyperButton(title: AureaText.t("hyper_locked_check_again"), id: "hyper.bloqueado.verificar", primary: false, action: onRetry)
            }
        }
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("hyper.bloqueado")
    }

    private var config: AppConfig? { AppConfigStore.shared.config }
    private var paid: Bool { config?.hyperMode == .entitled && access != .needsConfig && access != .off }
    private var badgeKey: String? { access == .off ? "hyper_off_badge" : paid ? "hyper_locked_badge" : nil }

    private var statusText: String {
        switch access {
        case .signedOut: return AureaText.t("hyper_locked_signed_out")
        case .locked: return AureaText.t("hyper_locked_not_owned")
        case .needsCheck: return AureaText.t("hyper_locked_offline")
        case .expired: return AureaText.t("hyper_locked_expired")
        case .needsConfig: return AureaText.t("hyper_needs_internet")
        case .off: return config?.message(for: AureaText.language) ?? AureaText.t("hyper_off_default")
        case .checking, .unlocked: return AureaText.t("hyper_locked_checking")
        }
    }
}

/// A folha que o editor abre ao tocar num efeito Hyper sem o acesso.
struct HyperLockedSheet: View {
    @EnvironmentObject private var conta: ContaModel
    @ObservedObject private var entitlements = HyperEntitlements.shared
    @State private var signingIn = false
    let onDismiss: () -> Void

    var body: some View {
        NavigationStack {
            Group {
                if signingIn { ContaView() }
                else {
                    ScrollView {
                        HyperLockedPanel(access: entitlements.access, onSignIn: { conta.entrando = true; signingIn = true },
                                         onRetry: { conta.atualizarAcessos() }).padding(.horizontal, 18)
                    }
                }
            }
            .background(AureaColors.background.ignoresSafeArea()).foregroundStyle(AureaColors.text)
            .toolbar { ToolbarItem(placement: .confirmationAction) {
                Button(AureaText.t("editor_fechar")) { onDismiss() }.accessibilityIdentifier("hyper.bloqueado.fechar")
            } }
        }
        .preferredColorScheme(.dark)
        .onAppear { conta.atualizarAcessos() }
        .onChange(of: conta.logado) { if $0 { signingIn = false; conta.atualizarAcessos() } }
        .onChange(of: entitlements.access) { if $0 == .unlocked { onDismiss() } }
    }
}

// MARK: - 1. Escolher

@MainActor private struct HyperChooseStep: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var flow: HyperFlow
    @State private var picking = false
    @State private var recording = false

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if let p = flow.pending {
                Spacer().frame(height: 16)
                VStack(alignment: .leading, spacing: 0) {
                    Text(AureaText.t("hyper_resume_title")).font(.aurea(size: 15, weight: .semibold))
                    Spacer().frame(height: 4)
                    Text(AureaText.t("hyper_resume_body", p.source.name)).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                        .fixedSize(horizontal: false, vertical: true)
                    Spacer().frame(height: 12)
                    HyperButton(title: AureaText.t("hyper_resume_continue"), id: "hyper.retomar.continuar", enabled: !flow.busy) { flow.resumePending() }
                    Spacer().frame(height: 8)
                    HyperButton(title: AureaText.t("hyper_resume_discard"), id: "hyper.retomar.descartar", primary: false, enabled: !flow.busy) {
                        flow.discardPending()
                    }
                }
                .padding(14).background(RoundedRectangle(cornerRadius: 14).fill(AureaColors.surface))
                .accessibilityElement(children: .contain).accessibilityIdentifier("hyper.retomar")
            }
            Spacer().frame(height: 16)
            Text(AureaText.t("hyper_choose_hint")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            Spacer().frame(height: 16)
            tile("photo.on.rectangle", AureaText.t("hyper_choose_gallery"), "hyper.escolher.galeria") { picking = true }
            Spacer().frame(height: 12)
            tile("video", AureaText.t("hyper_choose_record"), "hyper.escolher.gravar") {
                if UIImagePickerController.isSourceTypeAvailable(.camera) { recording = true }
                else { flow.error = AureaText.t("hyper_camera_denied") }
            }
            if flow.importing {
                Spacer().frame(height: 18)
                ProgressView().tint(AureaColors.accent)
                Spacer().frame(height: 8)
                Text(AureaText.t("hyper_choose_copying")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            HyperErrorBox(message: flow.error)
            Spacer().frame(height: 20)
            Text(AureaText.t("hyper_offline_note")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
        }
        .sheet(isPresented: $picking) {
            HyperVideoPicker(onDismiss: { picking = false }, onPick: { url in picking = false; flow.pick(url) },
                             onError: { message in picking = false; flow.error = message })
        }
        .fullScreenCover(isPresented: $recording) {
            HyperCameraPicker(onDismiss: { recording = false }, onPick: { url in recording = false; flow.pick(url) })
                .ignoresSafeArea()
        }
    }

    private func tile(_ symbol: String, _ title: String, _ id: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 8) {
                Image(systemName: symbol).font(.system(size: 28, weight: AureaIconWeight.symbol)).foregroundStyle(flow.busy ? AureaColors.disabled : AureaColors.accent)
                Text(title).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.text).multilineTextAlignment(.center)
            }
            .frame(maxWidth: .infinity, minHeight: 104).padding(18)
            .background(RoundedRectangle(cornerRadius: 14).fill(AureaColors.surface))
        }
        .buttonStyle(.plain).disabled(flow.busy)
        .accessibilityLabel(title).accessibilityIdentifier(id)
    }
}

// MARK: - 2. O que fazer

@MainActor private struct HyperRecipeStep: View {
    @ObservedObject var flow: HyperFlow

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if let s = flow.source {
                Spacer().frame(height: 12)
                Text(s.name).font(.aurea(size: 15, weight: .semibold)).lineLimit(1)
                Text("\(s.width) × \(s.height) · \(hyperFps(s.fps)) · \(hyperDuration(s.duration))")
                    .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            Spacer().frame(height: 16)
            Text(AureaText.t("hyper_recipe_title")).font(.aurea(size: 22, weight: .bold)).accessibilityAddTraits(.isHeader)
            Spacer().frame(height: 10)
            ForEach(HyperPreset.allCases) { p in
                card(p)
                Spacer().frame(height: 10)
            }
            Button { flow.advanced.toggle() } label: {
                HStack(spacing: 10) {
                    Image(systemName: "slider.horizontal.3").font(.system(size: 16, weight: AureaIconWeight.symbol))
                    Text(AureaText.t("hyper_adjust")).font(.aurea(size: 15, weight: .semibold))
                    Spacer()
                    Image(systemName: flow.advanced ? "chevron.up" : "chevron.down").font(.system(size: 13, weight: AureaIconWeight.symbol)).foregroundStyle(AureaColors.muted)
                }
                .padding(.horizontal, 14).frame(minHeight: 48)
                .background(RoundedRectangle(cornerRadius: 12).fill(AureaColors.surface))
            }
            .buttonStyle(.plain)
            .accessibilityLabel(AureaText.t("hyper_adjust")).accessibilityValue(flow.advanced ? "on" : "off")
            .accessibilityIdentifier("hyper.ajustar")
            if flow.advanced { HyperAdjustPanel(flow: flow) }
            Spacer().frame(height: 18)
            if flow.recipe.identity {
                Text(AureaText.t("hyper_recipe_pick_one")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            Spacer().frame(height: 10)
            HyperButton(title: AureaText.t("hyper_continue"), id: "hyper.continuar", enabled: !flow.recipe.identity) { flow.toPreview() }
        }
    }

    private func card(_ p: HyperPreset) -> some View {
        let selected = flow.preset == p
        return VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 10) {
                Image(systemName: p.symbol).font(.system(size: 18, weight: AureaIconWeight.symbol)).foregroundStyle(selected ? AureaColors.accent : AureaColors.text)
                    .accessibilityHidden(true)
                Text(AureaText.t(p.titleKey)).font(.aurea(size: 15, weight: .semibold))
                Spacer()
                if selected { Image(systemName: "checkmark.circle.fill").fontWeight(AureaIconWeight.symbol).foregroundStyle(AureaColors.accent).accessibilityHidden(true) }
            }
            Spacer().frame(height: 4)
            Text(AureaText.t(p.bodyKey)).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
            if p == .slowmo {
                Spacer().frame(height: 4)
                Text(AureaText.t("hyper_audio_muted")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            }
            if p == .restore {
                Spacer().frame(height: 4)
                Text(AureaText.t("hyper_face_risk_short")).font(.aurea(size: 12)).foregroundStyle(AureaColors.warning)
                    .fixedSize(horizontal: false, vertical: true)
            }
            if !p.variants.isEmpty {
                Spacer().frame(height: 10)
                HStack(spacing: 8) {
                    ForEach(p.variants, id: \.self) { v in
                        HyperChip(title: "\(v)×", id: "hyper.receita.\(p.id).\(v)", on: selected && flow.variant == v) { flow.choose(p, variant: v) }
                    }
                }
            }
        }
        .padding(14)
        .background(RoundedRectangle(cornerRadius: 14).fill(selected ? AureaColors.accent.opacity(0.16) : AureaColors.surface))
        .overlay(RoundedRectangle(cornerRadius: 14).stroke(selected ? AureaColors.accent : .clear, lineWidth: 1))
        .contentShape(Rectangle())
        .onTapGesture { flow.choose(p, variant: selected ? flow.variant : nil) }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(AureaText.t(p.titleKey))
        .accessibilityAddTraits(selected ? [.isButton, .isSelected] : .isButton)
        .accessibilityAction { flow.choose(p, variant: selected ? flow.variant : nil) }
        .accessibilityIdentifier("hyper.receita.\(p.id)")
    }
}

@MainActor private struct HyperAdjustPanel: View {
    @ObservedObject var flow: HyperFlow

    var body: some View {
        let r = flow.recipe
        VStack(alignment: .leading, spacing: 0) {
            section("hyper_adjust_upscale")
            chips(HyperRecipe.upscaleFactors.map { $0 == 1 ? AureaText.t("common_off") : "\($0)×" },
                  selected: HyperRecipe.upscaleFactors.firstIndex(of: r.upscale) ?? 0, id: "hyper.ajuste.upscale") { i in
                var n = r; n.upscale = HyperRecipe.upscaleFactors[i]; flow.edit(n)
            }
            if r.upscale > 1 {
                Spacer().frame(height: 8)
                chips([AureaText.t("hyper_mode_general"), AureaText.t("hyper_mode_anime")], selected: r.upscaleMode.rawValue, id: "hyper.ajuste.modo") { i in
                    var n = r; n.upscaleMode = i == 1 ? .anime : .general; flow.edit(n)
                }
            }
            section("hyper_adjust_clean")
            slider("hyper_adjust_denoise", value: r.denoise, id: "hyper.ajuste.ruido") { v in var n = r; n.denoise = v; flow.edit(n) }
            toggle("hyper_adjust_deblock", on: r.deblock, id: "hyper.ajuste.blocos") { v in var n = r; n.deblock = v; flow.edit(n) }
            Text(AureaText.t("hyper_adjust_deinterlace")).font(.aurea(size: 14)).padding(.vertical, 6)
            chips([AureaText.t("common_off"), AureaText.t("common_automatic"), AureaText.t("hyper_always")],
                  selected: min(max(r.deinterlace, 0), 2), id: "hyper.ajuste.entrelacado") { i in var n = r; n.deinterlace = i; flow.edit(n) }
            section("hyper_adjust_face")
            toggle("hyper_adjust_face_on", on: r.face, id: "hyper.ajuste.rosto") { v in var n = r; n.face = v; flow.edit(n) }
            Text(AureaText.t("hyper_face_risk")).font(.aurea(size: 12)).foregroundStyle(r.face ? AureaColors.warning : AureaColors.muted)
                .fixedSize(horizontal: false, vertical: true)
            if r.face && !flow.faceReady {
                Text(AureaText.t("hyper_face_model_missing")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true).padding(.top, 4).accessibilityIdentifier("hyper.rosto.sem.modelo")
            }
            if r.face {
                slider("hyper_adjust_strength", value: r.faceStrength, id: "hyper.ajuste.rosto.forca") { v in var n = r; n.faceStrength = v; flow.edit(n) }
            }
            section("hyper_adjust_motion")
            Text(AureaText.t("hyper_adjust_target_fps")).font(.aurea(size: 14)).padding(.bottom, 6)
            chips(HyperRecipe.targetFpsChoices.map { $0 <= 0 ? AureaText.t("hyper_keep") : hyperFps($0) },
                  selected: HyperRecipe.targetFpsChoices.firstIndex(of: r.targetFps) ?? 0, id: "hyper.ajuste.fps") { i in
                var n = r; n.targetFps = HyperRecipe.targetFpsChoices[i]; flow.edit(n)
            }
            Text(AureaText.t("hyper_adjust_slowmo")).font(.aurea(size: 14)).padding(.top, 10).padding(.bottom, 6)
            chips(HyperRecipe.slowMotionFactors.map { $0 == 1 ? AureaText.t("common_off") : "\($0)×" },
                  selected: HyperRecipe.slowMotionFactors.firstIndex(of: r.slowMotion) ?? 0, id: "hyper.ajuste.lento") { i in
                var n = r; n.slowMotion = HyperRecipe.slowMotionFactors[i]; flow.edit(n)
            }
            section("hyper_adjust_stable")
            toggle("hyper_adjust_stable_on", on: r.stabilize, id: "hyper.ajuste.estabilizar") { v in var n = r; n.stabilize = v; flow.edit(n) }
            if r.stabilize {
                slider("hyper_adjust_strength", value: r.stabilizeStrength, id: "hyper.ajuste.estabilizar.forca") { v in
                    var n = r; n.stabilizeStrength = v; flow.edit(n)
                }
                toggle("hyper_adjust_stable_keyframes", on: r.stabilizeToKeyframes, id: "hyper.ajuste.estabilizar.keyframes") { v in
                    var n = r; n.stabilizeToKeyframes = v; flow.edit(n)
                }
                Text(AureaText.t("hyper_adjust_stable_keyframes_note")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(14).background(RoundedRectangle(cornerRadius: 12).fill(AureaColors.surface)).padding(.top, 10)
    }

    private func section(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted)
            .padding(.top, 14).padding(.bottom, 8).accessibilityAddTraits(.isHeader)
    }

    private func chips(_ labels: [String], selected: Int, id: String, _ pick: @escaping (Int) -> Void) -> some View {
        HStack(spacing: 8) {
            ForEach(Array(labels.enumerated()), id: \.offset) { i, label in
                HyperChip(title: label, id: "\(id).\(i)", on: i == selected) { pick(i) }
            }
        }
    }

    private func slider(_ key: String, value: Double, id: String, _ set: @escaping (Double) -> Void) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text(AureaText.t(key)).font(.aurea(size: 14))
                Spacer()
                Text("\(Int((value * 100).rounded()))%").font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            Slider(value: Binding(get: { value }, set: { set(($0 * 20).rounded() / 20) }), in: 0...1)
                .tint(AureaColors.accent)
                .accessibilityLabel(AureaText.t(key)).accessibilityIdentifier(id)
        }
    }

    private func toggle(_ key: String, on: Bool, id: String, _ set: @escaping (Bool) -> Void) -> some View {
        Toggle(isOn: Binding(get: { on }, set: set)) { Text(AureaText.t(key)).font(.aurea(size: 14)) }
            .tint(AureaColors.accent).frame(minHeight: 44)
            .accessibilityIdentifier(id)
    }
}

// MARK: - 3. Prévia e estimativa

@MainActor private struct HyperPreviewStep: View {
    @ObservedObject var flow: HyperFlow
    @State private var split: Double = 0.5

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Spacer().frame(height: 12)
            if flow.measuring { HyperMeterPanel(startedAt: flow.measureStartedAt) }
            beforeAfter
            Spacer().frame(height: 16)
            Text(AureaText.t("hyper_path_title")).font(.aurea(size: 17, weight: .semibold)).accessibilityAddTraits(.isHeader)
            Text(AureaText.t("hyper_path_hint")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            Spacer().frame(height: 10)
            ForEach(HyperPath.choices, id: \.rawValue) { p in
                pathRow(p)
                Spacer().frame(height: 8)
            }
            if let e = flow.estimate, e.outputWidth > 0 {
                Text(AureaText.t("hyper_output", "\(e.outputWidth) × \(e.outputHeight)", hyperFps(e.outputFps)))
                    .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            if flow.estimating {
                Spacer().frame(height: 8)
                Text(AureaText.t("hyper_estimating")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            Spacer().frame(height: 6)
            Button(AureaText.t("hyper_measure_again")) { flow.measure() }
                .font(.aurea(size: 14, weight: .semibold)).foregroundStyle(flow.measuring ? AureaColors.disabled : AureaColors.accent)
                .disabled(flow.measuring).frame(minHeight: 44).accessibilityIdentifier("hyper.medir")
            HyperErrorBox(message: flow.error)
            Spacer().frame(height: 10)
            HyperButton(title: AureaText.t("hyper_start"), id: "hyper.comecar",
                        enabled: !flow.measuring && !flow.estimating && !flow.checkingConfig && flow.estimate?.available(flow.path) != false) { flow.start() }
        }
    }

    private var beforeAfter: some View {
        let ratio: CGFloat = flow.preview.map { max(0.5, min(2.4, $0.after.size.width / max(1, $0.after.size.height))) } ?? 16.0 / 9.0
        return VStack(alignment: .leading, spacing: 0) {
            GeometryReader { geo in
                ZStack {
                    Color.black
                    if let preview = flow.preview {
                        Image(uiImage: preview.after).resizable().scaledToFit()
                        Image(uiImage: preview.before).resizable().scaledToFit()
                            .mask(alignment: .leading) { Rectangle().frame(width: geo.size.width * split) }
                        Rectangle().fill(Color.white).frame(width: 2)
                            .position(x: geo.size.width * split, y: geo.size.height / 2)
                        VStack {
                            HStack {
                                label(AureaText.t("fx_browser_before"))
                                Spacer()
                                label(AureaText.t("fx_browser_after"))
                            }.padding(8)
                            Spacer()
                        }
                    }
                    if flow.previewLoading { ProgressView().tint(AureaColors.accent) }
                    else if flow.preview == nil {
                        Text(AureaText.t(flow.previewFailed ? "hyper_preview_failed" : "hyper_preview_waiting"))
                            .font(.aurea(size: 13)).foregroundStyle(.white).multilineTextAlignment(.center).padding(16)
                    }
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0).onChanged { v in split = min(1, max(0, v.location.x / max(1, geo.size.width))) })
            }
            .aspectRatio(ratio, contentMode: .fit)
            .clipShape(RoundedRectangle(cornerRadius: 14))
            .environment(\.layoutDirection, .leftToRight)
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(AureaText.t("hyper_preview_a11y"))
            .accessibilityIdentifier("hyper.previa")
            if let preview = flow.preview {
                Slider(value: $split, in: 0...1).tint(AureaColors.muted)
                    .accessibilityLabel(AureaText.t("hyper_preview_split")).accessibilityIdentifier("hyper.previa.divisor")
                if preview.path != flow.path {
                    Text(AureaText.t("hyper_preview_updating")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                }
            }
        }
    }

    private func label(_ text: String) -> some View {
        Text(text).font(.aurea(size: 11, weight: .bold)).foregroundStyle(.white)
            .padding(.horizontal, 6).padding(.vertical, 2).background(RoundedRectangle(cornerRadius: 6).fill(Color.black.opacity(0.55)))
    }

    private func pathRow(_ p: HyperPath) -> some View {
        let block = HyperPathRules.block(p, estimate: flow.estimate, profile: flow.profile)
        let available = flow.estimate == nil || block == .none
        let on = flow.path == p && available
        let detail: String = {
            if flow.estimating || flow.estimate == nil { return AureaText.t("hyper_estimating") }
            if block != .none { return AureaText.t(block.key) }
            return AureaText.t("hyper_estimate_time", hyperTime(flow.estimate?.secondsFor(p) ?? 0))
        }()
        return Button { flow.select(p) } label: {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    HStack(spacing: 8) {
                        Text(AureaText.t(p.nameKey)).font(.aurea(size: 15, weight: .semibold))
                            .foregroundStyle(available ? AureaColors.text : AureaColors.disabled)
                        if flow.profile?.defaultPath == p {
                            Text(AureaText.t("hyper_path_suggested")).font(.aurea(size: 11, weight: .bold)).foregroundStyle(AureaColors.accent)
                        }
                    }
                    Text(AureaText.t(p.bodyKey)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    Text(detail).font(.aurea(size: 13, weight: .semibold)).foregroundStyle(available ? AureaColors.text : AureaColors.warning)
                }
                Spacer()
                Image(systemName: on ? "checkmark.circle.fill" : "circle").fontWeight(AureaIconWeight.symbol).foregroundStyle(on ? AureaColors.accent : AureaColors.muted)
            }
            .padding(14)
            .background(RoundedRectangle(cornerRadius: 12).fill(on ? AureaColors.accent.opacity(0.16) : AureaColors.surface))
            .overlay(RoundedRectangle(cornerRadius: 12).stroke(on ? AureaColors.accent : .clear, lineWidth: 1))
        }
        .buttonStyle(.plain).disabled(!available || flow.estimate == nil)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(AureaText.t(p.nameKey)). \(detail)")
        .accessibilityAddTraits(on ? [.isButton, .isSelected] : .isButton)
        .accessibilityIdentifier("hyper.caminho.\(p.id)")
    }
}

/// O medidor (~10 s na primeira vez): barra pelo tempo esperado, nunca "100%" antes de acabar.
private struct HyperMeterPanel: View {
    let startedAt: Date
    var body: some View {
        SwiftUI.TimelineView(.periodic(from: startedAt, by: 0.2)) { context in
            let elapsed = context.date.timeIntervalSince(startedAt)
            VStack(alignment: .leading, spacing: 0) {
                Text(AureaText.t("hyper_meter_title")).font(.aurea(size: 15, weight: .semibold))
                Spacer().frame(height: 4)
                Text(AureaText.t("hyper_meter_body")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer().frame(height: 10)
                ProgressView(value: min(0.95, max(0, elapsed / 10))).tint(AureaColors.accent)
                Spacer().frame(height: 6)
                Text(elapsed < 10 ? AureaText.t("hyper_meter_left", max(1, Int(ceil(10 - elapsed)))) : AureaText.t("hyper_meter_almost"))
                    .font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
            }
            .padding(14).background(RoundedRectangle(cornerRadius: 12).fill(AureaColors.surface)).padding(.bottom, 14)
        }
        .accessibilityElement(children: .combine)
        .accessibilityIdentifier("hyper.medidor")
    }
}

// MARK: - 4. Trabalhando

@MainActor private struct HyperWorkingStep: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject var flow: HyperFlow

    private var phaseDone: Bool { !model.exporting && model.exportedURL != nil }
    private var phaseFailed: Bool { !model.exporting && model.exportedURL == nil }

    var body: some View {
        let done = (model.exportProgress["framesDone"] as? NSNumber)?.intValue ?? 0
        let total = (model.exportProgress["framesTotal"] as? NSNumber)?.intValue ?? 0
        let eta = (model.exportProgress["etaSeconds"] as? NSNumber)?.intValue ?? 0
        let fraction = total > 0 ? min(1, Double(done) / Double(total)) : 0
        VStack(alignment: .leading, spacing: 0) {
            Spacer().frame(height: 20)
            Text(AureaText.t(titleKey)).font(.aurea(size: 22, weight: .bold)).accessibilityAddTraits(.isHeader)
            Spacer().frame(height: 14)
            ForEach(steps(framesDone: done), id: \.0.rawValue) { step, state in stepRow(step, state) }
            if let info = flow.jobInfo, info.passCount > 1 {
                Spacer().frame(height: 6)
                Text(AureaText.t("hyper_pass", max(1, info.pass), info.passCount)).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            if flow.jobInfo?.audioMuted == true || flow.recipe.slowMotion > 1 {
                Spacer().frame(height: 6)
                Text(AureaText.t("hyper_audio_muted")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
            }
            Spacer().frame(height: 14)
            if model.exporting {
                ProgressView(value: fraction).tint(AureaColors.accent).accessibilityIdentifier("hyper.progresso")
                Spacer().frame(height: 8)
                Text(progressText(fraction: fraction, done: done, total: total, eta: eta)).font(.aurea(size: 14, weight: .semibold))
                if let message = model.exportMessage, !message.isEmpty {
                    Spacer().frame(height: 6)
                    Text(message).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                }
                Spacer().frame(height: 12)
                Text(AureaText.t("hyper_can_leave_ios")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer().frame(height: 18)
                HyperButton(title: AureaText.t(flow.cancelRequested ? "hyper_working_cancelling" : "common_cancel"), id: "hyper.cancelar",
                            primary: false, enabled: !flow.cancelRequested) { flow.cancel() }
            } else if phaseDone {
                if flow.finishing {
                    ProgressView().tint(AureaColors.accent)
                    Spacer().frame(height: 8)
                    Text(AureaText.t("hyper_creating_project")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted)
                }
                HyperErrorBox(message: flow.error)
                if flow.error != nil && !flow.finishing {
                    Spacer().frame(height: 12)
                    HyperButton(title: AureaText.t("home_retry"), id: "hyper.tentar") { flow.error = nil; flow.finishIfDone() }
                }
            } else {
                HyperErrorBox(message: model.exportMessage, warning: flow.cancelRequested)
                Spacer().frame(height: 16)
                HyperButton(title: AureaText.t("home_retry"), id: "hyper.tentar") { flow.retry() }
                Spacer().frame(height: 10)
                HyperButton(title: AureaText.t("hyper_back_to_preview"), id: "hyper.voltar.previa", primary: false) { flow.back() }
            }
        }
        .onAppear { flow.finishIfDone() }
    }

    private var titleKey: String {
        if model.exporting { return flow.cancelRequested ? "hyper_working_cancelling" : "hyper_working_title" }
        if phaseDone { return "hyper_working_done" }
        return flow.cancelRequested ? "hyper_working_cancelled" : "hyper_working_failed"
    }

    private func progressText(fraction: Double, done: Int, total: Int, eta: Int) -> String {
        var parts = ["\(Int(fraction * 100))%"]
        if total > 0 { parts.append(AureaText.t("hyper_frames", done, total)) }
        if eta > 0 { parts.append(AureaText.t("hyper_time_left", hyperTime(Double(eta)))) }
        return parts.joined(separator: " · ")
    }

    /// Os passos que o motor diz que existem (hyper_job_steps); sem ele, os da receita.
    private func steps(framesDone: Int) -> [(HyperStep, HyperStepState)] {
        if let info = flow.jobInfo {
            return HyperStep.allCases.filter { info.present($0) }.map { ($0, phaseDone ? .done : info.state($0)) }
        }
        let r = flow.recipe
        let wanted = HyperStep.allCases.filter { step in
            switch step {
            case .clean: return r.denoise > 0 || r.deblock || r.deinterlace > 0
            case .stable: return r.stabilize
            case .upscale: return r.upscale > 1
            case .face: return r.face
            case .motion: return r.targetFps > 0 || r.slowMotion > 1
            default: return true
            }
        }
        return wanted.map { step in
            if phaseDone { return (step, .done) }
            if !model.exporting { return (step, .waiting) }
            if step == .prepare { return (step, framesDone > 0 ? .done : .running) }
            if step == .finish { return (step, .waiting) }
            return (step, framesDone > 0 ? .running : .waiting)
        }
    }

    private func stepRow(_ step: HyperStep, _ state: HyperStepState) -> some View {
        let path = flow.jobInfo.flatMap { $0.present(step) ? $0.path(step) : nil }.flatMap { $0 == .auto || !step.processing ? nil : $0 }
        let stateKey: String = {
            switch state {
            case .done: return "hyper_state_done"
            case .running: return "hyper_state_running"
            case .skipped: return "hyper_state_skipped"
            case .waiting: return "hyper_state_waiting"
            }
        }()
        return HStack(spacing: 12) {
            Group {
                switch state {
                case .done: Image(systemName: "checkmark.circle.fill").fontWeight(AureaIconWeight.symbol).foregroundStyle(AureaColors.success)
                case .running: ProgressView().controlSize(.small).tint(AureaColors.accent)
                case .skipped: Image(systemName: "minus").fontWeight(AureaIconWeight.symbol).foregroundStyle(AureaColors.muted)
                case .waiting: Image(systemName: "circle").fontWeight(AureaIconWeight.symbol).foregroundStyle(AureaColors.muted)
                }
            }.frame(width: 22, height: 22)
            Text(AureaText.t(step.nameKey)).font(.aurea(size: 15, weight: state == .running ? .semibold : .regular))
                .foregroundStyle(state == .waiting || state == .skipped ? AureaColors.muted : AureaColors.text)
            Spacer()
            if let path { Text(AureaText.t(path.nameKey)).font(.aurea(size: 12, weight: .semibold)).foregroundStyle(AureaColors.muted) }
        }
        .frame(minHeight: 36)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(AureaText.t(step.nameKey) + ": " + AureaText.t(stateKey) + (path.map { " (\(AureaText.t($0.nameKey)))" } ?? ""))
        .accessibilityIdentifier("hyper.etapa.\(step.id)")
    }
}

// MARK: - Peças

private struct HyperErrorBox: View {
    let message: String?
    var warning = false
    var body: some View {
        if let message, !message.isEmpty {
            Text(message).font(.aurea(size: 14)).foregroundStyle(warning ? AureaColors.warning : AureaColors.danger)
                .frame(maxWidth: .infinity, alignment: .leading).padding(14)
                .background(RoundedRectangle(cornerRadius: 10).fill(AureaColors.surface))
                .padding(.top, 14)
                .accessibilityIdentifier("hyper.erro")
        }
    }
}

private struct HyperChip: View {
    let title: String
    let id: String
    let on: Bool
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(title).font(.aurea(size: 14, weight: .semibold))
                .foregroundStyle(on ? AureaColors.text : AureaColors.muted)
                .padding(.horizontal, 16).frame(minHeight: 40)
                .background(RoundedRectangle(cornerRadius: 10).fill(on ? AureaColors.accent.opacity(0.22) : AureaColors.chip))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(title)
        .accessibilityAddTraits(on ? .isSelected : [])
        .accessibilityIdentifier(id)
    }
}

struct HyperButton: View {
    let title: String
    let id: String
    var primary = true
    var enabled = true
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            Text(title).font(.aurea(size: 15, weight: .semibold))
                .foregroundStyle(!enabled ? AureaColors.disabled : (primary ? AureaColors.onAccent : AureaColors.text))
                .frame(maxWidth: .infinity, minHeight: 48)
                .background(RoundedRectangle(cornerRadius: 12).fill(!enabled ? AureaColors.surface : (primary ? AureaColors.accent : AureaColors.chip)))
        }
        .buttonStyle(.plain).disabled(!enabled)
        .accessibilityLabel(title).accessibilityIdentifier(id)
    }
}

func hyperTime(_ seconds: Double) -> String {
    let s = max(1, Int(seconds.rounded()))
    if s < 60 { return AureaText.t("hyper_time_seconds", s) }
    if s < 3600 { return AureaText.t("hyper_time_minutes", (s + 30) / 60) }
    return AureaText.t("hyper_time_hours", s / 3600, (s % 3600) / 60)
}

func hyperFps(_ fps: Double) -> String {
    let r = (fps * 100).rounded() / 100
    return (r == r.rounded() ? "\(Int(r))" : "\(r)") + " fps"
}

func hyperDuration(_ seconds: Double) -> String {
    let s = max(0, Int(seconds.rounded()))
    return s >= 3600 ? String(format: "%d:%02d:%02d", s / 3600, (s % 3600) / 60, s % 60) : String(format: "%d:%02d", s / 60, s % 60)
}

// MARK: - Seletores

/// Só vídeos (PHPicker). O arquivo temporário do provedor é copiado enquanto o callback vive.
private struct HyperVideoPicker: UIViewControllerRepresentable {
    let onDismiss: () -> Void
    let onPick: (URL) -> Void
    let onError: (String) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIViewController(context: Context) -> PHPickerViewController {
        var configuration = PHPickerConfiguration()
        configuration.filter = .videos
        configuration.selectionLimit = 1
        configuration.preferredAssetRepresentationMode = .current
        let picker = PHPickerViewController(configuration: configuration)
        picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: PHPickerViewController, context: Context) {}
    final class Coordinator: NSObject, PHPickerViewControllerDelegate {
        let parent: HyperVideoPicker
        init(_ parent: HyperVideoPicker) { self.parent = parent }
        func picker(_ picker: PHPickerViewController, didFinishPicking results: [PHPickerResult]) {
            guard let provider = results.first?.itemProvider else { parent.onDismiss(); return }
            let identifier = provider.registeredTypeIdentifiers.first { UTType($0)?.conforms(to: .movie) == true } ?? UTType.movie.identifier
            provider.loadFileRepresentation(forTypeIdentifier: identifier) { [parent] url, error in
                guard let url else {
                    DispatchQueue.main.async { parent.onError(error?.localizedDescription ?? AureaText.t("ios_media_open_failed")) }
                    return
                }
                do {
                    let copy = try hyperTemporaryCopy(of: url)
                    DispatchQueue.main.async { parent.onPick(copy) }
                } catch {
                    DispatchQueue.main.async { parent.onError(error.localizedDescription) }
                }
            }
        }
    }
}

/// "Gravar agora": a câmera do sistema em modo vídeo.
private struct HyperCameraPicker: UIViewControllerRepresentable {
    let onDismiss: () -> Void
    let onPick: (URL) -> Void
    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIViewController(context: Context) -> UIImagePickerController {
        let picker = UIImagePickerController()
        picker.sourceType = .camera
        picker.mediaTypes = [UTType.movie.identifier]
        picker.cameraCaptureMode = .video
        picker.videoQuality = .typeHigh
        picker.delegate = context.coordinator
        return picker
    }
    func updateUIViewController(_ controller: UIImagePickerController, context: Context) {}
    final class Coordinator: NSObject, UIImagePickerControllerDelegate, UINavigationControllerDelegate {
        let parent: HyperCameraPicker
        init(_ parent: HyperCameraPicker) { self.parent = parent }
        func imagePickerController(_ picker: UIImagePickerController, didFinishPickingMediaWithInfo info: [UIImagePickerController.InfoKey: Any]) {
            guard let url = info[.mediaURL] as? URL, let copy = try? hyperTemporaryCopy(of: url) else { parent.onDismiss(); return }
            parent.onPick(copy)
        }
        func imagePickerControllerDidCancel(_ picker: UIImagePickerController) { parent.onDismiss() }
    }
}

private func hyperTemporaryCopy(of url: URL) throws -> URL {
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent("HyperImports", isDirectory: true)
        .appendingPathComponent(UUID().uuidString, isDirectory: true)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    let copy = directory.appendingPathComponent(url.lastPathComponent)
    try FileManager.default.copyItem(at: url, to: copy)
    return copy
}
