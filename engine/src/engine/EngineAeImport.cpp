// =============================================================================
//  Aurea / engine / EngineAeImport.cpp
//
//  Engine::import_ae_file / import_ae_data: o .ffx ou o .jsx do After Effects
//  aplicado no projeto (project/AeImport.hpp). Tudo acontece sob a trava do
//  modelo e vira UM passo de desfazer: a composição é copiada antes, a
//  importação mexe na de verdade e, se algo mudou, a cópia vai para o
//  histórico como o "antes" (nada mudou = nenhum passo vazio).
//
//  O `Host` daqui cria camadas e efeitos pelas mesmas regras das ações da UI
//  (EffectAdd pelo apply_command_internal; camadas com os padrões de
//  add_text/add_null/add_camera/add_light), sem abrir passos próprios.
// =============================================================================
#include "aurea/Engine.hpp"

#include "aurea/core/Log.hpp"
#include "aurea/scene3d/SceneRenderer.hpp"
#include "aurea/text/Text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace aurea {

namespace {
constexpr usize kMaxAeFile = 64u << 20;

std::string unique_name(const Composition& comp, const std::string& base) {
    for (u32 n = 1;; ++n) {
        const std::string candidate = n == 1 ? base : base + " " + std::to_string(n);
        bool used = false;
        for (u32 i = 0; i < comp.order().size() && !used; ++i) {
            const Layer* l = comp.layer(comp.order().at(i));
            used = l && l->name == candidate;
        }
        if (!used || n > 9999) return candidate;
    }
}

void append_utf8(std::string& o, u32 cp) {
    if (cp < 0x80) {
        o.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        o.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        o.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        o.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        o.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

bool valid_utf8(const u8* p, usize n) noexcept {
    usize i = 0;
    while (i < n) {
        const u8 c = p[i];
        if (c < 0x80) { ++i; continue; }
        const usize len = (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > n || (len == 2 && c < 0xC2)) return false;
        for (usize k = 1; k < len; ++k) if ((p[i + k] & 0xC0) != 0x80) return false;
        i += len;
    }
    return true;
}

/// Texto do .jsx em UTF-8 (o léxico só lê UTF-8). Converte UTF-16 (BOM FF FE /
/// FE FF, ou sem BOM com o zero a cada dois bytes do ASCII) e o Latin-1 /
/// Windows-1252 dos scripts antigos do Windows (UTF-8 inválido). Falso = já era
/// UTF-8 (ou ASCII): usa o original sem cópia.
bool text_to_utf8(const std::string& d, std::string& out) {
    const auto* p = reinterpret_cast<const u8*>(d.data());
    const usize n = d.size();
    int utf16 = 0;   // 1 = little-endian, 2 = big-endian
    usize start = 0;
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) { utf16 = 1; start = 2; }
    else if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) { utf16 = 2; start = 2; }
    else if (n >= 4) {
        const usize m = std::min<usize>(n & ~static_cast<usize>(1), 1024);
        usize evenZero = 0, oddZero = 0;
        for (usize i = 0; i + 1 < m; i += 2) { evenZero += p[i] == 0 ? 1u : 0u; oddZero += p[i + 1] == 0 ? 1u : 0u; }
        const usize pairs = m / 2;
        if (oddZero * 10 >= pairs * 6 && evenZero * 10 <= pairs) utf16 = 1;
        else if (evenZero * 10 >= pairs * 6 && oddZero * 10 <= pairs) utf16 = 2;
    }
    if (utf16) {
        out.clear();
        out.reserve(n);
        auto unit = [&](usize i) -> u32 {
            return utf16 == 1 ? static_cast<u32>(p[i] | (p[i + 1] << 8)) : static_cast<u32>((p[i] << 8) | p[i + 1]);
        };
        for (usize i = start; i + 1 < n; i += 2) {
            u32 cp = unit(i);
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < n) {
                const u32 lo = unit(i + 2);
                if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 2; }
                else cp = 0xFFFD;
            } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
            append_utf8(out, cp);
        }
        return true;
    }
    if (valid_utf8(p, n)) return false;
    // Windows-1252 (o Latin-1 com aspas, travessões e € nos 0x80–0x9F).
    static constexpr u16 k1252[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
                                      0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};
    out.clear();
    out.reserve(n + n / 8);
    for (usize i = 0; i < n; ++i) {
        const u8 c = p[i];
        append_utf8(out, c >= 0x80 && c < 0xA0 ? k1252[c - 0x80] : c);
    }
    return true;
}

