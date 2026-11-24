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
//
//  Conteúdo de camada de forma (ADBE Root Vectors Group): entra numa camada
//  VETORIAL como grupos novos — caminhos (retângulo, elipse, estrela, livre),
//  preenchimento, contorno (traços 1..3), Aparar, Mesclar, Repetidor (escala
//  X/Y) e os operadores de caminho (ADBE Vector Filter - Offset, PB, RC,
//  Twist, Zigzag, Roughen = Ondular caminhos, Wiggler = Ondular transform),
//  com keyframes nas trilhas VectorParam. Grupos aninhados são achatados.
// =============================================================================
#include "AeImportBind.hpp"

#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/project/AeProject.hpp"
#include "aurea/text/TextAnimator.hpp"
#include "aurea/timeline/Composition.hpp"
#include "aurea/vector/Vector.hpp"

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
        // 1.0.8: plugin de terceiros com outro matchName (versão, fabricante):
        // o nome no painel do AE decide (só os apelidos conhecidos).
        if (!rule && !n.effectName.empty()) rule = find_effect_rule_alias(n.effectName);
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
                // I(0, "*"): a regra aproxima o efeito; os outros parâmetros
                // ficam no padrão do Aurea sem um item por parâmetro.
                bool rest = false;
                for (u32 i = 0; i < rule->paramCount; ++i)
                    rest = rest || (rule->params[i].ae == 0 && rule->params[i].silent);
                if (!known && rest) continue;
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

    // --- 0.4.6: animador do AE -> efeito Text Animator Pro (editavel) ----------
    bool pro = false;                 ///< importar animadores de texto no Text Animator Pro
    std::vector<u32> proEffects;      ///< efeitos Pro criados (recebem o agrupamento do "Mais opcoes")
    std::vector<const Node*> proMore; ///< propriedades do "Mais opcoes" (aplicadas no fim a cada Pro)

    void pro_apply(u32 fx, u32 kind, const Node& n, bool index) {
        const std::string_view m = n.match;
        if (kind == 0 && index && starts(m, "ADBE Text Percent")) return;
        if (kind == 0 && !index && starts(m, "ADBE Text Index")) return;
        apply_prop(bind_text_pro(fx, kind, m, w.layer ? w.layer->text.size : 72.0), n, kind == 3 ? "propriedade" : "seletor");
    }

    void animator_pro(const Node& n) {
        if (n.kind != Node::Kind::Group || !text_ok()) return;
        const EffectTypeId type = effect_type_id(text::kAnimatorProEffect);
        const u32 fx = host.add_effect(lid, type);
        refresh();
        EffectInstance* inst = w.layer ? w.layer->find_effect(EffectId{fx, 0}) : nullptr;
        if (fx == kInvalidIndex || !inst) { rep.skip("texto", n.name, "o Aurea recusou o Text Animator Pro nesta camada"); return; }
        inst->enabled = (n.flags & 1u) != 0 || n.flags == 0;
        proEffects.push_back(fx);
        ++rep.animators;
        ++applied;
        if (const Node* sels = n.child("ADBE Text Selectors")) {
            bool range = false, wiggly = false, expression = false;
            for (const Node& s : sels->children) {
                if (s.kind != Node::Kind::Group) continue;
                if ((s.flags & 1u) == 0 && s.flags != 0) continue;   // desligado no AE
                const u32 k = selector_kind(s.match);
                const std::string shown = s.name.empty() ? std::string(s.match) : s.name;
                if (k > 2 || (k == 0 && range) || (k == 1 && wiggly) || (k == 2 && expression)) {
                    rep.note("seletor", shown, "o Text Animator Pro combina um seletor de intervalo, um de oscilacao e um de expressao: este ficou de fora");
                    continue;
                }
                std::vector<const Node*> v;
                leaves(s, v);
                const bool index = k == 0 && leaf_value(v, "ADBE Text Range Units", 1) > 1.5;
                if (k == 2) {
                    refresh();
                    if (EffectInstance* e = w.layer->find_effect(EffectId{fx, 0}); e && text::apExprOn < e->params.size())
                        e->params[text::apExprOn].constant.v[0] = 1.0f;
                    expression = true;
                } else if (k == 1) {
                    refresh();
                    if (EffectInstance* e = w.layer->find_effect(EffectId{fx, 0}); e && text::apWiggly < e->params.size())
                        e->params[text::apWiggly].constant.v[0] = 1.0f;
                    // Sem intervalo antes: a oscilacao age no texto todo.
                    wiggly = true;
                } else {
                    range = true;
                }
                for (const Node* leaf : v) pro_apply(fx, k, *leaf, index);
            }
            // Só oscilação (sem intervalo): no AE ela age sozinha. No Pro o
            // intervalo some (Fim 0) e a oscilação SOMA ao nada.
            if (wiggly && !range) {
                refresh();
                if (EffectInstance* e = w.layer->find_effect(EffectId{fx, 0}); e && text::apWigglyMode < e->params.size()) {
                    e->params[text::apEnd].constant.v[0] = 0.0f;
                    e->params[text::apWigglyMode].constant.v[0] = 0.0f;
                }
            }
        }
        if (const Node* props = n.child("ADBE Text Animator Properties")) {
            for (const Node& p : props->children) {
                if (p.kind != Node::Kind::Property || p.hidden()) continue;
                pro_apply(fx, 3, p, false);
            }
        }
    }

    void animator(const Node& n) {
        if (pro && w.layer && w.layer->kind == LayerKind::Text) { animator_pro(n); return; }
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
        // Pro: o agrupamento e o alinhamento valem para cada Text Animator Pro do preset (no fim).
        if (pro && (n.match == "ADBE Text Anchor Point Option" || n.match == "ADBE Text Anchor Point Align")) { proMore.push_back(&n); return; }
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

    // --- conteúdo de camada de forma (ADBE Root Vectors Group) ------------------
    // A árvore do AE (grupos dentro de grupos; cada operador vale para tudo o
    // que está ACIMA dele no mesmo grupo) entra achatada nos grupos do Aurea:
    // cada grupo do AE com caminhos próprios vira um grupo; tintas e
    // operadores do grupo de fora que estão abaixo de um subgrupo também
    // valem para ele (herdados); o transform dos grupos de fora é composto no
    // do grupo (exato com escala uniforme). O primeiro item do AE fica por
    // cima (o último grupo do Aurea).
    struct ShapeCtx {
        Vec2 offset{0.0f, 0.0f};   ///< G(0) do grupo de fora, no espaço da camada do Aurea
        f32 scale = 1.0f;          ///< escala uniforme acumulada (fração)
        f32 rotation = 0.0f;       ///< giro acumulado (graus)
        f32 opacity = 1.0f;        ///< opacidade acumulada (fração)
        std::vector<const Node*> inherited;   ///< tintas/operadores de fora (em ordem)
        u32 depth = 0;
        bool moving = false;       ///< o transform do grupo de fora é animado (entrou parado)
        bool uneven = false;       ///< escala X ≠ Y no grupo de fora (vale a média)
    };
    bool shapeSkipped = false;
    u32 shapeGroups = 0;

    static bool item_on(const Node& n) noexcept { return n.flags == 0 || (n.flags & 1u) != 0; }
    static bool is_path_item(std::string_view m) noexcept { return starts(m, "ADBE Vector Shape - "); }
    static bool is_paint_or_op(std::string_view m) noexcept {
        return starts(m, "ADBE Vector Graphic - ") || starts(m, "ADBE Vector Filter - ");
    }
    static std::string shown_name(const Node& n) { return n.name.empty() ? n.match : n.name; }
    /// Primeiro valor (o parado ou o do primeiro keyframe), componente `i`.
    static f64 first(const Node& n, usize i, f64 def) noexcept {
        if (!n.keys.empty() && i < n.keys.front().value.size()) return n.keys.front().value[i];
        if (n.hasValue && i < n.value.size()) return n.value[i];
        return def;
    }
    static Vec2 first2(const Node& n, Vec2 def) noexcept {
        return Vec2{static_cast<f32>(first(n, 0, def.x)), static_cast<f32>(first(n, 1, def.y))};
    }
    /// Propriedades em % do conteúdo de forma: o .ffx pode guardar a fração
    /// (como a opacidade e a escala da camada). Todos os valores em 0..1 com
    /// algum diferente de zero = fração (não verificado num preset real).
    static f64 pct_mul(const Node& n) noexcept {
        bool any = false;
        auto look = [&](const std::vector<f64>& v) {
            for (const f64 x : v) {
                if (std::fabs(x) > 1.0001) return false;
                any = any || std::fabs(x) > 1e-9;
            }
            return true;
        };
        if (!look(n.value)) return 1.0;
        for (const Key& k : n.keys) if (!look(k.value)) return 1.0;
        return any ? 100.0 : 1.0;
    }
    static Channel vch(u32 gi, u32 param, u8 src, f64 mul = 1.0, f64 add = 0.0, f64 lo = -1.0e9, f64 hi = 1.0e9) {
        Channel c;
        c.home = Home::Track;
        c.prop = TrackProperty::VectorParam;
        c.ei = gi;
        c.pi = param;
        c.src = src;
        c.mul = mul;
        c.add = add;
        c.lo = lo;
        c.hi = hi;
        return c;
    }
    VectorGroup* vgroup(u32 gi) noexcept {
        refresh();
        return w.layer && gi < w.layer->shape.vector.groups.size() ? &w.layer->shape.vector.groups[gi] : nullptr;
    }
    /// Valor/keys/expressão da propriedade nos canais VectorParam do grupo.
    void vec_apply(const Node& n, std::vector<Channel> ch, u32 dims = 1) {
        Binding b;
        b.label = shown_name(n);
        b.dims = dims;
        b.spatial = n.spatial;
        b.ch = std::move(ch);
        refresh();
        apply_prop(b, n, "forma");
    }
    /// Propriedade sem trilha no Aurea: vale o valor parado (o primeiro keyframe).
    void vec_static_note(const Node& n, const char* what) {
        if (animated(n))
            rep.note("forma", shown_name(n) + " (" + what + ")",
                     "animação deste ajuste do conteúdo de forma não existe no Aurea; ficou o primeiro valor");
    }

    /// Caminho do AE (retângulo, elipse, estrela/polígono, caminho livre).
    bool shape_path(const Node& item, VectorPath& out) {
        const std::string_view m = item.match;
        out = VectorPath{};
        auto vec2 = [&](std::string_view pm, Vec2 def) {
            const Node* n = item.child(pm);
            if (!n) return def;
            vec_static_note(*n, "caminho");
            return first2(*n, def);
        };
        auto num = [&](std::string_view pm, f64 def) {
            const Node* n = item.child(pm);
            if (!n) return static_cast<f32>(def);
            vec_static_note(*n, "caminho");
            return static_cast<f32>(first(*n, 0, def));
        };
        if (m == "ADBE Vector Shape - Rect") {
            out.kind = VectorPathKind::Rect;
            out.size = vec2("ADBE Vector Rect Size", Vec2{100.0f, 100.0f});
            out.center = vec2("ADBE Vector Rect Position", Vec2{0.0f, 0.0f});
            out.roundness = std::max(0.0f, num("ADBE Vector Rect Roundness", 0.0));
        } else if (m == "ADBE Vector Shape - Ellipse") {
            out.kind = VectorPathKind::Ellipse;
            out.size = vec2("ADBE Vector Ellipse Size", Vec2{100.0f, 100.0f});
            out.center = vec2("ADBE Vector Ellipse Position", Vec2{0.0f, 0.0f});
        } else if (m == "ADBE Vector Shape - Star") {
            out.kind = num("ADBE Vector Star Type", 1.0) > 1.5f ? VectorPathKind::Polygon : VectorPathKind::Star;
            out.points = std::max(3.0f, num("ADBE Vector Star Points", 5.0));
            out.center = vec2("ADBE Vector Star Position", Vec2{0.0f, 0.0f});
            out.rotation = num("ADBE Vector Star Rotation", 0.0);
            out.outerRadius = num("ADBE Vector Star Outer Radius", 100.0);
            out.innerRadius = num("ADBE Vector Star Inner Radius", 50.0);
            out.outerRoundness = num("ADBE Vector Star Outer Roundess", 0.0);   // sic: o match name do AE
            out.innerRoundness = num("ADBE Vector Star Inner Roundess", 0.0);
        } else if (m == "ADBE Vector Shape - Group") {
            const Node* s = item.child("ADBE Vector Shape");
            if (!s || !s->hasShape || s->shape.vertices.empty()) {
                rep.note("forma", shown_name(item), "caminho do conteúdo de forma não lido; ficou de fora");
                return false;
            }
            out.kind = VectorPathKind::Free;
            out.path.closed = s->shape.closed;
            for (const ShapeVertex& v : s->shape.vertices) {
                BezierVertex bv;
                bv.p = Vec2{v.x, v.y};
                bv.in = Vec2{v.inX - v.x, v.inY - v.y};
                bv.out = Vec2{v.outX - v.x, v.outY - v.y};
                out.path.v.push_back(bv);
            }
            if (s->shapeKeyed) rep.note("forma", shown_name(item), "caminho animado: entrou só a primeira forma");
            if (!s->expression.empty()) rep.skip("expressao", shown_name(item), "expressão no caminho não existe no Aurea");
        } else {
            rep.skip("forma", shown_name(item), "caminho do AE sem equivalente");
            return false;
        }
        // Direção: 3 = invertida (1/2 = padrão / sentido horário).
        if (const Node* d = item.child("ADBE Vector Shape Direction")) out.reversed = std::lround(first(*d, 0, 1.0)) == 3;
        return true;
    }

    static Vec4 first_color(const Node& c) noexcept {
        return Vec4{static_cast<f32>(first(c, 0, 1.0)), static_cast<f32>(first(c, 1, 1.0)), static_cast<f32>(first(c, 2, 1.0)), 1.0f};
    }

    /// Degradê: tipo e pontos entram; as cores do AE (ADBE Vector Grad Colors,
    /// um dado próprio) não são lidas — ficam branco → preto.
    void shape_gradient_prop(u32 gi, const Node& c, bool fill) {
        VectorGroup* g = vgroup(gi);
        if (!g) return;
        VectorPaint& p = fill ? g->fill.paint : g->stroke.paint;
        if (c.match == "ADBE Vector Grad Type") {
            p.type = std::lround(first(c, 0, 1.0)) == 2 ? 2 : 1;
        } else if (c.match == "ADBE Vector Grad Start Pt") {
            vec_static_note(c, "degradê");
            p.start = first2(c, Vec2{0.0f, 0.0f});
        } else if (c.match == "ADBE Vector Grad End Pt") {
            vec_static_note(c, "degradê");
            p.end = first2(c, Vec2{100.0f, 0.0f});
        }
    }
    void shape_gradient_done(u32 gi, const Node& n, bool fill) {
        VectorGroup* g = vgroup(gi);
        if (!g) return;
        VectorPaint& p = fill ? g->fill.paint : g->stroke.paint;
        if (p.type == 0) p.type = 1;
        p.stops = {VectorStop{0.0f, Vec4{1, 1, 1, 1}}, VectorStop{1.0f, Vec4{0, 0, 0, 1}}};
        rep.note("forma", shown_name(n), "cores do degradê do AE não lidas: ficou branco → preto (tipo e pontos entraram)");
    }

    void shape_fill(u32 gi, const Node& n, bool gradient) {
        if (VectorGroup* g = vgroup(gi)) {
            g->fill.enabled = true;
            g->fill.paint.type = 0;
        }
        for (const Node& c : n.children) {
            if (c.kind != Node::Kind::Property) continue;
            if (c.match == "ADBE Vector Fill Color") {
                vec_static_note(c, "cor");
                if (VectorGroup* g = vgroup(gi)) g->fill.paint.color = first_color(c);
            } else if (c.match == "ADBE Vector Fill Opacity") {
                vec_apply(c, {vch(gi, kVecFillOpacity, 0, pct_mul(c), 0.0, 0.0, 100.0)});
            } else if (c.match == "ADBE Vector Fill Rule") {
                if (VectorGroup* g = vgroup(gi)) g->fill.rule = std::lround(first(c, 0, 1.0)) == 2 ? 1 : 0;
            } else if (c.match == "ADBE Vector Blend Mode") {
                if (std::lround(first(c, 0, 1.0)) != 1) rep.skip("forma", "Blend Mode", "modo de mesclagem do preenchimento não existe no Aurea");
            } else if (gradient) {
                shape_gradient_prop(gi, c, true);
            }
        }
        if (gradient) shape_gradient_done(gi, n, true);
    }

    /// Traço 1..3 / vão 1..3 e deslocamento (os traços são animáveis no Aurea).
    void shape_dashes(u32 gi, const Node& dashes) {
        const Node* d[kVecMaxDashPairs]{};
        const Node* gap[kVecMaxDashPairs]{};
        const Node* off = nullptr;
        for (const Node& c : dashes.children) {
            if (c.kind != Node::Kind::Property) continue;
            for (u32 k = 0; k < kVecMaxDashPairs; ++k) {
                if (c.match == "ADBE Vector Stroke Dash " + std::to_string(k + 1)) d[k] = &c;
                if (c.match == "ADBE Vector Stroke Gap " + std::to_string(k + 1)) gap[k] = &c;
            }
            if (c.match == "ADBE Vector Stroke Offset") off = &c;
        }
        std::vector<f32> pattern;
        u32 pairs = 0;
        for (u32 k = 0; k < kVecMaxDashPairs && d[k]; ++k) {
            const f64 dv = first(*d[k], 0, 0.0);
            pattern.push_back(static_cast<f32>(std::max(0.0, dv)));
            pattern.push_back(static_cast<f32>(std::max(0.0, gap[k] ? first(*gap[k], 0, dv) : dv)));   // sem vão: igual ao traço (AE)
            ++pairs;
        }
        if (pairs == 0) return;
        if (VectorGroup* g = vgroup(gi)) g->stroke.dashes = pattern;
        for (u32 k = 0; k < pairs; ++k) {
            vec_apply(*d[k], {vch(gi, kVecDash1 + 2 * k, 0, 1.0, 0.0, 0.0, 100000.0)});
            if (gap[k]) vec_apply(*gap[k], {vch(gi, kVecGap1 + 2 * k, 0, 1.0, 0.0, 0.0, 100000.0)});
        }
        if (off) vec_apply(*off, {vch(gi, kVecDashOffset, 0)});
    }

    void shape_stroke(u32 gi, const Node& n, bool gradient) {
        if (VectorGroup* g = vgroup(gi)) {
            g->stroke.enabled = true;
            g->stroke.paint.type = 0;
            g->stroke.width = 2.0f;   // padrão do AE
        }
        for (const Node& c : n.children) {
            const std::string& m = c.match;
            if (c.kind == Node::Kind::Group) {
                if (m == "ADBE Vector Stroke Dashes") {
                    shape_dashes(gi, c);
                } else if (m == "ADBE Vector Stroke Taper") {
                    for (const Node& t : c.children)
                        if (t.kind == Node::Kind::Property && (animated(t) || std::fabs(first(t, 0, 0.0)) > 1e-6) &&
                            (t.match == "ADBE Vector Taper Start Length" || t.match == "ADBE Vector Taper End Length")) {
                            rep.skip("forma", "Taper", "afinar do contorno do AE não entrou (o Aurea afina por outra régua)");
                            break;
                        }
                } else if (m == "ADBE Vector Stroke Wave") {
                    for (const Node& t : c.children)
                        if (t.kind == Node::Kind::Property && t.match == "ADBE Vector Taper Wave Amount" &&
                            (animated(t) || std::fabs(first(t, 0, 0.0)) > 1e-6)) {
                            rep.skip("forma", "Wave", "onda do contorno não existe no Aurea");
                            break;
                        }
                }
                continue;
            }
            if (c.kind != Node::Kind::Property) continue;
            if (m == "ADBE Vector Stroke Color") {
                vec_static_note(c, "cor");
                if (VectorGroup* g = vgroup(gi)) g->stroke.paint.color = first_color(c);
            } else if (m == "ADBE Vector Stroke Opacity") {
                vec_apply(c, {vch(gi, kVecStrokeOpacity, 0, pct_mul(c), 0.0, 0.0, 100.0)});
            } else if (m == "ADBE Vector Stroke Width") {
                vec_apply(c, {vch(gi, kVecStrokeWidth, 0, 1.0, 0.0, 0.0, 2000.0)});
            } else if (m == "ADBE Vector Stroke Line Cap") {
                if (VectorGroup* g = vgroup(gi)) g->stroke.cap = static_cast<u8>(std::clamp<long>(std::lround(first(c, 0, 1.0)) - 1, 0, 2));
            } else if (m == "ADBE Vector Stroke Line Join") {
                if (VectorGroup* g = vgroup(gi)) g->stroke.join = static_cast<u8>(std::clamp<long>(std::lround(first(c, 0, 1.0)) - 1, 0, 2));
            } else if (m == "ADBE Vector Stroke Miter Limit") {
                if (VectorGroup* g = vgroup(gi)) g->stroke.miterLimit = static_cast<f32>(std::clamp(first(c, 0, 4.0), 1.0, 100.0));
            } else if (m == "ADBE Vector Blend Mode") {
                if (std::lround(first(c, 0, 1.0)) != 1) rep.skip("forma", "Blend Mode", "modo de mesclagem do contorno não existe no Aurea");
            } else if (gradient) {
                shape_gradient_prop(gi, c, false);
            }
        }
        if (gradient) shape_gradient_done(gi, n, false);
    }

    void shape_trim(u32 gi, const Node& n) {
        for (const Node& c : n.children) {
            if (c.kind != Node::Kind::Property) continue;
            if (c.match == "ADBE Vector Trim Start") vec_apply(c, {vch(gi, kVecTrimStart, 0, pct_mul(c), 0.0, 0.0, 100.0)});
            else if (c.match == "ADBE Vector Trim End") vec_apply(c, {vch(gi, kVecTrimEnd, 0, pct_mul(c), 0.0, 0.0, 100.0)});
            else if (c.match == "ADBE Vector Trim Offset") vec_apply(c, {vch(gi, kVecTrimOffset, 0, 100.0 / 360.0)});   // graus → % do comprimento
            else if (c.match == "ADBE Vector Trim Type") {
                if (VectorGroup* g = vgroup(gi)) g->trim.mode = std::lround(first(c, 0, 1.0)) == 2 ? 1 : 0;
            }
        }
    }

    void shape_repeater(u32 gi, const Node& n) {
        std::vector<const Node*> leafs;
        leaves(n, leafs);
        for (const Node* c : leafs) {
            const std::string& m = c->match;
            if (m == "ADBE Vector Repeater Copies") {
                vec_apply(*c, {vch(gi, kVecRepCopies, 0, 1.0, 0.0, 0.0, 500.0)});
            } else if (m == "ADBE Vector Repeater Offset") {
                vec_apply(*c, {vch(gi, kVecRepOffset, 0)});
            } else if (m == "ADBE Vector Repeater Order") {
                if (VectorGroup* g = vgroup(gi)) g->repeater.above = std::lround(first(*c, 0, 1.0)) == 2 ? 1 : 0;
            } else if (m == "ADBE Vector Repeater Anchor") {
                vec_static_note(*c, "âncora do repetidor");
                if (VectorGroup* g = vgroup(gi)) g->repeater.anchor = first2(*c, Vec2{0.0f, 0.0f});
            } else if (m == "ADBE Vector Repeater Position") {
                vec_apply(*c, {vch(gi, kVecRepPosX, 0), vch(gi, kVecRepPosY, 1)}, 2);
            } else if (m == "ADBE Vector Repeater Scale") {
                // Escala X/Y (v6): X e Y iguais em todos os valores = ligada.
                bool same = std::fabs(first(*c, 0, 100.0) - first(*c, 1, 100.0)) < 1e-6;
                for (const Key& k : c->keys) same = same && k.value.size() >= 2 && std::fabs(k.value[0] - k.value[1]) < 1e-6;
                if (VectorGroup* g = vgroup(gi)) g->repeater.scaleLinked = same ? 1 : 0;
                const f64 mul = pct_mul(*c);
                if (same) vec_apply(*c, {vch(gi, kVecRepScale, 0, mul, 0.0, -10000.0, 10000.0)}, 2);
                else vec_apply(*c, {vch(gi, kVecRepScale, 0, mul, 0.0, -10000.0, 10000.0), vch(gi, kVecRepScaleY, 1, mul, 0.0, -10000.0, 10000.0)}, 2);
            } else if (m == "ADBE Vector Repeater Rotation") {
                vec_apply(*c, {vch(gi, kVecRepRotation, 0)});
            } else if (m == "ADBE Vector Repeater Opacity 1") {
                vec_apply(*c, {vch(gi, kVecRepStartOpacity, 0, pct_mul(*c), 0.0, 0.0, 100.0)});
            } else if (m == "ADBE Vector Repeater Opacity 2") {
                vec_apply(*c, {vch(gi, kVecRepEndOpacity, 0, pct_mul(*c), 0.0, 0.0, 100.0)});
            }
        }
    }

    /// Operador de caminho (Offset Paths, Pucker & Bloat, Wiggle Paths, Round
    /// Corners, Twist, Zig Zag, Wiggle Transform): os parâmetros vão para as
    /// trilhas do operador (vector_modifier_param), animáveis como no AE.
    void shape_modifier(u32 gi, const Node& n, VectorModifierType type) {
        VectorGroup* g = vgroup(gi);
        if (!g) return;
        if (g->modifiers.size() >= kVecMaxModifiers) {
            rep.skip("forma", shown_name(n), "o grupo já tem o máximo de operadores");
            return;
        }
        VectorModifier mod = vector::make_modifier(type);
        // Padrões do AE onde o preset não traz o valor (os do Aurea são os de
        // quem adiciona à mão: já mostram o efeito).
        switch (type) {
            case VectorModifierType::OffsetPaths: mod.params[kVecModOffsetAmount] = 0.0f; break;
            case VectorModifierType::PuckerBloat: mod.params[kVecModPuckerAmount] = 0.0f; break;
            case VectorModifierType::Twist: mod.params[kVecModTwistAngle] = 0.0f; break;
            case VectorModifierType::RoundCorners: mod.params[kVecModRoundRadius] = 0.0f; break;
            case VectorModifierType::ZigZag: mod.params[kVecModZigSize] = 0.0f; break;
            case VectorModifierType::WigglePaths: mod.params[kVecModRoughSize] = 0.0f; break;
            case VectorModifierType::WiggleTransform: mod.params[kVecModWigPosX] = mod.params[kVecModWigPosY] = 0.0f; break;
            default: break;
        }
        mod.enabled = item_on(n);
        g->modifiers.push_back(mod);
        const u32 mi = static_cast<u32>(g->modifiers.size() - 1);
        std::vector<const Node*> leafs;
        leaves(n, leafs);
        for (const Node* c : leafs) {
            const i32 pi = vector::modifier_param_from_ae_match_name(type, c->match);
            if (pi < 0) {
                if (animated(*c) || (c->hasValue && !c->value.empty()))
                    rep.skip("forma", shown_name(n) + ": " + shown_name(*c), "parâmetro do operador sem equivalente no Aurea");
                continue;
            }
            const u32 p = static_cast<u32>(pi);
            // Menus do AE começam em 1 (junta: 1 ponta, 2 redonda, 3 chanfro;
            // pontos: 1 canto, 2 suave) → índices do Aurea a partir de 0.
            const bool menu = (type == VectorModifierType::OffsetPaths && p == kVecModOffsetJoin) ||
                              ((type == VectorModifierType::WigglePaths || type == VectorModifierType::Roughen) && p == kVecModRoughPoints) ||
                              (type == VectorModifierType::ZigZag && p == kVecModZigPoints);
            const bool pair = (type == VectorModifierType::Twist && p == kVecModTwistCenterX) ||
                              (type == VectorModifierType::WiggleTransform &&
                               (p == kVecModWigAnchorX || p == kVecModWigPosX || p == kVecModWigScaleX));
            const f64 mul = type == VectorModifierType::WiggleTransform && p == kVecModWigScaleX ? pct_mul(*c) : 1.0;
            if (pair) {
                vec_apply(*c, {vch(gi, vector_modifier_param(mi, p), 0, mul), vch(gi, vector_modifier_param(mi, p + 1), 1, mul)}, 2);
            } else {
                Channel ch = vch(gi, vector_modifier_param(mi, p), 0, 1.0, menu ? -1.0 : 0.0);
                if (menu) ch.conv = Conv::Round;
                vec_apply(*c, {ch});
            }
        }
        if (VectorGroup* back = vgroup(gi); back && mi < back->modifiers.size())
            for (u32 k = 0; k < kVecModParams; ++k)
                back->modifiers[mi].params[k] = vector::clamp_modifier_param(type, k, back->modifiers[mi].params[k]);
    }

    /// Transform do grupo do AE composto com o de fora (`ctx`): a matriz do
    /// grupo do Aurea (posição · giro · escala · −âncora) = fora ∘ grupo.
    void shape_transform(u32 gi, const Node* xf, const ShapeCtx& ctx) {
        VectorGroup* g = vgroup(gi);
        if (!g) return;
        g->position = ctx.offset;
        g->anchor = Vec2{0.0f, 0.0f};
        g->scale = Vec2{100.0f * ctx.scale, 100.0f * ctx.scale};
        g->rotation = ctx.rotation;
        g->opacity = 100.0f * std::clamp(ctx.opacity, 0.0f, 1.0f);
        if (!xf) return;
        const f32 cr = std::cos(ctx.rotation * 0.017453292f), sr = std::sin(ctx.rotation * 0.017453292f);
        for (const Node& c : xf->children) {
            if (c.kind != Node::Kind::Property) continue;
            const std::string& m = c.match;
            if (m == "ADBE Vector Anchor") {
                vec_static_note(c, "âncora do grupo");
                if (VectorGroup* gg = vgroup(gi)) gg->anchor = first2(c, Vec2{0.0f, 0.0f});
            } else if (m == "ADBE Vector Position") {
                if (std::fabs(ctx.rotation) < 1e-6f) {
                    vec_apply(c, {vch(gi, kVecPosX, 0, ctx.scale, ctx.offset.x), vch(gi, kVecPosY, 1, ctx.scale, ctx.offset.y)}, 2);
                } else {
                    // Grupo de fora girado: X e Y do AE se misturam — fica o valor parado.
                    vec_static_note(c, "posição de grupo dentro de grupo girado");
                    const Vec2 p = first2(c, Vec2{0.0f, 0.0f});
                    if (VectorGroup* gg = vgroup(gi)) gg->position = ctx.offset + Vec2{cr * p.x - sr * p.y, sr * p.x + cr * p.y} * ctx.scale;
                }
            } else if (m == "ADBE Vector Scale") {
                const f64 mul = pct_mul(c);
                const f32 sx = static_cast<f32>(first(c, 0, 100.0) * mul), sy = static_cast<f32>(first(c, 1, 100.0) * mul);
                if (VectorGroup* gg = vgroup(gi)) gg->scale = Vec2{sx * ctx.scale, sy * ctx.scale};
                if (animated(c)) {
                    // O Aurea anima a escala do grupo numa trilha (X); Y segue a proporção.
                    bool uniform = true;
                    for (const Key& k : c.keys) uniform = uniform && k.value.size() >= 2 && std::fabs(k.value[0] - k.value[1]) < 1e-6;
                    if (!uniform) rep.note("forma", shown_name(c), "escala X/Y animada do grupo: Y segue a proporção do primeiro valor");
                    vec_apply(c, {vch(gi, kVecScale, 0, mul * ctx.scale, 0.0, -10000.0, 10000.0)}, 2);
                }
            } else if (m == "ADBE Vector Rotation") {
                vec_apply(c, {vch(gi, kVecRotation, 0, 1.0, ctx.rotation)});
            } else if (m == "ADBE Vector Group Opacity") {
                vec_apply(c, {vch(gi, kVecOpacity, 0, pct_mul(c) * ctx.opacity, 0.0, 0.0, 100.0)});
            } else if (m == "ADBE Vector Skew") {
                // Lote X: a inclinação do grupo existe (valor parado; a animação não).
                vec_static_note(c, "inclinação do grupo");
                if (VectorGroup* gg = vgroup(gi)) gg->skew = static_cast<f32>(std::clamp(first(c, 0, 0.0), -85.0, 85.0));
            } else if (m == "ADBE Vector Skew Axis") {
                vec_static_note(c, "eixo da inclinação do grupo");
                if (VectorGroup* gg = vgroup(gi)) gg->skewAxis = static_cast<f32>(first(c, 0, 0.0));
            }
        }
    }

    /// O transform do grupo vira o "de fora" dos subgrupos (valores parados;
    /// escala X ≠ Y: vale a média).
    ShapeCtx shape_compose(const ShapeCtx& ctx, const Node* xf, const std::string& name) {
        ShapeCtx out = ctx;
        out.inherited.clear();
        out.depth = ctx.depth + 1;
        if (!xf) return out;
        Vec2 p{0, 0}, a{0, 0};
        f32 s = 1.0f, r = 0.0f, o = 1.0f;
        bool moving = false, uneven = false;
        for (const Node& c : xf->children) {
            if (c.kind != Node::Kind::Property) continue;
            const std::string& m = c.match;
            if (m == "ADBE Vector Anchor") { a = first2(c, a); moving |= animated(c); }
            else if (m == "ADBE Vector Position") { p = first2(c, p); moving |= animated(c); }
            else if (m == "ADBE Vector Scale") {
                const f64 mul = pct_mul(c) * 0.01;
                const f64 sx = first(c, 0, 100.0) * mul, sy = first(c, 1, 100.0) * mul;
                uneven = std::fabs(sx - sy) > 1e-6;
                s = static_cast<f32>(0.5 * (sx + sy));
                moving |= animated(c);
            } else if (m == "ADBE Vector Rotation") { r = static_cast<f32>(first(c, 0, 0.0)); moving |= animated(c); }
            else if (m == "ADBE Vector Group Opacity") { o = static_cast<f32>(first(c, 0, 100.0) * pct_mul(c) * 0.01); moving |= animated(c); }
        }
        out.moving = moving;
        out.uneven = uneven;
        const f32 cr = std::cos(r * 0.017453292f), sr = std::sin(r * 0.017453292f);
        const Vec2 local = p - Vec2{cr * a.x - sr * a.y, sr * a.x + cr * a.y} * s;   // G(0)
        const f32 oc = std::cos(ctx.rotation * 0.017453292f), os = std::sin(ctx.rotation * 0.017453292f);
        out.offset = ctx.offset + Vec2{oc * local.x - os * local.y, os * local.x + oc * local.y} * ctx.scale;
        out.scale = ctx.scale * s;
        out.rotation = ctx.rotation + r;
        out.opacity = ctx.opacity * std::clamp(o, 0.0f, 1.0f);
        (void)name;
        return out;
    }

    /// Um grupo do AE (ou a raiz do conteúdo): `items` na ordem do AE.
    void shape_items(const std::vector<const Node*>& items, const ShapeCtx& ctx, const Node* xf, const std::string& name) {
        if (ctx.depth > 32) return;
        i32 firstPath = -1;
        for (usize i = 0; i < items.size(); ++i)
            if (is_path_item(items[i]->match)) { firstPath = static_cast<i32>(i); break; }
        const ShapeCtx inner = shape_compose(ctx, xf, name);
        bool subgroups = false;
        for (i32 i = static_cast<i32>(items.size()) - 1; i >= 0; --i) {
            const Node& it = *items[static_cast<usize>(i)];
            if (it.match == "ADBE Vector Group") {
                subgroups = true;
                ShapeCtx child = inner;
                for (usize k = static_cast<usize>(i) + 1; k < items.size(); ++k)
                    if (is_paint_or_op(items[k]->match)) child.inherited.push_back(items[k]);
                child.inherited.insert(child.inherited.end(), ctx.inherited.begin(), ctx.inherited.end());
                std::vector<const Node*> sub;
                if (const Node* contents = it.child("ADBE Vectors Group"))
                    for (const Node& c : contents->children) if (item_on(c)) sub.push_back(&c);
                shape_items(sub, child, it.child("ADBE Vector Transform Group"), it.name.empty() ? std::string("Grupo") : it.name);
            } else if (i == firstPath) {
                std::vector<const Node*> ops;
                for (usize k = static_cast<usize>(firstPath) + 1; k < items.size(); ++k)
                    if (is_paint_or_op(items[k]->match)) ops.push_back(items[k]);
                ops.insert(ops.end(), ctx.inherited.begin(), ctx.inherited.end());
                shape_emit(items, ops, ctx, xf, name);
            } else if (!is_path_item(it.match) && !is_paint_or_op(it.match)) {
                rep.skip("forma", shown_name(it), "item de camada de forma sem equivalente no Aurea");
            }
        }
        if (subgroups && inner.moving) rep.note("forma", name, "grupo com subgrupos: o transform animado dele entrou parado nos subgrupos (o Aurea não aninha grupos)");
        if (subgroups && inner.uneven) rep.note("forma", name, "grupo com subgrupos e escala X ≠ Y: os subgrupos usam a média");
    }

    void shape_emit(const std::vector<const Node*>& items, const std::vector<const Node*>& ops, const ShapeCtx& ctx,
                    const Node* xf, const std::string& name) {
        refresh();
        if (!w.layer) return;
        VectorGroup g;
        g.name = name;
        g.fill.enabled = false;
        g.stroke.enabled = false;
        for (const Node* it : items) {
            if (!is_path_item(it->match)) continue;
            VectorPath p;
            if (shape_path(*it, p)) g.paths.push_back(std::move(p));
        }
        if (g.paths.empty()) return;
        w.layer->shape.vector.groups.push_back(std::move(g));
        const u32 gi = static_cast<u32>(w.layer->shape.vector.groups.size() - 1);
        ++shapeGroups;
        ++applied;
        shape_transform(gi, xf, ctx);
        bool fill = false, stroke = false, trim = false, merge = false, repeater = false;
        for (const Node* op : ops) {
            const std::string& m = op->match;
            VectorModifierType type{};
            if (m == "ADBE Vector Graphic - Fill" || m == "ADBE Vector Graphic - G-Fill") {
                if (fill) { rep.note("forma", shown_name(*op), "o Aurea tem um preenchimento por grupo: valeu o de cima"); continue; }
                fill = true;
                shape_fill(gi, *op, m == "ADBE Vector Graphic - G-Fill");
                if (VectorGroup* gg = vgroup(gi)) gg->fill.enabled = item_on(*op);
            } else if (m == "ADBE Vector Graphic - Stroke" || m == "ADBE Vector Graphic - G-Stroke") {
                if (stroke) { rep.note("forma", shown_name(*op), "o Aurea tem um contorno por grupo: valeu o de cima"); continue; }
                stroke = true;
                shape_stroke(gi, *op, m == "ADBE Vector Graphic - G-Stroke");
                if (VectorGroup* gg = vgroup(gi)) gg->stroke.enabled = item_on(*op);
            } else if (m == "ADBE Vector Filter - Trim") {
                if (trim) { rep.note("forma", shown_name(*op), "o Aurea tem um Aparar por grupo: valeu o primeiro"); continue; }
                trim = true;
                shape_trim(gi, *op);
                if (VectorGroup* gg = vgroup(gi)) gg->trim.enabled = item_on(*op);
            } else if (m == "ADBE Vector Filter - Merge") {
                if (merge) continue;
                merge = true;
                // AE: 1 mesclar, 2 somar, 3 subtrair, 4 interseção, 5 excluir interseções.
                static constexpr u8 kMerge[] = {1, 1, 1, 2, 3, 4};
                const Node* t = op->child("ADBE Vector Merge Type");
                const long v = std::clamp<long>(t ? std::lround(first(*t, 0, 1.0)) : 1, 0, 5);
                if (VectorGroup* gg = vgroup(gi)) gg->merge = item_on(*op) ? kMerge[v] : 0;
            } else if (m == "ADBE Vector Filter - Repeater") {
                if (repeater) { rep.note("forma", shown_name(*op), "o Aurea tem um Repetidor por grupo: valeu o primeiro"); continue; }
                repeater = true;
                shape_repeater(gi, *op);
                if (VectorGroup* gg = vgroup(gi)) gg->repeater.enabled = item_on(*op);
            } else if (vector::modifier_from_ae_match_name(m, type)) {
                shape_modifier(gi, *op, type);
            } else {
                rep.skip("forma", shown_name(*op) + " (" + m + ")", "operador de camada de forma sem equivalente no Aurea");
            }
        }
        if (!fill && !stroke) rep.note("forma", name, "grupo sem preenchimento nem contorno (no AE ele também não aparece)");
        // Lote X: a ORDEM dos itens do AE (quem pinta por cima, o que cada operador
        // alcança). O grupo só passa para a ordem livre quando ela difere da ordem
        // de sempre do Aurea (operadores, Mesclar, Aparar, Contorno, Preencher,
        // Repetidor): o preset comum continua no caminho de sempre.
        if (VectorGroup* gg = vgroup(gi)) {
            std::vector<VectorItemKind> order(gg->paths.size(), VectorItemKind::Path);
            bool hasFill = false, hasStroke = false, hasTrim = false, hasMerge = false, hasRepeater = false;
            u32 mods = 0;
            for (const Node* op : ops) {
                const std::string& m = op->match;
                VectorModifierType type{};
                if (m == "ADBE Vector Graphic - Fill" || m == "ADBE Vector Graphic - G-Fill") {
                    if (!hasFill) order.push_back(VectorItemKind::Fill);
                    hasFill = true;
                } else if (m == "ADBE Vector Graphic - Stroke" || m == "ADBE Vector Graphic - G-Stroke") {
                    if (!hasStroke) order.push_back(VectorItemKind::Stroke);
                    hasStroke = true;
                } else if (m == "ADBE Vector Filter - Trim") {
                    if (!hasTrim) order.push_back(VectorItemKind::Trim);
                    hasTrim = true;
                } else if (m == "ADBE Vector Filter - Merge") {
                    if (!hasMerge) order.push_back(VectorItemKind::Merge);
                    hasMerge = true;
                } else if (m == "ADBE Vector Filter - Repeater") {
                    if (!hasRepeater) order.push_back(VectorItemKind::Repeater);
                    hasRepeater = true;
                } else if (vector::modifier_from_ae_match_name(m, type) && mods < gg->modifiers.size()) {
                    order.push_back(VectorItemKind::Modifier);
                    ++mods;
                }
            }
            std::vector<VectorItemKind> usual(gg->paths.size(), VectorItemKind::Path);
            usual.insert(usual.end(), mods, VectorItemKind::Modifier);
            if (hasMerge) usual.push_back(VectorItemKind::Merge);
            if (hasTrim) usual.push_back(VectorItemKind::Trim);
            if (hasStroke) usual.push_back(VectorItemKind::Stroke);
            if (hasFill) usual.push_back(VectorItemKind::Fill);
            if (hasRepeater) usual.push_back(VectorItemKind::Repeater);
            if (order != usual) {
                gg->freeOrder = true;
                gg->order = std::move(order);
            }
        }
    }

    void shapes(const Entry& e) {
        Layer* l = w.layer;
        if (!l || l->kind != LayerKind::Shape || l->shape.shapeType != kShapeVector) {
            if (!shapeSkipped)
                rep.skip("camada", "Contents",
                         "o preset traz conteúdo de camada de forma: escolha uma camada vetorial (ou nenhuma camada, para o Aurea criar uma)");
            shapeSkipped = true;
            return;
        }
        if (e.path.size() > 2) {
            rep.skip("forma", e.name.empty() ? std::string(e.path.back().match) : e.name,
                     "o preset ajusta um item de forma que já existe na camada (não suportado)");
            return;
        }
        std::vector<const Node*> items;
        if (e.path.size() == 1) {
            for (const Node& c : e.value.children) if (item_on(c)) items.push_back(&c);
        } else if (item_on(e.value)) {
            items.push_back(&e.value);
        }
        // Origem do conteúdo do AE = a âncora da camada (no AE a camada de
        // forma tem âncora 0 no meio da composição; no Aurea a camada vetorial
        // tem a âncora no centro do quadro dela).
        ShapeCtx root;
        root.offset = Vec2{l->transform.anchor.x, l->transform.anchor.y};
        // Caminhos soltos na raiz (sem grupo) viram um grupo implícito.
        shape_items(items, root, nullptr, e.name.empty() ? std::string("Forma") : e.name);
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
            shapes(e);
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
        for (const u32 fx : proEffects)
            for (const Node* n : proMore) { refresh(); pro_apply(fx, 3, *n, false); }
        refresh();
        l = w.layer;
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

bool apply_ffx(const FfxDocument& doc, Host& host, LayerId target, ImportReport& report, bool textAnimatorPro) {
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
    a.pro = textAnimatorPro;
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

namespace {
void project_key_times(Node& property, f64 origin, f64 stretch) {
    for (auto& key : property.keys) {
        key.time = origin + key.time * stretch;
        for (auto& ease : key.inEase) ease.speed /= std::abs(stretch);
        for (auto& ease : key.outEase) ease.speed /= std::abs(stretch);
        if (stretch < 0) {
            std::swap(key.in, key.out);
            std::swap(key.inEase, key.outEase);
            std::swap(key.inTangent, key.outTangent);
        }
    }
    if (stretch < 0) std::reverse(property.keys.begin(), property.keys.end());
    for (auto& child : property.children) project_key_times(child, origin, stretch);
}

Interpolation project_interpolation(const Key& key) {
    switch (key.out) {
        case KeyInterp::Hold: return Interpolation::Hold;
        case KeyInterp::Bezier: return Interpolation::EaseInOut;
        default: return Interpolation::Linear;
    }
}
} // namespace

void apply_project_properties(ProjectLayer& source, Host& host, LayerId target, ImportReport& report) {
    auto* comp = host.composition();
    auto* layer = comp->layer(target);
    Applier a(host, target, report);
    a.comp = comp;
    a.w.layer = layer;
    a.w.fps = comp->fps();
    a.w.report = &report;
    a.w.registry = &host.registry();
    a.w.compTime = true;
    layer_size(*layer, comp->width(), comp->height(), a.layerW, a.layerH);
    const f64 origin = source.start + (source.stretch < 0 ? source.stretch / 3000.0 : 0);
    for (auto& group : source.properties.children) {
        const auto& m = group.match;
        if (m == "ADBE Time Remapping") {
            if (group.keys.empty()) continue;
            project_key_times(group, origin, source.stretch);
            for (const auto& key : group.keys) {
                if (key.value.empty()) continue;
                const f32 frame = static_cast<f32>(std::clamp(key.value[0] * a.w.fps, -1e9, 1e9));
                (void)layer->timeRemap.set(FrameIndex{a.w.local_frame(key.time)}, frame, project_interpolation(key));
                ++report.keyframes;
            }
            if (!layer->timeRemap.keys.empty()) {
                layer->timeRemapEnabled = true;
                report.note("propriedade", "Time Remap", "curva de tempo editável; as alças temporais personalizadas precisam de revisão");
            }
            continue;
        }
        if (m == "ADBE Audio Group") {
            const Node* levels = group.child("ADBE Audio Levels");
            if (levels && levels->hasValue && !levels->value.empty()) {
                const f64 left = levels->value[0];
                const f64 right = levels->value.size() > 1 ? levels->value[1] : left;
                layer->gain = static_cast<f32>(std::pow(10.0, std::clamp((left + right) / 2, -96.0, 24.0) / 20));
                if (!levels->keys.empty() || left != right)
                    report.note("propriedade", "Audio Levels", "ganho inicial importado; automação e canais separados precisam de revisão");
            }
            continue;
        }
        if (m != "ADBE Transform Group" && m != "ADBE Effect Parade" && m != "ADBE Text Properties" &&
            m != "ADBE Mask Parade" && m != "ADBE Root Vectors Group") continue;
        if (m == "ADBE Effect Parade" && (source.flagsC & 4) == 0) continue;
        project_key_times(group, origin, source.stretch);
        Entry entry;
        entry.path.push_back(PathStep{m});
        entry.name = group.name;
        entry.value = std::move(group);
        a.refresh();
        a.entry(entry);
    }
    a.refresh();
    a.finish();
    report.changes += a.applied + a.w.writes;
}

} // namespace aurea::ae
