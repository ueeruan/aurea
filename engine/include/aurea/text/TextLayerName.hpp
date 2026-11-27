#pragma once

#include "aurea/DefaultNames.hpp"
#include "aurea/text/Text.hpp"
#include "aurea/timeline/Layer.hpp"

#include <string_view>

namespace aurea::text {

// Existing projects store the label as text. Recognize the old generated
// names as well as names already following the source or one of its keys.
inline bool follows_source_name(const Layer& layer) {
    if (layer.kind != LayerKind::Text || !layer.captions.empty()) return false;
    if (layer.name == layer.text.content || layer.name.empty()) return true;
    for (const TextKey& key : layer.text.sourceKeys) if (layer.name == key.content) return true;

    const auto generated = [&](std::string_view noun) {
        std::string_view label = layer.name;
        if (!label.starts_with(noun)) return false;
        label.remove_prefix(noun.size());
        while (!label.empty()) {
            if (label.starts_with(" copia")) label.remove_prefix(6);
            else if (label.starts_with(" copy")) label.remove_prefix(5);
            else if (label.front() == ' ' && label.size() > 1 && label[1] >= '0' && label[1] <= '9') {
                label.remove_prefix(1);
                while (!label.empty() && label.front() >= '0' && label.front() <= '9') label.remove_prefix(1);
            } else return false;
        }
        return true;
    };
    return generated("Text") || generated("Texto") || generated("Текст") || generated("टेक्स्ट") ||
           generated("نص") || generated("Teks") || generated(default_name(DefaultName::Text));
}

inline std::string source_layer_name(std::string_view source) {
    return source.find_first_not_of(" \r\n\t") == std::string_view::npos
        ? default_name(DefaultName::Text) : std::string(source);
}

inline std::string text_name_at(const Layer& layer, FrameIndex at) {
    const i32 key = source_key_index(layer.text, layer.local_time(at).value);
    return source_layer_name(key < 0 ? layer.text.content : layer.text.sourceKeys[static_cast<usize>(key)].content);
}

} // namespace aurea::text