/// "pasta/Desfoque leve.ffx" → "Desfoque leve".
std::string file_stem(const std::string& path) {
    const usize slash = path.find_last_of("/\\");
    std::string s = slash == std::string::npos ? path : path.substr(slash + 1);
    const usize dot = s.find_last_of('.');
    if (dot != std::string::npos && dot > 0) s.resize(dot);
    return s;
}
} // namespace

ae::ImportReport Engine::import_ae_file(const char* path, u64 targetLayer) noexcept {
    ae::ImportReport r;
    if (!path || !*path) {
        r.message = "Nenhum arquivo escolhido.";
        return r;
    }
    std::FILE* f = std::fopen(path, "rb");
    if (!f) {
        r.message = "Não foi possível abrir o arquivo.";
        return r;
    }
    std::string data;
    char buf[64 * 1024];
    usize n = 0;
    bool tooBig = false;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        if (data.size() + n > kMaxAeFile) { tooBig = true; break; }
        data.append(buf, n);
    }
    std::fclose(f);
    if (tooBig) {
        r.message = "Arquivo grande demais para importar.";
        return r;
    }
    return import_ae_data(data, path, targetLayer);
}

ae::ImportReport Engine::import_ae_data(const std::string& raw, const std::string& fileName, u64 targetLayer) noexcept {
    ae::ImportReport report;
    // Script salvo em UTF-16 ou Latin-1 vira UTF-8 antes de tudo (o .ffx é
    // binário RIFX e passa direto).
    std::string converted;
    const bool rifx = raw.size() >= 4 && raw.compare(0, 4, "RIFX") == 0;
    const std::string& data = !rifx && text_to_utf8(raw, converted) ? converted : raw;
    const std::string kind = ae::detect_kind(data, fileName);
    report.kind = kind;
    if (kind == "jsxbin") {
        report.message = "Script compilado (.jsxbin) não pode ser lido: use o .jsx original.";
        report.skip("arquivo", fileName, "formato binário do ExtendScript");
        return report;
    }
    if (kind.empty()) {
        report.message = "O arquivo não é um preset (.ffx) nem um script (.jsx) do After Effects.";
        return report;
    }
    // O .ffx é lido FORA da trava (só bytes); a aplicação vem depois.
    ae::FfxDocument doc;
    if (kind == "ffx") {
        std::string err;
        if (!ae::parse_ffx(data, doc, err)) {
            report.message = "Preset do After Effects ilegível: " + err + ".";
            return report;
        }
    }

    std::lock_guard<std::mutex> lock(modelMutex_);
    drain_commands_locked();
    Composition* comp = project_ ? current_composition() : nullptr;
    if (!comp) {
        report.message = "Nenhum projeto aberto.";
        return report;
    }
    const CompositionId compId = project_->timeline().current();
    const i64 durationBefore = comp->duration().value;
    std::unique_ptr<Composition> before = comp->clone();

    // O motor visto pelo importador (as regras das ações da UI, sem passo próprio).
    class EngineHost final : public ae::Host {
    public:
        EngineHost(Engine& e, Composition* c) : e_(e), comp_(c) {}
        Composition* composition() noexcept override { return comp_; }
        const EffectRegistry& registry() const noexcept override { return e_.effectRegistry_; }
        i64 playhead() const noexcept override { return e_.playback_.current().value; }
        u32 mutations = 0;

        LayerId create_layer(ae::NewLayer kind, const ae::NewLayerSpec& s) noexcept override {
            if (comp_->order().size() >= kMaxLayerCount) return LayerId{};
            const f32 cw = static_cast<f32>(comp_->width()), ch = static_cast<f32>(comp_->height());
            const Vec3 center{cw * 0.5f, ch * 0.5f, 0.0f};
            LayerId id{};
            switch (kind) {
                case ae::NewLayer::Text: {
                    if (!text::default_font()) return LayerId{};
                    id = comp_->add_layer(LayerKind::Text, s.name.empty() ? unique_name(*comp_, "Texto") : s.name);
                    Layer* l = comp_->layer(id);
                    if (!l) return LayerId{};
                    l->text.content = s.text.empty() ? " " : s.text;
                    l->text.size = std::round(0.12f * std::min(cw, ch));
                    l->text.color = Vec4{1, 1, 1, 1};
                    l->text.alignment = 1;
                    l->transform.position = center;
                    break;
                }
                case ae::NewLayer::Solid:
                case ae::NewLayer::Shape: {
                    const bool solid = kind == ae::NewLayer::Solid;
                    id = comp_->add_layer(LayerKind::Shape, s.name.empty() ? unique_name(*comp_, solid ? "Sólido" : "Forma") : s.name);
                    Layer* l = comp_->layer(id);
                    if (!l) return LayerId{};
                    const f32 w = s.width ? static_cast<f32>(s.width) : cw, h = s.height ? static_cast<f32>(s.height) : ch;
                    l->shape.shapeType = 0;
                    l->shape.bounds = Rect{0.0f, 0.0f, std::max(1.0f, w), std::max(1.0f, h)};
                    l->shape.fillColor = Vec4{s.color[0], s.color[1], s.color[2], s.color[3]};
                    l->shape.filled = solid;
                    l->transform.anchor = Vec3{l->shape.bounds.w * 0.5f, l->shape.bounds.h * 0.5f, 0.0f};
                    l->transform.position = center;
                    break;
                }
                case ae::NewLayer::Null: {
                    id = comp_->add_layer(LayerKind::Null, s.name.empty() ? unique_name(*comp_, "Nulo") : s.name);
                    Layer* l = comp_->layer(id);
                    if (!l) return LayerId{};
                    l->threeD = s.threeD;
                    l->transform.anchor = Vec3{50.0f, 50.0f, 0.0f};
                    l->transform.position = center;
                    break;
                }
                case ae::NewLayer::Camera: {
                    id = comp_->add_layer(LayerKind::Camera, s.name.empty() ? unique_name(*comp_, "Camera 3D") : s.name);
                    Layer* l = comp_->layer(id);
                    if (!l) return LayerId{};
                    const auto fallback = scene3d::default_camera(comp_->width(), comp_->height());
                    l->threeD = true;
                    l->camera.affectsLayersBelow = false;   // no AE a câmera só vê camadas 3D
                    l->transform.position = fallback.position;
                    l->transform.anchor = Vec3{0, 0, 0};
                    l->camera.fov = fallback.fovY / kDeg2Rad;
                    l->camera.focalLength = camera_focal_from_fov(l->camera.fov);
                    l->camera.focusDistance = std::max(1.0f, -fallback.position.z);
                    l->camera.nearPlane = fallback.nearZ;
                    // Como no AE: vale a câmera visível mais de cima (0.3.0).
                    l->camera.active = true;
                    break;
                }
                case ae::NewLayer::Light: {
                    const u32 lk = std::min<u32>(s.lightKind, 2);
                    id = comp_->add_layer(LayerKind::Light, s.name.empty() ? unique_name(*comp_, lk == 0 ? "Luz direcional" : lk == 1 ? "Luz pontual" : "Spot") : s.name);
                    Layer* l = comp_->layer(id);
                    if (!l) return LayerId{};
                    l->threeD = true;
                    l->light.kind = static_cast<LightKind>(lk);
                    l->light.intensity = lk == 0 ? 3.0f : 0.5f;
                    l->light.range = ch * 5.0f;
                    l->light.castShadows = lk == 0;
                    l->transform.position = lk == 0 ? Vec3{cw * 0.5f, ch * 0.25f, -ch} : Vec3{cw * 0.5f, ch * 0.5f, -0.25f * ch};
                    break;
                }
            }
            Layer* l = comp_->layer(id);
            if (!l) return LayerId{};
            // No AE as camadas novas ocupam a composição inteira a partir do 0.
            const f64 fps = comp_->fps() > 0 ? comp_->fps() : 30.0;
            const i64 dur = s.durationSec > 0 ? std::max<i64>(1, static_cast<i64>(std::llround(s.durationSec * fps)))
                                              : comp_->duration().value;
            l->start = FrameIndex{0};
            l->end = FrameIndex{std::max<i64>(1, dur)};
            if (l->end.value > comp_->duration().value) comp_->set_duration(l->end);
            if (kind == ae::NewLayer::Text) e_.recenter_text(*l);
            ++mutations;
            return id;
        }

        u32 add_effect(LayerId layer, u32 type) noexcept override {
            Command c;
            c.type = CommandType::EffectAdd;
            c.effect_add.layer = layer;
            c.effect_add.effectType = type;
            c.effect_add.index = kInvalidIndex;
            if (!e_.apply_command_internal(c, nullptr, false).ok()) return kInvalidIndex;
            const Layer* l = comp_->layer(layer);
            if (!l || l->effects.empty()) return kInvalidIndex;
            ++mutations;
            return l->effects.back().id;
        }

        void text_changed(LayerId layer) noexcept override {
            if (Layer* l = comp_->layer(layer)) e_.recenter_text(*l);
        }

        bool remove_layer(LayerId layer) noexcept override {
            const bool ok = comp_->remove_layer(layer);
            if (ok) ++mutations;
            return ok;
        }

        bool set_parent(LayerId child, LayerId parent, bool keepWorld) noexcept override {
            Layer* l = comp_->layer(child);
            if (!l || child == parent) return false;
            if (keepWorld) {
                // A mesma compensação do "vincular" da UI (LayerSetParent).
                Command c;
                c.type = CommandType::LayerSetParent;
                c.layer_parent.layer = child;
                c.layer_parent.parent = parent;
                if (!e_.apply_command_internal(c, nullptr, false).ok()) return false;
            } else {
                l->parent = parent;
            }
            ++mutations;
            return true;
        }

    private:
        Engine& e_;
        Composition* comp_;
    };

    EngineHost host(*this, comp);
    LayerId target = targetLayer ? LayerId::unpack(targetLayer) : LayerId{};
    if (kind == "ffx") {
        // Sem camada escolhida (ou ela não existe mais): como o AE, o preset
        // entra numa camada NOVA — de texto se ele mexe em texto, senão um
        // sólido do tamanho da composição com o nome do preset.
        LayerId created{};
        bool textLayer = false;
        if (!comp->layer(target)) {
            for (const ae::Entry& e : doc.entries)
                textLayer = textLayer || (!e.path.empty() && e.path[0].match == "ADBE Text Properties");
            ae::NewLayerSpec spec;
            if (textLayer) {
                spec.text = "Texto";
            } else {
                const std::string stem = file_stem(fileName);
                if (!stem.empty()) spec.name = unique_name(*comp, stem);
            }
            created = host.create_layer(textLayer ? ae::NewLayer::Text : ae::NewLayer::Solid, spec);
            if (!created.valid()) {
                report.message = textLayer ? "Não foi possível criar a camada de texto para o preset do After Effects."
                                           : "Não foi possível criar a camada para o preset do After Effects.";
                ae::finish_report(report);
                return report;
            }
            target = created;
        }
        (void)ae::apply_ffx(doc, host, target, report);
        if (created.valid()) {
            if (!report.ok) {
                // Nada do preset entrou: a camada nova sai e o projeto fica
                // como estava (sem passo de desfazer vazio). Tudo o que o preset
                // fez foi nessa camada.
                (void)comp->remove_layer(created);
                host.mutations = 0;
                report.changes = 0;
            } else {
                ++report.layers;
                ++report.changes;
            }
        }
    } else {
        ae::ScriptLimits limits;
        (void)ae::run_jsx(data, host, target, limits, report);
    }
    const bool changed = report.changes > 0 || host.mutations > 0;
    if (changed) {
        // O "antes" é a cópia de antes da importação: um passo só desfaz tudo.
        history_.before_mutation(*before, compId, kind == "jsx" ? "script do After Effects" : "preset do After Effects");
        bump_model_revision_locked();
        if (comp->duration().value != durationBefore) configure_playback_locked(comp);
        project_->mark_dirty();
        request_render();
    }
    ae::finish_report(report);
    AUREA_LOG_INFO("importar do AE (%s): %s", kind.c_str(), report.message.c_str());
    return report;
}

} // namespace aurea
