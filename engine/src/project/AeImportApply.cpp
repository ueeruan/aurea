// =============================================================================
//  Aurea / project / AeImportApply.cpp
//
//  Preset .ffx lido (AeImportFfx.cpp) → edições na camada de destino. Cada
//  entrada do preset tem o caminho até o grupo (efeitos, texto, transform,
//  máscaras) e a árvore de valores; os endereços e as conversões são os de
//  AeImportBind/AeImportTable, os mesmos do .jsx.
//
//  Ordem: tudo o que não é expressão primeiro; as expressões no FIM, já com
//  effect("Nome")(param) reescrito para os efeitos criados (o preset costuma
//  trazer o controle depois da propriedade que lê dele).
//
//  Texto: cada animador do preset vira um animador NOVO no fim da pilha (como
//  o AE faz ao aplicar o preset). O Aurea tem UM seletor por animador: com
//  intervalo cheio e parado + oscilador, vale o oscilador (é exato); com mais
//  de um seletor de verdade, o primeiro vale e os outros são relatados.
// =============================================================================
#include "AeImportBind.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/timeline/Composition.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>
#include <span>

namespace aurea::ae {
using namespace detail;

namespace {

bool starts(std::string_view s, std::string_view p) noexcept { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }

u16 suffix(std::string_view m, std::string_view effect) noexcept {
    if (m.size() != effect.size() + 5 || !starts(m, effect) || m[effect.size()] != '-') return 0;
    u32 v = 0;
    for (usize i = effect.size() + 1; i < m.size(); ++i) {
        if (m[i] < '0' || m[i] > '9') return 0;
        v = v * 10 + static_cast<u32>(m[i] - '0');
    }
    return static_cast<u16>(std::min<u32>(v, 9999));
}

bool animated(const Node& n) noexcept { return n.keys.size() > 1 || !n.expression.empty(); }

/// Propriedade de animador de texto sem equivalente parada no padrão do AE: não
/// muda nada no resultado, então não entra no relatório. Os presets antigos
/// gravam o conjunto inteiro (âncora 0, opacidade do contorno 100, matiz 0...).
bool at_ae_default(const Node& n) noexcept {
    if (animated(n) || !n.hasValue || n.value.empty()) return false;
    f64 def = NAN;
    const std::string& m = n.match;
    if (m == "ADBE Text Anchor Point 3D" || m == "ADBE Text Anchor Point" || m == "ADBE Text Line Spacing" ||
        m == "ADBE Text Fill Hue" || m == "ADBE Text Fill Saturation" || m == "ADBE Text Fill Brightness" ||
        m == "ADBE Text Stroke Hue" || m == "ADBE Text Stroke Saturation" || m == "ADBE Text Stroke Brightness")
        def = 0.0;
    else if (m == "ADBE Text Fill Opacity" || m == "ADBE Text Stroke Opacity")
        def = 100.0;
    if (std::isnan(def)) return false;
    return std::all_of(n.value.begin(), n.value.end(), [def](f64 x) { return std::fabs(x - def) < 1e-6; });
}

void min_key_time(const Node& n, f64& t) noexcept {
    for (const Key& k : n.keys) t = std::min(t, k.time);
    for (const Node& c : n.children) min_key_time(c, t);
}

struct Applier {
    Host& host;
    Composition* comp = nullptr;
    LayerId lid;
    ImportReport& rep;
    WriteCtx w;
    f64 layerW = 1, layerH = 1;
    struct PendingExpr { Binding b; std::string src; };
    std::vector<PendingExpr> exprs;
    std::vector<EffectAlias> aliases;
    std::vector<u32> maskIds;
    std::vector<u32> newAnimators;
    std::vector<std::pair<i32, u32>> partialAnimators;   ///< índice do AE → animador novo
    std::deque<ERule> synthRules;          ///< controles de expressão sintetizados (vivem até o fim)
    std::deque<PRule> synthParams;
    bool textSkipped = false;
    bool pivotAnimated = false;     ///< algum animador gira/escala/inclina caracteres
    bool alignNearCenter = false;   ///< Grouping Alignment do preset perto do centro
    bool alignSkipped = false;      ///< Grouping Alignment fora do centro (já relatado)
    u32 applied = 0;

    Applier(Host& h, LayerId id, ImportReport& r) : host(h), lid(id), rep(r) {}

    Layer* layer() noexcept { return comp ? comp->layer(lid) : nullptr; }
    void refresh() noexcept { w.layer = layer(); }

    // --- propriedade ----------------------------------------------------------
    void apply_prop(const Binding& b, const Node& n, const char* kind = "propriedade") {
        if (!b.ok()) {
            if (starts(b.unsupported, "~") || at_ae_default(n)) return;
            rep.skip(kind,n.name.empty() ? b.label : n.name + " (" + b.label + ")", b.unsupported);
            return;
        }
        if (n.keys.size() > 1) write_keys(w, b, n.keys, true);
        else if (n.hasValue) write_static(w, b, n.value, true);
        if (!n.expression.empty()) exprs.push_back(PendingExpr{b, n.expression});
        ++applied;
    }

