// Rig3DPanel.kt (Android): aba ESQUELETO do modelo 3D importado (0.3.6 BETA).
// Tudo no motor (rig/AutoRig.hpp); aqui só os comandos:
//  - AutoRig 3D (sem esqueleto): progresso real e Cancelar.
//  - Corrigir esqueleto: AutoRig = arrastar as juntas na Cena 3D; rig do
//    arquivo = escolher, para cada osso canônico, a junta do arquivo.
//  - Ossos: tocar abre a Cena 3D no modo de pose com o osso escolhido.
//  - IK de braços e pernas: liga/desliga e keyframe do controlador.
//  - Motion Capture (BETA) logo abaixo (MocapPanel.swift).
import SwiftUI
import UIKit

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
        // 0.4.5: o painel Esqueleto (árvore de ossos, rotação numérica, espelhar L/R) —
        // também para esqueleto do arquivo que não é humanoide.
        Spacer().frame(height: 6)
        card("skeleton_open", "skeleton_desc", id: "rig3d.skeleton") { model.openSkeletonPanel(true) }
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

// =============================================================================
// ESQUELETO 3D (0.4.5) — port de editor/panels/SkeletonPanel.kt (Android).
// Pedido de ⚡ᴍᴀʀᴋᴢ, pauloph2517 e Jess: uma interface própria para o esqueleto.
// Mora na área de baixo da Cena 3D (não cobre o palco) e fala com
// o MESMO osso escolhido do palco. Tudo que muda dado vai pelo motor
// (EngineRigSkeleton.cpp), um passo de desfazer por ação.
// =============================================================================

/// Anéis da articulação no palco (RigPoseStage.kt): raio em pt e as cores dos eixos locais.
enum SkeletonRing {
    static let radius: CGFloat = 52
    /// X vermelho, Y verde, Z azul (como no Blender/Maya).
    static let colors: [Color] = [Color(hex: 0xFFFF5A5F), Color(hex: 0xFF5BD86A), Color(hex: 0xFF4F9BFF)]
}

/// O desenho do osso no palco, como o octaedro do Blender visto de lado (RigPoseStage.kt).
enum SkeletonBoneInk {
    static let light = Color(hex: 0xFFC9CDD3), mid = Color(hex: 0xFFA6ABB3), dark = Color(hex: 0xFF7F858E)
    static let line = Color(hex: 0xFF0B0E12)
    static let onLight = Color(hex: 0xFFE3F2FB), onMid = Color(hex: 0xFFA9D3EC), onDark = Color(hex: 0xFF6FAED9)

    private static func facet(_ context: inout GraphicsContext, _ p: CGPoint, _ q: CGPoint, _ r: CGPoint, _ color: Color) {
        var path = Path()
        path.move(to: p); path.addLine(to: q); path.addLine(to: r); path.closeSubpath()
        context.fill(path, with: .color(color))
    }
    /// De `a` (pai) a `c` (filho): largo no pai, fino no filho; `on` = osso escolhido.
    static func draw(_ context: inout GraphicsContext, from a: CGPoint, to c: CGPoint, on: Bool) {
        let dx = c.x - a.x, dy = c.y - a.y, len = hypot(dx, dy)
        guard len >= 1 else { return }
        let ux = dx / len, uy = dy / len, w = min(12, max(2.5, len * 0.12))
        let k = CGPoint(x: a.x + ux * len * 0.12, y: a.y + uy * len * 0.12)
        let left = CGPoint(x: k.x - uy * w, y: k.y + ux * w), right = CGPoint(x: k.x + uy * w, y: k.y - ux * w)
        facet(&context, left, c, k, on ? onLight : light)
        facet(&context, right, c, k, on ? onDark : dark)
        facet(&context, a, left, k, on ? onMid : mid)
        facet(&context, a, right, k, on ? onDark : dark)
        var ridge = Path()
        ridge.move(to: k); ridge.addLine(to: c)
        ridge.move(to: left); ridge.addLine(to: right)
        context.stroke(ridge, with: .color(line), lineWidth: 0.75)
        var edge = Path()
        edge.move(to: a); edge.addLine(to: left); edge.addLine(to: c); edge.addLine(to: right); edge.closeSubpath()
        context.stroke(edge, with: .color(line), lineWidth: 1)
    }
}

