import SwiftUI
import UIKit

// =============================================================================
//  0.5.3: legendas no jeito do Premiere, com a cara do AUREA (par do Android:
//  CaptionWorkspace.kt, CaptionListTab.kt, CaptionTranscriptTab.kt e
//  CaptionTextBoxControls em CaptionLookControls.kt). As regras de edição dos
//  blocos moram no motor (`edit_caption_track`); aqui só a tela.
//  Desenho: build/reference/am-ui-20261113/CAPTIONS_PREMIERE.md.
// =============================================================================

/// Contas puras das abas Transcrição e Legendas (gêmeas de CaptionWorkspace.kt).
enum CaptionWorkspace {
    static func wordsBefore(_ text: String, cursor: Int) -> Int {
        let utf16 = text as NSString
        let offset = min(max(cursor, 0), utf16.length)
        let head = utf16.substring(to: offset)
        let count = head.split(whereSeparator: { $0.isWhitespace }).count
        let before = head.last
        let after = utf16.substring(from: offset).first
        let inside = before.map { !$0.isWhitespace } == true && after.map { !$0.isWhitespace } == true
        return max(0, count - (inside ? 1 : 0))
    }
    private static let sentenceEnd: Set<Character> = [".", "!", "?", "…", "。", "！", "？"]

    static func endsSentence(_ text: String) -> Bool {
        guard let last = text.trimmingCharacters(in: .whitespaces).last else { return false }
        return sentenceEnd.contains(last)
    }

    /// Onde começa cada parágrafo: pausa ≥ `pauseSec`, fim de frase com `softWords` palavras, no máximo `maxWords`.
    static func paragraphStarts(_ words: [CaptionWord], pauseSec: Double = 1.0, softWords: Int = 25, maxWords: Int = 60) -> [Int] {
        guard !words.isEmpty else { return [] }
        var out = [0]
        var count = 1
        for i in 1..<words.count {
            let prev = words[i - 1]
            let pause = words[i].start - prev.end >= pauseSec
            let sentence = endsSentence(prev.word) && count >= softWords
            if pause || sentence || count >= maxWords { out.append(i); count = 1 } else { count += 1 }
        }
        return out
    }

    /// Relógio do parágrafo: "m:ss" (ou "h:mm:ss").
    static func clock(_ sec: Double) -> String {
        let s = Int(max(0, sec))
        let h = s / 3600, m = s / 60 % 60, r = s % 60
        return h > 0 ? String(format: "%d:%02d:%02d", h, m, r) : String(format: "%d:%02d", m, r)
    }

    /// Código de tempo da lista: "mm:ss:qq" (quadros), com a hora quando houver.
    static func timecode(_ frame: Int, fps: Double) -> String {
        let rate = fps > 0 ? fps : 30
        let f = max(0, frame)
        let whole = max(1, Int(rate.rounded()))
        let sec = Int(Double(f) / rate)
        let q = min(max(0, f - Int((Double(sec) * rate).rounded())), whole - 1)
        let h = sec / 3600, m = sec / 60 % 60, s = sec % 60
        return h > 0 ? String(format: "%d:%02d:%02d:%02d", h, m, s, q) : String(format: "%02d:%02d:%02d", m, s, q)
    }

    /// Buscar e substituir na transcrição: cada palavra troca só o próprio texto e guarda o tempo.
    static func replaceWords(_ words: [CaptionWord], find: String, replacement: String, matchCase: Bool) -> ([CaptionWord], Int) {
        guard !find.isEmpty else { return (words, 0) }
        let options: String.CompareOptions = matchCase ? [] : [.caseInsensitive]
        var total = 0
        var out: [CaptionWord] = []
        for var w in words {
            let hits = count(w.word, find, matchCase: matchCase)
            guard hits > 0 else { out.append(w); continue }
            total += hits
            let trimmed = w.word.replacingOccurrences(of: find, with: replacement, options: options).trimmingCharacters(in: .whitespacesAndNewlines)
            if !trimmed.isEmpty { w.word = trimmed; out.append(w) }
        }
        return (out, total)
    }