    // --- efeitos --------------------------------------------------------------
    /// Efeito de controles do AE ("Pseudo/…" dos presets e "ADBE CM …" dos
    /// Behaviors): cada parâmetro vira um controle de expressão do Aurea
    /// (deslizante, ângulo, caixa, cor, ponto), com valor, keys e expressão. As
    /// expressões que citam effect("Nome")("Parâmetro") vão para o controle certo.
    /// Parâmetros de outro tipo (camada, caminho, dado próprio) entram em `missing`.
    std::vector<const ERule*> pseudo_controls(const Node& n, std::vector<std::string>& missing) {
        std::vector<const ERule*> out;
        if (!(starts(n.match, "Pseudo/") || starts(n.match, "ADBE CM "))) return out;
        for (const EffectParamDef& d : n.params) {
            const u16 num = suffix(d.match, n.match);
            if (num == 0 || d.type == 13 || d.type == 14 || d.type == 9 || d.type == 15) continue;
            const char* key = nullptr;
            PConv conv = PConv::Same;
            const char* id = nullptr;
            switch (d.type) {
                case 1: case 2: case 7: case 10: key = effect_keys::kSliderControl; id = "slider"; break;
                case 3: key = effect_keys::kAngleControl; id = "angle"; break;
                case 4: key = effect_keys::kCheckboxControl; id = "checkbox"; conv = PConv::Bool; break;
                case 5: key = effect_keys::kColorControl; id = "color"; conv = PConv::Color; break;
                case 6: key = effect_keys::kPointControl; id = "point"; conv = PConv::PointPx; break;
                default: break;
            }
            if (!key) {
                // Só relata quando o preset guarda algo nele (camada escolhida, keys...).
                for (const Node& c : n.children) {
                    if (c.kind != Node::Kind::Property || c.match != d.match) continue;
                    const bool used = animated(c) || (c.hasValue && !c.value.empty() && std::fabs(c.value[0]) > 1e-9);
                    if (used) missing.push_back(d.name.empty() ? d.match : d.name);
                }
                continue;
            }
            PRule p;
            p.ae = num;
            p.aeName = d.name.c_str();
            p.id = id;
            p.conv = conv;
            synthParams.push_back(p);
            ERule r;
            r.ae = n.match.c_str();
            r.aeName = n.effectName.c_str();
            r.key = key;
            r.params = &synthParams.back();
            r.paramCount = 1;
            synthRules.push_back(r);
            out.push_back(&synthRules.back());
        }
        return out;
    }

    void effect(const Node& n) {
        if (n.kind != Node::Kind::Effect) return;
        const std::string shown = !n.effectName.empty() ? n.effectName : !n.name.empty() ? n.name : n.match;
        const ERule* rule = find_effect_rule(n.match);
        if (!rule) {
            std::vector<std::string> missing;
            const std::vector<const ERule*> controls = pseudo_controls(n, missing);
            if (controls.empty()) {
                rep.skip("efeito", shown + " (" + n.match + ")", "efeito sem equivalente no Aurea");
                return;
            }
            bool menu = false;
            for (const ERule* c : controls) {
                apply_effect(n, shown, c, static_cast<i32>(c->params[0].ae));
                for (const EffectParamDef& d : n.params)
                    if (suffix(d.match, n.match) == c->params[0].ae && d.type == 7) menu = true;
            }
            if (!missing.empty()) {
                std::string names;
                for (const std::string& s : missing) names += (names.empty() ? "" : ", ") + s;
                rep.skip("parametro", shown + ": " + names, "parâmetro sem equivalente no Aurea (o resto do efeito entrou)");
            }
            rep.note("efeito", shown,
                     controls.size() == 1
                         ? (menu ? "menu suspenso virou controle deslizante (o valor é o número da opção)"
                                 : "controle próprio do AE virou o controle de expressão do Aurea")
                         : "os " + std::to_string(controls.size()) +
                               " parâmetros viraram controles de expressão do Aurea (um por parâmetro)" +
                               (menu ? "; menus viram o número da opção" : ""));
            return;
        }
        if (!rule->key) {
            rep.skip("efeito", shown, rule->reason ? rule->reason : "efeito sem equivalente no Aurea");
            return;
        }
        apply_effect(n, shown, rule, -1);
    }

