// =============================================================================
//  Aurea / platform / ios / app / EditPointsView.swift
//
//  Omega conteúdo (área 06, P0-5 / P1-8): o modo EDITAR PONTOS.
//
//    ┌──────────┬──────────────────────────────────┬──────────┐
//    │ ‹        │ ⌜                              ⌝ │  ·       │ faixa de pontos
//    │ Contornos│     TRACKPAD                     │  ●  ←sel │ (60 pt)
//    │ Tipo     │  texto-guia                      │  ·       │
//    │ ± ponto  │ ⌞                              ⌟ │  ·       │
//    └──────────┴──────────────────────────────────┴──────────┘
//  Na barra da seção, o segmento ◆ keyframes | • pontos (OmegaContent.swift).
//  As REGRAS (inserir, apagar, canto ↔ curva, travas, abrir/fechar, furo) são do
//  motor (Engine::edit_vector_path); aqui só a escolha e os gestos.
//  Par do EditPoints.kt (mesmos ids).
// =============================================================================
import SwiftUI
import UIKit

/// Operações de `Engine::PathOp` (mesma numeração).
enum PathOps {
    static let movePoints: UInt32 = 0
    static let moveHandle: UInt32 = 1
    static let insert: UInt32 = 2
    static let remove: UInt32 = 3
    static let setType: UInt32 = 4
    static let append: UInt32 = 5
    static let setClosed: UInt32 = 6
    static let setExclude: UInt32 = 7
}

/// Um vértice do contorno no espaço do grupo (alças RELATIVAS ao ponto).
struct EPVertex {
    var x: Float, y: Float, inX: Float, inY: Float, outX: Float, outY: Float
    var isCurve: Bool { hypot(inX, inY) > 1e-3 || hypot(outX, outY) > 1e-3 }
}

/// O contorno no cabeçote (Engine::vector_path_at): afim grupo → composição, bits e pontos.
struct EPPath {
    var affine: [Float]
    var flags: Int
    var closed: Bool
    var v: [EPVertex]

    @MainActor static func load(_ model: AureaModel, group: UInt32, path: UInt32) -> EPPath? {
        guard let id = model.primarySelection else { return nil }
        let d = model.engine.vectorPath(id, group: group, path: path).map(\.floatValue)
        guard d.count >= 9, d[8] >= 0, Int(d[8]) * 6 + 9 == d.count else { return nil }
        var v: [EPVertex] = []
        for i in 0..<Int(d[8]) {
            let o = 9 + i * 6
            v.append(EPVertex(x: d[o], y: d[o + 1], inX: d[o + 2], inY: d[o + 3], outX: d[o + 4], outY: d[o + 5]))
        }
        return EPPath(affine: Array(d[0..<6]), flags: Int(d[6]), closed: d[7] > 0.5, v: v)
    }

    var keyHere: Bool { flags & 4 != 0 }
    var animated: Bool { flags & 2 != 0 }
    var segments: Int { v.count < 2 ? 0 : (closed ? v.count : v.count - 1) }

    func toComp(_ x: Float, _ y: Float) -> SIMD2<Float> {
        SIMD2(affine[0] * x + affine[2] * y + affine[4], affine[1] * x + affine[3] * y + affine[5])
    }
    func fromComp(_ x: Float, _ y: Float) -> SIMD2<Float>? {
        guard let d = vecFromComp(x - affine[4], y - affine[5]) else { return nil }
        return d
    }
    /// Um deslocamento da composição no espaço do grupo (só a parte linear).
    func vecFromComp(_ dx: Float, _ dy: Float) -> SIMD2<Float>? {
        let det = affine[0] * affine[3] - affine[1] * affine[2]
        guard abs(det) > 1e-9 else { return nil }
        return SIMD2((affine[3] * dx - affine[2] * dy) / det, (-affine[1] * dx + affine[0] * dy) / det)
    }

    private func cubic(_ p0: Float, _ p1: Float, _ p2: Float, _ p3: Float, _ t: Float) -> Float {
        let u = 1 - t
        return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3
    }
    /// Ponto do trecho `seg` em `t` (espaço do grupo).
    func point(_ seg: Int, _ t: Float) -> SIMD2<Float>? {
        guard seg >= 0, seg < segments else { return nil }
        let a = v[seg], b = v[(seg + 1) % v.count]
        return SIMD2(cubic(a.x, a.x + a.outX, b.x + b.inX, b.x, t), cubic(a.y, a.y + a.outY, b.y + b.inY, b.y, t))
    }
    func length(_ seg: Int) -> Float {
        guard var prev = point(seg, 0) else { return 0 }
        var total: Float = 0
        for k in 1...16 {
            guard let q = point(seg, Float(k) / 16) else { break }
            total += hypot(q.x - prev.x, q.y - prev.y)
            prev = q
        }
        return total
    }
    func isCurve(_ i: Int) -> Bool { v.indices.contains(i) && v[i].isCurve }
}

/// O que está escolhido: nada, ponto, alça de entrada, alça de saída ou ENTRE dois pontos.
enum EPKind { static let none = 0, point = 1, inHandle = 2, outHandle = 3, between = 4 }

/// O que está escolhido no contorno (a faixa, o palco e o trackpad leem daqui).
@MainActor final class EditPointsState: ObservableObject {
    static let shared = EditPointsState()

    @Published var keysMode = false
    @Published var lockAngle = true
    @Published var lockLength = false
    @Published var kind = 0
    /// O ponto escolhido (−1 = nenhum).
    @Published var index = -1
    /// "Entre": o trecho e a posição nele.
    @Published var segment = -1
    @Published var t: Float = 0.5
    /// Laço: os pontos escolhidos juntos (andam juntos).
    @Published var multi = Set<Int>()
    /// Ponto pendente (espaço do grupo) na ponta aberta; o toque no trackpad fixa.
    @Published var pending: SIMD2<Float>?
    var pendingAtStart = false
    /// Retângulo do laço na tela, só enquanto o dedo arrasta.
    @Published var lasso: CGRect?
    /// Sobe quando a escolha vem de fora da faixa (palco, botões): a faixa centraliza.
    @Published var revision = 0