    /// Índice do bloco na agulha (quadro da timeline), ou -1.
    static func activeIndex(_ track: NativeCaptionTrack, frame: Int) -> Int {
        let local = frame - Int(track.shift ?? 0)
        return track.segments.firstIndex { local >= Int($0.start) && local < Int($0.end) } ?? -1
    }

    static func count(_ text: String, _ find: String, matchCase: Bool) -> Int {
        guard !find.isEmpty else { return 0 }
        var n = 0
        var from = text.startIndex
        while let r = text.range(of: find, options: matchCase ? [] : [.caseInsensitive], range: from..<text.endIndex) { n += 1; from = r.upperBound }
        return n
    }
}

/// Uma operação do motor na faixa (um passo de desfazer); falso = nada mudou.
@MainActor
func captionEdit(_ model: AureaModel, layer: Int64, op: String, ids: [Int64], fields: [String: Any] = [:]) -> Bool {
    var value = fields; value["op"] = op; value["ids"] = ids
    guard let data = try? JSONSerialization.data(withJSONObject: value), let command = String(data: data, encoding: .utf8) else { return false }
    guard model.engine.editCaptionTrack(layer, command: command) else { return false }
    model.refreshModel(force: true)
    return true
}

/// Buscar e substituir (lista e transcrição): `onReplace` devolve as trocas; zero = nada encontrado.
struct CaptionFindReplace: View {
    let onReplace: (_ find: String, _ with: String, _ matchCase: Bool) -> Int
    @State private var find = ""
    @State private var with = ""
    @State private var exact = false
    @State private var result: String?
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            field($find, AureaText.t("cap053_find"))
            field($with, AureaText.t("cap053_replace_with"))
            HStack {
                Text(AureaText.t("cap053_match_case")).font(.aurea(size: 13)).frame(maxWidth: .infinity, alignment: .leading)
                AureaToggle(checked: exact, onCheckedChange: { exact = $0 })
            }.frame(height: 44)
            CaptionAction(label: AureaText.t("cap053_replace_all"), primary: true, enabled: !find.isEmpty) {
                let n = onReplace(find, with, exact)
                result = n > 0 ? AureaText.t("cap053_replaced_n", n) : AureaText.t("cap053_not_found")
            }
            if let result { Text(result).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted) }
        }.padding(.vertical, 6)
    }
    private func field(_ text: Binding<String>, _ hint: String) -> some View {
        TextField(hint, text: text).font(.aurea(size: 14)).foregroundStyle(AureaColors.text).tint(AureaColors.accent)
            .padding(10).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
            .accessibilityLabel(hint)
    }
}

/// LEGENDAS (a lista, o centro do trabalho). Par de CaptionListTab.kt.
struct CaptionListView: View {
    @EnvironmentObject private var model: AureaModel
    let track: NativeCaptionTrack
    let onOwnStyle: () -> Void
    @State private var multi = false
    @State private var finding = false
    @State private var message: String?
    @State private var text = ""
    @State private var cursor = 0
    @State private var shareURL: URL?
    private var fps: Double { model.compositionFps > 0 ? Double(model.compositionFps) : 30 }
    private var shift: Int { Int(track.shift ?? 0) }
    private var chosen: Set<Int64> { model.captionSelection }

