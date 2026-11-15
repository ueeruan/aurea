#include "aurea/Engine.hpp"
#include "aurea/project/Presets.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>

namespace aurea {
i64 Engine::caption_word_frame(u64 sourceLayer, f64 seconds) noexcept {
    if (!std::isfinite(seconds) || seconds < 0) return -1;
    std::lock_guard lock(modelMutex_);
    const auto* comp = project_ ? current_composition() : nullptr;
    const Layer* source = comp ? comp->layer(LayerId::unpack(sourceLayer)) : nullptr;
    if (!source || source->end.value <= source->start.value || comp->fps() <= 0) return -1;
    const f64 target = seconds * comp->fps();
    if (!std::isfinite(target)) return -1;
    const i64 current = playback_.current().value;
    if ((!source->timeRemapEnabled || source->timeRemap.keys.empty()) && !source->speed_track()) {
        if (!std::isfinite(source->speed)) return -1;
        if (source->speed == 0.f) return std::clamp(current, source->start.value, source->end.value - 1);
        const f64 elapsed = (target - source->offset.value) / source->speed;
        const f64 timeline = source->reversed ? source->end.value - 1 - elapsed : source->start.value + elapsed;
        return static_cast<i64>(std::llround(std::clamp(timeline, static_cast<f64>(source->start.value),
            static_cast<f64>(source->end.value - 1))));
    }
    i64 best = source->start.value;
    f64 error = std::numeric_limits<f64>::infinity();
    for (i64 frame = source->start.value; frame < source->end.value; ++frame) {
        const f64 distance = std::abs(source->source_frame(FrameIndex{frame}) - target);
        if (distance < error || (distance == error && std::abs(frame - current) < std::abs(best - current))) {
            error = distance;
            best = frame;
        }
    }
    return std::isfinite(error) ? best : -1;
}

std::string Engine::caption_tracks() noexcept {
    std::lock_guard lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    json::Writer w; w.begin_array();
    if (comp) for (u32 i = 0; i < comp->order().size(); ++i) {
        const auto id = comp->order().at(i); const auto* l = comp->layer(id);
        if (!l || l->captions.empty()) continue;
        w.begin_object().key("layer").value(static_cast<i64>(id.pack())).key("source").value(static_cast<i64>(l->text.captionSource));
        // 0.5.3: quadro local do bloco + shift = quadro da timeline (a lista mostra e leva a agulha até lá).
        w.key("shift").value(l->start.value - l->offset.value);
        w.key("segments").begin_array();
        for (const auto& s : l->captions) {
            w.begin_object().key("id").value(static_cast<i64>(s.id)).key("start").value(s.start).key("end").value(s.end).key("text").value(s.text);
            // 0.4.6: estilo próprio do bloco (JSON das opções, o mesmo de caption_options).
            if (s.look) w.key("look").value(presets::caption_options_json(*s.look, true));
            w.key("words").begin_array();
            for (const auto& word : s.words) w.begin_object().key("text").value(word.text).key("start").value(word.start).key("end").value(word.end).end_object();
            w.end_array().end_object();
        }
        w.end_array().end_object();
    }
    return w.end_array().str();
}

namespace {
/// A faixa de legendas de `id`: a própria camada ou a faixa feita da fala dela.
Layer* caption_track_of(Composition& comp, u64 id) {
    Layer* direct = comp.layer(LayerId::unpack(id));
    if (direct && !direct->captions.empty()) return direct;
    for (u32 i = 0; i < comp.order().size(); ++i) {
        Layer* l = comp.layer(comp.order().at(i));
        if (l && !l->captions.empty() && l->text.captionSource == id) return l;
    }
    return nullptr;
}
} // namespace

namespace {
/// Segundo da MÍDIA da fonte → quadro da timeline, a mesma conta de
/// Engine::create_captions (velocidade, reverso e remapeamento inclusos).
struct MediaFrames {
    i64 s0 = 0, s1 = 1;
    f64 half = 0.0;
    bool monotonic = true;
    std::vector<f64> srcSec;
    MediaFrames(const Layer& src, f64 fps) {
        s0 = src.start.value; s1 = std::max(src.start.value + 1, src.end.value);
        srcSec.resize(static_cast<usize>(s1 - s0));
        for (i64 f = s0; f < s1; ++f) srcSec[static_cast<usize>(f - s0)] = src.source_frame(FrameIndex{f}) / fps;
        half = 0.5 / fps;
        monotonic = std::is_sorted(srcSec.begin(), srcSec.end());
    }
    i64 frame_of(f64 sec) const {
        if (monotonic) {
            const auto it = std::lower_bound(srcSec.begin(), srcSec.end(), sec, [this](f64 v, f64 target) { return v + half < target; });
            return it == srcSec.end() ? -1 : s0 + static_cast<i64>(it - srcSec.begin());
        }
        for (usize i = 0; i < srcSec.size(); ++i) if (srcSec[i] + half >= sec) return s0 + static_cast<i64>(i);
        return -1;
    }
};
} // namespace

std::string Engine::caption_export(u64 layerId, bool vtt) noexcept {
    std::lock_guard lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    const Layer* track = comp ? caption_track_of(*comp, layerId) : nullptr;
    if (!track) return {};
    const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
    return text::export_captions(track->captions, fps, track->start.value - track->offset.value, vtt, track->captionOptions.uppercase);
}

std::string Engine::caption_options(u64 layerId) noexcept {
    std::lock_guard lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    const Layer* track = comp ? caption_track_of(*comp, layerId) : nullptr;
    // "Tirar vícios" não mora na faixa (a fala já veio sem eles).
    return track ? presets::caption_options_json(track->captionOptions, true) : std::string{};
}

bool Engine::set_caption_options(u64 layerId, const text::CaptionOptions& options) noexcept {
    std::lock_guard lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    Layer* track = comp ? caption_track_of(*comp, layerId) : nullptr;
    if (!track || track->locked) return false;
    text::CaptionOptions next = options;
    next.style = std::min(next.style, text::kCaptionStyleCount - 1);
    next.look &= text::kCaptionLookAll;
    next.posY = std::clamp(next.posY, 0.1f, 0.9f);
    next.sizeFrac = std::clamp(next.sizeFrac, 0.02f, 0.25f);
    next.minDurationSec = std::isfinite(next.minDurationSec) ? std::clamp(next.minDurationSec, 0.1f, 10.0f) : 0.8f;
    next.gapFrames = std::min(next.gapFrames, 600u);
    next.align = std::min(next.align, 2u);
    if (!next.fontPath.empty() && next.fontPath.rfind("docs:", 0) != 0) next.fontPath = store_asset_path(next.fontPath);
    const text::CaptionOptions before = track->captionOptions;
    // Nada mudou (a interface manda tudo a cada toque): sem passo de desfazer vazio.
    if (presets::caption_options_json(before, true) == presets::caption_options_json(next, true)) return true;
    history_.before_mutation(*comp, project_->timeline().current(), "estilo da legenda");
    text::update_caption_look(before, next, std::min(comp->width(), comp->height()), track->text, track->tracks);
    if (before.posY != next.posY)
        track->transform.position.y = static_cast<f32>(comp->height()) * next.posY;
    track->captionOptions = next;
    recenter_text(*track);
    bump_model_revision_locked(); project_->mark_dirty(); request_render();
    return true;
}

u64 Engine::query_captions(bridge::CaptionRow* rows, u32 capacity, char* text, u32 textCapacity) noexcept {
    if (!rows && capacity) return 0;
    std::lock_guard lock(modelMutex_);
    const auto* comp = project_ ? current_composition() : nullptr;
    u32 count = 0, total = 0, used = 0;
    if (comp) for (u32 i = 0; i < comp->order().size(); ++i) {
        const auto layerId = comp->order().at(i);
        const auto* l = comp->layer(layerId);
        if (!l || l->captions.empty()) continue;
        // Tempo da régua: os blocos andam com a faixa quando ela é movida.
        const i64 shift = l->start.value - l->offset.value;
        for (const auto& segment : l->captions) {
            ++total;
            // Sem espaço em qualquer um dos buffers: conta e segue, para o
            // chamador ver `count < total` e crescer os dois (o texto nunca é
            // vazio, então dobrar os dois sempre acaba cabendo).
            if (count >= capacity || used + segment.text.size() > textCapacity) continue;
            bridge::CaptionRow& row = rows[count];
            row.layerId = layerId.pack();
            row.id = segment.id;
            row.start = static_cast<i32>(segment.start + shift);
            row.end = static_cast<i32>(segment.end + shift);
            row.words = static_cast<u32>(segment.words.size());
            row.reserved = 0;
            row.textOffset = used;
            row.textLength = static_cast<u32>(segment.text.size());
            std::memcpy(text + used, segment.text.data(), segment.text.size());
            used += static_cast<u32>(segment.text.size());
            ++count;
        }
    }
    return (static_cast<u64>(count) << 32) | total;
}

bool Engine::edit_caption_track(u64 layerId, const std::string& command) noexcept {
    if (command.size() > 256 * 1024) return false;
    json::Value value;
    if (!json::parse(command, value) || !value.is_object()) return false;
    const auto* operation = value.get("op"); const auto* ids = value.get("ids");
    if (!operation || !operation->is_string()) return false;
    // 0.5.3: inserir, buscar/substituir (vazio = todos) e criar da transcrição não precisam de bloco escolhido.
    const bool idsOptional = operation->string == "insert" || operation->string == "replace" || operation->string == "from_words";
    if (ids && !ids->is_array()) return false;
    if (!idsOptional && (!ids || ids->array.empty())) return false;
    std::set<u64> selected;
    if (ids) for (const auto& id : ids->array) {
        if (!id.is_number() || !std::isfinite(id.number) || id.number <= 0 || id.number > 100000000 || std::floor(id.number) != id.number) return false;
        selected.insert(static_cast<u64>(id.number));
    }
    auto number = [&](const char* key, i64& out) {
        const auto* n = value.get(key);
        if (!n || !n->is_number() || !std::isfinite(n->number) || std::abs(n->number) > 100000000 || std::floor(n->number) != n->number) return false;
        out = static_cast<i64>(n->number); return true;
    };
    std::lock_guard lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    auto* layer = comp ? comp->layer(LayerId::unpack(layerId)) : nullptr;
    if (!layer || layer->locked || layer->captions.empty()) return false;
    // Quadro da timeline - shift = quadro local dos blocos (a faixa pode ter sido movida).
    const i64 shift = layer->start.value - layer->offset.value;
    auto next = layer->captions;
    auto nextOptions = layer->captionOptions;
    auto nextTransform = layer->transform;
    if (std::count_if(next.begin(), next.end(), [&](const auto& s) { return selected.contains(s.id); }) != static_cast<i64>(selected.size())) return false;
    const auto& op = operation->string;
    if (op == "delete") {
        std::erase_if(next, [&](const auto& s) { return selected.contains(s.id); });
    } else if (op == "move") {
        i64 delta; if (!number("delta", delta)) return false;
        for (auto& s : next) if (selected.contains(s.id)) {
            s.start += delta; s.end += delta;
            for (auto& word : s.words) { word.start += delta; word.end += delta; }
        }
    } else if (op == "text") {
        const auto* text = value.get("text"); if (selected.size() != 1 || !text || !text->is_string() || text->string.empty() || text->string.size() > 16384) return false;
        for (auto& s : next) if (selected.contains(s.id)) text::edit_caption_text(s, text->string);
    } else if (op == "trim") {
        i64 start, end; if (selected.size() != 1 || !number("start", start) || !number("end", end) || start < 0 || end <= start) return false;
        for (auto& s : next) if (selected.contains(s.id)) {
            s.start = start; s.end = end;
            for (auto& word : s.words) { word.start = std::clamp(word.start, start, end-1); word.end = std::clamp(word.end, word.start+1, end); }
        }
    } else if (op == "split") {
        // "playhead" = quadro da timeline (a faixa pode ter sido movida); "frame" = local.
        i64 frame;
        if (selected.size() != 1) return false;
        if (number("playhead", frame)) frame -= shift;
        else if (!number("frame", frame)) return false;
        u64 newId = 1; for (const auto& s : next) newId = std::max(newId, s.id+1);
        auto it = std::find_if(next.begin(), next.end(), [&](const auto& s) { return selected.contains(s.id); });
        if (frame <= it->start || frame >= it->end) return false;
        auto right = *it; right.id = newId; right.start = frame; it->end = frame;
        // A palavra vai para o lado onde ela COMEÇA. Particionar por fim
        // duplicaria a palavra que atravessa o corte (ela apareceria nos dois
        // blocos, e o texto sairia repetido na tela).
        std::erase_if(it->words, [&](auto& word) { if (word.start >= frame) return true; word.end = std::min(word.end,frame); return false; });
        std::erase_if(right.words, [&](auto& word) { if (word.start < frame) return true; return false; });
        // Sem espaço entre ideogramas nem antes de pontuação (o mesmo do agrupamento).
        auto join = [](auto& segment) { segment.text.clear(); for (const auto& word : segment.words) segment.text = text::caption_join(segment.text, word.text); };
        join(*it); join(right); next.push_back(std::move(right));
    } else if (op == "split_word") {
        // Dividir no cursor do texto: as N primeiras palavras ficam, o resto vira bloco novo.
        i64 count; if (selected.size() != 1 || !number("words", count) || count <= 0) return false;
        u64 newId = 1; for (const auto& s : next) newId = std::max(newId, s.id + 1);
        auto it = std::find_if(next.begin(), next.end(), [&](const auto& s) { return selected.contains(s.id); });
        text::CaptionSegment right;
        if (!text::split_caption_at_word(*it, right, static_cast<usize>(count))) return false;
        right.id = newId;
        next.push_back(std::move(right));
    } else if (op == "nudge") {
        // Entrada/saída de um quadro (os ‹ › da lista), presas entre os vizinhos.
        i64 in = 0, out = 0;
        if (selected.size() != 1) return false;
        if (value.get("in") && !number("in", in)) return false;
        if (value.get("out") && !number("out", out)) return false;
        if (!in && !out) return false;
        const auto it = std::find_if(next.begin(), next.end(), [&](const auto& s) { return selected.contains(s.id); });
        text::nudge_caption(next, static_cast<usize>(it - next.begin()), in, out);
    } else if (op == "insert") {
        // Legenda nova na agulha (ou depois do bloco "after"): até o próximo bloco, no máximo "frames".
        const auto* text = value.get("text");
        if (!text || !text->is_string() || text->string.empty() || text->string.size() > 16384) return false;
        i64 at = 0, length = static_cast<i64>(std::llround(2.0 * (comp->fps() > 0.0 ? comp->fps() : 30.0)));
        if (const auto* after = value.get("after")) {
            if (!after->is_number()) return false;
            const auto it = std::find_if(next.begin(), next.end(), [&](const auto& s) { return static_cast<f64>(s.id) == after->number; });
            if (it == next.end()) return false;
            at = it->end;
        } else if (number("playhead", at)) at -= shift;
        else return false;
        if (value.get("frames") && (!number("frames", length) || length <= 0)) return false;
        at = std::max<i64>(at, 0);
        for (const auto& s : next) if (s.start <= at && at < s.end) at = s.end;
        i64 end = at + length;
        for (const auto& s : next) if (s.start >= at) { end = std::min(end, s.start); break; }
        if (end <= at) return false;
        u64 newId = 1; for (const auto& s : next) newId = std::max(newId, s.id + 1);
        next.push_back(text::make_caption_segment(newId, at, end, text->string));
    } else if (op == "replace") {
        // Buscar e substituir nos blocos escolhidos (nenhum = todos); o tempo das palavras que ficam continua.
        const auto* find = value.get("find"); const auto* with = value.get("replace"); const auto* matchCase = value.get("matchCase");
        if (!find || !find->is_string() || find->string.empty() || !with || !with->is_string() || with->string.size() > 4096) return false;
        const bool exact = matchCase && matchCase->type == json::Value::Type::Bool && matchCase->boolean;
        u32 total = 0;
        for (auto& s : next) {
            if (!selected.empty() && !selected.contains(s.id)) continue;
            std::string changed = s.text;
            const u32 n = text::replace_caption_text(changed, find->string, with->string, exact);
            if (!n) continue;
            total += n;
            if (changed.find_first_not_of(" \n\r\t") == std::string::npos) { s.text.clear(); continue; }
            if (changed.size() > 16384) return false;
            text::edit_caption_text(s, changed);
        }
        if (!total) return false;
        std::erase_if(next, [](const auto& s) { return s.text.empty(); });
    } else if (op == "from_words") {
        // Legendas de um trecho da transcrição: os blocos que o trecho cobre são
        // refeitos com as opções da faixa; o resto da faixa fica como está.
        const auto* list = value.get("words");
        if (!list || !list->is_array() || list->array.empty() || list->array.size() > 20000) return false;
        const Layer* src = comp->layer(LayerId::unpack(layer->text.captionSource));
        if (!src) return false;
        std::vector<text::CaptionWord> words;
        for (const auto& w : list->array) {
            const auto* t = w.get("text"); const auto* a = w.get("start"); const auto* b = w.get("end");
            if (!t || !t->is_string() || t->string.empty() || t->string.size() > 4096 || !a || !a->is_number() || !b || !b->is_number()
                || !std::isfinite(a->number) || !std::isfinite(b->number)) return false;
            words.push_back({t->string, a->number, b->number});
        }
        words = text::normalize_caption_words(words);
        const auto groups = text::group_captions(words, layer->captionOptions);
        if (groups.empty()) return false;
        const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        const MediaFrames media(*src, fps);
        std::vector<text::CaptionSegment> made;
        for (usize g = 0; g < groups.size(); ++g) {
            const auto& cg = groups[g];
            i64 a = media.frame_of(cg.start);
            if (a < 0) break;
            i64 b = media.frame_of(cg.end);
            if (b < 0) b = media.s1;
            b = std::max(b + std::min<i64>(8, static_cast<i64>(std::lround(fps * 0.25))), a + 1);
            if (g + 1 < groups.size()) {
                const i64 n = media.frame_of(groups[g + 1].start);
                if (n >= 0 && (n < b || n - b < static_cast<i64>(std::lround(fps * 0.3)))) b = std::max(n, a + 1);
            }
            b = std::min(b, media.s1);
            a -= shift; b -= shift;
            if (b <= a || a < 0) continue;
            text::CaptionSegment segment;
            segment.start = a; segment.end = b; segment.text = cg.text;
            for (u32 w = 0; w < cg.count; ++w) {
                const auto& input = words[cg.first + w];
                i64 begin = media.frame_of(input.start), finish = media.frame_of(input.end);
                begin = std::clamp(begin < 0 ? b - 1 : begin - shift, a, b - 1);
                finish = std::clamp(finish < 0 ? b : finish - shift, begin + 1, b);
                segment.words.push_back({input.text, begin, finish});
            }
            made.push_back(std::move(segment));
        }
        if (made.empty()) return false;
        // O trecho vai da primeira à última PALAVRA escolhida: a folga do fim do
        // bloco novo (até 8 quadros) não pode apagar o bloco seguinte.
        const i64 from = made.front().start;
        const i64 to = made.back().words.empty() ? made.back().end : made.back().words.back().end;
        std::erase_if(next, [&](const auto& s) { return s.start < to && s.end > from; });
        // Sem invadir os blocos que ficaram antes e depois do trecho.
        i64 floor = 0, ceiling = 100000000;
        for (const auto& s : next) { if (s.end <= from) floor = std::max(floor, s.end); if (s.start >= to) ceiling = std::min(ceiling, s.start); }
        u64 newId = 1; for (const auto& s : next) newId = std::max(newId, s.id + 1);
        for (auto& s : made) {
            s.start = std::max(s.start, floor); s.end = std::min(s.end, ceiling);
            if (s.end <= s.start) continue;
            for (auto& w : s.words) { w.start = std::clamp(w.start, s.start, s.end - 1); w.end = std::clamp(w.end, w.start + 1, s.end); }
            s.id = newId++;
            next.push_back(std::move(s));
        }
    } else if (op == "merge") {
        if (selected.size() < 2) return false;
        auto first = std::find_if(next.begin(), next.end(), [&](const auto& s) { return selected.contains(s.id); });
        const u64 keep = first->id; bool gap = false;
        for (auto it = first+1; it != next.end(); ++it) {
            if (!selected.contains(it->id)) { gap = true; continue; }
            if (gap) return false;
            first->end = it->end; first->text = text::caption_join(first->text, it->text);
            first->words.insert(first->words.end(), it->words.begin(), it->words.end());
        }
        std::erase_if(next, [&](const auto& s) { return s.id != keep && selected.contains(s.id); });
    } else if (op == "style") {
        // Trocar o estilo não move nada: os blocos e os tempos ficam.
        i64 style; if (!number("style", style) || style < 0 || style >= text::kCaptionStyleCount) return false;
        nextOptions.style = static_cast<u32>(style);
    } else if (op == "preset") {
        // Um preset de legenda traz estilo E posição (o que o autor montou).
        const auto* preset = value.get("preset");
        if (!preset || !preset->is_string() || preset->string.size() > 256 * 1024) return false;
        presets::Preset p;
        if (!presets::parse(preset->string, p) || p.kind != presets::PresetKind::Caption) return false;
        nextOptions = p.caption;
        nextTransform.position = Vec3{static_cast<f32>(comp->width()) * .5f,
                                         static_cast<f32>(comp->height()) * std::clamp(p.caption.posY, 0.1f, 0.9f), 0};
    } else if (op == "look") {
        // 0.4.6: estilo só dos blocos escolhidos ("options" = JSON de
        // caption_options). Vale a aparência; o agrupamento é o da faixa.
        const auto* options = value.get("options");
        if (!options || !options->is_string()) return false;
        text::CaptionOptions parsed; bool removeFillers = false;
        if (!presets::parse_caption_options(options->string, parsed, removeFillers)) return false;
        if (!parsed.fontPath.empty() && parsed.fontPath.rfind("docs:", 0) != 0) parsed.fontPath = store_asset_path(parsed.fontPath);
        text::CaptionOptions look;
        text::copy_caption_appearance(parsed, look);
        for (auto& s : next) if (selected.contains(s.id)) s.look = look;
    } else if (op == "clear_look") {
        // Volta ao estilo da faixa.
        for (auto& s : next) if (selected.contains(s.id)) s.look.reset();
    } else if (op == "regroup") {
        // Reagrupa SÓ os blocos escolhidos com as opções atuais — é o
        // "regenerar este trecho" sem tocar no resto da fala.
        const auto* current = comp->layer(LayerId::unpack(layerId));
        if (!current) return false;
        const f64 fps = comp->fps() > 0.0 ? comp->fps() : 30.0;
        std::vector<text::CaptionWord> words;
        i64 from = 0, to = 0; bool first = true, gap = false;
        for (const auto& s : next) {
            if (!selected.contains(s.id)) { if (!first && s.start < to) gap = true; continue; }
            if (first) { from = s.start; to = s.end; first = false; }
            else if (s.start < to) { gap = true; } else { to = s.end; }
            for (const auto& w : s.words) words.push_back({w.text, static_cast<f64>(w.start) / fps, static_cast<f64>(w.end) / fps});
        }
        if (words.empty() || gap) return false;
        // Os blocos novos herdam o estilo próprio do primeiro escolhido.
        std::optional<text::CaptionOptions> look;
        for (const auto& s : next) if (selected.contains(s.id)) { look = s.look; break; }
        words = text::normalize_caption_words(words);
        const auto groups = text::group_captions(words, current->captionOptions);
        if (groups.empty()) return false;
        u64 newId = 1; for (const auto& s : next) newId = std::max(newId, s.id + 1);
        std::erase_if(next, [&](const auto& s) { return selected.contains(s.id); });
        for (const auto& g : groups) {
            text::CaptionSegment segment;
            segment.id = newId++;
            segment.start = std::clamp(static_cast<i64>(std::llround(g.start * fps)), from, to - 1);
            segment.end = std::clamp(static_cast<i64>(std::llround(g.end * fps)), segment.start + 1, to);
            for (u32 w = 0; w < g.count; ++w) {
                const auto& src = words[g.first + w];
                const i64 begin = std::clamp(static_cast<i64>(std::llround(src.start * fps)), segment.start, segment.end - 1);
                const i64 finish = std::clamp(static_cast<i64>(std::llround(src.end * fps)), begin + 1, segment.end);
                segment.words.push_back({src.text, begin, finish});
            }
            segment.text = g.text;
            segment.look = look;
            next.push_back(std::move(segment));
        }
    } else return false;
    std::sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    // 0.5.3: o intervalo entre legendas da faixa vale para os blocos refeitos.
    if (op == "regroup" || op == "from_words") text::apply_caption_gap(next, nextOptions.gapFrames);
    if (!text::valid_caption_track(next)) return false;
    if (op == "look" || op == "clear_look") {
        // A interface manda a cada toque: igual ao que já está = sem passo vazio.
        auto json_of = [](const text::CaptionSegment& s) { return s.look ? presets::caption_options_json(*s.look, true) : std::string{}; };
        bool changed = false;
        for (usize i = 0; i < next.size() && !changed; ++i) changed = json_of(next[i]) != json_of(layer->captions[i]);
        if (!changed) return true;
    }
    history_.before_mutation(*comp, project_->timeline().current(), op == "look" || op == "clear_look" ? "estilo do bloco" : "editar legendas");
    layer->captionOptions = nextOptions;
    layer->transform = nextTransform;
    layer->captions = std::move(next);
    if (layer->captions.empty()) comp->remove_layer(LayerId::unpack(layerId));
    else {
        // Estilo/preset mudam a aparência: o texto da camada é refeito com as
        // opções novas (as frases e os tempos continuam os mesmos).
        if (op == "style" || op == "preset")
            text::apply_caption_style(layer->captionOptions, std::min(comp->width(), comp->height()), layer->text, layer->tracks, {}, 0);
        layer->text.content = layer->captions.front().text;
        recenter_text(*layer);
    }
    bump_model_revision_locked(); project_->mark_dirty(); request_render();
    return true;
}
}