    func reset() {
        kind = EPKind.none; index = -1; segment = -1; t = 0.5; multi = []; pending = nil; lasso = nil
    }
    func choosePoint(_ i: Int, kind k: Int = EPKind.point, fromStrip: Bool = false) {
        index = i
        kind = i < 0 ? EPKind.none : k
        segment = -1
        pending = nil
        if !fromStrip { revision &+= 1 }
    }
    func chooseBetween(_ seg: Int, at: Float, fromStrip: Bool = false) {
        index = -1
        kind = EPKind.between
        segment = seg
        t = min(0.98, max(0.02, at))
        pending = nil
        if !fromStrip { revision &+= 1 }
    }
    /// O ponto escolhido está na ponta de um contorno aberto (ou o contorno está vazio)?
    func atOpenEnd(_ path: EPPath?) -> Bool {
        guard let p = path, !p.v.isEmpty else { return true }
        if p.closed { return false }
        return kind == EPKind.point && (index == p.v.count - 1 || (index == 0 && p.v.count > 1))
    }
}

@MainActor enum EditPointsStage {
    /// O palco está no Editar pontos?
    static func active(_ model: AureaModel) -> Bool {
        guard model.panel == .editPoints, model.selection.count == 1, let row = model.selectedLayer, !row.locked else { return false }
        return model.isVectorLayer
    }

    /// Os contornos do grupo escolhido: (furo?, keyframes) por contorno.
    static func group(_ model: AureaModel) -> (exclude: [Bool], keyCounts: [Int]) {
        guard let id = model.primarySelection else { return ([], []) }
        let groups = model.engine.vectorGroups(id)
        guard groups.indices.contains(Int(model.vectorGroup)) else { return ([], []) }
        let g = groups[Int(model.vectorGroup)]
        let count = (g["paths"] as? [Any])?.count ?? 0
        let exclude = (g["pathExclude"] as? [NSNumber] ?? []).map(\.boolValue)
        let keys = (g["pathKeyCounts"] as? [NSNumber] ?? []).map(\.intValue)
        return ((0..<count).map { exclude.indices.contains($0) ? exclude[$0] : false },
                (0..<count).map { keys.indices.contains($0) ? keys[$0] : 0 })
    }

    /// Grupo e contorno dentro do que a camada tem (trocar de camada não deixa índice velho).
    static func clamp(_ model: AureaModel) {
        guard let id = model.primarySelection else { return }
        let groups = model.engine.vectorGroups(id)
        if Int(model.vectorGroup) >= groups.count { model.vectorGroup = UInt32(max(0, groups.count - 1)) }
        let paths = groups.indices.contains(Int(model.vectorGroup)) ? ((groups[Int(model.vectorGroup)]["paths"] as? [Any])?.count ?? 0) : 0
        if Int(model.vectorPath) >= paths { model.vectorPath = UInt32(max(0, paths - 1)) }
    }
}

// MARK: - O painel (três colunas)

struct EditPointsPanelView: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var state = EditPointsState.shared

    var body: some View {
        let path = EPPath.load(model, group: model.vectorGroup, path: model.vectorPath)
        HStack(spacing: 0) {
            PointsColumn(path: path).frame(width: 60)
            AureaColors.contentStrongLine.frame(width: 0.7)
            PointsTrackpad(path: path).padding(6)
            AureaColors.contentStrongLine.frame(width: 0.7)
            PointsStrip(path: path).frame(width: 60)
        }
        .foregroundStyle(AureaColors.text)
        .background(AureaColors.panelFrameBackground)
        .accessibilityElement(children: .contain)
        .accessibilityIdentifier("points.panel")
        .onAppear { EditPointsStage.clamp(model) }
        // Trocar de camada ou de contorno começa sem escolha.
        .onChange(of: model.primarySelection) { _ in EditPointsStage.clamp(model); state.reset() }
        .onChange(of: model.vectorGroup) { _ in state.reset() }
        .onChange(of: model.vectorPath) { _ in state.reset() }
    }
}