    /// Cria o efeito do Aurea da regra e aplica os parâmetros do nó. `onlyAe`
    /// ≥ 0: só esse parâmetro do AE (controles sintetizados, um por parâmetro).
    void apply_effect(const Node& n, const std::string& shown, const ERule* rule, i32 onlyAe) {
        const EffectTypeId type = effect_type_id(rule->key);
        const u32 id = host.add_effect(lid, type);
        refresh();
        Layer* l = w.layer;
        EffectInstance* fx = l ? l->find_effect(EffectId{id, 0}) : nullptr;
        const ParameterRegistry* specs = host.registry().params(type);
        if (id == kInvalidIndex || !fx || !specs) {
            rep.skip("efeito", shown, "o Aurea recusou este efeito nesta camada");
            return;
        }
        fx->enabled = (n.flags & 1u) != 0 || n.flags == 0;
        for (u32 i = 0; i < rule->fixedCount; ++i) {
            const u32 p = specs->find(rule->fixed[i].id);
            if (p == kInvalidIndex || p >= fx->params.size()) continue;
            for (u32 c = 0; c < 4; ++c) fx->params[p].constant.v[c] = rule->fixed[i].v[c];
        }
        PostIn in;
        in.layerW = layerW;
        in.layerH = layerH;
        in.fps = w.fps;
        std::vector<std::string> missing;
        for (const Node& c : n.children) {
            if (c.kind != Node::Kind::Property) continue;
            const u16 ae = suffix(c.match, n.match);
            if (ae == 0 || (onlyAe >= 0 && ae != onlyAe)) continue;
            in.values.push_back(PostIn::V{ae, c.hasValue ? c.value : std::vector<f64>{}, animated(c)});
            const Binding b = bind_effect_param(*rule, id, ae, std::max<u32>(1, c.dims), host.registry(), layerW, layerH, w.fps);
            if (!b.ok()) {
                if (b.unsupported == "~") continue;
                // Marcadores de grupo da interface (13 início, 14 fim) e o bloco
                // embutido (9) guardam um valor fictício: nunca são parâmetro.
                const EffectParamDef* def = nullptr;
                for (const EffectParamDef& d : n.params) if (d.match == c.match) def = &d;
                if (def && (def->type == 13 || def->type == 14 || def->type == 9)) continue;
                // Sem equivalente: só relata se o valor sai do padrão do AE.
                bool report = animated(c);
                bool known = false;
                for (u32 i = 0; i < rule->paramCount; ++i) {
                    const PRule& p = rule->params[i];
                    if (p.ae != ae) continue;
                    known = true;
                    if (p.hasDef && c.hasValue && !c.value.empty() && std::fabs(c.value[0] - p.def) > 1e-4) report = true;
                }
                if (!known) report = report || (c.hasValue && !c.value.empty() && c.dims <= 4);
                if (report) {
                    // Caixas de seleção guardam o rótulo no menu (pdnm), não no nome.
                    std::string pn = def ? def->name : std::string();
                    if (pn.empty() && def && !def->popup.empty()) pn = def->popup.front();
                    missing.push_back(pn.empty() ? c.match : pn);
                }
                continue;
            }
            refresh();
            apply_prop(b, c, "parametro");
        }
        refresh();
        fx = w.layer ? w.layer->find_effect(EffectId{id, 0}) : nullptr;
        if (fx && rule->post) rule->post(*fx, *specs, in, rep);
        if (!missing.empty()) {
            std::string names;
            for (const std::string& s : missing) names += (names.empty() ? "" : ", ") + s;
            rep.skip("parametro", shown + ": " + names, "parâmetro sem equivalente no Aurea (o resto do efeito entrou)");
        }
        if (rule->reason) rep.note("efeito", shown, rule->reason);
        // Apelidos para as expressões: instância, nome do efeito e matchName.
        u32 ordinal = 0;
        for (const EffectInstance& e : w.layer->effects) {
            if (e.type == type) ++ordinal;
            if (e.id == id) break;
        }
        const std::string ref = ordinal <= 1 ? std::string(rule->key) : std::string(rule->key) + " " + std::to_string(ordinal);
        // Posições de effect("X")(N): a lista do preset na ordem da interface,
        // sem a entrada (-0000) e o bloco embutido, com os marcadores de grupo.
        std::vector<u16> order;
        for (const EffectParamDef& d : n.params)
            if (const u16 num = suffix(d.match, n.match); num > 0) order.push_back(num);
        for (const std::string* nm : {&n.name, &n.effectName, &n.match})
            if (!nm->empty()) aliases.push_back(EffectAlias{*nm, ref, rule, order});
        aliases.push_back(EffectAlias{rule->aeName, ref, rule, order});
        ++rep.effects;
        ++applied;
    }

    // --- transform ------------------------------------------------------------
    void transform(const Node& n) {
        if (n.kind == Node::Kind::Group) {
            for (const Node& c : n.children) transform(c);
            return;
        }
        if (n.kind != Node::Kind::Property || n.hidden()) return;
        apply_prop(bind_transform(*w.layer, n.match), n);
    }