    var body: some View {
        let active = CaptionWorkspace.activeIndex(track, frame: Int(model.status.playhead))
        VStack(alignment: .leading, spacing: 0) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    CaptionAction(label: AureaText.t("cap053_insert"), primary: true) { insert() }
                    CaptionAction(label: AureaText.t("cap053_find_replace"), primary: finding) { finding.toggle() }
                    CaptionAction(label: AureaText.t(multi ? "cap053_done_choosing" : "cap053_choose_many"), primary: multi) {
                        multi.toggle()
                        if !multi && chosen.count > 1 { model.captionSelection = [] }
                    }
                    CaptionAction(label: AureaText.t("cap053_export_srt")) { export(vtt: false) }
                    CaptionAction(label: AureaText.t("cap053_export_vtt")) { export(vtt: true) }
                }.padding(.vertical, 4)
            }
            if finding {
                CaptionFindReplace { find, with, exact in
                    let ids = multi ? Array(chosen) : []
                    let n = track.segments.filter { ids.isEmpty || ids.contains($0.id) }.reduce(0) { $0 + CaptionWorkspace.count($1.text, find, matchCase: exact) }
                    guard n > 0, edit("replace", ids, ["find": find, "replace": with, "matchCase": exact]) else { return 0 }
                    return n
                }
            }
            if let message { Text(message).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.vertical, 6) }
            if multi && !chosen.isEmpty {
                Text(AureaText.t("cap053_selected_n", chosen.count)).font(.aurea(size: 13)).foregroundStyle(AureaColors.accent).padding(.vertical, 4)
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        CaptionAction(label: AureaText.t("panel_unir"), enabled: chosen.count > 1) { _ = edit("merge", Array(chosen)) }
                        CaptionAction(label: AureaText.t("cap053_regroup")) { _ = edit("regroup", Array(chosen)) }
                        CaptionAction(label: AureaText.t("cap053_own_style"), action: onOwnStyle)
                        CaptionAction(label: AureaText.t("common_delete")) { _ = edit("delete", Array(chosen)) }
                        CaptionAction(label: AureaText.t("editor_limpar_selecao")) { model.captionSelection = [] }
                    }
                }
            }
            ForEach(Array(track.segments.enumerated()), id: \.element.id) { index, block in
                row(block, playing: index == active)
                if !multi && chosen == [block.id] { editor(index) }
            }
        }
        .sheet(item: Binding(get: { shareURL.map(CaptionShareFile.init) }, set: { shareURL = $0?.url })) { file in ExportShareSheet(url: file.url) }
    }

    private func row(_ block: NativeCaptionBlock, playing: Bool) -> some View {
        let on = chosen.contains(block.id)
        let time = ltrIsolado(CaptionWorkspace.timecode(Int(block.start) + shift, fps: fps) + " – " + CaptionWorkspace.timecode(Int(block.end) + shift, fps: fps))
        return HStack(spacing: 8) {
            RoundedRectangle(cornerRadius: 2).fill(playing ? AureaColors.accent : Color.clear).frame(width: 3, height: 30)
            VStack(alignment: .leading, spacing: 2) {
                Text(time).font(.aurea(size: 11)).foregroundStyle(on ? AureaColors.accent : AureaColors.muted)
                Text(block.text.replacingOccurrences(of: "\n", with: " ")).font(.aurea(size: 14)).foregroundStyle(AureaColors.text).lineLimit(2)
            }.frame(maxWidth: .infinity, alignment: .leading)
            if block.look != nil {
                Circle().fill(AureaColors.accent).frame(width: 8, height: 8).accessibilityLabel(AureaText.t("cap046_block_own_style"))
            }
            if multi { Text(on ? "●" : "○").font(.aurea(size: 16)).foregroundStyle(on ? AureaColors.accent : AureaColors.muted) }
        }
        .padding(.horizontal, 12).padding(.vertical, 8).frame(minHeight: 52)
        .background(on ? AureaColors.accentDim : AureaColors.chip.opacity(playing ? 0.9 : 0.45), in: RoundedRectangle(cornerRadius: 10))
        .contentShape(Rectangle())
        .onTapGesture {
            if multi {
                if on { model.captionSelection.remove(block.id) } else { model.captionSelection.insert(block.id) }
            } else {
                model.captionSelection = on ? [] : [block.id]
                text = block.text
                model.seek(toFrame: Int64(Int(block.start) + shift))
            }
        }
        .onLongPressGesture { multi = true; model.captionSelection.insert(block.id) }
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(on ? [.isButton, .isSelected] : .isButton)
        .accessibilityAction(named: Text(AureaText.t("cap053_choose_many"))) { multi = true; model.captionSelection.insert(block.id) }
        .padding(.vertical, 3)
    }

    /// O bloco escolhido aberto na linha: texto, dividir antes de uma palavra, entrada/saída e ações.
    private func editor(_ index: Int) -> some View {
        let block = track.segments[index]
        let ids = [block.id]
        let words = block.text.split(whereSeparator: { $0 == " " || $0 == "\n" }).map(String.init)
        let prev = index > 0 ? track.segments[index - 1] : nil
        let next = index + 1 < track.segments.count ? track.segments[index + 1] : nil
        return VStack(alignment: .leading, spacing: 6) {
            CaptionCursorInput(text: $text, cursor: $cursor)
                .frame(height: 96)
                .padding(10).background(AureaColors.chip, in: RoundedRectangle(cornerRadius: 8))
                .onAppear { text = block.text; cursor = 0 }
            CaptionAction(label: AureaText.t("edt_cap_apply_text"), primary: true,
                          enabled: !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && text != block.text) {
                _ = edit("text", ids, ["text": text.trimmingCharacters(in: .whitespacesAndNewlines)])
            }
            let wordsBefore = CaptionWorkspace.wordsBefore(text, cursor: cursor)
            CaptionAction(label: AureaText.t("cap053_split_cursor"),
                          enabled: text == block.text && wordsBefore > 0 && wordsBefore < words.count) {
                _ = edit("split_word", ids, ["words": wordsBefore], failure: "cap053_split_failed")
            }
            edge("cap053_in", frame: Int(block.start) + shift, back: { _ = edit("nudge", ids, ["in": -1]) }, forward: { _ = edit("nudge", ids, ["in": 1]) })
            edge("cap053_out", frame: Int(block.end) + shift, back: { _ = edit("nudge", ids, ["out": -1]) }, forward: { _ = edit("nudge", ids, ["out": 1]) })
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    CaptionAction(label: AureaText.t("cap053_go_to")) { model.seek(toFrame: Int64(Int(block.start) + shift)) }
                    CaptionAction(label: AureaText.t("edt_cap_split_here")) { _ = edit("split", ids, ["playhead": model.status.playhead], failure: "cap053_split_failed") }
                    CaptionAction(label: AureaText.t("cap053_merge_prev"), enabled: prev != nil) {
                        if let prev, edit("merge", [prev.id, block.id]) { model.captionSelection = [prev.id] }
                    }
                    CaptionAction(label: AureaText.t("cap053_merge_next"), enabled: next != nil) {
                        if let next { _ = edit("merge", [block.id, next.id]) }
                    }
                    CaptionAction(label: AureaText.t("cap053_own_style"), action: onOwnStyle)
                    CaptionAction(label: AureaText.t("common_delete")) { _ = edit("delete", ids) }
                }
            }
        }.padding(.leading, 8).padding(.bottom, 10)
    }

    private func edge(_ key: String, frame: Int, back: @escaping () -> Void, forward: @escaping () -> Void) -> some View {
        let label = AureaText.t(key)
        return HStack {
            Text(label).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).frame(width: 72, alignment: .leading)
            Text(ltrIsolado(CaptionWorkspace.timecode(frame, fps: fps))).font(.aurea(size: 14, weight: .semibold)).frame(maxWidth: .infinity, alignment: .leading)
            CaptionAction(label: "‹", action: back).accessibilityLabel(label + ": " + AureaText.t("cap053_frame_back"))
            CaptionAction(label: "›", action: forward).accessibilityLabel(label + ": " + AureaText.t("cap053_frame_fwd"))
        }.frame(minHeight: 44)
    }

    @discardableResult
    private func edit(_ op: String, _ ids: [Int64], _ fields: [String: Any] = [:], failure: String = "ios_caption_edit_invalid") -> Bool {
        let ok = captionEdit(model, layer: track.layer, op: op, ids: ids, fields: fields)
        message = ok ? nil : AureaText.t(failure)
        // Bloco que sumiu (apagado, unido) sai da escolha.
        if ok, let fresh = model.captionTracks.first(where: { $0.layer == track.layer }) {
            let alive = Set(fresh.segments.map(\.id))
            model.captionSelection = model.captionSelection.intersection(alive)
        }
        return ok
    }

    private func insert() {
        let before = track.segments.map(\.id).max() ?? 0
        guard edit("insert", [], ["playhead": model.status.playhead, "text": AureaText.t("cap053_new_caption_text")]) else { return }
        if let fresh = model.captionTracks.first(where: { $0.layer == track.layer }), let made = fresh.segments.first(where: { $0.id > before }) {
            multi = false; model.captionSelection = [made.id]; text = made.text
        }
    }

    /// SRT/VTT num arquivo temporário e a folha de compartilhar (Salvar em Arquivos, AirDrop…).
    private func export(vtt: Bool) {
        let content = model.engine.captionExport(track.layer, vtt: vtt)
        let url = FileManager.default.temporaryDirectory.appendingPathComponent(vtt ? "legendas.vtt" : "legendas.srt")
        do {
            guard !content.isEmpty else { throw CocoaError(.fileWriteUnknown) }
            try content.write(to: url, atomically: true, encoding: .utf8)
            message = AureaText.t("cap053_exported"); shareURL = url
        } catch { message = AureaText.t("cap053_export_failed") }
    }
}