private struct PointsColumn: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var state = EditPointsState.shared
    let path: EPPath?
    @State private var contours = false

    var body: some View {
        let kind = state.kind
        let i = state.index
        VStack(spacing: 0) {
            cell("points.back", AureaText.t("panel_voltar_ferramentas"), enabled: true, action: { model.panel = .none }) { tint in
                CupertinoGlyph.text(CupertinoGlyph.ChevronLeft, size: 22, color: tint).mirrorsInRtl()
            }
            if state.keysMode {
                let look: KeyframeLook = path?.keyHere == true ? .keyHere : path?.animated == true ? .animated : .none
                let keyCount = EditPointsStage.group(model).keyCounts.indices.contains(Int(model.vectorPath)) ? EditPointsStage.group(model).keyCounts[Int(model.vectorPath)] : 0
                cell("points.key", AureaText.t(look == .keyHere ? "panel_tirar_keyframe_daqui" : "panel_marcar_keyframe_aqui"), enabled: path != nil,
                     action: togglePathKey, longPress: look == .keyHere ? { askDeleteKeys() } : nil) { _ in
                    KeyframeDiamondIcon(look: look, enabled: true).padding(1)
                }
                cell("points.curve", AureaText.t("panel_editar_curva_propriedade"), enabled: path != nil, action: {
                    if keyCount >= 2 { model.curveReturnPanel = .editPoints; model.openPanel(.curve) } else { curveWarning() }
                }) { _ in
                    CurveRailIcon(enabled: true, animated: keyCount > 0)
                }
                Color.clear.frame(maxHeight: .infinity)
            } else if kind == EPKind.inHandle || kind == EPKind.outHandle {
                cell("points.lock.angle", AureaText.t("om_pts_lock_angle"), enabled: true, on: state.lockAngle, action: {
                    state.lockAngle.toggle()
                    if !state.lockAngle { state.lockLength = false }
                }) { tint in ContentIcon(kind: .angleLock, size: 22, tint: tint) }
                cell("points.lock.length", AureaText.t("om_pts_lock_length"), enabled: state.lockAngle, on: state.lockLength,
                     action: { state.lockLength.toggle() }) { tint in ContentIcon(kind: .lengthLock, size: 22, tint: tint) }
                addRemoveCell
            } else {
                cell("points.contours", AureaText.t("om_pts_contours"), enabled: path != nil, action: { contours = true }) { tint in
                    CupertinoGlyph.text(CupertinoGlyph.SquareOnSquare, size: 22, color: tint)
                }
                .aureaAnchoredMenu(isPresented: $contours, tone: .editor, tag: "points.contours.menu") { contourEntries() }
                let curve = path?.isCurve(i) ?? false
                let canType = kind == EPKind.point && (path?.v.indices.contains(i) ?? false)
                // O ícone mostra o tipo PARA O QUAL o ponto vai mudar.
                cell("points.type", AureaText.t(curve ? "om_pts_type_corner" : "om_pts_type_curve"), enabled: canType, action: {
                    model.editVectorPath(PathOps.setType, [Float(i), curve ? 0 : 1])
                }) { tint in ContentIcon(kind: curve ? .pointCorner : .pointCurve, size: 22, tint: tint) }
                addRemoveCell
            }
        }
    }

    private func askDeleteKeys() {
        let clear = clearPathKeys
        AureaPopupPresenter.shared.presentDialog { close in
            AureaAlert(title: AureaText.t("om_panel_delete_all_keyframes_q"), confirmLabel: AureaText.t("panel_apagar"),
                       destructive: true, onConfirm: clear, onDismiss: close)
        }
    }

    private func curveWarning() {
        AureaPopupPresenter.shared.presentDialog { close in
            AureaAlert(title: AureaText.t("om_panel_keyframes_needed"), message: AureaText.t("pn_curve_need_two_keys"),
                       showCancel: false, onConfirm: {}, onDismiss: close)
        }
    }

    /// ± ponto: "+" com a escolha ENTRE dois pontos; "−" apaga o ponto escolhido.
    private var addRemoveCell: some View {
        let kind = state.kind, i = state.index
        let between = kind == EPKind.between
        let canRemove = (path?.v.indices.contains(i) ?? false) && kind != EPKind.between && kind != EPKind.none
        return cell("points.addRemove", AureaText.t(between ? "om_pts_add" : "om_pts_delete"), enabled: between || canRemove, action: {
            if between {
                let r = model.editVectorPath(PathOps.insert, [Float(state.segment), state.t])
                if r >= 0 { state.choosePoint(Int(r)) }
            } else if canRemove {
                let r = model.editVectorPath(PathOps.remove, [Float(i)])
                let left = EPPath.load(model, group: model.vectorGroup, path: model.vectorPath)?.v.count ?? 0
                if r >= 0 && left > 0 { state.choosePoint(min(Int(r), left - 1)) } else { state.choosePoint(-1) }
            }
        }) { tint in
            CupertinoGlyph.text(between ? CupertinoGlyph.AddCircled : CupertinoGlyph.MinusCircle, size: 22, color: tint)
        }
    }

    private func cell<Icon: View>(_ tag: String, _ label: String, enabled: Bool, on: Bool = false, action: @escaping () -> Void,
                                  longPress: (() -> Void)? = nil, @ViewBuilder icon: (Color) -> Icon) -> some View {
        icon(on ? AureaColors.accent : AureaColors.text)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .contentShape(Rectangle())
            .onTapGesture { if enabled { action() } }
            .onLongPressGesture(minimumDuration: 0.5) { if enabled { (longPress ?? action)() } }
            .opacity(enabled ? 1 : 0.3)
            .accessibilityElement()
            .accessibilityLabel(label)
            .accessibilityAddTraits(on ? [.isButton, .isSelected] : .isButton)
            .accessibilityAction { if enabled { action() } }
            .accessibilityIdentifier(tag)
    }

    /// O menu de contornos: um por caminho do grupo, com preencher/furo, apagar,
    /// abrir/fechar; e "Novo contorno".
    private func contourEntries() -> [MenuEntry] {
        guard let id = model.primarySelection else { return [] }
        let group = EditPointsStage.group(model)
        let count = group.exclude.count
        let currentEmpty = (path?.v.count ?? 0) == 0
        var entries: [MenuEntry] = []
        for n in 0..<count {
            let name = AureaText.t("om_pts_contour_n", n + 1)
            let hole = group.exclude[n]
            let other = EPPath.load(model, group: model.vectorGroup, path: UInt32(n))
            let closed = other?.closed ?? true
            let points = other?.v.count ?? 0
            let select = { model.chooseContour(n); EditPointsState.shared.reset() }
            entries.append(.item(name, glyph: CupertinoGlyph.SquareOnSquare, sublabel: hole ? AureaText.t("om_pts_hole_contour") : nil,
                                 selected: UInt32(n) == model.vectorPath, submenu: [
                .item(name, selected: UInt32(n) == model.vectorPath) { select() },
                .item(AureaText.t(hole ? "om_pts_fill_contour" : "om_pts_hole_contour"), tag: "points.contour.\(n + 1).fill") {
                    select(); model.editVectorPath(PathOps.setExclude, [hole ? 0 : 1])
                },
                .item(AureaText.t(closed ? "panel_abrir_caminho" : "panel_fechar_caminho"), enabled: points >= 2, tag: "points.contour.\(n + 1).close") {
                    select(); model.editVectorPath(PathOps.setClosed, [closed ? 0 : 1])
                },
                .item(AureaText.t("om_pts_delete_contour"), enabled: count > 1, destructive: true, tag: "points.contour.\(n + 1).delete") {
                    _ = model.engine.removeVectorPath(id, group: model.vectorGroup, path: UInt32(n))
                    EditPointsState.shared.reset()
                    model.contentRevision &+= 1
                    model.refreshModel(force: true)
                    EditPointsStage.clamp(model)
                },
            ], tag: "points.contour.\(n + 1)"))
        }
        entries.append(.divider)
        entries.append(.item(AureaText.t("om_pts_add_contour"), glyph: CupertinoGlyph.Plus, enabled: !currentEmpty, tag: "points.contour.add") {
            let created = model.engine.addVectorPath(id, group: model.vectorGroup, kind: 0)
            if created >= 0 { model.chooseContour(Int(created)) }
            EditPointsState.shared.reset()
            model.refreshModel(force: true)
        })
        return entries
    }

    /// ◆ do modo keyframes: marca o keyframe da forma aqui; com keyframe aqui, tira.
    private func togglePathKey() {
        guard let id = model.primarySelection else { return }
        _ = model.engine.toggleVectorPathKey(id, group: model.vectorGroup, path: model.vectorPath)
        model.contentRevision &+= 1
        model.refreshModel(force: true)
    }

    /// Segurar ◆: apaga todos os keyframes da forma (a forma do cabeçote fica parada). Um passo de desfazer.
    private func clearPathKeys() {
        guard let id = model.primarySelection else { return }
        _ = model.engine.clearVectorPathKeys(id, group: model.vectorGroup, path: model.vectorPath)
        model.contentRevision &+= 1
        model.refreshModel(force: true)
    }
}

