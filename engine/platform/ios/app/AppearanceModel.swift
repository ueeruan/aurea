import Foundation
import SwiftUI

// =============================================================================
// Omega aparência (área 05): o que os painéis Cor e preenchimento, Borda e
// sombra, Mover/Escala (ímã) e o seletor de cor pedem ao modelo. Par do bloco
// "Omega aparência" do EditorStore.kt; as regras moram no motor
// (EngineOmegaAppearance.cpp, core/SnapMath.hpp, core/ColorCode.hpp).
// =============================================================================

/// Conta-gotas ao vivo no palco (P1-1): o seletor liga, o palco amostra.
/// `onSample` a cada passo do arrasto (RGBA sRGB 0..1), `onCommit` ao soltar.
final class EyedropperRequest: Identifiable {
    let id = UUID()
    let onSample: ([Float]) -> Void
    let onCommit: ([Float]) -> Void
    let onCancel: () -> Void
    init(onSample: @escaping ([Float]) -> Void, onCommit: @escaping ([Float]) -> Void, onCancel: @escaping () -> Void) {
        self.onSample = onSample
        self.onCommit = onCommit
        self.onCancel = onCancel
    }
}

/// Índices de `AureaModel.shapeFill` (Engine::query_shape_fill).
enum ShapeFillIndex {
    static let kind = 0, gradientType = 1, filled = 2, color = 3, color0 = 7, color1 = 11, start = 15, end = 17
    static let animated = 19, keyHere = 20, supported = 21, boxWidth = 22, boxHeight = 23
    /// Grupos (timeline/ShapeFill.hpp): bits de `animated`/`keyHere` e o `group` dos setters.
    static let groupColor = 0, groupStart = 1, groupEnd = 2, groupStartPoint = 3, groupEndPoint = 4
}

extension AureaModel {
    private var appearanceLocked: Bool {
        if selectedLayer?.locked == true { toast = AureaText.t("editor_camada_bloqueada"); return true }
        return false
    }

    // --- Cor e preenchimento (forma) ------------------------------------------
    /// 0 nenhum, 1 cor sólida, 2 degradê (um passo de desfazer).
    func setShapeFillMode(_ mode: Int) {
        guard let id = primarySelection, !appearanceLocked else { return }
        if engine.setShapeFillMode(id, mode: Int32(mode)) { refreshModel(force: true) }
    }

    /// 0 linear, 1 radial, 2 angular.
    func setShapeGradientType(_ type: Int) {
        guard let id = primarySelection else { return }
        if engine.setShapeGradientType(id, type: Int32(type)) { refreshModel(force: true) }
    }

    /// Grupo 0 cor, 1 cor do início, 2 cor do fim (RGBA 0..1); 3/4 pontas (x, y relativos à caixa).
    /// Com keyframe no grupo grava no cabeçote. Arrasto: `beginGesture` antes.
    func setShapeFillValue(_ group: Int, _ values: [Float]) {
        guard let id = primarySelection, values.allSatisfy(\.isFinite) else { return }
        if engine.setShapeFillValue(id, group: Int32(group), values: values.map { NSNumber(value: $0) }) {
            refreshShapeFill(id)
            refreshModel(force: true)
        }
    }

    /// ◆ do grupo do preenchimento (keyframe no cabeçote ↔ tira).
    func toggleShapeFillKey(_ group: Int) {
        guard let id = primarySelection else { return }
        if engine.toggleShapeFillKey(id, group: Int32(group)) { refreshModel(force: true) }
    }

    /// Valor de 4 componentes do preenchimento a partir de `start` (cor) — vazio sem forma.
    func shapeFillColor(at start: Int) -> [Float] {
        guard shapeFill.count >= start + 4 else { return [] }
        return Array(shapeFill[start..<(start + 4)])
    }

