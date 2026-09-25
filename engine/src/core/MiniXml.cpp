// =============================================================================
//  Aurea / core / MiniXml.cpp
//
//  O leitor que morava dentro do Svg.cpp, sem mudança de comportamento: o SVG
//  continua chamando com os padrões de sempre; o limite de profundidade só
//  vale para quem pede.
// =============================================================================
#include "aurea/core/MiniXml.hpp"

#include <cctype>

namespace aurea::xml {

const std::string* Node::attr(const char* n) const {
    for (const auto& a : attrs) if (a.first == n) return &a.second;
    return nullptr;
}

const std::string* Node::attr_nocase(std::string_view n) const {
    for (const auto& a : attrs) if (iequals(a.first, n)) return &a.second;
    return nullptr;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (usize i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

std::string decode_entities(const std::string& s) {
    if (s.find('&') == std::string::npos) return s;
    std::string o;
    for (usize i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            const usize e = s.find(';', i);
            if (e != std::string::npos && e - i < 8) {
                const std::string ent = s.substr(i + 1, e - i - 1);
                char c = 0;
                if (ent == "amp") c = '&'; else if (ent == "lt") c = '<'; else if (ent == "gt") c = '>';
                else if (ent == "quot") c = '"'; else if (ent == "apos") c = '\'';
                if (c) { o.push_back(c); i = e; continue; }
            }
        }
        o.push_back(s[i]);
    }
    return o;
}

/// Árvore XML mínima (tags, atributos; texto ignorado).
bool parse(const std::string& s, Node& root, u32 maxDepth) {
    std::vector<Node*> stack{&root};
    usize i = 0;
    const usize n = s.size();
    while (i < n) {
        const usize lt = s.find('<', i);
        if (lt == std::string::npos) break;
        i = lt;
        if (s.compare(i, 4, "<!--") == 0) { const usize e = s.find("-->", i + 4); i = e == std::string::npos ? n : e + 3; continue; }
        if (s.compare(i, 9, "<![CDATA[") == 0) { const usize e = s.find("]]>", i); i = e == std::string::npos ? n : e + 3; continue; }
        if (s.compare(i, 2, "<?") == 0) { const usize e = s.find("?>", i); i = e == std::string::npos ? n : e + 2; continue; }
        if (s.compare(i, 2, "<!") == 0) {
            // DOCTYPE (pode ter [ ... ] com '>' dentro).
            int depth = 0;
            for (++i; i < n; ++i) {
                if (s[i] == '[') ++depth; else if (s[i] == ']') --depth;
                else if (s[i] == '>' && depth <= 0) { ++i; break; }
            }
            continue;
        }
        if (s.compare(i, 2, "</") == 0) {
            const usize e = s.find('>', i);
            if (stack.size() > 1) stack.pop_back();
            i = e == std::string::npos ? n : e + 1;
            continue;
        }
        ++i;
        Node node;
        while (i < n && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') node.name.push_back(s[i++]);
        // Prefixo de namespace fora (svg:path → path).
        if (const usize c = node.name.find(':'); c != std::string::npos) node.name = node.name.substr(c + 1);
        bool selfClose = false;
        while (i < n) {
            while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            if (i >= n) break;
            if (s[i] == '>') { ++i; break; }
            if (s[i] == '/') { selfClose = true; ++i; continue; }
            std::string key;
            while (i < n && s[i] != '=' && !std::isspace(static_cast<unsigned char>(s[i])) && s[i] != '>' && s[i] != '/') key.push_back(s[i++]);
            while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            std::string val;
            if (i < n && s[i] == '=') {
                ++i;
                while (i < n && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
                if (i < n && (s[i] == '"' || s[i] == '\'')) {
                    const char q = s[i++];
                    const usize e = s.find(q, i);
                    val = s.substr(i, (e == std::string::npos ? n : e) - i);
                    i = e == std::string::npos ? n : e + 1;
                }
            }
            if (!key.empty()) node.attrs.emplace_back(key, decode_entities(val));
        }
        Node* parent = stack.back();
        parent->kids.push_back(std::move(node));
        if (!selfClose) {
            stack.push_back(&parent->kids.back());
            if (maxDepth > 0 && stack.size() - 1 > maxDepth) return false;
        }
    }
    return !root.kids.empty();
}

} // namespace aurea::xml