// MARK: - Trackpad (centro)

/// O dedo no trackpad: a diferença (1 pt = 1 px da composição, convertida para o
/// espaço do grupo) move o ponto, a alça ou os pontos do laço. Na ponta aberta
/// (ou ENTRE pontos) o arraste posiciona o ponto pendente e o toque o fixa. Um
/// gesto = um passo de desfazer.
private struct PointsTrackpad: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var state = EditPointsState.shared
    let path: EPPath?
    @State private var last: CGPoint?
    @State private var moved = false
    @State private var sent = false

    var body: some View {
        let guide = AureaText.t((path == nil || path!.v.isEmpty || state.atOpenEnd(path)) ? "om_pts_guide_next"
            : state.kind == EPKind.between ? "om_pts_guide_insert" : "om_pts_guide_move")
        ZStack {
            AureaColors.contentTrackpad
            Canvas { context, size in
                let l: CGFloat = 8, o: CGFloat = 6
                let x0 = o, y0 = o, x1 = size.width - o, y1 = size.height - o
                var corners = Path()
                corners.move(to: CGPoint(x: x0 + l, y: y0)); corners.addLine(to: CGPoint(x: x0, y: y0)); corners.addLine(to: CGPoint(x: x0, y: y0 + l))
                corners.move(to: CGPoint(x: x1 - l, y: y0)); corners.addLine(to: CGPoint(x: x1, y: y0)); corners.addLine(to: CGPoint(x: x1, y: y0 + l))
                corners.move(to: CGPoint(x: x0 + l, y: y1)); corners.addLine(to: CGPoint(x: x0, y: y1)); corners.addLine(to: CGPoint(x: x0, y: y1 - l))
                corners.move(to: CGPoint(x: x1 - l, y: y1)); corners.addLine(to: CGPoint(x: x1, y: y1)); corners.addLine(to: CGPoint(x: x1, y: y1 - l))
                context.stroke(corners, with: .color(AureaColors.text), lineWidth: 1.5)
            }
            Text(guide).font(.aurea(size: 12)).foregroundStyle(AureaColors.contentGuide).multilineTextAlignment(.center)
                .padding(.horizontal, 18).allowsHitTesting(false)
        }
        .contentShape(Rectangle())
        .gesture(DragGesture(minimumDistance: 0)
            .onChanged { g in step(g) }
            .onEnded { g in end(g) })
        .accessibilityElement()
        .accessibilityLabel(guide)
        .accessibilityAddTraits(.isButton)
        .accessibilityAction { confirmPending() }
        .accessibilityIdentifier("points.trackpad")
    }

    private func step(_ g: DragGesture.Value) {
        if last == nil { last = g.startLocation; moved = false; sent = false }
        if !moved && hypot(g.location.x - g.startLocation.x, g.location.y - g.startLocation.y) < 8 { return }
        moved = true
        guard let at = EPPath.load(model, group: model.vectorGroup, path: model.vectorPath), let previous = last else { return }
        let dx = Float(g.location.x - previous.x), dy = Float(g.location.y - previous.y)
        last = g.location
        guard let d = at.vecFromComp(dx, dy) else { return }
        let i = state.index
        if state.atOpenEnd(at) || state.kind == EPKind.between || state.pending != nil {
            // O ponto pendente nasce na ponta (ou no trecho) e segue o dedo.
            let start = state.pending ?? pendingStart(at)
            state.pending = start + d
        } else if state.kind == EPKind.point && at.v.indices.contains(i) {
            let group = (state.multi.contains(i) ? state.multi : [i]).sorted()
            model.editVectorPath(PathOps.movePoints, [d.x, d.y] + group.map { Float($0) }, continuing: sent)
            sent = true
        } else if (state.kind == EPKind.inHandle || state.kind == EPKind.outHandle) && at.v.indices.contains(i) {
            model.editVectorPath(PathOps.moveHandle, [Float(i), state.kind == EPKind.outHandle ? 1 : 0, d.x, d.y,
                                                      state.lockAngle ? 1 : 0, state.lockLength ? 1 : 0], continuing: sent)
            sent = true
        }
    }

    private func end(_ g: DragGesture.Value) {
        let wasMoved = moved
        if sent { model.refreshModel(force: true) }
        last = nil; moved = false; sent = false
        if !wasMoved { confirmPending() }
    }

    /// Onde o ponto pendente nasce: na ponta aberta (no último ponto) ou no trecho escolhido.
    private func pendingStart(_ at: EPPath) -> SIMD2<Float> {
        if state.kind == EPKind.between, let p = at.point(state.segment, state.t) { return p }
        if at.v.isEmpty {
            // Contorno vazio: o primeiro ponto nasce no centro da composição.
            state.pendingAtStart = false
            return at.fromComp(Float(model.compositionWidth) / 2, Float(model.compositionHeight) / 2) ?? .zero
        }
        let atStart = state.index == 0 && at.v.count > 1
        state.pendingAtStart = atStart
        let end = atStart ? at.v[0] : at.v[at.v.count - 1]
        return SIMD2(end.x, end.y)
    }

    /// O toque no trackpad fixa o ponto pendente (no fim, no começo ou entre os pontos).
    private func confirmPending() {
        guard let p = state.pending else { return }
        if state.kind == EPKind.between && state.segment >= 0 {
            let r = model.editVectorPath(PathOps.insert, [Float(state.segment), state.t])
            if r >= 0 {
                if let q = EPPath.load(model, group: model.vectorGroup, path: model.vectorPath)?.v, q.indices.contains(Int(r)) {
                    model.editVectorPath(PathOps.movePoints, [p.x - q[Int(r)].x, p.y - q[Int(r)].y, Float(r)], continuing: true)
                    model.refreshModel(force: true)
                }
                state.choosePoint(Int(r))
            }
        } else {
            let r = model.editVectorPath(PathOps.append, [p.x, p.y, state.pendingAtStart ? 1 : 0])
            if r >= 0 { state.choosePoint(Int(r)) }
        }
        state.pending = nil
    }
}