    // --- máscaras -------------------------------------------------------------
    void mask(const Node& n) {
        if (n.kind != Node::Kind::Group) return;
        Layer& l = *w.layer;
        if (l.masks.size() >= kMaxMaskCount) {
            rep.skip("mascara", n.name, "a camada já tem o máximo de máscaras");
            return;
        }
        Mask m;
        m.id = l.alloc_mask_id();
        m.name = n.name.empty() ? "Máscara " + std::to_string(l.masks.size() + 1) : n.name;
        // Pontos do preset: relativos ao centro da camada (observado).
        const f64 cx = l.kind == LayerKind::Text ? l.transform.anchor.x : 0.5 * layerW;
        const f64 cy = l.kind == LayerKind::Text ? l.transform.anchor.y : 0.5 * layerH;
        const Node* shape = n.child("ADBE Mask Shape");
        if (shape && shape->hasShape) {
            m.closed = shape->shape.closed;
            for (const ShapeVertex& v : shape->shape.vertices) {
                MaskPoint p;
                p.position = Vec2{static_cast<f32>(v.x + cx), static_cast<f32>(v.y + cy)};
                p.inTangent = Vec2{v.inX - v.x, v.inY - v.y};
                p.outTangent = Vec2{v.outX - v.x, v.outY - v.y};
                m.points.push_back(p);
            }
            if (shape->shapeKeyed) rep.note("mascara", m.name, "forma da máscara animada: entrou só a primeira forma");
            if (!shape->expression.empty()) rep.skip("expressao", m.name, "expressão na forma da máscara não existe no Aurea");
        } else {
            rep.note("mascara", m.name, "forma da máscara não lida; ficou vazia");
        }
        l.masks.push_back(std::move(m));
        const u32 maskId = l.masks.back().id;
        maskIds.push_back(maskId);
        for (const Node& c : n.children) {
            if (c.kind != Node::Kind::Property || c.match == "ADBE Mask Shape") continue;
            apply_prop(bind_mask(*w.layer, maskId, c.match), c);
        }
        ++applied;
    }

    // --- texto ----------------------------------------------------------------
    bool text_ok() {
        if (w.layer && w.layer->kind == LayerKind::Text) return true;
        if (!textSkipped) rep.skip("texto", "Text Animator", "a camada escolhida não é de texto: escolha uma camada de texto");
        textSkipped = true;
        return false;
    }

    static u32 selector_kind(std::string_view m) noexcept {
        if (m == "ADBE Text Selector") return 0;
        if (m == "ADBE Text Wiggly Selector") return 1;
        if (m == "ADBE Text Expressible Selector") return 2;
        return 3;
    }

    /// Folhas do seletor (o grupo "Avançado" achatado).
    static void leaves(const Node& n, std::vector<const Node*>& out) {
        for (const Node& c : n.children) {
            if (c.kind == Node::Kind::Group) leaves(c, out);
            else if (c.kind == Node::Kind::Property) out.push_back(&c);
        }
    }

    static const Node* find_leaf(const std::vector<const Node*>& v, std::string_view m) {
        for (const Node* n : v) if (n->match == m) return n;
        return nullptr;
    }

    static f64 leaf_value(const std::vector<const Node*>& v, std::string_view m, f64 def) {
        const Node* n = find_leaf(v, m);
        return n && n->hasValue && !n->value.empty() ? n->value[0] : def;
    }

    /// Intervalo cheio e parado (0..100, sem deslocamento, quantidade 100)?
    static bool trivial_range(const Node& sel) {
        std::vector<const Node*> v;
        leaves(sel, v);
        for (const Node* n : v) if (animated(*n)) return false;
        return leaf_value(v, "ADBE Text Range Units", 1) < 1.5 && leaf_value(v, "ADBE Text Percent Start", 0) <= 1e-6 &&
               leaf_value(v, "ADBE Text Percent End", 100) >= 100 - 1e-6 &&
               std::fabs(leaf_value(v, "ADBE Text Percent Offset", 0)) <= 1e-6 &&
               std::fabs(leaf_value(v, "ADBE Text Selector Max Amount", 100) - 100) <= 1e-6;
    }

