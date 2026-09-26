#include "aurea/Engine.hpp"
#include "aurea/project/Presets.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace aurea {
std::string Engine::caption_tracks() noexcept {
    std::lock_guard lock(modelMutex_);
    auto* comp = project_ ? current_composition() : nullptr;
    json::Writer w; w.begin_array();
    if (comp) for (u32 i = 0; i < comp->order().size(); ++i) {
        const auto id = comp->order().at(i); const auto* l = comp->layer(id);
        if (!l || l->captions.empty()) continue;
        w.begin_object().key("layer").value(static_cast<i64>(id.pack())).key("source").value(static_cast<i64>(l->text.captionSource));
        w.key("segments").begin_array();
        for (const auto& s : l->captions) {
            w.begin_object().key("id").value(static_cast<i64>(s.id)).key("start").value(s.start).key("end").value(s.end).key("text").value(s.text);
            w.key("words").begin_array();
            for (const auto& word : s.words) w.begin_object().key("text").value(word.text).key("start").value(word.start).key("end").value(word.end).end_object();
            w.end_array().end_object();
        }
        w.end_array().end_object();
    }
    return w.end_array().str();
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
    if (!operation || !operation->is_string() || !ids || !ids->is_array() || ids->array.empty()) return false;
    std::set<u64> selected;
    for (const auto& id : ids->array) {
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
        i64 frame; if (selected.size() != 1 || !number("frame", frame)) return false;
        u64 newId = 1; for (const auto& s : next) newId = std::max(newId, s.id+1);
        auto it = std::find_if(next.begin(), next.end(), [&](const auto& s) { return selected.contains(s.id); });
        if (frame <= it->start || frame >= it->end) return false;
        auto right = *it; right.id = newId; right.start = frame; it->end = frame;
        // A palavra vai para o lado onde ela COMEÇA. Particionar por fim
        // duplicaria a palavra que atravessa o corte (ela apareceria nos dois
        // blocos, e o texto sairia repetido na tela).
        std::erase_if(it->words, [&](auto& word) { if (word.start >= frame) return true; word.end = std::min(word.end,frame); return false; });
        std::erase_if(right.words, [&](auto& word) { if (word.start < frame) return true; return false; });
        auto join = [](auto& segment) { segment.text.clear(); for (const auto& word : segment.words) segment.text += (segment.text.empty() ? "" : " ") + word.text; };
        join(*it); join(right); next.push_back(std::move(right));
    } else if (op == "merge") {
        if (selected.size() < 2) return false;
        auto first = std::find_if(next.begin(), next.end(), [&](const auto& s) { return selected.contains(s.id); });
        const u64 keep = first->id; bool gap = false;
        for (auto it = first+1; it != next.end(); ++it) {
            if (!selected.contains(it->id)) { gap = true; continue; }
            if (gap) return false;
            first->end = it->end; first->text += " " + it->text;
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
            next.push_back(std::move(segment));
        }
    } else return false;
    std::sort(next.begin(), next.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    if (!text::valid_caption_track(next)) return false;
    history_.before_mutation(*comp, project_->timeline().current(), "editar legendas");
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
    modelRevision_.fetch_add(1, std::memory_order_acq_rel); project_->mark_dirty(); request_render();
    return true;
}
}