// MARK: - Faixa de pontos (direita)

/// Uma parada da faixa: o ponto (POINT), a alça de entrada (IN) ou a de saída (OUT).
private struct StripStop { let y: CGFloat; let kind: Int; let index: Int }

/// A faixa "esticada": a altura de cada parada e de cada trecho, em pt.
private struct StripLayout {
    var stops: [StripStop] = []
    var segments: [(y0: CGFloat, y1: CGFloat, seg: Int)] = []
    var total: CGFloat = 0

    init(_ path: EPPath?) {
        guard let path, !path.v.isEmpty else { return }
        let n = path.v.count, segs = path.segments
        let lengths = (0..<segs).map { path.length($0) }
        let maxLen = max(1e-3, lengths.max() ?? 1)
        var y: CGFloat = 0
        for i in 0..<n {
            let v = path.v[i]
            if hypot(v.inX, v.inY) > 1e-3 { stops.append(StripStop(y: y - 14, kind: EPKind.inHandle, index: i)) }
            stops.append(StripStop(y: y, kind: EPKind.point, index: i))
            if hypot(v.outX, v.outY) > 1e-3 { stops.append(StripStop(y: y + 14, kind: EPKind.outHandle, index: i)) }
            if i < segs {
                // Espaço de 75 a 95 pt, pelo comprimento do trecho.
                let space = 75 + 20 * CGFloat(lengths[i] / maxLen)
                segments.append((y, y + space, i))
                y += space
            }
        }
        total = y
    }
}