    void selector(u32 a, const Node& sel, u32 kind) {
        Layer& l = *w.layer;
        TextAnimator& an = l.text.animators[a];
        an.selector.type = kind == 1 ? 1 : 2;
        std::vector<const Node*> v;
        leaves(sel, v);
        const f64 units = leaf_value(v, "ADBE Text Range Units", 1);
        const f64 basedOn = leaf_value(v, "ADBE Text Range Type2", 1);
        const bool index = kind == 0 && units > 1.5;
        const u32 indexUnits = index ? text_units(l.text.content, static_cast<u32>(std::lround(basedOn))) : 0;
        if (std::lround(basedOn) == 2) rep.note("seletor", sel.name, "\"caracteres sem espaços\" virou caracteres");
        const f64 mode = leaf_value(v, "ADBE Text Selector Mode", 1);
        if (std::lround(mode) != 1) rep.note("seletor", sel.name, "modo do seletor diferente de Adicionar: o Aurea usa só a seleção dele");
        if (kind == 0) {
            const f64 shape = leaf_value(v, "ADBE Text Range Shape", 1);
            const f64 smooth = leaf_value(v, "ADBE Text Selector Smoothness", 100);
            if (std::lround(shape) == 1 && std::fabs(smooth - 100) > 1e-3)
                rep.note("seletor", sel.name, "suavidade do seletor quadrado: o Aurea revela sempre suave (100 %)");
        } else if (kind == 1) {
            const f64 mx = leaf_value(v, "ADBE Text Wiggly Max Amount", 100), mn = leaf_value(v, "ADBE Text Wiggly Min Amount", -100);
            if (std::fabs(mx + mn) > 1e-3) rep.note("seletor", sel.name, "quantidade mínima do oscilador ≠ −máxima: o Aurea oscila simétrico");
            if (std::fabs(leaf_value(v, "ADBE Text Character Correlation", 50) - 50) > 1e-3 ||
                std::fabs(leaf_value(v, "ADBE Text Temporal Phase", 0)) > 1e-3 ||
                std::fabs(leaf_value(v, "ADBE Text Spatial Phase", 0)) > 1e-3 ||
                leaf_value(v, "ADBE Text Wiggly Lock Dim", 0) > 0.5)
                rep.note("seletor", sel.name, "correlação/fases/travar dimensões do oscilador não existem no Aurea");
            for (const Node* n : v)
                if ((n->match == "ADBE Text Temporal Phase" || n->match == "ADBE Text Spatial Phase") && animated(*n))
                    rep.skip("seletor", sel.name + " (" + n->match + ")", "animação da fase do oscilador não existe no Aurea");
        }
        for (const Node* n : v) {
            const std::string_view m = n->match;
            if (index && starts(m, "ADBE Text Percent")) continue;
            if (!index && starts(m, "ADBE Text Index")) continue;
            const Binding b = bind_text_selector(*w.layer, a, kind, m, indexUnits);
            apply_prop(b, *n, "seletor");
        }
    }

    void animator(const Node& n) {
        if (n.kind != Node::Kind::Group || !text_ok()) return;
        Layer& l = *w.layer;
        if (l.text.animators.size() >= 64) {
            rep.skip("texto", n.name, "a camada já tem 64 animadores");
            return;
        }
        TextAnimator an;
        an.name = n.name.empty() ? "Animador " + std::to_string(l.text.animators.size() + 1) : n.name;
        an.enabled = (n.flags & 1u) != 0 || n.flags == 0;
        an.selector.type = 2;
        l.text.animators.push_back(an);
        const u32 a = static_cast<u32>(l.text.animators.size() - 1);
        newAnimators.push_back(a);
        ++rep.animators;
        ++applied;
        if (const Node* sels = n.child("ADBE Text Selectors")) {
            std::vector<std::pair<const Node*, u32>> list;
            for (const Node& s : sels->children) {
                if (s.kind != Node::Kind::Group) continue;
                const u32 k = selector_kind(s.match);
                if (k > 2) { rep.skip("seletor", s.name, "tipo de seletor sem equivalente"); continue; }
                if ((s.flags & 1u) == 0 && s.flags != 0) continue;   // desligado no AE: não conta
                list.emplace_back(&s, k);
            }
            usize primary = 0;
            if (list.size() == 2 && list[0].second == 0 && list[1].second == 1 && trivial_range(*list[0].first)) primary = 1;
            for (usize i = 0; i < list.size(); ++i) {
                if (i == primary) continue;
                if (list.size() == 2 && primary == 1 && i == 0) continue;   // intervalo cheio: não muda nada
                rep.note("seletor", list[i].first->name.empty() ? std::string(list[i].first->match) : list[i].first->name,
                         "o Aurea usa um seletor por animador: este foi ignorado");
            }
            if (!list.empty()) selector(a, *list[primary].first, list[primary].second);
        }
        if (const Node* props = n.child("ADBE Text Animator Properties")) {
            for (const Node& p : props->children) {
                if (p.kind != Node::Kind::Property || p.hidden()) continue;
                anim_prop(a, p);
            }
        }
    }

    /// Propriedade do animador; guarda se ela gira/escala cada caractere (o pivô importa).
    void anim_prop(u32 a, const Node& p) {
        const Binding b = bind_text_property(*w.layer, a, p.match);
        apply_prop(b, p);
        if (b.ok() && (starts(p.match, "ADBE Text Rotation") || starts(p.match, "ADBE Text Scale") ||
                       p.match == "ADBE Text Skew"))
            pivotAnimated = true;
    }

    void text_path(const Node& n) {
        if (n.kind == Node::Kind::Group) { for (const Node& c : n.children) text_path(c); return; }
        if (n.kind != Node::Kind::Property || !text_ok()) return;
        apply_prop(bind_text_path(*w.layer, n.match), n);
    }

