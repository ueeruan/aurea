// =============================================================================
//  Omega 0.3.2, área 07 (efeitos), P0-1.
//
//  1. Apresentação do parâmetro: o controle da linha (slider, régua, ponto,
//     XYZ, orientação, disco de matiz), o passo da régua, a origem do slider,
//     o estilo da escolha e a habilitação cruzada — regra única do motor, que
//     chega pronta na linha da ponte (`EffectParamRow`, agora 160 bytes).
//  2. Duplicar efeito e colar configurações (menu ⋮ do efeito).
// =============================================================================
#include "TestFramework.hpp"

#include "aurea/Engine.hpp"
#include "aurea/bridge/BridgePods.hpp"
#include "aurea/command/Command.hpp"
#include "aurea/effects/EffectRegistry.hpp"
#include "aurea/effects/Parameter.hpp"
#include "aurea/project/Project.hpp"
#include "aurea/timeline/Layer.hpp"

#include <cmath>
#include <vector>

using namespace aurea;

namespace {

struct FxRig {
    Engine e;
    u64 layer = 0;
    bool ok = false;

    FxRig() {
        EngineConfig ec;
        ec.workerCount = 1;
        ec.disableAutosave = true;
        if (!e.initialize(ec).ok() || !e.new_project(320, 180, 30.0, nullptr).ok()) return;
        std::vector<u8> px(32 * 32 * 4, 255);
        const Result<u64> id = e.import_image(px.data(), 32, 32, "quadro.png", nullptr);
        if (!id.ok()) return;
        layer = *id;
        ok = true;
    }
    ~FxRig() { e.shutdown(); }

    Layer* L() {
        Composition* comp = e.project()->timeline().composition(e.project()->timeline().current());
        return comp ? comp->layer(LayerId::unpack(layer)) : nullptr;
    }
    /// Acrescenta um efeito e devolve o id dele (kInvalidIndex se falhou).
    u32 add(const char* key) {
        Command add;
        add.type = CommandType::EffectAdd;
        add.effect_add.layer = LayerId::unpack(layer);
        add.effect_add.effectType = effect_type_id(key);
        if (!e.apply_command(add).ok() || !L() || L()->effects.empty()) return kInvalidIndex;
        return L()->effects.back().id;
    }
    void set_param(u32 effect, u32 index, f32 value) {
        Command c;
        c.type = CommandType::EffectSetParam;
        c.effect_param.layer = LayerId::unpack(layer);
        c.effect_param.effect = EffectId{effect, 0};
        c.effect_param.paramIndex = index;
        c.effect_param.value = value;
        AUREA_CHECK(e.apply_command(c).ok());
    }
    void undo() {
        Command u;
        u.type = CommandType::Undo;
        AUREA_CHECK(e.apply_command(u).ok());
    }
    u32 query(u32 effect, std::vector<bridge::EffectParamRow>& rows, std::vector<char>& blob) {
        rows.assign(64, {});
        blob.assign(32 * 1024, 0);
        return e.query_effect_params(layer, effect, rows.data(), static_cast<u32>(rows.size()), blob.data(),
                                     static_cast<u32>(blob.size()));
    }
    const EffectInstance* fx(u32 id) { return L() ? L()->find_effect(EffectId{id, 0}) : nullptr; }
    u32 keys_of(u32 effect, u32 param) {
        const Track* t = L()->tracks.find(TrackProperty::EffectParam, effect, param_track_key(param, 0));
        return t ? static_cast<u32>(t->keys.size()) : 0u;
    }
};

const char* const kShort3[] = {"Baixo", "Médio", "Alto"};
const char* const kFive[] = {"A", "B", "C", "D", "E"};
const char* const kLong2[] = {"Curto", "Comprido demais"};

} // namespace