private struct PointsStrip: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var state = EditPointsState.shared
    let path: EPPath?
    @State private var scroll: CGFloat = 0
    @State private var dragStart: CGFloat?

    var body: some View {
        let layout = StripLayout(path)
        GeometryReader { geo in
            Canvas { context, size in
                let cx = size.width / 2, cy = size.height / 2
                func sy(_ y: CGFloat) -> CGFloat { cy + (y - scroll) }
                let base = AureaColors.contentPointMarker, accent = AureaColors.accent
                for s in layout.segments {
                    let curved = path.map { $0.isCurve(s.seg) || $0.isCurve((s.seg + 1) % max(1, $0.v.count)) } ?? false
                    var line = Path()
                    line.move(to: CGPoint(x: cx, y: sy(s.y0))); line.addLine(to: CGPoint(x: cx, y: sy(s.y1)))
                    context.stroke(line, with: .color(base), style: StrokeStyle(lineWidth: 1, dash: curved ? [4, 3] : []))
                }
                for s in layout.stops {
                    let chosen = (state.kind == s.kind && state.index == s.index) || (s.kind == EPKind.point && state.multi.contains(s.index))
                    let r: CGFloat = chosen ? 6 : 2.5
                    let dot = Path(ellipseIn: CGRect(x: cx - r, y: sy(s.y) - r, width: 2 * r, height: 2 * r))
                    if s.kind == EPKind.point { context.fill(dot, with: .color(chosen ? accent : base)) }
                    else { context.stroke(dot, with: .color(chosen ? accent : base), lineWidth: 1) }
                }
                if state.kind == EPKind.between {
                    context.stroke(Path(ellipseIn: CGRect(x: cx - 4, y: cy - 4, width: 8, height: 8)), with: .color(accent), lineWidth: 1.5)
                }
                // O centro: a marca da escolha.
                var marks = Path()
                marks.move(to: CGPoint(x: 4, y: cy)); marks.addLine(to: CGPoint(x: 10, y: cy))
                marks.move(to: CGPoint(x: size.width - 10, y: cy)); marks.addLine(to: CGPoint(x: size.width - 4, y: cy))
                context.stroke(marks, with: .color(accent.opacity(0.6)), style: StrokeStyle(lineWidth: 1.5, lineCap: .round))
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { g in
                    guard abs(g.translation.height) >= 8 || dragStart != nil else { return }
                    if dragStart == nil { dragStart = scroll }
                    scroll = min(layout.total + 20, max(-20, (dragStart ?? 0) - g.translation.height))
                    apply(layout, scroll)
                }
                .onEnded { g in
                    if dragStart == nil {
                        // Toque: a parada mais perto do dedo vem para o centro.
                        let yTap = scroll + (g.location.y - geo.size.height / 2)
                        if let stop = layout.stops.min(by: { abs($0.y - yTap) < abs($1.y - yTap) }), abs(stop.y - yTap) < 20 {
                            withAnimation(.easeOut(duration: 0.2)) { scroll = stop.y }
                            apply(layout, stop.y)
                        }
                        return
                    }
                    // Lançar: o embalo segue um pouco; ímã de 12 pt para a parada mais perto.
                    let fling = -(g.predictedEndTranslation.height - g.translation.height) * 0.5
                    var target = min(layout.total + 20, max(-20, scroll + fling))
                    if let near = layout.stops.min(by: { abs($0.y - target) < abs($1.y - target) }), abs(near.y - target) <= 12 { target = near.y }
                    dragStart = nil
                    withAnimation(.easeOut(duration: 0.3)) { scroll = target }
                    apply(layout, target)
                })
        }
        .accessibilityElement()
        .accessibilityLabel(AureaText.t("om_pts_strip_cd"))
        .accessibilityAdjustableAction { direction in
            guard !layout.stops.isEmpty else { return }
            let current = layout.stops.firstIndex { $0.index == state.index && $0.kind == state.kind } ?? -1
            let next = direction == .increment ? min(layout.stops.count - 1, current + 1) : max(0, current - 1)
            scroll = layout.stops[next].y
            apply(layout, scroll)
        }
        .accessibilityIdentifier("points.strip")
        // A escolha veio de fora (palco, botões): centraliza a parada dela.
        .onChange(of: state.revision) { _ in center(layout) }
        .onAppear { center(layout) }
    }

    private func center(_ layout: StripLayout) {
        guard dragStart == nil else { return }
        let target: CGFloat?
        switch state.kind {
        case EPKind.between:
            target = layout.segments.first { $0.seg == state.segment }.map { $0.y0 + ($0.y1 - $0.y0) * CGFloat(state.t) }
        case EPKind.none:
            target = nil
        default:
            target = layout.stops.first { $0.index == state.index && $0.kind == state.kind }?.y
        }
        if let target { withAnimation(.easeOut(duration: 0.25)) { scroll = target } }
    }

    /// A posição `y` da faixa vira a escolha: parada a até 12 pt, senão "entre" no trecho.
    private func apply(_ layout: StripLayout, _ y: CGFloat) {
        if let near = layout.stops.min(by: { abs($0.y - y) < abs($1.y - y) }), abs(near.y - y) <= 12 {
            if state.index != near.index || state.kind != near.kind { state.choosePoint(near.index, kind: near.kind, fromStrip: true) }
            return
        }
        guard let seg = layout.segments.first(where: { y >= $0.y0 && y <= $0.y1 }) else { return }
        let t = Float((y - seg.y0) / max(1e-3, seg.y1 - seg.y0))
        state.chooseBetween(seg.seg, at: t, fromStrip: true)
    }
}

// MARK: - O palco no Editar pontos

/// O contorno atual (pontos, alças da escolha, "entre", pendente e laço) e os
/// outros contornos do grupo; o gesto do palco. Alcance de 25 pt: alça da escolha
/// > ponto do contorno atual > ponto de outro contorno (troca o contorno) > linha
/// (põe um ponto) > vazio (laço de vários pontos; tocar acrescenta na ponta aberta
/// ou solta a escolha). Arrastar move o ponto, a alça ou o grupo do laço — um
/// passo de desfazer por arrasto.
@MainActor struct EditPointsStageOverlay: View {
    @EnvironmentObject private var model: AureaModel
    @ObservedObject private var state = EditPointsState.shared
    @ObservedObject private var viewZoom = StageViewZoom.shared

    private enum Mode { case idle, handle(Int), points([Int], hit: Int), empty }
    @State private var mode: Mode = .idle
    @State private var started = false
    @State private var last: CGPoint = .zero
    @State private var moved = false
    @State private var sent = false
    @State private var startPath: EPPath?
    @State private var lineHit: (seg: Int, t: Float)?

    private static let pathInk = Color(hex: 0x5AA8FF)
    private static let handleInk = Color(hex: 0xB8C4D0)