    void text_more(const Node& n) {
        if (n.kind == Node::Kind::Group) { for (const Node& c : n.children) text_more(c); return; }
        if (n.kind != Node::Kind::Property || !text_ok()) return;
        if (n.match == "ADBE Text Anchor Point Align") {
            const bool zero = !n.hasValue || std::all_of(n.value.begin(), n.value.end(), [](f64 x) { return std::fabs(x) < 1e-6; });
            if (zero && !animated(n)) return;
            // O pivô por caractere do Aurea é o centro da caixa do caractere. No
            // AE, um Y negativo sobe a âncora da linha de base; os presets da
            // Adobe usam (0%, −30%) para "girar pelo meio" (45 de 621). Na faixa
            // −20%..−60% o resultado fica próximo (inferido; não medido no AE).
            if (!animated(n) && n.value.size() >= 2 && std::fabs(n.value[0]) <= 15.0 && n.value[1] <= -20.0 &&
                n.value[1] >= -60.0) {
                alignNearCenter = true;
                rep.note("propriedade", "Grouping Alignment",
                         "o Aurea gira e escala cada caractere pelo centro da caixa: aproxima o alinhamento do preset");
                return;
            }
            alignSkipped = true;
            std::string shown = "Grouping Alignment";
            if (animated(n)) {
                shown += " (animado)";
            } else if (n.value.size() >= 2) {
                char buf[64];
                std::snprintf(buf, sizeof buf, " (%.4g%%, %.4g%%)", n.value[0], n.value[1]);
                shown += buf;
            }
            rep.skip("propriedade", shown, "alinhamento do agrupamento (o Aurea gira e escala cada caractere pelo centro)");
            return;
        }
        apply_prop(bind_text_more(*w.layer, n.match), n);
    }

    void text_document() {
        rep.skip("texto", "Source Text", "estilo/conteúdo do texto do preset não é lido (fica o texto da camada)");
    }

    void text_child(const Node& c) {
        if (c.match == "ADBE Text Animators") { for (const Node& a : c.children) animator(a); }
        else if (c.match == "ADBE Text Path Options") text_path(c);
        else if (c.match == "ADBE Text More Options") text_more(c);
        else if (c.kind == Node::Kind::TextDocument || c.match == "ADBE Text Document") text_document();
    }

    void text_entry(const std::vector<PathStep>& p, const Node& v) {
        if (!text_ok()) return;
        if (p.size() == 1) { for (const Node& c : v.children) text_child(c); return; }
        const std::string_view g = p[1].match;
        if (g == "ADBE Text Document") { text_document(); return; }
        if (g == "ADBE Text Path Options") { text_path(v); return; }
        if (g == "ADBE Text More Options") { text_more(v); return; }
        if (g != "ADBE Text Animators") { rep.skip("texto", std::string(g), "grupo de texto sem equivalente"); return; }
        if (p.size() == 2) { for (const Node& a : v.children) animator(a); return; }
        if (p.size() == 3) { animator(v); return; }
        // Caminho para DENTRO de um animador (o preset guardou só algumas
        // propriedades dele): o animador do preset vira um animador NOVO, um
        // por índice do AE — o AE cria o "Animator 1" do mesmo jeito.
        Layer& l = *w.layer;
        const i32 idx = std::max(0, p[2].index);
        u32 a = kInvalidIndex;
        for (const auto& m : partialAnimators) if (m.first == idx) a = m.second;
        if (a == kInvalidIndex) {
            if (l.text.animators.size() >= 64) { rep.skip("texto", v.name, "a camada já tem 64 animadores"); return; }
            TextAnimator an;
            an.name = "Animator " + std::to_string(l.text.animators.size() + 1);
            an.selector.type = 2;
            l.text.animators.push_back(an);
            a = static_cast<u32>(l.text.animators.size() - 1);
            partialAnimators.emplace_back(idx, a);
            newAnimators.push_back(a);
            ++rep.animators;
            ++applied;
        }
        if (p.size() >= 5 && p[3].match == "ADBE Text Selectors") {
            const u32 kind = selector_kind(p[4].match);
            if (kind == 1) l.text.animators[a].selector.type = 1;
        }
        if (p[3].match == "ADBE Text Animator Properties") {
            if (v.kind == Node::Kind::Group) {
                for (const Node& c : v.children) if (c.kind == Node::Kind::Property && !c.hidden()) anim_prop(a, c);
            } else {
                anim_prop(a, v);
            }
        } else if (p[3].match == "ADBE Text Selectors" && p.size() >= 5) {
            const u32 kind = selector_kind(p[4].match);
            if (kind > 2) return;
            if (v.kind == Node::Kind::Group) {
                std::vector<const Node*> leafs;
                if (p.size() == 5) leaves(v, leafs);
                else for (const Node& c : v.children) if (c.kind == Node::Kind::Property) leafs.push_back(&c);
                for (const Node* n : leafs) apply_prop(bind_text_selector(l, a, kind, n->match, 0), *n, "seletor");
            } else {
                apply_prop(bind_text_selector(l, a, kind, v.match, 0), v, "seletor");
            }
        }
    }

