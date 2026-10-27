// Rig3DPanel.kt (Android): aba ESQUELETO do modelo 3D importado (0.3.6 BETA).
// Tudo no motor (rig/AutoRig.hpp); aqui só os comandos:
//  - AutoRig 3D (sem esqueleto): progresso real e Cancelar.
//  - Corrigir esqueleto: AutoRig = arrastar as juntas na Cena 3D; rig do
//    arquivo = escolher, para cada osso canônico, a junta do arquivo.
//  - Ossos: tocar abre a Cena 3D no modo de pose com o osso escolhido.
//  - IK de braços e pernas: liga/desliga e keyframe do controlador.
//  - Motion Capture (BETA) logo abaixo (MocapPanel.swift).
import SwiftUI

struct Rig3DSection: View {
    @EnvironmentObject private var model: AureaModel
    let layerId: Int64
    @State private var assignList: Bool?
    @State private var assigning: Int?

    private static let limbKeys = ["rig3d_limb_0", "rig3d_limb_1", "rig3d_limb_2", "rig3d_limb_3"]

    var body: some View {
        let status = model.humanoidStatus
        VStack(alignment: .leading, spacing: 0) {
            title("rig3d_tab")
            Text(AureaText.t(["rig3d_status_none", "rig3d_status_other", "rig3d_status_compatible", "rig3d_status_auto"][min(3, max(0, status))]))
                .font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.bottom, 6)
                .accessibilityIdentifier("rig3d.status")
            if model.autoRigState == 1 {
                working
            } else if status == 0 {
                card("rig3d_autorig", "rig3d_autorig_desc", id: "rig3d.autorig") { model.startAutoRig() }
                Spacer().frame(height: 6)
                note("rig3d_tpose_hint")
            } else {
                rigged(status)
            }
        }
        .sheet(item: Binding(get: { assigning.map { AssignTarget(bone: $0) } }, set: { assigning = $0?.bone })) { target in
            assignSheet(target.bone)
        }
    }

    private struct AssignTarget: Identifiable { let bone: Int; var id: Int { bone } }

    private var working: some View {
        let pct = Int((model.autoRigProgress * 100).rounded(.down))
        let label = AureaText.t("rig3d_working", min(100, max(0, pct)))
        return VStack(alignment: .leading, spacing: 6) {
            Text(label).font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
            ProgressView(value: Double(model.autoRigProgress), total: 1).tint(AureaColors.accent)
                .accessibilityLabel(label)
            chip("rig3d_cancel", on: false) { model.cancelAutoRig() }.accessibilityIdentifier("rig3d.cancel")
        }
    }

    @ViewBuilder private func rigged(_ status: Int) -> some View {
        let showList = assignList ?? (status == 1)
        card("rig3d_fix", "rig3d_fix_desc", id: "rig3d.fix") {
            if status == 3 { model.setRigFix(true) } else { assignList = !showList }
        }
        if status >= 2 {
            Spacer().frame(height: 6)
            card("rig3d_animate", "rig3d_animate_desc", id: "rig3d.animate") { model.animateBones() }
        }
        let joints = model.humanoidJoints
        let f = AureaModel.humanoidJointFloats
        if !joints.isEmpty {
            if status != 3 && showList {
                title("rig3d_bones")
                note("rig3d_fix_assign_hint")
                horizontal {
                    ForEach(0..<min(17, joints.count / f), id: \.self) { b in
                        chip(AureaModel.humanoidBoneKeys[b], on: joints[b * f] >= 0) { assigning = b }
                    }
                }
            } else if status >= 2 {
                title("rig3d_bones")
                horizontal {
                    ForEach(0..<min(17, joints.count / f), id: \.self) { b in
                        let node = Int32(joints[b * f])
                        if node >= 0 {
                            chip(AureaModel.humanoidBoneKeys[b], on: model.rigPoseMode && model.poseBone == node) { model.animateBones(node) }
                                .accessibilityIdentifier("rig3d.bone.\(b)")
                        }
                    }
                }
            }
        }
        let ik = model.humanoidIk
        let g = AureaModel.humanoidIkFloats
        if status >= 2 && ik.count >= 4 * g {
            title("rig3d_ik")
            horizontal {
                ForEach(0..<4, id: \.self) { limb in
                    let on = ik[limb * g]
                    if on >= 0 {
                        chip(Self.limbKeys[limb], on: on > 0.5) { model.setHumanoidIk(limb, on: on <= 0.5) }
                            .accessibilityIdentifier("rig3d.ik.\(limb)")
                    }
                }
            }
            if (0..<4).contains(where: { ik[$0 * g] > 0.5 }) {
                note("rig3d_ik_hint")
                horizontal {
                    ForEach(0..<4, id: \.self) { limb in
                        if ik[limb * g] > 0.5 {
                            chip(Self.limbKeys[limb], prefix: "◆ ", on: ik[limb * g + 5] > 0.5) { model.toggleHumanoidIkKey(limb) }
                        }
                    }
                }
            }
        }
        // Motion Capture (BETA) logo abaixo, no mesmo painel (MocapSection.kt). Também com
        // esqueleto do arquivo não humanoide (status 1): a seção pede o mapeamento em "Corrigir esqueleto".
        if status >= 1 {
            MocapSection(engine: model.engine, layer: layerId, onAutoRig: { model.startAutoRig() })
        }
    }

    /// Escolher a junta do arquivo para o osso canônico `bone` (rig importado).
    private func assignSheet(_ bone: Int) -> some View {
        let f = AureaModel.boneFloats
        // Os ossos do arquivo (a lista da pose só se enche dentro da Cena 3D).
        let bones = model.engine.modelBones(layerId).map(\.floatValue)
        let nodes: [Int32] = stride(from: 0, to: bones.count - f + 1, by: f).map { Int32(bones[$0]) }
        let jf = AureaModel.humanoidJointFloats
        let current: Int32 = bone * jf < model.humanoidJoints.count ? Int32(model.humanoidJoints[bone * jf]) : -1
        return NavigationStack {
            List {
                Button(AureaText.t("rig3d_none_node")) { model.assignHumanoidBone(bone, node: -1); assigning = nil }
                    .foregroundStyle(current < 0 ? AureaColors.accent : AureaColors.text)
                ForEach(nodes, id: \.self) { node in
                    let name = model.modelNodeName(node)
                    Button(name.isEmpty ? "#\(node)" : name) { model.assignHumanoidBone(bone, node: node); assigning = nil }
                        .foregroundStyle(node == current ? AureaColors.accent : AureaColors.text)
                        .accessibilityAddTraits(node == current ? [.isSelected] : [])
                }
            }
            .navigationTitle(AureaText.t("rig3d_choose_node", AureaText.t(AureaModel.humanoidBoneKeys[min(16, max(0, bone))])))
            .navigationBarTitleDisplayMode(.inline)
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button(AureaText.t("rig3d_cancel")) { assigning = nil } } }
        }
        .presentationDetents([.medium, .large])
    }

    // --- Peças (as mesmas do Panel3DView) ------------------------------------

    private func title(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold))
            .foregroundStyle(AureaColors.muted).padding(.top, 8).padding(.bottom, 4)
    }
    private func note(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12)).lineSpacing(2)
            .foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }
    private func horizontal<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        ScrollView(.horizontal, showsIndicators: false) { HStack(spacing: 6, content: content) }
    }
    private func chip(_ key: String, prefix: String = "", on: Bool, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(prefix + AureaText.t(key)).font(.aurea(size: 12)).foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                .padding(.horizontal, 12).padding(.vertical, 6).frame(minHeight: 44)
                .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                .contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityAddTraits(on ? [.isSelected] : [])
    }
    private func card(_ name: String, _ detail: String, id: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(alignment: .leading, spacing: 2) {
                Text(AureaText.t(name)).font(.aurea(size: 14, weight: .semibold)).foregroundStyle(AureaColors.text)
                Text(AureaText.t(detail)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 14).padding(.vertical, 11)
            .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 12))
        }
        .buttonStyle(AureaPressStyle())
        .accessibilityIdentifier(id)
    }
}