    // --- Borda e sombra ---------------------------------------------------------
    /// Põe uma borda (Dentro, branca, 6 px) ou liga a sombra. Devolve o id do efeito (−1 = recusado).
    @discardableResult
    func addDecoration(role: UInt32) -> Int {
        guard let id = primarySelection, !appearanceLocked else { return -1 }
        let effect = Int(engine.decorationAdd(id, role: Int32(role)))
        if effect >= 0 { refreshModel(force: true); refreshSelectedLayer() }
        return effect
    }

    /// Reordena a borda dentro das bordas (`slot` 0 = a primeira).
    func moveDecoration(_ effectId: UInt32, slot: Int) {
        guard let id = primarySelection, slot >= 0 else { return }
        if engine.decorationMove(id, effect: effectId, slot: UInt32(slot)) { refreshModel(force: true); refreshSelectedLayer() }
    }

    /// Deslocamento X/Y (px) da sombra (vira direção/distância no motor).
    func setShadowOffset(_ effectId: UInt32, dx: Float, dy: Float) {
        guard let id = primarySelection, dx.isFinite, dy.isFinite else { return }
        if engine.decorationSetShadowOffset(id, effect: effectId, dx: dx, dy: dy) { refreshModel(force: true) }
    }

    /// [dx, dy] da sombra no cabeçote (vazio = não é sombra).
    func shadowOffset(_ effectId: UInt32) -> [Float] {
        guard let id = primarySelection else { return [] }
        return engine.decorationShadowOffset(id, effect: effectId).map(\.floatValue)
    }

    /// Parâmetros de uma decoração (o mesmo formato da lista de efeitos, sem a
    /// apresentação extra): o painel Borda e sombra desenha as linhas dele.
    func decorationParams(_ effectId: UInt32) -> [EffectParamItem] {
        guard let layerId = primarySelection else { return [] }
        let typeId = decorations.first { $0.effectId == effectId }?.typeId ?? 0
        return engine.effectParams(forLayer: layerId, effectId: effectId).map { row in
            let value = (row["value"] as? [NSNumber])?.map(\.floatValue) ?? [0, 0, 0, 0]
            let def = (row["defaultValue"] as? [NSNumber])?.map(\.floatValue) ?? [0, 0, 0, 0]
            let lo: Float = (row["min"] as? NSNumber)?.floatValue ?? 0
            let hi: Float = (row["max"] as? NSNumber)?.floatValue ?? 1
            let rawHardMin: Float = (row["hardMin"] as? NSNumber)?.floatValue ?? lo
            let rawHardMax: Float = (row["hardMax"] as? NSNumber)?.floatValue ?? hi
            let index = (row["index"] as? NSNumber)?.uint32Value ?? 0
            let text = fxLocalizedParam(typeId, index: Int(index), label: row["label"] as? String ?? "", unit: row["unit"] as? String ?? "",
                                        options: row["enumLabels"] as? [String] ?? [])
            return EffectParamItem(flags: (row["flags"] as? NSNumber)?.uint32Value ?? 0, index: index,
                                   type: (row["type"] as? NSNumber)?.uint32Value ?? 0,
                                   label: text.label, unit: text.unit, paramId: row["id"] as? String ?? "",
                                   value: value, defaultValue: def, minValue: lo, maxValue: hi,
                                   hardMin: rawHardMin.isNaN ? lo : Swift.min(rawHardMin, lo),
                                   hardMax: rawHardMax.isNaN ? hi : Swift.max(rawHardMax, hi),
                                   animated: row["animated"] as? Bool ?? false, enumLabels: text.options)
        }
    }

    // --- Ímã (P1-2) ---------------------------------------------------------------
    /// Alvos do ímã no cabeçote: pares (valor, guia). `axis` 0/1 posição X/Y (guia em px da
    /// composição), 2/3 larguras/alturas das outras camadas.
    func snapTargets(axis: Int, layer: Int64? = nil) -> [(value: Float, guide: Float)] {
        guard let id = layer ?? primarySelection else { return [] }
        let raw = engine.snapTargets(id, axis: Int32(axis)).map(\.floatValue)
        return stride(from: 0, to: raw.count - 1, by: 2).map { (raw[$0], raw[$0 + 1]) }
    }
}