    // --- entrada ----------------------------------------------------------------
    void entry(const Entry& e) {
        if (e.path.empty() || !w.layer) return;
        const std::string_view root = e.path[0].match;
        const Node& v = e.value;
        if (root == "ADBE Effect Parade") {
            if (e.path.size() == 1) { for (const Node& c : v.children) effect(c); }
            else if (v.kind == Node::Kind::Effect) effect(v);
            else rep.skip("parametro", e.name.empty() ? std::string(e.path.back().match) : e.name,
                          "o preset ajusta um efeito que já existe na camada (não suportado)");
        } else if (root == "ADBE Text Properties") {
            text_entry(e.path, v);
        } else if (root == "ADBE Transform Group") {
            transform(v);
        } else if (root == "ADBE Mask Parade") {
            if (e.path.size() == 1) { for (const Node& c : v.children) mask(c); }
            else if (e.path.size() == 2) mask(v);
            else rep.skip("mascara", e.name, "o preset ajusta uma máscara que já existe na camada (não suportado)");
        } else if (starts(root, "ADBE Material Options") || starts(root, "ADBE Plane Options") ||
                   starts(root, "ADBE Extrsn Options")) {
            // Opções de material 3D: sem efeito visual na camada do Aurea.
        } else if (root == "ADBE Marker") {
            rep.skip("propriedade", "Marker", "marcadores da camada não entram");
        } else if (root == "ADBE Time Remapping") {
            rep.skip("propriedade", "Time Remap", "remapeamento de tempo do preset não entra");
        } else if (root == "ADBE Audio Group") {
            rep.skip("propriedade", "Audio Levels", "níveis de áudio do preset não entram");
        } else if (root == "ADBE Root Vectors Group") {
            rep.skip("camada", "Contents", "conteúdo de camada de forma do AE não existe no Aurea");
        } else if (root == "ADBE Layer Styles") {
            rep.skip("efeito", "Layer Styles", "estilos de camada do AE não existem no Aurea");
        } else {
            rep.skip("propriedade", e.name.empty() ? std::string(root) : e.name, "grupo do AE sem equivalente");
        }
    }

    /// Expressão que só LIGA a outra propriedade de animador do mesmo texto
    /// (`text.animator("A").selector("S").maxAmount`, `….property.position`):
    /// os presets da Adobe usam para vários animadores andarem juntos. O Aurea
    /// não lê `text.animator`, então os valores e keys da origem são copiados
    /// (o resultado é o mesmo enquanto ninguém mexer na origem).
    bool bake_text_link(Layer& l, const PendingExpr& pe) {
        std::string_view s = pe.src;
        auto trim = [](std::string_view& v) {
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t' || v.front() == '\r' || v.front() == '\n')) v.remove_prefix(1);
            while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r' || v.back() == '\n' || v.back() == ';'))
                v.remove_suffix(1);
        };
        auto eat = [&](std::string_view word) {
            if (s.substr(0, word.size()) != word) return false;
            s.remove_prefix(word.size());
            return true;
        };
        auto quoted = [&](std::string& out) {
            if (s.empty() || (s.front() != '"' && s.front() != '\'')) return false;
            const char q = s.front();
            const usize e = s.find(q, 1);
            if (e == std::string_view::npos) return false;
            out = std::string(s.substr(1, e - 1));
            s.remove_prefix(e + 1);
            return true;
        };
        trim(s);
        std::string anim, sel;
        if (!eat("text.animator(") || !quoted(anim) || !eat(")")) return false;
        const bool isSel = eat(".selector(");
        if (isSel && (!quoted(sel) || !eat(")"))) return false;
        if (!isSel && !eat(".property")) return false;
        if (!eat(".")) return false;
        const std::string attr(s);
        for (const char c : attr) if (!std::isalnum(static_cast<unsigned char>(c))) return false;
        struct Attr { const char* name; const char* match; u32 kind; };
        static constexpr Attr kSel[] = {
            {"start", "ADBE Text Percent Start", 0}, {"end", "ADBE Text Percent End", 0},
            {"offset", "ADBE Text Percent Offset", 0}, {"amount", "ADBE Text Selector Max Amount", 0},
            {"smoothness", "ADBE Text Selector Smoothness", 0}, {"easeHigh", "ADBE Text Levels Max Ease", 0},
            {"easeLow", "ADBE Text Levels Min Ease", 0}, {"maxAmount", "ADBE Text Wiggly Max Amount", 1},
            {"minAmount", "ADBE Text Wiggly Min Amount", 1}, {"wigglesSecond", "ADBE Text Temporal Freq", 1},
            {"correlation", "ADBE Text Character Correlation", 1}, {"temporalPhase", "ADBE Text Temporal Phase", 1},
            {"spatialPhase", "ADBE Text Spatial Phase", 1},
        };
        static constexpr Attr kProp[] = {
            {"position", "ADBE Text Position 3D", 0}, {"scale", "ADBE Text Scale 3D", 0},
            {"rotation", "ADBE Text Rotation", 0}, {"opacity", "ADBE Text Opacity", 0},
            {"fillColor", "ADBE Text Fill Color", 0}, {"strokeColor", "ADBE Text Stroke Color", 0},
            {"strokeWidth", "ADBE Text Stroke Width", 0}, {"trackingAmount", "ADBE Text Tracking Amount", 0},
            {"blur", "ADBE Text Blur", 0}, {"skew", "ADBE Text Skew", 0}, {"skewAxis", "ADBE Text Skew Axis", 0},
            {"characterOffset", "ADBE Text Character Offset", 0},
        };
        const Attr* hit = nullptr;
        for (const Attr& a : isSel ? std::span<const Attr>(kSel) : std::span<const Attr>(kProp))
            if (attr == a.name) hit = &a;
        if (!hit) return false;
        u32 src = kInvalidIndex;
        for (u32 i = 0; i < l.text.animators.size(); ++i)
            if (iequals(l.text.animators[i].name, anim)) { src = i; break; }
        if (src == kInvalidIndex) return false;
        const Binding sb = isSel ? bind_text_selector(l, src, hit->kind, hit->match, 0) : bind_text_property(l, src, hit->match);
        if (!sb.ok() || sb.ch.size() != pe.b.ch.size()) return false;
        // Valor parado primeiro (todos os componentes), depois as keys trilha a trilha.
        write_static(w, pe.b, read_value(l, sb, w.localAnchor), false);
        for (usize i = 0; i < sb.ch.size(); ++i) {
            const Channel& a = sb.ch[i];
            const Channel& b = pe.b.ch[i];
            if (a.home != Home::Track && a.home != Home::TrackOnly) continue;
            const Track* from = l.tracks.find(a.prop, a.ei, a.pi);
            if (!from || from->keys.empty()) continue;
            const Track copy = *from;
            Track& to = l.tracks.get_or_create(b.prop, b.ei, b.pi);
            to.keys = copy.keys;
            to.staticValue = copy.staticValue;
            to.lastIndex = 0;
        }
        rep.note("expressao", pe.b.label,
                 "a ligação text.animator(\"" + anim + "\") virou cópia dos valores e keys (o Aurea não lê text.animator)");
        return true;
    }