    var body: some View {
        GeometryReader { geo in
            let placed = StageZoomMath.fit(size: geo.size, composition: CGSize(width: CGFloat(model.compositionWidth), height: CGFloat(model.compositionHeight)),
                                           zoom: viewZoom.zoom, pan: viewZoom.pan)
            Canvas { context, _ in draw(&context, placed) }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { g in change(g, placed) }
                    .onEnded { g in end(g, placed) })
        }
        .accessibilityElement()
        .accessibilityLabel(AureaText.t("om_pts_title"))
        .accessibilityIdentifier("stage.points")
        .onDisappear { state.lasso = nil }
    }

    private func screen(_ path: EPPath, _ x: Float, _ y: Float, _ placed: (scale: CGFloat, origin: CGPoint)) -> CGPoint {
        let c = path.toComp(x, y)
        return CGPoint(x: placed.origin.x + CGFloat(c.x) * placed.scale, y: placed.origin.y + CGFloat(c.y) * placed.scale)
    }

    private func others() -> [(Int, EPPath)] {
        let count = EditPointsStage.group(model).exclude.count
        return (0..<count).compactMap { n -> (Int, EPPath)? in
            guard UInt32(n) != model.vectorPath, let p = EPPath.load(model, group: model.vectorGroup, path: UInt32(n)) else { return nil }
            return (n, p)
        }
    }

    // MARK: Desenho

    private func draw(_ context: inout GraphicsContext, _ placed: (scale: CGFloat, origin: CGPoint)) {
        guard let path = EPPath.load(model, group: model.vectorGroup, path: model.vectorPath) else { return }
        func sx(_ x: Float, _ y: Float) -> CGPoint { screen(path, x, y, placed) }
        // Os outros contornos do grupo (finos).
        for (_, p) in others() { bezier(&context, p, { screen(p, $0, $1, placed) }, ink: Self.handleInk.opacity(0.6), width: 1) }
        bezier(&context, path, sx, ink: Self.pathInk, width: 1.5)
        let sel = state.index, kind = state.kind
        if path.v.indices.contains(sel) && kind != EPKind.between {
            let p = path.v[sel]
            let c = sx(p.x, p.y)
            for k in 0..<2 {
                let h = k == 0 ? sx(p.x + p.inX, p.y + p.inY) : sx(p.x + p.outX, p.y + p.outY)
                guard hypot(h.x - c.x, h.y - c.y) >= 1 else { continue }
                let chosen = (k == 0 && kind == EPKind.inHandle) || (k == 1 && kind == EPKind.outHandle)
                var line = Path(); line.move(to: c); line.addLine(to: h)
                context.stroke(line, with: .color(Self.handleInk), lineWidth: 1.4)
                disk(&context, h, 7.5, StageInk.outlineUnder)
                disk(&context, h, 6, chosen ? AureaColors.accent : .white)
            }
        }
        let r: CGFloat = 6
        for i in path.v.indices {
            let c = sx(path.v[i].x, path.v[i].y)
            let chosen = (i == sel && kind == EPKind.point) || state.multi.contains(i)
            context.fill(Path(CGRect(x: c.x - r - 1.5, y: c.y - r - 1.5, width: 2 * r + 3, height: 2 * r + 3)), with: .color(StageInk.outlineUnder))
            context.fill(Path(CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r)), with: .color(chosen ? AureaColors.accent : .white))
            if i == 0 && !path.closed && path.v.count >= 2 {
                context.stroke(Path(ellipseIn: CGRect(x: c.x - r * 1.9, y: c.y - r * 1.9, width: r * 3.8, height: r * 3.8)), with: .color(AureaColors.accent), lineWidth: 1.5)
            }
        }
        if kind == EPKind.between, let g = path.point(state.segment, state.t) {
            let c = sx(g.x, g.y)
            disk(&context, c, 7, StageInk.outlineUnder)
            context.stroke(Path(ellipseIn: CGRect(x: c.x - 5.5, y: c.y - 5.5, width: 11, height: 11)), with: .color(AureaColors.accent), lineWidth: 2)
        }
        if let g = state.pending {
            let c = sx(g.x, g.y)
            let anchor: SIMD2<Float>?
            if kind == EPKind.between { anchor = path.point(state.segment, state.t) }
            else if path.v.isEmpty { anchor = nil }
            else if state.pendingAtStart { anchor = SIMD2(path.v[0].x, path.v[0].y) }
            else { anchor = SIMD2(path.v[path.v.count - 1].x, path.v[path.v.count - 1].y) }
            if let a = anchor {
                var line = Path(); line.move(to: sx(a.x, a.y)); line.addLine(to: c)
                context.stroke(line, with: .color(AureaColors.accent), style: StrokeStyle(lineWidth: 1, dash: [6, 5]))
            }
            disk(&context, c, 8, StageInk.outlineUnder)
            context.stroke(Path(ellipseIn: CGRect(x: c.x - 6.5, y: c.y - 6.5, width: 13, height: 13)), with: .color(AureaColors.accent), lineWidth: 2)
        }
        if let box = state.lasso {
            context.fill(Path(box), with: .color(AureaColors.accent.opacity(0.12)))
            context.stroke(Path(box), with: .color(AureaColors.accent), lineWidth: 1)
        }
    }

    private func disk(_ context: inout GraphicsContext, _ c: CGPoint, _ r: CGFloat, _ color: Color) {
        context.fill(Path(ellipseIn: CGRect(x: c.x - r, y: c.y - r, width: 2 * r, height: 2 * r)), with: .color(color))
    }

    private func bezier(_ context: inout GraphicsContext, _ path: EPPath, _ sx: (Float, Float) -> CGPoint, ink: Color, width: CGFloat) {
        guard path.v.count >= 2 else { return }
        var p = Path()
        p.move(to: sx(path.v[0].x, path.v[0].y))
        for i in 0..<path.segments {
            let a = path.v[i], b = path.v[(i + 1) % path.v.count]
            p.addCurve(to: sx(b.x, b.y), control1: sx(a.x + a.outX, a.y + a.outY), control2: sx(b.x + b.inX, b.y + b.inY))
        }
        if path.closed { p.closeSubpath() }
        context.stroke(p, with: .color(StageInk.outlineUnder), lineWidth: width * 2)
        context.stroke(p, with: .color(ink), lineWidth: width)
    }

    // MARK: Gesto

    private func change(_ g: DragGesture.Value, _ placed: (scale: CGFloat, origin: CGPoint)) {
        if !started { begin(g.startLocation, placed); started = true }
        guard let at = startPath else { return }
        let k = max(placed.scale, 0.0001)
        switch mode {
        case .handle(let side):
            guard let d = at.vecFromComp(Float((g.location.x - last.x) / k), Float((g.location.y - last.y) / k)) else { return }
            last = g.location
            model.editVectorPath(PathOps.moveHandle, [Float(state.index), side == EPKind.outHandle ? 1 : 0, d.x, d.y,
                                                      state.lockAngle ? 1 : 0, state.lockLength ? 1 : 0], continuing: sent)
            sent = true
        case .points(let group, _):
            if !moved && hypot(g.location.x - g.startLocation.x, g.location.y - g.startLocation.y) < 6 { return }
            moved = true
            guard let d = at.vecFromComp(Float((g.location.x - last.x) / k), Float((g.location.y - last.y) / k)) else { return }
            last = g.location
            model.editVectorPath(PathOps.movePoints, [d.x, d.y] + group.map { Float($0) }, continuing: sent)
            sent = true
        case .empty:
            if !moved && hypot(g.location.x - g.startLocation.x, g.location.y - g.startLocation.y) < 6 { return }
            moved = true
            state.lasso = CGRect(x: min(g.startLocation.x, g.location.x), y: min(g.startLocation.y, g.location.y),
                                 width: abs(g.location.x - g.startLocation.x), height: abs(g.location.y - g.startLocation.y))
        case .idle:
            break
        }
    }

    private func begin(_ p: CGPoint, _ placed: (scale: CGFloat, origin: CGPoint)) {
        last = p; moved = false; sent = false; mode = .idle; lineHit = nil
        guard let path = EPPath.load(model, group: model.vectorGroup, path: model.vectorPath) else { startPath = nil; return }
        startPath = path
        let reach: CGFloat = 25
        let sel = state.index
        // 1) Alças do ponto escolhido.
        if path.v.indices.contains(sel) && state.kind != EPKind.between {
            let v = path.v[sel]
            let c = screen(path, v.x, v.y, placed)
            let hin = screen(path, v.x + v.inX, v.y + v.inY, placed), hout = screen(path, v.x + v.outX, v.y + v.outY, placed)
            let din = hypot(hin.x - c.x, hin.y - c.y) > 1 ? hypot(p.x - hin.x, p.y - hin.y) : .greatestFiniteMagnitude
            let dout = hypot(hout.x - c.x, hout.y - c.y) > 1 ? hypot(p.x - hout.x, p.y - hout.y) : .greatestFiniteMagnitude
            if din < reach && din <= dout { mode = .handle(EPKind.inHandle) }
            else if dout < reach { mode = .handle(EPKind.outHandle) }
            if case .handle(let side) = mode { state.choosePoint(sel, kind: side); return }
        }
        // 2) Pontos do contorno atual.
        var best = reach, hit = -1
        for i in path.v.indices {
            let c = screen(path, path.v[i].x, path.v[i].y, placed)
            let d = hypot(p.x - c.x, p.y - c.y)
            if d < best { best = d; hit = i }
        }
        if hit >= 0 {
            mode = .points((state.multi.contains(hit) ? state.multi : [hit]).sorted(), hit: hit)
            return
        }
        // 3) Outro contorno do grupo: troca o contorno.
        for (n, other) in others() {
            for i in other.v.indices {
                let c = screen(other, other.v[i].x, other.v[i].y, placed)
                if hypot(p.x - c.x, p.y - c.y) < reach {
                    model.chooseContour(n)
                    state.reset()
                    state.choosePoint(i)
                    startPath = nil
                    return
                }
            }
        }
        // 4) Linha (põe um ponto ali) ou vazio (laço / acrescentar / soltar).
        mode = .empty
        var bestLine = reach
        for s in 0..<path.segments {
            for k in 1..<48 {
                let t = Float(k) / 48
                guard let q = path.point(s, t) else { continue }
                let c = screen(path, q.x, q.y, placed)
                let d = hypot(c.x - p.x, c.y - p.y)
                if d < bestLine { bestLine = d; lineHit = (s, t) }
            }
        }
    }

    private func end(_ g: DragGesture.Value, _ placed: (scale: CGFloat, origin: CGPoint)) {
        defer { started = false; mode = .idle; startPath = nil; lineHit = nil; moved = false; sent = false }
        if !started { begin(g.startLocation, placed) }
        guard let path = startPath else { return }
        switch mode {
        case .handle:
            if sent { model.refreshModel(force: true) }
        case .points(_, let hit):
            if sent { model.refreshModel(force: true) }
            if !moved {
                // Tocar no 1º ponto com a ponta escolhida fecha o contorno aberto.
                if hit == 0 && !path.closed && path.v.count >= 3 && state.index == path.v.count - 1 && state.kind == EPKind.point {
                    model.editVectorPath(PathOps.setClosed, [1])
                }
                if !state.multi.contains(hit) { state.multi = [] }
            }
            state.choosePoint(hit)
        case .empty:
            if moved, let box = state.lasso {
                state.lasso = nil
                // Laço: os pontos dentro do retângulo andam juntos.
                let inside = Set(path.v.indices.filter { box.contains(screen(path, path.v[$0].x, path.v[$0].y, placed)) })
                state.multi = inside
                state.choosePoint(inside.min() ?? -1)
                return
            }
            state.lasso = nil
            if let line = lineHit {
                let r = model.editVectorPath(PathOps.insert, [Float(line.seg), line.t])
                if r >= 0 { state.choosePoint(Int(r)) }
                return
            }
            // Vazio: com a ponta aberta escolhida (ou o contorno vazio), acrescenta ali.
            let k = max(placed.scale, 0.0001)
            let comp = SIMD2(Float((g.startLocation.x - placed.origin.x) / k), Float((g.startLocation.y - placed.origin.y) / k))
            if state.atOpenEnd(path), let local = path.fromComp(comp.x, comp.y) {
                let atStart = path.v.count > 1 && state.index == 0
                let r = model.editVectorPath(PathOps.append, [local.x, local.y, atStart ? 1 : 0])
                if r >= 0 { state.choosePoint(Int(r)) }
            } else {
                state.multi = []
                state.choosePoint(-1)
            }
        case .idle:
            break
        }
    }
}