private struct CaptionShareFile: Identifiable {
    let url: URL
    var id: String { url.path }
}

private struct CaptionCursorInput: UIViewRepresentable {
    @Binding var text: String
    @Binding var cursor: Int

    func makeCoordinator() -> Coordinator { Coordinator(self) }
    func makeUIView(context: Context) -> UITextView {
        let view = UITextView()
        view.delegate = context.coordinator
        view.font = .systemFont(ofSize: 15)
        view.textColor = UIColor(AureaColors.text)
        view.tintColor = UIColor(AureaColors.accent)
        view.backgroundColor = .clear
        view.textContainerInset = .zero
        view.accessibilityLabel = AureaText.t("panel_texto")
        view.accessibilityIdentifier = "captions.text.input"
        return view
    }
    func updateUIView(_ view: UITextView, context: Context) {
        context.coordinator.parent = self
        if view.text != text && view.markedTextRange == nil {
            context.coordinator.updating = true
            view.text = text
            view.selectedRange = NSRange(location: min(cursor, (text as NSString).length), length: 0)
            context.coordinator.updating = false
        }
    }
    final class Coordinator: NSObject, UITextViewDelegate {
        var parent: CaptionCursorInput
        var updating = false
        init(_ parent: CaptionCursorInput) { self.parent = parent }
        func textViewDidChange(_ view: UITextView) {
            guard !updating else { return }
            parent.text = view.text ?? ""
            parent.cursor = view.selectedRange.location
        }
        func textViewDidChangeSelection(_ view: UITextView) {
            guard !updating else { return }
            parent.cursor = view.selectedRange.location
        }
    }
}