struct SkeletonPanelView: View {
    @EnvironmentObject private var model: AureaModel
    @State private var query = ""
    @State private var collapsed: Set<Int> = []
    @State private var names: [Int32: String] = [:]
    @State private var namesLayer: Int64 = 0
    @State private var tab = 0

    private struct Row: Identifiable { let slot: Int; let depth: Int; var id: Int { slot } }

    var body: some View {
        if model.poseBones.isEmpty { emptyView } else { bonesView }
    }

    /// Par do `SkeletonPanel` do Android: no alto o osso e o keyframe dele; embaixo as abas
    /// Ossos (árvore com busca), Osso (rotação, zerar, espelhar) e Pose, em alvos grandes.
    private var bonesView: some View {
        let b = model.poseBones, f = AureaModel.boneFloats
        let count = b.count / f
        let nodes: [Int32] = (0..<count).map { Int32(b[$0 * f]) }
        let parents: [Int] = (0..<count).map { Int(b[$0 * f + 1]) }
        let chosen = model.poseBone
        let chosenSlot = nodes.firstIndex(of: chosen) ?? -1
        return VStack(spacing: 0) {
            boneHeader(b, f, chosen, chosenSlot)
            tabBar
            Color.clear.frame(height: 6)
            switch tab {
            case 0: treeTab(count, b, f, nodes, parents, chosenSlot)
            case 1: boneTab(b, f, chosen, chosenSlot)
            default: poseTab
            }
        }
        .background(AureaColors.editorPanel)
        .accessibilityIdentifier("skeleton.panel")
        .onAppear { loadNames(nodes) }
        .onChange(of: nodes) { now in loadNames(now) }
        .onChange(of: chosenSlot) { slot in
            // Osso escolhido no palco: abre os pais dele na árvore.
            var p = slot >= 0 && slot < parents.count ? parents[slot] : -1
            while p >= 0 && p < parents.count { collapsed.remove(p); p = parents[p] }
        }
    }