    void finish() {
        Layer* l = w.layer;
        if (!l) return;
        if (w.pendingPathMask > 0) {
            const usize i = static_cast<usize>(w.pendingPathMask - 1);
            if (i < maskIds.size()) {
                l->text.pathMask = maskIds[i];
                l->text.pathLegacyLayout = false;
            } else {
                rep.note("texto", "Path", "o caminho do texto aponta para uma máscara que não veio no preset");
            }
        }
        if (w.pendingGrouping >= 0) {
            for (const u32 a : newAnimators) {
                Track& t = l->tracks.get_or_create(TrackProperty::TextAnimParam, a, text::kAnchorGrouping);
                t.clear();
                (void)t.set(FrameIndex{0}, static_cast<f32>(w.pendingGrouping), Interpolation::Hold);
            }
        }
        if (pivotAnimated && !alignNearCenter && !alignSkipped)
            rep.note("texto", "Grouping Alignment",
                     "o Aurea gira e escala cada caractere pelo centro; no AE (alinhamento 0%, 0%) o pivô fica na linha de base");
        for (PendingExpr& pe : exprs) {
            if (bake_text_link(*l, pe)) continue;
            const std::string src = rewrite_expression(pe.src, aliases);
            write_expression(w, pe.b, src);
            ++rep.expressions;
        }
        rep.keyframes += w.keyframes;
        if (l->kind == LayerKind::Text) host.text_changed(lid);
    }
};

} // namespace

bool apply_ffx(const FfxDocument& doc, Host& host, LayerId target, ImportReport& report) {
    report.kind = "ffx";
    Composition* comp = host.composition();
    Layer* l = comp ? comp->layer(target) : nullptr;
    if (!l) {
        report.message = "Escolha uma camada para aplicar o preset do After Effects.";
        return false;
    }
    if (l->locked) {
        report.message = "A camada está bloqueada.";
        return false;
    }
    Applier a(host, target, report);
    a.comp = comp;
    a.w.layer = l;
    a.w.fps = comp->fps() > 0 ? comp->fps() : 30.0;
    a.w.report = &report;
    a.w.registry = &host.registry();
    f64 t0 = 1e300;
    for (const Entry& e : doc.entries) min_key_time(e.value, t0);
    a.w.t0 = t0 < 1e299 ? t0 : 0.0;
    const FrameIndex ph{host.playhead()};
    a.w.localAnchor = l->contains_time(ph) ? l->local_time(ph).value : l->local_time(l->start).value;
    layer_size(*l, comp->width(), comp->height(), a.layerW, a.layerH);
    for (const Entry& e : doc.entries) {
        a.refresh();
        a.entry(e);
    }
    a.refresh();
    a.finish();
    report.ok = a.applied > 0;
    report.changes += a.applied + a.w.writes;
    if (!report.ok && report.message.empty() && a.textSkipped)
        report.message = "Este preset anima texto: escolha uma camada de texto (ou nenhuma camada, para o Aurea criar uma).";
    if (!report.ok && report.message.empty())
        report.message = report.skipped.empty() ? "O preset não tem nada que o Aurea aplique."
                                                : "Nada do preset tem equivalente no Aurea.";
    return report.ok;
}

} // namespace aurea::ae