/// TRANSCRIÇÃO: a fala em parágrafos; tocar escolhe e leva a agulha; corrigir,
/// buscar/substituir e criar legendas de um trecho. Par de CaptionTranscriptTab.kt.
struct CaptionTranscriptView: View {
    let words: [CaptionWord]
    let fillers: Set<UUID>
    let struckOn: Bool
    let source: String?
    let busy: Bool
    let onSeek: (CaptionWord) -> Void
    let onEdit: (UUID, String) -> Void
    let onReplace: (String, String, Bool) -> Int
    let onCreate: (ClosedRange<Int>) -> Void
    @State private var chosen: Int?
    @State private var anchor: Int?
    @State private var reach: Int?
    @State private var finding = false

    private var range: ClosedRange<Int>? {
        if let anchor { let r = reach ?? anchor; return min(anchor, r)...max(anchor, r) }
        return chosen.map { $0...$0 }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            if words.isEmpty {
                Text(AureaText.t("cap053_no_transcript")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted).padding(.vertical, 6)
            } else {
                Text(source.map { AureaText.plural("pn_caption_transcript_source", words.count, $0, words.count) }
                     ?? AureaText.plural("pn_caption_transcript", words.count, words.count))
                    .font(.aurea(size: 13, weight: .bold)).foregroundStyle(AureaColors.muted).padding(.top, 8)
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 6) {
                        CaptionAction(label: AureaText.t("cap053_find_replace"), primary: finding) { finding.toggle() }
                        if let range {
                            CaptionAction(label: AureaText.t("cap053_create_from_selection"), primary: true, enabled: !busy) { onCreate(range) }
                            CaptionAction(label: AureaText.t("editor_limpar_selecao")) { chosen = nil; anchor = nil; reach = nil }
                        }
                    }.padding(.vertical, 6)
                }
                if finding {
                    CaptionFindReplace { find, with, exact in
                        let n = onReplace(find, with, exact)
                        if n > 0 { chosen = nil; anchor = nil; reach = nil }
                        return n
                    }
                }
                if anchor != nil { Text(AureaText.t("cap053_select_hint")).font(.aurea(size: 13)).foregroundStyle(AureaColors.muted) }
                if let range, range.count > 1 {
                    Text(AureaText.t("cap053_selection_n", range.count)).font(.aurea(size: 13)).foregroundStyle(AureaColors.accent).padding(.vertical, 4)
                }
                let starts = CaptionWorkspace.paragraphStarts(words)
                LazyVStack(alignment: .leading, spacing: 8) {
                    ForEach(starts.indices, id: \.self) { p in paragraph(from: starts[p], to: p + 1 < starts.count ? starts[p + 1] : words.count) }
                }
            }
        }
    }

    private func paragraph(from: Int, to: Int) -> some View {
        VStack(alignment: .leading, spacing: 4) {
            Text(ltrIsolado(CaptionWorkspace.clock(words[from].start))).font(.aurea(size: 11)).foregroundStyle(AureaColors.muted)
            AureaFlowLayout(hGap: 4, vGap: 4) {
                ForEach(from..<to, id: \.self) { i in
                    let word = words[i]
                    let struck = struckOn && fillers.contains(word.id)
                    let on = range?.contains(i) ?? false
                    Button {
                        if anchor != nil { reach = i } else { chosen = i }
                        onSeek(word)
                    } label: {
                        Text(word.word).font(.aurea(size: 14)).strikethrough(struck)
                            .foregroundStyle(struck ? AureaColors.muted : AureaColors.text)
                            .padding(.horizontal, 4).padding(.vertical, 3)
                            .background(on ? AureaColors.accentDim : Color.clear, in: RoundedRectangle(cornerRadius: 6))
                    }.buttonStyle(AureaPressStyle()).accessibilityAddTraits(on ? .isSelected : [])
                }
            }
            if let c = chosen, anchor == nil, (from..<to).contains(c) {
                CaptionWordEditor(word: words[c], onDone: { onEdit(words[c].id, $0); chosen = nil },
                                  onExtend: { anchor = c; reach = c }).id(words[c].id)
            }
        }.padding(.bottom, 10)
    }
}