    /// Modelo sem esqueleto: criar com o AutoRig, com o progresso real.
    private var emptyView: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                if model.autoRigState == 1 {
                    Text(AureaText.t("rig3d_working", min(100, max(0, Int(model.autoRigProgress * 100)))))
                        .font(.aurea(size: 14)).foregroundStyle(AureaColors.text)
                    ProgressView(value: Double(model.autoRigProgress)).tint(AureaColors.accent)
                    tile(CupertinoGlyph.Xmark, "rig3d_cancel", id: "skeleton.autorig.cancel") { model.cancelAutoRig() }
                } else {
                    Text(AureaText.t("rig3d_status_none")).font(.aurea(size: 15, weight: .semibold)).foregroundStyle(AureaColors.text)
                    note("rig3d_tpose_hint")
                    tile(CupertinoGlyph.Sparkles, "rig3d_autorig", id: "skeleton.autorig", on: true) { model.startAutoRig() }
                }
            }.padding(14).frame(maxWidth: .infinity, alignment: .leading)
        }.background(AureaColors.editorPanel)
    }

    private func boneHeader(_ b: [Float], _ f: Int, _ chosen: Int32, _ slot: Int) -> some View {
        let partner = slot >= 0 ? model.bonePartner(chosen) : -1
        let keyed = slot >= 0 && b[slot * f + 6] > 0.5
        return HStack(spacing: 0) {
            VStack(alignment: .leading, spacing: 0) {
                Text(slot >= 0 ? name(chosen) : AureaText.t("skeleton_bone_none"))
                    .font(.aurea(size: slot >= 0 ? 15 : 13, weight: slot >= 0 ? .bold : .regular))
                    .foregroundStyle(slot >= 0 ? AureaColors.text : AureaColors.muted).lineLimit(1)
                    .accessibilityIdentifier("skeleton.bone.name")
                if partner >= 0 {
                    Text(AureaText.t("skeleton_partner", name(partner))).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).lineLimit(1)
                }
            }
            Spacer(minLength: 4)
            if slot >= 0 {
                Button { model.togglePoseKey(all: false) } label: {
                    CupertinoGlyph.text(keyed ? CupertinoGlyph.SuitDiamondFill : CupertinoGlyph.SuitDiamond, size: 22, color: keyed ? AureaColors.keyframe : AureaColors.text)
                        .frame(width: 48, height: 48).contentShape(Rectangle())
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("rig_key_bone"))
                    .accessibilityAddTraits(keyed ? [.isSelected] : []).accessibilityIdentifier("skeleton.key.bone")
            }
        }.padding(.leading, 14).padding(.trailing, 4).frame(height: 50)
    }

    private var tabBar: some View {
        HStack(spacing: 0) {
            ForEach(Array(["skeleton_bones", "skeleton_tab_bone", "skeleton_pose"].enumerated()), id: \.offset) { i, key in
                let on = i == tab
                Button { tab = i } label: {
                    Text(AureaText.t(key)).font(.aurea(size: 13, weight: on ? .semibold : .regular)).lineLimit(1)
                        .foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .background(on ? AureaColors.accentDim : Color.clear, in: RoundedRectangle(cornerRadius: 8)).contentShape(Rectangle())
                }.buttonStyle(.plain).accessibilityAddTraits(on ? [.isSelected] : []).accessibilityIdentifier("skeleton.tab.\(i)")
            }
        }.padding(3).frame(height: 44)
            .background(AureaColors.background, in: RoundedRectangle(cornerRadius: 10)).padding(.horizontal, 12)
    }

    private func treeTab(_ count: Int, _ b: [Float], _ f: Int, _ nodes: [Int32], _ parents: [Int], _ chosenSlot: Int) -> some View {
        let rows = visibleRows(count, parents)
        return VStack(spacing: 4) {
            searchBox
            if rows.isEmpty { note("skeleton_no_match") }
            ScrollView {
                LazyVStack(alignment: .leading, spacing: 0) {
                    ForEach(rows) { row in boneRow(row, b, f, nodes, parents, chosenSlot) }
                }
            }
        }.padding(.horizontal, 12)
    }

    private func boneTab(_ b: [Float], _ f: Int, _ chosen: Int32, _ slot: Int) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 8) {
                if slot < 0 { note("skeleton_bone_none") } else {
                    let o = slot * f
                    HStack(spacing: 6) {
                        ForEach(0..<3, id: \.self) { axis in
                            SkeletonAxisField(label: ["X", "Y", "Z"][axis], color: SkeletonRing.colors[axis], value: b[o + 2 + axis]) { value in
                                var v = [b[o + 2], b[o + 3], b[o + 4]]
                                v[axis] = value
                                model.setBoneRotation(chosen, x: v[0], y: v[1], z: v[2])
                            }
                            .id("\(chosen).\(axis)")
                        }
                    }
                    HStack(spacing: 6) {
                        tile(CupertinoGlyph.Minus, "skeleton_zero_bone", id: "skeleton.zero.bone") { model.zeroPose(all: false) }
                        tile(CupertinoGlyph.ArrowLeftRight, "skeleton_mirror_bone", id: "skeleton.mirror.bone", enabled: model.bonePartner(chosen) >= 0) { model.mirrorPose(all: false) }
                        tile(CupertinoGlyph.ArrowCounterclockwise, "rig_reset_bone", id: "skeleton.reset.bone") { model.resetPose(all: false) }
                    }
                }
            }.padding(.horizontal, 12).padding(.bottom, 8).frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var poseTab: some View {
        ScrollView {
            VStack(spacing: 6) {
                HStack(spacing: 6) {
                    tile(CupertinoGlyph.SuitDiamond, "rig_key_all", id: "skeleton.key.all") { model.togglePoseKey(all: true) }
                    tile(CupertinoGlyph.DocOnDoc, "skeleton_copy", id: "skeleton.copy") { model.copyPose() }
                    tile(CupertinoGlyph.DocOnClipboard, "skeleton_paste", id: "skeleton.paste", enabled: !model.poseClipboard.isEmpty) { model.pastePose() }
                }
                HStack(spacing: 6) {
                    tile(CupertinoGlyph.ArrowLeftRight, "skeleton_mirror_pose", id: "skeleton.mirror.pose") { model.mirrorPose(all: true) }
                    tile(CupertinoGlyph.Minus, "skeleton_zero_pose", id: "skeleton.zero.pose") { model.zeroPose(all: true) }
                    tile(CupertinoGlyph.ArrowCounterclockwise, "rig_reset_all", id: "skeleton.reset.all") { model.resetPose(all: true) }
                }
                tile(model.skeletonHidden ? CupertinoGlyph.EyeSlash : CupertinoGlyph.Eye, "skeleton_show", id: "skeleton.show", on: !model.skeletonHidden) { model.skeletonHidden.toggle() }
            }.padding(.horizontal, 12).padding(.bottom, 8)
        }
    }

    /// Ação do esqueleto: ícone e nome num alvo de 68 pt.
    private func tile(_ glyph: Character, _ key: String, id: String, on: Bool = false, enabled: Bool = true, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 4) {
                CupertinoGlyph.text(glyph, size: 20, color: on ? AureaColors.accent : AureaColors.text)
                Text(AureaText.t(key)).font(.aurea(size: 11)).foregroundStyle(on ? AureaColors.accent : AureaColors.text)
                    .lineLimit(2).multilineTextAlignment(.center)
            }
            .padding(.horizontal, 4).frame(maxWidth: .infinity).frame(height: 68)
            .background(on ? AureaColors.accentDim : AureaColors.chip, in: RoundedRectangle(cornerRadius: 10)).contentShape(Rectangle())
        }
        .buttonStyle(AureaPressStyle()).disabled(!enabled).opacity(enabled ? 1 : 0.35)
        .accessibilityAddTraits(on ? [.isSelected] : []).accessibilityIdentifier(id)
    }

    /// Nomes dos nós: só refaz com outro modelo (a pose muda a cada arrasto, os nomes não).
    private func loadNames(_ nodes: [Int32]) {
        let layer = model.primarySelection ?? 0
        if layer == namesLayer && nodes.allSatisfy({ names[$0] != nil }) { return }
        var next: [Int32: String] = [:]
        for n in nodes {
            let s = model.modelNodeName(n)
            next[n] = s.isEmpty ? "#\(n)" : s
        }
        names = next
        namesLayer = layer
    }
    private func name(_ node: Int32) -> String { names[node] ?? "#\(node)" }

    /// Profundidade-primeiro a partir das raízes, pulando os filhos recolhidos; com busca, a lista filtrada.
    private func visibleRows(_ count: Int, _ parents: [Int]) -> [Row] {
        let q = query.trimmingCharacters(in: .whitespaces).lowercased()
        let f = AureaModel.boneFloats
        if !q.isEmpty {
            return (0..<count).filter { name(Int32(model.poseBones[$0 * f])).lowercased().contains(q) }
                .map { Row(slot: $0, depth: 0) }
        }
        var children = Array(repeating: [Int](), count: count)
        var roots: [Int] = []
        for i in 0..<count {
            if parents[i] >= 0 && parents[i] < count { children[parents[i]].append(i) } else { roots.append(i) }
        }
        var out: [Row] = []
        func walk(_ i: Int, _ depth: Int) {
            out.append(Row(slot: i, depth: depth))
            if !collapsed.contains(i) { for c in children[i] { walk(c, depth + 1) } }
        }
        roots.forEach { walk($0, 0) }
        return out
    }

    private func boneRow(_ row: Row, _ b: [Float], _ f: Int, _ nodes: [Int32], _ parents: [Int], _ chosenSlot: Int) -> some View {
        let slot = row.slot
        let node = nodes[slot]
        let on = slot == chosenSlot
        let keyed = b[slot * f + 6] > 0.5, animated = b[slot * f + 5] > 0.5
        let expandable = query.isEmpty && parents.contains(slot)
        let open = !collapsed.contains(slot)
        let label = name(node)
        return HStack(spacing: 0) {
            if expandable {
                Button {
                    if open { collapsed.insert(slot) } else { collapsed.remove(slot) }
                } label: {
                    CupertinoGlyph.text(open ? CupertinoGlyph.ChevronDown : CupertinoGlyph.ChevronRight, size: 12, color: AureaColors.muted)
                        .frame(width: 44, height: 44).contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel(AureaText.t(open ? "skeleton_collapse" : "skeleton_expand", label))
            } else {
                Color.clear.frame(width: 44, height: 44).accessibilityHidden(true)
            }
            Button { model.choosePoseBone(node) } label: {
                HStack {
                    Text(label).font(.aurea(size: 14, weight: on ? .bold : .medium))
                        .foregroundStyle(on ? AureaColors.accent : AureaColors.text).lineLimit(1)
                    Spacer(minLength: 4)
                    if keyed || animated {
                        Text(keyed ? "◆" : "◇").font(.aurea(size: 12)).foregroundStyle(keyed ? AureaColors.accent : AureaColors.muted)
                    }
                }
                .frame(minHeight: 48).contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel(AureaText.t("skeleton_bone_a11y", label, row.depth + 1))
            .accessibilityValue(keyed ? AureaText.t("skeleton_keyed") : "")
            .accessibilityAddTraits(on ? [.isSelected] : [])
        }
        .padding(.leading, CGFloat(4 + 12 * min(row.depth, 10))).padding(.trailing, 8)
        .background(on ? AureaColors.accentDim : Color.clear, in: RoundedRectangle(cornerRadius: 8))
    }

    private var searchBox: some View {
        HStack(spacing: 8) {
            CupertinoGlyph.text(CupertinoGlyph.Search, size: 14, color: AureaColors.muted).accessibilityHidden(true)
            TextField(AureaText.t("skeleton_search"), text: $query)
                .font(.aurea(size: 13)).foregroundStyle(AureaColors.text)
                .textInputAutocapitalization(.never).autocorrectionDisabled()
                .accessibilityIdentifier("skeleton.search")
            if !query.isEmpty {
                Button { query = "" } label: {
                    CupertinoGlyph.text(CupertinoGlyph.Xmark, size: 12, color: AureaColors.muted).frame(width: 36, height: 36)
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("common_close"))
            }
        }
        .padding(.horizontal, 10).frame(height: 40)
        .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
    }

    // --- Peças (as mesmas da seção do Esqueleto) -------------------------------
    private func title(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 13, weight: .bold))
            .foregroundStyle(AureaColors.muted).padding(.top, 8).padding(.bottom, 4)
    }
    private func note(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12)).lineSpacing(2)
            .foregroundStyle(AureaColors.muted).fixedSize(horizontal: false, vertical: true)
    }
}