// O controle, o passo, a origem e o estilo: declarado vence; senão a regra.
AUREA_TEST(OmegaEffects, ResolveControlStepOriginAndEnumStyle) {
    ParameterRegistry p;
    const u32 unit = p.add_float("unit", "U", 0.5f, 0.0f, 1.0f);
    const u32 bipolar = p.add_float("bi", "B", 0.0f, -1.0f, 1.0f);
    const u32 radius = p.add_float("r", "R", 10.0f, 0.0f, 500.0f);
    p.typed_range(0.0f, 3000.0f);   // digitado > 4× o slider: régua
    const u32 count = p.add_int("n", "N", 3, 1, 10);
    const u32 ang = p.add_angle("ang", "Ângulo", 0.0f);
    const u32 pt = p.add_point2("pt", "Ponto", Vec2{0.5f, 0.5f}, 0.0f, 1.0f, kParamAnimatable | kParamRelative);
    const u32 xyz = p.add_point3("xyz", "XYZ", Vec3{0, 0, 0}, -100.0f, 100.0f);
    const u32 flag = p.add_bool("flag", "Flag", true);
    const u32 e3 = p.add_enum("e3", "E3", kShort3, 3, 0);
    const u32 e5 = p.add_enum("e5", "E5", kFive, 5, 0);
    const u32 eLong = p.add_enum("el", "EL", kLong2, 2, 0);

    AUREA_CHECK(resolve_control(p.at(unit)) == ParamControl::Slider);
    AUREA_CHECK(resolve_control(p.at(bipolar)) == ParamControl::Slider);
    AUREA_CHECK(resolve_control(p.at(radius)) == ParamControl::Ruler);
    AUREA_CHECK(resolve_control(p.at(count)) == ParamControl::Slider);
    AUREA_CHECK(resolve_control(p.at(ang)) == ParamControl::Ruler);
    AUREA_CHECK(resolve_control(p.at(pt)) == ParamControl::Point);
    AUREA_CHECK(resolve_control(p.at(xyz)) == ParamControl::Xyz);
    AUREA_CHECK(resolve_control(p.at(flag)) == ParamControl::Auto);
    AUREA_CHECK(resolve_control(p.at(e3)) == ParamControl::Auto);

    // Passo: Int 1, ângulo 1°, Float (max−min)/200 em 1/2/5·10ⁿ.
    AUREA_CHECK_NEAR(resolve_step(p.at(unit)), 0.005f, 1e-7f);
    AUREA_CHECK_NEAR(resolve_step(p.at(bipolar)), 0.01f, 1e-7f);
    AUREA_CHECK_NEAR(resolve_step(p.at(radius)), 2.0f, 1e-6f);    // 2,5 → 2
    AUREA_CHECK_NEAR(resolve_step(p.at(count)), 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(resolve_step(p.at(ang)), 1.0f, 1e-6f);
    AUREA_CHECK_NEAR(resolve_step(p.at(pt)), 0.005f, 1e-7f);
    AUREA_CHECK_NEAR(resolve_step(p.at(xyz)), 1.0f, 1e-6f);        // 200/200

    // Origem: mínimo, ou zero quando a faixa cruza o zero.
    AUREA_CHECK_NEAR(resolve_origin(p.at(unit)), 0.0f, 1e-7f);
    AUREA_CHECK_NEAR(resolve_origin(p.at(bipolar)), 0.0f, 1e-7f);
    AUREA_CHECK_NEAR(resolve_origin(p.at(count)), 1.0f, 1e-7f);

    // Estilo: até 4 opções curtas = segmentado ("Médio" tem 5 letras, não 6 bytes).
    AUREA_CHECK(resolve_enum_style(p.at(e3)) == EnumStyle::Segmented);
    AUREA_CHECK(resolve_enum_style(p.at(e5)) == EnumStyle::Dropdown);
    AUREA_CHECK(resolve_enum_style(p.at(eLong)) == EnumStyle::Dropdown);
    AUREA_CHECK(resolve_enum_style(p.at(unit)) == EnumStyle::Auto);

    // Declarado vence; combinação sem sentido é recusada sem mudar nada.
    AUREA_CHECK_EQ(p.set_control(unit, ParamControl::Ruler), unit);
    AUREA_CHECK(resolve_control(p.at(unit)) == ParamControl::Ruler);
    p.set_control(unit, ParamControl::Point);
    AUREA_CHECK(resolve_control(p.at(unit)) == ParamControl::Ruler);
    p.set_control(pt, ParamControl::HueDisc);
    AUREA_CHECK(resolve_control(p.at(pt)) == ParamControl::HueDisc);
    p.set_control(xyz, ParamControl::Orientation);
    AUREA_CHECK(resolve_control(p.at(xyz)) == ParamControl::Orientation);
    p.set_step(radius, 0.5f);
    AUREA_CHECK_NEAR(resolve_step(p.at(radius)), 0.5f, 1e-7f);
    p.set_step(radius, -3.0f);   // inválido: volta à regra
    AUREA_CHECK_NEAR(resolve_step(p.at(radius)), 2.0f, 1e-6f);
    p.set_origin(unit, 0.5f);
    AUREA_CHECK_NEAR(resolve_origin(p.at(unit)), 0.5f, 1e-7f);
    p.set_enum_style(e3, EnumStyle::Dropdown);
    AUREA_CHECK(resolve_enum_style(p.at(e3)) == EnumStyle::Dropdown);
    AUREA_CHECK_EQ(p.set_control(999u, ParamControl::Slider), kInvalidIndex);
}

// Habilitação cruzada: Bool, Enum, interruptor de seção e recursão (com ciclo).
AUREA_TEST(OmegaEffects, ParamEnabledFollowsBoolEnumSectionAndRecursion) {
    ParameterRegistry p;
    const u32 on = p.add_bool("on", "Ligar", true);
    const u32 mode = p.add_enum("mode", "Modo", kShort3, 3, 0);
    const u32 a = p.add_float("a", "A", 0, 0, 1);
    const u32 b = p.add_float("b", "B", 0, 0, 1);
    const u32 c = p.add_float("c", "C", 0, 0, 1);
    const u32 inner = p.add_bool("inner", "Interno", true);
    const u32 d = p.add_float("d", "D", 0, 0, 1);
    const u32 toggle = p.add_bool("sec_on", "Seção", false);
    const u32 m1 = p.add_float("m1", "M1", 0, 0, 1);
    const u32 x = p.add_bool("x", "X", true);
    const u32 y = p.add_bool("y", "Y", true);

    p.set_enabled_by(a, on, 1u);            // a: com "Ligar" ligado
    p.set_enabled_by(b, mode, 0b110u);      // b: modo 1 ou 2
    p.set_enabled_by(c, a, 1u);             // controlador Float: recusado
    p.set_enabled_by(inner, on, 1u);        // cadeia: d depende de inner, que depende de on
    p.set_enabled_by(d, inner, 1u);
    AUREA_CHECK(p.add_section("sec", "Seção", {m1}, toggle) != kInvalidIndex);
    p.set_enabled_by(x, y, 1u);             // ciclo x ↔ y
    p.set_enabled_by(y, x, 1u);
    AUREA_CHECK_EQ(p.at(c).enabledBy, kInvalidIndex);

    EffectInstance fx;
    fx.id = 0;
    initialize_instance(fx, p);
    const FrameIndex t{0};
    AUREA_CHECK(param_enabled(fx, p, a, nullptr, t));
    AUREA_CHECK(!param_enabled(fx, p, b, nullptr, t));   // modo 0
    AUREA_CHECK(param_enabled(fx, p, c, nullptr, t));
    AUREA_CHECK(param_enabled(fx, p, d, nullptr, t));
    AUREA_CHECK(!param_enabled(fx, p, m1, nullptr, t));  // seção desligada
    AUREA_CHECK(param_enabled(fx, p, toggle, nullptr, t));
    AUREA_CHECK(!param_enabled(fx, p, x, nullptr, t));   // ciclo: desliga, não trava
    AUREA_CHECK(!param_enabled(fx, p, 999u, nullptr, t));

    fx.params[on].constant = ParamValue::boolean(false);
    fx.params[mode].constant = ParamValue::scalar(2.0f);
    fx.params[toggle].constant = ParamValue::boolean(true);
    AUREA_CHECK(!param_enabled(fx, p, a, nullptr, t));
    AUREA_CHECK(param_enabled(fx, p, b, nullptr, t));
    AUREA_CHECK(!param_enabled(fx, p, d, nullptr, t));   // recursivo: inner desligado
    AUREA_CHECK(param_enabled(fx, p, m1, nullptr, t));

    // Máscara 2 = habilitado com o Bool DESLIGADO.
    ParameterRegistry q;
    const u32 sw = q.add_bool("sw", "S", true);
    const u32 v = q.add_float("v", "V", 0, 0, 1);
    q.set_enabled_by(v, sw, 2u);
    EffectInstance g;
    initialize_instance(g, q);
    AUREA_CHECK(!param_enabled(g, q, v, nullptr, t));
    g.params[sw].constant = ParamValue::boolean(false);
    AUREA_CHECK(param_enabled(g, q, v, nullptr, t));
}

// Seção "edite primeiro": declarada recolhida, nasce aberta na instância nova.
AUREA_TEST(OmegaEffects, EditFirstSectionStartsOpenOnANewInstance) {
    ParameterRegistry p;
    const u32 a = p.add_float("a", "A", 0, 0, 1);
    const u32 b = p.add_float("b", "B", 0, 0, 1);
    AUREA_CHECK_EQ(p.add_section("um", "Um", {a}, kInvalidIndex, false), 1u);
    AUREA_CHECK_EQ(p.add_section("dois", "Dois", {b}, kInvalidIndex, false), 2u);
    AUREA_CHECK_EQ(p.set_section_edit_first(1), 1u);
    AUREA_CHECK_EQ(p.set_section_edit_first(9), kInvalidIndex);
    EffectInstance fx;
    initialize_instance(fx, p);
    AUREA_CHECK(section_open(*p.section(1), fx.sectionsFlipped, 1));
    AUREA_CHECK(!section_open(*p.section(2), fx.sectionsFlipped, 2));
    AUREA_CHECK(effect_at_defaults(fx, p, nullptr));
    fx.params[a].constant = ParamValue::scalar(0.5f);
    AUREA_CHECK(!effect_at_defaults(fx, p, nullptr));
}

// A linha da ponte tem 160 bytes e traz a apresentação resolvida; o
// interruptor da seção desabilita os membros.
AUREA_TEST(OmegaEffects, RowIs160BytesAndCarriesPresentation) {
    AUREA_CHECK_EQ(sizeof(bridge::EffectParamRow), static_cast<usize>(160));
    FxRig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u32 saber = r.add(effect_keys::kSaber);
    AUREA_CHECK(saber != kInvalidIndex);
    if (saber == kInvalidIndex) return;
    std::vector<bridge::EffectParamRow> rows;
    std::vector<char> blob;
    const u32 n = r.query(saber, rows, blob);
    AUREA_CHECK(n >= 33);
    if (n < 33) return;
    // Saber: 0 = "Ativar brilho" (Bool, ligado) liga a seção 1; 1 é membro.
    AUREA_CHECK_EQ(rows[1].sectionToggle, 0);
    AUREA_CHECK((rows[0].rowFlags & bridge::kEffectRowEnabled) != 0);
    AUREA_CHECK((rows[1].rowFlags & bridge::kEffectRowEnabled) != 0);
    // Toda linha numérica chega com controle e passo; Bool não tem régua.
    for (u32 i = 0; i < n; ++i) {
        const ParamType type = static_cast<ParamType>(rows[i].type);
        if (type == ParamType::Float || type == ParamType::Int || type == ParamType::Angle ||
            type == ParamType::Point2D || type == ParamType::Point3D) {
            AUREA_CHECK(rows[i].control != 0);
            AUREA_CHECK(rows[i].step > 0.0f);
        }
        if (type == ParamType::Bool) AUREA_CHECK_EQ(rows[i].control, 0u);
        if (type == ParamType::Enum) AUREA_CHECK(rows[i].enumStyle != 0);
    }
    r.set_param(saber, 0, 0.0f);
    AUREA_CHECK(r.query(saber, rows, blob) >= 33);
    AUREA_CHECK((rows[0].rowFlags & bridge::kEffectRowEnabled) != 0);   // o interruptor continua editável
    AUREA_CHECK((rows[1].rowFlags & bridge::kEffectRowEnabled) == 0);
    // A ficha do tipo (sem instância) traz o mesmo controle, com os padrões.
    std::vector<bridge::EffectParamRow> spec(64);
    std::vector<char> specBlob(32 * 1024);
    const u32 m = r.e.query_effect_specs(effect_type_id(effect_keys::kSaber), spec.data(), 64, specBlob.data(),
                                         static_cast<u32>(specBlob.size()));
    AUREA_CHECK_EQ(m, n);
    for (u32 i = 0; i < m && i < n; ++i) {
        AUREA_CHECK_EQ(spec[i].control, rows[i].control);
        AUREA_CHECK_EQ(spec[i].enumStyle, rows[i].enumStyle);
    }
    AUREA_CHECK((spec[1].rowFlags & bridge::kEffectRowEnabled) != 0);   // padrão: brilho ligado
}

// Duplicar: a cópia vai para o FIM com parâmetros e keyframes; um desfazer.
AUREA_TEST(OmegaEffects, DuplicatePutsTheCopyAtTheEndWithItsTracks) {
    FxRig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u32 a = r.add(effect_keys::kGaussianBlur);
    const u32 b = r.add(effect_keys::kSaber);
    AUREA_CHECK(a != kInvalidIndex && b != kInvalidIndex);
    if (a == kInvalidIndex || b == kInvalidIndex) return;
    Layer* l = r.L();
    l->find_effect(EffectId{a, 0})->params[0].constant = ParamValue::scalar(7.0f);
    Track& tr = l->tracks.get_or_create(TrackProperty::EffectParam, a, param_track_key(0, 0));
    tr.set(FrameIndex{0}, 2.0f);
    tr.set(FrameIndex{20}, 9.0f);

    const u32 copy = r.e.duplicate_effect(r.layer, a);
    AUREA_CHECK(copy != kInvalidIndex && copy != a && copy != b);
    if (copy == kInvalidIndex) return;
    l = r.L();
    AUREA_CHECK_EQ(l->effects.size(), static_cast<usize>(3));
    AUREA_CHECK_EQ(l->effects.back().id, copy);   // no fim, depois de b
    AUREA_CHECK_EQ(l->effects[1].id, b);
    AUREA_CHECK(l->effects.back().type == effect_type_id(effect_keys::kGaussianBlur));
    AUREA_CHECK_NEAR(r.fx(copy)->params[0].constant.v[0], 7.0f, 1e-6f);
    AUREA_CHECK_EQ(r.keys_of(copy, 0), 2u);
    AUREA_CHECK_EQ(r.keys_of(a, 0), 2u);          // o original não muda
    AUREA_CHECK(l->nextEffectId > copy);

    r.undo();
    AUREA_CHECK_EQ(r.L()->effects.size(), static_cast<usize>(2));
    AUREA_CHECK(r.fx(copy) == nullptr);
    AUREA_CHECK_EQ(r.keys_of(copy, 0), 0u);

    // Efeito inexistente ou camada travada: nada muda.
    AUREA_CHECK_EQ(r.e.duplicate_effect(r.layer, 9999u), kInvalidIndex);
    r.L()->locked = true;
    AUREA_CHECK_EQ(r.e.duplicate_effect(r.layer, a), kInvalidIndex);
    AUREA_CHECK_EQ(r.L()->effects.size(), static_cast<usize>(2));
}

// Colar configurações: só UM efeito copiado e do MESMO tipo; troca valores e
// trilhas, mantém id e posição; um desfazer volta.
AUREA_TEST(OmegaEffects, PasteSettingsOnlyIntoTheSameType) {
    FxRig r;
    AUREA_CHECK(r.ok);
    if (!r.ok) return;
    const u32 a = r.add(effect_keys::kGaussianBlur);
    const u32 b = r.add(effect_keys::kSaber);
    const u32 c = r.add(effect_keys::kGaussianBlur);
    AUREA_CHECK(a != kInvalidIndex && b != kInvalidIndex && c != kInvalidIndex);
    if (a == kInvalidIndex || b == kInvalidIndex || c == kInvalidIndex) return;
    r.L()->find_effect(EffectId{a, 0})->params[0].constant = ParamValue::scalar(33.0f);
    Track& tr = r.L()->tracks.get_or_create(TrackProperty::EffectParam, a, param_track_key(0, 0));
    tr.set(FrameIndex{0}, 1.0f);
    tr.set(FrameIndex{10}, 4.0f);

    AUREA_CHECK_EQ(r.e.clipboard_effect_type(), 0u);
    AUREA_CHECK(!r.e.paste_effect_settings(r.layer, c));            // área de transferência vazia
    AUREA_CHECK_EQ(r.e.copy_effects(r.layer, a), 1u);
    AUREA_CHECK_EQ(r.e.clipboard_effect_type(), effect_type_id(effect_keys::kGaussianBlur));
    AUREA_CHECK(!r.e.paste_effect_settings(r.layer, b));            // tipo diferente
    AUREA_CHECK(r.e.paste_effect_settings(r.layer, c));
    AUREA_CHECK_NEAR(r.fx(c)->params[0].constant.v[0], 33.0f, 1e-6f);
    AUREA_CHECK_EQ(r.keys_of(c, 0), 2u);
    AUREA_CHECK_EQ(r.L()->effects.back().id, c);                    // mesmo id, mesma posição
    AUREA_CHECK_EQ(r.L()->effects.size(), static_cast<usize>(3));

    r.undo();
    AUREA_CHECK(std::fabs(r.fx(c)->params[0].constant.v[0] - 33.0f) > 1e-3f);
    AUREA_CHECK_EQ(r.keys_of(c, 0), 0u);

    // Vários efeitos copiados: "Colar configurações" não se aplica.
    AUREA_CHECK(r.e.copy_effects(r.layer) >= 3u);
    AUREA_CHECK_EQ(r.e.clipboard_effect_type(), 0u);
    AUREA_CHECK(!r.e.paste_effect_settings(r.layer, c));
}