/// "Texto e caixa": alinhamento, entrelinha, margem e cantos do fundo, cor e
/// desfoque da sombra. Par de CaptionTextBoxControls (Android).
struct CaptionTextBoxView: View {
    @Binding var look: CaptionLook
    let onChange: () -> Void
    private func change(_ f: (inout CaptionLook) -> Void) { f(&look); onChange() }
    var body: some View {
        let fromStyle = AureaText.t("cap046_from_style")
        VStack(alignment: .leading, spacing: 0) {
            section("cap053_align")
            CaptionChips(options: [fromStyle, AureaText.t("cap053_align_left"), AureaText.t("cap053_align_center"), AureaText.t("cap053_align_right")],
                         selected: look.has(CaptionLook.layout) ? min(max(look.align, 0), 2) + 1 : 0) { i in
                change { l in if i == 0 { l.set(CaptionLook.layout, false) } else { l.set(CaptionLook.layout, true); l.align = i - 1 } }
            }
            section("cap053_line_spacing")
            let spacings: [Float] = [1.0, 1.15, 1.3, 1.6]
            CaptionChips(options: [fromStyle] + spacings.map { numeroPtBr($0, casas: 2) },
                         selected: look.has(CaptionLook.layout) ? (spacings.firstIndex { abs($0 - look.lineSpacing) < 0.01 } ?? -1) + 1 : 0) { i in
                change { l in if i == 0 { l.set(CaptionLook.layout, false) } else { l.set(CaptionLook.layout, true); l.lineSpacing = spacings[i - 1] } }
            }
            if look.has(CaptionLook.box) && look.boxOn {
                section("cap053_box_padding")
                let pads: [Float] = [0.15, 0.3, 0.5]
                CaptionChips(options: ["pn_size_small_short", "pn_size_medium_short", "pn_size_large_short"].map { AureaText.t($0) },
                             selected: pads.firstIndex { abs($0 - look.boxPadding) < 0.01 } ?? -1) { i in change { $0.boxPadding = pads[i] } }
                section("cap053_box_radius")
                let radii: [Float] = [0, 0.25, 0.6]
                CaptionChips(options: ["cap053_corner_square", "cap053_corner_soft", "cap053_corner_round"].map { AureaText.t($0) },
                             selected: radii.firstIndex { abs($0 - look.boxRadius) < 0.01 } ?? -1) { i in change { $0.boxRadius = radii[i] } }
            } else {
                Text(AureaText.t("cap053_box_hint")).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.vertical, 4)
            }
            if look.has(CaptionLook.shadow) && look.shadowOn {
                section("cap053_shadow_color")
                let colors: [(UInt32, String)] = [(0x000000, "cap046_color_black"), (0xFFFFFF, "cap046_color_white"), (0x2F7BFF, "cap046_color_blue"),
                                                  (0xA259FF, "cap046_color_purple"), (0xE5252A, "cap046_color_red"), (0xFFC23D, "cap046_color_gold")]
                CaptionChips(options: colors.map { AureaText.t($0.1) },
                             selected: colors.firstIndex { c in abs(look.shadowColor[0] - Float((c.0 >> 16) & 255) / 255) < 0.01
                                 && abs(look.shadowColor[1] - Float((c.0 >> 8) & 255) / 255) < 0.01 && abs(look.shadowColor[2] - Float(c.0 & 255) / 255) < 0.01 } ?? -1) { i in
                    let hex = colors[i].0
                    change { $0.shadowColor = [Float((hex >> 16) & 255) / 255, Float((hex >> 8) & 255) / 255, Float(hex & 255) / 255, 0.75] }
                }
                section("cap053_shadow_blur")
                let blurs: [Float] = [0.06, 0.12, 0.3]
                CaptionChips(options: ["cap053_soft", "cap046_medium", "cap053_strong"].map { AureaText.t($0) },
                             selected: blurs.firstIndex { abs($0 - look.shadowBlur) < 0.01 } ?? -1) { i in change { $0.shadowBlur = blurs[i] } }
            }
            Spacer().frame(height: 6)
        }
    }
    private func section(_ key: String) -> some View {
        Text(AureaText.t(key)).font(.aurea(size: 12)).foregroundStyle(AureaColors.muted).padding(.top, 6)
    }
}