/// Campo de rotação em graus com −/+ 5°: o valor entra ao confirmar (um desfazer), não a cada tecla.
private struct SkeletonAxisField: View {
    let label: String
    let color: Color
    let value: Float
    let onCommit: (Float) -> Void
    @State private var draft = ""
    @FocusState private var focused: Bool

    private var shown: String { String(format: "%.1f", locale: Locale(identifier: "en_US_POSIX"), Double(value)) }

    var body: some View {
        VStack(spacing: 2) {
            Text(label).font(.aurea(size: 12, weight: .bold)).foregroundStyle(color).accessibilityHidden(true)
            TextField("", text: $draft)
                .font(.aurea(size: 14)).foregroundStyle(AureaColors.text).multilineTextAlignment(.center)
                .keyboardType(.numbersAndPunctuation).focused($focused)
                .environment(\.layoutDirection, .leftToRight)   // número sempre LTR
                .submitLabel(.done).onSubmit(commit)
                .accessibilityLabel(AureaText.t("skeleton_rot_a11y", label, shown))
            HStack(spacing: 0) {
                Button { onCommit(value - 5) } label: {
                    CupertinoGlyph.text(CupertinoGlyph.Minus, size: 14).frame(maxWidth: .infinity).frame(height: 44).contentShape(Rectangle())
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("skeleton_minus", label))
                Button { onCommit(value + 5) } label: {
                    CupertinoGlyph.text(CupertinoGlyph.Plus, size: 14).frame(maxWidth: .infinity).frame(height: 44).contentShape(Rectangle())
                }.buttonStyle(.plain).accessibilityLabel(AureaText.t("skeleton_plus", label))
            }
        }
        .padding(.vertical, 4).frame(maxWidth: .infinity)
        .background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 10))
        .onAppear { draft = shown }
        .onChange(of: shown) { now in if !focused { draft = now } }
        .onChange(of: focused) { now in if !now { commit() } }
    }

    private func commit() {
        if let v = Float(draft.replacingOccurrences(of: ",", with: ".")), v.isFinite, abs(v - value) > 1e-3 { onCommit(v) }
        else { draft = shown }
    }
}
